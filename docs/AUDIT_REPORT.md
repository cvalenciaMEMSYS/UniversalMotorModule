# Universal Motor Module — Comprehensive Audit Report

**Date:** April 7, 2026
**Firmware Version:** v2.0 (FastAccelStepper)
**Target Platform:** ESP32-S3 Super Mini (LOLIN S3 Mini)
**Purpose:** Full codebase and documentation audit to prepare for STM32 platform translation

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Repository Overview](#2-repository-overview)
3. [Architecture Analysis](#3-architecture-analysis)
4. [Source Code Audit](#4-source-code-audit)
   - 4.1 [Entry Point (main.cpp)](#41-entry-point-maincpp)
   - 4.2 [Pin Configuration (PinConfig.h)](#42-pin-configuration-pinconfigh)
   - 4.3 [Motor Controller (MotorController)](#43-motor-controller)
   - 4.4 [Status LED (StatusLED)](#44-status-led)
   - 4.5 [Driver Interface (IMotorDriver.h)](#45-driver-interface)
   - 4.6 [Driver Factory (DriverFactory)](#46-driver-factory)
   - 4.7 [FastAccelStepper Wrapper](#47-fastaccelstepper-wrapper)
   - 4.8 [TMC2209 Driver](#48-tmc2209-driver)
   - 4.9 [TMC2208 Driver](#49-tmc2208-driver)
   - 4.10 [STSPIN220 Driver](#410-stspin220-driver)
   - 4.11 [DC Motor Driver](#411-dc-motor-driver)
5. [Test Infrastructure Audit](#5-test-infrastructure-audit)
6. [Documentation Audit](#6-documentation-audit)
7. [Scripts & Tooling Audit](#7-scripts--tooling-audit)
8. [ESP32-Specific Dependencies Catalog](#8-esp32-specific-dependencies-catalog)
9. [Cross-Cutting Issues](#9-cross-cutting-issues)
10. [STM32 Translation Considerations](#10-stm32-translation-considerations)
11. [Recommendations & Action Items](#11-recommendations--action-items)

---

## 1. Executive Summary

The Universal Motor Module is a well-structured embedded motor-control firmware supporting four driver types (TMC2209, TMC2208, STSPIN220, DC Motor) with runtime auto-detection. The codebase totals approximately **6,950 lines of C++** across 17 source/header files, with an additional **~5,500 lines of Python** in analysis/testing scripts.

### Overall Quality Ratings

| Dimension | Rating | Notes |
|-----------|--------|-------|
| **Architecture** | 8/10 | Clean separation of concerns; factory pattern; abstract interface |
| **Code Quality** | 7/10 | Good validation; some magic numbers; minor type-safety gaps |
| **Test Coverage** | 4/10 | Only command parsing and status struct tested; no integration tests |
| **Documentation** | 6.5/10 | Comprehensive but contains critical inaccuracies |
| **STM32 Portability** | 6/10 | ESP32 APIs well-isolated; two major library dependencies |

### Critical Findings

| # | Severity | Finding |
|---|----------|---------|
| 1 | 🔴 CRITICAL | `docs/architecture.md` states TMC2209 UART pins as GPIO 17/18 — actual pins are GPIO 1/2 |
| 2 | 🔴 CRITICAL | `docs/motor-drivers.md` references `set jerk` command that does not exist — actual command is `set cubesteps` |
| 3 | 🔴 CRITICAL | Input buffer in `main.cpp` has no maximum length, risking memory exhaustion |
| 4 | 🟡 HIGH | `static_cast<TMC2209Driver*>` in MotorController.cpp without type verification |
| 5 | 🟡 HIGH | STSPIN220 driver fully implemented in code but not documented anywhere |
| 6 | 🟡 HIGH | Error polling only runs when motor is NOT moving (500ms interval) — errors during motion are missed |
| 7 | 🟡 MEDIUM | DC motor `move(steps)` actually means milliseconds — API semantically overloaded |
| 8 | 🟡 MEDIUM | `docs/dc-motor-guide.md` shows 8-bit PWM resolution; code uses 10-bit (0–1023) |

---

## 2. Repository Overview

```
UniversalMotorModule/
├── src/                          # Firmware source (C++)
│   ├── main.cpp                  # Entry point, serial loop
│   ├── config/
│   │   └── PinConfig.h           # GPIO pin definitions, defaults
│   ├── core/
│   │   ├── MotorController.cpp/h # Command processing, motion control
│   │   └── StatusLED.cpp/h       # WS2812 NeoPixel status LED
│   └── drivers/
│       ├── IMotorDriver.h        # Abstract driver interface
│       ├── DriverFactory.cpp/h   # Runtime hardware detection + factory
│       ├── FastAccelStepperWrapper.cpp/h  # Step generation wrapper
│       ├── TMC2209Driver.cpp/h   # TMC2209 UART + Step/Dir
│       ├── TMC2208Driver.cpp/h   # TMC2208 UART + Step/Dir
│       ├── STSPIN220Driver.cpp/h # STSPIN220 Step/Dir only
│       └── DCMotorDriver.cpp/h   # H-bridge PWM DC motor
├── test/                         # Native unit tests
│   ├── mocks/Arduino.cpp/h       # Arduino API mock for PC
│   ├── test_command_parsing/     # Command string parsing tests
│   └── test_motor_status/        # MotorStatus struct tests
├── docs/                         # Documentation (17 files)
├── Scripts/                      # Python analysis & measurement tools
│   ├── FD/                       # Force-Deflection measurement
│   ├── NoJS/                     # FD without Joulescope
│   └── OldAttempt/               # Legacy measurement system
├── Motor_comparison/             # Motor test result tables (12 files)
├── platformio.ini                # Build configuration
└── README.md                     # Project overview
```

### Build Environments (platformio.ini)

| Environment | Platform | Board | Purpose |
|-------------|----------|-------|---------|
| `esp32-s3-mini` | espressif32 | lolin_s3_mini | Hardware target (4MB flash) |
| `native` | native | — | PC unit testing (C++17) |

### External Library Dependencies

| Library | Version | Purpose | STM32 Equivalent Needed |
|---------|---------|---------|------------------------|
| FastAccelStepper | 0.33.9 | Hardware step pulse generation (200kHz+) | Yes — timer-based stepper library |
| TMCStepper | 0.7.3 | TMC2209/2208 UART register access | Yes — already supports STM32 |
| Arduino Core (ESP32) | 2.x | GPIO, Serial, timing, LEDC PWM | STM32 HAL or Arduino-STM32 |
| Unity | (test) | Unit testing framework | Portable — no change needed |

---

## 3. Architecture Analysis

### Layer Diagram

```
┌─────────────────────────────────────────────┐
│                  main.cpp                    │  Serial I/O, LED updates, reboot
├─────────────────────────────────────────────┤
│              MotorController                 │  Command parsing, validation, dispatch
├─────────────────────────────────────────────┤
│               IMotorDriver                   │  Abstract interface (virtual methods)
├──────────┬──────────┬───────────┬───────────┤
│ TMC2209  │ TMC2208  │ STSPIN220 │ DCMotor   │  Concrete driver implementations
│ Driver   │ Driver   │ Driver    │ Driver    │
├──────────┴──────────┴───────────┤           │
│     FastAccelStepperWrapper     │           │  Step pulse generation (stepper only)
├─────────────────────────────────┴───────────┤
│           DriverFactory                      │  GPIO-based hardware detection
├─────────────────────────────────────────────┤
│             StatusLED                        │  WS2812 NeoPixel feedback
├─────────────────────────────────────────────┤
│             PinConfig.h                      │  GPIO assignments, defaults
└─────────────────────────────────────────────┘
```

### Design Patterns Used
- **Factory Pattern:** `DriverFactory` creates the correct driver based on GPIO detection
- **Strategy Pattern:** `IMotorDriver` interface allows polymorphic driver behavior
- **Singleton:** FastAccelStepperEngine is a static singleton inside the wrapper
- **State Machine:** StatusLED maintains animation state with transitions

### Data Flow
1. **Serial input** → character accumulation in `main.cpp` → 100ms timeout → `processCommand()`
2. **processCommand()** → validates/parses → calls motion/config methods on `IMotorDriver*`
3. **IMotorDriver** → delegates step generation to `FastAccelStepperWrapper` (steppers) or LEDC PWM (DC)
4. **Status loop** → `update()` checks motion completion, polls errors (500ms), updates LED

---

## 4. Source Code Audit

### 4.1 Entry Point (main.cpp)

**Lines:** 187 | **Role:** Serial loop, startup, LED management

#### Key Behavior
- Waits up to 3 seconds for USB serial connection at startup
- Processes commands character-by-character with 100ms idle timeout
- Handles `reboot`/`restart` commands with LED animation before `ESP.restart()`
- Updates LED status based on priority: ERROR > MOVING > IDLE > READY

#### Constants
| Constant | Value | Notes |
|----------|-------|-------|
| `USB_SERIAL_BAUD` | 115200 | Standard baud rate |
| `SERIAL_TIMEOUT_MS` | 3000 | USB serial wait timeout |
| `IDLE_TIMEOUT_MS` | 5000 | Transition to idle LED after 5s |

#### Issues Found
| Severity | Issue | Location |
|----------|-------|----------|
| 🔴 CRITICAL | `inputBuffer` (Arduino `String`) has no max length — unbounded memory growth | Line 46 |
| 🟡 MEDIUM | 100ms command timeout could split commands over slow serial links | Line 133 |
| 🔵 LOW | No function-level documentation for `setup()` and `loop()` | Lines 59, 112 |

#### ESP32-Specific APIs Used
- `ESP.restart()` — hardware reset
- `Serial.begin(baud)` — USB CDC serial
- `millis()`, `delay()` — timing

---

### 4.2 Pin Configuration (PinConfig.h)

**Lines:** 102 | **Role:** Centralized GPIO assignments and default parameters

#### Pin Map

| Function | GPIO | Notes |
|----------|------|-------|
| DETECT_VCC_1 | 10 | Output HIGH — power source for detection |
| DETECT_VCC_2 | 13 | Output HIGH — power source for detection |
| DETECT_BIT_0 | 11 | Input pull-down — DC motor flag |
| DETECT_BIT_1 | 12 | Input pull-down — TMC2208 flag |
| TMC_TX_PIN | 1 | UART TX (through 1kΩ resistor) |
| TMC_RX_PIN | 2 | UART RX |
| TMC_EN_PIN | 4 | Enable (active LOW) |
| TMC_STEP_PIN | 5 | Step pulse |
| TMC_DIR_PIN | 6 | Direction |
| DC_IN1_PIN | 8 | H-bridge input 1 (PWM) |
| DC_IN2_PIN | 9 | H-bridge input 2 (PWM) |
| LED_PIN | 48 | WS2812 NeoPixel (onboard) |

#### Hardware Detection Truth Table

| GPIO 11 | GPIO 12 | Detected Driver |
|---------|---------|-----------------|
| LOW | LOW | TMC2209 (default — nothing connected) |
| HIGH | LOW | DC Motor (RZ7899) |
| LOW | HIGH | TMC2208 |
| HIGH | HIGH | STSPIN220 |

#### Default Motor Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| STEPPER_CURRENT_MA | 100 | Very conservative — most motors need 300–1000mA |
| STEPPER_HOLD_CURRENT | 0 | No hold torque by default |
| STEPPER_MICROSTEPS | 16 | 16× microstepping |
| STEPPER_MAX_SPEED | 1000.0 | Steps per second |
| STEPPER_ACCELERATION | 500.0 | Steps/s² |
| STEPPER_AUTO_DISABLE | true | Driver disables after moves |
| DC_MAX_SPEED | 1.0 | Full speed (0.0–1.0 range) |
| DC_ACCELERATION | 2.0 | Speed units per second |

#### Issues Found
| Severity | Issue |
|----------|-------|
| 🔵 LOW | Sense resistor value (0.11Ω) hardcoded for BigTreeTech TMC2209 v1.3 — other boards may differ |
| 🔵 LOW | No compile-time pin conflict detection |

---

### 4.3 Motor Controller

**Lines:** 205 (header) + 815 (implementation) | **Role:** Command processing, motion coordination, error polling

#### Motor Limits

| Limit | Value | Notes |
|-------|-------|-------|
| MAX_MOVE_STEPS | ±1,000,000 | Per command |
| MAX_POSITION | 100,000,000 | Absolute |
| MIN_SPEED | 1.0 steps/s | |
| MAX_SPEED | 200,000 steps/s | 200kHz |
| MIN_ACCELERATION | 0.0 steps/s² | Constant velocity |
| MAX_ACCELERATION | 1,000,000 steps/s² | |
| MIN_CURRENT_MA | 100 mA | |
| MAX_CURRENT_MA | 3,000 mA | 3A |
| MICROSTEPS | 1–256 | Power of 2 only |

#### Supported Commands

**Motion Commands:**
`move <steps>`, `abs <position>`, `home`, `stop`, `brake`, `run forward`, `run backward`

**Configuration Commands:**
`set speed <val>`, `set current <val>`, `set microsteps <val>`, `set accel <val>`, `set cubesteps <val>`, `set hold <percent>`, `set autoenable <on|off>`

**Query Commands:**
`get pos`, `get target`, `get speed`, `get rampstate`

**Utility Commands:**
`enable`, `disable`, `?`, `status`, `help`, `test`, `diag`, `scan`, `reconfigure`, `ping`, `stepdir on|off`, `stealthchop`, `spreadcycle`, `pwmautoscale`

#### Error Handling
- Error flags: `NONE (0x00)`, `OVER_TEMP (0x01)`, `SHORT_CIRCUIT (0x02)`, `OPEN_LOAD (0x04)`, `COMM_FAILURE (0x08)`, `STALL_DETECTED (0x10)`
- Critical errors (trigger ERROR LED): OVER_TEMP, SHORT_CIRCUIT, COMM_FAILURE
- Non-critical (logged only): OPEN_LOAD
- Error polling: every 500ms, only when motor is NOT moving

#### Issues Found
| Severity | Issue | Location |
|----------|-------|----------|
| 🟡 HIGH | `static_cast<TMC2209Driver*>(_driver)` without type verification — crash risk if wrong driver | Lines 316, 345 |
| 🟡 HIGH | Error polling disabled during motion — transient errors missed | Lines 88–95 |
| 🟡 MEDIUM | Default `_acceleration = 1000.0f` in constructor differs from `DefaultMotorConfig::STEPPER_ACCELERATION = 500.0f` | Line 26 |
| 🔵 LOW | Manual character-by-character command parsing is error-prone | Lines 129–204 |
| 🔵 LOW | No command queue — synchronous processing could block serial input | processCommand() |

---

### 4.4 Status LED

**Lines:** 273 (header) + 455 (implementation) | **Role:** WS2812 NeoPixel status feedback via RMT peripheral

#### State Machine

| Status | Color | Animation | Priority |
|--------|-------|-----------|----------|
| INITIALIZING | Blue (0,0,50) | Solid | — |
| READY | Driver color | Solid | 4 |
| MOVING | Driver color | Fast blink (80ms) | 2 |
| COMMAND_RX | Orange | 100ms flash | — |
| DRIVER_OFF | Off | None | — |
| WARNING | Yellow | Slow blink (500ms) | 3 |
| ERROR | Red (255,0,0) | Solid | 1 (highest) |
| STALL | Red | Fast blink (100ms) | 1 |
| IDLE | Driver color | Pulse/breathe (50ms step) | 5 |
| REBOOTING | Rainbow | Sweep + fade (blocking) | — |

#### Driver Base Colors

| Driver | Color | RGB |
|--------|-------|-----|
| TMC2209 | Green | (0, 255, 0) |
| TMC2208 | Cyan | (0, 255, 200) |
| DC Motor | Blue | (0, 100, 255) |
| STSPIN220 | Magenta | (255, 0, 180) |
| Unknown | White | (255, 255, 255) |

#### Acceleration Profile Tints
- **Constant:** No modifier
- **Trapezoidal:** Yellow tint (+50 red)
- **S-Curve:** Purple tint (+50 blue)

#### Issues Found
| Severity | Issue | Location |
|----------|-------|----------|
| 🟡 MEDIUM | `playRebootAnimation()` and `playStartupSequence()` use blocking `delay()` — blocks main loop ~950ms | Lines 165–234 |
| 🔵 LOW | Only one pending status can be queued — rapid changes could be lost | Lines 252–253 |
| 🔵 LOW | Pulse update interval changed from 20ms to 50ms for RMT timing fix — root cause not resolved | Line 434 |
| 🔵 LOW | No gamma correction for LED brightness levels | — |

#### ESP32-Specific APIs
- `neopixelWrite(LED_PIN, r, g, b)` — RMT peripheral driver (built into ESP32 Arduino Core)
- `delay()` — blocking delays in animations

---

### 4.5 Driver Interface (IMotorDriver.h)

**Lines:** 408 | **Role:** Abstract interface for all motor driver types

#### Interface Method Groups

| Group | Methods | Notes |
|-------|---------|-------|
| Identity | `init()`, `getType()`, `getName()` | Required |
| Enable | `enable()`, `disable()`, `isEnabled()` | Required |
| Motion | `move()`, `moveTo()`, `stop()`, `emergencyStop()`, `isMoving()`, `update()` | Required |
| Motion (optional) | `runForward()`, `runBackward()`, `brake()` | Default empty impl |
| Config | `setMaxSpeed()`, `setCurrent()`, `setMicrosteps()`, `setAcceleration()` | Required |
| Config (optional) | `setLinearAcceleration()`, `setHoldCurrentPercent()`, `setAutoDisable()` | Default empty impl |
| Position | `getPosition()`, `setPosition()`, `home()` | Required |
| Query (optional) | `getTargetPosition()`, `getActualSpeed()`, `getRampState()`, `isRunningContinuously()` | Default return 0/false |
| Diagnostics | `getStatus()`, `isStalling()`, `printDiagnostics()`, `testConnection()` | Required |

#### MotorStatus Structure
```cpp
struct MotorStatus {
    bool enabled = false;
    bool moving = false;
    bool stalling = false;
    int32_t position = 0;
    int32_t targetPosition = 0;
    uint16_t currentMA = 0;
    uint16_t loadValue = 0;        // StallGuard result (0 if N/A)
    uint8_t errorFlags = 0;
    float currentSpeed = 0.0f;
};
```

#### Issues Found
| Severity | Issue |
|----------|-------|
| 🟡 MEDIUM | Many methods have empty default implementations — derived classes may silently not implement needed features |
| 🔵 LOW | `home(int8_t direction = -1)` direction convention not documented |
| 🔵 LOW | Speed semantics differ between stepper (steps/s) and DC motor (0.0–1.0) — interface doesn't enforce |

---

### 4.6 Driver Factory

**Lines:** 73 (header) + 136 (implementation) | **Role:** GPIO-based hardware detection and driver instantiation

#### Detection Flow
1. Set GPIO 10, 13 to OUTPUT HIGH (VCC sources)
2. Set GPIO 11, 12 to INPUT_PULLDOWN
3. Wait 10ms for settling
4. Read GPIO 11 (bit0) and GPIO 12 (bit1)
5. Decode truth table → `MotorType` enum
6. `new` appropriate driver class → return `IMotorDriver*`

#### Issues Found
| Severity | Issue |
|----------|-------|
| 🟡 MEDIUM | 10ms GPIO settling delay may be insufficient with capacitive filtering on jumper lines |
| 🟡 MEDIUM | Drivers allocated with `new` but no documented ownership — memory never freed |
| 🔵 LOW | Static `_detectionInitialized` flag prevents re-initialization if GPIOs corrupted |

---

### 4.7 FastAccelStepper Wrapper

**Lines:** 256 (header) + 248 (implementation) | **Role:** Hardware step pulse generation isolation layer

#### Key Behavior
- Wraps the `FastAccelStepper` library (gin66/FastAccelStepper v0.33.9)
- Uses singleton `FastAccelStepperEngine` shared across all steppers
- On ESP32-S3: uses MCPWM peripheral (not RMT) for step generation
- Supports: move, moveTo, runForward, runBackward, stop, brake
- S-curve acceleration via `setLinearAcceleration(steps)` → cubic motion profile

#### ESP32-Specific APIs
- `FastAccelStepperEngine::init()` — initializes MCPWM peripheral
- `engine->stepperConnectToPin(pin)` — allocates MCPWM channel
- Pin type: `gpio_num_t` (ESP32 GPIO number type)

#### Issues Found
| Severity | Issue |
|----------|-------|
| 🟡 MEDIUM | Default acceleration (500 steps/s²) hardcoded in `init()` — different from PinConfig default |
| 🔵 LOW | `gpio_num_t` → `uint8_t` cast may lose information on some platforms |
| 🔵 LOW | Destructor does not free `_stepper` object |

#### STM32 Translation Note
This is the **most critical component to replace**. FastAccelStepper does not support STM32. A timer-based step generator with acceleration profile support is needed (e.g., AccelStepper with timer interrupts, or a custom implementation using STM32 hardware timers).

---

### 4.8 TMC2209 Driver

**Lines:** 223 (header) + 714 (implementation) | **Role:** UART-controlled stepper with StallGuard

#### Key Features
- Full UART communication for register read/write
- Automatic fallback to Step/Dir mode if UART fails
- StealthChop (silent) and SpreadCycle (high-torque) modes
- StallGuard sensorless load detection
- PWM autoscale for automatic current optimization
- Address scanning (4 addresses via MS1/MS2 pins)

#### UART Configuration
| Parameter | Value |
|-----------|-------|
| Baud Rate | 115200 |
| Driver Address | 0b00 (MS1/MS2 floating) |
| Sense Resistor | 0.11Ω (BigTreeTech v1.3) |
| TX Pin | GPIO 1 (through 1kΩ resistor for single-wire) |
| RX Pin | GPIO 2 |

#### TMCStepper Library Usage
```cpp
TMC2209Stepper* _driver;
_driver = new TMC2209Stepper(serial, rSense, address);
_driver->begin();
_driver->rms_current(mA);
_driver->microsteps(ms);
_driver->en_spreadCycle(!enable);  // StealthChop = NOT SpreadCycle
_driver->pwm_autoscale(enable);
_driver->SGTHRS(threshold);       // StallGuard threshold
```

#### Issues Found
| Severity | Issue |
|----------|-------|
| 🟡 MEDIUM | UART init timing (100ms after begin, 50ms after TMCStepper creation) may be insufficient on noisy boards |
| 🟡 MEDIUM | StallGuard default threshold (50/255) is arbitrary — needs motor-specific tuning |
| 🔵 LOW | Hold current percentage (50%) hardcoded — different motors need different values |
| 🔵 LOW | Microstepping validation doesn't check for power-of-2 before sending to hardware |

---

### 4.9 TMC2208 Driver

**Lines:** 207 (header) + 644 (implementation) | **Role:** UART-controlled stepper without StallGuard

Nearly identical to TMC2209 with these differences:
- **No StallGuard** — `isStalling()` always returns `false`
- **No CoolStep** — no automatic current reduction
- Higher voltage range: up to 36V (vs 29V for TMC2209)
- Uses `TMC2208Stepper` class from TMCStepper library
- Same UART fallback mechanism to Step/Dir mode

---

### 4.10 STSPIN220 Driver

**Lines:** 136 (header) + implementation | **Role:** Simple Step/Dir stepper with no communication

#### Characteristics
- **No UART, no communication protocol** — simplest driver
- Microstepping configured via hardware jumpers (MODE1/MODE2)
- Current limit set via potentiometer (Vref)
- `setCurrent()`, `setMicrosteps()` are **no-ops** (cannot change at runtime)
- `enable()`/`disable()` are **no-ops** (driver auto-manages standby)
- Motion control entirely via FastAccelStepperWrapper

#### Issues Found
| Severity | Issue |
|----------|-------|
| 🟡 MEDIUM | No way to verify hardware jumper settings match software expectations |
| 🔵 LOW | All configuration methods silently do nothing — could confuse users |

---

### 4.11 DC Motor Driver

**Lines:** 168 (header) + 476 (implementation) | **Role:** H-bridge PWM control for DC motors

#### Key Behavior
- Dual-channel PWM via ESP32 LEDC peripheral
- Speed mapped from -1.0 to +1.0 → PWM duty cycle on IN1/IN2
- Direction control: Forward (IN1=PWM, IN2=LOW), Reverse (IN1=LOW, IN2=PWM)
- Timed moves: `move(steps)` where "steps" = milliseconds
- Software acceleration ramping from current to target speed

#### PWM Configuration
| Parameter | Value |
|-----------|-------|
| Frequency | 20,000 Hz (ultrasonic) |
| Resolution | 10-bit (0–1023 duty range) |
| Channel 1 | LEDC channel 0 (IN1) |
| Channel 2 | LEDC channel 1 (IN2) |

#### ESP32-Specific APIs
- `ledcSetup(channel, freq, resolution)` — LEDC PWM peripheral setup
- `ledcAttachPin(pin, channel)` — attach GPIO to PWM channel
- `ledcWrite(channel, duty)` — set PWM duty cycle

#### Issues Found
| Severity | Issue |
|----------|-------|
| 🟡 MEDIUM | `move(steps)` actually means milliseconds — semantically overloaded with stepper interface |
| 🟡 MEDIUM | Virtual position counter exists but never meaningfully updated |
| 🔵 LOW | `brake()` not implemented despite H-bridge supporting it (IN1=HIGH, IN2=HIGH) |
| 🔵 LOW | No coast vs. brake distinction in stop behavior |

---

## 5. Test Infrastructure Audit

### Build Configuration (platformio.ini native environment)
```ini
[env:native]
platform = native
build_flags = -std=c++17 -DUNITY_INCLUDE_FLOAT -DNATIVE_BUILD -I test/mocks -I src
build_src_filter = +<../test/mocks/Arduino.cpp>
test_build_src = false
lib_compat_mode = off
```

### Test Suites

#### test_command_parsing (30+ test cases)
- Tests `parseMoveCommand()`, `parseAbsCommand()`, `parseSetCommand()`, `parseStepDirCommand()`, `isSimpleCommand()`
- Covers: positive/negative values, zero, large values, spaces, case sensitivity, missing values, wrong commands
- **Critical Note:** Parsing functions are **defined inline in the test file**, not linked from actual source code

#### test_motor_status (10+ test cases)
- Tests `MotorStatus` struct and `MotorError` flag system
- Covers: individual flags, combined flags, default values, `hasError()` method
- **Critical Note:** Struct and error flags are **defined inline in the test file**

### Arduino Mock (test/mocks/)
| API Category | Mock Quality | Notes |
|--------------|-------------|-------|
| GPIO | Stub only | `digitalRead()` always returns LOW — can't simulate detection |
| Timing | Functional | `mockAdvanceTime()` and `mockSetTime()` for time control |
| Serial | Redirect to stdout | `Serial.print()` → `std::cout` |
| Serial1 | Stub only | `available()=0`, `read()=-1` — can't test UART |
| String | Functional | Wraps `std::string` with Arduino API |
| LEDC (PWM) | Stub only | No duty cycle tracking |

### Test Coverage Assessment

| Component | Unit Tested | Integration Tested | Notes |
|-----------|------------|-------------------|-------|
| Command parsing | ✅ Yes | ❌ No | Tests use local copies, not linked code |
| MotorStatus/Errors | ✅ Yes | ❌ No | Tests use local copies, not linked code |
| MotorController | ❌ No | ❌ No | No tests for command processing |
| DriverFactory | ❌ No | ❌ No | No tests for hardware detection |
| TMC2209/2208 | ❌ No | ❌ No | Requires hardware |
| DCMotorDriver | ❌ No | ❌ No | Requires hardware |
| STSPIN220 | ❌ No | ❌ No | Requires hardware |
| StatusLED | ❌ No | ❌ No | Requires hardware |
| FastAccelWrapper | ❌ No | ❌ No | Requires hardware |

**Overall Test Coverage: ~10%** (only command string parsing and struct initialization tested)

---

## 6. Documentation Audit

### Documentation Inventory

| File | Lines | Quality | Critical Issues |
|------|-------|---------|----------------|
| README.md | ~450 | Good | Missing STSPIN220 mention |
| Quick_Wiring_Guide_Custom_Pins.md | 314 | Excellent | — |
| docs/architecture.md | 387 | Good | 🔴 **Wrong GPIO pins for TMC2209 UART** |
| docs/command-protocol.md | 380 | Excellent | Missing `set jerk` clarification |
| docs/dc-motor-guide.md | 369 | Very Good | PWM resolution mismatch (8-bit vs 10-bit) |
| docs/esp32-s3-hardware.md | 236 | Excellent | — |
| docs/fastaccelstepper.md | 167 | Good | — |
| docs/hardware-detection.md | 73 | Good | STSPIN220 listed as "Reserved/Future" |
| docs/hardware-testing-validation.md | 866 | Excellent | Sign-off section empty |
| docs/led-status-codes.md | 150 | Good | Color naming inconsistency |
| docs/motor-drivers.md | 273 | Good | 🔴 **References non-existent `set jerk` command** |
| docs/tmc2209-guide.md | ~500 | Very Good | — |
| docs/troubleshooting.md | 222 | Excellent | — |
| docs/CHANGELOG.md | 81 | Good | Both versions dated same month |
| docs/arduino-core-version.md | 160 | Excellent | — |
| docs/energy-measurement-test-plan.md | ~700 | Good | Large, not fully analyzed |
| docs/testing_results.md | ~100 | Adequate | References removed source files |

### Critical Documentation-Code Mismatches

#### 1. GPIO Pin Error in architecture.md
```
DOCUMENTED:  TMC2209 UART uses RX=GPIO 17, TX=GPIO 18
ACTUAL CODE: TMC_TX_PIN=1, TMC_RX_PIN=2 (PinConfig.h)
IMPACT:      Users wiring based on this document will fail
```

#### 2. Non-existent Command in motor-drivers.md
```
DOCUMENTED:  "set jerk 5000" for S-curve acceleration
ACTUAL:      "set cubesteps <value>" is the real command (MotorController.cpp)
NOTE:        command-protocol.md correctly lists "set cubesteps"
```

#### 3. STSPIN220 Undocumented
```
CODE:        Full STSPIN220Driver implementation, factory detection (both bits HIGH)
DOCS:        hardware-detection.md says "Reserved/Future" for both-HIGH condition
             No STSPIN220 wiring guide, no mention in README
```

#### 4. PWM Resolution Mismatch
```
dc-motor-guide.md: Shows "8-bit resolution (0-255 duty cycle)"
PinConfig.h:       DC_PWM_RES = 10 (10-bit = 0-1023)
```

#### 5. Stale References in testing_results.md
```
References:  MCPWMStepper.cpp, AccelerationProfile.h
Reality:     These files were removed in v2.0 (per CHANGELOG.md)
```

---

## 7. Scripts & Tooling Audit

### Python Analysis Tools

| Script | Lines | Purpose | Issues |
|--------|-------|---------|--------|
| `analyze_energy_data.py` | 631 | Joulescope data analysis | Hard-coded Windows paths |
| `interactive_energy_analyzer.py` | 1,927 | Interactive GUI for energy/force data | Hard-coded paths; complex parsing |
| `motor_jog_tool.py` | 569 | PyQt6 motor control GUI | 2s ESP32 boot delay; no settings persistence |
| `test_discovery.py` | 15 | Quick test file finder | Hard-coded paths |

### Force-Deflection System (Scripts/FD/ and Scripts/NoJS/)

| Component | Language | Role |
|-----------|----------|------|
| `fd_arduino.ino` | Arduino C++ | HX711 load cell + stepper control |
| `fd_server_nojs.py` | Python | Raspberry Pi TCP server for FD Arduino |
| `fd_client_nojs.py` | Python | TCP client library |
| `ForceDeflection_NoMotor.py` | Python/PyQt6 | Single-position FD GUI |
| `ForceDeflection_MotorAndForces.py` | Python/PyQt6 | Multi-position automated FD |
| `orchestrator_nojs.py` | Python/PyQt6 | Main test orchestrator |
| `dut_controller.py` | Python | Serial interface to UMM |

### Common Script Issues
1. **Hard-coded Windows paths** throughout (`C:\Users\camiv\OneDrive...`)
2. **Hard-coded IP address** for FD server (192.168.1.12:5002)
3. **Code duplication** between FD/ and NoJS/ directories (fd_client, fd_arduino)
4. **No configuration file** support — all values hard-coded
5. **No logging framework** — only `print()` statements
6. **No reconnection logic** for serial/network failures

---

## 8. ESP32-Specific Dependencies Catalog

This section catalogs every ESP32-specific API and peripheral for STM32 translation planning.

### Hardware Peripherals Used

| Peripheral | ESP32 API | Used By | STM32 Equivalent |
|-----------|-----------|---------|-------------------|
| **MCPWM** | FastAccelStepper lib | Step pulse generation | Hardware timer (TIM1/TIM2/TIM8) with interrupt |
| **RMT** | `neopixelWrite()` | WS2812 LED | SPI/DMA bitbanging or timer-based |
| **LEDC** | `ledcSetup/Attach/Write()` | DC motor PWM | TIM PWM channels |
| **UART1** | `Serial1.begin(baud, config, rx, tx)` | TMC2209/2208 comms | USART with configurable pins |
| **USB CDC** | `Serial.begin()` | Host communication | USB CDC (STM32 with USB) or UART |
| **GPIO** | `pinMode/Write/Read()` | Detection, enable | HAL_GPIO functions |

### Arduino Core APIs Used

| API | Used In | STM32 Alternative |
|-----|---------|-------------------|
| `millis()` | Timing everywhere | `HAL_GetTick()` |
| `micros()` | Timing | `DWT->CYCCNT` or timer |
| `delay()` | Startup, animations | `HAL_Delay()` |
| `delayMicroseconds()` | — | Timer-based |
| `Serial.printf()` | Formatted output | `sprintf()` + UART write |
| `String` class | Command parsing | `std::string` or char arrays |
| `ESP.restart()` | Reboot | `NVIC_SystemReset()` |
| `INPUT_PULLDOWN` | GPIO mode | `GPIO_PULLDOWN` |

### Library Dependencies — Portability Assessment

| Library | ESP32 Support | STM32 Support | Action Needed |
|---------|--------------|---------------|---------------|
| FastAccelStepper v0.33.9 | ✅ Native | ❌ Not supported | **Replace** — use timer-based stepper (AccelStepper + HW timer, or custom) |
| TMCStepper v0.7.3 | ✅ | ✅ (via HardwareSerial) | **Minor adaptation** — provide UART interface |
| Arduino String | ✅ | ✅ (Arduino-STM32) | Works if using Arduino framework; otherwise replace |
| Unity (test) | ✅ | ✅ | Fully portable |

---

## 9. Cross-Cutting Issues

### Memory Management
- Driver objects allocated with `new` in DriverFactory but never `delete`d
- `inputBuffer` in main.cpp is unbounded Arduino `String`
- TMCStepper object allocated with `new`, freed in destructor
- FastAccelStepper objects managed by library (not explicitly freed)

### Error Handling Gaps
1. **No watchdog timer** — motor lockup could freeze system indefinitely
2. **No input buffer limit** — could exhaust RAM with long input
3. **No motor lockup detection** — `stop()` has no timeout
4. **Error polling paused during motion** — 500ms gap between checks only when idle
5. **Silent configuration failures** — STSPIN220 config methods return no error

### Type Safety
- `static_cast<TMC2209Driver*>` used without runtime type check
- MotorType-to-DriverType conversion uses `static_cast<int>` on enum

### Naming Inconsistencies
- DC motor `move(steps)` actually means milliseconds
- Speed is "steps/s" for steppers but "0.0–1.0" for DC — same interface
- `_mcpwmStepper` name retained even though MCPWM code was replaced by FastAccelStepper

---

## 10. STM32 Translation Considerations

### Components Requiring No Changes
- `IMotorDriver.h` — pure abstract interface, no platform code
- `MotorController.cpp/h` — uses only `Serial.print*()` and `millis()`; easy to abstract
- `PinConfig.h` — just needs GPIO number remapping
- `DriverFactory.cpp/h` — uses only `pinMode()`, `digitalRead()`, `digitalWrite()`
- Test infrastructure — Unity framework is fully portable

### Components Requiring Minor Adaptation
| Component | Changes Needed |
|-----------|---------------|
| `TMC2209Driver` | Replace `Serial1.begin(baud, config, rx, tx)` with STM32 UART init |
| `TMC2208Driver` | Same as TMC2209 |
| `main.cpp` | Replace `ESP.restart()` with `NVIC_SystemReset()` |

### Components Requiring Significant Rewrite
| Component | Reason | Approach |
|-----------|--------|----------|
| `FastAccelStepperWrapper` | Library not available for STM32 | Implement timer-based step generator with acceleration profiles |
| `StatusLED` | `neopixelWrite()` is ESP32-only RMT driver | Use SPI+DMA or timer-based WS2812 driver |
| `DCMotorDriver` | LEDC PWM is ESP32-specific | Use STM32 TIM PWM (HAL_TIM_PWM_Start) |

### Recommended STM32 Target
For equivalent capabilities (MCPWM, multiple UARTs, USB CDC, adequate GPIO):
- **STM32F4** series (e.g., STM32F411, STM32F446) — good timer/PWM support, USB
- **STM32G4** series — advanced timers, ideal for motor control
- **STM32H7** series — if higher performance needed

### Critical Path for Translation
1. **Replace FastAccelStepper** — Most complex task; need timer ISR-based step generation with trapezoidal/S-curve acceleration
2. **Port LEDC PWM** — Replace with STM32 TIM PWM for DC motor
3. **Port WS2812 driver** — Replace `neopixelWrite()` with SPI or timer-based protocol
4. **Port Serial** — Replace Arduino Serial with STM32 UART HAL
5. **Update pin map** — Remap all GPIO numbers for STM32 board
6. **Adapt TMCStepper** — Provide compatible HardwareSerial interface

---

## 11. Recommendations & Action Items

### 🔴 P0 — Fix Before Translation (Critical)

| # | Action | Details |
|---|--------|---------|
| 1 | **Fix architecture.md GPIO pins** | Change TMC2209 UART from "GPIO 17/18" to "GPIO 1/2" |
| 2 | **Fix motor-drivers.md command** | Remove `set jerk` references; clarify `set cubesteps` |
| 3 | **Add input buffer limit** | Cap `inputBuffer` in main.cpp at 256 characters |
| 4 | **Add type-safe driver casting** | Use `dynamic_cast` or store driver type for runtime check |

### 🟡 P1 — Fix Before Translation (Important)

| # | Action | Details |
|---|--------|---------|
| 5 | **Document STSPIN220 driver** | Add wiring guide, update detection truth table, add to README |
| 6 | **Fix dc-motor-guide.md** | Update PWM resolution from 8-bit to 10-bit |
| 7 | **Remove stale references** | Update testing_results.md to remove MCPWMStepper references |
| 8 | **Add hardware abstraction layer** | Create a HAL header for `millis()`, `Serial`, GPIO that can be swapped for STM32 |
| 9 | **Increase test coverage** | Add tests for MotorController.processCommand() using existing mock infrastructure |

### 🔵 P2 — Improve (Before Next Release)

| # | Action | Details |
|---|--------|---------|
| 10 | **Add watchdog timer** | Prevent system freeze on motor lockup |
| 11 | **Improve error polling** | Consider non-blocking error check during motion |
| 12 | **Unify speed semantics** | Document DC motor "steps=ms" explicitly in interface |
| 13 | **Add documentation index** | Master index linking all docs with brief descriptions |
| 14 | **Fix hard-coded paths in scripts** | Use configuration files or environment variables |
| 15 | **Resolve code duplication** | Merge FD/ and NoJS/ shared files into common location |

---

*End of Audit Report*
