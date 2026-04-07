# STM32 DC Motor Portability Guide — Nucleo L031K6

> **Audience:** AI agent or developer porting the DC motor subsystem from ESP32-S3 to STM32.
> **Scope:** DC motor (H-bridge) driver only. Stepper-specific drivers (TMC2209/2208, STSPIN220) and ESP32-only peripherals (NeoPixel LED, FastAccelStepper) are out of scope for this document.
> **Target Board:** STM32 Nucleo-L031K6 (STM32L031K6T6 — ARM Cortex-M0+, 32 MHz, 32 KB Flash, 8 KB RAM)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Pin Mapping — ESP32 to STM32](#2-pin-mapping--esp32-to-stm32)
3. [DC Motor Driver — Core Logic](#3-dc-motor-driver--core-logic)
   - 3.1 [Class Structure](#31-class-structure)
   - 3.2 [State Machine](#32-state-machine)
   - 3.3 [Speed Model](#33-speed-model)
   - 3.4 [Acceleration Ramping](#34-acceleration-ramping)
4. [ESP32 API → STM32 HAL Translation](#4-esp32-api--stm32-hal-translation)
   - 4.1 [PWM Initialization](#41-pwm-initialization)
   - 4.2 [PWM Duty Cycle Control](#42-pwm-duty-cycle-control)
   - 4.3 [GPIO Configuration](#43-gpio-configuration)
5. [H-Bridge Control Logic](#5-h-bridge-control-logic)
6. [Command Protocol — DC Motor Subset](#6-command-protocol--dc-motor-subset)
7. [Serial Interface](#7-serial-interface)
8. [Stepper Motor UART Pins (Reference)](#8-stepper-motor-uart-pins-reference)
9. [Build & Integration Notes](#9-build--integration-notes)
10. [Checklist for Porting](#10-checklist-for-porting)

---

## 1. Architecture Overview

The Universal Motor Module uses a **Strategy + Factory** architecture:

```
                ┌──────────────┐
                │ main.cpp     │  Serial loop, input buffer
                └──────┬───────┘
                       │
                       ▼
              ┌────────────────┐
              │MotorController │  Command parsing, status, error polling
              └────────┬───────┘
                       │ uses IMotorDriver*
                       ▼
              ┌────────────────┐
              │ IMotorDriver   │  Abstract interface (virtual methods)
              └────────┬───────┘
                       │ implementations
          ┌────────────┼────────────┐
          ▼            ▼            ▼
   ┌────────────┐ ┌──────────┐ ┌────────────────┐
   │TMC2209     │ │TMC2208   │ │ DCMotorDriver  │ ← this is what you port
   │Driver      │ │Driver    │ │                │
   └────────────┘ └──────────┘ └────────────────┘
```

**For the STM32 DC motor port, only these files are relevant:**

| File | Role | Port? |
|------|------|-------|
| `src/drivers/IMotorDriver.h` | Abstract interface + `MotorType` enum + `MotorStatus` struct | **Copy as-is** (pure C++, no platform deps) |
| `src/drivers/DCMotorDriver.h` | DC motor class declaration | **Port** (remove LEDC types) |
| `src/drivers/DCMotorDriver.cpp` | DC motor implementation (477 lines) | **Port** (replace `ledcSetup`/`ledcWrite` with STM32 HAL) |
| `src/config/PinConfig.h` | Pin numbers, PWM config, defaults | **Rewrite** for STM32 pin map |
| `src/core/MotorController.h/.cpp` | Command parsing and high-level control | **Port** (mostly platform-agnostic) |
| `src/main.cpp` | `setup()` / `loop()`, serial input buffer | **Port** (remove ESP32-specific calls) |

**Files you do NOT need:**

| File | Reason |
|------|--------|
| `src/drivers/TMC2209Driver.*` | Stepper-specific (UART) |
| `src/drivers/TMC2208Driver.*` | Stepper-specific (Step/Dir) |
| `src/drivers/STSPIN220Driver.*` | Stepper-specific (Step/Dir) |
| `src/drivers/FastAccelStepperWrapper.*` | ESP32 MCPWM step generation — not used by DC motor |
| `src/drivers/DriverFactory.*` | GPIO jumper detection — replace with compile-time selection or simplified detection for STM32 |
| `src/core/StatusLED.*` | WS2812 NeoPixel — ESP32 RMT peripheral, not available on STM32 |

---

## 2. Pin Mapping — ESP32 to STM32

### DC Motor Pins

| Function | ESP32-S3 Pin | STM32 Nucleo L031K6 Pin | Arduino Alias | Notes |
|----------|-------------|------------------------|---------------|-------|
| H-bridge IN1 (Forward/PWM) | GPIO 8 | **PB4** | **D12** | Must be Timer-capable for PWM |
| H-bridge IN2 (Backward/PWM) | GPIO 5 | **PB5** | **D11** | Must be Timer-capable for PWM |
| Enable (optional) | N/A (RZ7899 has no EN pin) | **PB0** | **D3** | Available if driver needs it |

### UART Pins (for future stepper support via TMC2209)

| Function | ESP32-S3 Pin | STM32 Nucleo L031K6 Pin | Arduino Alias | Notes |
|----------|-------------|------------------------|---------------|-------|
| TMC UART RX | GPIO 2 | **PB7** | **D4** | USART1_RX on L031K6 |
| TMC UART TX | GPIO 1 | **PB6** | **D5** | USART1_TX on L031K6 |

### STM32 Timer Assignment for PWM

On the Nucleo L031K6:

| Pin | Timer Channel | Alternate Function |
|-----|--------------|-------------------|
| PB4 (D12) | TIM22_CH1 or TIM3_CH1 | AF4 (TIM22) or AF2 (TIM3) |
| PB5 (D11) | TIM22_CH2 or TIM3_CH2 | AF4 (TIM22) or AF2 (TIM3) |

**Recommendation:** Use **TIM22** (or TIM3) for both PWM channels since PB4 and PB5 can share the same timer (CH1 and CH2), simplifying configuration. Both channels will run at the same frequency and resolution, which is exactly what the DC motor needs.

> **Important:** Verify timer availability against your full pin usage. The L031K6 has limited timers (TIM2, TIM21, TIM22, LPTIM1). TIM2 is often used by `millis()` in the Arduino STM32 core.

---

## 3. DC Motor Driver — Core Logic

### 3.1 Class Structure

`DCMotorDriver` implements the `IMotorDriver` interface. Key members:

```
DCMotorDriver
├── Hardware Config
│   ├── _in1Pin, _in2Pin          // GPIO pin numbers
│   ├── _pwmChannel1, _pwmChannel2 // ESP32 LEDC channels (NOT needed on STM32)
│   ├── _pwmFreq = 20000          // 20 kHz PWM frequency
│   ├── _pwmResolution = 10       // 10-bit (0–1023 duty range)
│   └── _maxDuty = 1023           // (1 << 10) - 1
│
├── Motion State
│   ├── _enabled                   // Logical enable flag
│   ├── _currentSpeed              // Float: -1.0 to +1.0
│   ├── _targetSpeed               // Float: -1.0 to +1.0
│   ├── _moving                    // Currently executing timed move
│   ├── _moveStartTime             // millis() when move started
│   ├── _moveDuration              // Duration in ms (0 = indefinite)
│   └── _moveDirection             // +1 or -1
│
├── Config
│   ├── _maxSpeedLimit             // 0.0–1.0, set via `set speed`
│   ├── _accelerationRate          // Speed units/sec ramping rate
│   └── _lastUpdateTime            // For delta-time calculation
│
└── Key Methods
    ├── init()                     // PWM setup
    ├── applySpeed(float)          // Converts ±1.0 speed → PWM duty
    ├── updateRamping()            // Linear acceleration toward target
    ├── update()                   // Called every loop() — handles timed moves + ramping
    ├── move(int32_t ms)           // Timed move (ms = milliseconds)
    ├── moveTo(int32_t pct)        // Direct speed set (-100 to +100 → float)
    ├── coast()                    // Both pins LOW → freewheeling
    └── brake()                    // Both pins HIGH → locked rotor
```

### 3.2 State Machine

The DC motor has a simple implicit state machine:

```
         ┌──────────┐  move(ms)   ┌───────────┐
         │          │────────────►│           │
         │  IDLE    │             │  TIMED    │──── update(): elapsed >= duration
         │ (coast)  │◄────────────│  MOVE     │
         │          │  stop/time  │           │
         └────┬─────┘  expires    └───────────┘
              │
              │ runForward/runBackward
              ▼
         ┌──────────┐
         │CONTINUOUS│──── stop/brake → back to IDLE
         │   RUN    │
         └──────────┘
```

Motion state is tracked by `_moving` (bool) and `_moveDuration`:
- `_moving=true, _moveDuration>0` → timed move
- `_moving=true, _moveDuration=0` → continuous run
- `_moving=false` → idle

### 3.3 Speed Model

Speed is represented as a **normalized float from -1.0 to +1.0**:
- `+1.0` = full speed forward
- `-1.0` = full speed backward
- `0.0` = stopped

The `_maxSpeedLimit` (0.0–1.0) caps the effective speed. Commands like `set speed 80` set this to `0.8`.

**Conversion to PWM duty cycle** (in `applySpeed()`):

```
duty = abs(speed) × _maxDuty    // e.g., 0.5 × 1023 = 511
```

Direction is controlled by which pin gets the PWM:
- `speed > 0` → IN1=duty, IN2=0 (forward)
- `speed < 0` → IN1=0, IN2=duty (reverse)
- `speed ≈ 0` → IN1=0, IN2=0 (coast)

A deadband of ±0.01 prevents floating-point drift from causing unintended low-duty output.

### 3.4 Acceleration Ramping

The `updateRamping()` method applies simple linear acceleration:

```
dt = (now - lastUpdateTime) / 1000.0   // seconds
maxChange = accelerationRate × dt
if |speedDiff| ≤ maxChange:
    currentSpeed = targetSpeed          // reached target
else:
    currentSpeed += sign(diff) × maxChange  // ramp toward target
```

- When `_accelerationRate ≤ 0`, speed changes are **instantaneous** (no ramping).
- Default acceleration rate is `1.0` speed-units/sec (takes 1 second to go from 0 to full).
- This logic is **purely mathematical** — no platform dependency. It ports as-is.

---

## 4. ESP32 API → STM32 HAL Translation

The DC motor driver has exactly **three** ESP32-specific API families to replace. All are in `DCMotorDriver::init()` and `DCMotorDriver::applySpeed()`/`coast()`/`brake()`.

### 4.1 PWM Initialization

**ESP32 code (Arduino Core 2.x):**
```cpp
// DCMotorDriver::init()
ledcSetup(_pwmChannel1, _pwmFreq, _pwmResolution);   // Configure channel
ledcSetup(_pwmChannel2, _pwmFreq, _pwmResolution);
ledcAttachPin(_in1Pin, _pwmChannel1);                 // Attach pin to channel
ledcAttachPin(_in2Pin, _pwmChannel2);
```

**STM32 equivalent (Arduino STM32 core / STM32duino):**
```cpp
// Using Arduino STM32 core (stm32duino), analogWrite is timer-based
// PB4 = D12, PB5 = D11 — both on same timer (TIM22 or TIM3)
pinMode(_in1Pin, OUTPUT);
pinMode(_in2Pin, OUTPUT);

// Set PWM frequency before first analogWrite
// STM32duino provides analogWriteFrequency() and analogWriteResolution()
analogWriteFrequency(20000);     // 20 kHz
analogWriteResolution(10);       // 10-bit (0–1023)

// Initial state: motor stopped
analogWrite(_in1Pin, 0);
analogWrite(_in2Pin, 0);
```

**If using STM32 HAL directly (without Arduino core):**
```cpp
// 1. Enable timer clock: __HAL_RCC_TIM22_CLK_ENABLE();
// 2. Configure GPIO PB4/PB5 as AF (alternate function) for TIM22 CH1/CH2
// 3. Configure timer:
//    - Prescaler: (SystemCoreClock / (pwmFreq × (maxDuty+1))) - 1
//    - Period (ARR): maxDuty (1023 for 10-bit)
//    - Mode: PWM1, output active high
// 4. Start PWM: HAL_TIM_PWM_Start(&htim22, TIM_CHANNEL_1);
//               HAL_TIM_PWM_Start(&htim22, TIM_CHANNEL_2);
```

**Clock calculation for L031K6 (32 MHz system clock):**
```
Target: 20 kHz PWM, 10-bit resolution (0–1023)
Timer clock = SystemCoreClock = 32,000,000 Hz
Required timer frequency = 20,000 × 1024 = 20,480,000 Hz
Prescaler = (32,000,000 / 20,480,000) - 1 ≈ 0  (no prescaler needed at 32 MHz)
ARR = 1023

Actual PWM frequency = 32,000,000 / (1 × 1024) = 31,250 Hz
```

> **Note:** At 32 MHz, achieving exactly 20 kHz with 10-bit resolution isn't possible. You get ~31.25 kHz (still ultrasonic, perfectly acceptable) or reduce resolution to 9-bit for ~62.5 kHz. Alternatively, use prescaler=1 for ~15.6 kHz. Any frequency above ~18 kHz is inaudible and fine for motor control.

### 4.2 PWM Duty Cycle Control

**ESP32 code:**
```cpp
ledcWrite(_pwmChannel1, duty);   // Set duty cycle (0–1023)
ledcWrite(_pwmChannel2, 0);
```

**STM32 equivalent (Arduino STM32 core):**
```cpp
analogWrite(_in1Pin, duty);      // Set duty cycle (0–1023, matching resolution)
analogWrite(_in2Pin, 0);
```

**STM32 HAL direct:**
```cpp
__HAL_TIM_SET_COMPARE(&htim22, TIM_CHANNEL_1, duty);  // IN1
__HAL_TIM_SET_COMPARE(&htim22, TIM_CHANNEL_2, 0);     // IN2
```

These calls appear in:
- `applySpeed(float speed)` — main speed application (3 branches: forward, reverse, stop)
- `coast()` — both channels to 0
- `brake()` — both channels to `_maxDuty` (1023)

### 4.3 GPIO Configuration

**ESP32 code:**
```cpp
pinMode(pin, OUTPUT);
digitalWrite(pin, HIGH/LOW);
```

**STM32 equivalent:** Same Arduino API — `pinMode()` and `digitalWrite()` work identically in the STM32duino core. No changes needed for any GPIO operations outside of PWM.

---

## 5. H-Bridge Control Logic

The H-bridge truth table is **platform-independent** and applies to any dual-input H-bridge (RZ7899 or equivalent):

| IN1 (PB4/D12) | IN2 (PB5/D11) | Motor Action |
|----------------|----------------|--------------|
| PWM (duty) | LOW (0) | **Forward** at duty% speed |
| LOW (0) | PWM (duty) | **Reverse** at duty% speed |
| LOW (0) | LOW (0) | **Coast** (freewheeling) |
| HIGH (max) | HIGH (max) | **Brake** (locked rotor) |

The `applySpeed()` function in `DCMotorDriver.cpp` lines 289–311 implements this directly. The logic is pure math + PWM writes — fully portable.

### Enable Pin

The RZ7899 H-bridge does **not** have an enable pin — it's always active. However, the STM32 board exposes **PB0 (D3)** as an enable pin for modules that need it. If your H-bridge has an enable input:

```cpp
constexpr uint8_t MOTOR_EN_PIN = PB0;  // D3

void enableMotor() {
    digitalWrite(MOTOR_EN_PIN, HIGH);  // or LOW, depending on active level
}
```

The `DCMotorDriver` class already has `enable()`/`disable()` virtual methods — they currently just set a boolean flag. You can extend them to drive the enable pin.

---

## 6. Command Protocol — DC Motor Subset

When the system detects a DC motor, the following commands are available via serial (115200 baud):

| Command | DC Motor Meaning | Implementation |
|---------|-----------------|----------------|
| `move <n>` | Run for `n` milliseconds (positive=forward, negative=reverse) | `DCMotorDriver::move(n)` |
| `abs <n>` | Set speed to `n`% (-100 to +100; mapped to -1.0…+1.0) | `DCMotorDriver::moveTo(n)` |
| `run forward` | Run continuously forward at current max speed | `DCMotorDriver::runForward()` |
| `run backward` | Run continuously backward at current max speed | `DCMotorDriver::runBackward()` |
| `stop` | Controlled stop (ramp down via acceleration rate) | `DCMotorDriver::stop()` |
| `brake` | Immediate stop (both pins HIGH, motor locked) | `DCMotorDriver::brake()` |
| `enable` | Enable driver (no-op for RZ7899) | `DCMotorDriver::enable()` |
| `disable` | Disable / coast | `DCMotorDriver::disable()` |
| `set speed <n>` | Set max speed limit (0–100%) | `DCMotorDriver::setMaxSpeed(n)` |
| `set accel <n>` | Set acceleration ramp rate | `DCMotorDriver::setAcceleration(n)` |
| `status` or `?` | Print current speed, target, direction, etc. | `DCMotorDriver::printDiagnostics()` |
| `help` | List available commands | `MotorController::printHelp()` |

**Note on `move` semantics:** For DC motors, the `steps` parameter in `move(int32_t steps)` is reinterpreted as **milliseconds** (duration). This is documented in `IMotorDriver.h` and `command-protocol.md`. The sign determines direction.

---

## 7. Serial Interface

The serial interface in `main.cpp` is straightforward and mostly platform-agnostic:

- **Baud rate:** 115200
- **Buffer:** Up to 128 printable ASCII characters
- **Termination:** `\n` or `\r`, or 100ms timeout after last character
- **Processing:** `controller.processCommand(inputBuffer)` dispatches to `MotorController`

**ESP32-specific calls to remove/replace in `main.cpp`:**

| ESP32 Call | Purpose | STM32 Replacement |
|------------|---------|-------------------|
| `ESP.restart()` | Software reboot | `NVIC_SystemReset()` or `HAL_NVIC_SystemReset()` |
| `statusLED.begin()` / `statusLED.update()` | WS2812 NeoPixel LED | Remove or replace with simple GPIO LED |
| `motorTypeToDriverType()` | LED color per driver | Remove (LED-specific) |

The `Serial` object works identically in STM32duino — `Serial.available()`, `Serial.read()`, `Serial.print()` all use the same Arduino API.

---

## 8. Stepper Motor UART Pins (Reference)

While the DC motor port does not use UART for motor control, the physical STM32 board is prepared for TMC2209 stepper communication in the future:

| Function | STM32 Pin | Arduino Alias | Peripheral |
|----------|-----------|---------------|------------|
| TMC UART RX | PB7 | D4 | USART1_RX |
| TMC UART TX | PB6 | D5 | USART1_TX |

The TMCStepper library already supports STM32 — if stepper support is added later, configure USART1 on these pins at 115200 baud with the same single-wire UART protocol used on ESP32 (TX through 1kΩ resistor to RX, shared with TMC2209 PDN_UART).

---

## 9. Build & Integration Notes

### PlatformIO Configuration

The existing `platformio.ini` targets ESP32-S3. For STM32 Nucleo L031K6, add a new environment:

```ini
[env:nucleo_l031k6]
platform = ststm32
board = nucleo_l031k6
framework = arduino
monitor_speed = 115200
build_flags =
    -D TARGET_STM32
    -D DC_IN1_PIN=PB4
    -D DC_IN2_PIN=PB5
    -D MOTOR_EN_PIN=PB0
```

### Conditional Compilation

Use `#ifdef TARGET_STM32` to gate platform-specific code:

```cpp
#ifdef TARGET_STM32
    // STM32 PWM init
    analogWriteFrequency(20000);
    analogWriteResolution(10);
    analogWrite(_in1Pin, 0);
    analogWrite(_in2Pin, 0);
#else
    // ESP32 LEDC init
    ledcSetup(_pwmChannel1, _pwmFreq, _pwmResolution);
    ledcSetup(_pwmChannel2, _pwmFreq, _pwmResolution);
    ledcAttachPin(_in1Pin, _pwmChannel1);
    ledcAttachPin(_in2Pin, _pwmChannel2);
    ledcWrite(_pwmChannel1, 0);
    ledcWrite(_pwmChannel2, 0);
#endif
```

### Memory Considerations

The L031K6 has only **32 KB Flash** and **8 KB RAM**. The DC motor driver alone is small (~2 KB compiled), but be mindful of:
- `String` objects (dynamic allocation) — consider replacing with fixed `char[]` buffers
- `Serial.print()` format strings — stored in Flash on Arduino; should be fine
- Remove all stepper driver code to save Flash

### Removed Dependencies

| ESP32 Library | Used By | STM32 Action |
|---------------|---------|--------------|
| FastAccelStepper | Stepper drivers only | **Remove** — not used by DC motor |
| TMCStepper | TMC2209/2208 drivers | **Remove** for DC-motor-only build |
| neopixelWrite / RMT | StatusLED | **Remove** — replace with basic GPIO LED or omit |

---

## 10. Checklist for Porting

### Phase 1: Minimal DC Motor Control

- [ ] Create STM32 PlatformIO environment (`nucleo_l031k6`)
- [ ] Create `PinConfig_STM32.h` with STM32 pin definitions:
  - `DC_IN1_PIN = PB4` (D12)
  - `DC_IN2_PIN = PB5` (D11)
  - `MOTOR_EN_PIN = PB0` (D3) — if needed
  - `DC_PWM_FREQ = 20000`
  - `DC_PWM_RES = 10`
- [ ] Copy `IMotorDriver.h` as-is (no platform deps)
- [ ] Port `DCMotorDriver.h` — remove `_pwmChannel1`/`_pwmChannel2` members (not needed on STM32)
- [ ] Port `DCMotorDriver.cpp`:
  - Replace `ledcSetup()`/`ledcAttachPin()` with `analogWriteFrequency()`/`analogWriteResolution()`/`pinMode()`
  - Replace all `ledcWrite(channel, duty)` with `analogWrite(pin, duty)` (7 call sites)
- [ ] Port `MotorController.h/.cpp`:
  - Remove `#include <FastAccelStepper.h>`
  - Remove TMC2209/TMC2208-specific includes and `static_cast` blocks
  - Keep all command parsing and DC motor logic
- [ ] Port `main.cpp`:
  - Remove `ESP.restart()` → use `NVIC_SystemReset()`
  - Remove `StatusLED` calls (or stub them out)
  - Remove `DriverFactory` — directly instantiate `DCMotorDriver`
  - Keep serial input loop as-is
- [ ] Verify PWM output with oscilloscope (20 kHz, 10-bit duty)
- [ ] Test all DC motor commands via serial

### Phase 2: Polish & Integration

- [ ] Replace Arduino `String` with fixed `char[128]` buffer if RAM is tight
- [ ] Add enable pin support in `DCMotorDriver` if hardware requires it
- [ ] Add error LED on a spare GPIO (single-color, no NeoPixel needed)
- [ ] Add `reboot` command using `NVIC_SystemReset()`

### Phase 3: Future Stepper Support (Optional)

- [ ] Configure USART1 on PB6 (TX) / PB7 (RX) at 115200 baud
- [ ] Port TMC2209Driver with TMCStepper library (already supports STM32)
- [ ] Implement step generation using STM32 timer interrupts (replaces FastAccelStepper)
- [ ] Add hardware detection or compile-time driver selection

---

**Document Author:** GitHub Copilot
**Last Updated:** April 2026
**Source Platform:** ESP32-S3 Super Mini (LOLIN S3 Mini)
**Target Platform:** STM32 Nucleo-L031K6
