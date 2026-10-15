import cv2
import numpy as np

# ==============================
# Basic configuration
# ==============================

# Select an available local camera index.
CAMERA_INDEX = 0

# Use the same dictionary used to generate the marker.
ARUCO_DICT = cv2.aruco.DICT_4X4_50

# Allowed image-center error
# 0.08 means the marker center is within 8% of the frame width.
CENTER_TOLERANCE = 0.08

# Simulated control values only; this script sends no vehicle commands.
# ROS 2 /cmd_vel integration is handled by the separate docking node.
REVERSE_SPEED = -0.03
TURN_SPEED = 0.10


# ==============================
# Initialize the ArUco detector.
# ==============================

aruco_dict = cv2.aruco.getPredefinedDictionary(ARUCO_DICT)
detector_params = cv2.aruco.DetectorParameters()
detector = cv2.aruco.ArucoDetector(aruco_dict, detector_params)


# ==============================
# Open the camera.
# ==============================

cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_AVFOUNDATION)

if not cap.isOpened():
    print(f"Cannot open camera index={CAMERA_INDEX}")
    exit()

print("Camera opened.")
print("Overlay guide:")
print("1. Green vertical line: frame center")
print("2. Red dot: marker center")
print("3. error_x > 0: marker to the right")
print("4. error_x < 0: marker to the left")
print("Press q or Escape to exit.")


while True:
    ret, frame = cap.read()

    if not ret:
        print("Camera frame capture failed")
        break

    # Convert to grayscale.
    if len(frame.shape) == 3:
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    else:
        gray = frame

    # Convert grayscale back to BGR for colored text and lines.
    display = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

    height, width = gray.shape[:2]

    # Frame center
    image_center_x = width // 2
    image_center_y = height // 2

    # Draw the green centerline.
    cv2.line(
        display,
        (image_center_x, 0),
        (image_center_x, height),
        (0, 255, 0),
        2
    )

    # Detect markers.
    corners, ids, rejected = detector.detectMarkers(gray)

    # Default simulated control state
    state = "NO_MARKER"
    linear_x = 0.0
    angular_z = 0.0

    if ids is not None and len(ids) > 0:
        # Use the first detected marker for this diagnostic.
        corner = corners[0]
        marker_id = int(ids[0][0])

        # Draw the marker outline.
        cv2.aruco.drawDetectedMarkers(display, corners, ids)

        # Four marker corners
        pts = corner[0]

        # Marker center
        center = pts.mean(axis=0)
        marker_center_x = int(center[0])
        marker_center_y = int(center[1])

        # Horizontal error: marker center minus frame center
        error_x_px = marker_center_x - image_center_x

        # Normalize by frame width.
        # This keeps the diagnostic comparable across resolutions.
        error_x_norm = error_x_px / width

        # Draw the red center point.
        cv2.circle(
            display,
            (marker_center_x, marker_center_y),
            6,
            (0, 0, 255),
            -1
        )

        # Connect the frame center to the marker center.
        cv2.line(
            display,
            (image_center_x, marker_center_y),
            (marker_center_x, marker_center_y),
            (255, 0, 0),
            2
        )

        # ==============================
        # Prototype control logic
        # ==============================

        if abs(error_x_norm) > CENTER_TOLERANCE:
            # Correct heading when the marker is far from the center.
            state = "ALIGNING"

            if error_x_norm > 0:
                # Marker is to the right.
                direction_text = "MARKER_RIGHT"
                angular_z = -TURN_SPEED
            else:
                # Marker is to the left.
                direction_text = "MARKER_LEFT"
                angular_z = TURN_SPEED

            linear_x = 0.0

        else:
            # Marker is centered; simulate slow reverse motion.
            state = "REVERSE_SLOWLY"
            direction_text = "CENTERED"
            linear_x = REVERSE_SPEED
            angular_z = 0.0

        # Steering convention:
        # The sign of angular_z is an initial assumption.
        # Verify whether positive angular_z turns the real chassis left or right.
        # Match the sign to the actual chassis before ROS 2 integration.

        # Display detection values.
        cv2.putText(
            display,
            f"ID: {marker_id}",
            (20, 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (0, 255, 0),
            2
        )

        cv2.putText(
            display,
            f"marker_center=({marker_center_x},{marker_center_y})",
            (20, 65),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (0, 255, 0),
            2
        )

        cv2.putText(
            display,
            f"error_x_px={error_x_px}, error_x_norm={error_x_norm:.3f}",
            (20, 100),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (0, 255, 255),
            2
        )

        cv2.putText(
            display,
            f"state={state}, {direction_text}",
            (20, 135),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (255, 255, 0),
            2
        )

        cv2.putText(
            display,
            f"mock cmd: linear_x={linear_x:.2f}, angular_z={angular_z:.2f}",
            (20, 170),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (255, 255, 0),
            2
        )

        # Print values to the terminal as well.
        print(
            f"ID={marker_id}, "
            f"error_x_px={error_x_px}, "
            f"error_x_norm={error_x_norm:.3f}, "
            f"state={state}, "
            f"linear_x={linear_x:.2f}, "
            f"angular_z={angular_z:.2f}"
        )

    else:
        cv2.putText(
            display,
            "No marker detected",
            (20, 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (0, 0, 255),
            2
        )

        cv2.putText(
            display,
            "mock cmd: linear_x=0.00, angular_z=0.00",
            (20, 65),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (0, 0, 255),
            2
        )

    cv2.putText(
        display,
        "Press q or ESC to quit",
        (20, height - 20),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (255, 0, 0),
        2
    )

    cv2.imshow("Aruco Control Debug", display)

    key = cv2.waitKey(10) & 0xFF

    if key == ord("q") or key == 27:
        break

    if cv2.getWindowProperty("Aruco Control Debug", cv2.WND_PROP_VISIBLE) < 1:
        break


cap.release()
cv2.destroyAllWindows()
print("Program finished.")
