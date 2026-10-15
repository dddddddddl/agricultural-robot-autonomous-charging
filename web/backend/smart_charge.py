import json
import re
import threading
from datetime import datetime, timedelta, timezone
from statistics import median
from zoneinfo import ZoneInfo

import requests

from config import DEEPSEEK_API_KEY, DEEPSEEK_BASE_URL, DEEPSEEK_MODEL, LOCAL_TIMEZONE
from database import (
    get_active_smart_plan,
    get_config,
    get_smart_runtime,
    habit_samples,
    latest_telemetry,
    latest_vehicle_status,
    queue_command,
    save_smart_plan,
    update_smart_runtime,
)

TIME_PATTERN = re.compile(r"^([01]\d|2[0-3]):[0-5]\d$")
_scheduler_stop = threading.Event()
_scheduler_thread = None


def _local_datetime(value):
    parsed = datetime.fromisoformat(value)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(ZoneInfo(LOCAL_TIMEZONE))


def _median_time(values, fallback):
    if not values:
        return fallback
    minutes = []
    for value in values:
        local = _local_datetime(value)
        minutes.append(local.hour * 60 + local.minute)
    if max(minutes) - min(minutes) > 12 * 60:
        minutes = [value + 24 * 60 if value < 12 * 60 else value for value in minutes]
    midpoint = int(median(minutes))
    midpoint %= 24 * 60
    return f"{midpoint // 60:02d}:{midpoint % 60:02d}"


def build_habit_profile():
    config = get_config()
    samples = habit_samples(days=30)
    sessions = samples["sessions"]
    departures = samples["departures"]
    departure_times = [event["timestamp"] for event in departures]
    departure_times.extend(
        session["end_time"] for session in sessions if session.get("end_time")
    )
    charge_time = _median_time(
        [session["start_time"] for session in sessions if session.get("start_time")],
        config.get("default_charge_time") or "22:00",
    )
    departure_time = _median_time(
        departure_times,
        config.get("default_leave_time") or "09:00",
    )
    return {
        "sample_days": 30,
        "charge_session_count": len(sessions),
        "departure_sample_count": len(departure_times),
        "observed_charge_times": [
            _local_datetime(session["start_time"]).strftime("%H:%M")
            for session in sessions[-20:] if session.get("start_time")
        ],
        "observed_departure_times": [
            _local_datetime(value).strftime("%H:%M")
            for value in departure_times[-20:]
        ],
        "fallback_charge_time": charge_time,
        "fallback_departure_time": departure_time,
    }


def _ask_model_for_habit(profile, override=None):
    if not DEEPSEEK_API_KEY:
        return None
    system = (
        "Predict the next usual charging start and vehicle departure from the supplied 30-day history. "
        "Return valid JSON with predicted_charge_time, predicted_departure_time, reasoning_cn, confidence_cn. "
        "Both times must be HH:MM. Write reasoning and confidence in English despite the legacy _cn keys. "
        "If history is insufficient, state that limitation and use the supplied fallback times. "
        "Do not suggest relay or hardware control."
    )
    payload = dict(profile)
    if override:
        payload["user_departure_override"] = override
    response = requests.post(
        f"{DEEPSEEK_BASE_URL}/chat/completions",
        headers={"Authorization": f"Bearer {DEEPSEEK_API_KEY}", "Content-Type": "application/json"},
        json={
            "model": DEEPSEEK_MODEL,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": json.dumps(payload, ensure_ascii=False)},
            ],
            "response_format": {"type": "json_object"},
            "temperature": 0.1,
        },
        timeout=30,
    )
    response.raise_for_status()
    parsed = json.loads(response.json()["choices"][0]["message"]["content"])
    if not TIME_PATTERN.match(str(parsed.get("predicted_charge_time", ""))):
        raise ValueError("invalid predicted charge time")
    if not TIME_PATTERN.match(str(parsed.get("predicted_departure_time", ""))):
        raise ValueError("invalid predicted departure time")
    return parsed


def _next_schedule(charge_time, departure_time):
    now = datetime.now(ZoneInfo(LOCAL_TIMEZONE))
    departure_hour, departure_minute = (int(part) for part in departure_time.split(":"))
    departure = now.replace(hour=departure_hour, minute=departure_minute, second=0, microsecond=0)
    if departure <= now:
        departure += timedelta(days=1)
    charge_hour, charge_minute = (int(part) for part in charge_time.split(":"))
    charge_start = departure.replace(hour=charge_hour, minute=charge_minute)
    if charge_start >= departure:
        charge_start -= timedelta(days=1)
    return now, charge_start, departure


def generate_smart_plan(departure_override=None):
    config = get_config()
    profile = build_habit_profile()
    prediction = None
    source = "Usage rules"
    try:
        prediction = _ask_model_for_habit(profile, departure_override)
        if prediction:
            source = "Model-assisted usage prediction"
    except (requests.RequestException, ValueError, KeyError, TypeError, json.JSONDecodeError):
        prediction = None
    charge_time = (prediction or {}).get("predicted_charge_time") or profile["fallback_charge_time"]
    departure_time = departure_override or (prediction or {}).get("predicted_departure_time") or profile["fallback_departure_time"]
    now, predicted_charge_start, predicted_departure = _next_schedule(charge_time, departure_time)
    base_start = max(now, predicted_charge_start)
    target = int(config["charge_limit_percent"])
    base_limit = min(80, target)
    buffer_minutes = int(config.get("departure_buffer_minutes") or 30)
    topoff_deadline = predicted_departure - timedelta(minutes=buffer_minutes)
    max_power = float(config.get("max_charge_power_w") or 76.65)
    capacity = float(config.get("battery_capacity_wh") or 134.4)
    topoff_gap = max(0, target - base_limit)
    topoff_minutes = max(5, round(capacity * topoff_gap / 100 / max_power * 60 * 1.25))
    topoff_start = topoff_deadline - timedelta(minutes=topoff_minutes)
    if topoff_start < base_start:
        topoff_start = base_start
    reasoning = (prediction or {}).get("reasoning_cn")
    if not reasoning:
        if profile["charge_session_count"] or profile["departure_sample_count"]:
            reasoning = "Schedule based on median charging and departure times over the available 30-day history."
        else:
            reasoning = "Insufficient history; using 22:00 charging and 09:00 departure defaults until records accumulate."
    confidence = (prediction or {}).get("confidence_cn") or (
        "Medium" if profile["charge_session_count"] >= 3 and profile["departure_sample_count"] >= 3 else "Low"
    )
    plan = {
        "source": source,
        "predicted_charge_start": base_start.isoformat(timespec="minutes"),
        "predicted_departure": predicted_departure.isoformat(timespec="minutes"),
        "topoff_start": topoff_start.isoformat(timespec="minutes"),
        "topoff_deadline": topoff_deadline.isoformat(timespec="minutes"),
        "base_limit": base_limit,
        "target_limit": target,
        "reasoning_cn": reasoning,
        "confidence_cn": confidence,
        "raw": {"profile": profile, "prediction": prediction},
    }
    plan["plan_id"] = save_smart_plan(plan)
    update_smart_runtime("Schedule generated", "Waiting for vehicle charge estimate and dock status")
    return plan


def _dock_allows_start(telemetry):
    if not telemetry:
        return False
    return bool(
        not telemetry.get("emergency_stop")
        and telemetry.get("fault") in (None, "", "NONE")
        and telemetry.get("micro_switch_stable")
        and telemetry.get("temp_ok")
        and telemetry.get("voltage_ok")
        and telemetry.get("current_ok")
    )


def _queue_if_needed(command, phase, detail):
    runtime = get_smart_runtime() or {}
    last_at = runtime.get("last_command_at")
    same_recent = runtime.get("last_command") == command
    if same_recent and last_at:
        try:
            same_recent = datetime.now(timezone.utc) - datetime.fromisoformat(last_at) < timedelta(minutes=5)
        except ValueError:
            same_recent = False
    if not same_recent:
        queue_command(command)
        update_smart_runtime(phase, detail, command)
    else:
        update_smart_runtime(phase, detail)


def evaluate_smart_charge():
    config = get_config()
    plan = get_active_smart_plan()
    if not config.get("smart_charge_enabled"):
        runtime = update_smart_runtime("Disabled", "Smart charging is disabled")
        return {"enabled": False, "plan": plan, "runtime": runtime}
    if not plan:
        plan = generate_smart_plan()
    else:
        try:
            planned_departure = datetime.fromisoformat(plan["predicted_departure"])
            if datetime.now(ZoneInfo(LOCAL_TIMEZONE)) > planned_departure + timedelta(minutes=5):
                plan = generate_smart_plan()
        except (ValueError, TypeError):
            plan = generate_smart_plan()
    vehicle = latest_vehicle_status()
    if not vehicle:
        runtime = update_smart_runtime("Waiting for vehicle", "No vehicle charge estimate received")
        return {"enabled": True, "plan": plan, "runtime": runtime}
    try:
        vehicle_age = datetime.now(timezone.utc) - datetime.fromisoformat(vehicle["timestamp"])
    except ValueError:
        vehicle_age = timedelta.max
    if vehicle_age > timedelta(seconds=45):
        runtime = update_smart_runtime("Waiting for vehicle", "Vehicle charge estimate is stale")
        return {"enabled": True, "plan": plan, "runtime": runtime}

    now = datetime.now(ZoneInfo(LOCAL_TIMEZONE))
    base_start = datetime.fromisoformat(plan["predicted_charge_start"])
    departure = datetime.fromisoformat(plan["predicted_departure"])
    topoff_start = datetime.fromisoformat(plan["topoff_start"])
    soc = float(vehicle["soc_percent"])
    charging = None if vehicle["charging"] is None else bool(vehicle["charging"])
    target, base_limit = int(plan["target_limit"]), int(plan["base_limit"])

    if soc >= target:
        if charging:
            _queue_if_needed("STOP_CHARGING", "Charge complete", f"Estimated vehicle charge reached {target}%")
        else:
            update_smart_runtime("Charge complete", f"Estimated vehicle charge reached {target}%")
    elif now >= departure:
        if charging:
            _queue_if_needed("STOP_CHARGING", "Schedule finished", "Predicted departure time reached")
        else:
            update_smart_runtime("Schedule finished", "Predicted departure time reached")
    elif now < base_start:
        if charging:
            _queue_if_needed("STOP_CHARGING", "Waiting for scheduled time", "Scheduled charging time has not arrived")
        else:
            update_smart_runtime("Waiting for scheduled time", "Scheduled charging time has not arrived")
    elif now < topoff_start and soc >= base_limit:
        if charging:
            _queue_if_needed("STOP_CHARGING", "Hold charge", f"Base target {base_limit}% reached; waiting for departure top-off")
        else:
            update_smart_runtime("Hold charge", f"Base target {base_limit}% reached; waiting for departure top-off")
    else:
        phase = "Top off before departure" if now >= topoff_start else "Base charging"
        goal = target if now >= topoff_start else base_limit
        if not _dock_allows_start(latest_telemetry()):
            update_smart_runtime("Waiting for local interlocks", "Dock startup checks have not passed")
        elif not charging:
            _queue_if_needed("REQUEST_START_CHARGE", phase, f"Charging toward the {goal}% target")
        else:
            update_smart_runtime(phase, f"Charging toward the {goal}% target")
    return {"enabled": True, "plan": plan, "runtime": get_smart_runtime()}


def smart_charge_status():
    try:
        return evaluate_smart_charge()
    except Exception as exc:
        runtime = update_smart_runtime("Schedule paused", f"Schedule evaluation unavailable: {type(exc).__name__}")
        return {"enabled": bool(get_config().get("smart_charge_enabled")), "plan": get_active_smart_plan(), "runtime": runtime}


def _scheduler_loop():
    while not _scheduler_stop.wait(20):
        smart_charge_status()


def start_scheduler():
    global _scheduler_thread
    if _scheduler_thread and _scheduler_thread.is_alive():
        return
    _scheduler_stop.clear()
    _scheduler_thread = threading.Thread(target=_scheduler_loop, name="smart-charge-scheduler", daemon=True)
    _scheduler_thread.start()


def stop_scheduler():
    _scheduler_stop.set()
