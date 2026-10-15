import json
import sqlite3
from datetime import datetime, timedelta, timezone

from config import DATABASE_PATH


def connect():
    db = sqlite3.connect(DATABASE_PATH, timeout=10)
    db.row_factory = sqlite3.Row
    return db


def now_iso():
    return datetime.now(timezone.utc).isoformat()


def _ensure_column(db, table, column, declaration):
    columns = {row["name"] for row in db.execute(f"PRAGMA table_info({table})")}
    if column not in columns:
        db.execute(f"ALTER TABLE {table} ADD COLUMN {column} {declaration}")


def init_db():
    with connect() as db:
        db.executescript("""
        CREATE TABLE IF NOT EXISTS telemetry (
          id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL, device TEXT,
          dock_state TEXT, fault TEXT, relay_on INTEGER, wifi_connected INTEGER, wifi_ip TEXT,
          uptime_ms INTEGER, tof_mm REAL, tof_ok INTEGER, micro_switch_pressed INTEGER,
          micro_switch_stable INTEGER, emergency_stop INTEGER, voltage_v REAL, current_a REAL,
          power_w REAL, ina226_online INTEGER, ina226_read_ok INTEGER, voltage_ok INTEGER,
          current_ok INTEGER, temperature_c REAL, temp_online INTEGER, temp_ok INTEGER,
          temp_warning INTEGER, soc_percent REAL, soc_source TEXT
        );
        CREATE TABLE IF NOT EXISTS charge_sessions (
          session_id INTEGER PRIMARY KEY AUTOINCREMENT, start_time TEXT, end_time TEXT,
          start_soc REAL, end_soc REAL, target_soc REAL, energy_wh REAL, average_power_w REAL,
          max_temperature_c REAL, smart_charge_enabled INTEGER, stop_reason TEXT,
          fault_count INTEGER DEFAULT 0, abnormal_stop_count INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS user_config (
          id INTEGER PRIMARY KEY CHECK(id=1), charge_limit_percent INTEGER,
          smart_charge_enabled INTEGER, default_leave_time TEXT, battery_capacity_wh REAL,
          battery_capacity_ah REAL, nominal_voltage_v REAL, charge_limit_voltage_v REAL,
          charge_current_a REAL, tariff_region TEXT
        );
        CREATE TABLE IF NOT EXISTS ai_reports (
          report_id INTEGER PRIMARY KEY AUTOINCREMENT, created_at TEXT, period_start TEXT,
          period_end TEXT, input_summary_json TEXT, model_name TEXT, report_json TEXT,
          report_text TEXT
        );
        CREATE TABLE IF NOT EXISTS command_queue (
          id INTEGER PRIMARY KEY AUTOINCREMENT, created_at TEXT, command TEXT,
          consumed_at TEXT
        );
        CREATE TABLE IF NOT EXISTS vehicle_telemetry (
          id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL,
          soc_percent REAL NOT NULL, charging INTEGER, source TEXT NOT NULL,
          voltage_v REAL
        );
        CREATE INDEX IF NOT EXISTS idx_vehicle_telemetry_time
          ON vehicle_telemetry(timestamp);
        CREATE TABLE IF NOT EXISTS vehicle_events (
          id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL,
          event_type TEXT NOT NULL, soc_percent REAL, metadata_json TEXT
        );
        CREATE TABLE IF NOT EXISTS saved_location (
          id INTEGER PRIMARY KEY CHECK(id=1), updated_at TEXT, latitude REAL, longitude REAL,
          city TEXT, province TEXT, country TEXT, display_name TEXT
        );
        CREATE TABLE IF NOT EXISTS smart_charge_plans (
          plan_id INTEGER PRIMARY KEY AUTOINCREMENT, created_at TEXT NOT NULL,
          source TEXT NOT NULL, predicted_charge_start TEXT NOT NULL,
          predicted_departure TEXT NOT NULL, topoff_start TEXT NOT NULL,
          topoff_deadline TEXT NOT NULL, base_limit INTEGER NOT NULL,
          target_limit INTEGER NOT NULL, reasoning_cn TEXT NOT NULL,
          confidence_cn TEXT NOT NULL, raw_json TEXT, active INTEGER NOT NULL DEFAULT 1
        );
        CREATE TABLE IF NOT EXISTS smart_charge_runtime (
          id INTEGER PRIMARY KEY CHECK(id=1), phase_cn TEXT, detail_cn TEXT,
          last_command TEXT, last_command_at TEXT, updated_at TEXT
        );
        """)
        _ensure_column(db, "user_config", "default_charge_time", "TEXT DEFAULT '22:00'")
        _ensure_column(db, "user_config", "departure_buffer_minutes", "INTEGER DEFAULT 30")
        _ensure_column(db, "user_config", "smart_base_limit_percent", "INTEGER DEFAULT 80")
        _ensure_column(db, "user_config", "max_charge_power_w", "REAL DEFAULT 76.65")
        _ensure_column(db, "vehicle_telemetry", "voltage_v", "REAL")
        db.execute(
            """INSERT OR IGNORE INTO user_config(
              id, charge_limit_percent, smart_charge_enabled, default_leave_time,
              battery_capacity_wh, battery_capacity_ah, nominal_voltage_v,
              charge_limit_voltage_v, charge_current_a, tariff_region,
              default_charge_time, departure_buffer_minutes,
              smart_base_limit_percent, max_charge_power_w
            ) VALUES (1, 90, 0, '09:00', 134.4, 6.0, 22.4, 25.55, 3.0,
                      'Jiangsu', '22:00', 30, 80, 76.65)"""
        )
        db.execute(
            """INSERT OR IGNORE INTO smart_charge_runtime(
              id, phase_cn, detail_cn, updated_at
            ) VALUES (1, 'Disabled', 'Enable smart charging to create a schedule', ?)""",
            (now_iso(),),
        )


def get_config():
    with connect() as db:
        row = db.execute("SELECT * FROM user_config WHERE id=1").fetchone()
        return dict(row)


def update_config(field, value):
    allowed = {
        "charge_limit_percent", "smart_charge_enabled", "default_leave_time",
        "default_charge_time", "departure_buffer_minutes", "smart_base_limit_percent",
    }
    if field not in allowed:
        raise ValueError("unsupported config field")
    with connect() as db:
        db.execute(f"UPDATE user_config SET {field}=? WHERE id=1", (value,))
    return get_config()


def insert_telemetry(data):
    mapping = {
        "timestamp": now_iso(), "device": data.device, "dock_state": data.state,
        "fault": data.fault, "relay_on": data.relayOn, "wifi_connected": data.wifiConnected,
        "wifi_ip": data.wifiIp, "uptime_ms": data.uptime_ms, "tof_mm": data.tof_mm,
        "tof_ok": data.tof_ok, "micro_switch_pressed": data.micro_switch_pressed,
        "micro_switch_stable": data.micro_switch_stable, "emergency_stop": data.emergency_stop,
        "voltage_v": data.voltage_v, "current_a": data.current_a, "power_w": data.power_w,
        "ina226_online": data.ina226_online, "ina226_read_ok": data.ina226_read_ok,
        "voltage_ok": data.voltage_ok, "current_ok": data.current_ok,
        "temperature_c": data.temperature_c, "temp_online": data.temp_online,
        "temp_ok": data.temp_ok, "temp_warning": data.temp_warning,
        "soc_percent": None, "soc_source": "vehicle_ros_power_voltage_estimated",
    }
    cols = ", ".join(mapping)
    marks = ", ".join("?" for _ in mapping)
    with connect() as db:
        cur = db.execute(f"INSERT INTO telemetry ({cols}) VALUES ({marks})", tuple(mapping.values()))
        return cur.lastrowid


def latest_telemetry():
    with connect() as db:
        row = db.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1").fetchone()
        return dict(row) if row else None


def history(limit=500, from_time=None, to_time=None):
    clauses, values = [], []
    if from_time:
        clauses.append("timestamp >= ?")
        values.append(from_time)
    if to_time:
        clauses.append("timestamp <= ?")
        values.append(to_time)
    where = (" WHERE " + " AND ".join(clauses)) if clauses else ""
    values.append(limit)
    with connect() as db:
        rows = db.execute(
            f"SELECT * FROM telemetry{where} ORDER BY id DESC LIMIT ?", values
        ).fetchall()
    return [dict(row) for row in reversed(rows)]


def record_vehicle_status(
    soc_percent,
    charging,
    source="rosbridge_power_voltage_estimated",
    voltage_v=None,
):
    timestamp = now_iso()
    with connect() as db:
        previous = db.execute(
            "SELECT * FROM vehicle_telemetry ORDER BY id DESC LIMIT 1"
        ).fetchone()
        cur = db.execute(
            """INSERT INTO vehicle_telemetry(
              timestamp, soc_percent, charging, source, voltage_v
            ) VALUES (?, ?, ?, ?, ?)""",
            (
                timestamp,
                round(float(soc_percent), 2),
                None if charging is None else int(charging),
                source,
                None if voltage_v is None else round(float(voltage_v), 3),
            ),
        )
        was_charging = None if previous is None or previous["charging"] is None else bool(previous["charging"])
        is_charging = None if charging is None else bool(charging)
        if is_charging is True and was_charging is not True:
            open_session = db.execute(
                "SELECT session_id FROM charge_sessions WHERE end_time IS NULL ORDER BY session_id DESC LIMIT 1"
            ).fetchone()
            if not open_session:
                config = db.execute("SELECT * FROM user_config WHERE id=1").fetchone()
                db.execute(
                    """INSERT INTO charge_sessions(
                      start_time, start_soc, target_soc, smart_charge_enabled
                    ) VALUES (?, ?, ?, ?)""",
                    (timestamp, soc_percent, config["charge_limit_percent"], config["smart_charge_enabled"]),
                )
        elif is_charging is False and was_charging is True:
            session = db.execute(
                "SELECT session_id FROM charge_sessions WHERE end_time IS NULL ORDER BY session_id DESC LIMIT 1"
            ).fetchone()
            if session:
                db.execute(
                    "UPDATE charge_sessions SET end_time=?, end_soc=?, stop_reason=? WHERE session_id=?",
                    (timestamp, soc_percent, "vehicle_charging_ended", session["session_id"]),
                )
        return cur.lastrowid


def latest_vehicle_status():
    with connect() as db:
        row = db.execute("SELECT * FROM vehicle_telemetry ORDER BY id DESC LIMIT 1").fetchone()
        return dict(row) if row else None


def vehicle_history(limit=500, since=None):
    values = []
    where = ""
    if since:
        where = "WHERE timestamp >= ?"
        values.append(since)
    values.append(limit)
    with connect() as db:
        rows = db.execute(
            f"SELECT * FROM vehicle_telemetry {where} ORDER BY id DESC LIMIT ?", values
        ).fetchall()
    return [dict(row) for row in reversed(rows)]


def record_vehicle_event(event_type, soc_percent=None, metadata=None):
    with connect() as db:
        cur = db.execute(
            "INSERT INTO vehicle_events(timestamp, event_type, soc_percent, metadata_json) VALUES (?, ?, ?, ?)",
            (now_iso(), event_type, soc_percent, json.dumps(metadata or {}, ensure_ascii=False)),
        )
        return cur.lastrowid


def habit_samples(days=30):
    since = (datetime.now(timezone.utc) - timedelta(days=days)).isoformat()
    with connect() as db:
        sessions = db.execute(
            "SELECT * FROM charge_sessions WHERE start_time >= ? ORDER BY start_time", (since,)
        ).fetchall()
        departures = db.execute(
            """SELECT * FROM vehicle_events
               WHERE timestamp >= ? AND event_type='ros_disconnected' ORDER BY timestamp""",
            (since,),
        ).fetchall()
    return {
        "sessions": [dict(row) for row in sessions],
        "departures": [dict(row) for row in departures],
    }


def recent_sessions(days=7):
    since = (datetime.now(timezone.utc) - timedelta(days=days)).isoformat()
    with connect() as db:
        rows = db.execute(
            "SELECT * FROM charge_sessions WHERE start_time >= ? ORDER BY start_time", (since,)
        ).fetchall()
    return [dict(row) for row in rows]


def save_location(location):
    with connect() as db:
        db.execute(
            """INSERT INTO saved_location(
              id, updated_at, latitude, longitude, city, province, country, display_name
            ) VALUES (1, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
              updated_at=excluded.updated_at, latitude=excluded.latitude,
              longitude=excluded.longitude, city=excluded.city,
              province=excluded.province, country=excluded.country,
              display_name=excluded.display_name""",
            (
                now_iso(), location.get("latitude"), location.get("longitude"),
                location.get("city", ""), location.get("province", ""),
                location.get("country", ""), location.get("display_name", ""),
            ),
        )
    return get_location()


def get_location():
    with connect() as db:
        row = db.execute("SELECT * FROM saved_location WHERE id=1").fetchone()
        return dict(row) if row else None


def save_smart_plan(plan):
    with connect() as db:
        db.execute("UPDATE smart_charge_plans SET active=0 WHERE active=1")
        cur = db.execute(
            """INSERT INTO smart_charge_plans(
              created_at, source, predicted_charge_start, predicted_departure,
              topoff_start, topoff_deadline, base_limit, target_limit,
              reasoning_cn, confidence_cn, raw_json, active
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)""",
            (
                now_iso(), plan["source"], plan["predicted_charge_start"],
                plan["predicted_departure"], plan["topoff_start"],
                plan["topoff_deadline"], plan["base_limit"], plan["target_limit"],
                plan["reasoning_cn"], plan["confidence_cn"],
                json.dumps(plan.get("raw", {}), ensure_ascii=False),
            ),
        )
        return cur.lastrowid


def get_active_smart_plan():
    with connect() as db:
        row = db.execute(
            "SELECT * FROM smart_charge_plans WHERE active=1 ORDER BY plan_id DESC LIMIT 1"
        ).fetchone()
        return dict(row) if row else None


def deactivate_smart_plans():
    with connect() as db:
        db.execute("UPDATE smart_charge_plans SET active=0 WHERE active=1")


def get_smart_runtime():
    with connect() as db:
        row = db.execute("SELECT * FROM smart_charge_runtime WHERE id=1").fetchone()
        return dict(row) if row else None


def update_smart_runtime(phase_cn, detail_cn, command=None):
    current = get_smart_runtime() or {}
    last_command = current.get("last_command")
    last_command_at = current.get("last_command_at")
    if command:
        last_command = command
        last_command_at = now_iso()
    with connect() as db:
        db.execute(
            """UPDATE smart_charge_runtime SET phase_cn=?, detail_cn=?,
               last_command=?, last_command_at=?, updated_at=? WHERE id=1""",
            (phase_cn, detail_cn, last_command, last_command_at, now_iso()),
        )
    return get_smart_runtime()


def queue_command(command):
    allowed = {"REQUEST_START_CHARGE", "STOP_CHARGING", "CLEAR_FAULT", "QUERY_STATUS"}
    if command not in allowed:
        raise ValueError("unsafe or unsupported command")
    with connect() as db:
        db.execute("INSERT INTO command_queue(created_at, command) VALUES (?, ?)", (now_iso(), command))


def consume_command():
    with connect() as db:
        row = db.execute(
            "SELECT * FROM command_queue WHERE consumed_at IS NULL ORDER BY id LIMIT 1"
        ).fetchone()
        if not row:
            return "NONE"
        db.execute("UPDATE command_queue SET consumed_at=? WHERE id=?", (now_iso(), row["id"]))
        return row["command"]


def save_report(summary, model, report_json, report_text):
    with connect() as db:
        db.execute(
            """INSERT INTO ai_reports(created_at, period_start, period_end,
               input_summary_json, model_name, report_json, report_text)
               VALUES (?, ?, ?, ?, ?, ?, ?)""",
            (
                now_iso(), summary["period_start"], summary["period_end"],
                json.dumps(summary, ensure_ascii=False), model,
                json.dumps(report_json, ensure_ascii=False) if report_json else None,
                report_text,
            ),
        )
