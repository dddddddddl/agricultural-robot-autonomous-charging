from typing import Any, Literal, Optional

from pydantic import BaseModel, Field


class DockTelemetry(BaseModel):
    device: str = "ESP32S3_SMART_CHARGING_DOCK"
    state: str = "IDLE"
    fault: str = "NONE"
    relayOn: bool = False
    wifiConnected: bool = False
    wifiIp: str = ""
    uptime_ms: int = 0
    tof_mm: Optional[float] = None
    tof_ok: bool = False
    micro_switch_pressed: bool = False
    micro_switch_stable: bool = False
    emergency_stop: bool = False
    voltage_v: Optional[float] = None
    current_a: Optional[float] = None
    power_w: Optional[float] = None
    ina226_online: bool = False
    ina226_read_ok: bool = False
    voltage_ok: bool = False
    current_ok: bool = False
    temperature_c: Optional[float] = None
    temp_online: bool = False
    temp_ok: bool = False
    temp_warning: bool = False

    model_config = {"extra": "allow"}


class VehicleStatusRequest(BaseModel):
    soc_percent: float = Field(ge=0, le=100)
    voltage_v: float = Field(gt=0, le=60)
    charging: Optional[bool] = None
    source: Literal["rosbridge_power_voltage_estimated"] = "rosbridge_power_voltage_estimated"


class VehicleEventRequest(BaseModel):
    event: Literal["ros_connected", "ros_disconnected"]
    soc_percent: Optional[float] = Field(default=None, ge=0, le=100)


class LocationRequest(BaseModel):
    latitude: float = Field(ge=-90, le=90)
    longitude: float = Field(ge=-180, le=180)


class ChargeLimitRequest(BaseModel):
    charge_limit_percent: int = Field(ge=80, le=100)


class SmartChargeRequest(BaseModel):
    enabled: bool
    departure_time: Optional[str] = Field(default=None, pattern=r"^([01]\d|2[0-3]):[0-5]\d$")


class ReportRequest(BaseModel):
    latitude: Optional[float] = Field(default=None, ge=-90, le=90)
    longitude: Optional[float] = Field(default=None, ge=-180, le=180)


class MockStatusRequest(BaseModel):
    scenario: str = "charging"
    points: int = Field(default=24, ge=1, le=300)
    overrides: dict[str, Any] = {}
