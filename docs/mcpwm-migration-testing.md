# MCPWM Migration - Quick Testing Checklist

**Date**: January 13, 2026  
**Firmware**: UniversalMotorModule v2.0 (MCPWM New API)

---

## ⚡ Quick Start

### 1. Upload Firmware
```bash
platformio run --target upload
platformio device monitor
```

### 2. Basic Sanity Check
Send these commands via serial:
```
S 1000 500      # Set speed 1kHz, accel 500
M 10000         # Move 10k steps
Q               # Query status
X               # Stop
```

Expected: Motor moves, stops on command, no crashes

---

## 🔬 Oscilloscope Tests

### Test 1: Pulse Gap Fix (CRITICAL)
**Setup:**
- Oscilloscope probe on STEP pin (GPIO 1)
- Trigger: Rising edge, single capture
- Timebase: 10ms/div

**Commands:**
```
S 1000 500      # Low speed: 1kHz, 500 accel
M 100000        # Long move to see full acceleration
```

**What to Look For:**
- ❌ **BAD**: Gaps in pulse train during acceleration (old behavior)
- ✅ **GOOD**: Continuous uninterrupted pulses from 0 to 1kHz

**Acceptance Criteria:**
- Zero pulse gaps
- Zero extended pulses
- Smooth continuous waveform

---

### Test 2: 50kHz Cap Removed
**Setup:**
- Oscilloscope probe on STEP pin
- Trigger: Normal, continuous
- Timebase: 50μs/div

**Commands:**
```
S 100000 10000  # 100kHz target
M 10000
```

**What to Look For:**
- Frequency reaches 100kHz (10μs period)
- Clean square wave
- 50% duty cycle (5μs HIGH, 5μs LOW)

**Measurements:**
- [ ] Frequency: 100kHz ± 1%
- [ ] Duty cycle: 50% ± 2%
- [ ] No distortion

**Bonus Test (if motor can handle it):**
```
S 500000 50000  # 500kHz target
```
- Should see 2μs period (1MHz hardware limit)

---

### Test 3: Position Tracking Accuracy
**Setup:**
- Oscilloscope on STEP pin, count pulses
- Serial monitor open to see position reports

**Commands:**
```
P 0             # Reset position to 0
S 10000 5000    # 10kHz, 5k accel
M 100000        # Move exactly 100k steps
Q               # Query status to see position
```

**What to Look For:**
- Oscilloscope pulse count: Should be exactly 100,000
- Serial position report: Should be exactly 100,000
- No drift between hardware counter and actual pulses

**Acceptance Criteria:**
- Hardware counter matches oscilloscope within ±1 pulse
- No runaway behavior
- Position stable after move completes

---

## 🎯 Motion Quality Tests

### Test 4: Smooth Acceleration
**Setup:**
- Watch motor physically
- Listen for smooth sound

**Commands:**
```
S 1000 500      # Low speed
M 50000

S 10000 5000    # Medium speed
M 50000

S 50000 10000   # High speed
M 50000
```

**What to Look For:**
- ✅ Smooth acceleration (no stuttering)
- ✅ Smooth deceleration
- ✅ No visible "step skipping"
- ✅ Quiet operation (no grinding sounds)

---

### Test 5: High-Speed Operation
**Setup:**
- Oscilloscope on STEP pin
- Serial monitor for errors

**Commands:**
```
S 50000 20000   # 50kHz
M 100000
```

**What to Look For:**
- Motor reaches target speed
- No position tracking errors
- No crashes or resets
- Waveform remains clean at high speed

---

## 📋 Results Template

Copy this to your testing notes:

```
=== MCPWM Migration Test Results ===
Date: __________
Firmware Version: __________
Board: ESP32-S3 Super Mini

[ ] Test 1: Pulse Gap Fix
    - Pulse gaps observed: YES / NO
    - Oscilloscope screenshot attached: ___________
    - Notes: _________________________________

[ ] Test 2: 50kHz Cap Removed
    - Max frequency achieved: ________ Hz
    - Waveform quality: GOOD / DISTORTED
    - Duty cycle measured: ________ %
    - Notes: _________________________________

[ ] Test 3: Position Tracking
    - Target steps: 100000
    - Oscilloscope count: __________
    - Hardware counter: __________
    - Difference: __________ (should be 0)
    - Notes: _________________________________

[ ] Test 4: Motion Quality
    - Smooth acceleration: YES / NO
    - Smooth deceleration: YES / NO
    - Audible issues: YES / NO
    - Visual stuttering: YES / NO
    - Notes: _________________________________

[ ] Test 5: High-Speed Operation
    - Max speed tested: ________ Hz
    - Operation stable: YES / NO
    - Position tracking working: YES / NO
    - Notes: _________________________________

Overall Result: PASS / FAIL / PARTIAL
Next Steps: _________________________________
```

---

## 🚨 Troubleshooting

### Issue: Motor doesn't move
**Check:**
1. Is ENABLE pin working? (should be LOW when enabled)
2. Are DIR and STEP pins connected correctly?
3. Is power supply providing 12V?
4. Check serial output for initialization errors

### Issue: Pulse gaps still present
**Check:**
1. Verify firmware uploaded correctly
2. Check serial output for "NEW API" message during init
3. Verify oscilloscope trigger settings (not skipping pulses)
4. Try different speeds to isolate

### Issue: Position counter not incrementing
**Check:**
1. Look for "position tracking: ENABLED" in serial output
2. If "DISABLED", capture channel failed to initialize
3. Check GPIO conflicts
4. Verify capture ISR is being called (add debug print)

### Issue: Frequency cap still at 50kHz
**Check:**
1. Verify MAX_FREQUENCY = 1000000 in header
2. Check TIMER_RESOLUTION_HZ = 1000000
3. Verify period calculation: `period = 1000000 / freq`
4. Check for clamping in setFrequency()

---

## ✅ Success Criteria Summary

| Test | Metric | Target | Critical? |
|------|--------|--------|-----------|
| Pulse Gaps | Gap count | 0 | YES |
| Max Frequency | Frequency reached | ≥100kHz | YES |
| Position Accuracy | Counter error | ±1 pulse | YES |
| Motion Quality | Smooth operation | No stuttering | YES |
| High-Speed Stability | Crashes | 0 | YES |

**All 5 tests must PASS to proceed to Phase 2**

---

## 📸 Documentation

For each test, capture:
1. Oscilloscope screenshot (PNG format)
2. Serial output log (copy/paste to .txt)
3. Video of motor motion (if quality issues visible)

Save to: `docs/testing_results/mcpwm_migration_YYYYMMDD/`

---

**Ready to Test!** 🚀
