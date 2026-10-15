#include "MicroSwitchSensor.h"

MicroSwitchSensor::MicroSwitchSensor(uint8_t pin, bool activeLow, unsigned long debounceMs)
  : _pin(pin),
    _activeLow(activeLow),
    _debounceMs(debounceMs),
    _lastRawLevel(HIGH),
    _stableRawLevel(HIGH),
    _pressed(false),
    _lastPressed(false),
    _changed(false),
    _lastChangeTime(0) {
}

void MicroSwitchSensor::begin() {
  pinMode(_pin, INPUT_PULLUP);

  _lastRawLevel = digitalRead(_pin);
  _stableRawLevel = _lastRawLevel;

  if (_activeLow) {
    _pressed = (_stableRawLevel == LOW);
  } else {
    _pressed = (_stableRawLevel == HIGH);
  }

  _lastPressed = _pressed;
  _changed = false;
  _lastChangeTime = millis();
}

void MicroSwitchSensor::update() {
  _changed = false;

  int currentRawLevel = digitalRead(_pin);

  // Restart the debounce timer whenever the raw level changes.
  if (currentRawLevel != _lastRawLevel) {
    _lastRawLevel = currentRawLevel;
    _lastChangeTime = millis();
  }

  // Accept a change only after the level remains stable for debounceMs.
  if ((millis() - _lastChangeTime) >= _debounceMs) {
    if (currentRawLevel != _stableRawLevel) {
      _stableRawLevel = currentRawLevel;

      _lastPressed = _pressed;

      if (_activeLow) {
        _pressed = (_stableRawLevel == LOW);
      } else {
        _pressed = (_stableRawLevel == HIGH);
      }

      if (_pressed != _lastPressed) {
        _changed = true;
      }
    }
  }
}

bool MicroSwitchSensor::isPressed() const {
  return _pressed;
}

bool MicroSwitchSensor::hasChanged() const {
  return _changed;
}

int MicroSwitchSensor::rawLevel() const {
  return _stableRawLevel;
}
