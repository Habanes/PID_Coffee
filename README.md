# CoffeePID

ESP32-based PID temperature controller for the QuickMill espresso machine.

## Arduino IDE Setup

### 1. Board Support (Boards Manager)

Add the ESP32 board URL in **File → Preferences → Additional boards manager URLs**:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then in **Tools → Board → Boards Manager**, search for **esp32 by Espressif Systems** and install it.

### 2. Libraries (Library Manager)

Install via **Sketch → Include Library → Manage Libraries**:

| Library | Author |
|---------|--------|
| SevSeg | Dean Reading |
| RotaryEncoder | Matthias Hertel |

### 3. Libraries (Manual GitHub Install)

Download as ZIP and install via **Sketch → Include Library → Add .ZIP Library**:

| Library | URL |
|---------|-----|
| Arduino-PID-Library (rancilio fork) | https://github.com/rancilio-pid/Arduino-PID-Library |
| ZACwire-Library | https://github.com/lebuni/ZACwire-Library | Provides `TSIC.h` — this is the TSIC sensor library |

> **Important:** Do not install the standard `PID_v1` from the Library Manager. It is missing the `SetIntegratorLimits()` and `SetSmoothingFactor()` extensions used here. If both are installed the wrong one will be picked up and the sketch will not compile.

## Hardware Pin Mapping

| Function | GPIO |
|----------|------|
| SSR (relay) | 14 |
| TSIC 306 signal | 21 |
| Rotary encoder A | 6 |
| Rotary encoder B | 5 |
| Rotary button | 4 |
| Brew button | 2 |
| Buzzer (reserved) | 47 |
| Display digit 1–4 | 3, 11, 12, 8 |
| Display segments A–DP | 9, 13, 17, 15, 7, 10, 18, 16 |
