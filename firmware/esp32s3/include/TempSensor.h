#pragma once

#include <Arduino.h>

// Common 3-pin analog NTC module wiring:
// VCC -> ESP32-S3 3V3, GND -> GND, AO/S -> GPIO4 (ADC1_CH3).
// The defaults match a 10k NTC (B=3950) module with a 10k divider resistor.
constexpr uint8_t TEMP_SENSOR_PIN = 4;
constexpr float NTC_NOMINAL_RESISTANCE_OHM = 10000.0f;
constexpr float NTC_NOMINAL_TEMPERATURE_C = 25.0f;
constexpr float NTC_BETA_VALUE = 3950.0f;
constexpr float NTC_DIVIDER_RESISTANCE_OHM = 10000.0f;
// Measure the actual PCB 3V3 rail and adjust this value during calibration.
constexpr float NTC_DIVIDER_SUPPLY_MV = 3300.0f;

// PCB1 actual divider: 3V3--10k fixed resistor--AO(GPIO4)--NTC--GND.
// Therefore the fixed resistor is NOT the resistor connected to GND.
constexpr bool NTC_FIXED_RESISTOR_TO_GND = false;

bool tempSensorBegin();
float tempSensorReadC();
bool tempSensorIsValid(float temperatureC);
bool tempSensorIsOnline();

// Diagnostic values from the most recent reading.
uint16_t tempSensorRawAdc();
uint16_t tempSensorMilliVolts();
float tempSensorResistanceOhm();
