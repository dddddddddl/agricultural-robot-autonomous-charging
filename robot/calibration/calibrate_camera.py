import cv2
import numpy as np
import glob
from pathlib import Path

# ==============================
# Calibration board configuration
# ==============================

# Checkerboard geometry:
# 11 horizontal by 8 vertical inner corners
# Specify inner corners, not the number of squares.
PATTERN_SIZE = (11, 8)

# Physical square side length
# Measured square size: 10.0 mm (1 cm).
SQUARE_SIZE = 10.0  # mm

# Calibration image directory
# Must match SAVE_DIR in capture_calib_images.py.
IMAGE_DIR = Path("calib_images_11x8")
IMAGE_PATTERN = str(IMAGE_DIR / "*.jpg")

# Save corner overlays to inspect successful detections.
DEBUG_DIR = Path("calib_debug_11x8")
DEBUG_DIR.mkdir(exist_ok=True)

# ==============================
# OpenCV corner refinement criteria
# ==============================

criteria = (
    cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER,
    30,
    0.001
)

# ==============================
# Prepare object points.
# ==============================

# objp contains checkerboard corner coordinates in physical space.
# Example:
# (0,0,0), (10,0,0), (20,0,0) ...
# (0,10,0), (10,10,0), ...
# Units are millimeters.
objp = np.zeros((PATTERN_SIZE[0] * PATTERN_SIZE[1], 3), np.float32)

objp[:, :2] = np.mgrid[
    0:PATTERN_SIZE[0],
    0:PATTERN_SIZE[1]
].T.reshape(-1, 2)

objp *= SQUARE_SIZE

# Collect 3D object points and 2D image points.
objpoints = []  # Physical points in millimeters
imgpoints = []  # Image points in pixels

# ==============================
# Read images.
# ==============================

images = sorted(glob.glob(IMAGE_PATTERN))

if len(images) == 0:
    print(f"No calibration images found: {IMAGE_PATTERN}")
    print("Check that capture_calib_images.py saves to calib_images_11x8")
    exit()

print(f"Found {len(images)} images.")
print(f"Inner-corner pattern: PATTERN_SIZE = {PATTERN_SIZE}")
print(f"Square side: SQUARE_SIZE = {SQUARE_SIZE} mm")
print(f"Image directory: {IMAGE_DIR}")
print(f"Corner overlays: {DEBUG_DIR}")

image_size = None
good_count = 0

# ==============================
# Detect checkerboard corners in every image.
# ==============================

for fname in images:
    img = cv2.imread(fname)

    if img is None:
        print(f"[READ FAIL] {fname}")
        continue

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    image_size = gray.shape[::-1]  # (width, height)

    # Try standard corner detection first.
    found, corners = cv2.findChessboardCorners(gray, PATTERN_SIZE, None)

    # Try the more robust SB method if standard detection fails.
    # OpenCV 4.x generally provides findChessboardCornersSB.
    if not found and hasattr(cv2, "findChessboardCornersSB"):
        found_sb, corners_sb = cv2.findChessboardCornersSB(gray, PATTERN_SIZE, None)
        if found_sb:
            found = True
            corners = corners_sb

    if found:
        # cornerSubPix refines the standard detector's output.
        # SB output can be passed directly to calibrateCamera.
        try:
            corners2 = cv2.cornerSubPix(
                gray,
                corners,
                (11, 11),
                (-1, -1),
                criteria
            )
        except cv2.error:
            corners2 = corners

        objpoints.append(objp)
        imgpoints.append(corners2)
        good_count += 1

        print(f"[OK] {fname}")

        # Save a corner overlay for inspection.
        debug_img = img.copy()
        cv2.drawChessboardCorners(debug_img, PATTERN_SIZE, corners2, True)

        debug_name = DEBUG_DIR / (Path(fname).stem + "_corners.jpg")
        cv2.imwrite(str(debug_name), debug_img)

    else:
        print(f"[FAIL] {fname}")

if image_size is None:
    print("No images could be read.")
    exit()

print("\n===== Image Check Result =====")
print(f"Total images: {len(images)}")
print(f"Successful detections: {good_count}")
print(f"Image size: {image_size}")

if good_count < 10:
    print("\nFewer than 10 valid images; calibration is not recommended.")
    print("Inspect calib_debug_11x8 for detected corner overlays.")
    print("If no overlays exist, checkerboard detection failed; capture new views.")
    exit()

# ==============================
# Calibrate the camera.
# ==============================

ret, camera_matrix, dist_coeffs, rvecs, tvecs = cv2.calibrateCamera(
    objpoints,
    imgpoints,
    image_size,
    None,
    None
)

print("\n===== Calibration Result =====")
print(f"RMS ret = {ret}")

print("\nCamera Matrix:")
print(camera_matrix)

print("\nDistortion Coefficients:")
print(dist_coeffs)

# ==============================
# Calculate reprojection error.
# ==============================

mean_error = 0.0

for i in range(len(objpoints)):
    imgpoints2, _ = cv2.projectPoints(
        objpoints[i],
        rvecs[i],
        tvecs[i],
        camera_matrix,
        dist_coeffs
    )

    # Normalize corner layouts across OpenCV versions before comparison.
    observed = np.asarray(imgpoints[i], dtype=np.float32).reshape(-1, 2)
    projected = np.asarray(imgpoints2, dtype=np.float32).reshape(-1, 2)
    error = cv2.norm(observed, projected, cv2.NORM_L2) / len(projected)
    mean_error += error

mean_error = mean_error / len(objpoints)

print(f"\nMean reprojection error = {mean_error}")

# ==============================
# Save calibration results.
# ==============================

np.savez(
    "ov9281_calibration.npz",
    camera_matrix=camera_matrix,
    dist_coeffs=dist_coeffs,
    image_width=image_size[0],
    image_height=image_size[1],
    rms=ret,
    reprojection_error=mean_error,
    pattern_cols=PATTERN_SIZE[0],
    pattern_rows=PATTERN_SIZE[1],
    square_size_mm=SQUARE_SIZE,
    good_image_count=good_count
)

print("\nSaved: ov9281_calibration.npz")

fs = cv2.FileStorage("ov9281_calibration.yaml", cv2.FILE_STORAGE_WRITE)
fs.write("image_width", image_size[0])
fs.write("image_height", image_size[1])
fs.write("camera_matrix", camera_matrix)
fs.write("distortion_coefficients", dist_coeffs)
fs.write("rms", ret)
fs.write("reprojection_error", mean_error)
fs.write("pattern_cols", PATTERN_SIZE[0])
fs.write("pattern_rows", PATTERN_SIZE[1])
fs.write("square_size_mm", SQUARE_SIZE)
fs.write("good_image_count", good_count)
fs.release()

print("Saved: ov9281_calibration.yaml")

print("\nCalibration complete.")
print("Calibration summary:")
print("1. Camera Matrix")
print("2. Distortion Coefficients")
print("3. Mean reprojection error")
