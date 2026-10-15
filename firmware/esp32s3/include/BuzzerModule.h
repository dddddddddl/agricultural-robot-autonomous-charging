#ifndef BUZZER_MODULE_H
#define BUZZER_MODULE_H

#include <Arduino.h>

// Current wiring: active-high buzzer IN/SIG -> GPIO2, VCC -> 3V3, GND -> GND.
class BuzzerModule {
public:
  BuzzerModule(uint8_t pin, bool activeHigh = true);
  void begin();
  void on();
  void off();
  void beep(unsigned long durationMs);
  void successBeep();
  void doubleBeep();
  void faultBeep();
  void startFaultAlarm(unsigned long onMs = 120, unsigned long offMs = 80);
  void stopAlarm();
  void update();
  void setVolume(uint8_t value);
  bool isOn() const;
  bool isAlarmActive() const;
  bool isPatternActive() const;

private:
  uint8_t _pin;
  bool _activeHigh;
  bool _isOn;
  bool _alarmActive;
  unsigned long _alarmOnMs;
  unsigned long _alarmOffMs;
  unsigned long _lastToggleTime;
  bool _patternActive;
  int _patternTotalBeeps;
  int _patternDoneBeeps;
  bool _patternOnPhase;
  unsigned long _patternOnMs;
  unsigned long _patternOffMs;
  unsigned long _patternLastChangeTime;
  uint8_t _pwmValue;
  void setOutput(bool on);
  void startPattern(int beepCount, unsigned long onMs, unsigned long offMs);
};

#endif
