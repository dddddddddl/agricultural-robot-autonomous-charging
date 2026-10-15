#!/usr/bin/env python3

import math
import time
import cv2
import numpy as np

import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
from std_msgs.msg import Bool
from std_msgs.msg import Int8


# ============================================================
# 1. Main tuning parameters
# ============================================================

# Camera source:
# Use 0 if the rear camera is index 0.
# Use 1 if the rear camera is index 1.
# A device path such as /dev/video2 is also accepted.
CAMERA_SOURCE = 0

FRAME_WIDTH = 1280
FRAME_HEIGHT = 720

# Chassis velocity command topic
CMD_VEL_TOPIC = "/cmd_vel"

# ArUco configuration
ARUCO_DICT = cv2.aruco.DICT_4X4_50
TARGET_MARKER_ID = 0

# Measure the black marker coding region, excluding the white border.
# The supplied configuration uses MARKER_LENGTH = 0.068 m.
# Update this value if the printed marker has a different physical size.
MARKER_LENGTH = 0.068  # meter

# Visual docking range
STOP_DISTANCE = 0.16      # Stop at the configured z distance (0.16 m in this snapshot).
MAX_DOCK_DISTANCE = 3   # Do not take over beyond the configured maximum range (3 m).

# Lateral alignment tolerance
X_TOLERANCE = 0.025        # A lateral error below 2.5 cm is treated as approximately centered.

# Conservative speed settings
REVERSE_SPEED = -0.03      # m/s; negative linear.x commands reverse motion.
MAX_TURN_SPEED = 0.12      # rad/s; maximum angular speed during alignment.
SMALL_TURN_SPEED = 0.05    # rad/s; small angular correction while reversing.

# ============================================================
# 1.0.1 Steering smoothing for a chassis that cannot turn in place
# ============================================================

# ALIGNING must retain a small reverse velocity because this chassis cannot pivot.
# Use a lower speed than normal reverse to allow more correction time.
ALIGN_REVERSE_SPEED = -0.018  # m/s

# Separate entry/exit thresholds prevent ALIGNING/REVERSING oscillation.
# Enter ALIGNING above 3.0 cm; exit within 1.8 cm.
ALIGN_ENTER_X_TOLERANCE = 0.030  # m
ALIGN_EXIT_X_TOLERANCE = 0.018   # m

# Low-pass filter x and bearing to reduce marker detection jitter.
# Lower alpha is smoother but slower; original guidance: 0.20 to 0.35.
POSE_FILTER_ALPHA = 0.25

# Treat very small bearing errors as aligned to avoid steering chatter.
BEARING_DEADBAND_DEG = 1.2

# Limit angular.z slew per second to prevent frame-to-frame steering reversals.
# Lower slew limits are smoother but slower; original guidance: 0.20 to 0.50.
ANGULAR_SLEW_RATE = 0.35  # rad/s^2

# Zero very small angular commands to suppress frequent small corrections.
MIN_ABS_ANGULAR_Z = 0.008  # rad/s

# Control direction configuration
# Set LINEAR_SIGN = -1.0 if forward/reverse direction is inverted.
LINEAR_SIGN = 1.0

# Set ANGULAR_SIGN = -1.0 if steering direction is inverted.
ANGULAR_SIGN = -1.0

# Whether to publish motion commands
# For initial commissioning, set False to inspect video and logs without moving.
ENABLE_MOTION = True

# Publish zero velocity when visual takeover conditions are not met.
# Use True for standalone testing so the chassis stops.
# Nav2 integration needs explicit velocity arbitration to avoid competing publishers.
PUBLISH_STOP_WHEN_INACTIVE = True

# Subscribe to vehicle charging state.
# Stop whenever /robot_charging_flag is True.
USE_CHARGING_FLAG = True
CHARGING_FLAG_TOPIC = "/robot_charging_flag"

# Optionally notify the chassis of recharge mode.
# Disabled by default; enable only after verifying the chassis protocol.
USE_RECHARGE_FLAG = False
RECHARGE_FLAG_TOPIC = "/robot_recharge_flag"

# Log output interval
LOG_INTERVAL_SEC = 0.5

# Velocity publishing frequency
CMD_PUBLISH_HZ = 15.0

# Delay motion after startup to allow operator reaction.
START_DELAY_SEC = 3.0

# Optionally exit after reaching the stop distance.
# False keeps the node running and continuously publishing stop.
EXIT_AFTER_STOP_DISTANCE = False
EXIT_AFTER_STOP_DELAY_SEC = 2.0


# ============================================================
# 1.1 Debug view configuration
# ============================================================

# Enable the camera debug window.
SHOW_DEBUG_WINDOW = True

# Window name
DEBUG_WINDOW_NAME = "Aruco Docking Debug View"

# Exit the program on q or Escape.
QUIT_KEY_STOPS_PROGRAM = True

# Optionally exit when the window is closed manually.
# Disabled by default because some Ubuntu/OpenCV/GTK builds misreport closed windows.
# Use q, Escape or Ctrl+C to exit explicitly.
CLOSE_WINDOW_STOPS_PROGRAM = False

# Pose-axis length in meters
AXIS_LENGTH = 0.03

# Draw overlays every frame; throttle terminal output with LOG_INTERVAL_SEC.
PRINT_POSE_TO_TERMINAL_EVERY_FRAME = False


# ============================================================
# 2. OV9281 calibration at 1280 x 720
# ============================================================

camera_matrix = np.array([
    [916.94501473, 0.0, 666.38351775],
    [0.0, 917.00803016, 345.00759932],
    [0.0, 0.0, 1.0]
], dtype=np.float32)

dist_coeffs = np.array([
    0.04678922,
    -0.06804788,
    0.00068632,
    0.00011322,
    0.00403198
], dtype=np.float32)


# ============================================================
# 3. Utility functions
# ============================================================

def clamp(value: float, min_value: float, max_value: float) -> float:
    return max(min_value, min(max_value, value))


def make_twist(linear_x: float, angular_z: float) -> Twist:
    msg = Twist()
    msg.linear.x = float(linear_x)
    msg.linear.y = 0.0
    msg.linear.z = 0.0
    msg.angular.x = 0.0
    msg.angular.y = 0.0
    msg.angular.z = float(angular_z)
    return msg


def create_marker_object_points(marker_length: float) -> np.ndarray:
    half = marker_length / 2.0

    # Corner order matches ArUco detections:
    # Top-left, top-right, bottom-right, bottom-left
    return np.array([
        [-half,  half, 0.0],
        [ half,  half, 0.0],
        [ half, -half, 0.0],
        [-half, -half, 0.0],
    ], dtype=np.float32)


def open_camera(source):
    # Prefer V4L2 on Linux.
    cap = cv2.VideoCapture(source, cv2.CAP_V4L2)

    if not cap.isOpened():
        # Fall back to automatic backend selection.
        cap = cv2.VideoCapture(source)

    return cap


def put_text(img, text, org, color=(255, 255, 255), scale=0.65, thickness=2):
    """Draw outlined text for visibility on bright or complex backgrounds."""
    cv2.putText(
        img,
        text,
        org,
        cv2.FONT_HERSHEY_SIMPLEX,
        scale,
        (0, 0, 0),
        thickness + 2,
        cv2.LINE_AA
    )
    cv2.putText(
        img,
        text,
        org,
        cv2.FONT_HERSHEY_SIMPLEX,
        scale,
        color,
        thickness,
        cv2.LINE_AA
    )


# ============================================================
# 4. ROS 2 node
# ============================================================

class ArucoDockingWithView(Node):
    def __init__(self):
        super().__init__("aruco_docking_with_view")

        self.cmd_pub = self.create_publisher(Twist, CMD_VEL_TOPIC, 10)

        self.recharge_pub = None
        if USE_RECHARGE_FLAG:
            self.recharge_pub = self.create_publisher(Int8, RECHARGE_FLAG_TOPIC, 10)

        self.is_charging = False
        if USE_CHARGING_FLAG:
            self.create_subscription(
                Bool,
                CHARGING_FLAG_TOPIC,
                self.charging_callback,
                10
            )

        self.aruco_dict = cv2.aruco.getPredefinedDictionary(ARUCO_DICT)
        self.detector_params = cv2.aruco.DetectorParameters()
        self.detector = cv2.aruco.ArucoDetector(self.aruco_dict, self.detector_params)

        self.object_points = create_marker_object_points(MARKER_LENGTH)

        self.cap = open_camera(CAMERA_SOURCE)

        if not self.cap.isOpened():
            raise RuntimeError(
                f"Cannot open camera CAMERA_SOURCE={CAMERA_SOURCE}. "
                f"Try CAMERA_SOURCE=1 or '/dev/video0' / '/dev/video1'."
            )

        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

        # Read initial frames to allow the camera to settle.
        ret = False
        frame = None
        for _ in range(10):
            ret, frame = self.cap.read()
            if ret:
                break
            time.sleep(0.1)

        if not ret or frame is None:
            raise RuntimeError("Camera opened but frame capture failed.")

        actual_h, actual_w = frame.shape[:2]

        self.last_cmd_publish_time = 0.0
        self.last_log_time = 0.0
        self.last_state = None
        self.stop_reached_time = None
        self.window_created = False

        # Control filtering and hysteresis
        self.pose_filter_initialized = False
        self.filtered_x = 0.0
        self.filtered_bearing_x_deg = 0.0
        self.motion_state_for_hysteresis = None
        self.last_smoothed_angular_z = 0.0
        self.last_smoothing_time = time.monotonic()

        self.get_logger().info("===== ArUco Docking With View Started =====")
        self.get_logger().info(f"CAMERA_SOURCE = {CAMERA_SOURCE}")
        self.get_logger().info(f"Requested resolution = {FRAME_WIDTH} x {FRAME_HEIGHT}")
        self.get_logger().info(f"Actual resolution    = {actual_w} x {actual_h}")
        self.get_logger().info(f"CMD_VEL_TOPIC = {CMD_VEL_TOPIC}")
        self.get_logger().info(f"TARGET_MARKER_ID = {TARGET_MARKER_ID}")
        self.get_logger().info(f"MARKER_LENGTH = {MARKER_LENGTH:.3f} m")
        self.get_logger().info(f"STOP_DISTANCE = {STOP_DISTANCE:.3f} m")
        self.get_logger().info(f"MAX_DOCK_DISTANCE = {MAX_DOCK_DISTANCE:.3f} m")
        self.get_logger().info(f"X_TOLERANCE = {X_TOLERANCE:.3f} m")
        self.get_logger().info(f"ENABLE_MOTION = {ENABLE_MOTION}")
        self.get_logger().info(f"PUBLISH_STOP_WHEN_INACTIVE = {PUBLISH_STOP_WHEN_INACTIVE}")
        self.get_logger().info(f"USE_CHARGING_FLAG = {USE_CHARGING_FLAG}")
        self.get_logger().info(f"USE_RECHARGE_FLAG = {USE_RECHARGE_FLAG}")
        self.get_logger().info(f"SHOW_DEBUG_WINDOW = {SHOW_DEBUG_WINDOW}")

        if actual_w != FRAME_WIDTH or actual_h != FRAME_HEIGHT:
            self.get_logger().warn(
                "Capture resolution differs from calibration; pose estimates may be inaccurate."
            )

        if SHOW_DEBUG_WINDOW:
            cv2.namedWindow(DEBUG_WINDOW_NAME, cv2.WINDOW_NORMAL)
            self.window_created = True

        self.publish_stop_repeated(times=5, interval=0.03)

        if USE_RECHARGE_FLAG:
            self.publish_recharge_flag(1)

        self.get_logger().info(f"Control starts in {START_DELAY_SEC:.1f} seconds.")
        time.sleep(START_DELAY_SEC)

    def charging_callback(self, msg: Bool):
        self.is_charging = bool(msg.data)

    def publish_recharge_flag(self, value: int):
        if self.recharge_pub is None:
            return

        msg = Int8()
        msg.data = int(value)
        self.recharge_pub.publish(msg)

    def publish_stop(self):
        self.cmd_pub.publish(make_twist(0.0, 0.0))

    def publish_stop_repeated(self, times=10, interval=0.05):
        for _ in range(times):
            self.publish_stop()
            time.sleep(interval)

    def compute_pose(self, corners, ids):
        """
        Returns:
        found, x, y, z, distance, bearing_x_deg, rvec, tvec, image_points, marker_id
        """
        if ids is None or len(ids) == 0:
            return False, None, None, None, None, None, None, None, None, None

        for i in range(len(ids)):
            marker_id = int(ids[i][0])

            if marker_id != TARGET_MARKER_ID:
                continue

            image_points = corners[i][0].astype(np.float32)

            try:
                success, rvec, tvec = cv2.solvePnP(
                    self.object_points,
                    image_points,
                    camera_matrix,
                    dist_coeffs,
                    flags=cv2.SOLVEPNP_IPPE_SQUARE
                )
            except cv2.error:
                success, rvec, tvec = cv2.solvePnP(
                    self.object_points,
                    image_points,
                    camera_matrix,
                    dist_coeffs,
                    flags=cv2.SOLVEPNP_ITERATIVE
                )

            if not success:
                return False, None, None, None, None, None, None, None, None, marker_id

            x = float(tvec[0][0])
            y = float(tvec[1][0])
            z = float(tvec[2][0])

            distance = math.sqrt(x * x + y * y + z * z)
            bearing_x_deg = math.degrees(math.atan2(x, z))

            return True, x, y, z, distance, bearing_x_deg, rvec, tvec, image_points, marker_id

        return False, None, None, None, None, None, None, None, None, None

    def reset_control_filters(self):
        self.pose_filter_initialized = False
        self.motion_state_for_hysteresis = None
        self.last_smoothed_angular_z = 0.0
        self.last_smoothing_time = time.monotonic()

    def filter_pose_for_control(self, found, x, bearing_x_deg):
        """Low-pass filter control x and bearing; the view can still display raw values."""
        if not found or x is None or bearing_x_deg is None:
            self.pose_filter_initialized = False
            return x, bearing_x_deg

        if not self.pose_filter_initialized:
            self.filtered_x = float(x)
            self.filtered_bearing_x_deg = float(bearing_x_deg)
            self.pose_filter_initialized = True
        else:
            alpha = POSE_FILTER_ALPHA
            self.filtered_x = alpha * float(x) + (1.0 - alpha) * self.filtered_x
            self.filtered_bearing_x_deg = (
                alpha * float(bearing_x_deg)
                + (1.0 - alpha) * self.filtered_bearing_x_deg
            )

        return self.filtered_x, self.filtered_bearing_x_deg

    def apply_angular_smoothing(self, state, linear_x, angular_z):
        """Limit angular.z slew to suppress rapid left/right steering changes."""
        active_motion_states = {
            "ALIGNING",
            "REVERSING",
        }

        if state not in active_motion_states:
            self.last_smoothed_angular_z = 0.0
            self.last_smoothing_time = time.monotonic()
            return linear_x, 0.0

        now = time.monotonic()
        dt = max(0.001, now - self.last_smoothing_time)
        self.last_smoothing_time = now

        # Zero very small steering commands to suppress jitter.
        if abs(angular_z) < MIN_ABS_ANGULAR_Z:
            target_angular_z = 0.0
        else:
            target_angular_z = angular_z

        max_delta = ANGULAR_SLEW_RATE * dt
        smoothed_angular_z = clamp(
            target_angular_z,
            self.last_smoothed_angular_z - max_delta,
            self.last_smoothed_angular_z + max_delta
        )

        self.last_smoothed_angular_z = smoothed_angular_z
        return linear_x, smoothed_angular_z

    def decide_cmd(self, found, x, z, bearing_x_deg):
        if self.is_charging:
            self.reset_control_filters()
            return "CHARGING_DETECTED", 0.0, 0.0

        if not found:
            self.reset_control_filters()
            return "NO_MARKER", 0.0, 0.0

        if z is None or z <= 0:
            self.reset_control_filters()
            return "INVALID_Z", 0.0, 0.0

        if z > MAX_DOCK_DISTANCE:
            self.reset_control_filters()
            return "TOO_FAR_WAIT_NAV2", 0.0, 0.0

        if z <= STOP_DISTANCE:
            self.reset_control_filters()
            return "STOP_DISTANCE_REACHED", 0.0, 0.0

        # Use filtered control values so detection jitter does not reach the steering directly.
        control_x, control_bearing_x_deg = self.filter_pose_for_control(
            found,
            x,
            bearing_x_deg
        )

        # Treat very small horizontal bearing errors as zero.
        if abs(control_bearing_x_deg) < BEARING_DEADBAND_DEG:
            control_bearing_x_deg = 0.0

        # State hysteresis:
        # Use the larger threshold to enter ALIGNING and the smaller one to exit.
        # This avoids repeated switching around X_TOLERANCE.
        if self.motion_state_for_hysteresis == "ALIGNING":
            need_aligning = abs(control_x) > ALIGN_EXIT_X_TOLERANCE
        else:
            need_aligning = abs(control_x) > ALIGN_ENTER_X_TOLERANCE

        if need_aligning:
            state = "ALIGNING"
            self.motion_state_for_hysteresis = "ALIGNING"

            # Ackermann alignment:
            # Steer while reversing slowly rather than attempting to turn in place.
            # A chassis that cannot pivot requires nonzero linear_x.
            linear_x = LINEAR_SIGN * ALIGN_REVERSE_SPEED

            # bearing_x > 0 means the marker is to the camera's right.
            # Use ANGULAR_SIGN to match the chassis steering convention.
            raw_angular = -0.015 * control_bearing_x_deg
            angular_z = ANGULAR_SIGN * clamp(
                raw_angular,
                -MAX_TURN_SPEED,
                MAX_TURN_SPEED
            )

            return state, linear_x, angular_z

        # When approximately centered, reverse slowly with small smooth corrections.
        state = "REVERSING"
        self.motion_state_for_hysteresis = "REVERSING"

        raw_angular = -0.006 * control_bearing_x_deg
        angular_z = ANGULAR_SIGN * clamp(
            raw_angular,
            -SMALL_TURN_SPEED,
            SMALL_TURN_SPEED
        )

        linear_x = LINEAR_SIGN * REVERSE_SPEED

        return state, linear_x, angular_z

    def should_publish_this_cycle(self):
        now = time.monotonic()
        period = 1.0 / CMD_PUBLISH_HZ

        if now - self.last_cmd_publish_time < period:
            return False

        self.last_cmd_publish_time = now
        return True

    def publish_cmd(self, state, linear_x, angular_z):
        if not self.should_publish_this_cycle():
            return

        active_motion_states = {
            "ALIGNING",
            "REVERSING",
        }

        stop_states = {
            "STOP_DISTANCE_REACHED",
            "CHARGING_DETECTED",
            "INVALID_Z",
        }

        inactive_states = {
            "NO_MARKER",
            "TOO_FAR_WAIT_NAV2",
        }

        if state in active_motion_states:
            if ENABLE_MOTION:
                self.cmd_pub.publish(make_twist(linear_x, angular_z))
            else:
                self.cmd_pub.publish(make_twist(0.0, 0.0))
            return

        if state in stop_states:
            self.cmd_pub.publish(make_twist(0.0, 0.0))
            return

        if state in inactive_states:
            if PUBLISH_STOP_WHEN_INACTIVE:
                self.cmd_pub.publish(make_twist(0.0, 0.0))
            return

        self.cmd_pub.publish(make_twist(0.0, 0.0))

    def log_status(self, state, found, x, z, distance, bearing_x_deg, linear_x, angular_z):
        now = time.monotonic()

        state_changed = state != self.last_state
        time_to_log = now - self.last_log_time >= LOG_INTERVAL_SEC

        if not state_changed and not time_to_log:
            return

        self.last_state = state
        self.last_log_time = now

        motion_text = "ON" if ENABLE_MOTION else "DRY_RUN"

        if found:
            self.get_logger().info(
                f"state={state}, "
                f"x={x:.3f}m, z={z:.3f}m, distance={distance:.3f}m, "
                f"bearing_x={bearing_x_deg:.1f}deg, "
                f"cmd.linear.x={linear_x:.3f}, cmd.angular.z={angular_z:.3f}, "
                f"motion={motion_text}"
            )
        else:
            self.get_logger().info(
                f"state={state}, no target marker, "
                f"cmd.linear.x={linear_x:.3f}, cmd.angular.z={angular_z:.3f}, "
                f"motion={motion_text}"
            )

    def make_display_frame(
        self,
        frame,
        corners,
        ids,
        found,
        x,
        y,
        z,
        distance,
        bearing_x_deg,
        rvec,
        tvec,
        image_points,
        marker_id,
        state,
        linear_x,
        angular_z
    ):
        # Convert grayscale or BGR camera frames to BGR for colored overlays.
        if len(frame.shape) == 2:
            display = cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)
        else:
            display = frame.copy()

        h, w = display.shape[:2]

        # Draw the image centerline for alignment reference.
        cv2.line(display, (w // 2, 0), (w // 2, h), (255, 255, 255), 1)
        cv2.line(display, (0, h // 2), (w, h // 2), (255, 255, 255), 1)

        # Draw outlines for all detected markers.
        if ids is not None and len(ids) > 0:
            cv2.aruco.drawDetectedMarkers(display, corners, ids)

        # Draw pose axes and center only for the target marker.
        if found and rvec is not None and tvec is not None and image_points is not None:
            cv2.drawFrameAxes(
                display,
                camera_matrix,
                dist_coeffs,
                rvec,
                tvec,
                AXIS_LENGTH
            )

            center = image_points.mean(axis=0)
            cx, cy = int(center[0]), int(center[1])
            cv2.circle(display, (cx, cy), 6, (0, 0, 255), -1)
            cv2.line(display, (w // 2, h // 2), (cx, cy), (0, 255, 255), 2)

            put_text(display, f"Target ID: {marker_id}", (20, 35), (0, 255, 0), 0.8, 2)
            put_text(display, f"x={x:.3f} m, y={y:.3f} m, z={z:.3f} m", (20, 70), (0, 255, 255), 0.75, 2)
            put_text(display, f"distance={distance:.3f} m, bearing_x={bearing_x_deg:.1f} deg", (20, 105), (0, 255, 255), 0.75, 2)
        else:
            put_text(display, "No target marker detected", (20, 35), (0, 0, 255), 0.8, 2)

        motion_text = "ON" if ENABLE_MOTION else "DRY_RUN"
        put_text(display, f"state={state}", (20, 145), (255, 255, 0), 0.75, 2)
        put_text(display, f"cmd.linear.x={linear_x:.3f}, cmd.angular.z={angular_z:.3f}", (20, 180), (255, 255, 0), 0.75, 2)
        put_text(display, f"motion={motion_text}", (20, 215), (255, 255, 0), 0.75, 2)
        put_text(display, "Press q or ESC to quit", (20, h - 20), (255, 0, 0), 0.7, 2)

        return display

    def show_debug_frame(self, display):
        if not SHOW_DEBUG_WINDOW:
            return False

        cv2.imshow(DEBUG_WINDOW_NAME, display)
        key = cv2.waitKey(10) & 0xFF

        if QUIT_KEY_STOPS_PROGRAM and (key == ord("q") or key == 27):
            self.get_logger().info("q/Escape detected; stopping and exiting.")
            return True

        if CLOSE_WINDOW_STOPS_PROGRAM:
            try:
                visible = cv2.getWindowProperty(DEBUG_WINDOW_NAME, cv2.WND_PROP_VISIBLE)
                if visible < 1:
                    self.get_logger().info("Debug window closed; stopping and exiting.")
                    return True
            except cv2.error:
                self.get_logger().info("Debug window unavailable; stopping and exiting.")
                return True

        return False

    def run(self):
        while rclpy.ok():
            ret, frame = self.cap.read()

            if not ret:
                self.get_logger().error("Camera capture failed; stopping.")
                self.publish_stop_repeated(times=10, interval=0.05)
                break

            if len(frame.shape) == 2:
                gray = frame
            else:
                gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

            corners, ids, _ = self.detector.detectMarkers(gray)

            (
                found,
                x,
                y,
                z,
                distance,
                bearing_x_deg,
                rvec,
                tvec,
                image_points,
                marker_id
            ) = self.compute_pose(corners, ids)

            state, linear_x, angular_z = self.decide_cmd(
                found,
                x,
                z,
                bearing_x_deg
            )

            # Limit the final steering command slew before publishing to the chassis.
            linear_x, angular_z = self.apply_angular_smoothing(
                state,
                linear_x,
                angular_z
            )

            self.publish_cmd(state, linear_x, angular_z)
            self.log_status(
                state,
                found,
                x,
                z,
                distance,
                bearing_x_deg,
                linear_x,
                angular_z
            )

            if PRINT_POSE_TO_TERMINAL_EVERY_FRAME and found:
                print(
                    f"ID={marker_id}, "
                    f"x={x:.3f} m, "
                    f"y={y:.3f} m, "
                    f"z={z:.3f} m, "
                    f"distance={distance:.3f} m, "
                    f"bearing_x={bearing_x_deg:.1f} deg, "
                    f"state={state}"
                )

            if SHOW_DEBUG_WINDOW:
                display = self.make_display_frame(
                    frame,
                    corners,
                    ids,
                    found,
                    x,
                    y,
                    z,
                    distance,
                    bearing_x_deg,
                    rvec,
                    tvec,
                    image_points,
                    marker_id,
                    state,
                    linear_x,
                    angular_z
                )
                should_quit = self.show_debug_frame(display)
                if should_quit:
                    self.publish_stop_repeated(times=10, interval=0.05)
                    break

            if state == "STOP_DISTANCE_REACHED":
                if self.stop_reached_time is None:
                    self.stop_reached_time = time.monotonic()

                if EXIT_AFTER_STOP_DISTANCE:
                    elapsed = time.monotonic() - self.stop_reached_time
                    if elapsed >= EXIT_AFTER_STOP_DELAY_SEC:
                        self.get_logger().info("Stop distance reached; exiting automatically.")
                        break
            else:
                self.stop_reached_time = None

            # Process ROS 2 callbacks, including /robot_charging_flag.
            rclpy.spin_once(self, timeout_sec=0.0)

    def cleanup(self):
        self.get_logger().info("Cleanup: publishing stop commands.")
        self.publish_stop_repeated(times=15, interval=0.05)

        if USE_RECHARGE_FLAG:
            self.get_logger().info("Cleanup: clearing robot_recharge_flag.")
            self.publish_recharge_flag(0)
            time.sleep(0.1)

        if self.cap is not None:
            self.cap.release()

        if self.window_created:
            cv2.destroyAllWindows()


def main():
    rclpy.init()

    node = None

    try:
        node = ArucoDockingWithView()
        node.run()

    except KeyboardInterrupt:
        if node is not None:
            node.get_logger().info("Ctrl+C received; stopping and exiting.")
        else:
            print("Ctrl+C received.")

    except Exception as e:
        print(f"Program error: {e}")

    finally:
        if node is not None:
            try:
                node.cleanup()
            except Exception as e:
                print(f"Cleanup error: {e}")

            node.destroy_node()

        try:
            rclpy.shutdown()
        except Exception:
            pass


if __name__ == "__main__":
    main()
