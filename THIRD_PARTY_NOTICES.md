# Third-party notices

## ROSLIB

`web/frontend/vendor/roslib.min.js` is the ROSLIB browser bundle supplied with the project. The source page labels it 1.3.0; it is retained rather than regenerated. A complete BSD license notice from the upstream `roslib` 1.3.0 npm distribution is included as [ROSLIB-LICENSE.txt](web/frontend/vendor/ROSLIB-LICENSE.txt).

Upstream: https://github.com/RobotWebTools/roslibjs

The project includes a compatibility shim that exposes the bundle through `window.ROSLIB` in browser containers. No claim of authorship over ROSLIB is made.

## External dependencies

PlatformIO downloads the SparkFun VL53L1X, OneWire, DallasTemperature and Adafruit display libraries declared in `platformio.ini`, together with their transitive dependencies and Arduino framework. They are not vendored as project-authored sources.

The Python modules depend on FastAPI, Uvicorn, Pydantic, Requests, python-dotenv, NumPy and OpenCV. ROS 2, Nav2, vehicle drivers and rosbridge are external runtime components. Each dependency retains its own license and attribution requirements.

## Original project artifacts

The poster, report, certificate, mechanical drawings and electronics design files are supplied project artifacts. Their existing credits and notices are retained. No repository-wide open-source license is inferred from their inclusion.
