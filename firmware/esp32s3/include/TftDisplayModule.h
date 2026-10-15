#ifndef TFT_DISPLAY_MODULE_H
#define TFT_DISPLAY_MODULE_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

class TftDisplayModule {
public:
  TftDisplayModule(
    int8_t csPin,
    int8_t dcPin,
    int8_t rstPin,
    int8_t sckPin,
    int8_t mosiPin
  );

  bool begin();

  void showBootScreen();

  void showStatus(
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
  );

  void showMessage(const String& title, const String& message);

  void showKnobPage(
    int leftCount,
    int rightCount,
    int pushCount,
    int k0Count,
    bool pushDown,
    bool k0Down
  );

private:
  int8_t _csPin;
  int8_t _dcPin;
  int8_t _rstPin;
  int8_t _sckPin;
  int8_t _mosiPin;

  Adafruit_ST7789 _tft;

  void drawHeader(const String& title);
  void drawLine(int y, const String& label, const String& value);
  void drawLineColor(int y, const String& label, const String& value, uint16_t valueColor);
};

#endif
