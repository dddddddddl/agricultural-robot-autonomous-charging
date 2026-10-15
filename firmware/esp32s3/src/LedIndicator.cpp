#include "LedIndicator.h"

/*
  Actual GPIO assignments for the three LEDs

  Original standalone LED test assignments:
  - LED_SYSTEM  -> GPIO39 (green)
  - LED_PROCESS -> GPIO40 (yellow)
  - LED_FAULT   -> GPIO41

  Note:
  GPIO39 / GPIO40 / GPIO41 are dedicated to the three status LEDs.
*/
static const int LED_SYSTEM_PIN  = 39;
static const int LED_PROCESS_PIN = 40;
static const int LED_FAULT_PIN   = 41;

static const int LED_PINS[3] = {
  LED_SYSTEM_PIN,
  LED_PROCESS_PIN,
  LED_FAULT_PIN
};

// Green (SYSTEM/GPIO39) and red are active HIGH; yellow (PROCESS/GPIO40) is active LOW.
static const int LED_ACTIVE_LEVELS[3] = { HIGH, LOW, HIGH };
static const int LED_INACTIVE_LEVELS[3] = { LOW, HIGH, LOW };

// Heartbeat state
static bool heartbeatLedOn = false;
static unsigned long lastHeartbeatTime = 0;

// Warning flash state
static bool warningLedOn = false;
static unsigned long lastWarningBlinkTime = 0;

static bool isValidChannel(LedChannel channel) {
  return channel == LED_SYSTEM || channel == LED_PROCESS || channel == LED_FAULT;
}

void ledIndicatorBegin() {
  for (int i = 0; i < 3; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LED_INACTIVE_LEVELS[i]);
  }

  heartbeatLedOn = false;
  warningLedOn = false;
  lastHeartbeatTime = millis();
  lastWarningBlinkTime = millis();
}

void ledSet(LedChannel channel, bool on) {
  if (!isValidChannel(channel)) {
    return;
  }

  int index = (int)channel;
  int pin = LED_PINS[index];
  digitalWrite(pin, on ? LED_ACTIVE_LEVELS[index] : LED_INACTIVE_LEVELS[index]);
}

void ledAllOff() {
  ledSet(LED_SYSTEM, false);
  ledSet(LED_PROCESS, false);
  ledSet(LED_FAULT, false);
}

void ledAllOn() {
  ledSet(LED_SYSTEM, true);
  ledSet(LED_PROCESS, true);
  ledSet(LED_FAULT, true);
}

void ledSelfTest() {
  ledAllOff();
  delay(300);

  // System LED
  ledSet(LED_SYSTEM, true);
  delay(400);
  ledSet(LED_SYSTEM, false);

  // Process LED
  ledSet(LED_PROCESS, true);
  delay(400);
  ledSet(LED_PROCESS, false);

  // Fault LED
  ledSet(LED_FAULT, true);
  delay(400);
  ledSet(LED_FAULT, false);

  // All ON
  ledAllOn();
  delay(500);

  // All OFF
  ledAllOff();
  delay(300);
}

void ledUpdateHeartbeat(unsigned long intervalMs) {
  if (intervalMs == 0) {
    return;
  }

  unsigned long now = millis();

  if (now - lastHeartbeatTime >= intervalMs) {
    lastHeartbeatTime = now;
    heartbeatLedOn = !heartbeatLedOn;
    // Yellow LED (GPIO40) is the system heartbeat.
    ledSet(LED_PROCESS, heartbeatLedOn);
  }
}

void ledUpdateForDockState(const String& stateName, bool chargeSafetyOKNow, bool temperatureWarningNow) {
  /*
    LED_PROCESS (yellow):
      - ledUpdateHeartbeat() controls its blinking separately.

    LED_SYSTEM (green):
      - CHARGING: steady ON
      - No fault and safety checks pass: steady ON, ready to charge
      - Temperature warning: blinking
      - Otherwise: OFF

    LED_FAULT：
      - FAULT: steady ON
      - Otherwise: OFF
  */

  if (stateName == "FAULT") {
    ledSet(LED_SYSTEM, false);
    ledSet(LED_FAULT, true);
    return;
  }

  if (stateName == "COMPLETE") {
    ledSet(LED_SYSTEM, false);
    ledSet(LED_FAULT, false);
    return;
  }

  ledSet(LED_FAULT, false);

  if (temperatureWarningNow) {
    unsigned long now = millis();

    if (now - lastWarningBlinkTime >= 300) {
      lastWarningBlinkTime = now;
      warningLedOn = !warningLedOn;
      ledSet(LED_SYSTEM, warningLedOn);
    }

    return;
  }

  if (stateName == "CHARGING") {
    ledSet(LED_SYSTEM, true);
    return;
  }

  if (chargeSafetyOKNow) {
    ledSet(LED_SYSTEM, true);
    return;
  }

  ledSet(LED_SYSTEM, false);
}

void ledPrintPins() {
  Serial.println("LED pins:");
  Serial.print("  LED_SYSTEM  -> GPIO");
  Serial.println(LED_SYSTEM_PIN);
  Serial.print("  LED_PROCESS -> GPIO");
  Serial.println(LED_PROCESS_PIN);
  Serial.print("  LED_FAULT   -> GPIO");
  Serial.println(LED_FAULT_PIN);
}
