#include "INA226Sensor.h"

// INA226 register addresses
#define INA226_REG_CONFIG        0x00
#define INA226_REG_SHUNTVOLTAGE  0x01
#define INA226_REG_BUSVOLTAGE    0x02
#define INA226_REG_POWER         0x03
#define INA226_REG_CURRENT       0x04
#define INA226_REG_CALIBRATION   0x05

// INA226 configuration
#define INA226_CONFIG_RESET          0x8000
#define INA226_CONFIG_16AVG_1P1MS_CONTINUOUS 0x4527

INA226Sensor::INA226Sensor(TwoWire& wireBus, uint8_t address)
  : _wire(&wireBus),
    _address(address),
    _shuntResistorOhms(0.01f),
    _online(false),
    _consecutiveFailures(0),
    _lastRecoveryAttemptMs(0),
    _busVoltageV(0.0f),
    _shuntVoltageMv(0.0f),
    _currentA(0.0f),
    _powerW(0.0f),
    _errorCode(ERROR_NONE) {
}

bool INA226Sensor::begin() {
  // Shared Wire bus is initialized once by setup().
  uint16_t dummy = 0;
  if (!readRegister(INA226_REG_CONFIG, dummy)) {
    _online = false;
    _errorCode = ERROR_NOT_CONNECTED;
    Serial.println("[INA226] init failed: device not connected.");
    return false;
  }

  // Software reset
  if (!writeRegister(INA226_REG_CONFIG, INA226_CONFIG_RESET)) {
    _online = false;
    _errorCode = ERROR_I2C;
    Serial.println("[INA226] init failed: reset failed.");
    return false;
  }

  delay(10);

  // Continuous bus-voltage and shunt-voltage measurements
  // AVG=16, VBUSCT=1.1 ms, VSHCT=1.1 ms, continuous shunt+bus.
  // This is an INA226 register value; one filtered result takes ~35.2 ms.
  const uint16_t config = INA226_CONFIG_16AVG_1P1MS_CONTINUOUS;

  if (!writeRegister(INA226_REG_CONFIG, config)) {
    _online = false;
    _errorCode = ERROR_CONFIG;
    Serial.println("[INA226] init failed: config failed.");
    return false;
  }

  uint16_t configReadback = 0;
  if (!readRegister(INA226_REG_CONFIG, configReadback) || configReadback != config) {
    _online = false;
    _errorCode = ERROR_CONFIG;
    Serial.println("[INA226] init failed: config readback mismatch.");
    return false;
  }

  // This calibration value is mainly used by the CURRENT and POWER registers.
  // Current below is calculated directly from shunt voltage divided by shunt resistance.
  float currentLSB = 0.0001f; // 100uA / bit
  uint16_t calibration = static_cast<uint16_t>(0.00512f / (currentLSB * _shuntResistorOhms));

  if (!writeRegister(INA226_REG_CALIBRATION, calibration)) {
    _online = false;
    _errorCode = ERROR_CONFIG;
    Serial.println("[INA226] init failed: calibration failed.");
    return false;
  }

  _online = true;
  _consecutiveFailures = 0;
  _lastRecoveryAttemptMs = millis();
  _errorCode = ERROR_NONE;

  Serial.println("[INA226] init OK.");
  return true;
}

void INA226Sensor::setShuntResistorOhms(float shuntOhms) {
  if (shuntOhms <= 0.0f) {
    return;
  }

  _shuntResistorOhms = shuntOhms;
}

bool INA226Sensor::readMeasurement() {
  if (!_online) {
    _errorCode = ERROR_NOT_CONNECTED;
    const unsigned long now = millis();
    if (now - _lastRecoveryAttemptMs < RECOVERY_INTERVAL_MS) return false;
    _lastRecoveryAttemptMs = now;
    Serial.println("[INA226] recovery attempt...");
    if (!begin()) return false;
  }

  uint16_t shuntRaw = 0;
  uint16_t busRaw = 0;

  if (!readRegister(INA226_REG_SHUNTVOLTAGE, shuntRaw)) {
    markReadFailure(ERROR_I2C);
    return false;
  }

  if (!readRegister(INA226_REG_BUSVOLTAGE, busRaw)) {
    markReadFailure(ERROR_I2C);
    return false;
  }

  int16_t shuntSigned = static_cast<int16_t>(shuntRaw);

  // INA226 bus voltage LSB = 1.25mV
  _busVoltageV = busRaw * 1.25f / 1000.0f;

  // INA226 shunt voltage LSB = 2.5uV = 0.0025mV
  _shuntVoltageMv = shuntSigned * 0.0025f;

  // I = Vshunt / Rshunt
  _currentA = (_shuntVoltageMv / 1000.0f) / _shuntResistorOhms;

  _powerW = _busVoltageV * _currentA;

  _consecutiveFailures = 0;
  _errorCode = ERROR_NONE;
  return true;
}

bool INA226Sensor::isOnline() const {
  return _online;
}

uint8_t INA226Sensor::consecutiveFailures() const {
  return _consecutiveFailures;
}

void INA226Sensor::markReadFailure(ErrorCode code) {
  _errorCode = code;
  if (_consecutiveFailures < 255) _consecutiveFailures++;
  if (_consecutiveFailures >= MAX_CONSECUTIVE_FAILURES && _online) {
    _online = false;
    _lastRecoveryAttemptMs = millis();
    Serial.println("[INA226] marked offline after consecutive read failures.");
  }
}

float INA226Sensor::getBusVoltageV() const {
  return _busVoltageV;
}

float INA226Sensor::getShuntVoltageMv() const {
  return _shuntVoltageMv;
}

float INA226Sensor::getCurrentA() const {
  return _currentA;
}

float INA226Sensor::getPowerW() const {
  return _powerW;
}

uint8_t INA226Sensor::getAddress() const {
  return _address;
}

INA226Sensor::ErrorCode INA226Sensor::getErrorCode() const {
  return _errorCode;
}

const char* INA226Sensor::getErrorString() const {
  switch (_errorCode) {
    case ERROR_NONE:
      return "NO_ERROR";
    case ERROR_I2C:
      return "I2C_ERROR";
    case ERROR_NOT_CONNECTED:
      return "DEVICE_NOT_CONNECTED";
    case ERROR_CONFIG:
      return "CONFIG_ERROR";
    case ERROR_MEASURE:
      return "MEASURE_ERROR";
    default:
      return "UNKNOWN_ERROR";
  }
}

bool INA226Sensor::writeRegister(uint8_t reg, uint16_t value) {
  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->write(static_cast<uint8_t>(value >> 8));
  _wire->write(static_cast<uint8_t>(value & 0xFF));
  return _wire->endTransmission() == 0;
}

bool INA226Sensor::readRegister(uint8_t reg, uint16_t& value) {
  // INA226 keeps the selected register pointer after STOP. Using a complete
  // write followed by a read avoids the ESP32 Wire non-stop transaction path,
  // which can become unstable when this bus is shared with the VL53L1X.
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    _wire->beginTransmission(_address);
    _wire->write(reg);

    if (_wire->endTransmission(true) == 0 &&
        _wire->requestFrom(_address, static_cast<uint8_t>(2)) == 2) {
      uint8_t high = _wire->read();
      uint8_t low = _wire->read();
      value = static_cast<uint16_t>(high) << 8 | low;
      return true;
    }

    delay(2);
  }

  return false;
}
