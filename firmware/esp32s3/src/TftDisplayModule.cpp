#include "TftDisplayModule.h"

TftDisplayModule::TftDisplayModule(
  int8_t csPin,
  int8_t dcPin,
  int8_t rstPin,
  int8_t sckPin,
  int8_t mosiPin
)
  : _csPin(csPin),
    _dcPin(dcPin),
    _rstPin(rstPin),
    _sckPin(sckPin),
    _mosiPin(mosiPin),
    // Hardware-SPI constructor. SPI.begin() below assigns the custom SCK/MOSI
    // pins before the display is initialized.
    _tft(csPin, dcPin, rstPin) {
}

bool TftDisplayModule::begin() {
  pinMode(_csPin, OUTPUT);
  pinMode(_dcPin, OUTPUT);
  pinMode(_rstPin, OUTPUT);

  SPI.begin(_sckPin, -1, _mosiPin, _csPin);

  _tft.init(240, 320);
  _tft.setRotation(1);
  _tft.fillScreen(ST77XX_BLACK);

  showBootScreen();

  return true;
}

void TftDisplayModule::showBootScreen() {
  _tft.fillScreen(ST77XX_BLACK);

  drawHeader("SMART CHARGING DOCK");

  _tft.setTextSize(2);
  _tft.setTextColor(ST77XX_GREEN);
  _tft.setCursor(20, 55);
  _tft.println("SAFETY V11.1");

  _tft.setTextSize(1);
  _tft.setTextColor(ST77XX_WHITE);
  _tft.setCursor(20, 95);
  _tft.println("Emergency + Relay + MicroSwitch");

  _tft.setCursor(20, 115);
  _tft.println("ToF + INA226 + NTC(GPIO4)");

  _tft.setCursor(20, 135);
  _tft.println("LED + Buzzer + TFT + Knob");

  _tft.setTextColor(ST77XX_YELLOW);
  _tft.setCursor(20, 175);
  _tft.println("Safety logic still local on ESP32-S3");
}

void TftDisplayModule::showStatus(
  const String& stateName,
  const String& faultName,
  int tofDistanceMm,
  bool tofOk,
  bool microSwitchPressed,
  float voltage,
  bool voltageOk,
  float current,
  bool currentOk,
  float temperature,
  bool tempOk,
  bool tempWarning,
  bool emergencyStop,
  bool relayOn,
  bool buzzerAlarm
) {
  _tft.fillScreen(ST77XX_BLACK);

  drawHeader("DOCK STATUS");

  uint16_t stateColor = ST77XX_WHITE;

  if (stateName == "FAULT") {
    stateColor = ST77XX_RED;
  } else if (stateName == "CHARGING") {
    stateColor = ST77XX_GREEN;
  } else {
    stateColor = ST77XX_CYAN;
  }

  drawLineColor(32, "State", stateName, stateColor);
  drawLineColor(50, "Fault", faultName, faultName == "NONE" ? ST77XX_GREEN : ST77XX_RED);

  String tofValue = tofDistanceMm > 0 ? String(tofDistanceMm) + " mm" : "INVALID";
  drawLineColor(68, "ToF", tofValue, tofOk ? ST77XX_GREEN : ST77XX_RED);

  drawLineColor(
    86,
    "Micro",
    microSwitchPressed ? "CONTACT OK" : "NOT PRESSED",
    microSwitchPressed ? ST77XX_GREEN : ST77XX_YELLOW
  );

  drawLineColor(
    104,
    "Voltage",
    String(voltage, 2) + " V",
    voltageOk ? ST77XX_GREEN : ST77XX_RED
  );

  drawLineColor(
    122,
    "Current",
    String(current, 3) + " A",
    currentOk ? ST77XX_GREEN : ST77XX_RED
  );

  uint16_t tempColor = ST77XX_GREEN;
  if (!tempOk) {
    tempColor = ST77XX_RED;
  } else if (tempWarning) {
    tempColor = ST77XX_YELLOW;
  }

  if (isnan(temperature)) {
    drawLineColor(140, "Temp", "INVALID", ST77XX_RED);
  } else {
    drawLineColor(140, "Temp", String(temperature, 1) + " C", tempColor);
  }

  drawLineColor(
    158,
    "E-Stop",
    emergencyStop ? "ACTIVE" : "NORMAL",
    emergencyStop ? ST77XX_RED : ST77XX_GREEN
  );

  drawLineColor(
    176,
    "Relay",
    relayOn ? "ON" : "OFF",
    relayOn ? ST77XX_GREEN : ST77XX_WHITE
  );

  drawLineColor(
    194,
    "Buzzer",
    buzzerAlarm ? "ALARM" : "NORMAL",
    buzzerAlarm ? ST77XX_RED : ST77XX_WHITE
  );

  _tft.setTextSize(1);
  _tft.setTextColor(ST77XX_YELLOW);
  _tft.setCursor(10, 222);
  _tft.println("Knob: rotate/page test | Push/K0 input test");
}

void TftDisplayModule::showMessage(const String& title, const String& message) {
  _tft.fillScreen(ST77XX_BLACK);

  drawHeader(title);

  _tft.setTextSize(2);
  _tft.setTextColor(ST77XX_WHITE);
  _tft.setCursor(10, 70);
  _tft.println(message);
}

void TftDisplayModule::showKnobPage(
  int leftCount,
  int rightCount,
  int pushCount,
  int k0Count,
  bool pushDown,
  bool k0Down
) {
  _tft.fillScreen(ST77XX_BLACK);

  drawHeader("TFT KNOB TEST");

  drawLine(40, "Rotate Left", String(leftCount));
  drawLine(60, "Rotate Right", String(rightCount));
  drawLine(80, "Push Count", String(pushCount));
  drawLine(100, "K0 Count", String(k0Count));

  drawLineColor(130, "PUSH", pushDown ? "DOWN" : "UP", pushDown ? ST77XX_GREEN : ST77XX_WHITE);
  drawLineColor(150, "K0", k0Down ? "DOWN" : "UP", k0Down ? ST77XX_GREEN : ST77XX_WHITE);

  _tft.setTextColor(ST77XX_YELLOW);
  _tft.setTextSize(1);
  _tft.setCursor(10, 205);
  _tft.println("Knob/buttons do NOT control relay yet.");
}

void TftDisplayModule::drawHeader(const String& title) {
  _tft.fillRect(0, 0, 320, 25, ST77XX_BLUE);

  _tft.setTextSize(2);
  _tft.setTextColor(ST77XX_WHITE);
  _tft.setCursor(8, 5);
  _tft.println(title);
}

void TftDisplayModule::drawLine(int y, const String& label, const String& value) {
  drawLineColor(y, label, value, ST77XX_WHITE);
}

void TftDisplayModule::drawLineColor(int y, const String& label, const String& value, uint16_t valueColor) {
  _tft.setTextSize(1);

  _tft.setTextColor(ST77XX_CYAN);
  _tft.setCursor(10, y);
  _tft.print(label);
  _tft.print(": ");

  _tft.setTextColor(valueColor);
  _tft.println(value);
}
