# Power path and wiring revisions

This guide consolidates the supplied circuit notes and the selected firmware. It preserves their revision differences rather than treating an old schematic pin list as an instruction to flash a differently wired board.

## Power path

The project notes specify an original charger output of **25.55 V / 2.8 A**. Firmware protection thresholds and software charging-power assumptions are separate values and should not be confused with that charger rating.

```text
Charger DC+
  → external fuse
  → PCB FUSE_OUT+
  → XL74610-family ideal-diode module
  → protected positive rail VIN_PROT+
      ├─ buck converter → 5 V → ESP32-S3 5V input and relay control supply
      └─ INA226 VIN → INA226 VOUT
                      → external heavy wire → relay COM
                      → relay NO → external heavy wire → positive contact PAD+

Charger DC− → GND_IN → common ground → heavy return path → negative contact PAD−
```

A unidirectional TVS is connected **in parallel**, cathode to fused positive and anode to ground. It is not a series power-path element. The source notes suggest input filtering with 0.1 µF ceramic and 100 µF or larger electrolytic capacitance, subject to actual voltage ratings and polarity.

The later wiring notes route INA226 VOUT-to-relay COM and relay NO-to-contact through module terminals and external heavy wire rather than fine PCB traces. The PCB supports protection, module mounting, supply distribution and small-signal interfaces. Power ratings, trace sizing, fusing and electrical isolation were not independently validated in this preparation.

## Selected firmware GPIO map

Values below come from the committed `main.cpp`, `TempSensor.h` and `LedIndicator.cpp`.

| Function | GPIO / configuration |
|---|---|
| Emergency-stop input | 42; normally closed input configuration |
| Relay | 21; active HIGH |
| Micro-switch | 1; active LOW, 80 ms debounce |
| Shared VL53L1X/INA226 I²C | SDA 13, SCL 14; INA226 address `0x40` |
| Analog NTC | 4 |
| Active buzzer | 2; active HIGH |
| TFT ST7789 | SCK 5, MOSI 6, RESET 7, DC 15, CS 16 |
| Encoder A / B / PUSH / K0 | 17 / 18 / 8 / 9 |
| System / process / fault LED | 39 / 40 / 41; process LED active LOW, others active HIGH |

The NTC source uses a 10 kΩ, B=3950 reference with a 10 kΩ divider. Its current topology is `3V3 → fixed resistor → ADC → NTC → GND`. Measure the real rail and calibrate against a known temperature before relying on the displayed value.

## Differences in older connection notes

The older PCB explanation lists separate INA226 I²C on 6/8 and ToF on 9/11, relay GPIO36, micro-switch GPIO5 and emergency stop GPIO7. It also lists alternative buzzer/LED/TFT pins. Those values conflict with the selected executable firmware and are **not** applied to it.

The notes describe both DS18B20 and NTC options. The selected `TempSensor` implementation is analog NTC on GPIO4; retained OneWire/DallasTemperature dependency entries do not establish active DS18B20 use. A digital probe must not be substituted for the analog divider without changing and validating the implementation.

Source notes also recommend a relay control pull-down and an optional normally closed emergency-stop hardware interruption of relay supply. That hardware interruption is a design recommendation, not a verified property of the supplied board. A firmware emergency-stop input alone does not establish an independent hardware power cut.

## Connectors, grounding and inspection

- Use appropriate high-current terminals/wire for charger and contact paths. The source notes reserve XH2.54 connectors for small-signal interfaces.
- The buck converter provides 5 V; the ESP32-S3 board provides the 3.3 V logic rail. Sensor logic grounds are common, but high charging return current should not flow through sensor ground wiring.
- The micro-switch detects presence and must not carry charging current. Use the actual relay/module pinout rather than assuming identical labeling across modules.
- Before any powered integration, reconcile the physical board against the selected pin map and check supply polarity, rail voltages, normally open relay contacts and default-OFF behavior.

Suggested source-design test points include fused positive, protected positive, 5 V, 3.3 V, ground, INA input, relay signal, I²C, NTC ADC and both charging contacts. No continuity, insulation, loaded-current or thermal test was performed here.
