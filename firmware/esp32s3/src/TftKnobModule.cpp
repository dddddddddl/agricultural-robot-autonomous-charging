#include "TftKnobModule.h"

TftKnobModule::TftKnobModule(
  uint8_t pinA,
  uint8_t pinB,
  uint8_t pinPush,
  uint8_t pinK0
)
  : _pinA(pinA),
    _pinB(pinB),
    _pinPush(pinPush),
    _pinK0(pinK0),
    _lastEncoded(0),
    _encoderDelta(0),
    _leftEvent(false),
    _rightEvent(false),
    _lastPushReading(false),
    _stablePushState(false),
    _pushPressEvent(false),
    _lastPushChangeTime(0),
    _lastK0Reading(false),
    _stableK0State(false),
    _k0PressEvent(false),
    _lastK0ChangeTime(0) {
}

void TftKnobModule::begin() {
  pinMode(_pinA, INPUT_PULLUP);
  pinMode(_pinB, INPUT_PULLUP);
  pinMode(_pinPush, INPUT_PULLUP);
  pinMode(_pinK0, INPUT_PULLUP);

  int a = digitalRead(_pinA);
  int b = digitalRead(_pinB);
  _lastEncoded = (a << 1) | b;

  _lastPushReading = readActiveLow(_pinPush);
  _stablePushState = _lastPushReading;

  _lastK0Reading = readActiveLow(_pinK0);
  _stableK0State = _lastK0Reading;
}

void TftKnobModule::update() {
  _leftEvent = false;
  _rightEvent = false;
  _pushPressEvent = false;
  _k0PressEvent = false;

  updateEncoder();

  updateButton(
    _pinPush,
    _lastPushReading,
    _stablePushState,
    _pushPressEvent,
    _lastPushChangeTime
  );

  updateButton(
    _pinK0,
    _lastK0Reading,
    _stableK0State,
    _k0PressEvent,
    _lastK0ChangeTime
  );
}

bool TftKnobModule::rotatedLeft() {
  if (_leftEvent) {
    _leftEvent = false;
    return true;
  }

  return false;
}

bool TftKnobModule::rotatedRight() {
  if (_rightEvent) {
    _rightEvent = false;
    return true;
  }

  return false;
}

bool TftKnobModule::pushPressed() {
  if (_pushPressEvent) {
    _pushPressEvent = false;
    return true;
  }

  return false;
}

bool TftKnobModule::k0Pressed() {
  if (_k0PressEvent) {
    _k0PressEvent = false;
    return true;
  }

  return false;
}

bool TftKnobModule::isPushDown() const {
  return _stablePushState;
}

bool TftKnobModule::isK0Down() const {
  return _stableK0State;
}

bool TftKnobModule::readActiveLow(uint8_t pin) const {
  return digitalRead(pin) == LOW;
}

void TftKnobModule::updateEncoder() {
  int a = digitalRead(_pinA);
  int b = digitalRead(_pinB);
  int encoded = (a << 1) | b;

  int sum = (_lastEncoded << 2) | encoded;

  if (
    sum == 0b1101 ||
    sum == 0b0100 ||
    sum == 0b0010 ||
    sum == 0b1011
  ) {
    _encoderDelta++;
  }

  if (
    sum == 0b1110 ||
    sum == 0b0111 ||
    sum == 0b0001 ||
    sum == 0b1000
  ) {
    _encoderDelta--;
  }

  _lastEncoded = encoded;

  if (_encoderDelta >= 4) {
    _rightEvent = true;
    _encoderDelta = 0;
  }

  if (_encoderDelta <= -4) {
    _leftEvent = true;
    _encoderDelta = 0;
  }
}

void TftKnobModule::updateButton(
  uint8_t pin,
  bool& lastReading,
  bool& stableState,
  bool& pressEvent,
  unsigned long& lastChangeTime
) {
  bool currentReading = readActiveLow(pin);

  if (currentReading != lastReading) {
    lastChangeTime = millis();
    lastReading = currentReading;
  }

  if ((millis() - lastChangeTime) > BUTTON_DEBOUNCE_MS) {
    if (currentReading != stableState) {
      stableState = currentReading;

      if (stableState == true) {
        pressEvent = true;
      }
    }
  }
}
