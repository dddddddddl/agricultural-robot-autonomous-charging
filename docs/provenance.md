# Source provenance and repository preparation

## Selected versions

| Component | Selection and evidence |
|---|---|
| Dock firmware | The IoT archive's `ESP32S3.zip`; all meaningful source/config files match the expanded `Auto_charge/ESP32S3` snapshot |
| Low-battery return | `dev` package `src/auto_charge` at commit `4a6e4c5528ae5fa616463bc72d7b3da0a3279375`; C++ logic and package build configuration retained |
| Vision and calibration | Expanded `Auto_charge/aruco` and `camera_test`; the separately supplied `aruco_docking3.py` is byte-identical to that archived script |
| Web backend/frontend | Expanded `Auto_charge/web`; executable backend/frontend match the nested copy |
| PCB | Supplied EasyEDA Pro export and July 14 Gerber set; retained as separate design artifacts without assuming they represent the identical manufactured revision |
| Mechanical files | Supplied model archive; all distinct model/drawing/render files preserved with English paths |
| Report / video | Original IoT report and demonstration; renamed without content inspection |
| Poster / award | Separately supplied English SURF poster and team award certificate |

The nested `Auto_charge(2)` firmware differs in two thresholds: its maximum bus voltage is 30 V instead of 28 V and maximum absolute current is 3.5 A instead of 3.0 A. The owner specifically identified the IoT firmware as the preferred state-machine source, and no validation established that those higher thresholds were appropriate, so the IoT values were retained.

A separate `charging-dock.zip` contains an alternative ToF stability implementation and patch. Its use on hardware and chronological relationship to the selected firmware were not established. It remains in the unchanged local source archive rather than becoming a second competing firmware entry point. A standalone JSON-builder snippet in the nested web copy is redundant with the selected firmware's status builder.

The initial `waypoint.zip` and `charging2.zip` contain empty directories. The low-battery return package was later added from the identified `dev` snapshot. Waypoint mission source and full deployment configuration are still absent. The return package's existing maintainer and unresolved license declaration are retained; no new license was assigned.

## Changes made for this repository

- Organized modules and renamed presentation, drawing, model, marker, report and video paths; source/include relationships are preserved.
- Translated source comments, diagnostics, web UI, offline reports and model prompts to English. Legacy protocol/database field names, including `_cn` keys, remain compatible.
- Removed original network credentials from distributable firmware and supplied an ignored `local_config.h` workflow with an example header. Original `.env` files and the historical SQLite database are excluded.
- Normalized observed/projected corner array layouts for OpenCV 5 compatibility in the calibration error calculation. The original calibration outputs and images are preserved.
- Aligned Python's non-negative percentage rounding with frontend `Math.round` at half steps; previously they could differ by one percentage point.
- Kept missing vehicle voltage/charge unknown in the frontend instead of letting JavaScript convert `null` into zero.
- Made geolocation explicitly user-initiated and removed the report button's implicit location request.
- Labeled tariff data as a historical project reference and removed an unsupported UI implication that the existing scheduler optimizes against tariff periods.
- Tightened local report labels to distinguish configured times from observed habits, absent telemetry from a no-flags result, and unconfirmed contact samples from established charging interruptions. The default tariff region is identified when no location was supplied.
- Added module guides and focused host checks. These checks and documentation improvements are repository-preparation work, not evidence of original project experiments.
- Preserved the supplied ROSLIB bundle and added its upstream BSD license notice.

No relay thresholds, GPIO assignments, motion constants, state transitions or steering conventions were changed during preparation. The added return manager retains its C++ program logic, with English comments and a descriptive package summary. Its README documents the existing behavior and integration gaps. No repository-wide license was added.

## Excluded from the public source tree

| Category | Reason |
|---|---|
| Virtual environments, PlatformIO build output, dependency copies, object/dependency files | Rebuildable local tooling, not project implementation |
| `.env`, hardcoded credentials and historical SQLite telemetry | Local configuration or private runtime data |
| macOS resource forks, `.DS_Store`, editor caches and machine-specific paths | Host metadata |
| Repeated source snapshots and nested ZIP containers | One documented primary implementation per module |
| Vendor MCU sample workspaces, component manuals/shopping photos | Third-party reference material rather than this project's code; module dependencies are identified separately |
| Competition rules and competition-specific presentation variants | Outside the technical portfolio scope |
| Presentation-generation caches, intermediate renders and automation scratch files | Production intermediates; useful final project materials are retained |
| Superseded standalone technical notes/PDF explanations | Consolidated English guides preserve their useful content and revision conflicts |

Original inputs remain unchanged outside the repository. A full file-by-file disposition inventory and source-to-destination hashes are retained locally for the owner; only a concise provenance guide is published.

## Attribution

The English SURF poster names Siyuan Lin, Xunuo Zhuang and Yuhan Qiu, with supervisors Xiaohui Zhu and Yong Yue. The award certificate records the award's own team membership. These artifacts are preserved without turning team achievements into sole-authorship claims. There is no verified per-person module allocation in the supplied request, so none is invented.
