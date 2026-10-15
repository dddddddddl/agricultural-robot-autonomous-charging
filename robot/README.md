# Robot-side software

This directory contains the executable vision docking snapshot and camera tools. Read [vision/README.md](vision/README.md) for the controller and [calibration/README.md](calibration/README.md) for the dataset and intrinsics.

The intended robot is an Ackermann platform with a rear-facing OV9281 camera. The docking node uses ROS 2 `rclpy`, `geometry_msgs/Twist` and `std_msgs/Bool`/`Int8`, plus NumPy and OpenCV's ArUco module. The project navigation notes identify ROS 2 Humble, but no ROS distribution was executed during the host-only checks.

## Dependencies

On the robot, use its existing ROS 2 environment and install the Python image dependencies into a compatible interpreter:

```bash
python3 -m pip install -r robot/requirements.txt
```

If using a virtual environment with a system ROS installation, ensure that ROS Python packages are visible, for example by creating that environment with `--system-site-packages` and sourcing the actual ROS setup script. These dependencies do not install ROS, vehicle drivers or rosbridge.

## Source boundary

`vision/aruco_docking.py` was supplied as a standalone script. The supplied `waypoint.zip` and `charging2.zip` contain empty directories. No navigation package, package manifest, launch file, map or Nav2 configuration was recovered from them.

The [navigation integration notes](../docs/navigation-integration.md) preserve the useful architecture and interface requirements from the source documents. They are not a replacement for the missing implementation. In particular, this script must not compete with Nav2 for `/cmd_vel`.
