from datetime import datetime, timedelta
from math import floor


VEHICLE_BATTERY_POINTS = (
    (21.0, 0),
    (21.6, 10),
    (22.2, 20),
    (22.8, 35),
    (23.4, 50),
    (24.0, 65),
    (24.6, 80),
    (25.2, 100),
)


def estimate_soc_from_vehicle_voltage(voltage_v):
    """Mirror the original index15.html 24V/6S piecewise curve."""
    voltage = float(voltage_v)
    if voltage <= VEHICLE_BATTERY_POINTS[0][0]:
        return 0
    if voltage >= VEHICLE_BATTERY_POINTS[-1][0]:
        return 100
    for previous, current in zip(VEHICLE_BATTERY_POINTS, VEHICLE_BATTERY_POINTS[1:]):
        if voltage <= current[0]:
            ratio = (voltage - previous[0]) / (current[0] - previous[0])
            # Match JavaScript Math.round for non-negative percentages.
            return floor(previous[1] + ratio * (current[1] - previous[1]) + 0.5)
    return 0


def estimate_remaining_time(current_soc, target_soc, power_w, capacity_wh=134.4):
    """Estimate time from SOC derived from the vehicle ROS voltage only.

    The vehicle voltage is converted in the browser with the original index15
    24V/6S curve. Dock voltage/current are never converted into vehicle SOC;
    dock power is used only to estimate delivery time.
    """
    if current_soc is None or power_w is None or float(power_w) < 5:
        return {"minutes": None, "completion_time": None, "reason": "Waiting for valid charging power"}
    gap = max(0.0, float(target_soc) - float(current_soc))
    if gap == 0:
        return {
            "minutes": 0,
            "completion_time": datetime.now().isoformat(timespec="minutes"),
            "reason": None,
        }
    weighted_gap = 0.0
    cursor = float(current_soc)
    for end, factor in ((80, 1.0), (90, 1.2), (100, 1.5)):
        portion = max(0.0, min(float(target_soc), end) - cursor)
        weighted_gap += portion * factor
        cursor += portion
    hours = float(capacity_wh) * weighted_gap / 100.0 / float(power_w)
    minutes = max(1, round(hours * 60))
    return {
        "minutes": minutes,
        "completion_time": (datetime.now() + timedelta(minutes=minutes)).isoformat(timespec="minutes"),
        "reason": None,
    }
