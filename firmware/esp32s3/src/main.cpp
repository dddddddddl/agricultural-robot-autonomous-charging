#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "EmergencyStop.h"
#include "RelayControl.h"
#include "MicroSwitchSensor.h"
#include "ToFSensor.h"
#include "INA226Sensor.h"
#include "TempSensor.h"
#include "LedIndicator.h"
#include "BuzzerModule.h"
#include "TftDisplayModule.h"
#include "TftKnobModule.h"
#include "WiFiDockModule.h"

#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef DOCK_WIFI_SSID
#define DOCK_WIFI_SSID ""
#endif
#ifndef DOCK_WIFI_PASSWORD
#define DOCK_WIFI_PASSWORD ""
#endif
#ifndef DOCK_BACKEND_URL
#define DOCK_BACKEND_URL "http://192.168.1.100:8000"
#endif

// =======================================================
// ESP32-S3 integrated smart charging dock, Safety V11.1
// Integrated modules:
// 1. Emergency stop
// 2. Relay
// 3. Contact micro-switch
// 4. VL53L1X time-of-flight distance sensor
// 5. INA226 voltage/current sensing
// 6. Analog NTC temperature sensing on GPIO4
// 7. Status LEDs
// 8. Active buzzer
// 9. TFT display and rotary encoder/buttons
//
// Display/input boundary:
// The TFT and encoder support display and input diagnostics.
// Encoder/button inputs cannot bypass local relay safety checks.
// =======================================================


// ==========================
// 1. GPIO configuration
// ==========================

constexpr uint8_t PIN_EMERGENCY_STOP = 42;

constexpr uint8_t PIN_RELAY = 21;
constexpr int RELAY_ACTIVE_LEVEL = HIGH;

constexpr uint8_t PIN_MICRO_SWITCH = 1;
constexpr bool MICRO_SWITCH_ACTIVE_LOW = true;
constexpr unsigned long MICRO_SWITCH_DEBOUNCE_MS = 80;

// VL53L1X and INA226 share the same I2C0 (Wire) bus.
constexpr uint8_t PIN_I2C_SDA = 13;
constexpr uint8_t PIN_I2C_SCL = 14;
constexpr uint8_t INA226_ADDRESS = 0x40;

constexpr uint8_t PIN_NTC = TEMP_SENSOR_PIN;

constexpr uint8_t PIN_BUZZER = 2;
// Three-pin active buzzer: VCC / GND / IN.
// Active-high wiring: IN=HIGH sounds; IN=LOW stops.
constexpr bool BUZZER_ACTIVE_HIGH = true;

// TFT ST7789 SPI
constexpr uint8_t TFT_SCK = 5;
constexpr uint8_t TFT_MOSI = 6;
constexpr uint8_t TFT_RST = 7;
constexpr uint8_t TFT_DC = 15;
constexpr uint8_t TFT_CS = 16;

// TFT encoder and buttons
constexpr uint8_t KNOB_A = 17;
constexpr uint8_t KNOB_B = 18;
constexpr uint8_t KNOB_PUSH = 8;
constexpr uint8_t KNOB_K0 = 9;


// ==========================
// Wi-Fi configuration for telemetry and command polling
// ==========================

// Enable or temporarily disable the Wi-Fi module.
constexpr bool WIFI_MODULE_ENABLED = true;

// Set the 2.4 GHz network name in the ignored local_config.h.
const char* WIFI_SSID = DOCK_WIFI_SSID;

// Set the Wi-Fi password in the ignored local_config.h.
const char* WIFI_PASSWORD = DOCK_WIFI_PASSWORD;

// Set the backend URL in local_config.h for the current LAN.
// Use the computer's LAN address with backend port 8000.
const char* WIFI_BACKEND_URL = DOCK_BACKEND_URL;

// Connect to Wi-Fi automatically after boot.
// Relay, contact, buzzer, sensors and state-machine behavior are unchanged.
constexpr bool WIFI_AUTO_START_ON_BOOT = true;
constexpr unsigned long WIFI_AUTO_START_DELAY_MS = 3000;

// Firmware tag for identifying the automatic Wi-Fi connection build.
const char* FIRMWARE_TAG = "SAFETY_V11_1_BEEPER_5X3_001";


// ==========================
// 2. Threshold configuration
// ==========================

constexpr float INA226_SHUNT_OHMS = 0.01f;

constexpr int TOF_MIN_DISTANCE_MM = 1;
constexpr int TOF_MAX_DISTANCE_MM = 150;

constexpr float INA_MIN_BUS_VOLTAGE_V = 20.0f;
constexpr float INA_MAX_BUS_VOLTAGE_V = 28.0f;
constexpr float INA_MAX_ABS_CURRENT_A = 3.0f;

// INA226 backup charge-complete thresholds require vehicle calibration.
// Require minimum charging time, sufficient voltage and sustained low absolute current.
// This backup stop does not replace the vehicle charger or BMS.
constexpr bool INA_CHARGE_COMPLETE_ENABLED = true;
constexpr float INA_CHARGE_COMPLETE_MIN_VOLTAGE_V = 25.2f;
constexpr float INA_CHARGE_COMPLETE_MAX_ABS_CURRENT_A = 0.20f;
constexpr unsigned long INA_CHARGE_COMPLETE_MIN_CHARGING_MS = 60000;
constexpr unsigned long INA_CHARGE_COMPLETE_HOLD_MS = 90000;

constexpr float TEMP_WARNING_C = 45.0f;
constexpr float TEMP_MAX_C = 55.0f;

constexpr unsigned long TOF_UPDATE_INTERVAL_MS = 200;
constexpr unsigned long INA226_UPDATE_INTERVAL_MS = 300;
constexpr unsigned long TEMP_UPDATE_INTERVAL_MS = 1000;
constexpr unsigned long TFT_REFRESH_INTERVAL_MS = 8000;

// Require stable contact beyond the module's internal 80 ms debounce.
constexpr unsigned long MICRO_SWITCH_STABLE_ON_MS = 250;
constexpr unsigned long MICRO_SWITCH_STABLE_OFF_MS = 300;

// All startup conditions must remain valid for this duration before relay activation.
constexpr unsigned long AUTO_START_STABLE_MS = 600;

// Confirm ordinary charging interruptions briefly to avoid relay chatter.
// Emergency stop and severe over-temperature still cut power immediately.
constexpr unsigned long CHARGING_UNSAFE_CONFIRM_MS = 600;

// Ordinary contact/position loss sounds five groups of three short beeps.
// Latched FAULT uses BuzzerModule's continuous alarm independently.
constexpr uint8_t CHARGE_INTERRUPTED_BEEP_GROUPS = 5;
constexpr uint8_t CHARGE_INTERRUPTED_BEEPS_PER_GROUP = 3;
constexpr unsigned long CHARGE_INTERRUPTED_BEEP_ON_MS = 100;
constexpr unsigned long CHARGE_INTERRUPTED_BEEP_INNER_GAP_MS = 100;
constexpr unsigned long CHARGE_INTERRUPTED_BEEP_GROUP_GAP_MS = 700;

// Require stable recovery before silencing the interruption reminder.
constexpr unsigned long CHARGE_INTERRUPT_RECOVER_STABLE_MS = 500;

// Suppress interruption alerts briefly after a deliberate Wi-Fi stop command.
constexpr unsigned long WIFI_STOP_COMMAND_IGNORE_ALERT_MS = 3000;

// Upload status automatically while connected; no wifi_upload command is needed.
constexpr unsigned long WIFI_AUTO_UPLOAD_INTERVAL_MS = 5000;


// ==========================
// 3. Module instances
// ==========================

EmergencyStop emergencyStopButton(PIN_EMERGENCY_STOP, true);
RelayControl relay(PIN_RELAY, RELAY_ACTIVE_LEVEL);
MicroSwitchSensor microSwitch(PIN_MICRO_SWITCH, MICRO_SWITCH_ACTIVE_LOW, MICRO_SWITCH_DEBOUNCE_MS);
ToFSensor tofSensor(Wire);

// Both sensors use the same Wire (I2C0) instance.
INA226Sensor ina226Sensor(Wire, INA226_ADDRESS);

BuzzerModule buzzer(PIN_BUZZER, BUZZER_ACTIVE_HIGH);

TftDisplayModule tftDisplay(
  TFT_CS,
  TFT_DC,
  TFT_RST,
  TFT_SCK,
  TFT_MOSI
);

TftKnobModule tftKnob(
  KNOB_A,
  KNOB_B,
  KNOB_PUSH,
  KNOB_K0
);

WiFiDockModule wifiDock(
  WIFI_SSID,
  WIFI_PASSWORD,
  WIFI_BACKEND_URL
);

// Tracks whether automatic startup or a serial command has started Wi-Fi.
bool wifiStartedByUser = false;


// ==========================
// 4. System state machine
// ==========================

enum SystemState {
  STATE_IDLE,
  STATE_WAIT_CAR,
  STATE_POSITION_CHECK,
  STATE_SAFETY_CHECK,
  STATE_READY_TO_CHARGE,
  STATE_CHARGING,
  STATE_COMPLETE,
  STATE_STOPPED,
  STATE_FAULT
};

SystemState currentState = STATE_IDLE;
String faultCode = "NONE";


// ==========================
// 5. Runtime data
// ==========================

bool emergencyStopTriggered = false;
bool microSwitchPressed = false;

int tofDistanceMm = -1;
bool tofDistanceOK = false;

bool ina226Online = false;
bool ina226ReadOK = false;
bool ina226VoltageOK = false;
bool ina226CurrentOK = false;

float inaBusVoltageV = 0.0f;
float inaShuntVoltageMv = 0.0f;
float inaCurrentA = 0.0f;
float inaPowerW = 0.0f;

bool tempSensorOnline = false;
bool tempReadOK = false;
bool tempWarning = false;
bool tempOK = false;
float temperatureC = NAN;

bool forceHighTemperatureTest = false;
bool forceWarningTemperatureTest = false;
bool forceYellowLedTest = false;

unsigned long lastTofUpdateTime = 0;
unsigned long lastIna226UpdateTime = 0;
unsigned long lastTempUpdateTime = 0;
unsigned long lastTftRefreshTime = 0;

bool relayPulseActive = false;
unsigned long relayPulseOffTime = 0;

bool faultAlarmStarted = false;

// Latch stable micro-switch contact to suppress contact bounce.
bool microSwitchStablePressed = false;
unsigned long microSwitchPressedSince = 0;
unsigned long microSwitchReleasedSince = 0;

// Activate automatically only after all conditions remain stable.
unsigned long autoReadySince = 0;
unsigned long lastAutoOnTime = 0;

// Timer for the high-voltage, sustained-low-current charge-complete candidate.
unsigned long chargeCompleteCandidateSince = 0;

// Confirm interruptions to prevent repeated OFF/AUTO-ON relay chatter.
unsigned long chargingUnsafeSince = 0;
String chargingUnsafeCode = "NONE";

// Charging interruption alert applies to loss of local safety or position while charging.
// It is separate from latched FAULT and does not change relay activation logic.
bool chargeInterruptedAlertActive = false;
String chargeInterruptedAlertCode = "NONE";
unsigned long chargeInterruptRecoveredSince = 0;

// Timestamp of Wi-Fi STOP_CHARGING distinguishes deliberate and abnormal stops.
unsigned long lastWifiStopCommandMs = 0;

// The main loop directly drives GPIO for the charging interruption alarm.
// Prevent ordinary beeps or state changes from overriding this alarm.
bool chargeInterruptBuzzerOutputOn = false;
unsigned long chargeInterruptBuzzerLastToggleMs = 0;
uint8_t chargeInterruptBuzzerGroup = 0;
uint8_t chargeInterruptBuzzerBeep = 0;
bool chargeInterruptBuzzerPatternFinished = false;

unsigned long lastWifiAutoUploadTime = 0;
uint32_t telemetrySequence = 0;
SystemState trackedState = STATE_IDLE;
unsigned long stateEnteredMs = 0;

String serialCommandBuffer;

// A Wi-Fi stop holds the relay off while the vehicle still presses the switch.
// Release the hold after vehicle/contact departure; a new arrival may charge again.
// Unified stop latch for serial and Wi-Fi STOP commands. It is released only
// after the car leaves or an explicit START_CHARGE/charge request is received.
bool manualStopHoldActive = false;

int knobLeftCount = 0;
int knobRightCount = 0;
int pushPressCount = 0;
int k0PressCount = 0;


// ==========================
// 6. Utility functions
// ==========================

String stateToString(SystemState state) {
  switch (state) {
    case STATE_IDLE:
      return "IDLE";
    case STATE_WAIT_CAR:
      return "WAIT_CAR";
    case STATE_POSITION_CHECK:
      return "POSITION_CHECK";
    case STATE_SAFETY_CHECK:
      return "SAFETY_CHECK";
    case STATE_READY_TO_CHARGE:
      return "READY_TO_CHARGE";
    case STATE_CHARGING:
      return "CHARGING";
    case STATE_COMPLETE:
      return "COMPLETE";
    case STATE_STOPPED:
      return "STOPPED";
    case STATE_FAULT:
      return "FAULT";
    default:
      return "UNKNOWN";
  }
}

bool isTofDistanceOK(int distanceMm) {
  return distanceMm >= TOF_MIN_DISTANCE_MM && distanceMm <= TOF_MAX_DISTANCE_MM;
}

bool isInaVoltageOK(float voltageV) {
  return voltageV >= INA_MIN_BUS_VOLTAGE_V && voltageV <= INA_MAX_BUS_VOLTAGE_V;
}

bool isInaCurrentOK(float currentA) {
  return fabs(currentA) <= INA_MAX_ABS_CURRENT_A;
}

bool isTemperatureOK(float tempC) {
  return tempSensorIsValid(tempC) && tempC <= TEMP_MAX_C;
}

bool isTemperatureWarning(float tempC) {
  return tempSensorIsValid(tempC) && tempC >= TEMP_WARNING_C && tempC <= TEMP_MAX_C;
}

void updateInputs(
  bool forceTofRead,
  bool forceInaRead,
  bool forceTempRead
);

bool isCarApproaching() {
  // In-range ToF without stable micro-switch contact means the vehicle is approaching.
  return tofSensor.isOnline() && tofDistanceOK && !microSwitchStablePressed;
}

bool isCarPositioned() {
  // Require both in-range ToF and stable micro-switch contact to confirm docking.
  return tofSensor.isOnline() && tofDistanceOK && microSwitchStablePressed;
}

bool isElectricalSafetyOK(bool updateFaultCode = true) {
  if (updateFaultCode) {
    updateInputs(true, true, true);
  } else {
    updateInputs(false, false, false);
  }

  if (emergencyStopTriggered) {
    if (updateFaultCode) faultCode = "EMERGENCY_STOP";
    return false;
  }

  if (!ina226Online) {
    if (updateFaultCode) faultCode = "INA226_OFFLINE";
    return false;
  }

  if (!ina226ReadOK) {
    if (updateFaultCode) faultCode = "INA226_READ_FAIL";
    return false;
  }

  if (!ina226VoltageOK) {
    if (updateFaultCode) faultCode = "INA226_VOLTAGE_ABNORMAL";
    return false;
  }

  if (!ina226CurrentOK) {
    if (updateFaultCode) faultCode = "INA226_CURRENT_ABNORMAL";
    return false;
  }

  if (!tempSensorOnline) {
    if (updateFaultCode) faultCode = "TEMP_SENSOR_OFFLINE";
    return false;
  }

  if (!tempReadOK) {
    if (updateFaultCode) faultCode = "TEMP_SENSOR_READ_FAILED";
    return false;
  }

  if (!tempOK) {
    if (updateFaultCode) faultCode = "TEMPERATURE_ABNORMAL";
    return false;
  }

  if (updateFaultCode) faultCode = "NONE";
  return true;
}

bool localFaultConditionsRecovered() {
  // Reset requires fresh, safe readings from every source that can latch a
  // fault. Car position is intentionally checked later, before relay pickup.
  updateInputs(true, true, true);
  if (!isElectricalSafetyOK(true)) return false;

  if (!tofSensor.isOnline()) {
    faultCode = "TOF_SENSOR_OFFLINE";
    return false;
  }

  faultCode = "NONE";
  return true;
}

bool isPositionReady(bool updateFaultCode = true) {
  if (updateFaultCode) {
    updateInputs(true, true, true);
  } else {
    updateInputs(false, false, false);
  }

  if (!microSwitchStablePressed) {
    if (updateFaultCode) faultCode = "MICRO_SWITCH_NOT_PRESSED";
    return false;
  }

  if (!tofSensor.isOnline()) {
    if (updateFaultCode) faultCode = "TOF_SENSOR_OFFLINE";
    return false;
  }

  if (!tofDistanceOK) {
    if (updateFaultCode) faultCode = "TOF_DISTANCE_ABNORMAL";
    return false;
  }

  return true;
}

// Check every prerequisite sensor and interlock before automatic relay activation.
bool sensorsReadyForAutoRelay(bool forceRead = false) {
  updateInputs(forceRead, forceRead, forceRead);

  if (emergencyStopTriggered) return false;

  // A deliberate Wi-Fi stop prevents immediate restart while contact is held.
  if (manualStopHoldActive) return false;

  // Automatic startup requires stable micro-switch contact.
  // This prevents accidental reactivation while the state machine changes state.
  if (!microSwitchStablePressed) return false;

  if (!tofSensor.isOnline() || !tofDistanceOK) return false;
  if (!ina226Online || !ina226ReadOK || !ina226VoltageOK || !ina226CurrentOK) return false;
  if (!tempSensorOnline || !tempReadOK || !tempOK) return false;

  return true;
}

bool shouldEnterFaultForSafety(const String& code) {
  return code == "EMERGENCY_STOP" ||
         code == "TEMPERATURE_ABNORMAL" ||
         code == "TEMP_SENSOR_OFFLINE" ||
         code == "TEMP_SENSOR_READ_FAILED" ||
         code == "TEMP_SENSOR_INIT_FAILED" ||
         code == "INA226_INIT_FAILED" ||
         code == "INA226_OFFLINE" ||
         code == "INA226_READ_FAIL" ||
         code == "INA226_VOLTAGE_ABNORMAL" ||
         code == "INA226_CURRENT_ABNORMAL" ||
         code == "TOF_SENSOR_INIT_FAILED" ||
         code == "TOF_SENSOR_OFFLINE";
}

void updateEmergencyStop() {
  emergencyStopButton.update();
  emergencyStopTriggered = emergencyStopButton.isTriggered();
}

void updateMicroSwitch() {
  microSwitch.update();
  microSwitchPressed = microSwitch.isPressed();

  unsigned long now = millis();

  if (microSwitchPressed) {
    if (microSwitchPressedSince == 0) {
      microSwitchPressedSince = now;
    }
    microSwitchReleasedSince = 0;

    if ((now - microSwitchPressedSince) >= MICRO_SWITCH_STABLE_ON_MS) {
      microSwitchStablePressed = true;
    }
  } else {
    if (microSwitchReleasedSince == 0) {
      microSwitchReleasedSince = now;
    }
    microSwitchPressedSince = 0;

    // Charging stop is asymmetric: once the debounced contact releases,
    // invalidate the stable contact immediately. Do not add the normal 300 ms
    // release confirmation while the relay is energized.
    if (relay.isOn() || currentState == STATE_CHARGING) {
      microSwitchStablePressed = false;
    } else if ((now - microSwitchReleasedSince) >= MICRO_SWITCH_STABLE_OFF_MS) {
      microSwitchStablePressed = false;
    }
  }
}

void updateTofDistance(bool forceRead = false) {
  unsigned long now = millis();

  if (!forceRead && (now - lastTofUpdateTime < TOF_UPDATE_INTERVAL_MS)) {
    return;
  }

  lastTofUpdateTime = now;

  int distance = tofSensor.readDistanceMm();

  if (distance > 0) {
    tofDistanceMm = distance;
  } else {
    tofDistanceMm = -1;
  }

  tofDistanceOK = tofSensor.isOnline() && isTofDistanceOK(tofDistanceMm);
}

void updateIna226Reading(bool forceRead = false) {
  unsigned long now = millis();

  if (!forceRead && (now - lastIna226UpdateTime < INA226_UPDATE_INTERVAL_MS)) {
    return;
  }

  lastIna226UpdateTime = now;

  // readMeasurement() also performs a rate-limited recovery attempt when the
  // device was marked offline.
  ina226ReadOK = ina226Sensor.readMeasurement();
  ina226Online = ina226Sensor.isOnline();

  if (!ina226Online || !ina226ReadOK) {
    ina226VoltageOK = false;
    ina226CurrentOK = false;
    return;
  }

  inaBusVoltageV = ina226Sensor.getBusVoltageV();
  inaShuntVoltageMv = ina226Sensor.getShuntVoltageMv();
  inaCurrentA = ina226Sensor.getCurrentA();
  inaPowerW = ina226Sensor.getPowerW();

  ina226VoltageOK = isInaVoltageOK(inaBusVoltageV);
  ina226CurrentOK = isInaCurrentOK(inaCurrentA);
}

void updateTemperatureReading(bool forceRead = false) {
  unsigned long now = millis();

  if (!forceRead && (now - lastTempUpdateTime < TEMP_UPDATE_INTERVAL_MS)) {
    return;
  }

  lastTempUpdateTime = now;

  if (forceHighTemperatureTest) {
    temperatureC = 75.0f;
  } else if (forceWarningTemperatureTest) {
    temperatureC = 48.0f;
  } else {
    temperatureC = tempSensorReadC();
  }

  tempSensorOnline = tempSensorIsOnline();

  if (!tempSensorOnline && !forceHighTemperatureTest && !forceWarningTemperatureTest) {
    tempReadOK = false;
    tempWarning = false;
    tempOK = false;
    temperatureC = NAN;
    return;
  }

  tempReadOK = tempSensorIsValid(temperatureC);
  tempWarning = isTemperatureWarning(temperatureC);
  tempOK = isTemperatureOK(temperatureC);
}

void updateInputs(
  bool forceTofRead = false,
  bool forceInaRead = false,
  bool forceTempRead = false
) {
  updateEmergencyStop();
  updateMicroSwitch();
  updateTofDistance(forceTofRead);
  updateIna226Reading(forceInaRead);
  updateTemperatureReading(forceTempRead);
}

void writeBuzzerGpioDirect(bool on) {
  // Active-high module: on=true drives HIGH; on=false drives LOW.
  // Do not use tone/PWM for this active buzzer module.
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_HIGH ? (on ? HIGH : LOW) : (on ? LOW : HIGH));
}

void startChargeInterruptBuzzerDirect() {
  // Cancel ordinary beeps so BuzzerModule cannot overwrite the direct GPIO output.
  buzzer.off();

  chargeInterruptBuzzerOutputOn = true;
  chargeInterruptBuzzerLastToggleMs = millis();
  chargeInterruptBuzzerGroup = 1;
  chargeInterruptBuzzerBeep = 1;
  chargeInterruptBuzzerPatternFinished = false;
  writeBuzzerGpioDirect(true);
}

void stopChargeInterruptBuzzerDirect() {
  chargeInterruptBuzzerOutputOn = false;
  chargeInterruptBuzzerLastToggleMs = 0;
  chargeInterruptBuzzerGroup = 0;
  chargeInterruptBuzzerBeep = 0;
  chargeInterruptBuzzerPatternFinished = false;
  writeBuzzerGpioDirect(false);
}

void updateChargeInterruptBuzzerDirect() {
  if (!chargeInterruptedAlertActive) {
    return;
  }

  unsigned long now = millis();

  // Sound immediately if an interruption alert has just started.
  if (chargeInterruptBuzzerLastToggleMs == 0) {
    startChargeInterruptBuzzerDirect();
    return;
  }

  if (chargeInterruptBuzzerOutputOn) {
    if ((now - chargeInterruptBuzzerLastToggleMs) < CHARGE_INTERRUPTED_BEEP_ON_MS) return;

    chargeInterruptBuzzerOutputOn = false;
    chargeInterruptBuzzerLastToggleMs = now;
    writeBuzzerGpioDirect(false);

    if (chargeInterruptBuzzerGroup >= CHARGE_INTERRUPTED_BEEP_GROUPS &&
        chargeInterruptBuzzerBeep >= CHARGE_INTERRUPTED_BEEPS_PER_GROUP) {
      chargeInterruptBuzzerPatternFinished = true;
    }
    return;
  }

  if (chargeInterruptBuzzerPatternFinished) return;

  const bool groupFinished =
    chargeInterruptBuzzerBeep >= CHARGE_INTERRUPTED_BEEPS_PER_GROUP;
  const unsigned long silenceMs = groupFinished
    ? CHARGE_INTERRUPTED_BEEP_GROUP_GAP_MS
    : CHARGE_INTERRUPTED_BEEP_INNER_GAP_MS;

  if ((now - chargeInterruptBuzzerLastToggleMs) < silenceMs) return;

  if (groupFinished) {
    chargeInterruptBuzzerGroup++;
    chargeInterruptBuzzerBeep = 1;
  } else {
    chargeInterruptBuzzerBeep++;
  }

  chargeInterruptBuzzerOutputOn = true;
  chargeInterruptBuzzerLastToggleMs = now;
  writeBuzzerGpioDirect(true);
}

void forcePowerOff() {
  relay.forceOff();
  relayPulseActive = false;
  chargeCompleteCandidateSince = 0;
}

bool wasRecentlyStoppedByWiFiCommand() {
  if (lastWifiStopCommandMs == 0) {
    return false;
  }

  return (millis() - lastWifiStopCommandMs) <= WIFI_STOP_COMMAND_IGNORE_ALERT_MS;
}

void stopChargingInterruptAlert(const char* reason) {
  if (!chargeInterruptedAlertActive) {
    return;
  }

  chargeInterruptedAlertActive = false;
  chargeInterruptedAlertCode = "NONE";
  chargeInterruptRecoveredSince = 0;
  stopChargeInterruptBuzzerDirect();

  // Stop only the intermittent reminder; enterFault() controls genuine fault alarms.
  if (currentState != STATE_FAULT && buzzer.isAlarmActive()) {
    buzzer.stopAlarm();
  }

  Serial.print("[BUZZER] charging interrupt alert OFF: ");
  Serial.println(reason);
}

void markWiFiStopCommandReceived() {
  lastWifiStopCommandMs = millis();
  stopChargingInterruptAlert("WiFi STOP_CHARGING command received");
}

void startChargingInterruptAlert(const String& code) {
  // Do not report a deliberate Wi-Fi stop as an abnormal interruption.
  if (wasRecentlyStoppedByWiFiCommand()) {
    return;
  }

  // Leave the buzzer to enterFault() when a real fault occurs.
  if (currentState == STATE_FAULT) {
    return;
  }

  chargeInterruptedAlertActive = true;
  chargeInterruptedAlertCode = code;
  chargeInterruptRecoveredSince = 0;

  // Drive intermittent reminders directly to prevent ordinary beeps overriding them.
  startChargeInterruptBuzzerDirect();

  Serial.print("[BUZZER] charging interrupt alert ON: ");
  Serial.println(code);
}

void updateChargingInterruptAlert() {
  if (!chargeInterruptedAlertActive) {
    return;
  }

  if (chargeInterruptBuzzerPatternFinished) {
    stopChargingInterruptAlert("5 groups x 3 beeps finished");
    return;
  }

  // A hard fault supersedes and clears the ordinary interruption reminder.
  if (currentState == STATE_FAULT) {
    chargeInterruptedAlertActive = false;
    chargeInterruptedAlertCode = "NONE";
    chargeInterruptRecoveredSince = 0;
    return;
  }

  // Normal recovery requires stable contact, valid ToF, INA226, temperature and E-stop.
  // Relay reactivation is not required to silence the reminder after recovery.
  bool recoveredNow = sensorsReadyForAutoRelay(false);
  unsigned long now = millis();

  if (recoveredNow) {
    if (chargeInterruptRecoveredSince == 0) {
      chargeInterruptRecoveredSince = now;
    }

    if ((now - chargeInterruptRecoveredSince) >= CHARGE_INTERRUPT_RECOVER_STABLE_MS) {
      stopChargingInterruptAlert("local safety/position restored");
    }

    return;
  }

  chargeInterruptRecoveredSince = 0;

  // Keep the intermittent GPIO reminder active while the alert remains latched.
  // updateBuzzerStatus() applies the actual on/off timing.
  if (chargeInterruptBuzzerLastToggleMs == 0) {
    startChargeInterruptBuzzerDirect();
  }
}

void resetStateMachineToIdle(const char* reason) {
  forcePowerOff();
  currentState = STATE_IDLE;
  faultCode = "NONE";
  autoReadySince = 0;
  chargingUnsafeSince = 0;
  chargingUnsafeCode = "NONE";
  stopChargingInterruptAlert("state machine reset");
  stopChargeInterruptBuzzerDirect();
  buzzer.stopAlarm();
  faultAlarmStarted = false;
  buzzer.successBeep();

  Serial.print("[RESET] ");
  Serial.println(reason);
}

void enterFault(const String& code) {
  forcePowerOff();
  faultCode = code;
  currentState = STATE_FAULT;
  autoReadySince = 0;
  chargingUnsafeSince = 0;
  chargingUnsafeCode = code;

  // Stop ordinary interruption beeps before a hard-fault alarm takes over.
  chargeInterruptedAlertActive = false;
  chargeInterruptedAlertCode = "NONE";
  chargeInterruptRecoveredSince = 0;
  stopChargeInterruptBuzzerDirect();

  if (!faultAlarmStarted) {
    buzzer.startFaultAlarm(120, 80);
    faultAlarmStarted = true;
  }

  Serial.print("[FAULT] ");
  Serial.println(faultCode);
  Serial.println("[SAFETY] Relay forced OFF. Fix the problem, then use reset.");
}

bool enforceRelaySafetyInvariant() {
  // No state except CHARGING may ever retain relay power. A corrupted state is
  // treated as a hard fault rather than silently returning to IDLE.
  if (currentState != STATE_CHARGING && relay.isOn()) {
    enterFault("RELAY_ON_OUTSIDE_CHARGING");
    return false;
  }

  if (currentState == STATE_CHARGING && manualStopHoldActive) {
    forcePowerOff();
    currentState = STATE_STOPPED;
    faultCode = "NONE";
    return false;
  }

  return true;
}

void handleChargingContactLoss(const String& code) {
  forcePowerOff();
  currentState = STATE_IDLE;
  faultCode = "NONE";
  autoReadySince = 0;
  chargingUnsafeSince = 0;
  chargingUnsafeCode = "NONE";
  startChargingInterruptAlert(code);
  Serial.print("[SAFETY] charging contact/position lost, relay OFF: ");
  Serial.println(code);
}

void updateStateDurationTracking() {
  if (trackedState != currentState) {
    trackedState = currentState;
    stateEnteredMs = millis();
  }
}


// ==========================
// 7. Safety evaluation
// ==========================

bool chargeSafetyOK(
  bool updateFaultCode = true,
  bool forceTofRead = true,
  bool forceInaRead = true,
  bool forceTempRead = true
) {
  updateInputs(forceTofRead, forceInaRead, forceTempRead);

  if (emergencyStopTriggered) {
    if (updateFaultCode) faultCode = "EMERGENCY_STOP";
    return false;
  }

  if (!microSwitchStablePressed) {
    if (updateFaultCode) faultCode = "MICRO_SWITCH_NOT_PRESSED";
    return false;
  }

  if (!tofSensor.isOnline()) {
    if (updateFaultCode) faultCode = "TOF_SENSOR_OFFLINE";
    return false;
  }

  if (!tofDistanceOK) {
    if (updateFaultCode) faultCode = "TOF_DISTANCE_ABNORMAL";
    return false;
  }

  if (!ina226Online) {
    if (updateFaultCode) faultCode = "INA226_OFFLINE";
    return false;
  }

  if (!ina226ReadOK) {
    if (updateFaultCode) faultCode = "INA226_READ_FAIL";
    return false;
  }

  if (!ina226VoltageOK) {
    if (updateFaultCode) faultCode = "INA226_VOLTAGE_ABNORMAL";
    return false;
  }

  if (!ina226CurrentOK) {
    if (updateFaultCode) faultCode = "INA226_CURRENT_ABNORMAL";
    return false;
  }

  if (!tempSensorOnline) {
    if (updateFaultCode) faultCode = "TEMP_SENSOR_OFFLINE";
    return false;
  }

  if (!tempReadOK) {
    if (updateFaultCode) faultCode = "TEMP_SENSOR_READ_FAILED";
    return false;
  }

  if (!tempOK) {
    if (updateFaultCode) faultCode = "TEMPERATURE_ABNORMAL";
    return false;
  }

  if (updateFaultCode) {
    faultCode = "NONE";
  }

  return true;
}

void startAutoCharging(const char* reason) {
  relay.set(true);
  relayPulseActive = false;
  currentState = STATE_CHARGING;
  faultCode = "NONE";
  lastAutoOnTime = millis();
  chargeCompleteCandidateSince = 0;
  autoReadySince = 0;
  chargingUnsafeSince = 0;
  chargingUnsafeCode = "NONE";
  stopChargingInterruptAlert("auto charging resumed");
  buzzer.successBeep();

  Serial.print("[AUTO] relay ON, state CHARGING. reason=");
  Serial.println(reason);
}

bool updateInaChargeCompleteDetection(unsigned long now) {
  if (!INA_CHARGE_COMPLETE_ENABLED ||
      currentState != STATE_CHARGING ||
      !relay.isOn() ||
      !ina226Online ||
      !ina226ReadOK) {
    chargeCompleteCandidateSince = 0;
    return false;
  }

  // Avoid treating startup transients or an immediately open load as a
  // completed charge. This value is intentionally conservative and should be
  // tuned together with the voltage/current thresholds after real logging.
  if ((now - lastAutoOnTime) < INA_CHARGE_COMPLETE_MIN_CHARGING_MS) {
    chargeCompleteCandidateSince = 0;
    return false;
  }

  const bool voltageInCompleteRange =
    inaBusVoltageV >= INA_CHARGE_COMPLETE_MIN_VOLTAGE_V &&
    inaBusVoltageV <= INA_MAX_BUS_VOLTAGE_V;
  const bool currentInCompleteRange =
    fabsf(inaCurrentA) <= INA_CHARGE_COMPLETE_MAX_ABS_CURRENT_A;

  if (!voltageInCompleteRange || !currentInCompleteRange) {
    if (chargeCompleteCandidateSince != 0) {
      Serial.println("[COMPLETE] candidate cancelled: voltage/current left completion range.");
    }
    chargeCompleteCandidateSince = 0;
    return false;
  }

  if (chargeCompleteCandidateSince == 0) {
    chargeCompleteCandidateSince = now;
    Serial.print("[COMPLETE] candidate started: voltage=");
    Serial.print(inaBusVoltageV, 3);
    Serial.print(" V, current=");
    Serial.print(inaCurrentA, 4);
    Serial.println(" A");
    return false;
  }

  if ((now - chargeCompleteCandidateSince) < INA_CHARGE_COMPLETE_HOLD_MS) {
    return false;
  }

  forcePowerOff();
  currentState = STATE_COMPLETE;
  faultCode = "NONE";
  autoReadySince = 0;
  chargingUnsafeSince = 0;
  chargingUnsafeCode = "NONE";
  stopChargingInterruptAlert("INA226 charge complete");
  buzzer.doubleBeep();

  Serial.println("[COMPLETE] INA226 voltage/current condition held long enough; relay OFF and COMPLETE latched.");
  return true;
}

void updateStateMachine() {
  if (!enforceRelaySafetyInvariant()) return;

  // Read the two direct safety inputs before any I2C operation. A stalled I2C
  // device must not delay emergency-stop or contact-loss handling.
  updateEmergencyStop();
  if (emergencyStopTriggered) {
    enterFault("EMERGENCY_STOP");
    return;
  }

  updateMicroSwitch();
  if (currentState == STATE_CHARGING && !microSwitchPressed) {
    handleChargingContactLoss("MICRO_SWITCH_NOT_PRESSED");
    return;
  }

  updateTofDistance(false);
  updateIna226Reading(false);
  updateTemperatureReading(false);
  unsigned long now = millis();

  // Release the Wi-Fi stop hold only after vehicle/contact departure.
  if (manualStopHoldActive && !microSwitchStablePressed) {
    manualStopHoldActive = false;
    Serial.println("[STOP] manual stop hold released: micro switch released.");
  }

  // Emergency stop has highest priority: immediately open the relay and latch FAULT.
  if (emergencyStopTriggered && currentState != STATE_FAULT) {
    enterFault("EMERGENCY_STOP");
    return;
  }

  // FAULT is latched and does not recover automatically.
  // This prevents repeated fault/reconnect cycles when a fault briefly clears.
  if (currentState == STATE_FAULT) {
    forcePowerOff();
    autoReadySince = 0;
    chargingUnsafeSince = 0;
    return;
  }

  // Hard sensor or electrical faults outside charging enter FAULT directly.
  // An unpressed micro-switch alone is not a hard fault.
  if (!isElectricalSafetyOK(false)) {
    // Refresh sensors and faultCode so FAULT is not displayed with code NONE.
    isElectricalSafetyOK(true);
    enterFault(faultCode);
    return;
  }

  // Automatic startup:
  // 1. No serial charge command is required.
  // 2. Stable micro-switch contact is mandatory.
  // 3. ToF, INA226, temperature and E-stop checks must all pass.
  // 4. Conditions must remain valid for AUTO_START_STABLE_MS before relay ON.
  bool autoReadyNow = sensorsReadyForAutoRelay(false);

  if (currentState == STATE_READY_TO_CHARGE && autoReadyNow) {
    if (autoReadySince == 0) {
      autoReadySince = now;
    }

    if ((now - autoReadySince) >= AUTO_START_STABLE_MS) {
      // Force a fresh sensor read immediately before activation.
      if (chargeSafetyOK(true, true, true, true)) {
        startAutoCharging("READY_TO_CHARGE safety stable");
        return;
      }

      autoReadySince = 0;
    }
  } else {
    autoReadySince = 0;
  }

  switch (currentState) {
    case STATE_IDLE:
      if (isCarPositioned()) {
        currentState = STATE_POSITION_CHECK;
      } else if (isCarApproaching()) {
        currentState = STATE_WAIT_CAR;
      }
      break;

    case STATE_WAIT_CAR:
      if (isCarPositioned()) {
        currentState = STATE_POSITION_CHECK;
      } else if (!isCarApproaching()) {
        currentState = STATE_IDLE;
      }
      break;

    case STATE_POSITION_CHECK:
      // Check confirmed docking before checking whether the vehicle is approaching.
      // Stable contact makes isCarApproaching() false; checking approach first would
      // incorrectly return to IDLE and cause relay/state chatter.
      if (isCarPositioned()) {
        currentState = STATE_SAFETY_CHECK;
      } else if (isCarApproaching()) {
        currentState = STATE_WAIT_CAR;
      } else {
        currentState = STATE_IDLE;
      }
      break;

    case STATE_SAFETY_CHECK:
      if (!isCarPositioned()) {
        currentState = isCarApproaching() ? STATE_WAIT_CAR : STATE_IDLE;
      } else if (!chargeSafetyOK(true, false, false, false)) {
        if (shouldEnterFaultForSafety(faultCode)) {
          enterFault(faultCode);
        } else {
          currentState = STATE_POSITION_CHECK;
        }
      } else {
        currentState = STATE_READY_TO_CHARGE;
      }
      break;

    case STATE_READY_TO_CHARGE:
      if (!isCarPositioned()) {
        currentState = isCarApproaching() ? STATE_WAIT_CAR : STATE_IDLE;
      }
      // The automatic-start latch above controls actual relay activation.
      break;

    case STATE_CHARGING: {
      if (!relay.isOn()) {
        enterFault("RELAY_UNEXPECTED_OFF");
        break;
      }

      // The contact state below is already debounced by MicroSwitchSensor.
      // During charging, release must cut power without another 300 ms timer.
      if (!microSwitchPressed) {
        handleChargingContactLoss("MICRO_SWITCH_NOT_PRESSED");
        break;
      }

      // Backup charge-complete thresholds are provisional until vehicle calibration.
      // COMPLETE remains latched until the vehicle leaves.
      if (updateInaChargeCompleteDetection(now)) {
        break;
      }

      if (chargeSafetyOK(true, false, false, false)) {
        chargingUnsafeSince = 0;
        chargingUnsafeCode = "NONE";
        faultCode = "NONE";
        break;
      }

      String codeNow = faultCode;

      // Severe faults cut power immediately without the interruption delay.
      if (codeNow == "EMERGENCY_STOP" ||
          codeNow == "TEMPERATURE_ABNORMAL" ||
          codeNow == "TEMP_SENSOR_OFFLINE" ||
          codeNow == "TEMP_SENSOR_READ_FAILED" ||
          codeNow == "INA226_OFFLINE" ||
          codeNow == "INA226_READ_FAIL") {
        enterFault(codeNow);
        break;
      }

      unsigned long confirmMs = CHARGING_UNSAFE_CONFIRM_MS;
      if (codeNow == "MICRO_SWITCH_NOT_PRESSED") {
        confirmMs = MICRO_SWITCH_STABLE_OFF_MS;
      }

      if (chargingUnsafeSince == 0 || chargingUnsafeCode != codeNow) {
        chargingUnsafeSince = now;
        chargingUnsafeCode = codeNow;
        Serial.print("[WARN] charging unsafe detected, confirming: ");
        Serial.println(codeNow);
        break;
      }

      // Ignore a single transient; act after the confirmation duration expires.
      if ((now - chargingUnsafeSince) < confirmMs) {
        break;
      }

      if (shouldEnterFaultForSafety(codeNow)) {
        enterFault(codeNow);
      } else {
        // Contact loss or invalid ToF opens the relay and returns to waiting without a hard fault.
        // Sound an intermittent reminder unless a deliberate Wi-Fi stop caused the interruption.
        handleChargingContactLoss(codeNow);
      }
      break;
    }

    case STATE_COMPLETE:
      // Charge completion is a normal latched stop, not a FAULT. Only removal
      // of the car/contact arms the next automatic charging session.
      forcePowerOff();
      if (!microSwitchStablePressed) {
        lastAutoOnTime = 0;
        currentState = isCarApproaching() ? STATE_WAIT_CAR : STATE_IDLE;
        Serial.println("[COMPLETE] car/contact released; next charging session armed.");
      }
      break;

    case STATE_STOPPED:
      forcePowerOff();
      if (!manualStopHoldActive) {
        if (isCarPositioned()) {
          currentState = STATE_POSITION_CHECK;
        } else if (isCarApproaching()) {
          currentState = STATE_WAIT_CAR;
        } else {
          currentState = STATE_IDLE;
        }
      }
      break;

    default:
      forcePowerOff();
      enterFault("INVALID_STATE");
      break;
  }
}


// ==========================
// 8. LED / buzzer / TFT updates
// ==========================

void updateLedStatus() {
  ledUpdateHeartbeat(1000);

  if (forceYellowLedTest) {
    ledSet(LED_PROCESS, true);
    return;
  }

  bool safetyNow = chargeSafetyOK(false, false, false, false);

  String ledStateName = stateToString(currentState);

  if (currentState == STATE_FAULT || faultCode != "NONE") {
    ledStateName = "FAULT";
  }

  ledUpdateForDockState(ledStateName, safetyNow, tempWarning);
}

void updateBuzzerStatus() {
  // The non-FAULT interruption reminder overrides ordinary success/double beeps.
  // Drive GPIO directly instead of BuzzerModule.update() for this reminder.
  if (chargeInterruptedAlertActive) {
    updateChargeInterruptBuzzerDirect();
    return;
  }

  // When no reminder is active, release direct control before normal buzzer updates.
  if (chargeInterruptBuzzerOutputOn || chargeInterruptBuzzerLastToggleMs != 0) {
    stopChargeInterruptBuzzerDirect();
  }

  buzzer.update();

  if (currentState != STATE_FAULT && faultCode == "NONE" && buzzer.isAlarmActive()) {
    buzzer.stopAlarm();
    faultAlarmStarted = false;
  }
}

void updateTftDisplayNow() {
  bool safetyNow = chargeSafetyOK(false, false, false, false);

  String screenState = stateToString(currentState);
  if (currentState == STATE_FAULT || faultCode != "NONE") {
    screenState = "FAULT";
  }

  tftDisplay.showStatus(
    screenState,
    faultCode,
    tofDistanceMm,
    tofDistanceOK,
    microSwitchPressed,
    inaBusVoltageV,
    ina226VoltageOK,
    inaCurrentA,
    ina226CurrentOK,
    temperatureC,
    tempOK,
    tempWarning,
    emergencyStopTriggered,
    relay.isOn(),
    buzzer.isAlarmActive()
  );
}

void handleTftKnobEvents() {
  tftKnob.update();

  if (tftKnob.rotatedLeft()) {
    knobLeftCount++;
    Serial.print("[TFT] knob rotate LEFT, count=");
    Serial.println(knobLeftCount);
    tftDisplay.showMessage("KNOB INPUT", "ROTATE LEFT");
  }

  if (tftKnob.rotatedRight()) {
    knobRightCount++;
    Serial.print("[TFT] knob rotate RIGHT, count=");
    Serial.println(knobRightCount);
    tftDisplay.showMessage("KNOB INPUT", "ROTATE RIGHT");
  }

  if (tftKnob.pushPressed()) {
    pushPressCount++;
    Serial.print("[TFT] PUSH pressed, count=");
    Serial.println(pushPressCount);

    tftDisplay.showKnobPage(
      knobLeftCount,
      knobRightCount,
      pushPressCount,
      k0PressCount,
      tftKnob.isPushDown(),
      tftKnob.isK0Down()
    );
  }

  if (tftKnob.k0Pressed()) {
    k0PressCount++;
    Serial.print("[TFT] K0 pressed, count=");
    Serial.println(k0PressCount);

    if (!localFaultConditionsRecovered()) {
      forcePowerOff();
      currentState = STATE_FAULT;
      Serial.print("[BLOCK] K0 reset blocked: local fault remains: ");
      Serial.println(faultCode);
    } else {
      resetStateMachineToIdle("K0 button reset: relay OFF, state IDLE.");
    }

    updateTftDisplayNow();
  }
}


// ==========================
// 9. Relay Pulse
// ==========================

void startRelayPulse(unsigned long durationMs) {
  if (currentState != STATE_READY_TO_CHARGE ||
      faultCode != "NONE" ||
      manualStopHoldActive) {
    Serial.println("[BLOCK] relay_pulse requires READY_TO_CHARGE with no FAULT or manual STOP hold");
    forcePowerOff();
    return;
  }

  if (!chargeSafetyOK(true, true, true, true)) {
    Serial.print("[BLOCK] relay_pulse blocked: ");
    Serial.println(faultCode);
    forcePowerOff();
    return;
  }

  Serial.println("good safe");

  relay.set(true);
  currentState = STATE_CHARGING;
  lastAutoOnTime = millis();
  chargeCompleteCandidateSince = 0;
  relayPulseActive = true;
  relayPulseOffTime = millis() + durationMs;
  buzzer.successBeep();

  Serial.print("[TEST] relay pulse ON for ");
  Serial.print(durationMs);
  Serial.println(" ms");
}

void updateRelayPulse() {
  if (!relayPulseActive) {
    return;
  }

  if (!chargeSafetyOK(true, false, false, false)) {
    enterFault(faultCode);
    return;
  }

  if ((long)(millis() - relayPulseOffTime) >= 0) {
    relay.set(false);
    relayPulseActive = false;
    currentState = STATE_IDLE;
    faultCode = "NONE";
    buzzer.doubleBeep();
    Serial.println("[TEST] relay pulse finished, relay OFF.");
  }
}


// ==========================
// 10. Serial status output
// ==========================

void printTofInfo() {
  updateTofDistance(true);

  Serial.println("---------- TOF DATA ----------");
  Serial.print("ToF online: ");
  Serial.println(tofSensor.isOnline() ? "YES" : "NO");

  Serial.print("ToF distance: ");
  if (tofDistanceMm > 0) {
    Serial.print(tofDistanceMm);
    Serial.println(" mm");
  } else {
    Serial.println("INVALID / READ_FAILED");
  }

  Serial.print("ToF range check: ");
  Serial.println(tofDistanceOK ? "OK" : "NOT_OK");
  Serial.println("------------------------------");
}

void printIna226Info() {
  updateIna226Reading(true);

  Serial.println("---------- INA226 DATA ----------");
  Serial.print("INA226 online: ");
  Serial.println(ina226Online ? "YES" : "NO");

  Serial.print("INA226 address: 0x");
  Serial.println(INA226_ADDRESS, HEX);

  Serial.print("INA226 error: ");
  Serial.println(ina226Sensor.getErrorString());

  Serial.print("INA226 read: ");
  Serial.println(ina226ReadOK ? "OK" : "FAIL");

  if (ina226ReadOK) {
    Serial.print("Bus voltage: ");
    Serial.print(inaBusVoltageV, 3);
    Serial.println(" V");

    Serial.print("Shunt voltage: ");
    Serial.print(inaShuntVoltageMv, 4);
    Serial.println(" mV");

    Serial.print("Current: ");
    Serial.print(inaCurrentA, 4);
    Serial.println(" A");

    Serial.print("Power: ");
    Serial.print(inaPowerW, 4);
    Serial.println(" W");

    Serial.print("Voltage range check: ");
    Serial.println(ina226VoltageOK ? "OK" : "NOT_OK");

    Serial.print("Current range check: ");
    Serial.println(ina226CurrentOK ? "OK" : "NOT_OK");
  }

  Serial.println("---------------------------------");
}

void printTemperatureInfo() {
  updateTemperatureReading(true);

  Serial.println("----------- NTC TEMP DATA ------------");
  Serial.print("NTC module online: ");
  Serial.println(tempSensorOnline ? "YES" : "NO");

  Serial.print("NTC raw ADC: ");
  Serial.println(tempSensorRawAdc());

  Serial.print("NTC calibrated voltage: ");
  Serial.print(tempSensorMilliVolts());
  Serial.println(" mV");

  Serial.print("NTC resistance: ");
  if (isfinite(tempSensorResistanceOhm())) {
    Serial.print(tempSensorResistanceOhm(), 1);
    Serial.println(" ohm");
  } else {
    Serial.println("INVALID");
  }

  Serial.print("Temperature read: ");
  Serial.println(tempReadOK ? "OK" : "FAIL");

  Serial.print("Temperature: ");
  if (tempReadOK) {
    Serial.print(temperatureC, 2);

    if (forceHighTemperatureTest) {
      Serial.println(" C (TEST HIGH TEMP)");
    } else if (forceWarningTemperatureTest) {
      Serial.println(" C (TEST WARNING TEMP)");
    } else {
      Serial.println(" C");
    }
  } else {
    Serial.println("INVALID / READ_FAILED");
  }

  Serial.print("Temperature warning: ");
  Serial.println(tempWarning ? "YES" : "NO");

  Serial.print("Temperature range check: ");
  Serial.println(tempOK ? "OK" : "NOT_OK");
  Serial.println("---------------------------------------");
}

void printBuzzerInfo() {
  Serial.println("---------- BUZZER DATA ----------");
  Serial.print("Buzzer pin: GPIO");
  Serial.println(PIN_BUZZER);

  Serial.print("Buzzer trigger: ");
  Serial.println(BUZZER_ACTIVE_HIGH ? "ACTIVE_HIGH" : "ACTIVE_LOW");

  Serial.print("Buzzer output: ");
  Serial.println(buzzer.isOn() ? "ON" : "OFF");

  Serial.print("Buzzer alarm: ");
  Serial.println(buzzer.isAlarmActive() ? "ACTIVE" : "INACTIVE");

  Serial.print("Buzzer pattern: ");
  Serial.println(buzzer.isPatternActive() ? "ACTIVE" : "INACTIVE");
  Serial.println("---------------------------------");
}

void printKnobInfo() {
  Serial.println("---------- TFT KNOB DATA ----------");
  Serial.print("Rotate left count: ");
  Serial.println(knobLeftCount);

  Serial.print("Rotate right count: ");
  Serial.println(knobRightCount);

  Serial.print("PUSH press count: ");
  Serial.println(pushPressCount);

  Serial.print("K0 press count: ");
  Serial.println(k0PressCount);

  Serial.print("PUSH now: ");
  Serial.println(tftKnob.isPushDown() ? "DOWN" : "UP");

  Serial.print("K0 now: ");
  Serial.println(tftKnob.isK0Down() ? "DOWN" : "UP");

  Serial.println("Pins:");
  Serial.print("  KNOB_A GPIO");
  Serial.println(KNOB_A);
  Serial.print("  KNOB_B GPIO");
  Serial.println(KNOB_B);
  Serial.print("  KNOB_PUSH GPIO");
  Serial.println(KNOB_PUSH);
  Serial.print("  KNOB_K0 GPIO");
  Serial.println(KNOB_K0);

  Serial.println("-----------------------------------");
}

void printRaw() {
  updateInputs(true, true, true);

  Serial.println("---------- RAW DATA ----------");

  Serial.print("Emergency raw GPIO");
  Serial.print(PIN_EMERGENCY_STOP);
  Serial.print(": ");
  Serial.println(emergencyStopButton.rawValue() == HIGH ? "HIGH" : "LOW");

  Serial.print("Emergency logical state: ");
  Serial.println(emergencyStopTriggered ? "PRESSED" : "NORMAL");

  Serial.print("Micro switch raw GPIO");
  Serial.print(PIN_MICRO_SWITCH);
  Serial.print(": ");
  Serial.println(microSwitch.rawLevel() == LOW ? "LOW" : "HIGH");

  Serial.print("Micro switch logical state: ");
  Serial.println(microSwitchPressed ? "PRESSED / CONTACT_OK" : "NOT_PRESSED / CONTACT_NOT_OK");

  Serial.print("Micro switch stable latch: ");
  Serial.println(microSwitchStablePressed ? "STABLE_PRESSED" : "NOT_STABLE");

  Serial.print("ToF distance: ");
  if (tofDistanceMm > 0) {
    Serial.print(tofDistanceMm);
    Serial.println(" mm");
  } else {
    Serial.println("INVALID / READ_FAILED");
  }

  Serial.print("ToF logical state: ");
  Serial.println(tofDistanceOK ? "DISTANCE_OK" : "DISTANCE_NOT_OK");

  Serial.print("INA226 online: ");
  Serial.println(ina226Online ? "YES" : "NO");

  Serial.print("INA226 read: ");
  Serial.println(ina226ReadOK ? "OK" : "FAIL");

  if (ina226ReadOK) {
    Serial.print("INA226 voltage: ");
    Serial.print(inaBusVoltageV, 3);
    Serial.println(" V");

    Serial.print("INA226 current: ");
    Serial.print(inaCurrentA, 4);
    Serial.println(" A");
  }

  Serial.print("NTC module online: ");
  Serial.println(tempSensorOnline ? "YES" : "NO");

  Serial.print("Temperature: ");
  if (tempReadOK) {
    Serial.print(temperatureC, 2);
    Serial.println(" C");
  } else {
    Serial.println("INVALID / READ_FAILED");
  }

  Serial.print("Relay GPIO");
  Serial.print(PIN_RELAY);
  Serial.print(": ");
  Serial.println(relay.isOn() ? "ON" : "OFF");

  Serial.print("Buzzer GPIO");
  Serial.print(PIN_BUZZER);
  Serial.print(": ");
  Serial.println(buzzer.isOn() ? "ON" : "OFF");

  Serial.println("------------------------------");
}

void printStatus() {
  updateInputs(true, true, true);

  Serial.println("========== SAFETY V11.1 STATUS ==========");

  Serial.print("State: ");
  Serial.println(stateToString(currentState));

  Serial.print("Fault: ");
  Serial.println(faultCode);

  Serial.print("Emergency stop: ");
  Serial.println(emergencyStopTriggered ? "PRESSED" : "NORMAL");

  Serial.print("Micro switch: ");
  Serial.println(microSwitchPressed ? "PRESSED / CONTACT_OK" : "NOT_PRESSED / CONTACT_NOT_OK");

  Serial.print("Micro switch stable latch: ");
  Serial.println(microSwitchStablePressed ? "STABLE_PRESSED" : "NOT_STABLE");

  Serial.print("ToF online: ");
  Serial.println(tofSensor.isOnline() ? "YES" : "NO");

  Serial.print("ToF distance: ");
  if (tofDistanceMm > 0) {
    Serial.print(tofDistanceMm);
    Serial.println(" mm");
  } else {
    Serial.println("INVALID / READ_FAILED");
  }

  Serial.print("ToF range check: ");
  Serial.println(tofDistanceOK ? "OK" : "NOT_OK");

  Serial.print("INA226 online: ");
  Serial.println(ina226Online ? "YES" : "NO");

  Serial.print("INA226 read: ");
  Serial.println(ina226ReadOK ? "OK" : "FAIL");

  if (ina226ReadOK) {
    Serial.print("INA226 voltage: ");
    Serial.print(inaBusVoltageV, 3);
    Serial.println(" V");

    Serial.print("INA226 current: ");
    Serial.print(inaCurrentA, 4);
    Serial.println(" A");

    Serial.print("INA226 voltage check: ");
    Serial.println(ina226VoltageOK ? "OK" : "NOT_OK");

    Serial.print("INA226 current check: ");
    Serial.println(ina226CurrentOK ? "OK" : "NOT_OK");
  }

  Serial.print("INA226 completion enabled: ");
  Serial.println(INA_CHARGE_COMPLETE_ENABLED ? "YES" : "NO");

  Serial.print("Completion threshold: V >= ");
  Serial.print(INA_CHARGE_COMPLETE_MIN_VOLTAGE_V, 3);
  Serial.print(" V, |I| <= ");
  Serial.print(INA_CHARGE_COMPLETE_MAX_ABS_CURRENT_A, 3);
  Serial.print(" A for ");
  Serial.print(INA_CHARGE_COMPLETE_HOLD_MS / 1000UL);
  Serial.println(" s");

  Serial.print("Completion candidate: ");
  Serial.println(chargeCompleteCandidateSince != 0 ? "ACTIVE" : "INACTIVE");

  Serial.print("NTC module online: ");
  Serial.println(tempSensorOnline ? "YES" : "NO");

  Serial.print("Temperature: ");
  if (tempReadOK) {
    Serial.print(temperatureC, 2);
    Serial.println(" C");
  } else {
    Serial.println("INVALID / READ_FAILED");
  }

  Serial.print("Temperature warning: ");
  Serial.println(tempWarning ? "YES" : "NO");

  Serial.print("Temperature check: ");
  Serial.println(tempOK ? "OK" : "NOT_OK");

  Serial.print("Charge safety: ");
  Serial.println(chargeSafetyOK(false, false, false, false) ? "OK" : "NOT_OK");

  Serial.print("Relay: ");
  Serial.println(relay.isOn() ? "ON" : "OFF");

  Serial.print("Buzzer: ");
  Serial.println(buzzer.isOn() ? "ON" : "OFF");

  Serial.print("Buzzer alarm: ");
  Serial.println(buzzer.isAlarmActive() ? "ACTIVE" : "INACTIVE");

  Serial.print("Charging interrupt alert: ");
  Serial.println(chargeInterruptedAlertActive ? "ACTIVE" : "INACTIVE");

  Serial.print("Charging interrupt direct GPIO: ");
  Serial.println(chargeInterruptBuzzerOutputOn ? "ON" : "OFF");

  Serial.print("Charging interrupt code: ");
  Serial.println(chargeInterruptedAlertCode);

  Serial.print("Manual stop hold: ");
  Serial.println(manualStopHoldActive ? "ACTIVE" : "INACTIVE");

  Serial.println("Pins:");
  Serial.print("  Emergency Stop GPIO");
  Serial.println(PIN_EMERGENCY_STOP);
  Serial.print("  Relay GPIO");
  Serial.println(PIN_RELAY);
  Serial.print("  Micro Switch GPIO");
  Serial.println(PIN_MICRO_SWITCH);
  Serial.print("  Shared I2C SDA GPIO");
  Serial.println(PIN_I2C_SDA);
  Serial.print("  Shared I2C SCL GPIO");
  Serial.println(PIN_I2C_SCL);
  Serial.print("  NTC AO GPIO");
  Serial.println(PIN_NTC);
  Serial.print("  Buzzer GPIO");
  Serial.println(PIN_BUZZER);

  Serial.println("TFT pins:");
  Serial.print("  TFT_SCK GPIO");
  Serial.println(TFT_SCK);
  Serial.print("  TFT_MOSI GPIO");
  Serial.println(TFT_MOSI);
  Serial.print("  TFT_RST GPIO");
  Serial.println(TFT_RST);
  Serial.print("  TFT_DC GPIO");
  Serial.println(TFT_DC);
  Serial.print("  TFT_CS GPIO");
  Serial.println(TFT_CS);

  Serial.println("Knob pins:");
  Serial.print("  KNOB_A GPIO");
  Serial.println(KNOB_A);
  Serial.print("  KNOB_B GPIO");
  Serial.println(KNOB_B);
  Serial.print("  KNOB_PUSH GPIO");
  Serial.println(KNOB_PUSH);
  Serial.print("  KNOB_K0 GPIO");
  Serial.println(KNOB_K0);

  ledPrintPins();

  Serial.println("=======================================");
}

String buildSimpleDockStatusJson() {
  updateStateDurationTracking();
  const unsigned long now = millis();
  telemetrySequence++;

  String json;
  // Keep the V11 diagnostic fields and also publish the field names consumed
  // by smart_charging_control_center/backend/models.py.  The web backend uses
  // snake_case for sensor telemetry, while the original V11 payload used
  // camelCase, which made the HTTP request succeed but left the UI values at
  // their defaults.
  json.reserve(1536);

  json += "{";
  json += "\"schemaVersion\":2,";
  json += "\"device\":\"ESP32S3_SMART_CHARGING_DOCK\",";
  json += "\"sequence\":" + String(telemetrySequence) + ",";
  json += "\"sampleUptimeMs\":" + String(now) + ",";
  json += "\"stateDurationMs\":" + String(now - stateEnteredMs) + ",";
  json += "\"state\":\"" + stateToString(currentState) + "\",";
  json += "\"fault\":\"" + faultCode + "\",";
  json += "\"relayOn\":";
  json += relay.isOn() ? "true" : "false";
  json += ",";
  json += "\"manualStopHold\":";
  json += manualStopHoldActive ? "true" : "false";
  json += ",";
  json += "\"emergencyStop\":";
  json += emergencyStopTriggered ? "true" : "false";
  json += ",";
  json += "\"contact\":";
  json += microSwitchPressed ? "true" : "false";
  json += ",";
  json += "\"contactStable\":";
  json += microSwitchStablePressed ? "true" : "false";
  json += ",";
  json += "\"distanceMm\":" + String(tofDistanceMm) + ",";
  json += "\"tofOnline\":";
  json += tofSensor.isOnline() ? "true" : "false";
  json += ",";
  json += "\"tofValid\":";
  json += tofDistanceOK ? "true" : "false";
  json += ",";
  json += "\"tofFailureCount\":" + String(tofSensor.consecutiveFailures()) + ",";
  json += "\"voltageV\":" + String(inaBusVoltageV, 3) + ",";
  json += "\"currentA\":" + String(inaCurrentA, 4) + ",";
  json += "\"shuntVoltageMv\":" + String(inaShuntVoltageMv, 4) + ",";
  json += "\"powerW\":" + String(inaPowerW, 3) + ",";
  json += "\"ina226Online\":";
  json += ina226Online ? "true" : "false";
  json += ",";
  json += "\"ina226Valid\":";
  json += ina226ReadOK ? "true" : "false";
  json += ",";
  json += "\"ina226FailureCount\":" + String(ina226Sensor.consecutiveFailures()) + ",";
  json += "\"chargeCompleteEnabled\":";
  json += INA_CHARGE_COMPLETE_ENABLED ? "true" : "false";
  json += ",";
  json += "\"chargeCompleteMinVoltageV\":" + String(INA_CHARGE_COMPLETE_MIN_VOLTAGE_V, 3) + ",";
  json += "\"chargeCompleteMaxCurrentA\":" + String(INA_CHARGE_COMPLETE_MAX_ABS_CURRENT_A, 3) + ",";
  json += "\"chargeCompleteCandidateMs\":";
  json += chargeCompleteCandidateSince == 0
    ? "0"
    : String(now - chargeCompleteCandidateSince);
  json += ",";
  json += "\"temperatureC\":";
  json += tempReadOK ? String(temperatureC, 2) : "null";
  json += ",";
  json += "\"ntcOnline\":";
  json += tempSensorOnline ? "true" : "false";
  json += ",";
  json += "\"ntcValid\":";
  json += tempReadOK ? "true" : "false";
  json += ",";

  // Web dashboard telemetry contract. Keep this block in sync with the
  // DockTelemetry model used by smart_charging_control_center.
  json += "\"tof_mm\":";
  json += tofDistanceMm > 0 ? String(tofDistanceMm) : "null";
  json += ",";
  json += "\"tof_ok\":";
  json += tofDistanceOK ? "true" : "false";
  json += ",";
  json += "\"micro_switch_pressed\":";
  json += microSwitchPressed ? "true" : "false";
  json += ",";
  json += "\"micro_switch_stable\":";
  json += microSwitchStablePressed ? "true" : "false";
  json += ",";
  json += "\"emergency_stop\":";
  json += emergencyStopTriggered ? "true" : "false";
  json += ",";

  json += "\"voltage_v\":";
  json += ina226ReadOK && isfinite(inaBusVoltageV) ? String(inaBusVoltageV, 3) : "null";
  json += ",";
  json += "\"current_a\":";
  json += ina226ReadOK && isfinite(inaCurrentA) ? String(inaCurrentA, 4) : "null";
  json += ",";
  json += "\"power_w\":";
  json += ina226ReadOK && isfinite(inaPowerW) ? String(inaPowerW, 3) : "null";
  json += ",";
  json += "\"ina226_online\":";
  json += ina226Online ? "true" : "false";
  json += ",";
  json += "\"ina226_read_ok\":";
  json += ina226ReadOK ? "true" : "false";
  json += ",";
  json += "\"voltage_ok\":";
  json += ina226VoltageOK ? "true" : "false";
  json += ",";
  json += "\"current_ok\":";
  json += ina226CurrentOK ? "true" : "false";
  json += ",";

  json += "\"temperature_c\":";
  json += tempReadOK && isfinite(temperatureC) ? String(temperatureC, 2) : "null";
  json += ",";
  json += "\"temp_online\":";
  json += tempSensorOnline ? "true" : "false";
  json += ",";
  json += "\"temp_ok\":";
  json += tempOK ? "true" : "false";
  json += ",";
  json += "\"temp_warning\":";
  json += tempWarning ? "true" : "false";
  json += ",";

  json += "\"wifiConnected\":";
  json += wifiDock.isConnected() ? "true" : "false";
  json += ",";
  json += "\"wifiIp\":\"" + wifiDock.ipAddress() + "\",";
  json += "\"telemetryIntervalMs\":" + String(WIFI_AUTO_UPLOAD_INTERVAL_MS) + ",";
  json += "\"uptime_ms\":";
  json += String(now);
  json += "}";

  return json;
}

bool uploadDockStatus(const char* reason) {
  String json = buildSimpleDockStatusJson();

  Serial.print("[WiFi] ");
  Serial.print(reason);
  Serial.print(" upload json: ");
  Serial.println(json);

  bool ok = wifiDock.uploadStatus(json);

  Serial.print("[WiFi] ");
  Serial.print(reason);
  Serial.print(" upload queue: ");
  Serial.println(ok ? "QUEUED" : "FAILED");

  Serial.print("[WiFi] HTTP code: ");
  Serial.println(wifiDock.lastHttpCode());

  Serial.print("[WiFi] last error: ");
  Serial.println(wifiDock.lastError());

  return ok;
}

void handleWiFiAutoUpload() {
  if (!WIFI_MODULE_ENABLED || !wifiStartedByUser || !wifiDock.isConnected()) {
    return;
  }

  unsigned long now = millis();
  if ((now - lastWifiAutoUploadTime) < WIFI_AUTO_UPLOAD_INTERVAL_MS) {
    return;
  }

  lastWifiAutoUploadTime = now;
  uploadDockStatus("auto");
}

void printWiFiInfo() {
  Serial.println("---------- WIFI DATA ----------");

  Serial.print("WiFi module enabled: ");
  Serial.println(WIFI_MODULE_ENABLED ? "YES" : "NO");

  Serial.print("Configured: ");
  Serial.println(wifiDock.isConfigured() ? "YES" : "NO");

  Serial.print("Connected: ");
  Serial.println(wifiDock.isConnected() ? "YES" : "NO");

  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);

  Serial.print("Backend URL: ");
  Serial.println(WIFI_BACKEND_URL);

  Serial.print("IP: ");
  Serial.println(wifiDock.ipAddress());

  Serial.print("MAC: ");
  Serial.println(wifiDock.macAddress());

  Serial.print("Last HTTP code: ");
  Serial.println(wifiDock.lastHttpCode());

  Serial.print("Last error: ");
  Serial.println(wifiDock.lastError());

  Serial.println("-------------------------------");
}

void startWiFiAutomatically() {
  if (!WIFI_MODULE_ENABLED) {
    return;
  }

  if (!WIFI_AUTO_START_ON_BOOT) {
    return;
  }

  // Do not repeat begin() after automatic startup or the wifi_start command.
  if (wifiStartedByUser) {
    return;
  }

  // Delay Wi-Fi startup to avoid contention during power-on initialization.
  if (millis() < WIFI_AUTO_START_DELAY_MS) {
    return;
  }

  Serial.println("[WiFi] auto start requested.");
  wifiStartedByUser = true;
  wifiDock.begin();
  printWiFiInfo();
}

void startWiFiBySerial() {
  if (!WIFI_MODULE_ENABLED) {
    Serial.println("[WiFi] WIFI_MODULE_ENABLED is false.");
    return;
  }

  Serial.println("[WiFi] manual start requested.");
  wifiStartedByUser = true;
  wifiDock.begin();
  printWiFiInfo();
}

void stopWiFiBySerial() {
  Serial.println("[WiFi] manual stop requested. Restart board to fully reset WiFi stack if needed.");
  wifiStartedByUser = false;
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  printWiFiInfo();
}

void printHelp() {
  Serial.println();
  Serial.println("Available commands:");
  Serial.println("  help               - show this help");
  Serial.println("  status             - show system status");
  Serial.println("  raw                - show all raw states");
  Serial.println("  tof                - show ToF distance only");
  Serial.println("  ina                - show INA226 voltage/current only");
  Serial.println("  temp               - show GPIO4 NTC temperature only");
  Serial.println("  temp_test_warning  - simulate temperature warning");
  Serial.println("  temp_test_high     - simulate high temperature fault");
  Serial.println("  temp_test_off      - disable temperature simulation");
  Serial.println("  yellow_test        - force yellow LED ON");
  Serial.println("  yellow_test_off    - turn off yellow LED test");
  Serial.println("  led_test           - run LED self test");
  Serial.println("  buzzer             - show buzzer state");
  Serial.println("  buzzer_test        - run buzzer self test");
  Serial.println("  buzzer_high_test   - force GPIO2 HIGH, high-level trigger buzzer should sound");
  Serial.println("  buzzer_low_test    - force GPIO2 LOW, high-level trigger buzzer should stop");
  Serial.println("  buzzer_on          - force buzzer ON, same as HIGH for this module");
  Serial.println("  buzzer_off         - force buzzer OFF, same as LOW for this module");
  Serial.println("  beep               - short beep once");
  Serial.println("  fault_beep         - quick fault beep pattern");
  Serial.println("  tft                - refresh TFT status screen");
  Serial.println("  tft_boot           - show TFT boot screen");
  Serial.println("  knob               - show knob/button status");
  Serial.println("  wifi               - show WiFi connection status");
  Serial.println("  wifi_start         - manually start WiFi connection");
  Serial.println("  wifistart          - same as wifi_start");
  Serial.println("  wifi start         - same as wifi_start");
  Serial.println("  wifi_stop          - turn WiFi OFF");
  Serial.println("  wifistop           - same as wifi_stop");
  Serial.println("  wifi_reconnect     - reconnect WiFi now");
  Serial.println("  charge             - release manual STOP hold; safety checks still control relay");
  Serial.println("  relay_on           - same as charge; never bypasses FAULT/safety");
  Serial.println("  stop               - stop charge / turn relay OFF");
  Serial.println("  relay_off          - same as stop");
  Serial.println("  relay_pulse        - turn relay ON for 1 second, only when safety OK");
  Serial.println("  fault              - enter manual FAULT, relay forced OFF");
  Serial.println("  reset              - clear FAULT only after all local faults recover");
  Serial.println();
}


// ==========================
// 11. Serial commands
// ==========================

void handleCommand(String command) {
  command.trim();
  command.toLowerCase();

  if (command.length() == 0) {
    return;
  }

  // While energized, serial accepts only the safety stop command. Printing a
  // full status page or running a display/test routine can fill the UART buffer
  // or call delay(), which would postpone local safety processing.
  if ((currentState == STATE_CHARGING || relay.isOn()) &&
      command != "stop" && command != "relay_off") {
    Serial.println("[BLOCK] while charging, serial accepts only stop/relay_off.");
    return;
  }

  if (command == "help") {
    printHelp();
  }

  else if (command == "status") {
    printStatus();
  }

  else if (command == "raw") {
    printRaw();
  }

  else if (command == "tof") {
    printTofInfo();
  }

  else if (command == "ina") {
    printIna226Info();
  }

  else if (command == "temp") {
    printTemperatureInfo();
  }

  else if (command == "buzzer") {
    printBuzzerInfo();
  }

  else if (command == "knob") {
    printKnobInfo();
    tftDisplay.showKnobPage(
      knobLeftCount,
      knobRightCount,
      pushPressCount,
      k0PressCount,
      tftKnob.isPushDown(),
      tftKnob.isK0Down()
    );
  }

  else if (command == "wifi") {
    printWiFiInfo();
  }

  else if (command == "wifi_start" || command == "wifistart" || command == "wifi start") {
    startWiFiBySerial();
  }

  else if (command == "wifi_stop" || command == "wifistop" || command == "wifi stop") {
    stopWiFiBySerial();
  }

  else if (command == "wifi_reconnect" || command == "wifireconnect" || command == "wifi reconnect") {
    if (!wifiStartedByUser) {
      Serial.println("[WiFi] not started yet. Use wifi_start first.");
    } else {
      wifiDock.reconnectNow();
      printWiFiInfo();
    }
  }

  else if (command == "tft") {
    updateTftDisplayNow();
    Serial.println("[TFT] status screen refreshed.");
  }

  else if (command == "tft_boot") {
    tftDisplay.showBootScreen();
    Serial.println("[TFT] boot screen shown.");
  }

  else if (command == "temp_test_warning") {
    forceWarningTemperatureTest = true;
    forceHighTemperatureTest = false;
    updateTemperatureReading(true);
    Serial.println("[TEST] temperature warning simulation ON.");
    printTemperatureInfo();
  }

  else if (command == "temp_test_high") {
    forceHighTemperatureTest = true;
    forceWarningTemperatureTest = false;
    updateTemperatureReading(true);
    Serial.println("[TEST] high temperature simulation ON.");
    printTemperatureInfo();
  }

  else if (command == "temp_test_off") {
    forceHighTemperatureTest = false;
    forceWarningTemperatureTest = false;
    updateTemperatureReading(true);
    Serial.println("[TEST] temperature simulation OFF.");
    printTemperatureInfo();
  }

  else if (command == "yellow_test") {
    forceYellowLedTest = true;
    ledSet(LED_PROCESS, true);
    Serial.println("[LED] yellow LED test ON.");
  }

  else if (command == "yellow_test_off") {
    forceYellowLedTest = false;
    ledSet(LED_PROCESS, false);
    Serial.println("[LED] yellow LED test OFF.");
  }

  else if (command == "led_test") {
    Serial.println("[LED] self test start.");
    ledSelfTest();
    Serial.println("[LED] self test finished.");
  }

  else if (command == "buzzer_test") {
    Serial.println("[BUZZER] self test start.");
    buzzer.successBeep();
    delay(300);
    buzzer.doubleBeep();
    delay(600);
    buzzer.faultBeep();
    Serial.println("[BUZZER] self test scheduled.");
  }

  else if (command == "buzzer_high_test") {
    // Drive the buzzer input HIGH directly for this diagnostic.
    // The active-high three-pin module should sound immediately.
    chargeInterruptedAlertActive = false;
    chargeInterruptedAlertCode = "NONE";
    chargeInterruptRecoveredSince = 0;
    buzzer.off();
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH);
    chargeInterruptBuzzerOutputOn = true;
    chargeInterruptBuzzerLastToggleMs = millis();
    Serial.println("[BUZZER] GPIO2 forced HIGH. High-level trigger buzzer should sound now.");
  }

  else if (command == "buzzer_low_test") {
    // Drive the buzzer input LOW directly for this diagnostic.
    // The active-high three-pin module should stop immediately.
    chargeInterruptedAlertActive = false;
    chargeInterruptedAlertCode = "NONE";
    chargeInterruptRecoveredSince = 0;
    buzzer.off();
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    chargeInterruptBuzzerOutputOn = false;
    chargeInterruptBuzzerLastToggleMs = 0;
    Serial.println("[BUZZER] GPIO2 forced LOW. High-level trigger buzzer should stop now.");
  }

  else if (command == "buzzer_on") {
    // Active-high module: ON drives IN HIGH.
    buzzer.on();
    Serial.println("[BUZZER] ON = GPIO HIGH.");
  }

  else if (command == "buzzer_off") {
    buzzer.off();
    faultAlarmStarted = false;
    chargeInterruptedAlertActive = false;
    chargeInterruptedAlertCode = "NONE";
    chargeInterruptRecoveredSince = 0;
    stopChargeInterruptBuzzerDirect();
    Serial.println("[BUZZER] OFF.");
  }

  else if (command == "beep") {
    buzzer.successBeep();
    Serial.println("[BUZZER] beep.");
  }

  else if (command == "fault_beep") {
    buzzer.faultBeep();
    Serial.println("[BUZZER] fault beep pattern.");
  }

  else if (command == "relay_on" || command == "charge") {
    if (currentState == STATE_FAULT) {
      Serial.println("[BLOCK] charge cannot bypass FAULT; fix and reset first.");
    } else {
      manualStopHoldActive = false;
      autoReadySince = 0;
      Serial.println("[AUTO] manual stop hold released; relay still requires READY_TO_CHARGE and all local safety checks.");
    }
  }

  else if (command == "relay_off" || command == "stop") {
    forcePowerOff();
    manualStopHoldActive = true;
    stopChargingInterruptAlert("serial/manual stop command");

    if (currentState != STATE_FAULT) {
      currentState = STATE_STOPPED;
      faultCode = "NONE";
    }

    buzzer.doubleBeep();

    Serial.println("charge stop");
  }

  else if (command == "relay_pulse") {
    if (currentState != STATE_READY_TO_CHARGE ||
        faultCode != "NONE" ||
        manualStopHoldActive) {
      Serial.println("[BLOCK] relay_pulse requires READY_TO_CHARGE with no FAULT or manual STOP hold");
      return;
    }

    if (!isCarPositioned()) {
      Serial.println("[BLOCK] relay_pulse blocked: car not positioned");
      return;
    }

    startRelayPulse(1000);
  }

  else if (command == "fault") {
    enterFault("TEST_FAULT");
  }

  else if (command == "reset") {
    if (!localFaultConditionsRecovered()) {
      forcePowerOff();
      currentState = STATE_FAULT;
      Serial.print("[BLOCK] reset blocked: local fault remains: ");
      Serial.println(faultCode);
      return;
    }

    resetStateMachineToIdle("serial reset: relay OFF, state IDLE.");
  }

  else if (command == "wifi_upload") {
    Serial.println("[WiFi] manual upload requested.");
    uploadDockStatus("manual");
  }

  else {
    Serial.print("[CMD] unknown command: ");
    Serial.println(command);
    printHelp();
  }

  updateTftDisplayNow();
}

void handleSerialInputNonBlocking() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;

    if (c == '\n') {
      if (serialCommandBuffer.length() > 0) {
        String command = serialCommandBuffer;
        serialCommandBuffer = "";
        handleCommand(command);
        if (currentState != STATE_CHARGING && !relay.isOn()) printStatus();
      }
      continue;
    }

    if (serialCommandBuffer.length() < 64) {
      serialCommandBuffer += c;
    } else {
      serialCommandBuffer = "";
      Serial.println("[CMD] discarded: command is longer than 64 bytes.");
    }
  }
}


void handleWiFiRemoteCommand() {
  if (!WIFI_MODULE_ENABLED || !wifiStartedByUser || !wifiDock.isConnected()) {
    return;
  }

  // Non-blocking: returns a command completed by the background HTTP task and
  // queues a future poll at the module's own rate limit.
  WiFiDockModule::RemoteCommand cmd = wifiDock.pollCommand();
  if (cmd == WiFiDockModule::CMD_NONE) {
    return;
  }

  Serial.print("[WiFi CMD] ");
  Serial.println(WiFiDockModule::commandToString(cmd));

  if (cmd == WiFiDockModule::CMD_STOP_CHARGING) {
    markWiFiStopCommandReceived();
    manualStopHoldActive = true;
    forcePowerOff();

    if (currentState != STATE_FAULT) {
      currentState = STATE_STOPPED;
      faultCode = "NONE";
    }

    autoReadySince = 0;
    chargingUnsafeSince = 0;
    chargingUnsafeCode = "NONE";

    // A deliberate Wi-Fi stop is normal control and does not trigger interruption beeps.
    Serial.println("[WiFi] STOP_CHARGING handled: relay OFF, no abnormal buzzer.");
    updateTftDisplayNow();
    return;
  }

  if (cmd == WiFiDockModule::CMD_CLEAR_FAULT) {
    if (currentState == STATE_COMPLETE) {
      forcePowerOff();
      Serial.println("[WiFi] CLEAR_FAULT ignored: COMPLETE is latched until the car/contact is released.");
      updateTftDisplayNow();
      return;
    }

    if (!localFaultConditionsRecovered()) {
      forcePowerOff();
      currentState = STATE_FAULT;
      Serial.print("[WiFi] CLEAR_FAULT blocked: local fault remains: ");
      Serial.println(faultCode);
    } else {
      resetStateMachineToIdle("WiFi CLEAR_FAULT / RESET command");
    }

    updateTftDisplayNow();
    return;
  }

  if (cmd == WiFiDockModule::CMD_QUERY_STATUS) {
    bool ok = uploadDockStatus("QUERY_STATUS");
    Serial.print("[WiFi] QUERY_STATUS upload: ");
    Serial.println(ok ? "OK" : "FAILED");
    return;
  }

  if (cmd == WiFiDockModule::CMD_REQUEST_START_CHARGE) {
    // The web start request cannot directly energize the relay.
    // It releases the Wi-Fi stop hold and asks the local state machine to re-evaluate.
    // Stable contact, ToF, INA226, temperature and E-stop must still pass.
    updateInputs(true, true, true);

    if (emergencyStopTriggered) {
      forcePowerOff();
      currentState = STATE_FAULT;
      faultCode = "EMERGENCY_STOP";
      Serial.println("[WiFi] START_CHARGE blocked: emergency stop is pressed.");
      updateTftDisplayNow();
      return;
    }

    if (currentState == STATE_FAULT) {
      forcePowerOff();
      autoReadySince = 0;
      Serial.print("[WiFi] START_CHARGE blocked: device is in FAULT, code=");
      Serial.println(faultCode);
      Serial.println("[WiFi] Send CLEAR_FAULT first after fixing the fault.");
      updateTftDisplayNow();
      return;
    }

    if (currentState == STATE_COMPLETE && microSwitchStablePressed) {
      forcePowerOff();
      Serial.println("[WiFi] START_CHARGE blocked: COMPLETE is latched until the car/contact is released.");
      updateTftDisplayNow();
      return;
    }

    // Allow backend START_CHARGE to release the deliberate STOP hold.
    manualStopHoldActive = false;
    lastWifiStopCommandMs = 0;
    autoReadySince = 0;
    chargingUnsafeSince = 0;
    chargingUnsafeCode = "NONE";
    stopChargingInterruptAlert("WiFi START_CHARGE request");

    if (currentState != STATE_CHARGING) {
      if (isCarPositioned()) {
        currentState = STATE_POSITION_CHECK;
      } else if (isCarApproaching()) {
        currentState = STATE_WAIT_CAR;
      } else {
        currentState = STATE_IDLE;
      }
    }

    Serial.println("[WiFi] START_CHARGE accepted: auto charging re-enabled, relay will turn ON only when local safety conditions are OK.");
    updateTftDisplayNow();
    return;
  }
}


// ==========================
// 12. Arduino entry points
// ==========================

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(1000);

  relay.begin();
  relay.forceOff();

  ledIndicatorBegin();

  buzzer.begin();

  bool tftOk = tftDisplay.begin();

  if (tftOk) {
    Serial.println("[TFT] init OK.");
  } else {
    Serial.println("[TFT] init FAILED.");
  }

  tftKnob.begin();

  // setup() does not call WiFi.begin() directly.
  // The main loop starts Wi-Fi after WIFI_AUTO_START_DELAY_MS.
  if (WIFI_MODULE_ENABLED && WIFI_AUTO_START_ON_BOOT) {
    Serial.print("[WiFi] auto start enabled. Delay ms: ");
    Serial.println(WIFI_AUTO_START_DELAY_MS);
  } else {
    Serial.println("[WiFi] auto start disabled. Type wifi_start to begin WiFi connection.");
  }

  ledSelfTest();

  // Short power-on chirp; other success indications keep their normal length.
  buzzer.beep(50);

  emergencyStopButton.begin(true);
  microSwitch.begin();

  // Initialize the shared bus once before either I2C device.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  bool tofBeginOK = tofSensor.begin();
  if (!tofBeginOK) {
    faultCode = "TOF_SENSOR_INIT_FAILED";
  }

  ina226Sensor.setShuntResistorOhms(INA226_SHUNT_OHMS);
  bool inaBeginOK = ina226Sensor.begin();
  ina226Online = inaBeginOK;

  if (!inaBeginOK) {
    faultCode = "INA226_INIT_FAILED";
  }

  bool tempBeginOK = tempSensorBegin();
  tempSensorOnline = tempBeginOK;
  if (!tempBeginOK) {
    faultCode = "TEMP_SENSOR_INIT_FAILED";
  }

  updateInputs(true, true, true);

  currentState = STATE_IDLE;
  trackedState = currentState;
  stateEnteredMs = millis();
  if (!tofBeginOK) {
    enterFault("TOF_SENSOR_INIT_FAILED");
  } else if (!inaBeginOK) {
    enterFault("INA226_INIT_FAILED");
  } else if (!tempBeginOK) {
    enterFault("TEMP_SENSOR_INIT_FAILED");
  } else if (!isElectricalSafetyOK(true)) {
    enterFault(faultCode);
  }

  Serial.println();
  Serial.println("ESP32-S3 Smart Charging Dock Safety V11.1 Boot OK");
  Serial.print("Firmware tag: ");
  Serial.println(FIRMWARE_TAG);
  Serial.println("Modules: Emergency Stop + Relay + Micro Switch + ToF + INA226 + NTC + LED + Buzzer + TFT + WiFi");

  Serial.println("I2C bus:");
  Serial.println("  ToF VL53L1X + INA226: Wire (I2C0), SDA=GPIO13, SCL=GPIO14");

  Serial.println("NTC analog temperature module:");
  Serial.println("  AO/S=GPIO4, VCC=3V3, GND=GND (default: 10k NTC, B=3950)");

  Serial.println("TFT:");
  Serial.println("  SCK=GPIO5, MOSI=GPIO6, RST=GPIO7, DC=GPIO15, CS=GPIO16");

  Serial.println("TFT Knob:");
  Serial.println("  A=GPIO17, B=GPIO18, PUSH=GPIO8, K0=GPIO9");

  Serial.println("WiFi:");
  Serial.println("  First-stage test commands: wifi, wifi_start, wifi_stop, wifi_reconnect");
  Serial.println("  Aliases: wifistart, wifi start, wifistop, wifi stop");
  Serial.println("  Edit WIFI_SSID and WIFI_PASSWORD in src/main.cpp first.");

  ledPrintPins();

  Serial.print("Buzzer GPIO");
  Serial.println(PIN_BUZZER);

  printHelp();
  printStatus();

  updateTftDisplayNow();
  lastTftRefreshTime = millis();
}

void loop() {
  // Safety/state processing always runs before any network housekeeping.
  updateStateMachine();

  updateRelayPulse();

  startWiFiAutomatically();

  if (WIFI_MODULE_ENABLED && wifiStartedByUser) {
    wifiDock.update();
    handleWiFiRemoteCommand();
  }

  updateStateDurationTracking();

  handleWiFiAutoUpload();

  updateLedStatus();

  updateChargingInterruptAlert();

  updateBuzzerStatus();

  // Keep the local UI alive in every state.  Previously both the button scan
  // and TFT rendering were skipped while charging, so pressing the micro
  // switch (which starts charging) made the whole UI appear locked.
  handleTftKnobEvents();

  if ((long)(millis() - lastTftRefreshTime) >= TFT_REFRESH_INTERVAL_MS) {
    updateTftDisplayNow();
    lastTftRefreshTime = millis();
  }

  handleSerialInputNonBlocking();

  // Catch any actuator/state mismatch introduced by a command handler in the
  // same loop iteration, not one iteration later.
  enforceRelaySafetyInvariant();
  updateStateDurationTracking();
}
