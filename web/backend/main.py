from typing import Optional

from fastapi import FastAPI, Query
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from battery import estimate_remaining_time, estimate_soc_from_vehicle_voltage
from config import PROJECT_DIR
from database import (
    consume_command,
    deactivate_smart_plans,
    get_config,
    get_location,
    history,
    init_db,
    insert_telemetry,
    latest_telemetry,
    latest_vehicle_status,
    queue_command,
    recent_sessions,
    record_vehicle_event,
    record_vehicle_status,
    save_location,
    save_report,
    update_config,
    vehicle_history,
)
from llm_report import generate_report
from location_service import reverse_geocode
from models import (
    ChargeLimitRequest,
    DockTelemetry,
    LocationRequest,
    MockStatusRequest,
    ReportRequest,
    SmartChargeRequest,
    VehicleEventRequest,
    VehicleStatusRequest,
)
from smart_charge import (
    generate_smart_plan,
    smart_charge_status,
    start_scheduler,
    stop_scheduler,
)
from tariff import get_tariff

app = FastAPI(title="Charging Control Center", version="2.0.0")
FRONTEND_DIR = PROJECT_DIR / "frontend"
app.mount("/static", StaticFiles(directory=FRONTEND_DIR), name="static")


@app.on_event("startup")
def startup():
    init_db()
    start_scheduler()


@app.on_event("shutdown")
def shutdown():
    stop_scheduler()


@app.get("/")
def index():
    return FileResponse(FRONTEND_DIR / "index.html")


@app.get("/api/health")
def health():
    return {"ok": True, "service": "smart-charging-dock", "version": "2.0.0"}


def accept_status(payload: DockTelemetry):
    row_id = insert_telemetry(payload)
    return {"ok": True, "telemetry_id": row_id, "soc_source": "vehicle_ros_power_voltage_estimated"}


@app.post("/api/dock/status")
def dock_status(payload: DockTelemetry):
    return accept_status(payload)


@app.post("/api/status")
def legacy_dock_status(payload: DockTelemetry):
    return accept_status(payload)


@app.get("/api/status")
def current_status():
    row = latest_telemetry()
    vehicle = latest_vehicle_status()
    config = get_config()
    if not row:
        row = {
            "timestamp": None, "dock_state": "NO_DATA", "fault": "NONE", "relay_on": 0,
            "power_w": None, "temperature_c": None, "emergency_stop": 0,
            "micro_switch_stable": 0, "temp_ok": 0, "voltage_ok": 0, "current_ok": 0,
        }
    vehicle_soc = vehicle.get("soc_percent") if vehicle else None
    remaining = estimate_remaining_time(
        vehicle_soc,
        config["charge_limit_percent"],
        row.get("power_w"),
        config["battery_capacity_wh"],
    )
    max_power = float(config.get("max_charge_power_w") or 76.65)
    actual_power = float(row.get("power_w") or 0)
    power_ratio = actual_power / max_power if max_power else 0
    charging = bool(row.get("relay_on"))
    return {
        "telemetry": row,
        "vehicle": vehicle,
        "remaining": remaining,
        "config": config,
        "power_delivery": {
            "max_power_w": round(max_power, 1),
            "actual_power_w": round(actual_power, 1),
            "ratio_percent": round(min(power_ratio, 1.5) * 100),
            "at_max_power": bool(charging and power_ratio >= 0.9),
            "charging": charging,
        },
        "smart_charge": smart_charge_status(),
    }


@app.post("/api/vehicle/status")
def vehicle_status(payload: VehicleStatusRequest):
    # Do not trust a browser-supplied percentage as an independent source.
    # Recompute it from the same /PowerVoltage curve used by index15.html.
    estimated_soc = estimate_soc_from_vehicle_voltage(payload.voltage_v)
    row_id = record_vehicle_status(
        estimated_soc,
        payload.charging,
        payload.source,
        payload.voltage_v,
    )
    state = smart_charge_status()
    return {
        "ok": True,
        "vehicle_telemetry_id": row_id,
        "soc_percent": estimated_soc,
        "smart_charge": state,
    }


@app.post("/api/vehicle/event")
def vehicle_event(payload: VehicleEventRequest):
    row_id = record_vehicle_event(payload.event, payload.soc_percent)
    return {"ok": True, "event_id": row_id}


@app.get("/api/history")
def get_history(
    limit: int = Query(500, ge=1, le=5000),
    from_time: Optional[str] = Query(None, alias="from"),
    to_time: Optional[str] = Query(None, alias="to"),
):
    return {"items": history(limit, from_time, to_time)}


@app.get("/api/vehicle/history")
def get_vehicle_history(limit: int = Query(500, ge=1, le=5000)):
    return {"items": vehicle_history(limit=limit)}


@app.post("/api/config/charge-limit")
def charge_limit(body: ChargeLimitRequest):
    config = update_config("charge_limit_percent", body.charge_limit_percent)
    plan = None
    if config["smart_charge_enabled"]:
        plan = generate_smart_plan()
    return {"ok": True, "config": config, "plan": plan, "message": "Charge limit saved"}


@app.post("/api/config/smart-charge")
def smart_charge(body: SmartChargeRequest):
    if body.departure_time:
        update_config("default_leave_time", body.departure_time)
    config = update_config("smart_charge_enabled", int(body.enabled))
    if body.enabled:
        plan = generate_smart_plan(body.departure_time)
        state = smart_charge_status()
        return {"ok": True, "config": config, "plan": plan, "smart_charge": state, "message": "Smart charging enabled; schedule generated"}
    deactivate_smart_plans()
    return {"ok": True, "config": config, "smart_charge": smart_charge_status(), "message": "Smart charging disabled"}


@app.post("/api/smart-charge/regenerate")
def regenerate_smart_charge(body: Optional[SmartChargeRequest] = None):
    departure = body.departure_time if body else None
    plan = generate_smart_plan(departure)
    return {"ok": True, "plan": plan, "smart_charge": smart_charge_status(), "message": "Schedule updated"}


def command_response(command, message):
    queue_command(command)
    return {"ok": True, "queued_command": command, "message": message}


@app.post("/api/command/start")
def start_command():
    return command_response("REQUEST_START_CHARGE", "Start request queued")


@app.post("/api/command/stop")
def stop_command():
    return command_response("STOP_CHARGING", "Stop request queued")


@app.post("/api/command/clear-fault")
def clear_fault_command():
    return command_response("CLEAR_FAULT", "Fault-clear request queued")


@app.get("/api/command")
@app.get("/api/command/poll")
def poll_command():
    return {"command": consume_command()}


@app.get("/api/battery/health")
def battery_health():
    rows = vehicle_history(limit=500)
    sessions = recent_sessions(days=30)
    return {
        "status_cn": "Charging observations available" if sessions else "Collecting data",
        "vehicle_soc_points": len(rows),
        "charge_sessions": len(sessions),
        "note_cn": "Battery state-of-health data is unavailable; capacity and lifetime are not inferred.",
    }


@app.post("/api/location")
def update_location(body: LocationRequest):
    location = save_location(reverse_geocode(body.latitude, body.longitude))
    return {"ok": True, "location": location, "tariff": get_tariff(location)}


@app.get("/api/tariff")
def tariff():
    return get_tariff(get_location())


@app.post("/api/report/generate")
def report(body: Optional[ReportRequest] = None):
    if body and body.latitude is not None and body.longitude is not None:
        save_location(reverse_geocode(body.latitude, body.longitude))
    summary, parsed, text, model = generate_report()
    save_report(summary, model, parsed, text)
    return {"ok": True, "report": parsed, "analysis_source": model, "message": "Weekly report generated"}


@app.post("/api/dev/mock-status")
def mock_status(body: MockStatusRequest):
    scenarios = {
        "positioned": ("READY_TO_CHARGE", False),
        "charging": ("CHARGING", True),
        "smart_paused": ("SMART_PAUSED", False),
        "fault": ("FAULT", False),
        "emergency": ("FAULT", False),
    }
    state, relay = scenarios.get(body.scenario, scenarios["charging"])
    inserted = 0
    for index in range(body.points):
        progress = index / max(1, body.points - 1)
        voltage = 22.8 + progress * 1.8
        payload = DockTelemetry(
            state=state,
            fault="EMERGENCY_STOP" if body.scenario == "emergency" else ("TOF_SENSOR_OFFLINE" if body.scenario == "fault" else "NONE"),
            relayOn=relay,
            wifiConnected=True,
            wifiIp="192.168.43.188",
            uptime_ms=100000 + index * 5000,
            tof_mm=94,
            tof_ok=True,
            micro_switch_pressed=True,
            micro_switch_stable=True,
            emergency_stop=body.scenario == "emergency",
            voltage_v=round(voltage, 3),
            current_a=2.72 if relay else 0.03,
            power_w=round(voltage * (2.72 if relay else 0.03), 2),
            ina226_online=True,
            ina226_read_ok=True,
            voltage_ok=True,
            current_ok=True,
            temperature_c=round(29 + progress * 4, 1),
            temp_online=True,
            temp_ok=True,
            temp_warning=False,
            **body.overrides,
        )
        accept_status(payload)
        record_vehicle_status(
            estimate_soc_from_vehicle_voltage(voltage),
            relay,
            voltage_v=round(voltage, 3),
        )
        inserted += 1
    return {"ok": True, "scenario": body.scenario, "inserted": inserted}
