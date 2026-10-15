# Host verification

These checks exercise the backend contract, offline English output, calibration artifacts and vision decision/pose calculations without physical hardware. They were added during repository preparation.

From the repository root:

```bash
python -m pip install -r tests/requirements.txt
python -m unittest discover -s tests -v
node --check web/frontend/app.js
node tests/test_frontend.cjs
```

The backend tests create temporary databases and disable optional external model/tariff calls. The vision tests load actual source methods with ROS names stubbed, without constructing a node or opening a camera. The JavaScript check uses the actual battery renderer with a small DOM stub.

A passing result does not verify ROS transport, a complete mission, real sensor failures, relay timing or vehicle dynamics. See [the validation record](../docs/validation.md) for the firmware build, calibration re-run and remaining physical checks.
