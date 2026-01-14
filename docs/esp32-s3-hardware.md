# ESP32-S3 Super Mini - Hardware Reference

Comprehensive hardware reference for the ESP32-S3 Super Mini development board.

---

## 📋 Board Overview

The **ESP32-S3 SuperMini** is a compact and powerful IoT development board based on the Espressif ESP32-S3 WiFi/Bluetooth dual-mode chip.

### Key Specifications

| Feature | Specification |
|---------|---------------|
| **MCU** | ESP32-S3 Dual-core Xtensa LX7 @ 240MHz |
| **Flash** | 4 MB |
| **SRAM** | 512 KB |
| **WiFi** | 802.11 b/g/n (2.4 GHz) |
| **Bluetooth** | 5.0 (LE) |
| **USB** | USB-C (Native USB-OTG) |
| **Dimensions** | 22.52 × 18 mm |
| **Deep Sleep** | ~43 µA |

### Onboard Features

- ⚡ Ultra-compact: 22.52 × 18 mm
- 📡 Built-in PCB antenna
- 🌈 WS2812 RGB LED on GPIO48
- 🔵 Battery charging indicator LED
- 🔒 Security: AES-128/256, RSA, HMAC, secure boot

---

## 📌 Pin Reference

### ✅ Safe Pins (Recommended)

These pins have no boot conflicts and are safe for any use:

| GPIO | ADC | Notes |
|------|-----|-------|
| IO1 | ✓ | UART TX capable |
| IO2 | ✓ | UART RX capable |
| IO4 | ✓ | General GPIO |
| IO5 | ✓ | General GPIO |
| IO6 | ✓ | General GPIO |
| IO7 | ✓ | General GPIO |
| IO8 | ✓ | I2C SDA default |
| IO15 | | General GPIO |
| IO16 | | General GPIO |
| IO17 | | General GPIO |
| IO18 | | General GPIO |
| IO21 | | General GPIO |

### ⚠️ Caution Pins

| GPIO | Issue |
|------|-------|
| IO0 | Boot mode strapping |
| IO3 | JTAG enable strapping |
| IO45 | VDD_SPI voltage select |
| IO46 | Boot mode strapping |

### ❌ Avoid These Pins

| GPIO | Reason |
|------|--------|
| IO19 | USB D- (breaks USB) |
| IO20 | USB D+ (breaks USB) |
| IO43 | UART0 TX (Serial console) |
| IO44 | UART0 RX (Serial console) |
| IO9-12 | SPI Flash (on some boards) |
| IO26-32 | SPI Flash |

---

## 🔌 Project Pin Mapping

| Function | GPIO | Notes |
|----------|------|-------|
| **STEP** | GPIO5 | Step pulses to stepper driver |
| **DIR** | GPIO6 | Direction control |
| **ENABLE** | GPIO4 | Driver enable (active LOW) |
| **UART TX** | GPIO1 | TMC2209 communication |
| **UART RX** | GPIO2 | TMC2209 communication |
| **DC Motor FI** | GPIO8 | H-bridge forward (PWM) |
| **DC Motor BI** | GPIO9 | H-bridge backward (PWM) |
| **Detect 1** | GPIO11 | Driver auto-detection |
| **Detect 2** | GPIO12 | Driver auto-detection |
| **RGB LED** | GPIO48 | WS2812 status LED |

---

## 🎚️ GPIO Matrix

The ESP32-S3 features a **GPIO Matrix** that allows routing almost any peripheral to any pin:

```cpp
// UART can be remapped to custom pins
Serial1.begin(115200, SERIAL_8N1, 2, 1);  // RX=GPIO2, TX=GPIO1
```

This flexibility enables:
- ✅ Custom pin assignments
- ✅ Hardware UART on any safe GPIO
- ✅ PWM on any GPIO
- ✅ No fixed peripheral pins

---

## ⚡ Peripheral Overview

### UART (3 Controllers)

| UART | Default Use | Project Use |
|------|-------------|-------------|
| UART0 | USB Serial console | Debug output |
| UART1 | Available | TMC2209 communication |
| UART2 | Available | Expansion |

**Configuration:**
```cpp
Serial1.begin(115200, SERIAL_8N1, 2, 1);  // RX=2, TX=1
```

### PWM (8 Channels)

Used for DC motor speed control:
```cpp
ledcAttach(DC_FI_PIN, 20000, 8);  // 20kHz, 8-bit resolution
ledcWrite(DC_FI_PIN, 128);         // 50% duty cycle
```

### ADC (2× 12-bit)

- 20 channels total
- 0-3.3V range (with attenuation)
- ⚠️ ADC2 unavailable when WiFi active

### Timers (4× 64-bit)

Hardware timers for precise timing:
- Used internally by FastAccelStepper
- 1µs resolution possible

---

## 💡 Onboard LEDs

### WS2812 RGB LED (GPIO48)

```cpp
// Using FastLED or similar
#include <FastLED.h>
CRGB leds[1];
FastLED.addLeds<NEOPIXEL, 48>(leds, 1);
leds[0] = CRGB::Green;
FastLED.show();
```

### Blue LED (Battery Indicator)

| State | Meaning |
|-------|---------|
| On | Charging |
| Off | Battery connected |
| Blinking | No battery |

*Not GPIO controllable*

---

## ⚡ Power Requirements

### ESP32-S3 Super Mini

| Parameter | Value |
|-----------|-------|
| Input Voltage | 5V USB or 3.3V direct |
| Operating Range | 3.0V - 3.6V |
| Typical Current | 80mA (WiFi off) |
| WiFi Active | 180mA |
| Peak (WiFi TX) | 500mA |

### GPIO Current Limits

| Limit | Value |
|-------|-------|
| Per Pin (recommended) | 12mA |
| Per Pin (absolute max) | 40mA |
| Total (all pins) | 200mA |

### Power Architecture

```
USB 5V ─────────► [LDO] ──────► 3.3V
                               │
                    ├──► ESP32-S3
                    └──► TMC2209 VIO

Separate PSU ────────────────► TMC2209 VS (12-28V)

⚠️ Common GND Required!
```

---

## 🔧 PlatformIO Configuration

```ini
[env:esp32-s3-mini]
platform = espressif32
board = lolin_s3_mini
framework = arduino
monitor_speed = 115200

lib_deps = 
    teemuatlut/TMCStepper@^0.7.3
    gin66/FastAccelStepper@^0.33.9

build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
```

---

## 🔗 Resources

- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESPBoards - ESP32-S3 Super Mini](https://www.espboards.dev/esp32/esp32-s3-super-mini/)

---

*Last updated: January 2026*
