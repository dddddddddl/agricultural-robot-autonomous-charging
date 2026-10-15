#include "EmergencyStop.h"

EmergencyStop::EmergencyStop(uint8_t pin, bool activeHigh)
  : _pin(pin),
    _activeHigh(activeHigh),
    _rawValue(LOW) {
}

void EmergencyStop::begin(bool useInternalPullup) {
  if (useInternalPullup) {
    pinMode(_pin, INPUT_PULLUP);
  } else {
    pinMode(_pin, INPUT);
  }

  _rawValue = digitalRead(_pin);
}

void EmergencyStop::update() {
  _rawValue = digitalRead(_pin);
}

bool EmergencyStop::isTriggered() const {
  if (_activeHigh) {
    return _rawValue == HIGH;
  }

  return _rawValue == LOW;
}

int EmergencyStop::rawValue() const {
  return _rawValue;
}
