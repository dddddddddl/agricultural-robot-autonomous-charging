#include "BuzzerModule.h"

BuzzerModule::BuzzerModule(uint8_t pin, bool activeHigh)
  : _pin(pin),
    _activeHigh(activeHigh),
    _isOn(false),
    _alarmActive(false),
    _alarmOnMs(120),
    _alarmOffMs(80),
    _lastToggleTime(0),
    _patternActive(false),
    _patternTotalBeeps(0),
    _patternDoneBeeps(0),
    _patternOnPhase(false),
    _patternOnMs(0),
    _patternOffMs(0),
    _patternLastChangeTime(0),
    _pwmValue(128) {
}


void BuzzerModule::begin() {
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, _activeHigh ? LOW : HIGH);
}

void BuzzerModule::setOutput(bool on) {
  // Three-pin active buzzer modules expect a stable logic level, not PWM.
  digitalWrite(_pin, _activeHigh ? (on ? HIGH : LOW) : (on ? LOW : HIGH));
  _isOn = on;
}

void BuzzerModule::setVolume(uint8_t value) {
  _pwmValue = value;
}

void BuzzerModule::on() {
  _alarmActive = false;
  _patternActive = false;
  setOutput(true);
}

void BuzzerModule::off() {
  _alarmActive = false;
  _patternActive = false;
  setOutput(false);
}

void BuzzerModule::beep(unsigned long durationMs) {
  startPattern(1, durationMs, 0);
}

void BuzzerModule::successBeep() {
  startPattern(1, 150, 0);
}

void BuzzerModule::doubleBeep() {
  startPattern(2, 120, 100);
}

void BuzzerModule::faultBeep() {
  startPattern(6, 120, 80);
}

void BuzzerModule::startFaultAlarm(unsigned long onMs, unsigned long offMs) {
  _patternActive = false;

  _alarmActive = true;
  _alarmOnMs = onMs;
  _alarmOffMs = offMs;
  _lastToggleTime = millis();

  setOutput(true);
}

void BuzzerModule::stopAlarm() {
  if (_alarmActive) {
    _alarmActive = false;
    setOutput(false);
  }
}

void BuzzerModule::startPattern(int beepCount, unsigned long onMs, unsigned long offMs) {
  if (beepCount <= 0) {
    return;
  }

  _alarmActive = false;

  _patternActive = true;
  _patternTotalBeeps = beepCount;
  _patternDoneBeeps = 0;
  _patternOnPhase = true;
  _patternOnMs = onMs;
  _patternOffMs = offMs;
  _patternLastChangeTime = millis();

  setOutput(true);
}

void BuzzerModule::update() {
  unsigned long now = millis();

  // Continuous fault-alarm mode
  if (_alarmActive) {
    unsigned long interval = _isOn ? _alarmOnMs : _alarmOffMs;

    if (now - _lastToggleTime >= interval) {
      _lastToggleTime = now;
      setOutput(!_isOn);
    }

    return;
  }

  // Ordinary non-blocking beep mode
  if (!_patternActive) {
    return;
  }

  if (_patternOnPhase) {
    if (now - _patternLastChangeTime >= _patternOnMs) {
      setOutput(false);
      _patternDoneBeeps++;

      if (_patternDoneBeeps >= _patternTotalBeeps) {
        _patternActive = false;
        return;
      }

      _patternOnPhase = false;
      _patternLastChangeTime = now;
    }
  } else {
    if (now - _patternLastChangeTime >= _patternOffMs) {
      setOutput(true);
      _patternOnPhase = true;
      _patternLastChangeTime = now;
    }
  }
}

bool BuzzerModule::isOn() const {
  return _isOn;
}

bool BuzzerModule::isAlarmActive() const {
  return _alarmActive;
}

bool BuzzerModule::isPatternActive() const {
  return _patternActive;
}
