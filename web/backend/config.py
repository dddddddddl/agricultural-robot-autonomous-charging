import os
from pathlib import Path

from dotenv import load_dotenv

BASE_DIR = Path(__file__).resolve().parent
PROJECT_DIR = BASE_DIR.parent
load_dotenv(BASE_DIR / ".env")

DATABASE_PATH = Path(os.getenv("DATABASE_PATH", BASE_DIR / "smart_charging_dock.db"))
DEEPSEEK_API_KEY = os.getenv("DEEPSEEK_API_KEY", "")
DEEPSEEK_BASE_URL = os.getenv("DEEPSEEK_BASE_URL", "https://api.deepseek.com").rstrip("/")
DEEPSEEK_MODEL = os.getenv("DEEPSEEK_MODEL", "deepseek-chat")
LOCAL_TIMEZONE = os.getenv("LOCAL_TIMEZONE", "Asia/Shanghai")
TARIFF_API_URL = os.getenv("TARIFF_API_URL", "").strip()
TARIFF_API_KEY = os.getenv("TARIFF_API_KEY", "").strip()

BATTERY = {
    "model": "P760S",
    "capacity_ah": 6.0,
    "capacity_wh": 134.4,
    "nominal_voltage_v": 22.4,
    "charge_limit_voltage_v": 25.55,
    "charge_current_a": 3.0,
    "soc_source": "vehicle_ros_power_voltage_estimated",
}
