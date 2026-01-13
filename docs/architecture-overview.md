# Universal Motor Module - Architecture Overview

**Date**: January 13, 2026  
**Purpose**: Complete system architecture before migrating to FastAccelStepper

---

## 🏗️ System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        MAIN APPLICATION                          │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  main.cpp - Entry Point & Serial Communication Handler     │ │
│  │  • setup() - Initialize hardware and subsystems            │ │
│  │  • loop() - Process serial commands and update systems     │ │
│  │  • Input buffer management and command echo                │ │
│  └────────────────────────────────────────────────────────────┘ │
└───────────────────────────┬──────────────────────────────────────┘
                            │
        ┌───────────────────┴──────────────────┬──────────────────┐
        │                                       │                   │
        ▼                                       ▼                   ▼
┌─────────────────┐                ┌──────────────────┐  ┌─────────────────┐
│  USER INTERFACE │                │  CORE CONTROL    │  │  VISUAL FEEDBACK│
│                 │                │                  │  │                 │
│  Serial @ 115200│                │  MotorController │  │  StatusLED      │
│  • USB CDC      │──commands──────>  • Command parser│  │  • WS2812 LED   │
│  • Echo back    │                │  • Motion state  │  │  • Color codes  │
│  • Help menu    │<───status──────   • Error handling│  │  • Animations   │
│                 │                │  • Profile mgmt  │  │                 │
└─────────────────┘                └────────┬─────────┘  └─────────────────┘
                                            │
                        ┌───────────────────┴──────────────────┐
                        │                                       │
                        ▼                                       ▼
            ┌──────────────────────┐              ┌─────────────────────────┐
            │  MOTION MATH ENGINE  │              │   HARDWARE DETECTION    │
            │                      │              │                         │
            │  AccelerationProfile │              │  DriverFactory          │
            │  • Trapezoidal       │              │  • Read GPIO 11, 12     │
            │  • S-Curve           │              │  • Auto-detect driver   │
            │  • Linear            │              │  • Create instance      │
            │                      │              │                         │
            │  MotionMath          │              └─────────┬───────────────┘
            │  • Velocity calcs    │                        │
            │  • Position tracking │                        │
            │  • Timing            │                        │
            └──────────────────────┘                        │
                                                            ▼
                                            ┌───────────────────────────────┐
                                            │  DRIVER ABSTRACTION LAYER     │
                                            │                               │
                                            │  IMotorDriver (Interface)     │
                                            │  • Pure virtual functions     │
                                            │  • Common API for all drivers │
                                            └───────────┬───────────────────┘
                                                        │
                        ┌───────────────────────────────┼─────────────────────────────┐
                        │                               │                             │
                        ▼                               ▼                             ▼
            ┌───────────────────────┐      ┌──────────────────────┐      ┌────────────────────┐
            │  TMC2209 DRIVER       │      │  TMC2208 DRIVER      │      │  DC MOTOR DRIVER   │
            │                       │      │                      │      │                    │
            │  TMC2209Driver.cpp/.h │      │  TMC2208Driver.cpp/.h│      │  DCMotorDriver.cpp │
            │  • UART comms (RX/TX) │      │  • Step/Dir fallback │      │  • H-bridge RZ7899 │
            │  • TMCStepper lib     │      │  • Limited features  │      │  • PWM speed ctrl  │
            │  • Current control    │      │  • Vref current      │      │  • Direction pins  │
            │  • Microstepping      │      │  • MS pins           │      │                    │
            │  • StallGuard         │      │                      │      │                    │
            │  • StealthChop        │      │                      │      │                    │
            └───────────┬───────────┘      └──────────┬───────────┘      └────────┬───────────┘
                        │                             │                           │
                        └────────────┬────────────────┘                           │
                                     │                                            │
                                     ▼                                            ▼
                        ┌────────────────────────┐                  ┌──────────────────────┐
                        │  PULSE GENERATION      │                  │  PWM GENERATION      │
                        │                        │                  │                      │
                        │  MCPWMStepper.cpp/.h   │                  │  Arduino PWM API     │
                        │  • ESP32 MCPWM periph  │                  │  • ledcWrite()       │
                        │  • STEP/DIR pins       │                  │  • Motor A/B pins    │
                        │  • Frequency control   │                  │                      │
                        │  ⚠️ Legacy Arduino API │                  └──────────────────────┘
                        │  • 50kHz cap           │
                        │  • Pulse gaps          │
                        │  • No sync updates     │
                        └────────────────────────┘
```

---

## 📦 Code Groups by Function

### GROUP 1: 🎮 **USER INTERFACE** (Serial Communication)
**Purpose**: Accept commands from user via USB serial

**Files:**
- `src/main.cpp` (partial - loop() serial handling)

**Responsibilities:**
- Read serial input at 115200 baud
- Buffer incoming characters until newline
- Echo commands back to user
- Route commands to MotorController

**Technologies:**
- Arduino `Serial` class (USB CDC)
- String buffering with timeout detection

**Current Status:** ✅ Working well

---

### GROUP 2: 🎯 **CORE CONTROL** (Motor Controller)
**Purpose**: High-level motor control and command processing

**Files:**
- `src/core/MotorController.cpp`
- `src/core/MotorController.h`

**Responsibilities:**
- Parse command strings (e.g., "move 1000", "set speed 500")
- Dispatch to appropriate driver methods
- Track motor state (moving, idle, error)
- Manage acceleration profile selection
- Error handling and recovery
- Status reporting

**Technologies:**
- String parsing (strtol, strtof)
- State machine (idle, moving, error)
- Polymorphic driver calls via IMotorDriver*

**Current Status:** ✅ Working well, well-structured

---

### GROUP 3: 🧮 **MOTION MATH** (Acceleration Profiles & Calculations)
**Purpose**: Calculate velocity curves and motion timing

**Files:**
- `src/core/AccelerationProfile.h` (header-only)
- `src/core/MotionMath.h` (header-only)

**Responsibilities:**
- Trapezoidal acceleration/deceleration
- S-Curve (jerk-limited) motion
- Linear (constant speed) motion
- Velocity-to-time conversions
- Position prediction

**Technologies:**
- Template-based math functions
- Inline calculations for speed
- No external dependencies

**Current Issues:** 
- ⚠️ Issue #2: Rounding errors at low speeds (<1000 Hz)
- ⚠️ Issue #3: Triangular profile overshoot

**Current Status:** ⚠️ Has bugs, needs fixes

---

### GROUP 4: 🔧 **HARDWARE DETECTION** (Auto-Configuration)
**Purpose**: Detect which motor driver is connected

**Files:**
- `src/drivers/DriverFactory.cpp`
- `src/drivers/DriverFactory.h`

**Responsibilities:**
- Read GPIO 11, 12 (detection pins)
- Create appropriate driver instance:
  - No jumper → TMC2209 (default)
  - GPIO 11 HIGH → DC Motor
  - GPIO 12 HIGH → TMC2208
- Print detection info to serial

**Technologies:**
- GPIO reads with pull-downs
- Factory pattern (creational design pattern)
- Dynamic memory allocation

**Current Status:** ✅ Working well

---

### GROUP 5: 🎨 **VISUAL FEEDBACK** (Status LED)
**Purpose**: Provide visual system state indication

**Files:**
- `src/core/StatusLED.cpp`
- `src/core/StatusLED.h`

**Responsibilities:**
- WS2812 RGB LED control (GPIO 48)
- State indication:
  - Blue = Initializing
  - Green = Ready
  - Cyan/Magenta/Yellow = Driver type
  - Pulsing = Moving
  - Red = Error
  - Rainbow = Reboot animation
- Brightness control
- Animation timing

**Technologies:**
- ESP32 RMT peripheral (for WS2812 timing)
- Color interpolation
- Non-blocking animations

**Current Issues:**
- ⚠️ Issue #8: LED status overwritten by status queries

**Current Status:** ⚠️ Minor bug

---

### GROUP 6: 🔌 **DRIVER ABSTRACTION** (Common Interface)
**Purpose**: Unified API for all motor types

**Files:**
- `src/drivers/IMotorDriver.h` (pure virtual interface)

**Responsibilities:**
- Define common methods all drivers must implement:
  - `bool init()` - Initialize hardware
  - `void enable()/disable()` - Control motor power
  - `void setSpeed(float)` - Set target speed
  - `void setAcceleration(float)` - Set accel rate
  - `void moveBy(int32_t)` - Relative move
  - `void moveTo(int32_t)` - Absolute move
  - `void stop()` - Emergency stop
  - `void update()` - Called every loop
  - `bool isBusy()` - Check if moving
  - `MotorType getType()` - Identify driver
- Enums for motor types, profiles, errors

**Technologies:**
- Abstract base class (polymorphism)
- Pure virtual functions

**Current Status:** ✅ Well-designed interface

---

### GROUP 7: 🚂 **TMC2209 STEPPER DRIVER** (UART-Controlled Stepper)
**Purpose**: Advanced stepper control with TMC2209 features

**Files:**
- `src/drivers/TMC2209Driver.cpp`
- `src/drivers/TMC2209Driver.h`

**Responsibilities:**
- UART communication (RX=GPIO 17, TX=GPIO 18)
- Current control (0-2000mA via UART register)
- Microstepping configuration (1-256)
- StealthChop (silent operation)
- StallGuard (stall detection)
- Homing via StallGuard
- Register access and diagnostics
- Error monitoring (shorts, overtemp)

**Technologies:**
- TMCStepper library (Teemuatlut)
- Hardware UART (Serial2)
- MCPWMStepper for pulses

**Dependencies:**
- MCPWMStepper (for STEP/DIR pulses)

**Current Issues:**
- ⚠️ Issue #4: Weak holding torque (IHOLD register)

**Current Status:** ⚠️ Mostly working, holding current issue

---

### GROUP 8: 🚂 **TMC2208 STEPPER DRIVER** (Limited Stepper)
**Purpose**: Basic stepper control when TMC2209 not available

**Files:**
- `src/drivers/TMC2208Driver.cpp`
- `src/drivers/TMC2208Driver.h`

**Responsibilities:**
- Fallback to step/dir mode if UART fails
- Limited features vs TMC2209
- Current set by Vref resistor
- Microstepping via MS1/MS2 pins

**Technologies:**
- TMCStepper library
- MCPWMStepper for pulses

**Dependencies:**
- MCPWMStepper

**Current Status:** ✅ Working (rarely used)

---

### GROUP 9: 🚂 **DC MOTOR DRIVER** (H-Bridge PWM)
**Purpose**: Control DC motors via RZ7899 H-bridge

**Files:**
- `src/drivers/DCMotorDriver.cpp`
- `src/drivers/DCMotorDriver.h`

**Responsibilities:**
- Speed control via PWM (0-255)
- Direction control (IN1/IN2 pins)
- Forward/reverse/brake/coast
- Enable/disable

**Technologies:**
- Arduino `ledcWrite()` PWM functions
- GPIO for direction pins

**Current Status:** ✅ Working (separate product line)

---

### GROUP 10: ⚡ **PULSE GENERATION** (MCPWM Hardware PWM)
**Purpose**: Generate high-frequency STEP pulses for steppers

**Files:**
- `src/drivers/MCPWMStepper.cpp`
- `src/drivers/MCPWMStepper.h`

**Responsibilities:**
- Hardware PWM generation using ESP32 MCPWM peripheral
- STEP pin pulse output
- DIR pin direction control
- Frequency control (10 Hz - 50kHz)
- 50% duty cycle

**Technologies:**
- ESP32 MCPWM peripheral (legacy Arduino API)
- Functions: `mcpwm_init()`, `mcpwm_set_frequency()`, `mcpwm_start/stop()`

**Current Issues (CRITICAL):**
- ⚠️ Issue #11: **Pulse gaps during acceleration** (immediate timer updates)
- ⚠️ Issue #9: **50kHz frequency cap** (prescaler configuration)
- ⚠️ Issue #10: **Position tracking runaway** (time-based, not pulse-based)

**Root Cause:** Legacy Arduino MCPWM API doesn't expose synchronization flags

**Attempted Fix:** Tried migrating to new ESP-IDF API → **BLOCKED** (headers not available)

**Current Status:** ❌ **BROKEN** - Root cause of step skipping

---

### GROUP 11: 📐 **CONFIGURATION** (Pin Definitions & Constants)
**Purpose**: Centralized hardware configuration

**Files:**
- `src/config/PinConfig.h`

**Responsibilities:**
- GPIO pin assignments
- Default motor parameters (speed, accel, current)
- Serial baud rates
- Timeouts and limits

**Technologies:**
- C++ constexpr constants
- Namespace organization

**Current Status:** ✅ Well-organized

---

### GROUP 12: 🧪 **TESTING** (Unit Tests)
**Purpose**: Automated testing of core functions

**Files:**
- `test/test_acceleration_profile/`
- `test/test_command_parsing/`
- `test/test_motion_math/`
- `test/test_motor_status/`
- `test/mocks/` (Arduino.h mock)

**Responsibilities:**
- Unit tests for math functions
- Command parser validation
- Motion profile verification
- Mock Arduino environment for PC testing

**Technologies:**
- PlatformIO native testing
- Unity test framework

**Current Status:** ✅ Tests exist but not run recently

---

## 📊 Issue Summary by Group

| Group | Files | Status | Critical Issues |
|-------|-------|--------|-----------------|
| 1. User Interface | main.cpp | ✅ Good | Issue #7: Serial reconnection |
| 2. Core Control | MotorController | ✅ Good | None |
| 3. Motion Math | AccelerationProfile, MotionMath | ⚠️ Bugs | #2 (low-speed), #3 (overshoot) |
| 4. Hardware Detection | DriverFactory | ✅ Good | None |
| 5. Visual Feedback | StatusLED | ⚠️ Minor | #8 (LED overwrite) |
| 6. Driver Abstraction | IMotorDriver | ✅ Good | None |
| 7. TMC2209 Driver | TMC2209Driver | ⚠️ Issue | #4 (holding torque) |
| 8. TMC2208 Driver | TMC2208Driver | ✅ Good | None |
| 9. DC Motor Driver | DCMotorDriver | ✅ Good | None |
| 10. **PULSE GENERATION** | **MCPWMStepper** | ❌ **CRITICAL** | **#11, #9, #10** |
| 11. Configuration | PinConfig.h | ✅ Good | None |
| 12. Testing | test/ | ⚠️ Stale | Tests not run |

---

## 🎯 Key Insight: The Problem is Isolated

**ONE group is causing most issues:** GROUP 10 (Pulse Generation)

- MCPWMStepper.cpp/MCPWMStepper.h
- Only ~200 lines of code
- Uses legacy Arduino MCPWM API
- Cannot be fixed without ESP-IDF framework migration (too risky)

**Solution:** Replace MCPWMStepper with FastAccelStepper library!

---

## 🚀 FastAccelStepper Migration Strategy

### What FastAccelStepper Provides

From their GitHub: https://github.com/gin66/FastAccelStepper

1. ✅ **High-speed pulse generation** (up to 500kHz, way more than your 200kHz need)
2. ✅ **Hardware-driven** (uses ESP32 MCPWM/LEDC properly)
3. ✅ **Acceleration profiles** (linear, cubic - better than our buggy ones)
4. ✅ **Synchronous updates** (no pulse gaps)
5. ✅ **Position tracking** (hardware-based)
6. ✅ **ESP32-S3 support** (tested platform)

### What We Keep

- ✅ GROUP 1-2: User interface and MotorController (command handling)
- ✅ GROUP 4: Hardware detection
- ✅ GROUP 5: Status LED
- ✅ GROUP 6: IMotorDriver interface
- ✅ GROUP 7-9: TMC2209/TMC2208/DC drivers
- ✅ GROUP 11: Pin configuration

### What We Replace

- ❌ GROUP 3: AccelerationProfile.h → Use FastAccelStepper's profiles
- ❌ GROUP 3: MotionMath.h → Use FastAccelStepper's calculations
- ❌ GROUP 10: MCPWMStepper → Use FastAccelStepper directly

### Implementation Plan

1. **Add library**: `platformio.ini` add `gin66/FastAccelStepper`
2. **Modify TMC2209Driver**: Replace MCPWMStepper with FastAccelStepper engine
3. **Simplify MotorController**: Remove our acceleration math, use library's
4. **Test**: Oscilloscope verification - no gaps, high frequency
5. **Cleanup**: Delete MCPWMStepper, AccelerationProfile, MotionMath

**Estimated Time:** 2-4 hours (much less than ESP-IDF migration!)

---

## 📁 Final Architecture After Migration

```
USER INTERFACE (main.cpp)
    ↓
MOTOR CONTROLLER (command parsing)
    ↓
DRIVER ABSTRACTION (IMotorDriver)
    ↓
┌─────────────────┬─────────────────┬──────────────────┐
│                 │                 │                  │
TMC2209Driver  TMC2208Driver  DCMotorDriver
    │                 │                  │
    ↓                 ↓                  ↓
FastAccelStepper  FastAccelStepper   Arduino PWM
(MCPWM hardware)  (MCPWM hardware)   (ledcWrite)
```

**Result:** Clean, maintainable, bug-free pulse generation! 🎉

---

**End of Document**
