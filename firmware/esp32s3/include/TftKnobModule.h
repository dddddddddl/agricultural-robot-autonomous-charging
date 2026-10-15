#ifndef TFT_KNOB_MODULE_H
#define TFT_KNOB_MODULE_H

#include <Arduino.h>

class TftKnobModule {
public:
  TftKnobModule(
    uint8_t pinA,
    uint8_t pinB,
    uint8_t pinPush,
    uint8_t pinK0
  );

  void begin();
  void update();

  bool rotatedLeft();
  bool rotatedRight();

  bool pushPressed();
  bool k0Pressed();

  bool isPushDown() const;
  bool isK0Down() const;

private:
  uint8_t _pinA;
  uint8_t _pinB;
  uint8_t _pinPush;
  uint8_t _pinK0;

  int _lastEncoded;
  int _encoderDelta;

  bool _leftEvent;
  bool _rightEvent;

  bool _lastPushReading;
  bool _stablePushState;
  bool _pushPressEvent;
  unsigned long _lastPushChangeTime;

  bool _lastK0Reading;
  bool _stableK0State;
  bool _k0PressEvent;
  unsigned long _lastK0ChangeTime;

  static const unsigned long BUTTON_DEBOUNCE_MS = 30;

  bool readActiveLow(uint8_t pin) const;

  void updateEncoder();

  void updateButton(
    uint8_t pin,
    bool& lastReading,
    bool& stableState,
    bool& pressEvent,
    unsigned long& lastChangeTime
  );
};

#endif
