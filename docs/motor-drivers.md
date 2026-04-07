# Motor Driver Documentation

This document describes the motor drivers supported by the Universal Motor Module.

## Supported Drivers

| Driver | Type | Interface | Auto-Detection |
|--------|------|-----------|----------------|
| TMC2209 | Stepper | UART + Step/Dir | Default (GPIO11=LOW, GPIO12=LOW) |
| TMC2208 | Stepper | UART + Step/Dir | GPIO11=LOW, GPIO12=HIGH |
| DC Motor | DC | H-Bridge PWM | GPIO11=HIGH, GPIO12=LOW |

---

## TMC2209 vs TMC2208 Comparison

Both drivers use UART for configuration and support Step/Dir fallback mode.

| Feature | TMC2209 | TMC2208 |
|---------|---------|---------|
| UART Control | ✅ Yes | ✅ Yes |
| StealthChop2 (Silent) | ✅ Yes | ✅ Yes |
| SpreadCycle (High Torque) | ✅ Yes | ✅ Yes |
| **StallGuard4 (Sensorless Homing)** | ✅ Yes | ❌ **No** |
| **CoolStep (Current Reduction)** | ✅ Yes | ❌ **No** |
| Voltage Range | 4.75-29V | 4.75-**36V** |
| Max Current (peak) | **2.8A** | 2A |
| Microstep Pin Settings | 8,16,32,64 | 2,4,8,16,32 |
| Step/Dir Fallback | ✅ Yes | ✅ Yes |

**Key Difference**: TMC2209 has StallGuard for sensorless homing; TMC2208 requires physical limit switches.

---

## TMC2209 Driver (Full UART Control)

### Overview
The TMC2209 is a high-performance stepper driver with UART configuration. It provides:
- **StealthChop**: Silent operation mode
- **StallGuard**: Sensorless stall/homing detection
- **UART Configuration**: All parameters adjustable at runtime
- **Current Control**: Precise run/hold current setting

### Wiring

```
ESP32 GPIO 1 (TX) ──[1kΩ]── ESP32 GPIO 2 (RX)
                                   │
          TMC2209 PDN_UART/RX pin ←┘

TMC2209 TX pin = floating (not connected)

Control Pins:
  GPIO 4  →  TMC2209 EN   (active LOW = enabled)
  GPIO 5  →  TMC2209 STEP (rising edge = 1 microstep)
  GPIO 6  →  TMC2209 DIR  (direction)
```

### Features
- **Current Setting**: `set current 800` (0-2000 mA)
- **Microstepping**: `set microsteps 16` (1, 2, 4, 8, 16, 32, 64, 128, 256)
- **StallGuard Homing**: `home` command uses sensorless homing
- **Diagnostics**: Full register readout via `status` command

### Commands
All standard commands work:
```
move 1000          # Move 1000 steps forward
abs 5000           # Move to absolute position 5000
set speed 2000     # Set max speed to 2000 steps/sec
set accel 500      # Enable trapezoidal acceleration
set cubesteps 400  # Enable S-curve acceleration (0 = trapezoidal)
set current 600    # Set run current to 600mA
home               # StallGuard sensorless homing
status             # Show full diagnostics
```

### Step/Dir Fallback Mode
If UART communication fails (wiring issue, etc.), you can still operate in Step/Dir mode:

```
stepdir on         # Switch to Step/Dir only mode
stepdir off        # Try to re-enable UART
```

**In Step/Dir mode:**
- Current limit set by Vref potentiometer (hardware)
- Microstepping set by MS1/MS2 pins (hardware)
- StallGuard homing NOT available
- Runtime configuration NOT available

---

## TMC2208 Driver (UART Control)

### Overview
The TMC2208 is a stepper driver with UART configuration, similar to TMC2209 but **without StallGuard or CoolStep**:
- **UART Configuration**: Current, microstepping, StealthChop via UART
- **StealthChop2**: Silent operation mode
- **SpreadCycle**: High torque mode
- **No StallGuard**: Requires external limit switch for homing
- **Fallback Mode**: Step/Dir only if UART unavailable

### Wiring

```
ESP32 GPIO 1 (TX) ──[1kΩ]── ESP32 GPIO 2 (RX)
                                   │
          TMC2208 PDN_UART/RX pin ←┘

TMC2208 TX pin = floating (not connected)

Control Pins:
  GPIO 4  →  TMC2208 EN   (active LOW = enabled)
  GPIO 5  →  TMC2208 STEP (rising edge = 1 microstep)
  GPIO 6  →  TMC2208 DIR  (direction)
```

### Features (via UART)
- **Current Setting**: `set current 800` (0-2000 mA)
- **Microstepping**: `set microsteps 16` (1, 2, 4, 8, 16, 32, 64, 128, 256)
- **StealthChop**: Silent operation enabled by default
- **Diagnostics**: Register readout via `status` command

### Limitations (vs TMC2209)
- `home` command **NOT supported** (no StallGuard)
- No automatic current reduction (no CoolStep)
- Use physical limit switches for homing

### Commands
```
move 1000          # Move 1000 steps forward
abs 5000           # Move to absolute position 5000
set speed 2000     # Set max speed to 2000 steps/sec
set accel 500      # Enable trapezoidal acceleration
set cubesteps 400  # Enable S-curve acceleration (0 = trapezoidal)
set current 600    # Set run current to 600mA (via UART)
stepdir on         # Switch to Step/Dir fallback mode
status             # Show diagnostics
```

### Step/Dir Fallback Mode
Same as TMC2209 - use `stepdir on` if UART fails:
- Current set by Vref potentiometer
- Microstepping set by MS1/MS2 pins

---

## DC Motor Driver (H-Bridge PWM)

### Overview
Controls DC motors via an H-bridge (like RZ7899). Uses PWM for speed control.

### Wiring

```
GPIO 8  →  H-bridge IN1 (PWM capable)
GPIO 9  →  H-bridge IN2 (PWM capable)

For RZ7899 or similar:
  VCC  →  Motor supply voltage (6-12V typical)
  GND  →  Common ground with ESP32
  OUT1 →  Motor terminal 1
  OUT2 →  Motor terminal 2
```

### Control Modes
| IN1 | IN2 | Motor State |
|-----|-----|-------------|
| PWM | LOW | Forward |
| LOW | PWM | Reverse |
| LOW | LOW | Coast (free-wheel) |
| HIGH | HIGH | Brake (locked) |

### PWM Configuration
- **Frequency**: 20kHz (ultrasonic, silent)
- **Resolution**: 10-bit (0-1023 duty cycle)
- **Channels**: Uses LEDC channels 0 and 1

### Commands
DC motor interprets commands differently:
```
move 1000          # Run forward for 1000ms (1 second)
move -500          # Run reverse for 500ms
abs 500            # Set speed to 50% forward (-1000 to +1000)
abs -300           # Set speed to 30% reverse
set speed 80       # Set max speed limit to 80%
stop               # Controlled stop with ramping
```

### Special Behavior
- **Position**: Virtual position counter (no encoder feedback)
- **Acceleration**: Ramping applied for smooth speed transitions
- **Homing**: Not supported (no position feedback)

---

## Auto-Detection

The module automatically detects which driver is connected using GPIO 10-13:

### Detection Circuit
```
GPIO 10 ──┐
GPIO 13 ──┴── VCC source for jumpers

GPIO 11 ── Detect Bit 0 (pull-down input)
GPIO 12 ── Detect Bit 1 (pull-down input)
```

### Detection Table
| GPIO 11 | GPIO 12 | Detected Driver |
|---------|---------|-----------------|
| LOW | LOW | TMC2209 (default) |
| HIGH | LOW | DC Motor |
| LOW | HIGH | TMC2208 |
| HIGH | HIGH | STSPIN220 (Step/Dir stepper) |

To select a driver, connect jumper wires from VCC pins to the appropriate detect pins.

---

## Acceleration Profiles

All drivers support the same acceleration profiles:

### Constant (Default)
```
set accel 0        # Disable acceleration
```
Instant speed changes. Simple but can cause mechanical stress.

### Trapezoidal
```
set accel 500      # Steps/sec² acceleration
```
Linear acceleration and deceleration ramps. Smooth but not optimal.

### S-Curve (Cubic)
```
set accel 500      # Steps/sec² max acceleration
set cubesteps 400  # Ramp steps for S-curve (0 = trapezoidal)
```
S-curve motion using FastAccelStepper's cubic acceleration ramp. The `cubesteps`
parameter controls how many steps are used for the acceleration/deceleration
transition, producing smoother jerk-limited motion compared to a linear ramp.

---

## Error Detection

### TMC2209 (Full Error Detection)
- Over-temperature
- Short circuit (coils to GND/VCC)
- Open load (motor disconnected)
- Communication failure (UART)
- Stall detection (StallGuard)

### TMC2208 (Limited)
- No error detection without UART
- Physical symptoms only (overheating, no motion)

### DC Motor (None)
- No error detection
- Monitor motor externally

Error states are shown via the status LED (solid red).
