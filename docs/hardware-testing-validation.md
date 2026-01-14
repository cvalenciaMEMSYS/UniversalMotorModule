# Universal Motor Module - Hardware Testing and Validation Plan

## 1. Overview

This document provides a comprehensive testing and validation plan for the Universal Motor Module v2.0 firmware using FastAccelStepper. All test commands have been updated to match the current command protocol.

**Firmware Version**: v2.0 (FastAccelStepper)

---

## 2. Required Equipment

### Hardware

- ESP32-S3 Super Mini (target platform)
- TMC2209 v1.3 stepper driver module
- TMC2208 stepper driver module  
- RZ7899 H-Bridge DC motor driver
- NEMA 17 stepper motor (17HS4401 or similar)
- Small DC motor (6-12V)
- 12V/2A power supply
- USB cable for programming/debugging
- Multimeter
- Oscilloscope (recommended for timing analysis)

### Software Tools

- PlatformIO IDE
- Serial terminal (115200 baud)

---

## 3. Pre-Test Checklist

### 3.1 Visual Inspection

- [ ] Check solder joints on ESP32-S3 module
- [ ] Verify correct orientation of driver modules
- [ ] Confirm power supply voltage (12V for motors)
- [ ] Check all wire connections match PinConfig.h

### 3.2 Pin Assignment Verification

| Function     | GPIO | Verify Connection |
| ------------ | ---- | ----------------- |
| TMC TX       | 1    | To 1kΩ → GPIO 2   |
| TMC RX       | 2    | To TMC PDN_UART   |
| TMC EN       | 4    | To TMC EN pin     |
| TMC STEP     | 5    | To TMC STEP       |
| TMC DIR      | 6    | To TMC DIR        |
| DC FI        | 8    | To H-Bridge FI    |
| DC BI        | 9    | To H-Bridge BI    |
| DETECT VCC 1 | 10   | Detection circuit |
| DETECT BIT 0 | 11   | Detection circuit |
| DETECT BIT 1 | 12   | Detection circuit |
| DETECT VCC 2 | 13   | Detection circuit |
| LED          | 48   | Onboard NeoPixel  |

---

## 4. Testing Phases

### Phase 1: Basic System Validation

---

#### 1.1 ESP32-S3 Boot Test

**Objective**: Verify ESP32 boots and USB communication works

**Procedure**:

1. Connect ESP32-S3 via USB
2. Open serial monitor at 115200 baud
3. Press reset button
4. Observe boot messages

**Expected Results**:

```
Universal Motor Module v2.0
Detecting motor driver...
TMC2209 detected
Ready for commands. Type 'help' for available commands.
```

**Pass Criteria**: Boot messages appear within 5 seconds

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 1.2 Startup LED Sequence Test

**Objective**: Verify startup RGB chase sequence

**Procedure**:

1. Power on or reset the ESP32
2. Observe LED immediately after boot

**Expected Results**:

- Red flash (150ms)
- Green flash (150ms)
- Blue flash (150ms)
- White flash (150ms)
- Then transitions to initializing (blue) → ready (driver color)

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 1.3 Status LED Colors Test

**Objective**: Verify LED shows correct colors for states

**Commands to test**:

```
status          # LED shows driver color (green for TMC2209)
enable          # LED bright
disable         # LED dimmed
move 3200       # LED pulses during motion
```

**Expected Colors**:

| State            | LED Color |
| ---------------- | --------- |
| Startup sequence | R→G→B→W   |
| Initializing     | Blue      |
| TMC2209 Ready    | Green     |
| TMC2208 Ready    | Cyan      |
| DC Motor Ready   | Yellow    |
| Moving           | Pulsing   |
| Error            | Red       |

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 1.4 Driver Detection Test

**Objective**: Verify hardware detection circuit

**Procedure**:

1. Connect detection jumpers for each driver type
2. Reset ESP32
3. Observe detected driver in boot message

**Expected Results**:

| GPIO 11 | GPIO 12 | Expected Driver |
| ------- | ------- | --------------- |
| LOW     | LOW     | TMC2209         |
| HIGH    | LOW     | DC Motor        |
| LOW     | HIGH    | TMC2208         |

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

### Phase 2: TMC2209 Stepper Driver Tests

---

#### 2.1 UART Communication Test

**Objective**: Verify TMC2209 UART communication

**Commands**:

```
status
diag
```

**Expected Results**:

- Status shows driver info, position, speed settings
- Diag shows TMC2209 register values and no error flags

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.2 Enable/Disable Test

**Objective**: Verify motor enable control

**Commands**:

```
enable
disable
enable
```

**Procedure**:

1. Send `enable` - motor should hold position (hard to turn)
2. Send `disable` - motor should free-wheel (easy to turn)

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.3 Basic Motion Test

**Objective**: Verify step/dir motion control

**Commands**:

```
enable
set speed 3200
set accel 5000
move 3200
status
move -3200
status
```

**Expected Results**:

- Motor completes one full revolution forward
- Position shows 3200
- Motor completes one full revolution reverse
- Position shows 0

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.4 Speed Configuration Test

**Commands**:

```
set speed 1000
move 3200
set speed 10000
move 3200
set speed 50000
move 3200
```

**Expected Results**:

- Each move completes at visibly different speeds
- All moves accurate (1 revolution each)

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.5 Acceleration Test

**Commands**:

```
set accel 500
move 10000
set accel 5000
move 10000
set accel 50000
move 10000
```

**Expected Results**:

- Low accel: Slow ramp up/down visible
- High accel: Quick ramp, almost instant speed changes
- All moves accurate

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.6 Constant Velocity Mode Test

**Objective**: Test `set accel 0` for constant velocity

**Commands**:

```
set accel 0
move 3200
```

**Expected Results**:

- Message: "Constant velocity mode (very high internal accel)"
- Motor moves at set speed with minimal ramp

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.7 S-Curve (Cubesteps) Test

**Objective**: Test cubic acceleration for smooth motion

**Commands**:

```
set cubesteps 50
set speed 5000
set accel 2000
move 10000
set cubesteps 0
move 10000
```

**Expected Results**:

- With cubesteps: Smoother acceleration start/end
- Without cubesteps: Standard linear acceleration
- Verify with oscilloscope for smooth frequency ramp

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.8 Continuous Rotation Test

**Objective**: Test run forward/backward commands

**Commands**:

```
run forward
(wait 3 seconds)
brake
run backward
(wait 3 seconds)
stop
```

**Expected Results**:

- `run forward`: Motor accelerates to set speed, continues
- `brake`: Motor decelerates smoothly to stop
- `run backward`: Motor runs opposite direction
- `stop`: Motor stops immediately

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.9 Position Query Test

**Commands**:

```
set pos 0
move 1000
get pos
get target
move 500
get pos
get target
```

**Expected Results**:

- `get pos` shows current position
- `get target` shows target position
- Values update correctly during/after moves

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.10 Speed Query Test

**Commands**:

```
run forward
get speed
get rampstate
brake
get speed
get rampstate
```

**Expected Results**:

- During motion: `get speed` shows current speed
- `get rampstate`: 1 (accelerating), 0 (coasting), -1 (decelerating)
- After stop: speed shows 0

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.11 Current Control Test

**Commands**:

```
set current 400
enable
(try to turn motor by hand)
set current 1200
(try to turn motor by hand)
```

**Expected Results**:

- Low current: Motor can be turned with some resistance
- High current: Motor much harder to turn

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.12 Hold Current Test

**Objective**: Test IHOLD configuration

**Commands**:

```
set ihold 25
enable
(motor at 25% hold current when stationary)
set ihold 100
(motor at full hold current)
```

**Expected Results**:

- Lower ihold: Weaker holding torque
- Higher ihold: Stronger holding torque

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.13 Auto-Disable Test

**Objective**: Test automatic motor enable/disable

**Commands**:

```
set autodisable on
move 3200
(wait 200ms after move completes)
(check if motor is free-wheeling)
set autodisable off
move 3200
(motor should hold position after move)
```

**Expected Results**:

- With autodisable on: Motor disables ~100ms after motion
- With autodisable off: Motor stays enabled

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.14 Microstep Test

**Commands**:

```
set microsteps 16
move 3200
set microsteps 64
move 12800
set microsteps 256
move 51200
```

**Expected Results**:

- Each move = 1 revolution
- Higher microsteps = smoother motion

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 2.15 StealthChop/SpreadCycle Test

**Commands**:

```
stealthchop
move 3200
(listen for motor noise)
spreadcycle
move 3200
(compare noise levels)
```

**Expected Results**:

- StealthChop: Very quiet operation
- SpreadCycle: Audible stepping, more torque

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

### Phase 3: High-Speed & Precision Tests

---

#### 3.1 Maximum Speed Test

**Commands**:

```
set speed 100000
set accel 50000
move 100000
```

**Expected Results**:

- Motor reaches high speed without stalling
- Use oscilloscope to verify pulse rate

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 3.2 Position Accuracy Test

**Commands**:

```
set pos 0
move 32000
move -32000
get pos
```

**Expected Results**:

- After forward+backward: Position returns to 0
- No accumulated error

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 3.3 Pulse Gap Test (Oscilloscope)

**Objective**: Verify no gaps during acceleration

**Commands**:

```
set speed 10000
set accel 1000
move 100000
```

**Oscilloscope Setup**:

- Probe on STEP pin (GPIO 5)
- Trigger on rising edge
- Timebase: 10ms/div

**Expected Results**:

- Continuous pulse train
- No gaps during acceleration/deceleration
- Smooth frequency ramp

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

### Phase 4: Edge Case Tests

---

#### 4.1 Command Spam Test

**Procedure**: Send many commands rapidly

**Commands**:

```
move 100
move 200
move 300
stop
move 100
```

**Expected Results**:

- System handles rapid commands
- No crashes or hangs

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 4.2 Invalid Input Test

**Commands to test**:

```
move abc
move 999999999999
set speed -100
set current 99999
invalidcommand
```

**Expected Results**:

- Error messages for invalid input
- System remains stable

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 4.3 Emergency Stop Test

**Commands**:

```
run forward
stop
get pos
```

**Expected Results**:

- `stop` immediately halts motor
- Position tracking remains accurate

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

### Phase 5: DC Motor Tests

---

#### 5.1 DC Motor Detection

**Procedure**: Connect DC motor jumper (GPIO 11 HIGH)

**Expected**: System detects DC Motor mode

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 5.2 DC Motor Speed Control

**Commands**:

```
set speed 64
run forward
set speed 128
set speed 255
stop
```

**Expected Results**: Variable speed operation

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

### Phase 6: Help & Status Commands

---

#### 6.1 Help Command

**Command**: `help`

**Expected**: Full list of available commands displayed

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 6.2 Status Command

**Command**: `status`

**Expected**: Motor state, position, speed, configuration displayed

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

#### 6.3 Diagnostics Command

**Command**: `diag`

**Expected**: TMC2209 register values and flags

##### Test Result

- [ ] PASS
- [ ] FAILED
- [ ] SKIPPED

---

## 5. Test Results Summary

| Test Phase | Tests | Passed | Failed | Skipped |
|------------|-------|--------|--------|---------|
| Phase 1: Basic System | 4 | | | |
| Phase 2: TMC2209 | 15 | | | |
| Phase 3: Precision | 3 | | | |
| Phase 4: Edge Cases | 3 | | | |
| Phase 5: DC Motor | 2 | | | |
| Phase 6: Commands | 3 | | | |
| **TOTAL** | **30** | | | |

---

## 6. Sign-Off

| Test Phase            | Completed By | Date | Result |
| --------------------- | ------------ | ---- | ------ |
| Phase 1: Basic System |              |      |        |
| Phase 2: TMC2209      |              |      |        |
| Phase 3: Precision    |              |      |        |
| Phase 4: Edge Cases   |              |      |        |
| Phase 5: DC Motor     |              |      |        |
| Phase 6: Commands     |              |      |        |

**Final Approval**: _________________ Date: _________________

---

*Last updated: January 2026 - v2.0 FastAccelStepper*
