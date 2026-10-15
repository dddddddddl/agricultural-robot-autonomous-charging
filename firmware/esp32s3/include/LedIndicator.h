#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Arduino.h>

// Three status LEDs:
// LED_SYSTEM = green (process / charging / warning)
// LED_PROCESS = yellow (runtime heartbeat)
// LED_FAULT = fault indicator
enum LedChannel {
  LED_SYSTEM = 0,
  LED_PROCESS = 1,
  LED_FAULT = 2
};

void ledIndicatorBegin();
void ledSet(LedChannel channel, bool on);
void ledAllOff();
void ledAllOn();
void ledSelfTest();

// Heartbeat indicates that the program is running.
void ledUpdateHeartbeat(unsigned long intervalMs);

// Update LEDs from the actual system state.
void ledUpdateForDockState(const String& stateName, bool chargeSafetyOKNow, bool temperatureWarningNow);

// Print LED pins.
void ledPrintPins();

#endif
