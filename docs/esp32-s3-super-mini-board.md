# ESP32-S3 Super Mini Board Reference

> **Source:** [ESPBoards - ESP32-S3 Super Mini](https://www.espboards.dev/esp32/esp32-s3-super-mini/)

Complete technical reference for the ESP32-S3 Super Mini development board used in this project.

---

## 📋 Overview

The **ESP32-S3 SuperMini** is a compact and powerful IoT development board based on the Espressif ESP32-S3 WiFi/Bluetooth dual-mode chip. It features a dual-core Xtensa LX7 processor running up to 240 MHz, delivering impressive performance for embedded projects.

### Key Features

| Feature | Specification |
|---------|---------------|
| **Microcontroller** | ESP32-S3 |
| **Architecture** | Xtensa LX7 (Dual-core) |
| **Clock Speed** | 240 MHz |
| **Flash Memory** | 4MB |
| **SRAM** | 512 KB |
| **ROM** | 384 KB |
| **WiFi** | 802.11 b/g/n (2.4 GHz) |
| **Bluetooth** | 5.0 (LE) |
| **USB** | USB-C (Native) |
| **Dimensions** | 22.52 × 18 mm |
| **Deep Sleep Power** | ~43 µA |

### Highlights

- ⚡ **Ultra-compact design:** 22.52 × 18 mm
- 📡 **Built-in PCB antenna** for reliable WiFi/Bluetooth
- 🔒 **Security features:** AES-128/256, RSA, HMAC, secure boot
- 🌈 **Onboard WS2812 RGB LED** on GPIO48
- 🔋 **Battery charging indicator** (Blue LED)

---

## 🔌 Pin Reference

### GPIO Summary

| Type | Count |
|------|-------|
| Digital I/O | 11 |
| Analog Input (ADC) | 6 |
| PWM Capable | 11 |
| Interrupts | 22 |

---

## ✅ Safe Pins to Use

These pins are safe for general GPIO usage without boot or system conflicts:

| GPIO | Status | Notes |
|------|--------|-------|
| **IO1** | ✅ Safe | ADC capable |
| **IO2** | ✅ Safe | ADC capable |
| **IO4** | ✅ Safe | ADC capable |
| **IO5** | ✅ Safe | ADC capable |
| **IO6** | ✅ Safe | ADC capable |
| **IO7** | ✅ Safe | ADC capable |
| **IO8** | ✅ Safe | WS2812/SDA |
| **IO15** | ✅ Safe | General GPIO |
| **IO16** | ✅ Safe | General GPIO |
| **IO17** | ✅ Safe | General GPIO |
| **IO18** | ✅ Safe | General GPIO |
| **IO21** | ✅ Safe | General GPIO |

### Why These Pins Are Safe

- ✓ No boot sequence involvement
- ✓ No flash/PSRAM connections
- ✓ No USB or JTAG conflicts
- ✓ Freely assignable without issues

---

## ⚠️ Pins to Avoid or Use with Caution

Reserved for critical functions. Misuse may cause boot failures, programming issues, or system conflicts.

### Strapping Pins

| GPIO | Label | Issue |
|------|-------|-------|
| **IO3** | GPIO3 | Sampled at reset to select JTAG interface. Improper use can disable external JTAG or alter debug interface |

### Flash/SPI Pins

| GPIO | Label | Issue |
|------|-------|-------|
| **IO9** | FSPIHD | Connected to external flash (data/hold signal). Required for flash communication |
| **IO10** | FSPICS0 | Flash chip select. Required for flash access |
| **IO11** | FSPID | Flash data line. Must remain dedicated to flash |
| **IO12** | FSPICLK | Flash clock. Critical signal for memory access |

### USB Pins

| GPIO | Issue |
|------|-------|
| **IO19** | USB D- (breaks USB if used) |
| **IO20** | USB D+ (breaks USB if used) |

### UART Serial (Default)

| GPIO | Issue |
|------|-------|
| **IO43** | UART0 TX (Serial console) |
| **IO44** | UART0 RX (Serial console) |

---

## 💡 Onboard LEDs

### 🔴 Red LED (Power Indicator)

| Property | Value |
|----------|-------|
| **GPIO** | GPIO48 |
| **Control** | `digitalWrite()` |
| **Note** | Shares GPIO48 with WS2812 LED |

```cpp
void setup() {
  pinMode(48, OUTPUT);
}

void loop() {
  digitalWrite(48, HIGH);
  delay(1000);
  digitalWrite(48, LOW);
  delay(1000);
}
```

### 🔵 Blue LED (Battery Charge Indicator)

| State | Behavior |
|-------|----------|
| ⚡ Charging | LED on |
| ✅ Battery connected | LED off |
| 🔋 No battery | LED blinks |

**Note:** This LED is not controllable via GPIO.

### 🌈 WS2812 RGB LED (Programmable)

| Property | Value |
|----------|-------|
| **GPIO** | GPIO48 |
| **Control** | FastLED, NeoPixel, etc. |
| **Note** | Shares GPIO48 with Red LED |

```cpp
#include <FastLED.h>

#define NUM_LEDS 1
#define DATA_PIN 48

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
}

void loop() {
  leds[0] = CRGB::Red; FastLED.show(); delay(1000);
  leds[0] = CRGB::Green; FastLED.show(); delay(1000);
  leds[0] = CRGB::Blue; FastLED.show(); delay(1000);
}
```

> ⚠️ **Warning:** Both the Red LED and WS2812 share GPIO48. They use different signal types (digital vs. timing-based), but due to the board's fixed hardware design, they cannot be used independently.

---

## 📐 Board Dimensions

| Dimension | Value |
|-----------|-------|
| Width | 18 mm |
| Length | 23.50 mm |
| Pin Gap | 2.54 mm |

---

## 🔧 Build Configuration (PlatformIO)

### Default Settings

| Setting | Value |
|---------|-------|
| Bootloader tool | esptool_py |
| Uploader tool | esptool_py |
| Network uploader | esp_ota |
| Flash mode | QIO |
| Boot mode | QIO |
| Max upload size | 1,280 KB (1,310,720 bytes) |
| Max data size | 320 KB (327,680 bytes) |

### Recommended platformio.ini

```ini
[env:esp32-s3-supermini]
platform = espressif32
board = lolin_s3_mini          ; Compatible board definition
framework = arduino
monitor_speed = 115200

build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
```

---

## 🛒 Where to Buy

| Vendor | Notes |
|--------|-------|
| [ESPBoards Store](https://www.espboards.dev/) | Curated selection |
| Amazon | Ships worldwide |
| AliExpress | Best value |

**Typical price:** ~$5 per unit

---

## 🔗 Resources

- **[ESPBoards - ESP32-S3 Super Mini](https://www.espboards.dev/esp32/esp32-s3-super-mini/)** - Complete board documentation
- **[ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)** - Microcontroller reference
- **[ESP32-S3 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)** - Full technical manual

---

## 📌 Pin Mapping Table (This Project)

For the TMC2209 stepper control project, we use these pins:

| Function | GPIO | Board Pin | Notes |
|----------|------|-----------|-------|
| STEP | GPIO5 | IO5 | Step pulses to driver |
| DIR | GPIO6 | IO6 | Direction control |
| ENABLE | GPIO4 | IO4 | Driver enable (active LOW) |
| UART RX | GPIO1 | IO1 | Receive from TMC2209 |
| UART TX | GPIO2 | IO2 | Transmit to TMC2209 |

All selected pins are in the **✅ Safe** category with:
- ADC capability (though not used)
- No boot conflicts
- Full GPIO functionality
- Hardware UART support via GPIO matrix
