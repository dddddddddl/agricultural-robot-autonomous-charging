#ifndef MICRO_SWITCH_SENSOR_H
#define MICRO_SWITCH_SENSOR_H

#include <Arduino.h>

// Current wiring: switch COM -> GND, NO -> GPIO1 using INPUT_PULLUP.
class MicroSwitchSensor {
public:
  MicroSwitchSensor(uint8_t pin, bool activeLow = true, unsigned long debounceMs = 30);
  void begin();
  void update();
  bool isPressed() const;
  bool hasChanged() const;
  int rawLevel() const;

private:
  uint8_t _pin;
  bool _activeLow;
  unsigned long _debounceMs;
  int _lastRawLevel;
  int _stableRawLevel;
  bool _pressed;
  bool _lastPressed;
  bool _changed;
  unsigned long _lastChangeTime;
};

#endif
