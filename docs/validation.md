# Validation record

Initial checks were performed during repository preparation on **October 15, 2026**, on an Apple Silicon macOS host. Source provenance and report wording were reviewed on **October 16, 2026**, with the expanded host checks passing that day. These checks are separate from the original project's prototype demonstrations; the firmware build and calibration results below are from October 15.

## Completed checks

| Check | Result | Scope |
|---|---|---|
| Python source parsing | Passed | Syntax of the distributed Python modules |
| JavaScript syntax | Passed | `node --check web/frontend/app.js` |
| Backend host checks | 8 passed | Fresh DB/unknown data, telemetry mapping, vehicle voltage SOC, one-shot command compatibility, English offline output, request validation, regional tariff behavior and report distinctions between observations, missing data and defaults |
| Vision host checks | 3 passed | Stop/reverse decisions, synthetic projected-marker pose and consistency of calibration/marker artifacts |
| JavaScript renderer check | Passed | Missing SOC remains unknown; valid zero and measured data display correctly |
| Camera calibration re-run | Passed | All 31 original inputs, original corner geometry, scratch outputs |
| ESP32-S3 build | Passed | Complete firmware compile/link/image generation, no physical flashing |
| Added return package, static checks | Passed | XML/CMake dependency and source-path consistency; C++ program tokens preserved after comment translation; new docking attachment matches the existing controller |
| Local browser review | Passed | English dashboard and offline report rendered with no hardware connected; unknown charge retained |

### Source and report review

All 173 tracked files were accounted for: 110 match selected source artifacts, 36 are translated or adapted source files, and 27 are added supporting files such as guides, tests and configuration examples. The executable token comparison of 23 firmware C++/header files found only the documented credential extraction and configuration diagnostic change beyond comments and whitespace.

The local report previously described fallback times as typical usage and could show a reassuring risk status without telemetry. It also described runs of unconfirmed contact samples, including idle-dock samples, as contact interruptions. These labels now distinguish recorded observations, configured defaults and unavailable evidence. The default tariff region is also identified when no location was supplied. Three added API regression checks cover these cases; the full Python suite now has 11 passing checks. No hardware control thresholds or motion logic changed in this review.

### Calibration re-run

```text
Valid images:                31
Image size:                  1280 x 720
OpenCV RMS:                  0.13130584353093966 px
Script-specific mean error:  0.01395079701572334
```

The original saved RMS is `0.1313058435309403`; the tiny floating-point difference does not change the reported precision. The script-specific mean is an average of per-image `L2 / corner_count`, not the same statistic as the RMS. Original calibration outputs were retained; the re-run used a scratch copy.

### Firmware build

```text
Platform:  espressif32 7.1.3
Board:     esp32-s3-devkitc-1
Framework: framework-arduinoespressif32 4.20017.260907+sha.dcc1105b
RAM:       47,348 / 327,680 bytes (14.4%)
Flash:     974,513 / 3,342,336 bytes (29.2%)
Result:    SUCCESS
```

Resolved libraries included SparkFun VL53L1X 1.2.12, OneWire 2.3.8, DallasTemperature 4.0.6, Adafruit GFX 1.12.6, Adafruit ST7735/ST7789 1.11.0 and Adafruit BusIO 1.17.4. The retained PlatformIO configuration requests external PSRAM despite the generic board definition's “No PSRAM” label; actual hardware compatibility was not exercised.

## Software environment

The tested environment used Python 3.12 and the following resolved packages. These are observed test versions, not reconstructed original development pins.

| Package | Version |
|---|---|
| FastAPI | 0.143.1 |
| Uvicorn | 0.54.0 |
| Pydantic | 2.14.1 |
| Requests | 2.34.2 |
| python-dotenv | 1.2.4 |
| NumPy | 2.5.4 |
| opencv-contrib-python | 5.0.0.93 |
| PlatformIO Core | 6.2.0 |
| httpx | 0.28.1 |

The backend test client emitted an upstream deprecation warning about `httpx`; the checks still passed. OneWire emitted existing preprocessor warnings during compilation; they did not prevent the firmware build.

## Reproduce the host checks

From the repository root in a Python environment:

```bash
python -m pip install -r tests/requirements.txt
python -m unittest discover -s tests -v
node --check web/frontend/app.js
node tests/test_frontend.cjs
pio run -d firmware/esp32s3
```

See the [calibration guide](../robot/calibration/README.md) for replaying the image dataset in a working copy.

## Not verified

The added `auto_charge` return package was not compiled: ROS 2, ament and colcon are unavailable on this macOS host. Package metadata, CMake references and translation-only source preservation were checked instead.

No ROS node/rosbridge transport, camera hardware, vehicle, loaded charger or physical dock was connected. No relay was energized or firmware flashed. Accordingly, these checks do not establish:

- physical docking accuracy, success rate or completion time;
- measured emergency-stop/thermal/electrical fault response latency;
- board-to-firmware GPIO correspondence, PCB manufacturability or loaded thermal behavior;
- full Nav2 return, motion arbitration or mission resumption;
- outdoor performance, dust/water resistance or unattended operation;
- cloud model output quality or live tariff/geocoding availability.

The source report and demonstration video were preserved without content inspection, as requested. The award certificate and English poster support their own historical/project descriptions; they do not replace new hardware tests. No synthetic telemetry generated for software checks is presented as a real charging experiment.
