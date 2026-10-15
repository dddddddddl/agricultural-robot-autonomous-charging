import cv2

CAMERA_INDEX = 0

FRAME_WIDTH = 1280
FRAME_HEIGHT = 720

cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_AVFOUNDATION)

if not cap.isOpened():
    print("Cannot open camera")
    exit()

# Request the image resolution.
cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

# Read one frame to check the effective resolution.
ret, frame = cap.read()

if not ret:
    print("Frame capture failed")
    cap.release()
    exit()

# Read the resolution reported by OpenCV.
actual_width = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
actual_height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
actual_fps = cap.get(cv2.CAP_PROP_FPS)

# Read the resolution from the actual image array.
frame_height, frame_width = frame.shape[:2]

print("===== Camera Resolution Check =====")
print(f"Requested width  = {FRAME_WIDTH}")
print(f"Requested height = {FRAME_HEIGHT}")
print()
print(f"Actual width from cap  = {actual_width}")
print(f"Actual height from cap = {actual_height}")
print(f"Actual fps from cap    = {actual_fps}")
print()
print(f"Actual frame shape width  = {frame_width}")
print(f"Actual frame shape height = {frame_height}")

cap.release()
