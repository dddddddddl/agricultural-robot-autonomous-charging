# OV9281 camera calibration

This folder preserves the calibration inputs, tools and saved output used by the visual docking script. The images are camera calibration views, not a docking-success benchmark.

## Contents and workflow

| File or folder | Use |
|---|---|
| `check_resolution.py` | Inspect requested and actual camera frame size |
| `capture_calib_images.py` | Capture checkerboard views; `s` saves, `q`/Escape exits |
| `calibrate_camera.py` | Detect/refine corners, calibrate, export results and overlays |
| `calib_images_11x8/` | All 31 supplied input JPEGs |
| `calib_debug_11x8/` | All 31 supplied corner overlays |
| `ov9281_calibration.yaml` / `.npz` | Original machine-readable calibration results |
| `aruco_4x4_id0.png` | Supplied target marker; confirm printed black side length separately |
| `aruco_control_debug.py` | Early camera-only alignment diagnostic; prints simulated velocities and does not drive ROS |

After installing [robot requirements](../requirements.txt), run from **this folder** because the original tools use paths relative to the working directory:

```bash
cd robot/calibration
python check_resolution.py
python capture_calib_images.py
python calibrate_camera.py
```

For a review of the supplied dataset, only the final command is needed. It overwrites the local calibration outputs and corner overlays. Make a working copy if you want to preserve the supplied results exactly.

## Calibration results

- Image size: **1280 × 720**.
- Board: **11 × 8 inner corners**, **10 mm square side**.
- Valid images: **31**.
- OpenCV calibration RMS: **0.1313058435309403 px** in the saved file.
- Saved script-specific mean reprojection error: **0.01395079701572334**.

The script's second metric averages `L2(observed - projected) / number_of_corners` over images. It is **not** the same definition as OpenCV's RMS and should not be interpreted as mean Euclidean corner error or as physical docking error.

```text
K = [[916.94501473,   0.0,        666.38351775],
     [  0.0,        917.00803016, 345.00759932],
     [  0.0,          0.0,          1.0       ]]

D = [0.04678922, -0.06804788, 0.00068632, 0.00011322, 0.00403198]
```

The controller embeds the same intrinsics to the precision shown. A scratch re-run with OpenCV 5.0.0 reproduced RMS **0.13130584353093966** and the same script-specific mean error. Only the comparison arrays were normalized to a common `float32`, `N × 2` layout to handle the newer OpenCV output shape; calibration targets and optimization settings were unchanged.

Recalibrate after changing lens, focus, mounting or image size. Camera intrinsics and the physical marker size are independent inputs: good checkerboard reprojection does not validate the printed marker scale.
