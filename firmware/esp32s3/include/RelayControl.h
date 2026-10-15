#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <Arduino.h>

class RelayControl {
public:
  /*
    pin: GPIO connected to the relay module's IN pin
    activeLevel:
      HIGH = active-high relay
      LOW = active-low relay

    Original standalone relay test configuration:
      RELAY_PIN = 36
      RELAY_ACTIVE_LEVEL = HIGH
  */
  RelayControl(uint8_t pin, int activeLevel = HIGH);

  // Initialize with the relay OFF at boot.
  void begin();

  // Set relay state.
  void set(bool on);

  // Force OFF for emergency-stop or fault handling.
  void forceOff();

  // Whether the relay is currently ON.
  bool isOn() const;

  // Return the relay control GPIO.
  uint8_t pin() const;

private:
  uint8_t _pin;
  int _activeLevel;
  int _inactiveLevel;
  bool _isOn;
};

#endif
