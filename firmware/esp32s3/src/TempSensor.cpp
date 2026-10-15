#include "TempSensor.h"

#include <math.h>

namespace {
constexpr uint16_t ADC_FULL_SCALE = 4095;
constexpr uint16_t ADC_RAIL_MARGIN = 20;
constexpr uint16_t ADC_MV_RAIL_MARGIN = 30;
constexpr uint8_t ADC_SAMPLE_COUNT = 16;

uint16_t lastRawAdc = 0;
uint16_t lastMilliVolts = 0;
float lastResistanceOhm = NAN;
bool lastReadingOnline = false;

uint16_t readAveragedAdc() {
  // Discard the first conversion after selecting/configuring the ADC pin.
  analogRead(TEMP_SENSOR_PIN);

  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_SAMPLE_COUNT; ++i) {
    sum += analogRead(TEMP_SENSOR_PIN);
    delayMicroseconds(200);
  }
  return static_cast<uint16_t>(sum / ADC_SAMPLE_COUNT);
}

uint16_t readAveragedMilliVolts() {
  analogReadMilliVolts(TEMP_SENSOR_PIN);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_SAMPLE_COUNT; ++i) {
    sum += analogReadMilliVolts(TEMP_SENSOR_PIN);
    delayMicroseconds(200);
  }
  return static_cast<uint16_t>(sum / ADC_SAMPLE_COUNT);
}
}

bool tempSensorBegin() {
  pinMode(TEMP_SENSOR_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(TEMP_SENSOR_PIN, ADC_11db);

  (void)tempSensorReadC();

  Serial.print("NTC analog module GPIO");
  Serial.print(TEMP_SENSOR_PIN);
  Serial.print(" init, ADC=");
  Serial.println(lastRawAdc);
  return lastReadingOnline;
}

float tempSensorReadC() {
  lastRawAdc = readAveragedAdc();
  // Arduino-ESP32 characterizes analogReadMilliVolts(); raw ADC codes are
  // retained only for diagnostics and rail/open-circuit sanity checking.
  lastMilliVolts = readAveragedMilliVolts();
  lastReadingOnline = lastRawAdc > ADC_RAIL_MARGIN &&
                      lastRawAdc < (ADC_FULL_SCALE - ADC_RAIL_MARGIN) &&
                      lastMilliVolts > ADC_MV_RAIL_MARGIN &&
                      lastMilliVolts < (NTC_DIVIDER_SUPPLY_MV - ADC_MV_RAIL_MARGIN);

  if (!lastReadingOnline) {
    lastResistanceOhm = NAN;
    return NAN;
  }

  if (NTC_FIXED_RESISTOR_TO_GND) {
    lastResistanceOhm = NTC_DIVIDER_RESISTANCE_OHM *
                        (NTC_DIVIDER_SUPPLY_MV / lastMilliVolts - 1.0f);
  } else {
    lastResistanceOhm = NTC_DIVIDER_RESISTANCE_OHM * lastMilliVolts /
                        (NTC_DIVIDER_SUPPLY_MV - lastMilliVolts);
  }

  if (!isfinite(lastResistanceOhm) || lastResistanceOhm <= 0.0f) {
    lastReadingOnline = false;
    return NAN;
  }

  // Beta equation: 1/T = 1/T0 + ln(R/R0)/B, temperature in kelvin.
  const float nominalKelvin = NTC_NOMINAL_TEMPERATURE_C + 273.15f;
  const float inverseKelvin = (1.0f / nominalKelvin) +
                              logf(lastResistanceOhm / NTC_NOMINAL_RESISTANCE_OHM) /
                                NTC_BETA_VALUE;
  const float temperatureC = (1.0f / inverseKelvin) - 273.15f;

  if (!tempSensorIsValid(temperatureC)) {
    lastReadingOnline = false;
    return NAN;
  }

  return temperatureC;
}

bool tempSensorIsValid(float temperatureC) {
  return isfinite(temperatureC) && temperatureC >= -40.0f && temperatureC <= 125.0f;
}

bool tempSensorIsOnline() {
  return lastReadingOnline;
}

uint16_t tempSensorRawAdc() {
  return lastRawAdc;
}

uint16_t tempSensorMilliVolts() {
  return lastMilliVolts;
}

float tempSensorResistanceOhm() {
  return lastResistanceOhm;
}
