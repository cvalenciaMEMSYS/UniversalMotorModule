# MCPWM High-Speed Stepping - Troubleshooting Guide

**Date:** January 7, 2025  
**Hardware:** ESP32-S3-Mini + TMC2209 + NEMA 17 Stepper Motor  
**Implementation:** MCPWM (Hardware PWM) replacing software stepping

---

## 📋 Implementation Summary

### What Was Changed

**Architecture Shift:**
- **Old:** Software stepping via `digitalWrite()` in timed loops
- **New:** ESP32-S3 MCPWM peripheral generates hardware PWM pulses
- **Goal:** Enable high-speed operation (26,000-53,000 steps/sec for 1000 RPM)

**Files Created:**
- `src/drivers/MCPWMStepper.h` - MCPWM wrapper class
- `src/drivers/MCPWMStepper.cpp` - ESP-IDF MCPWM implementation

**Files Modified:**
- `src/drivers/TMC2209Driver.h` / `.cpp` - Integrated MCPWM
- `src/drivers/TMC2208Driver.h` / `.cpp` - Integrated MCPWM
- `src/core/MotorController.cpp` - Added `pwmautoscale` command

**Key Implementation Details:**
- MCPWM Unit 0, Timer 0, Generator A
- 50% duty cycle fixed
- Frequency range: 10 Hz - 100 kHz (practical limit ~50 kHz)
- Time-based position estimation in `update()` loop
- Rate-limited profile updates (every 5ms)

**Removed:**
- Fullstep (1/1 microstepping) support - now minimum 1/2
- `doStep()` method (software stepping)
- `calculateStepInterval()` method (timing calculation)

---

## 🐛 Current Issue: Speed Limitation

### Symptoms

**Observed Behavior:**
- ✅ MCPWM successfully generates pulses
- ✅ Motor spins at low/medium speeds
- ❌ **Maximum ~14,000 steps/sec before skipping**
- ❌ Motor audibly "buzzes" faster but doesn't follow
- ❌ Skips steps on the way up, catches up on the way down (~7k steps/sec)
- ⚠️ Current draw only 100-200mA (expected 800-1500mA)

**Target:**
- 53,333 steps/sec (1000 RPM at 1/16 microstepping)
- 200 steps/rev × 16 microsteps × 16.67 RPS = 53,333 steps/sec

**Configuration Tested:**
```
Motor: NEMA 17 (200 steps/rev, rated for high-speed operation)
Driver: TMC2209 via UART
Voltage: 24V
Current Limit: 800mA (tried up to 1500mA - motor rated for 1.5A RMS)
Microstepping: 1/2, 1/4, 1/8, 1/16 (all tested)
Acceleration: 1000, 10000, 50000 steps/s² (all tested)
Profile: Constant, Trapezoidal (both tested)
Mode: StealthChop (default)
```

### Root Cause Analysis

**Acceleration Issue (RESOLVED):**
- Initial tests used `accel 1000` with `speed 53333`
- Time to reach full speed = 53333 ÷ 1000 = **53.3 seconds**
- Motor tried to ramp slowly but couldn't maintain higher speeds during long ramp
- **Fix:** Changed to `set profile constant` or `set accel 50000` → No improvement

**Current Draw Issue (LOW PRIORITY):**
- PWM autoscale is enabled by default → driver reduces current under light load
- At 100-200mA (vs 800mA limit), motor has minimal torque
- **Fix:** Added `pwmautoscale off` command to force full current → **Testing pending**

**Suspected Issues:**

1. **Motor Physical Limitation:**
   - Even at rated 1.5A, motor can't follow 14k+ steps/sec
   - Possible resonance at this frequency
   - Motor spec may be optimistic or requires different configuration

2. **MCPWM Configuration:**
   - Frequency updates every 5ms might still be too aggressive
   - MCPWM reinitialization during frequency changes could cause jitter
   - Direction changes with `delayMicroseconds(1)` may be insufficient

3. **Driver Settings:**
   - StealthChop efficiency might be limiting torque at high speeds
   - TOFF, blanking time, or chopper settings may need tuning
   - StallGuard interference (disabled now but still registered)

4. **Electrical Issues:**
   - 24V may be insufficient for high-speed operation (though should be adequate)
   - Wiring inductance/capacitance affecting signal integrity
   - Step pulse duty cycle (50%) might not be optimal

---

## 🔧 Troubleshooting Steps

### 1. Test PWM Autoscale (NEW - Added Jan 7, 2025)

**Command:** `pwmautoscale off`

**What it does:**
- Disables automatic current reduction
- Forces driver to output full configured current at all times
- Motor will draw full 800-1500mA even when idle (will run hotter)

**Test procedure:**
```
> set current 1500      # Max motor rating
> pwmautoscale off      # Force full current
> set profile constant
> set speed 20000       # Start lower than limit
> move 100000
```

**Expected outcomes:**
- ✅ Higher current draw (closer to 800-1500mA)
- ✅ More torque available for acceleration
- ✅ Potentially higher achievable speeds
- ⚠️ Motor will be warmer

**Result:** _[TO BE TESTED]_

---

### 2. Try SpreadCycle Mode

**Command:** `spreadcycle`

**What it does:**
- Switches from StealthChop (silent, efficient) to SpreadCycle (loud, high-torque)
- SpreadCycle has more consistent torque output
- Works better at higher speeds and with lower microstepping

**Test procedure:**
```
> spreadcycle           # Switch mode
> set microsteps 4      # Lower microstepping for more torque
> set speed 20000       # 200 × 4 × 25 RPS = 20k steps/sec (should work)
> move 100000
```

**Expected outcomes:**
- ✅ Higher achievable speeds
- ✅ More consistent torque
- ❌ Louder operation
- ❌ More power consumption

**Result:** _[TO BE TESTED]_

---

### 3. Optimize MCPWM Update Rate

**Current implementation:**
```cpp
// update() method - updates every 5ms
static uint32_t lastProfileUpdate = 0;
if (now - lastProfileUpdate >= 5000) {  // 5ms = 200 Hz
    lastProfileUpdate = now;
    // ... update frequency
}
```

**Potential issue:** 200 Hz frequency updates may still cause jitter

**Test modification:**
1. Increase update interval to 10ms (100 Hz)
2. Increase to 20ms (50 Hz)
3. Try only updating frequency on significant speed changes (threshold-based)

**Code location:** `src/drivers/TMC2209Driver.cpp` line ~276

---

### 4. Check MCPWM Frequency Transitions

**Hypothesis:** Rapid frequency changes cause stepper driver to lose sync

**Test:** Add logging to see actual frequency changes
```cpp
void TMC2209Driver::updateHardwareFrequency() {
    float speed = _currentSpeed;
    if (speed < 10.0f) speed = 10.0f;
    if (speed > 50000.0f) speed = 50000.0f;
    
    // Log frequency changes
    static float lastFreq = 0;
    if (abs(speed - lastFreq) > 100) {  // Only log significant changes
        Serial.print("Freq: ");
        Serial.print(lastFreq);
        Serial.print(" → ");
        Serial.println(speed);
        lastFreq = speed;
    }
    
    _mcpwmStepper.setFrequency(speed);
    _lastFreqUpdate = micros();
}
```

---

### 5. Adjust Motor Driver Settings

**TOFF (Off time):**
- Current: `_driver->toff(5)`
- Try: Values 2-8 affect chopper frequency
- Lower = higher chopper frequency = faster current decay

**Blanking Time:**
- Try adjusting `TBL` setting (time-blanking)
- May help with high-speed operation

**Chopper Mode:**
- Try `_driver->chm(1)` for constant off-time mode
- May be more stable at high speeds

**Code location:** `src/drivers/TMC2209Driver.cpp` - `configureDriver()` method

---

### 6. Test with Constant Velocity (No Ramping)

**Command sequence:**
```
> set profile constant
> set speed 5000        # Start low
> move 50000
> set speed 10000       # Increase gradually
> move 50000
> set speed 15000       # Keep increasing
> move 50000
```

**Find the exact speed where it starts skipping**

---

### 7. Reduce Microstepping

**Theory:** Lower microstepping = more torque per step

**Test:**
```
> set microsteps 2      # Minimum allowed (fullstep removed)
> set speed 6667        # 200 × 2 × 16.67 = 6,667 steps/sec for 1000 RPM
> move 50000
```

**Expected:** Should work reliably, establishes baseline

---

### 8. Check for Resonance

**Stepper motors have resonant frequencies around 100-300 Hz**

**Calculate motor RPM at 14k steps/sec:**
```
14000 steps/sec ÷ (200 steps/rev × 2 microsteps) = 35 RPS = 2100 RPM
14000 steps/sec ÷ (200 steps/rev × 16 microsteps) = 4.375 RPS = 262.5 RPM
```

**At 1/16:** 262.5 RPM = 4.375 Hz (too low for resonance)  
**At 1/2:** 2100 RPM = 35 Hz (still below typical resonance)

**Conclusion:** Unlikely to be resonance, but try microstepping variations

---

### 9. Hardware Checks

**Power Supply:**
- Verify stable 24V under load
- Check for voltage drops during high-speed operation
- Try higher voltage (28V) if available

**Wiring:**
- Shorten step/dir signal wires if possible
- Add 100-200Ω series resistor on STEP pin (reduce ringing)
- Add 10-100pF capacitor to ground on STEP pin (reduce EMI)

**Motor Connections:**
- Verify coil wiring (A+, A-, B+, B-)
- Check for loose connections
- Measure coil resistance (should be ~2-4Ω per coil)

---

## 📊 Test Matrix

| Test | Config | Speed Target | Expected Result | Actual Result | Notes |
|------|--------|--------------|-----------------|---------------|-------|
| 1 | Default (StealthChop, 1/16, pwmautoscale on) | 53333 | ❌ Fail | ❌ 14k limit | Baseline |
| 2 | **pwmautoscale off**, 1/16 | 53333 | ✅ Pass? | ⏳ Pending | Should increase torque |
| 3 | SpreadCycle, 1/16 | 53333 | ✅ Pass? | ⏳ Pending | More torque |
| 4 | SpreadCycle, 1/4 | 13333 | ✅ Pass | ⏳ Pending | Lower target |
| 5 | Constant profile, 1/2 | 6667 | ✅ Pass | ⏳ Pending | Minimum viable |
| 6 | TOFF=3, 1/16 | 53333 | ✅ Pass? | ⏳ Pending | Faster decay |
| 7 | 10ms update rate | 53333 | ✅ Pass? | ⏳ Pending | Less jitter |

---

## 🔍 Diagnostic Commands

**Check current configuration:**
```
> r                     # Full diagnostics
> status                # Quick status
```

**Test sequence:**
```
> set current 1500      # Max current
> pwmautoscale off      # Full current always
> spreadcycle           # High-torque mode
> set microsteps 4      # Reduce microstepping
> set profile constant  # No acceleration
> set speed 13333       # 1000 RPM at 1/4 step
> move 50000            # Long move to observe
```

**Monitor during operation:**
- Watch current draw with ammeter
- Listen for resonance/skipping sounds
- Observe LED patterns (if diagnostic LEDs available)

---

## 📝 Implementation Notes

### MCPWM Configuration Details

```cpp
// MCPWMStepper.cpp - init() method
mcpwm_config_t pwm_config;
pwm_config.frequency = (uint32_t)_currentFrequency;
pwm_config.cmpr_a = 50.0f;          // 50% duty cycle
pwm_config.cmpr_b = 0.0f;           // Unused
pwm_config.duty_mode = MCPWM_DUTY_MODE_0;  // Active high
pwm_config.counter_mode = MCPWM_UP_COUNTER;

mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
```

**Frequency Clamping:**
- Minimum: 10 Hz (MCPWM hardware limit)
- Maximum: 50,000 Hz (safety limit, can go to 100kHz theoretically)

**Direction Control:**
- Direct GPIO write with 1µs delay
- May need to increase delay for high-speed direction changes

---

### Position Tracking

**Time-Based Estimation:**
```cpp
float elapsedSec = (now - _lastStepTime) / 1000000.0f;
int32_t estimatedSteps = (int32_t)(_currentSpeed * elapsedSec);
_position += _moveDirection * estimatedSteps;
```

**Potential Issue:** If update rate is inconsistent, position tracking drifts

**Solution:** Could add step count feedback from driver if available

---

## 🚀 Next Steps

### Immediate Actions (Priority Order)

1. ✅ **Test `pwmautoscale off`** - Uploaded, ready to test
2. ⏳ **Test SpreadCycle mode** - Should provide more consistent torque
3. ⏳ **Try lower microstepping (1/4, 1/2)** - Reduce target speed proportionally
4. ⏳ **Adjust TOFF setting** - May help with high-speed response

### If Still Limited After Above

5. ⏳ **Increase MCPWM update interval to 10-20ms** - Reduce frequency change rate
6. ⏳ **Add threshold-based frequency updates** - Only update on significant speed changes
7. ⏳ **Test with different motor** - Verify if motor is the limitation
8. ⏳ **Try external driver (A4988/DRV8825)** - Compare with TMC2209

### Long-Term Solutions

- **Step/Dir pulse optimization** - Adjust duty cycle, add delays
- **Closed-loop control** - Add encoder for feedback
- **Different motor** - Higher voltage rating (48V capable)
- **Alternative architecture** - Consider RMT peripheral instead of MCPWM
- **Field-oriented control (FOC)** - For true high-speed operation

---

## 📚 Reference Information

### Motor Specifications
- Type: NEMA 17 Stepper Motor
- Steps/Rev: 200 (1.8° per step)
- Rated Current: 1.5A RMS per phase
- Voltage Rating: Typically 3-4V (but can run at 12-48V with chopper)
- Inductance: ~2-4 mH per phase
- Resistance: ~2-4Ω per phase

### TMC2209 Specifications
- Max Current: 2.8A RMS, 3.5A peak
- Voltage Range: 4.75-29V
- Microstepping: 1/2 to 1/256 (fullstep removed in our implementation)
- Modes: StealthChop2, SpreadCycle
- UART Control: Yes (115200 baud)

### ESP32-S3 MCPWM Specifications
- Units: 2 (MCPWM0, MCPWM1)
- Timers per unit: 3
- Operators per timer: 2
- Frequency range: DC to ~40 MHz (practical limit ~1 MHz for accurate PWM)
- Resolution: Configurable

### Useful Calculations

**Steps per second for target RPM:**
```
steps/sec = (RPM ÷ 60) × steps_per_rev × microsteps
```

**Example: 1000 RPM at 1/16 microstepping:**
```
steps/sec = (1000 ÷ 60) × 200 × 16 = 53,333 steps/sec
```

**Current acceleration time:**
```
time = target_speed ÷ acceleration
time = 53333 ÷ 50000 = 1.07 seconds (with accel=50000)
```

---

## 🆘 Contact & Support

**Documentation:**
- Main README: `README.md`
- Command Protocol: `docs/command-protocol.md`
- TMC2209 Guide: `docs/tmc2209-guide.md`
- Code Architecture: `docs/code-architecture.md`

**Key Files to Review:**
- MCPWM Implementation: `src/drivers/MCPWMStepper.cpp`
- Driver Integration: `src/drivers/TMC2209Driver.cpp` (lines 258-320)
- Command Handler: `src/core/MotorController.cpp` (line ~270)

**Helpful Commands:**
```bash
# Build only
pio run -e esp32-s3-mini

# Upload only (if already built)
pio run -e esp32-s3-mini -t upload

# Build + Upload
pio run -e esp32-s3-mini -t upload

# Serial monitor
pio device monitor
```

---

## 📈 Update Log

**January 7, 2025:**
- ✅ Implemented MCPWM hardware PWM stepping
- ✅ Removed fullstep support (minimum 1/2 microstepping)
- ✅ Added rate-limited frequency updates (5ms)
- ✅ Added `pwmautoscale on/off` command
- ⚠️ Current limitation: ~14,000 steps/sec max before skipping
- ⏳ Testing `pwmautoscale off` for full current
- ⏳ Will test SpreadCycle mode next

---

**End of Document**
