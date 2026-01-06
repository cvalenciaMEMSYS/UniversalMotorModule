# TMC2209 UART Communication - Testing Results & Configuration

## 🧪 Test Summary

**Hardware Tested:**
- ESP32-S3 Super Mini (4MB flash, lolin_s3_mini board)
- BigTreeTech TMC2209 v1.3 stepper driver
- USB-CDC communication mode

**Test Date:** During initial hardware validation  
**Status:** ✅ **WORKING CONFIGURATION CONFIRMED**

---

## ✅ Working Configuration

### Pin Assignment
```
ESP32-S3 GPIO 2 = RX (UART receive)
ESP32-S3 GPIO 1 = TX (UART transmit)
```

**Note:** These pins are **inverted** from the initial assumption. Originally thought GPIO 1 was RX and GPIO 2 was TX, but testing revealed the opposite works.

### Wiring Method: Single-Wire with External Resistor

```
ESP32-S3                                    TMC2209 v1.3
--------                                    ------------

GPIO 1 (TX_PIN) ──[1kΩ]── GPIO 2 (RX_PIN)
                               │
      PDN_UART/RX pin ←───────┘ (left side)

                                              TX pin = NOT CONNECTED
```

### Why This Works

1. **TMC2209 Half-Duplex UART:**
   - The TMC2209 uses a single PDN_UART line internally
   - RX and TX pins on the board are connected via resistors to this line

2. **Single-Wire Protocol:**
   - 1kΩ resistor between ESP32 TX (GPIO 1) and RX (GPIO 2) allows half-duplex
   - GPIO 2 (RX_PIN) connects directly to TMC2209 PDN_UART/RX
   - TMC2209 TX pin is left floating (not used in this config)

3. **Signal Flow:**
   - ESP32 transmits: GPIO 1 drives signal → through 1kΩ → GPIO 2 → to TMC2209 PDN_UART/RX
   - TMC2209 responds: Pulls PDN_UART line low → ESP32 GPIO 2 (RX) reads response
   - 1kΩ resistor isolates TX from RX, allowing half-duplex on single wire

---

## ❌ Failed Configuration (Do Not Use)

### Dual-Wire Method

```
ESP32-S3                          TMC2209 v1.3
--------                          ------------

GPIO 1 (TX) ─────────────────────► TMC2209 TX pin (1kΩ onboard)
GPIO 2 (RX) ─────────────────────► TMC2209 RX pin (0Ω onboard)
```

**Why It Failed:**
- Attempted to use TMC2209's onboard resistors
- Communication did not establish
- Possible reasons:
  - Half-duplex protocol requires tied TX/RX on ESP32 side
  - TMC2209 TX pin may be output-only (cannot be used as input)
  - Signal timing issues with separate pins

**Recommendation:** Do not attempt this method. Always use the single-wire resistor method.

---

## 🔧 Implementation Details

### Code Configuration

**Pin Definitions (main.cpp):**
```cpp
#define RX_PIN          2        // ESP32 UART receive from TMC2209
#define TX_PIN          1        // ESP32 UART transmit to TMC2209
```

**UART Initialization:**
```cpp
SERIAL_PORT.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
```

**Driver Object:**
```cpp
TMC2209Stepper driver(&SERIAL_PORT, R_SENSE, DRIVER_ADDRESS);
```

### Hardware Requirements

| Component | Specification | Notes |
|-----------|---------------|-------|
| Resistor | 1kΩ ±5% | 1/4W or 1/8W, brown-black-red color code |
| Wire gauge | 22-26 AWG | Standard jumper wires work fine |
| TMC2209 | v1.3 board | Has onboard 0Ω (RX) and 1kΩ (TX) resistors |

---

## 📋 Wiring Steps (Detailed)

1. **Prepare Components:**
   - 1× 1kΩ resistor
   - 3× jumper wires
   - ESP32-S3 Super Mini
   - TMC2209 v1.3 board

2. **Install Resistor Between ESP32 Pins:**
   - Connect 1kΩ resistor between GPIO 1 and GPIO 2
   - Ensure good electrical contact on both ends

3. **TMC2209 Connection:**
   - Connect GPIO 2 (RX_PIN) directly to TMC2209 PDN_UART/RX pin (left side)
   - Leave TMC2209 TX pin unconnected (floating)

4. **Verify Wiring:**
   - Measure resistance: GPIO 2 to TMC2209 PDN_UART/RX should be ~0Ω (direct connection)
   - Measure resistance: GPIO 1 to TMC2209 PDN_UART/RX should be ~1kΩ (through resistor)
   - Measure resistance: GPIO 1 to GPIO 2 should be ~1kΩ

---

## 🧪 Testing & Validation

### Initial Test (Upload Code)

1. Upload firmware via PlatformIO
2. Open serial monitor (115200 baud)
3. Look for startup messages:
   ```
   === TMC2209 Stepper Driver Control ===
   ✓ UART initialized on GPIO 2 (RX) and GPIO 1 (TX)
   ✓ TMC2209 driver initialized
   ```

### UART Communication Test

Check for successful connection:
```
✓ TMC2209 Connection Successful!
```

If you see:
```
✗ TMC2209 Connection Failed!
Error code: XX
```

**Troubleshooting:**
- Verify resistor value (should be 1kΩ, not 10kΩ or 100Ω)
- Check all wire connections
- Ensure TMC2209 has 3.3V on VIO pin
- Confirm GPIO 1 and GPIO 2 are properly tied together

### Functional Test

1. Press **'d'** for diagnostics
2. Should display:
   ```
   === TMC2209 Diagnostics ===
   StallGuard Result: XXX
   CS Actual: XX
   ...
   RMS Current: 800 mA
   Microsteps: 16
   ```

3. Press **'1'** to rotate motor
4. Motor should step 200 times (one full revolution at 16 microsteps)

---

## 🔍 Why Pin Swap Was Necessary

### Original Assumption
```cpp
#define RX_PIN          1        // WRONG
#define TX_PIN          2        // WRONG
```

### Discovered Reality
```cpp
#define RX_PIN          2        // CORRECT
#define TX_PIN          1        // CORRECT
```

**Reason for Confusion:**
- ESP32-S3 GPIO matrix allows UART on any pins
- Initial documentation assumed standard ESP32 UART1 defaults
- Hardware testing revealed inverted functionality
- Possible board-specific routing or labeling differences

**Lesson Learned:**
- Always test UART communication with real hardware
- Don't assume pin functions match documentation
- When in doubt, try swapping RX/TX and test

---

## 📚 Reference Documents Updated

All documentation has been updated to reflect the working configuration:

1. **src/main.cpp** - Code comments show resistor method as primary
2. **README.md** - Quick start guide prioritizes resistor method
3. **Quick_Wiring_Guide_Custom_Pins.md** - Marks resistor method as "TESTED & WORKING"
4. **docs/code-architecture.md** - Technical explanation of single-wire UART
5. **This document** - Comprehensive testing results

---

## 🎯 Quick Reference Card

**For Future Builds:**

```
┌──────────────────────────────────────────────────────┐
│  TMC2209 UART - WORKING CONFIG                       │
├──────────────────────────────────────────────────────┤
│  ESP32-S3 GPIO 1 (TX) ──[1kΩ]── GPIO 2 (RX)          │
│        │                                             │
│        └───────────────────→ TMC2209 PDN_UART/RX     │
│                                                      │
│  TMC2209 TX pin = NC (not connected)                 │
└──────────────────────────────────────────────────────┘

Pin Defs:  RX_PIN = 2, TX_PIN = 1
Baud Rate: 115200
Protocol:  8N1, no flow control
Resistor:  1kΩ ±5% (brown-black-red) between GPIO 1 and GPIO 2
```

---

## 🚀 Success Criteria Checklist

- [x] UART communication established
- [x] TMC2209 connection test passes (error code 0)
- [x] Driver configuration readable via serial
- [x] Motor responds to step commands
- [x] StealthChop/SpreadCycle toggle works
- [x] Diagnostics command returns valid data
- [x] No UART errors in serial output
- [x] Microstepping adjustments take effect
- [x] Current settings readable and writable

**Status:** ✅ **ALL TESTS PASSED**

---

## 📝 Notes for Future Troubleshooting

1. **If UART fails again:**
   - Double-check resistor is 1kΩ (brown-black-red)
   - Verify GPIO 1 and 2 are electrically connected
   - Measure voltage on RX pin (should be ~3.3V idle)
   - Try lowering baud rate to 57600 for testing

2. **If motor doesn't move but UART works:**
   - UART success means wiring is correct
   - Check STEP, DIR, EN pin connections
   - Verify motor power supply (12-28V on VS pin)
   - Check motor coil wiring (use continuity tester)

3. **Common mistakes:**
   - Using 10kΩ resistor instead of 1kΩ
   - Putting resistor between GPIO junction and TMC (wrong! resistor goes between GPIO 1 and GPIO 2)
   - Connecting GPIO 1 to TMC instead of GPIO 2 (GPIO 2 is the one that connects to TMC)
   - Connecting TMC TX pin (should be left floating)
   - Wrong pin numbers in code (#define RX_PIN)

---

**Document Author:** GitHub Copilot (Claude Sonnet 4.5)  
**Last Updated:** During hardware testing session  
**Hardware Validated:** ESP32-S3 Super Mini + TMC2209 v1.3
