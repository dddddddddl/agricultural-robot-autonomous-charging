import cv2
from pathlib import Path

CAMERA_INDEX = 0

# Calibration board: 11 horizontal by 8 vertical inner corners
PATTERN_SIZE = (11, 8)

# Fixed image resolution
FRAME_WIDTH = 1280
FRAME_HEIGHT = 720

# Use a separate directory to avoid mixing captures.
SAVE_DIR = Path("calib_images_11x8")
SAVE_DIR.mkdir(exist_ok=True)

cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_AVFOUNDATION)

if not cap.isOpened():
    print("Cannot open camera")
    exit()

cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

criteria = (
    cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER,
    30,
    0.001
)

saved_count = 0

print("Calibration image capture started")
print("Controls:")
print("1. Point the camera at the checkerboard")
print("2. FOUND is advisory and does not prevent saving")
print("3. Press s to save the current frame")
print("4. Capture at least 20 views; 25 to 30 or more are recommended")
print("5. Press q or Escape to exit")

while True:
    ret, frame = cap.read()

    if not ret:
        print("Camera capture failed")
        break

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY) if len(frame.shape) == 3 else frame.copy()

    found, corners = cv2.findChessboardCorners(gray, PATTERN_SIZE, None)

    display = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

    if found:
        corners2 = cv2.cornerSubPix(
            gray,
            corners,
            (11, 11),
            (-1, -1),
            criteria
        )
        cv2.drawChessboardCorners(display, PATTERN_SIZE, corners2, found)

        status_text = "FOUND"
        status_color = (0, 255, 0)
    else:
        status_text = "NOT FOUND"
        status_color = (0, 0, 255)

    cv2.putText(
        display,
        status_text,
        (20, 40),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.9,
        status_color,
        2
    )

    cv2.putText(
        display,
        f"saved: {saved_count}",
        (20, 80),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.8,
        (255, 255, 0),
        2
    )

    cv2.putText(
        display,
        "press s to save anyway, q/ESC to quit",
        (20, display.shape[0] - 20),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (255, 0, 0),
        2
    )

    cv2.imshow("Capture Calibration Images", display)

    key = cv2.waitKey(10) & 0xFF

    if key == ord("s"):
        filename = SAVE_DIR / f"calib_{saved_count:02d}.jpg"
        cv2.imwrite(str(filename), frame)

        if found:
            print(f"saved: {filename}  [FOUND]")
        else:
            print(f"saved: {filename}  [NOT FOUND, saved anyway]")

        saved_count += 1

    elif key == ord("q") or key == 27:
        break

    if cv2.getWindowProperty("Capture Calibration Images", cv2.WND_PROP_VISIBLE) < 1:
        break

cap.release()
cv2.destroyAllWindows()
print("Capture finished.")
