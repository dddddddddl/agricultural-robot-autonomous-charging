#ifndef EMERGENCY_STOP_H
#define EMERGENCY_STOP_H

#include <Arduino.h>

// Current wiring: GPIO42 -> emergency-stop NC, COM -> GND. INPUT_PULLUP makes
// an open wire or pressed emergency stop read HIGH and therefore fail safe.
class EmergencyStop {
public:
  EmergencyStop(uint8_t pin, bool activeHigh = true);
  void begin(bool useInternalPullup = true);
  void update();
  bool isTriggered() const;
  int rawValue() const;

private:
  uint8_t _pin;
  bool _activeHigh;
  int _rawValue;
};

#endif
