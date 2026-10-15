# ArUco reverse docking controller

[`aruco_docking.py`](aruco_docking.py) is the supplied `aruco_docking3.py` snapshot, renamed for a stable entry point. Its algorithm and motion defaults are retained; comments and operator messages have been translated to English, with stale comments corrected to match the executable constants.

## Read the controller

1. Module constants define the camera, marker, topic names and motion limits.
2. `compute_pose()` solves marker pose using IPPE Square; an OpenCV exception triggers iterative PnP fallback.
3. `decide_cmd()` chooses alignment, reversing or a stop state.
4. `filter_pose_for_control()` and `apply_angular_smoothing()` reduce steering jitter.
5. `publish_cmd()` applies the motion-enable switch and publishes `Twist`.
6. The main loop handles the debug view, callbacks and repeated stop commands on cleanup.

The supplied implementation uses **lateral offset and bearing** for control. It does not implement the Kalman/yaw controller or Hybrid A* planner described in later design notes.

## Configuration in this snapshot

| Setting | Value |
|---|---|
| Camera source / requested size | index 0 / 1280 × 720 |
| Marker | `DICT_4X4_50`, ID 0, black coding side 0.068 m |
| Pose frame | OpenCV camera coordinates; range is `tvec.z` |
| Maximum visual takeover range | 3.0 m |
| Stop range | `z <= 0.16 m` |
| Reverse speed | −0.030 m/s |
| Alignment reverse speed | −0.018 m/s; no pivot turn |
| Alignment hysteresis | enter above 0.030 m lateral error; exit within 0.018 m |
| Low-pass alpha / bearing deadband | 0.25 / 1.2 degrees |
| Angular slew limit | 0.35 rad/s² |
| Velocity publication | 15 Hz |
| Steering sign | `ANGULAR_SIGN = -1.0`; verify against actual chassis |
| Supplied motion default | `ENABLE_MOTION = True` |

## Standalone use on the robot

First inspect `CAMERA_SOURCE`, calibration, marker size and steering conventions. Set `ENABLE_MOTION = False` for an initial camera/log review. Then, in a sourced ROS 2 environment, run from the repository root:

```bash
python3 robot/vision/aruco_docking.py
```

The node opens a debug window by default. `q`, Escape or Ctrl+C exits and attempts repeated stop publication. A missing marker, invalid range, excessive range or an active charging flag produces a stop decision under the supplied defaults. These are software behaviors, not a measured physical stopping guarantee.

## ROS interfaces

| Direction | Topic | Type | Meaning |
|---|---|---|---|
| Publish | `/cmd_vel` | `geometry_msgs/msg/Twist` | Chassis motion command |
| Subscribe | `/robot_charging_flag` | `std_msgs/msg/Bool` | True stops visual motion |
| Optional publish, disabled by default | `/robot_recharge_flag` | `std_msgs/msg/Int8` | Chassis-specific recharge-mode flag |

There is no `/start_docking` subscriber or `/docking_complete` publisher in this snapshot. With `PUBLISH_STOP_WHEN_INACTIVE = True`, the node also publishes stop while inactive. Integrating it into a navigation system needs an explicit command mux/ownership gate and completion feedback.

The [host tests](../../tests/test_vision.py) exercise the actual decision and pose methods with ROS names stubbed and no node constructed. They do not validate ROS transport, camera capture or chassis response.
