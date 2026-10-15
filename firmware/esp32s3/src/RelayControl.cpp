#include "RelayControl.h"

RelayControl::RelayControl(uint8_t pin, int activeLevel)
  : _pin(pin),
    _activeLevel(activeLevel),
    _inactiveLevel(activeLevel == HIGH ? LOW : HIGH),
    _isOn(false) {
}

void RelayControl::begin() {
  pinMode(_pin, OUTPUT);

  // Write OFF before initialization to prevent unintended relay activation.
  digitalWrite(_pin, _inactiveLevel);
  _isOn = false;
}

void RelayControl::set(bool on) {
  if (_isOn == on) {
    return;
  }

  int writeLevel = on ? _activeLevel : _inactiveLevel;
  digitalWrite(_pin, writeLevel);
  delay(1);
  int readLevel = digitalRead(_pin);
  _isOn = on;

  Serial.print(on ? "[RELAY] ON" : "[RELAY] OFF");
  Serial.print(" pin=");
  Serial.print(_pin);
  Serial.print(" writeLevel=");
  Serial.print(writeLevel == HIGH ? "HIGH" : "LOW");
  Serial.print(" readLevel=");
  Serial.println(readLevel == HIGH ? "HIGH" : "LOW");
}

void RelayControl::forceOff() {
  digitalWrite(_pin, _inactiveLevel);
  _isOn = false;
}

bool RelayControl::isOn() const {
  return _isOn;
}

uint8_t RelayControl::pin() const {
  return _pin;
}
