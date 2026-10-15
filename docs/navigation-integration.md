# Navigation and docking integration

The robot software has two separate components:

- [`auto_charge_manager`](../robot/navigation/auto_charge/README.md): battery-triggered Nav2 return to a staging pose.
- [`aruco_docking.py`](../robot/vision/README.md): standalone rear-camera reverse alignment.

The return manager publishes `/start_docking` after arriving at the staging pose. The vision controller does not subscribe to that signal, so the handover still needs to be connected. Neither module provides the complete return, contact confirmation and mission-resumption cycle by itself.

## Existing return behavior

Five consecutive BatteryState samples at or below 25% trigger return. The node publishes `/return_to_charge_active = true`, requests cancellation of the three navigation action types, waits 800 ms and sends a `NavigateToPose` staging goal. It retries failures up to two additional times.

Nav2 success moves the node to AT_STAGING and publishes `/start_docking = true`. Battery CHARGING/FULL status sets CHARGING and clears return-active. Once no longer charging, a reading at or above 35% can re-arm detection from CHARGING or FAILED.

Those defaults are configurable; they are code values, not measurements of the actual vehicle battery or station pose.

## Interfaces to connect

| Interface | Current implementation | Integration work |
|---|---|---|
| `/battery_status` | Return manager subscribes to BatteryState | Connect the actual battery source with 0–1 percentage units |
| Nav2 actions | Return manager cancels existing goals and sends staging goal | Verify cancellation completion and return priority |
| `/return_to_charge_active` | Published by return manager | External mission gate must reject ordinary goals during return |
| `/start_docking` | True published on successful arrival | Add an authorization path and control ownership for vision |
| `/docking_complete` | Not implemented by either component | Define completion, timeout, failure and reset feedback |
| `/robot_charging_flag` | Vision subscribes and stops when true | Verify its relationship to the BatteryState charging status |

## Deployment configuration still needed

The waypoint mission source, station map, full Nav2 configuration and vehicle drivers are not included. Use the actual vehicle's localization and Nav2 bringup. Measure the staging pose in the map frame and align it so the Ackermann platform has room to reverse toward the dock.

Planner/controller motion models and turning-radius constraints must match the chassis. A mention of Smac Hybrid A* or MPPI in design notes does not establish the installed configuration. The standalone visual controller uses lateral offset and bearing with low-pass filtering, hysteresis and angular slew limiting; it does not implement the later v14/v16 controllers described in other notes.

The return node also retains pending-work and cancellation limitations described in its [module README](../robot/navigation/auto_charge/README.md#current-integration-limits). Wiring the two topic names alone would not resolve motion arbitration, stale callbacks or mission recovery.
