#ifndef INA226_SENSOR_H
#define INA226_SENSOR_H

#include <Arduino.h>
#include <Wire.h>

#define INA226_DEFAULT_I2C_ADDRESS 0x40

// Project wiring:
//   VCC -> 3V3
//   GND -> GND
//   SDA -> GPIO13 (shared with VL53L1X)
//   SCL -> GPIO14 (shared with VL53L1X)

class INA226Sensor {
public:
  enum ErrorCode {
    ERROR_NONE = 0,
    ERROR_I2C,
    ERROR_NOT_CONNECTED,
    ERROR_CONFIG,
    ERROR_MEASURE
  };

  INA226Sensor(TwoWire& wireBus, uint8_t address = INA226_DEFAULT_I2C_ADDRESS);

  // Initialize INA226.
  bool begin();

  // Set shunt resistance in ohms.
  // The original module test uses 0.01 ohm.
  void setShuntResistorOhms(float shuntOhms);

  // Read voltage, current and power once.
  bool readMeasurement();

  bool isOnline() const;
  uint8_t consecutiveFailures() const;

  float getBusVoltageV() const;
  float getShuntVoltageMv() const;
  float getCurrentA() const;
  float getPowerW() const;

  uint8_t getAddress() const;

  ErrorCode getErrorCode() const;
  const char* getErrorString() const;

private:
  bool writeRegister(uint8_t reg, uint16_t value);
  bool readRegister(uint8_t reg, uint16_t& value);

  TwoWire* _wire;
  uint8_t _address;

  float _shuntResistorOhms;

  bool _online;
  uint8_t _consecutiveFailures;
  unsigned long _lastRecoveryAttemptMs;

  static const uint8_t MAX_CONSECUTIVE_FAILURES = 3;
  static const unsigned long RECOVERY_INTERVAL_MS = 5000;

  void markReadFailure(ErrorCode code);

  float _busVoltageV;
  float _shuntVoltageMv;
  float _currentA;
  float _powerW;

  ErrorCode _errorCode;
};

#endif
