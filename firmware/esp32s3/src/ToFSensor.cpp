#include "ToFSensor.h"

ToFSensor::ToFSensor(TwoWire& wireBus)
  : _online(false),
    _lastDistanceMm(-1),
    _currentDistanceMm(-1),
    _consecutiveFailures(0),
    _lastRecoveryAttemptMs(0),
    _sensor(wireBus) {
}

bool ToFSensor::begin() {
  // SparkFun VL53L1X begin() returns 0 on success.
  if (_sensor.begin() != 0) {
    _online = false;
    _lastDistanceMm = -1;
    _currentDistanceMm = -1;
    Serial.println("[ToF] VL53L1X init failed.");
    return false;
  }

  _sensor.startRanging();

  _online = true;
  _consecutiveFailures = 0;
  _lastRecoveryAttemptMs = millis();
  _lastDistanceMm = -1;
  _currentDistanceMm = -1;

  Serial.println("[ToF] VL53L1X init OK.");
  return true;
}

int ToFSensor::readDistanceMm() {
  if (!_online) {
    _currentDistanceMm = -1;
    const unsigned long now = millis();
    if (now - _lastRecoveryAttemptMs < RECOVERY_INTERVAL_MS) return -1;
    _lastRecoveryAttemptMs = now;
    Serial.println("[ToF] recovery attempt...");
    if (!begin()) return -1;
  }

  // Wait for data with a timeout so a missing sensor cannot block the main loop forever.
  unsigned long startTime = millis();

  while (!_sensor.checkForDataReady()) {
    if (millis() - startTime > 25) {
      markReadFailure();
      return -1;
    }

    delay(1);
  }

  int distance = _sensor.getDistance();
  _sensor.clearInterrupt();

  if (distance <= 0) {
    markReadFailure();
    return -1;
  }

  _consecutiveFailures = 0;
  _currentDistanceMm = distance;
  _lastDistanceMm = distance;

  return distance;
}

int ToFSensor::getLastDistanceMm() const {
  return _lastDistanceMm;
}

int ToFSensor::getCurrentDistanceMm() const {
  return _currentDistanceMm;
}

bool ToFSensor::isOnline() const {
  return _online;
}

uint8_t ToFSensor::consecutiveFailures() const {
  return _consecutiveFailures;
}

void ToFSensor::markReadFailure() {
  _currentDistanceMm = -1;
  if (_consecutiveFailures < 255) _consecutiveFailures++;
  if (_consecutiveFailures >= MAX_CONSECUTIVE_FAILURES && _online) {
    _online = false;
    _lastRecoveryAttemptMs = millis();
    Serial.println("[ToF] marked offline after consecutive read failures.");
  }
}
