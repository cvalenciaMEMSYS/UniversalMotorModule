# Arduino-ESP32 Core Version Decision

> **Decision: This project uses Arduino-ESP32 Core 2.x API**

This document explains why we use the Arduino-ESP32 Core 2.x LEDC API instead of the newer 3.x API, and provides guidance for future development.

---

## Background

The ESP32 Arduino framework has two major versions with **incompatible APIs**:

- **Core 2.x** (ESP-IDF 4.4) - Current PlatformIO default (`platform = espressif32`)
- **Core 3.x** (ESP-IDF 5.x) - Requires `pioarduino` platform package

The most visible difference is the **LEDC (PWM) API**:

| Operation | Core 2.x | Core 3.x |
|-----------|----------|----------|
| Configure PWM | `ledcSetup(channel, freq, res)` | Not needed |
| Attach pin | `ledcAttachPin(pin, channel)` | `ledcAttach(pin, freq, res)` |
| Write duty | `ledcWrite(channel, duty)` | `ledcWrite(pin, duty)` |

---

## Decision Rationale

### Why We Chose Core 2.x

| Factor | Reasoning |
|--------|-----------|
| **TMCStepper Library** | Critical for TMC2209 control; proven stable on 2.x |
| **PlatformIO Native Support** | No extra platform packages needed |
| **Library Compatibility** | Most ESP32 libraries tested against 2.x |
| **Stability** | Motor control requires reliability over features |
| **Documentation** | More tutorials and Stack Overflow answers |
| **Project Simplicity** | Only 4 lines of code needed to adapt |

### Core 3.x Advantages We're Not Using

- Better ESP32-S3 optimizations (not critical for our use case)
- Cleaner LEDC API (minor convenience)
- ESP-IDF 5.x features (not required)
- Improved USB-CDC handling (works fine in 2.x)

---

## Comparison: Core 2.x vs 3.x

### Core 2.x (Our Choice)

**Pros:**
- ✅ Mature, battle-tested codebase
- ✅ Native PlatformIO support
- ✅ Maximum library compatibility
- ✅ Extensive community documentation
- ✅ TMCStepper library verified

**Cons:**
- ❌ Based on older ESP-IDF 4.4
- ❌ Legacy API patterns (channel management)
- ❌ Will eventually reach end-of-life

### Core 3.x

**Pros:**
- ✅ Based on ESP-IDF 5.x (latest SDK)
- ✅ Simplified, cleaner APIs
- ✅ Better ESP32-S3/S2/C3 support
- ✅ Active development
- ✅ Security patches and improvements

**Cons:**
- ❌ Breaking API changes from 2.x
- ❌ Requires pioarduino platform in PlatformIO
- ❌ Some library compatibility issues
- ❌ Fewer community examples (newer)

---

## Code Pattern Reference

### DC Motor PWM (Our Implementation - Core 2.x)

```cpp
// Pin and channel definitions
#define DC_FI_PIN       8
#define DC_BI_PIN       9
#define DC_FI_CHANNEL   0
#define DC_BI_CHANNEL   1
#define DC_PWM_FREQ     20000
#define DC_PWM_RES      8

// Setup (configure channel, then attach pin)
ledcSetup(DC_FI_CHANNEL, DC_PWM_FREQ, DC_PWM_RES);
ledcSetup(DC_BI_CHANNEL, DC_PWM_FREQ, DC_PWM_RES);
ledcAttachPin(DC_FI_PIN, DC_FI_CHANNEL);
ledcAttachPin(DC_BI_PIN, DC_BI_CHANNEL);

// Write (use CHANNEL, not pin)
ledcWrite(DC_FI_CHANNEL, speed);  // ✅ Correct
ledcWrite(DC_FI_PIN, speed);      // ❌ Wrong (would work but unexpected channel)
```

### What Core 3.x Would Look Like

```cpp
// No channel definitions needed
#define DC_FI_PIN       8
#define DC_BI_PIN       9

// Setup (all-in-one)
ledcAttach(DC_FI_PIN, 20000, 8);
ledcAttach(DC_BI_PIN, 20000, 8);

// Write (use PIN directly)
ledcWrite(DC_FI_PIN, speed);
```

---

## Future Migration to Core 3.x

If you decide to migrate in the future:

### 1. Update platformio.ini

```ini
; Replace this:
platform = espressif32

; With this:
platform = https://github.com/pioarduino/platform-espressif32.git
```

### 2. Update LEDC Code

```cpp
// Remove channel definitions and ledcSetup calls
// Change ledcAttachPin(pin, channel) → ledcAttach(pin, freq, res)
// Change ledcWrite(channel, duty) → ledcWrite(pin, duty)
```

### 3. Test TMCStepper Library

Verify UART communication still works after upgrade.

---

## References

- [Arduino-ESP32 Core 3.0 Migration Guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [pioarduino Platform (Core 3.x for PlatformIO)](https://github.com/pioarduino/platform-espressif32)
- [ESP32 LEDC API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html)

---

*Last updated: January 2026*
