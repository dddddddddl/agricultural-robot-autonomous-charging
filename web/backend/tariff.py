import json
from datetime import datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

import requests

from config import LOCAL_TIMEZONE, TARIFF_API_KEY, TARIFF_API_URL


def _minutes(value):
    hours, minutes = (int(part) for part in value.split(":"))
    return hours * 60 + minutes


def _contains(now_minutes, start, end):
    start_minutes, end_minutes = _minutes(start), _minutes(end)
    if start_minutes < end_minutes:
        return start_minutes <= now_minutes < end_minutes
    return now_minutes >= start_minutes or now_minutes < end_minutes


def _next_start(period, now):
    hours, minutes = (int(part) for part in period["start"].split(":"))
    candidate = now.replace(hour=hours, minute=minutes, second=0, microsecond=0)
    if candidate <= now:
        candidate += timedelta(days=1)
    return candidate


def _read_local_tariff():
    path = Path(__file__).with_name("tariff_config.json")
    return json.loads(path.read_text(encoding="utf-8"))


def _live_tariff(location):
    if not TARIFF_API_URL or not location:
        return None
    headers = {"Accept": "application/json"}
    if TARIFF_API_KEY:
        headers["Authorization"] = f"Bearer {TARIFF_API_KEY}"
    response = requests.get(
        TARIFF_API_URL,
        params={"lat": location.get("latitude"), "lon": location.get("longitude")},
        headers=headers,
        timeout=8,
    )
    response.raise_for_status()
    payload = response.json()
    if not isinstance(payload.get("periods"), list):
        raise ValueError("tariff API response is missing periods")
    payload["data_mode"] = "live_api"
    return payload


def get_tariff(location=None):
    try:
        tariff = _live_tariff(location)
    except (requests.RequestException, ValueError, TypeError):
        tariff = None
    if tariff is None:
        tariff = _read_local_tariff()
        tariff["data_mode"] = "reference_region_cache"

    now = datetime.now(ZoneInfo(LOCAL_TIMEZONE))
    province = (location or {}).get("province", "")
    region_match = location is None or bool(
        province and (tariff.get("region", "") in province or province in tariff.get("region", ""))
    )
    current = None
    now_minutes = now.hour * 60 + now.minute
    for period in tariff.get("periods", []):
        if _contains(now_minutes, period["start"], period["end"]):
            current = period
            break
    valley_periods = [period for period in tariff.get("periods", []) if period.get("kind") == "valley"]
    next_valley = None
    if valley_periods:
        active_valley = next(
            (period for period in valley_periods if _contains(now_minutes, period["start"], period["end"])),
            None,
        )
        if active_valley:
            next_valley = {"start": now.isoformat(timespec="minutes"), "period": active_valley}
        else:
            period = min(valley_periods, key=lambda item: _next_start(item, now))
            next_valley = {"start": _next_start(period, now).isoformat(timespec="minutes"), "period": period}
    result = dict(tariff)
    if not region_match:
        current = None
        next_valley = None
        result["periods"] = []
    result.update(
        {
            "checked_at": now.isoformat(timespec="seconds"),
            "location": location,
            "region_match": region_match,
            "current_period": current,
            "next_cheapest_window": next_valley,
            "pricing_status_cn": "Reference region matched" if region_match else "Local tariff requires verification",
        }
    )
    return result
