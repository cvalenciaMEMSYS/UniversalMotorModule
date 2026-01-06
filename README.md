# Universal Motor Module - TMC2209 & DC Motor Control

Full-featured motor control system supporting TMC2209 stepper drivers and RZ7899-MS H-bridge DC motor controllers, built with PlatformIO for ESP32-S3 Super Mini.

## 🎯 Quick Start

### Hardware Required
- **ESP32-S3 Super Mini** development board
- **BigTreeTech TMC2209 v1.3** stepper driver
- **Bipolar stepper motor** (4-wire)
- **12-28V DC power supply** for motor
- **Jumper wires**
- **1× 1kΩ resistor** (for UART Option 1) or just wires (for Option 2)
- **100µF capacitor** (recommended for VS power filtering)
- **MSKSEMI RZ7899-MS H-Bridge** (for DC motor control) - OR similar H-bridge
- **DC brushed motor** (optional, for DC motor mode)

---

## ⚡ Quick Setup (5 Minutes)

### 1. Clone & Install
```bash
git clone <your-repo-url>
cd UniversalMotorModule
pio run  # Auto-installs TMCStepper library
```

### 2. Wire Your Hardware

**Essential Connections:**
```
ESP32-S3          TMC2209 v1.3
--------          ------------
GPIO 4     →      EN (Enable)
GPIO 5     →      STEP (Step pulses)
GPIO 6     →      DIR (Direction)
3.3V       →      VIO (Logic power)
GND        →      GND (Common ground)
12-28V     →      VS (Motor power)
```

**UART Connection (Single-Wire Half-Duplex):**

**✅ WORKING METHOD (Resistor - TESTED):**
```
GPIO 1 (TX_PIN) ──[1kΩ]── GPIO 2 (RX_PIN)
      │
      └──────────────────→ TMC2209 PDN_UART/RX pin

TMC2209 TX pin = NOT CONNECTED
```

**Wiring Steps:**
1. Connect 1kΩ resistor between GPIO 1 and GPIO 2
2. From GPIO 1 (not GPIO 2), connect directly to TMC2209 PDN_UART/RX pin
3. Leave TMC2209 TX pin floating (not connected)

**❌ Option 2 - Dual Wire (NOT WORKING):**
```
GPIO 1  ──────────  TMC2209 TX pin
GPIO 2  ──────────  TMC2209 RX pin
```
This method failed in testing. Stick with the resistor method above.

📖 **Detailed wiring:** See [Quick_Wiring_Guide_Custom_Pins.md](Quick_Wiring_Guide_Custom_Pins.md)

**DC Motor H-Bridge (RZ7899-MS):**
```
ESP32-S3          RZ7899-MS
--------          ---------
GPIO 8     →      FI (Pin 2 - Forward Input)
GPIO 9     →      BI (Pin 1 - Backward Input)
GND        →      GND (Pin 3) - common ground

Motor Power (3-25V) →  VCC (Pin 4) - NOT 3.3V!

Motor Wire 1  →   FO (Pins 5+6 tied together)
Motor Wire 2  →   BO (Pins 7+8 tied together)
```
**Note:** VCC is motor power (3-25V), NOT logic power from ESP32!

### 3. Build & Upload
```bash
pio run --target upload
pio device monitor
```

### 4. Test It!
Open Serial Monitor (115200 baud), you should see:
```
==============================================
   Universal Motor Module
   ESP32-S3 Super Mini Edition
==============================================
✓ UART initialized successfully
✓ DC Motor H-Bridge initialized (GPIO 8, 9)
✓ TMC2209 Connection Successful!
```

Press **'h'** for the interactive menu, then:
- **'1'** - Rotate clockwise
- **'2'** - Rotate counter-clockwise
- **'7'** - Toggle quiet/torque mode
- **'d'** - Display diagnostics

---

## 📋 Features

### Motor Control
- ✅ **Precise positioning** - Microstepping from 1 to 256
- ✅ **Bidirectional rotation** - Clockwise and counter-clockwise
- ✅ **Speed control** - Adjustable on-the-fly
- ✅ **Continuous or stepped** motion modes
- ✅ **DC Motor Control** - Forward/backward with PWM speed
- ✅ **Coast Stop** - Gentle motor stopping

### Advanced TMC2209 Features
- ✅ **StealthChop™** - Ultra-quiet operation
- ✅ **SpreadCycle™** - Maximum torque mode
- ✅ **StallGuard™** - Sensorless homing and stall detection
- ✅ **CoolStep™** - Automatic energy optimization
- ✅ **PWM Autoscale** - Automatic current adjustment

### Safety & Diagnostics
- ✅ **Thermal monitoring** - Overtemperature warnings
- ✅ **Short circuit detection** - Phase A & B monitoring
- ✅ **Open load detection** - Disconnected motor detection
- ✅ **Real-time diagnostics** - Complete driver status readout

---

## 🎮 Serial Commands & Examples

### Basic Movement Commands

**Command '1' - Rotate Clockwise**
```
Input:  1
Action: Motor rotates 200 steps clockwise (1 full revolution)
Output: Rotating clockwise (200 steps)...
```

**Command '2' - Rotate Counter-Clockwise**
```
Input:  2
Action: Motor rotates 200 steps counter-clockwise
Output: Rotating counter-clockwise (200 steps)...
```

**Command '3' - Continuous Rotation CW**
```
Input:  3
Action: Motor spins continuously clockwise
Output: Continuous rotation CW (press '0' to stop)
Note:   Press '0' to stop the rotation
```

**Command '4' - Continuous Rotation CCW**
```
Input:  4
Action: Motor spins continuously counter-clockwise
Output: Continuous rotation CCW (press '0' to stop)
Note:   Press '0' to stop the rotation
```

**Command '0' - Stop Continuous Rotation**
```
Input:  0
Action: Stops any continuous rotation
Output: Stopped
```

---

### Speed Control Commands

**Command '5' - Increase Speed**
```
Input:  5
Action: Reduces delay between steps (faster motion)
Output: Speed increased. Delay: 900 µs
Note:   Minimum delay is 100µs
```

**Command '6' - Decrease Speed**
```
Input:  6
Action: Increases delay between steps (slower motion)
Output: Speed decreased. Delay: 1100 µs
Note:   Maximum delay is 5000µs
```

---

### Driver Mode Commands

**Command '7' - Toggle Chopper Mode**
```
Input:  7
Action: Toggles between StealthChop (quiet) and SpreadCycle (torque)
Output: StealthChop ENABLED (Quiet mode)
   OR:  SpreadCycle ENABLED (High torque mode)
Use:    These modes are mutually exclusive (only one can be active)
        StealthChop = Ultra-quiet, best for 3D printing, low-noise
        SpreadCycle = Maximum torque, best for CNC, heavy loads
Note:   Press '7' repeatedly to switch between modes
```

---

### Configuration Commands

**Command '9' - Change Microstepping**
```
Input:  9
Output: Select microstepping:
        1: 1  | 2: 2  | 3: 4  | 4: 8
        5: 16 | 6: 32 | 7: 64 | 8: 128 | 9: 256

Input:  5 (for example)
Output: Microstepping set to: 16
Note:   Higher = smoother but slower maximum speed
```

**Command 'c' - Change Motor Current**
```
Input:  c
Output: Enter RMS current in mA (e.g., 800):

Input:  1000
Output: Current set to: 1000 mA
Note:   Use 70-80% of your motor's rated current
        Range: 100-2000 mA
```

**Command 'e' - Toggle Enable/Disable**
```
Input:  e
Output: Driver DISABLED
Input:  e (again)
Output: Driver ENABLED
Note:   When disabled, motor has no holding torque
```

**Command 'r' - Reset Driver**
```
Input:  r
Output: Resetting driver...
        Driver reset complete
Note:   Restores default settings (current, microsteps, mode)
```

---

### Diagnostic Commands

**Command 's' - Read StallGuard Value**
```
Input:  s
Output: === StallGuard Reading ===
        SG Result: 142
        SG Threshold: 10
        Standstill: No
Note:   High value = light load, Low value = heavy load/stall
```

**Command 'd' - Full Diagnostics**
```
Input:  d
Output: === TMC2209 Diagnostics ===
        StallGuard Result: 156
        CS Actual: 18
        Standstill: 0
        Open Load A: 0
        Open Load B: 0
        Low-side short A: 0
        Low-side short B: 0
        Ground short A: 0
        Ground short B: 0
        Overtemperature: 0
        Overtemp Warning: 0
        Temperature: ✓ Normal
        
        === Current Configuration ===
        RMS Current: 800 mA
        Microsteps: 16
        PWM Scale: 43
        Mode: StealthChop
        TOFF: 5
Note:   All values should be 0 for error flags
        Temperature should show "Normal"
```

**Command 'h' - Show Help Menu**
```
Input:  h
Output: ========================================
            TMC2209 Control Menu
        ========================================
        Basic Movement:
          1 - Rotate clockwise (200 steps)
          2 - Rotate counter-clockwise (200 steps)
          ...
        ========================================
```

**Command 'x' - Restart ESP32**
```
Input:  x
Output: Restarting ESP32...
        [Device restarts and shows startup banner]
Note:   Performs software reset via esp_restart()
        Useful for applying changes or recovering from errors
```

---

### Quick Command Reference

| Key | Function | Expected Response |
|-----|----------|-------------------|
| **1** | Rotate CW | `Rotating clockwise (200 steps)...` |
| **2** | Rotate CCW | `Rotating counter-clockwise (200 steps)...` |
| **3** | Continuous CW | `Continuous rotation CW (press '0' to stop)` |
| **4** | Continuous CCW | `Continuous rotation CCW (press '0' to stop)` |
| **0** | Stop | `Stopped` |
| **5** | Speed Up | `Speed increased. Delay: XXX µs` |
| **6** | Speed Down | `Speed decreased. Delay: XXX µs` |
| **7** | Toggle Chopper | `StealthChop ENABLED` or `SpreadCycle ENABLED` |
| **9** | Microstepping | Menu → `Microstepping set to: XX` |
| **c** | Current | Prompt → `Current set to: XXXX mA` |
| **e** | Toggle Enable | `Driver ENABLED` or `Driver DISABLED` |
| **r** | Reset | `Resetting driver...` → `Driver reset complete` |
| **s** | StallGuard | StallGuard data with SG result and threshold |
| **d** | Diagnostics | Complete driver status report (LIVE from UART) |
| **t** | Test Connection | UART connection test with multiple checks |
| **h** | Help | Full command menu |
| **x** | Restart ESP32 | `Restarting ESP32...` → Device reboots |
| **f** | DC Forward | `DC Motor FORWARD at speed XXX` |
| **b** | DC Backward | `DC Motor BACKWARD at speed XXX` |
| **o** | DC Stop | `DC Motor STOPPED (coast)` |
| **p** | DC Speed | Prompt → `DC Motor speed set to: XXX` |

---

## 🎮 DC Motor Commands

| Key | Function | Description |
|-----|----------|-------------|
| **f** | Forward | DC motor forward at current speed |
| **b** | Backward | DC motor backward at current speed |
| **o** | Stop | Stop DC motor (coast mode) |
| **p** | Speed | Set DC motor PWM speed (0-255) |

**Command 'f' - DC Motor Forward**
```
Input:  f
Output: DC Motor FORWARD at speed 128
```

**Command 'b' - DC Motor Backward**
```
Input:  b  
Output: DC Motor BACKWARD at speed 128
```

**Command 'p' - Set Speed**
```
Input:  p
Output: Enter DC motor speed (0-255):
Input:  200
Output: DC Motor speed set to: 200
```

**Command 'o' - Stop Motor**
```
Input:  o
Output: DC Motor STOPPED (coast)
```

---

## 🔧 Configuration

### Default Settings
```cpp
#define STEP_PIN        5        // Step pulses
#define DIR_PIN         6        // Direction
#define ENABLE_PIN      4        // Enable (LOW = on)
#define RX_PIN          1        // UART receive
#define TX_PIN          2        // UART transmit

#define MOTOR_STEPS     200      // 1.8° motor
#define MICROSTEPS      16       // 16x microstepping
#define RMS_CURRENT     800      // 800mA motor current
#define R_SENSE         0.11f    // TMC2209 v1.3 value

// DC Motor H-Bridge (RZ7899-MS)
#define DC_FI_PIN       8        // Forward Input
#define DC_BI_PIN       9        // Backward Input
```

### Customizing for Your Motor

**1. Set Current (70-80% of rated current):**
```cpp
#define RMS_CURRENT     1000     // For 1.4A motor
```

**2. Adjust Microstepping:**
```cpp
#define MICROSTEPS      32       // Higher = smoother, slower
```

**3. Change Pins (if needed):**
- Safe GPIO pins: 1, 2, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 17, 18, 21
- **Avoid:** 0, 3, 19, 20, 43, 44, 45, 46, 48

---

## 📚 Documentation

- **[Quick Wiring Guide](Quick_Wiring_Guide_Custom_Pins.md)** - Visual pinout and connections
- **[Code Deep Dive](docs/code-architecture.md)** - How the code works
- **[ESP32-S3 Capabilities](docs/esp32-s3-guide.md)** - MCU features and specifications
- **[ESP32-S3 Super Mini Board Reference](docs/esp32-s3-super-mini-board.md)** - Complete board pinout, safe pins, and specifications ([source](https://www.espboards.dev/esp32/esp32-s3-super-mini/))
- **[TMC2209 Features](docs/tmc2209-guide.md)** - Stepper driver capabilities and configuration
- **[DC Motor Guide](docs/dc-motor-guide.md)** - RZ7899-MS H-bridge specs and DC motor control

---

## 🚨 Troubleshooting

### "TMC2209 Connection Failed"
1. Check all GND connections are common
2. Verify VIO has 3.3V
3. Try UART Option 2 instead of Option 1
4. Check resistor value (should be 1kΩ for Option 1)

### Motor Not Moving
1. Verify EN pin is LOW (enabled)
2. Check motor current setting (`'c'` command)
3. Test with `'1'` or `'2'` commands
4. Press `'d'` to check for error flags

### Motor Stuttering/Skipping
1. Increase motor current (`'c'` command)
2. Slow down speed (`'6'` command)
3. Switch to SpreadCycle (`'8'` command)
4. Check power supply can handle current

### USB Serial Not Working
- ESP32-S3 USB takes 1-3 seconds to enumerate
- Press reset button after upload
- Check `platformio.ini` has `-DARDUINO_USB_CDC_ON_BOOT=1`

### Overheating
1. Add heatsink to TMC2209
2. Reduce motor current
3. Add active cooling (fan)
4. Check for short circuits

### DC Motor Not Running
1. Check H-bridge power connections (VM)
2. Verify GPIO 8/9 connections to FI/BI
3. Test with 'f' command - motor should spin
4. Try increasing speed with 'p' command (e.g., 200)

---

## ⚙️ PlatformIO Setup

**platformio.ini:**
```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200

lib_deps = 
    teemuatlut/TMCStepper@^0.7.3

build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
```

**Build Commands:**
```bash
pio run                    # Build
pio run -t upload         # Upload to board
pio device monitor        # Serial monitor
pio run -t clean          # Clean build files
```

---

## 📌 Pin Reference

### ESP32-S3 Super Mini GPIO Safety

✅ **Safe to use:** 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21

⚠️ **Avoid:**
- GPIO 0, 3 - Strapping pins (boot mode)
- GPIO 19, 20 - USB D-/D+ (breaks USB)
- GPIO 43, 44 - UART0 TX/RX (Serial console)
- GPIO 45, 46 - Strapping pins
- GPIO 48 - Onboard LED (usually)

---

## 🙏 Credits

- **TMCStepper Library** by teemuatlut
- **BigTreeTech** for TMC2209 v1.3 hardware
- **Trinamic** for TMC2209 IC and documentation

---

## 🔗 Resources

- [TMCStepper Library Documentation](https://github.com/teemuatlut/TMCStepper)
- [TMC2209 Datasheet](https://www.trinamic.com/products/integrated-circuits/details/tmc2209-la/)
- [ESP32-S3 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [PlatformIO Documentation](https://docs.platformio.org/)

---

**For technical support:** Check the [docs](docs/) folder for detailed guides
