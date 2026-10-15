# Robot-side software

This directory contains the ROS 2 low-battery return manager, vision docking controller and camera tools. Start with [navigation/auto_charge/README.md](navigation/auto_charge/README.md) for return-to-charge, [vision/README.md](vision/README.md) for reverse docking, or [calibration/README.md](calibration/README.md) for camera intrinsics.

The intended robot is an Ackermann platform with a rear-facing OV9281 camera. The docking node uses ROS 2 `rclpy`, `geometry_msgs/Twist` and `std_msgs/Bool`/`Int8`, plus NumPy and OpenCV's ArUco module. The project navigation notes identify ROS 2 Humble, but no ROS distribution was executed during the host-only checks.

## Dependencies

On the robot, use its existing ROS 2 environment and install the Python image dependencies into a compatible interpreter:

```bash
python3 -m pip install -r robot/requirements.txt
```

If using a virtual environment with a system ROS installation, ensure that ROS Python packages are visible, for example by creating that environment with `--system-site-packages` and sourcing the actual ROS setup script. These dependencies do not install ROS, vehicle drivers or rosbridge.

## Source boundary

`navigation/auto_charge` contains the low-battery return package, including its C++ node, `package.xml` and `CMakeLists.txt`. `vision/aruco_docking.py` is the standalone reverse-docking script; the newly supplied attachment matches its original archived source exactly.

The return manager publishes `/start_docking` on arrival, but the vision script does not consume that topic. It also does not publish `/docking_complete`. Connect the modules through an explicit handover and velocity ownership scheme before running them together. The [integration notes](../docs/navigation-integration.md) describe the current gap. Waypoint source, deployment maps and Nav2 planner/controller configuration remain absent.
