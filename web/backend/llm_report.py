import json
import re
from datetime import datetime, timedelta, timezone

import requests

from config import BATTERY, DEEPSEEK_API_KEY, DEEPSEEK_BASE_URL, DEEPSEEK_MODEL
from database import (
    get_config,
    get_location,
    history,
    recent_sessions,
    vehicle_history,
)
from smart_charge import build_habit_profile
from tariff import get_tariff


def _incident_count(rows, predicate):
    count, active = 0, False
    for row in rows:
        current = bool(predicate(row))
        if current and not active:
            count += 1
        active = current
    return count


def _duration_hours(sessions):
    total = timedelta(0)
    for session in sessions:
        if not session.get("start_time") or not session.get("end_time"):
            continue
        try:
            total += datetime.fromisoformat(session["end_time"]) - datetime.fromisoformat(session["start_time"])
        except ValueError:
            continue
    return round(total.total_seconds() / 3600, 2)


def build_summary():
    now = datetime.now(timezone.utc)
    period_start = now - timedelta(days=7)
    rows = [row for row in history(limit=5000) if row["timestamp"] >= period_start.isoformat()]
    vehicle_rows = vehicle_history(limit=5000, since=period_start.isoformat())
    sessions = recent_sessions(days=7)
    active_powers = [float(row["power_w"]) for row in rows if row.get("power_w") is not None and row["power_w"] >= 5]
    temps = [float(row["temperature_c"]) for row in rows if row.get("temperature_c") is not None]
    completed = [session for session in sessions if session.get("end_time")]
    location = get_location()
    tariff = get_tariff(location)
    return {
        "period_start": period_start.isoformat(),
        "period_end": now.isoformat(),
        "battery": BATTERY,
        "summary": {
            "telemetry_points": len(rows),
            "vehicle_soc_points": len(vehicle_rows),
            "charge_sessions": len(sessions),
            "completed_sessions": len(completed),
            "charging_hours": _duration_hours(completed),
            "average_active_power_w": round(sum(active_powers) / len(active_powers), 2) if active_powers else None,
            "max_temperature_c": round(max(temps), 2) if temps else None,
            "target_reached_count": sum(
                1 for session in completed
                if session.get("end_soc") is not None and session["end_soc"] >= (session.get("target_soc") or 100)
            ),
            "high_temperature_incidents": _incident_count(rows, lambda row: row.get("temp_warning")),
            "fault_incidents": _incident_count(rows, lambda row: row.get("fault") not in (None, "", "NONE")),
            "contact_interruptions": _incident_count(rows, lambda row: not row.get("micro_switch_stable")),
        },
        "latest_vehicle_soc": vehicle_rows[-1]["soc_percent"] if vehicle_rows else None,
        "latest_vehicle_voltage_v": vehicle_rows[-1].get("voltage_v") if vehicle_rows else None,
        "habit_profile": build_habit_profile(),
        "user_config": get_config(),
        "location": location,
        "tariff": tariff,
        "data_quality": {
            "soc_source": "Vehicle /PowerVoltage estimated using the original 24 V/6S curve",
            "has_vehicle_soc": bool(vehicle_rows),
            "has_completed_sessions": bool(completed),
            "battery_soh_available": False,
            "note": "Charge is estimated from vehicle voltage; dock INA226 voltage is not used for vehicle charge estimation.",
        },
    }


def _rate_text(period):
    if not period:
        return "Current-period tariff unverified"
    rate = period.get("rate")
    rate_text = f"{rate:.4f} CNY/kWh" if isinstance(rate, (int, float)) else "Rate unverified"
    return f"{period.get('label', 'Current period')}, {rate_text}"


def _local_report(summary, source_note="Rule-based analysis"):
    stats = summary["summary"]
    tariff = summary["tariff"]
    current_period = tariff.get("current_period")
    next_window = tariff.get("next_cheapest_window")
    session_count = stats["charge_sessions"]
    if session_count:
        executive = (
            f"Recorded {session_count} charging sessions over seven days, totaling about {stats['charging_hours']} hours. "
            "Charge is estimated from vehicle ROS voltage using the original 24 V/6S curve."
        )
    else:
        executive = (
            "No complete charging sessions were recorded over seven days. Current observations use live status and default usage times. "
            "Usage predictions update as connected-vehicle records accumulate."
        )
    habit = summary["habit_profile"]
    tariff_recommendation = "The local tariff is unverified. Check the customer category before scheduling charging."
    cheapest = "Unverified"
    if tariff.get("region_match") and next_window:
        period = next_window["period"]
        cheapest = f"{period['start']}—{period['end']}"
        tariff_recommendation = (
            f"Consider the reference off-peak window {cheapest}, subject to the required departure time."
        )
    risks = []
    if stats["fault_incidents"]:
        risks.append({"level": "Attention", "title": "Charging faults", "detail": f"Recorded {stats['fault_incidents']} separate fault events; inspect their fault codes."})
    if stats["contact_interruptions"]:
        risks.append({"level": "Attention", "title": "Docking contact stability", "detail": f"Recorded {stats['contact_interruptions']} contact interruptions; inspect contact cleanliness and parking alignment."})
    if stats["high_temperature_incidents"]:
        risks.append({"level": "Elevated", "title": "Temperature events", "detail": f"Recorded {stats['high_temperature_incidents']} temperature-warning events; inspect before resuming charging."})
    if not risks:
        risks.append({"level": "Normal", "title": "No repeated event pattern detected", "detail": "The current records show no repeated temperature-warning or fault events."})
    return {
        "headline": "Weekly charging summary",
        "executive_summary": executive,
        "charging_habits": {
            "analysis": f"Typical charging start: {habit['fallback_charge_time']}; estimated departure: {habit['fallback_departure_time']}.",
            "evidence": [
                f"Charging-session samples over 30 days: {habit['charge_session_count']}",
                f"Departure-time samples over 30 days: {habit['departure_sample_count']}",
            ],
        },
        "battery_assessment": {
            "status": "Insufficient data" if not summary["data_quality"]["has_completed_sessions"] else "Operating observations available",
            "analysis": "Charging and temperature observations are available, but battery health, true capacity and lifetime cannot be inferred.",
            "evidence": [
                f"Maximum recorded temperature: {stats['max_temperature_c']} C" if stats["max_temperature_c"] is not None else "No valid temperature samples",
                (
                    f"Latest vehicle voltage: {summary['latest_vehicle_voltage_v']:.2f} V; charge estimated from the voltage curve"
                    if isinstance(summary.get("latest_vehicle_voltage_v"), (int, float))
                    else "Charge estimated from the vehicle ROS voltage curve"
                ),
            ],
        },
        "tariff_advice": {
            "location": (summary.get("location") or {}).get("display_name") or tariff.get("region", "Default region"),
            "current_period": _rate_text(current_period),
            "cheapest_window": cheapest,
            "recommendation": tariff_recommendation,
            "source_note": tariff.get("pricing_status_cn", "Tariff unverified"),
        },
        "risks": risks,
        "actions": [
            {"priority": 1, "action": "Keep vehicle voltage telemetry connected", "reason": "Completion estimates and schedules use the vehicle voltage-derived charge estimate."},
            {"priority": 2, "action": f"Use the configured daily target of {summary['user_config']['charge_limit_percent']}%", "reason": "The schedule charges to 80% before topping off for the predicted departure."},
            {"priority": 3, "action": "Check the tariff category on the electricity bill", "reason": "Time-of-use policies vary by customer category and supply contract."},
        ],
        "data_note": "Charge estimates use vehicle ROS voltage and the 24 V/6S curve, not dock voltage. Battery state-of-health data is unavailable.",
        "analysis_source": source_note,
    }


def _ask_model(summary):
    system = (
        "You analyze charging operations. Write a concise English weekly report using only the supplied data. "
        "Return valid JSON without Markdown. Structure: headline, executive_summary, "
        "charging_habits{analysis,evidence[]}, battery_assessment{status,analysis,evidence[]}, "
        "tariff_advice{location,current_period,cheapest_window,recommendation,source_note}, "
        "risks[{level,title,detail}], actions[{priority,action,reason}], data_note. "
        "Distinguish telemetry samples from charging sessions and isolated samples from incidents. "
        "Use only vehicle voltage-derived charge estimates; do not infer battery health, capacity or lifetime. "
        "Use only supplied reference tariff periods and rates; state when the tariff is unverified. "
        "Do not recommend direct relay control. Use English for all explanatory values."
    )
    response = requests.post(
        f"{DEEPSEEK_BASE_URL}/chat/completions",
        headers={"Authorization": f"Bearer {DEEPSEEK_API_KEY}", "Content-Type": "application/json"},
        json={
            "model": DEEPSEEK_MODEL,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": json.dumps(summary, ensure_ascii=False)},
            ],
            "response_format": {"type": "json_object"},
            "temperature": 0.15,
        },
        timeout=45,
    )
    response.raise_for_status()
    text = response.json()["choices"][0]["message"]["content"]
    return json.loads(text), text


def _values_contain_cjk(value):
    if isinstance(value, str):
        return bool(re.search(r"[\u3400-\u9fff]", value))
    if isinstance(value, dict):
        return any(_values_contain_cjk(item) for item in value.values())
    if isinstance(value, list):
        return any(_values_contain_cjk(item) for item in value)
    return False


def generate_report():
    summary = build_summary()
    if not DEEPSEEK_API_KEY:
        report = _local_report(summary, "Local rule-based analysis")
        return summary, report, json.dumps(report, ensure_ascii=False), "Local rule-based analysis"
    try:
        report, text = _ask_model(summary)
        if _values_contain_cjk(report):
            raise ValueError("report contains untranslated Chinese wording")
        report["analysis_source"] = "Model-assisted analysis"
        return summary, report, text, DEEPSEEK_MODEL
    except (requests.RequestException, ValueError, KeyError, TypeError, json.JSONDecodeError):
        report = _local_report(summary, "Model unavailable; local rule-based analysis used")
        return summary, report, json.dumps(report, ensure_ascii=False), "Local rule-based analysis"
