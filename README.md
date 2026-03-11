# Universal Motor Module

Full-featured motor control system supporting TMC2209/TMC2208 stepper drivers, STSPIN220 simple stepper drivers, and RZ7899-MS H-bridge DC motor controllers, built with PlatformIO for ESP32-S3 Super Mini. Automatically detects connected hardware via jumper pins.

**Features:**
- 🚀 **FastAccelStepper** library for high-performance pulse generation (up to 200kHz+)
- 🔇 **StealthChop/SpreadCycle** modes for quiet or high-torque operation
- 📍 **Hardware position tracking** - accurate step counting
- ⚡ **S-curve acceleration** for smooth motion
- 🔄 **Auto-enable/disable** for power saving
- 🔍 **Auto-detect hardware** - jumper-based driver selection at startup
- 💡 **WS2812 status LED** - color-coded system state feedback

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
      TMC2209 PDN_UART/RX pin ←┘

TMC2209 TX pin = NOT CONNECTED
```

**Wiring Steps:**
1. Connect 1kΩ resistor between GPIO 1 and GPIO 2
2. From GPIO 2 (not GPIO 1), connect directly to TMC2209 PDN_UART/RX pin
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
╔═══════════════════════════════════════════════════════════╗
║         UNIVERSAL MOTOR MODULE v1.0                       ║
╚═══════════════════════════════════════════════════════════╝

[Hardware detection info — auto-detects driver via jumper pins]

✓ Motor controller ready!
  Driver: TMC2209
```

Type `help` to see all available commands, then try:
- `move 200` — Rotate 200 steps forward
- `move -200` — Rotate 200 steps backward
- `status` — Show full motor status
- `stealthchop` — Switch to silent mode

---

## 📋 Features

### Motor Control
- ✅ **Text-based command interface** - Human-readable serial commands (115200 baud)
- ✅ **Precise positioning** - Hardware-based step counting via FastAccelStepper
- ✅ **High-speed operation** - Up to 200kHz+ step rates
- ✅ **Bidirectional rotation** - Relative and absolute positioning
- ✅ **Speed control** - Adjustable on-the-fly via `set speed`
- ✅ **Acceleration profiles** - Trapezoidal and S-curve (cubesteps)
- ✅ **Continuous rotation** - `run forward` / `run backward` with acceleration
- ✅ **Auto-enable/disable** - Automatic motor power management
- ✅ **DC Motor Control** - Forward/backward with PWM speed via unified commands
- ✅ **Multi-driver support** - TMC2209, TMC2208, STSPIN220, and DC Motor (RZ7899)

### Hardware Detection
- ✅ **Auto-detect driver** - Jumper-based hardware detection at startup
- ✅ **No code changes needed** - Same firmware works with all supported drivers
- ✅ **Detection pins** - GPIO 10/13 (VCC source), GPIO 11/12 (sense inputs)

### Status LED (WS2812 NeoPixel)
- ✅ **Color-coded driver type** - Green (TMC2209), Cyan (TMC2208), Magenta (STSPIN220), Blue (DC Motor)
- ✅ **System state indication** - Ready, Moving, Idle, Error, Warning, Stall
- ✅ **Command feedback** - Orange flash on command received
- ✅ **Startup animation** - RGB chase sequence on boot

### Advanced TMC2209/TMC2208 Features
- ✅ **StealthChop™** - Ultra-quiet operation
- ✅ **SpreadCycle™** - Maximum torque mode
- ✅ **StallGuard™** - Sensorless homing and stall detection (TMC2209)
- ✅ **PWM Autoscale** - Automatic current adjustment
- ✅ **Step/Dir fallback** - Works without UART if communication fails

### STSPIN220 Features
- ✅ **Simple Step/Dir** - No communication needed, just wire and go
- ✅ **Auto standby** - Driver auto-manages power internally (no EN pin needed)
- ✅ **Full step mode** - Hardware-configured microstepping via MODE pins
- ✅ **Hardware current limit** - Set via Vref potentiometer on Pololu board

### Safety & Diagnostics
- ✅ **Thermal monitoring** - Overtemperature warnings
- ✅ **Short circuit detection** - Phase A & B monitoring
- ✅ **Open load detection** - Disconnected motor detection
- ✅ **Real-time diagnostics** - Complete driver status readout via `diag`
- ✅ **Input validation** - Range checking on all command parameters

---

## 🎮 Serial Commands & Examples

All commands are text-based, entered via Serial Monitor at **115200 baud**. Commands are case-insensitive. The system echoes each command with a `> ` prefix and flashes the LED orange on receipt.

### Motion Commands

**`move <steps>` — Relative Move**
```
Input:  move 200
Output: > move 200
        Complete

Input:  move -50
Output: > move -50
        Complete
Note:   Range: ±1,000,000 steps per command
```

**`abs <position>` — Absolute Move**
```
Input:  abs 1000
Output: > abs 1000
        Complete
Note:   Stepper: position must be ≥ 0 (max 100,000,000)
        DC Motor: value is speed -100 to +100 (negative = reverse)
```

**`run forward` — Continuous Forward Rotation**
```
Input:  run forward
Output: > run forward
Note:   Aliases: "runforward", "run f"
        Use "stop" or "brake" to halt
```

**`run backward` — Continuous Backward Rotation**
```
Input:  run backward
Output: > run backward
Note:   Aliases: "runbackward", "run b"
        Use "stop" or "brake" to halt
```

**`stop` — Emergency Stop**
```
Input:  stop
Output: > stop
        Emergency stop!
Note:   Immediate halt, no deceleration ramp
```

**`brake` — Controlled Stop**
```
Input:  brake
Output: > brake
        Braking with deceleration...
Note:   Decelerates to stop using configured acceleration profile
```

**`home` — Find Home Position**
```
Input:  home
Output: > home
        Homing...
Note:   TMC2209 only — uses StallGuard for sensorless homing
```

---

### Query Commands

**`get pos` — Current Position**
```
Input:  get pos
Output: Position: 1000 steps
```

**`get target` — Target Position**
```
Input:  get target
Output: Target: 2000 steps
```

**`get speed` — Actual Speed**
```
Input:  get speed
Output: Current speed: 500 steps/s
```

**`get rampstate` — Ramp Generator State**
```
Input:  get rampstate
Output: Ramp state: ACCELERATING (direction: FORWARD)
Note:   States: IDLE, COASTING, ACCELERATING, DECELERATING, REVERSING
```

---

### Configuration Commands

**`set speed <val>` — Maximum Speed**
```
Input:  set speed 2000
Output: Speed set to 2000.00 steps/sec
Note:   Range: 1 to 200,000 steps/sec
```

**`set accel <val>` — Acceleration**
```
Input:  set accel 500
Output: Acceleration set to 500.00 steps/sec²
Note:   Range: 0 to 1,000,000 steps/sec²
        0 = constant velocity (very high accel, no ramp)
        Alias: "set acceleration <val>"
```

**`set cubesteps <n>` — S-Curve Smoothing**
```
Input:  set cubesteps 100
Output: (S-curve ramp applied over 100 steps)
Note:   0 = trapezoidal acceleration (default)
        >0 = S-curve smoothing over N steps (max 10,000)
```

**`set current <mA>` — Motor Run Current**
```
Input:  set current 800
Output: Current set to 800 mA
Note:   Range: 100 to 3,000 mA (UART drivers only)
        Use 70-80% of your motor's rated current
```

**`set ihold <%>` — Hold Current Percent**
```
Input:  set ihold 50
Output: (Hold current set to 50% of run current)
Note:   Range: 0-100%. Default: 0% (no hold)
```

**`set microsteps <n>` — Microstepping**
```
Input:  set microsteps 32
Output: Microsteps set to 32
Note:   Must be power of 2: 1, 2, 4, 8, 16, 32, 64, 128, 256
        UART drivers only. Higher = smoother but slower max speed
```

**`set autodisable on|off` — Auto Enable/Disable**
```
Input:  set autodisable off
Output: (Auto-disable turned off)
Note:   When ON (default), motor is automatically disabled after moves
```

---

### Enable/Disable

**`enable` / `disable` — Motor Driver Power**
```
Input:  enable
Output: Motor enabled

Input:  disable
Output: Motor disabled
Note:   When disabled, motor has no holding torque
```

---

### UART Control (TMC2209/TMC2208 Only)

**`stealthchop` — Silent Mode**
```
Input:  stealthchop
Output: (StealthChop enabled)
Note:   Ultra-quiet operation, best for low-noise applications
```

**`spreadcycle` — High Torque Mode**
```
Input:  spreadcycle
Output: (SpreadCycle enabled)
Note:   Maximum torque, best for CNC and heavy loads
```

**`pwmautoscale on|off` — Auto Current Reduction**
```
Input:  pwmautoscale on
Output: (PWM autoscale enabled)
Note:   Automatically reduces current when motor has low load
```

**`stepdir on|off` — Step/Dir Fallback Mode**
```
Input:  stepdir on
Output: (Step/Dir mode enabled, UART commands disabled)
Note:   Bypasses UART, uses only STEP/DIR pins
        Useful if UART communication is unreliable
```

**`reconfigure` — Re-apply UART Settings**
```
Input:  reconfigure
Output: Reconfiguring TMC2209...
Note:   Re-sends all UART config to driver. Alias: "reconfig"
        Useful after power glitch or driver reset
```

**`scan` — Scan UART Addresses**
```
Input:  scan
Output: (Scans all 4 TMC2209 UART addresses)
Note:   TMC2209 only. Useful for debugging multi-driver setups
```

---

### Status & Debug Commands

**`status` or `?` — Full Status Report**
```
Input:  status
Output: ╔═══════════════════════════════════════════════════════════╗
        ║                      MOTOR STATUS                         ║
        ╚═══════════════════════════════════════════════════════════╝

          Driver:       TMC2209 (UART mode)
          State:        STOPPED
          Position:     0 steps
          Target:       0 steps
          Speed:        0 / 1000 steps/s (current / max)
          Ramp:         IDLE
          Current:      100mA run, 0% hold
          Accel:        500 steps/s², cubesteps: 0 (trapezoidal)
          Auto-disable: ON
```

**`test` or `t` — Connection Test**
```
Input:  test
Output: ✓ Connection OK
Note:   Tests UART communication with the driver
```

**`diag` or `r` — Full Diagnostics**
```
Input:  diag
Output: (Complete TMC register readout — StallGuard, current,
         temperature, error flags, mode, configuration)
Note:   Shows live data from the driver via UART
```

**`help` or `h` — Show All Commands**
```
Input:  help
Output: ┌─────────────────────────────────────────────────────────────┐
        │                     AVAILABLE COMMANDS                      │
        ├─────────────────────────────────────────────────────────────┤
        │  Motion:                                                    │
        │    move <steps>      Relative move (+ or -)                 │
        │    abs <position>    Move to absolute position (>= 0)       │
        │    ...                                                      │
        └─────────────────────────────────────────────────────────────┘
```

**`reboot` or `restart` — Restart ESP32**
```
Input:  reboot
Output: Rebooting...
        [LED plays rainbow sweep → fade animation]
        [Device restarts and shows startup banner]
Note:   Performs software reset via ESP.restart()
```

---

### Quick Command Reference

| Command | Function | Example |
|---------|----------|---------|
| `move <steps>` | Relative move | `move 200`, `move -50` |
| `abs <position>` | Absolute move / DC speed | `abs 1000`, `abs -75` |
| `run forward` | Continuous forward | `run f` |
| `run backward` | Continuous backward | `run b` |
| `stop` | Emergency stop | `stop` |
| `brake` | Controlled stop | `brake` |
| `home` | Sensorless homing (TMC2209) | `home` |
| `enable` | Enable motor driver | `enable` |
| `disable` | Disable motor driver | `disable` |
| `set speed <val>` | Max speed (steps/sec) | `set speed 2000` |
| `set accel <val>` | Acceleration (steps/sec²) | `set accel 500` |
| `set cubesteps <n>` | S-curve ramp (0=trap) | `set cubesteps 100` |
| `set current <mA>` | Motor current (UART only) | `set current 800` |
| `set ihold <%>` | Hold current percent | `set ihold 50` |
| `set microsteps <n>` | Microstepping (UART only) | `set microsteps 32` |
| `set autodisable on/off` | Auto enable/disable | `set autodisable off` |
| `stealthchop` | Silent mode (TMC only) | `stealthchop` |
| `spreadcycle` | High torque mode (TMC only) | `spreadcycle` |
| `pwmautoscale on/off` | Auto current reduction | `pwmautoscale on` |
| `stepdir on/off` | Step/Dir fallback | `stepdir on` |
| `reconfigure` | Re-apply UART config | `reconfigure` |
| `scan` | Scan UART addresses (TMC2209) | `scan` |
| `status` or `?` | Full status report | `status` |
| `test` or `t` | Connection test | `test` |
| `diag` or `r` | Full diagnostics | `diag` |
| `help` or `h` | Show all commands | `help` |
| `reboot` / `restart` | Restart ESP32 | `reboot` |

---

## 🎮 DC Motor Notes

DC motors use the **same unified command interface** as stepper motors. Key differences:

| Command | Stepper Behavior | DC Motor Behavior |
|---------|-----------------|-------------------|
| `abs <value>` | Move to absolute position (≥ 0) | Set speed (-100 to +100, negative = reverse) |
| `run forward` | Continuous rotation CW | Motor forward at current speed |
| `run backward` | Continuous rotation CCW | Motor backward at current speed |
| `stop` | Emergency stop | Coast stop (motor free) |
| `brake` | Decelerate to stop | Brake stop (motor locked) |
| `set speed <val>` | Max speed in steps/sec | Max PWM duty cycle |
| `enable` / `disable` | Enable/disable driver | Enable/disable H-bridge |

---

## 🔧 Configuration

### Default Settings
```cpp
// Pin Configuration (PinConfig.h)
constexpr uint8_t TMC_TX_PIN      = 1;    // UART TX (through 1kΩ to RX)
constexpr uint8_t TMC_RX_PIN      = 2;    // UART RX → TMC2209 PDN_UART/RX
constexpr uint8_t TMC_EN_PIN      = 4;    // Enable (active LOW)
constexpr uint8_t TMC_STEP_PIN    = 5;    // Step pulses
constexpr uint8_t TMC_DIR_PIN     = 6;    // Direction
constexpr uint8_t DC_IN1_PIN      = 8;    // H-bridge input 1
constexpr uint8_t DC_IN2_PIN      = 9;    // H-bridge input 2
constexpr float   TMC_R_SENSE     = 0.11f; // TMC2209 v1.3 sense resistor

// Hardware Detection Pins
constexpr uint8_t DETECT_VCC_1    = 10;   // Output HIGH (VCC source)
constexpr uint8_t DETECT_VCC_2    = 13;   // Output HIGH (VCC source)
constexpr uint8_t DETECT_BIT_0    = 11;   // Input pull-down (DC motor flag)
constexpr uint8_t DETECT_BIT_1    = 12;   // Input pull-down (TMC2208 flag)

// Default Motor Parameters (DefaultMotorConfig namespace)
constexpr uint16_t STEPPER_CURRENT_MA   = 100;    // Safe startup (100mA)
constexpr uint16_t STEPPER_MICROSTEPS   = 16;     // 16x microstepping
constexpr float    STEPPER_MAX_SPEED    = 1000.0f; // Steps per second
constexpr float    STEPPER_ACCELERATION = 500.0f;  // Steps per second²
constexpr bool     STEPPER_AUTO_DISABLE = true;    // Auto-disable after moves
```

### Hardware Detection (Jumper Configuration)

| GPIO 11 | GPIO 12 | Detected Driver |
|---------|---------|-----------------|
| LOW | LOW | TMC2209 (default — no jumper needed) |
| HIGH | LOW | DC Motor (RZ7899) |
| LOW | HIGH | TMC2208 |
| HIGH | HIGH | STSPIN220 |

Connect GPIO 11 or 12 to GPIO 10 or 13 (which output HIGH) using a jumper wire to select the driver.

### Customizing for Your Motor

**1. Set current at runtime (70-80% of rated current):**
```
set current 800
```

**2. Adjust microstepping at runtime:**
```
set microsteps 32
```

**3. Change defaults in code (`PinConfig.h`):**
```cpp
namespace DefaultMotorConfig {
    constexpr uint16_t STEPPER_CURRENT_MA = 800;   // For your motor
    constexpr float    STEPPER_MAX_SPEED  = 2000.0f;
}
```

**4. Change pins (if needed) in `PinConfig.h`:**
- Safe GPIO pins: 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21
- **Avoid:** 0, 3, 19, 20, 43, 44, 45, 46, 48

---

## 📚 Documentation

- **[Quick Wiring Guide](Quick_Wiring_Guide_Custom_Pins.md)** - Visual pinout and connections
- **[Command Protocol](docs/command-protocol.md)** - Full command reference
- **[Architecture](docs/architecture.md)** - System design and code structure
- **[ESP32-S3 Hardware](docs/esp32-s3-hardware.md)** - MCU specs and pin reference
- **[TMC2209 Features](docs/tmc2209-guide.md)** - Stepper driver capabilities
- **[DC Motor Guide](docs/dc-motor-guide.md)** - RZ7899-MS H-bridge control
- **[FastAccelStepper](docs/fastaccelstepper.md)** - Pulse generation library reference
- **[Troubleshooting](docs/troubleshooting.md)** - Common issues and solutions

---

## 🚨 Troubleshooting

### "TMC2209 Connection Failed"
1. Check all GND connections are common
2. Verify VIO has 3.3V
3. Try `reconfigure` to re-send UART settings
4. Check resistor value (should be 1kΩ for single-wire half-duplex)
5. Try `stepdir on` to use Step/Dir fallback without UART

### Motor Not Moving
1. Type `enable` to ensure driver is powered
2. Check motor current with `set current 800`
3. Test with `move 200` or `move -200`
4. Type `diag` to check for error flags

### Motor Stuttering/Skipping
1. Increase motor current: `set current 1000`
2. Slow down speed: `set speed 500`
3. Switch to SpreadCycle: `spreadcycle`
4. Check power supply can handle current

### USB Serial Not Working
- ESP32-S3 USB takes 1-3 seconds to enumerate
- Press reset button after upload
- Check `platformio.ini` has `-DARDUINO_USB_CDC_ON_BOOT=1`

### Overheating
1. Add heatsink to TMC2209
2. Reduce motor current: `set current 400`
3. Add active cooling (fan)
4. Check for short circuits with `diag`

### DC Motor Not Running
1. Check H-bridge power connections (VCC on pin 4, NOT 3.3V)
2. Verify GPIO 8/9 connections to IN1/IN2
3. Verify correct jumper: GPIO 11 connected to GPIO 10 or 13
4. Test with `run forward` — motor should spin
5. Try setting speed: `abs 50`

### Hardware Detection Issues
1. Check jumper wires between detection pins (GPIO 10-13)
2. Type `reboot` to re-run hardware detection
3. Check startup banner for detected driver type
4. Ensure jumper connections are solid (no intermittent contact)

---

## ⚙️ PlatformIO Setup

**platformio.ini:**
```ini
[env:esp32-s3-mini]
platform = espressif32
board = lolin_s3_mini
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps = 
    teemuatlut/TMCStepper@^0.7.3
    gin66/FastAccelStepper@^0.33.9

build_flags = 
    -DARDUINO_USB_MODE=1
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
- GPIO 48 - Onboard WS2812 NeoPixel LED (used by status LED)

### Current Pin Assignments

| GPIO | Function | Notes |
|------|----------|-------|
| 1 | TMC TX | UART transmit (through 1kΩ to RX) |
| 2 | TMC RX | UART receive → TMC2209 PDN_UART |
| 4 | TMC EN | Enable pin (active LOW) |
| 5 | TMC STEP | Step pulse output |
| 6 | TMC DIR | Direction output |
| 8 | DC IN1 | H-bridge input 1 (PWM) |
| 9 | DC IN2 | H-bridge input 2 (PWM) |
| 10 | DETECT VCC | Output HIGH for detection jumpers |
| 11 | DETECT BIT 0 | Input pull-down (DC motor flag) |
| 12 | DETECT BIT 1 | Input pull-down (TMC2208 flag) |
| 13 | DETECT VCC | Output HIGH for detection jumpers |
| 48 | LED | WS2812 NeoPixel status LED |

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
