# Charging console and telemetry service

A FastAPI/SQLite backend serves the English dashboard and accepts telemetry from the dock and vehicle. The frontend is plain HTML/CSS/JavaScript, with a vendored ROSLIB bundle for rosbridge; no Node build step is required.

## Start locally

From the repository root, with Python 3.10+:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r web/backend/requirements.txt
python -m uvicorn main:app --app-dir web/backend --host 127.0.0.1 --port 8000
```

Open **http://127.0.0.1:8000**. Do not open `frontend/index.html` directly: it depends on API and static routes supplied by the backend.

The backend creates `web/backend/smart_charging_dock.db`. That file and `.env` are ignored by Git. To keep demonstrations separate from real telemetry, set `DATABASE_PATH` to an absolute path before starting the service.

For optional configuration:

```bash
cp web/backend/.env.example web/backend/.env
```

No secret is needed for telemetry, schedules or local rule-based reports. Dependencies as resolved in the tested environment are recorded in [validation](../docs/validation.md); the original requirements file remains the install entry point.

## Try a software-only demonstration

With the server running, insert explicitly synthetic data:

```bash
curl -X POST http://127.0.0.1:8000/api/dev/mock-status \
  -H 'Content-Type: application/json' \
  -d '{"scenario":"charging","points":24}'
```

Supported mock scenarios include `positioned`, `charging`, `smart_paused`, `fault` and `emergency`. The mock endpoint inserts records with current timestamps; it does not replay a real time-series experiment. The UI refreshes status periodically; use **Refresh** for charts and **View weekly report** for the report. Synthetic records are not project performance evidence.

## Connect the actual dock and robot

1. Set the dock's `DOCK_BACKEND_URL` to the computer's reachable LAN address and port 8000 in its ignored `local_config.h`.
2. Bind the service to that interface, or to `0.0.0.0` on an appropriately controlled LAN. The localhost-only default is for review.
3. The ESP32 sends `POST /api/status` and polls `GET /api/command/poll`.
4. Start the vehicle's existing rosbridge service and enter its host address in **Vehicle connection**. The default WebSocket port is 9090.
5. The browser subscribes to `/PowerVoltage` (`std_msgs/msg/Float32`) and `/robot_charging_flag` (`std_msgs/msg/Bool`). Verify the actual vehicle message names/types; its ROS setup is not bundled here.

The browser forwards vehicle voltage and charging state to the backend. The backend recomputes estimated charge from voltage instead of trusting the submitted percentage. Dock INA226 voltage is used for electrical monitoring, not vehicle state-of-charge estimation.

The retained 24 V/6S reference curve is:

```text
21.0 V → 0%    21.6 V → 10%   22.2 V → 20%   22.8 V → 35%
23.4 V → 50%   24.0 V → 65%   24.6 V → 80%   25.2 V → 100%
```

This is a voltage-based estimate, not a validated BMS capacity or health measurement. Battery capacity, power limits and charging duration factors in the code are project assumptions.

## How the modules fit

| Module | Responsibility |
|---|---|
| `backend/main.py` / `models.py` | Routes and input validation |
| `backend/database.py` | Telemetry, sessions, settings, command queue and reports |
| `backend/battery.py` | Vehicle voltage curve and completion estimate |
| `backend/smart_charge.py` | Usage-history prediction and staged charging schedule |
| `backend/tariff.py` / `tariff_config.json` | Optional live tariff service or supplied regional reference |
| `backend/location_service.py` | Optional reverse geocoding |
| `backend/llm_report.py` | English local reports or optional model-assisted reports |
| `frontend/app.js` | API rendering, charts, rosbridge subscriptions and command requests |

Some JSON/database field names retain the historical `_cn` suffix for compatibility. Their displayed values are English. Firmware telemetry aliases `relayOn`, `wifiConnected` and `wifiIp` remain unchanged.

## Scheduling and optional services

The schedule uses median observed charging/departure times over 30 days, or an optional model prediction. It first targets 80%, holds, then estimates a top-off window before departure. It sends the existing `REQUEST_START_CHARGE` / `STOP_CHARGING` requests; firmware still checks local interlocks.

The current scheduler **does not use tariff periods to optimize the schedule**. Tariff display and advice are separate features. A remote START releases the firmware STOP hold; it cannot clear `COMPLETE` or bypass sensors. Physical pause/resume behavior therefore requires integration testing.

- **Reports and prediction:** setting `DEEPSEEK_API_KEY` enables requests to the configured model provider. Report inputs include summarized telemetry and, if selected, location/tariff information. Leaving the key empty uses local rules. Paid/cloud calls were not exercised in validation.
- **Location:** requested only through the location button. Selected coordinates are sent to the backend and OpenStreetMap Nominatim for reverse geocoding. Do not grant location access unless you want that feature.
- **Tariffs:** `TARIFF_API_URL` and optional key can supply a live service. Otherwise the provided Jiangsu residential reference is used. Its original reference date and rates are retained; they were not reverified as current prices.

## Key API routes

| Method | Route | Purpose |
|---|---|---|
| GET | `/api/health` | Service check |
| POST | `/api/status` or `/api/dock/status` | Dock telemetry |
| GET | `/api/status` | Latest dock/vehicle state and plan |
| POST | `/api/vehicle/status` | Vehicle voltage and charging state |
| POST | `/api/vehicle/event` | Vehicle connection/disconnection event |
| GET | `/api/history`, `/api/vehicle/history` | Recorded telemetry |
| POST | `/api/config/charge-limit` | Target charge percentage |
| POST | `/api/config/smart-charge`, `/api/smart-charge/regenerate` | Scheduling |
| POST | `/api/command/start`, `/stop`, `/clear-fault` | Queue high-level requests |
| GET | `/api/command` or `/api/command/poll` | Consume one queued command |
| POST | `/api/report/generate` | English report |
| POST / GET | `/api/location` / `/api/tariff` | Optional location/reference tariff |

Interactive API documentation is available at `/docs` while the service runs.

## Deployment boundary

This is a local prototype with unauthenticated telemetry, commands and a development mock endpoint. It also retains a simple FIFO backend command queue. The firmware's local STOP priority is not a guarantee about arbitrary queued network requests. Keep demonstration data separate, avoid direct public hosting, and add authentication, transport protection and command auditing before a real remote deployment.
