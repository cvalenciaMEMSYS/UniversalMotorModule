# Universal Motor Module - Hardware Testing and Validation Plan

## 1. Overview

This document provides a comprehensive testing and validation plan for the Universal Motor Module hardware. It covers basic connectivity through to edge case validation for all supported motor drivers (TMC2209, TMC2208, DC Motor RZ7899).

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
- Logic analyzer (optional, useful for UART debugging)

### Software Tools
- PlatformIO IDE
- Serial terminal (PuTTY, minicom, or VS Code Serial Monitor)
- Logic analyzer software (if using)

---

## 3. Pre-Test Checklist

### 3.1 Visual Inspection
- [ ] Check solder joints on ESP32-S3 module
- [ ] Verify correct orientation of driver modules
- [ ] Confirm power supply voltage (12V for motors)
- [ ] Check all wire connections match [PinConfig.h](../src/config/PinConfig.h)

### 3.2 Pin Assignment Verification

| Function | GPIO | Verify Connection |
|----------|------|-------------------|
| TMC TX   | 1    | To 1kΩ → GPIO 2   |
| TMC RX   | 2    | To TMC PDN_UART   |
| TMC EN   | 4    | To TMC EN pin     |
| TMC STEP | 5    | To TMC STEP       |
| TMC DIR  | 6    | To TMC DIR        |
| DC IN1   | 7    | To H-Bridge IN1   |
| DC IN2   | 8    | To H-Bridge IN2   |
| DETECT VCC 1 | 10 | Detection circuit |
| DETECT BIT 0 | 11 | Detection circuit |
| DETECT BIT 1 | 12 | Detection circuit |
| DETECT VCC 2 | 13 | Detection circuit |
| LED      | 48   | Onboard NeoPixel  |

---

## 4. Testing Phases

### Phase 1: Basic System Validation

#### 1.1 ESP32-S3 Alive Test
**Objective**: Verify ESP32 boots and USB communication works

**Procedure**:
1. Connect ESP32-S3 via USB
2. Open serial monitor at 115200 baud
3. Press reset button
4. Observe boot messages

**Expected Results**:
```
Universal Motor Module v1.0
Detecting motor driver...
```

**Pass Criteria**: Boot messages appear within 5 seconds

---

#### 1.2 Status LED Test
**Objective**: Verify NeoPixel LED (GPIO 48) works

**Procedure**:
1. Power on system
2. Observe LED color sequence during boot
3. Verify color matches motor detection state

**Expected Results**:
| State | LED Color |
|-------|-----------|
| Booting | Blue flash |
| TMC2209 detected | Green |
| TMC2208 detected | Cyan |
| DC Motor detected | Yellow |
| Error | Red blink |

---

#### 1.3 Driver Detection Test
**Objective**: Verify hardware detection circuit

**Procedure**:
1. Connect detection jumpers for each driver type
2. Reset ESP32
3. Verify correct driver detected

**Expected Results**:
| GPIO 11 | GPIO 12 | Expected Driver |
|---------|---------|-----------------|
| LOW     | LOW     | TMC2209         |
| HIGH    | LOW     | DC Motor        |
| LOW     | HIGH    | TMC2208         |
| HIGH    | HIGH    | Reserved        |

---

### Phase 2: TMC2209 Stepper Driver Tests

#### 2.1 UART Communication Test
**Objective**: Verify TMC2209 UART communication

**Procedure**:
1. Connect TMC2209 with UART wiring
2. Power on system
3. Send command: `STATUS`

**Expected Results**:
```
TMC2209 UART: Initialized
GCONF: 0x000001C3
Connection: OK
```

**Troubleshooting**:
- If "UART unavailable", check 1kΩ resistor between TX/RX
- If garbled output, verify baud rate (115200)

---

#### 2.2 Enable/Disable Test
**Objective**: Verify motor enable control

**Procedure**:
1. Send `ENABLE` command
2. Measure EN pin with multimeter (should be LOW)
3. Send `DISABLE` command
4. Measure EN pin (should be HIGH)

**Expected Results**:
- Motor holds position when enabled
- Motor free-wheels when disabled

---

#### 2.3 Basic Motion Test
**Objective**: Verify step/dir motion control

**Procedure**:
1. Send `ENABLE`
2. Send `MOVE 200` (one revolution on 200-step motor)
3. Observe motor rotation
4. Send `MOVE -200`
5. Observe reverse rotation

**Expected Results**:
- Motor completes one full revolution forward
- Motor completes one full revolution reverse
- Position reported as 0 after return move

---

#### 2.4 Acceleration Profile Tests

##### 2.4.1 Trapezoidal Profile
**Procedure**:
1. Send `PROFILE TRAPEZOIDAL`
2. Send `SPEED 1000` (steps/sec)
3. Send `ACCEL 500` (steps/sec²)
4. Send `MOVE 5000`
5. Observe speed ramp-up, cruise, ramp-down

**Expected Results**:
- Smooth acceleration from rest
- Constant cruise speed in middle
- Smooth deceleration to stop
- No missed steps or stalls

##### 2.4.2 S-Curve Profile
**Procedure**:
1. Send `PROFILE SCURVE`
2. Send `JERK 5000`
3. Send `MOVE 5000`
4. Compare smoothness to trapezoidal

**Expected Results**:
- Even smoother acceleration transitions
- Reduced mechanical vibration
- No audible "jerk" at profile transitions

##### 2.4.3 Constant Speed
**Procedure**:
1. Send `PROFILE CONSTANT`
2. Send `MOVE 1000`

**Expected Results**:
- Immediate jump to target speed (may cause step loss at high speeds)

---

#### 2.5 Current Control Tests
**Objective**: Verify UART-based current control

**Procedure**:
1. Send `CURRENT 200` (200mA run current)
2. Feel motor torque (should be weak)
3. Send `CURRENT 800` (800mA)
4. Feel motor torque (should be stronger)

**Expected Results**:
- Noticeable torque difference
- Motor temperature increases with higher current

**Safety**: Do not exceed motor/driver rated current

---

#### 2.6 Microstep Tests
**Procedure**:
1. Send `MICROSTEPS 16`
2. Send `MOVE 200` → should rotate 1/16 revolution
3. Send `MICROSTEPS 256`
4. Send `MOVE 200` → should rotate 200/256 = 0.78 of a step

**Expected Results**:
- Higher microsteps = smoother motion
- Position accuracy maintained

---

#### 2.7 StealthChop/SpreadCycle Tests
**Procedure**:
1. Enable StealthChop: `STEALTHCHOP ON`
2. Move motor and listen for noise
3. Switch to SpreadCycle: `STEALTHCHOP OFF`
4. Compare noise levels

**Expected Results**:
- StealthChop: Very quiet, slight torque ripple
- SpreadCycle: Audible stepping, more consistent torque

---

### Phase 3: TMC2208 Stepper Driver Tests

*Similar to TMC2209 tests but with these differences:*

#### 3.1 UART Connection Test
**Note**: TMC2208 has single-wire UART on different pin

**Expected Results**:
- `TMC2208 UART: Initialized` message
- Read GCONF register successfully

#### 3.2 Fallback Mode Test
**Objective**: Test Step/Dir fallback when UART unavailable

**Procedure**:
1. Disconnect UART wire
2. Reset ESP32
3. Verify Step/Dir mode activates

**Expected Results**:
```
TMC2208: UART unavailable - using Step/Dir mode
```

---

### Phase 4: DC Motor Driver Tests

#### 4.1 Direction Control Test
**Objective**: Verify H-bridge direction control

**Procedure**:
1. Send `FORWARD`
2. Observe motor direction
3. Send `REVERSE`
4. Observe direction change
5. Send `COAST`
6. Motor should free-wheel
7. Send `BRAKE`
8. Motor should lock

**Expected Results**:
| Command | IN1 | IN2 | Motor State |
|---------|-----|-----|-------------|
| FORWARD | PWM | LOW | Forward     |
| REVERSE | LOW | PWM | Reverse     |
| COAST   | LOW | LOW | Free-wheel  |
| BRAKE   | HIGH| HIGH| Locked      |

---

#### 4.2 Speed Control Test
**Procedure**:
1. Send `SPEED 0.25` (25% speed)
2. Listen/observe motor speed
3. Send `SPEED 0.5` (50%)
4. Send `SPEED 1.0` (100%)

**Expected Results**:
- Proportional speed changes
- PWM frequency inaudible (20kHz)

---

#### 4.3 Timed Move Test
**Procedure**:
1. Send `MOVE 1000` (run for 1000ms)
2. Verify motor runs for ~1 second
3. Verify motor stops automatically

---

### Phase 5: Edge Case Testing

#### 5.1 Power Failure Recovery
**Procedure**:
1. Start a long move (MOVE 100000)
2. Cut 12V power during move
3. Restore 12V power
4. Check system state

**Expected Results**:
- ESP32 should detect motor fault
- Position tracking should indicate error
- System should require re-homing

---

#### 5.2 Rapid Command Spam
**Procedure**:
1. Send 100 commands rapidly via script:
   ```python
   for i in range(100):
       serial.write(f"MOVE {i*10}\n")
   ```
2. Observe behavior

**Expected Results**:
- Commands should be queued or rejected
- No crashes or hangs
- Buffer overflow should be handled gracefully

---

#### 5.3 Invalid Command Handling
**Procedure**:
Test various invalid inputs:
- `MOVE abc` (non-numeric)
- `MOVE 999999999999` (overflow)
- `SPEED -100` (negative)
- `CURRENT 10000` (out of range)
- Empty command
- Very long string (1000+ chars)

**Expected Results**:
- Error messages for each invalid input
- System remains stable
- No crashes or resets

---

#### 5.4 Stall Detection Test (TMC2209 only)
**Procedure**:
1. Enable stall detection: `STALLGUARD ON`
2. Set threshold: `STALLGUARD_THRESHOLD 50`
3. Hold motor shaft during move
4. Observe stall detection

**Expected Results**:
- Motor should stop on stall
- `STALL DETECTED` message appears
- Position tracking stops

---

#### 5.5 Overtemperature Handling
**Procedure**:
1. Run motor at high current continuously
2. Monitor TMC driver status
3. Check for thermal shutdown

**Expected Results**:
- `OVER_TEMP_WARNING` at 120°C
- `OVER_TEMP_SHUTDOWN` at 150°C
- Motor disabled automatically

---

#### 5.6 Short Move / Triangular Profile Test
**Procedure**:
1. Set high speed/low accel: `SPEED 10000`, `ACCEL 100`
2. Send short move: `MOVE 50`
3. Verify triangular profile detected

**Expected Results**:
- Motor accelerates partway, then decelerates
- Never reaches max speed
- Move completes accurately

---

#### 5.7 Very Slow Speed Test
**Procedure**:
1. Send `SPEED 10` (10 steps/sec)
2. Send `MOVE 100`
3. Observe very slow motion

**Expected Results**:
- 10 seconds to complete move
- Consistent timing
- No jittering or stalling

---

#### 5.8 Maximum Speed Test
**Procedure**:
1. Gradually increase speed:
   - `SPEED 1000` → move
   - `SPEED 2000` → move
   - `SPEED 5000` → move
   - Continue until step loss

**Expected Results**:
- Identify maximum reliable speed
- Step loss detection if available
- Document speed limit for motor

---

### Phase 6: Long-Duration Tests

#### 6.1 Endurance Test
**Procedure**:
1. Run continuous back-and-forth motion
2. Log motor temperature every 10 minutes
3. Run for 4+ hours

**Expected Results**:
- Temperature stabilizes below thermal limit
- No drift in position
- No UART communication errors

---

#### 6.2 Position Accuracy Test
**Procedure**:
1. Mark motor shaft position
2. Run 10,000 cycles of MOVE 200, MOVE -200
3. Check shaft position matches mark

**Expected Results**:
- Zero position error after 10,000 cycles
- If error exists, document magnitude

---

## 5. Test Logging Template

```
Date: _______________
Tester: _______________
Firmware Version: _______________

Test: _______________
Configuration:
  - Driver: TMC2209 / TMC2208 / DC Motor
  - Current: _____ mA
  - Speed: _____ steps/sec
  - Acceleration: _____ steps/sec²
  - Microsteps: _____
  - Profile: Trapezoidal / S-Curve / Constant

Procedure:
1. _______________________
2. _______________________
3. _______________________

Observations:
_________________________
_________________________

Result: PASS / FAIL

Notes:
_________________________
```

---

## 6. Troubleshooting Quick Reference

| Symptom | Likely Cause | Solution |
|---------|--------------|----------|
| No serial output | USB not recognized | Check driver, try different cable |
| Wrong driver detected | Detection jumpers | Check GPIO 10-13 wiring |
| UART timeout | Resistor missing | Add 1kΩ between TX and RX |
| Motor vibrates but doesn't move | Current too low | Increase current setting |
| Motor overheats | Current too high | Reduce current, improve cooling |
| Step loss at high speed | Speed too high | Reduce max speed, increase current |
| Jerky S-curve motion | Jerk too high | Reduce jerk parameter |
| Motor stalls mid-move | Load too high | Increase current, reduce acceleration |

---

## 7. Sign-Off

| Test Phase | Completed By | Date | Result |
|------------|--------------|------|--------|
| Phase 1: Basic System | | | |
| Phase 2: TMC2209 | | | |
| Phase 3: TMC2208 | | | |
| Phase 4: DC Motor | | | |
| Phase 5: Edge Cases | | | |
| Phase 6: Endurance | | | |

**Final Approval**: _________________ Date: _________
