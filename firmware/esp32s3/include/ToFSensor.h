#ifndef TOF_SENSOR_H
#define TOF_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_VL53L1X.h>

// =======================================================
// VL53L1X time-of-flight distance sensor wrapper
//
// Project wiring:
//   VIN / VCC -> 3V3
//   GND       -> GND
//   SDA       -> GPIO13 (shared with INA226)
//   SCL       -> GPIO14 (shared with INA226)
//
// Distance is returned in millimeters.
// =======================================================
class ToFSensor {
public:
  explicit ToFSensor(TwoWire& wireBus);

  // Initialize VL53L1X; return true on success.
  bool begin();

  // Read distance in millimeters.
  // Return -1 on read failure.
  int readDistanceMm();

  // Last successfully read distance.
  int getLastDistanceMm() const;

  // Most recent result, including -1 on failure.
  int getCurrentDistanceMm() const;

  // Whether sensor initialization succeeded.
  bool isOnline() const;
  uint8_t consecutiveFailures() const;

private:
  bool _online;
  int _lastDistanceMm;
  int _currentDistanceMm;
  uint8_t _consecutiveFailures;
  unsigned long _lastRecoveryAttemptMs;

  static const uint8_t MAX_CONSECUTIVE_FAILURES = 3;
  static const unsigned long RECOVERY_INTERVAL_MS = 5000;

  void markReadFailure();

  SFEVL53L1X _sensor;
};

#endif
