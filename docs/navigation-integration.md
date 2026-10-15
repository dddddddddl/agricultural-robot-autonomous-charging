# Navigation and mission integration notes

This document preserves the useful Nav2 design from the supplied technical notes. The actual navigation packages are missing from the provided ZIPs, so the interfaces below are **integration requirements/documented design**, not runnable components in this repository.

## Intended return-to-charge sequence

1. Read battery state and require repeated low samples before interrupting the current task.
2. Lock out ordinary waypoint requests while returning to charge.
3. Cancel active navigation actions and confirm cancellation before sending a new goal.
4. Use `NavigateToPose` to reach a measured pre-docking pose aligned with the station.
5. Transfer velocity ownership to visual docking.
6. Require explicit docking/charging confirmation and apply bounded timeouts/retries.
7. Release or restore mission state according to a defined recovery policy.

The notes describe a 25% low threshold, five consecutive samples and a 35% reset threshold. `sensor_msgs/BatteryState.percentage` uses 0–1 units; these source-design values are not a verified configuration on a supplied running vehicle.

## Documented interfaces

| Interface | Intended purpose |
|---|---|
| `/battery_status` / `sensor_msgs/msg/BatteryState` | Battery monitoring for return decisions |
| `/navigate_to_pose` / `nav2_msgs/action/NavigateToPose` | Return to pre-docking pose |
| `/navigate_through_poses` and `/follow_waypoints` | Existing navigation actions to cancel/arbitrate |
| `/return_to_charge_active` / `std_msgs/msg/Bool` | Mission lock/gate state |
| `/start_docking` / `std_msgs/msg/Bool` | Authorize visual takeover |
| `/docking_complete` / `std_msgs/msg/Bool` | Completion feedback described by later notes |

None of the last three interfaces is implemented by the supplied standalone vision node. A boolean trigger alone also does not define timeout, result, cancellation or failure semantics; those need an integration contract.

## Navigation constraints

The earlier navigation document identifies ROS 2 Humble, an Ackermann chassis, map-frame waypoint coordinates and separate waypoint/low-battery branches. It describes pixel-to-map conversion using map resolution, dimensions, origin and origin yaw. It also identifies gaps in occupancy/safety-radius checking, action-cancel confirmation, return-task priority and mission persistence.

The intended TF chain is `map → odom → base frame → sensors`, with a single publisher for each transform responsibility. Map origin, camera extrinsics, staging pose and actual base frame must be measured for the deployed vehicle. No sample coordinates from an old document are promoted into an executable configuration here.

A pre-docking pose must allow an Ackermann vehicle to approach with feasible curvature. Planner/controller motion models and minimum turning radii must agree; a design mention of Smac Hybrid A* or MPPI does not establish the installed Nav2 configuration because the corresponding YAML is absent.

## Later visual-planner descriptions

A later note describes v14 Kalman filtering with yaw/lateral control and v16 visual Hybrid A* with forward/reverse segments and limited gear changes. Neither implementation was found in the supplied archive set. Their claimed yaw/gear-change behavior must not be attributed to `robot/vision/aruco_docking.py`, which implements low-pass-filtered lateral/bearing reverse control.

The earlier document names `Mingdong24/Agricultural_vehicle_nav_backend` as a navigation repository reference. No external code was fetched or reassigned to this project's owner. Recovering the intended source and its authorship/version would be the next step toward a complete mission-level release.
