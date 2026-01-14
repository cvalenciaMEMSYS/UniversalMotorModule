# Universal Motor Module - Testing Results & Known Issues

**Last Updated**: January 2026  
**Hardware**: ESP32-S3 Super Mini + TMC2209 v1.3 + NEMA 17 Stepper Motor  
**Firmware Version**: v2.0 (FastAccelStepper)  
**Test Configuration**:
- Power Supply: 12V
- Motor: Small NEMA 17 stepper motor
- PCB: Custom verified PCB (no loose wiring)
- Motor phases: Properly connected to output
- Microstepping: 1/16 (3200 steps/revolution)

---

## ✅ Issues FIXED by FastAccelStepper Migration

The following issues were fixed by migrating from custom MCPWM code to FastAccelStepper library:

### 1. Step Skipping ✅ FIXED

**Previous**: Motor skipped steps across all acceleration profiles
**Root Cause**: Pulse gaps during MCPWM timer updates
**Solution**: FastAccelStepper uses synchronous timer updates
**Status**: ✅ Verified with oscilloscope - no pulse gaps

### 2. Low Speed Timing Issues ✅ FIXED

**Previous**: Extra steps generated at speeds below 1000 steps/s
**Root Cause**: Frequency calculation rounding errors in custom code
**Solution**: FastAccelStepper uses hardware-based pulse generation
**Status**: ✅ Fixed - accurate step counts at all speeds

### 3. Position Overshoot ✅ FIXED

**Previous**: Triangular profile overshooting end position
**Root Cause**: Acceleration math errors in custom implementation
**Solution**: FastAccelStepper's tested acceleration algorithms
**Status**: ✅ Fixed - accurate position tracking

### 9. 50kHz Frequency Cap ✅ FIXED

**Previous**: Maximum step rate limited to 50kHz
**Root Cause**: MCPWM prescaler configuration
**Solution**: FastAccelStepper achieves 200kHz+
**Status**: ✅ Verified - high-speed operation confirmed

### 10. Position Tracking Runaway ✅ FIXED

**Previous**: Position counter drift at high speeds
**Root Cause**: Time-based position calculation
**Solution**: FastAccelStepper uses hardware pulse counter
**Status**: ✅ Fixed - hardware-based position tracking

### 11. Pulse Gaps During Acceleration ✅ FIXED

**Previous**: 4+ gaps visible on oscilloscope during speed changes
**Root Cause**: Timer updated mid-cycle
**Solution**: FastAccelStepper synchronous updates
**Status**: ✅ Verified - clean continuous pulse trains

---

## ⚠️ Remaining Issues

### 4. Hold Current Configuration

**Severity**: MEDIUM (Now configurable)  
**Status**: Fixed with `set ihold` command

**Previous Issue**: Holding torque remained weak regardless of current setting
**Solution**: Added `set ihold <0-100>` command to configure IHOLD register
**Usage**:
```
set ihold 50   # 50% of run current for holding
set ihold 100  # Full run current for holding
```

---

## Command Handling Issues

### 5. Integer Overflow in MOVE Command

**Severity**: LOW  
**Status**: Known limitation

**Input**: Very large values like `move 999999999999`
**Behavior**: Value wraps due to 32-bit overflow
**Recommendation**: Use reasonable step values (within int32 range)

---

### MCPWM Configuration Problems
- Frequency calculation rounding errors at low speeds
- Peripheral not optimally configured for dynamic speed changes
- Check [MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp) frequency calculations

### Acceleration Profile Math Errors
- Position overshoot indicates calculation bugs in acceleration/deceleration
- Check [AccelerationProfile.h](../src/core/AccelerationProfile.h)
- Check [MotionMath.h](../src/core/MotionMath.h)
- Timing integration may accumulate errors

### Step Pulse Timing Violations
- TMC2209 requirements:
  - Minimum step pulse width: 100ns
  - Minimum DIR setup time: 100ns before STEP
- At high speeds, pulses may be too short
- At low speeds, accumulated timing drift

### TMC2209 Configuration Issues
- TPOWERDOWN not set correctly (affects holding current)
- IHOLD vs IRUN current settings
- StealthChop mode affecting holding torque
- CoolStep or other features interfering

---

## Recommended Investigation Steps

### Immediate Oscilloscope Analysis
1. **STEP pulse width and consistency** at different speeds (especially <1000 steps/s)
2. **DIR signal setup/hold time** relative to STEP pulses
3. **Pulse spacing regularity** during acceleration/deceleration phases
4. **Frequency accuracy** at low speeds - measure actual vs commanded
5. **Glitches or spurious pulses** during velocity profile transitions

### Code Review Priorities
1. MCPWM frequency calculation (especially at low frequencies)
2. Acceleration profile mathematics (triangular profile overshoot)
3. Timer interrupt timing and jitter
4. TMC2209 register configuration (IHOLD, TPOWERDOWN, etc.)

---

## Test Environment Notes

- All tests performed on verified custom PCB with proper connections
- No wiring issues - all components mounted on PCB
- Motor phases verified correct
- Power supply stable at 12V
- No thermal issues observed during testing

---

## Summary

**Total Issues**: 6 (4 Critical, 2 Medium)  
**Working Correctly**: 5 command validation items  
**Not Implemented**: 1 feature (StallGuard)

The most critical issues revolve around **timing accuracy** and **step generation consistency** at different speeds, particularly:
1. Step skipping across all profiles
2. Extra steps at speeds <1000 steps/s  
3. Position overshoot in short moves
4. Weak holding torque despite current settings

Further investigation with oscilloscope is recommended to identify root cause of timing-related issues before code modifications.

---

## Additional Issues Found - Oscilloscope & Serial Testing

### 7. Serial Connection Reset Issue

**Severity**: HIGH  
**Affects**: Serial communication/debugging

#### 7.1 ESP Becomes Unresponsive After Serial Reconnection
- **Symptom**: 
  - Close serial terminal connection
  - Attempt to reconnect
  - ESP no longer responds to any commands
- **Workaround**: Press physical reset button on ESP
- **Impact**: 
  - Not practical for deployed systems in hard-to-reach locations
  - Breaks remote operation/monitoring
- **Root Cause Analysis**:
  - Likely serial buffer not being flushed/reset on disconnect
  - Serial.begin() may need to be called again on DTR/RTS signal change
  - USB CDC (Communications Device Class) state not properly handled
- **Recommendation**: 
  - Implement DTR/RTS signal detection
  - Send startup menu/banner on any new connection (not just boot)
  - Consider watchdog timer for serial communication timeout

---

### 8. LED Status Management Issues

**Severity**: MEDIUM  
**Affects**: StatusLED.cpp feedback system

#### 8.1 LED Status Overwritten and Not Updated
- **Symptom**: 
  - Motor moving and LED blinking to indicate motion ✓
  - Request device status via command
  - Status info prints correctly ✓
  - LED **stops blinking** even though motor still turning ✗
  - In some cases, LED turns off completely until idle mode resumes
- **Impact**: User loses visual feedback of system state during operations
- **Root Cause Analysis**:
  - Status request command likely calls LED update without checking current motion state
  - LED state machine not properly preserving "background" activity states
  - Missing priority/layering system for LED status (motion > status request > idle)
- **Code Location**: [StatusLED.cpp](../src/core/StatusLED.cpp)
- **Recommendation**:
  - Implement LED state priority queue
  - Status requests should be temporary overlay, not state replacement
  - Background activities (motion) should resume LED indication after status display

---

### 9. MCPWM Frequency Limit - 50kHz Cap

**Severity**: CRITICAL  
**Affects**: High-speed motion capability

#### 9.1 Frequency Capped at 50kHz Despite Higher Capability
- **Test Method**: Oscilloscope measurement
- **Hardware Capability**: MCPWM module can theoretically run up to 8MHz (1MHz reasonable target)
- **Observed**: Current configuration **capped at 50kHz**
- **Oscilloscope Findings**:
  - At 50kHz: Waveform looks very good (clean edges, proper duty cycle)
  - Attempting speeds >50kHz: Output remains flat at 50kHz
  - **Not a hardware limit** - signal quality is excellent
- **Impact**: Severely limits maximum motor speed capability
- **Root Cause Analysis**:
  - Likely MCPWM prescaler or timer configuration incorrect
  - Possible issues:
    - Timer resolution set too low
    - Prescaler value limiting max frequency
    - Incorrect clock source (APB_CLK vs other)
    - Period register reaching minimum value
- **Code Location**: [MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp)
- **Recommendation**: 
  - Review MCPWM timer configuration
  - Check prescaler settings
  - Verify clock source selection (should use fastest available)
  - Test with different timer resolution settings

---

### 10. Runaway Motor at 50kHz

**Severity**: CRITICAL  
**Affects**: Position control at high speeds

#### 10.1 Continuous Pulse Generation and Position Counter Overflow
- **Test Conditions**: Speed set to 50kHz (50,000 steps/s) or higher
- **Observed Behavior**:
  1. **Frequency cap**: Pulse output frequency capped at 50kHz (as described in issue #9)
  2. **Runaway motion**: Motor attempts to turn non-stop
  3. **ESP does not stop** sending pulses after target reached
  4. **Position counter goes crazy**:
     - Example: Position was 6400 after successful low-speed move
     - Set speed to 50,000 steps/s
     - In <1 second: position jumped from 6400 to ~68,000
  5. **Worse at higher commanded speeds**:
     - Set speed to 100,000 steps/s (even though capped at 50kHz output)
     - In <0.5 seconds: position increases by ~100,000
     - Way above target number of steps
- **Impact**: Complete loss of position control at high speeds
- **Root Cause Analysis**:
  - Step counter likely incrementing based on **commanded** frequency, not **actual** output
  - Missing feedback between MCPWM timer and position tracking
  - Position updates may be timer-based rather than pulse-based
  - Possible interrupt overflow or counter wraparound
  - Step counting logic not synchronized with actual MCPWM pulses
- **Code Locations**: 
  - [MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp) - pulse generation
  - [MotorController.cpp](../src/core/MotorController.cpp) - position tracking
- **Recommendation**:
  - Position counter MUST be synchronized with actual MCPWM pulses
  - Use MCPWM capture/interrupt on actual step edges
  - Add safety limit: stop if position error exceeds threshold
  - Implement velocity feedback/verification

---

### 11. Pulse Output Gaps During Acceleration

**Severity**: CRITICAL  
**Affects**: Motion smoothness and step loss

#### 11.1 Dead Bands in Acceleration Phase
- **Test Method**: Oscilloscope observation during motion profiles
- **Test Setup**: Motor disconnected (too fast for motor to follow)
- **Motion Profiles Tested**: Linear and Trapezoidal (S-curve TBD)
- **Observed**: Profiles **do work** - clear acceleration, cruise, deceleration phases visible ✓
- **Problem**: Acceleration phase has visible gaps where **no pulses are sent**
  
#### 11.2 Specific Test Results

**Test 1 - Low Speed**
- Speed: 1000 steps/s
- Acceleration: 500 steps/s²
- Result: **4 distinct dead bands** during acceleration
- Location: Very specific positions (not random) - repeatable
- Deceleration: Smooth, no gaps ✓
- Cruise: Smooth, no gaps ✓

**Test 2 - Medium Speed**
- Speed: Increased (exact value TBD)
- Result: Dead bands reduced to **3**
- Location: Close to start of acceleration sequence
- Deceleration: Smooth, no gaps ✓

**Test 3 - Higher Speed**
- Speed: Further increased
- Result: Dead bands persist (did not eliminate)
- Count/location: Still present despite higher speed

#### 11.3 Key Observations
- **Frequency during active periods**: Correct for the point in acceleration curve
- **Internal speed tracking**: Appears to work (frequency increases appropriately)
- **Pulse emission**: Intermittent complete stops during acceleration
- **Consistency**: Dead bands occur at same points in profile (deterministic, not random)
- **Phase-specific**: Only affects acceleration, NOT deceleration or cruise

#### 11.4 Root Cause Analysis
- **Most likely**: Timer update/reload issue during frequency changes
- Possible causes:
  1. **MCPWM period update conflict**: 
     - Updating period register while timer is running
     - Missing "load on zero" or "synchronous update" configuration
  2. **Acceleration step calculation**:
     - Discrete steps in velocity causing timer reload gaps
     - Missing interpolation during frequency transitions
  3. **ISR timing issues**:
     - Interrupt takes too long
     - Next period update missed
     - Results in "skip" until next update cycle
  4. **Buffer underrun**:
     - Velocity profile buffer not keeping up with updates
     - Results in momentary stops until next value calculated
  5. **Integer math rounding**:
     - Frequency calculation at certain velocities produces invalid period
     - System waits for valid value

#### 11.5 Why Deceleration Works
- Deceleration is smooth with no gaps
- Suggests asymmetric handling or different code path for decel vs accel
- May indicate issue is in acceleration calculation specifically, not MCPWM config

#### 11.6 Impact
- Explains step loss observed in earlier tests (Issues #1.1, #1.2, #1.3)
- Steps are not just "skipped" - **pulses are not sent** during gaps
- Motor sees inconsistent step stream
- Position tracking still increments (explains position errors)

**Code Locations**:
- [AccelerationProfile.h](../src/core/AccelerationProfile.h) - profile calculations
- [MotionMath.h](../src/core/MotionMath.h) - velocity/position math
- [MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp) - frequency updates

**Recommendation**:
- Add oscilloscope trigger on STEP signal with persistence mode
- Capture exact timing of gaps
- Review MCPWM period update mechanism (must be synchronous)
- Check if using double-buffered timer updates
- Add frequency change rate limiting
- Consider pre-calculating entire velocity profile to buffer

---

## Updated Summary

**Total Issues**: 11 (8 Critical, 3 Medium)  
**Working Correctly**: 5 command validation items  
**Not Implemented**: 1 feature (StallGuard)

### Critical Issues Breakdown:
1. Step skipping across all profiles (now explained by pulse gaps)
2. Low-speed timing issues (<1000 steps/s)
3. Position overshoot in short moves
4. Serial connection reset requirement
5. **MCPWM frequency capped at 50kHz** (NEW)
6. **Runaway motor and position counter at high speeds** (NEW)
7. **Pulse output gaps during acceleration** (NEW - Root cause of step skipping)

### Medium Issues:
1. Current control not affecting holding torque
2. Integer overflow in MOVE command
3. **LED status management** (NEW)

### Root Cause Identified:
The oscilloscope testing has revealed that **issues #1.1, #1.2, #1.3 (step skipping)** are caused by **issue #11 (pulse gaps during acceleration)**. The motor is not skipping steps - the ESP is failing to send pulses during specific points in the acceleration phase.

---

## Critical Priority Items

Based on oscilloscope findings, the following issues should be addressed in priority order:

### Priority 1 - MCPWM Core Issues (Blocking all motion)
1. **Issue #11**: Pulse gaps during acceleration (root cause of step loss)
2. **Issue #9**: 50kHz frequency cap (blocks high-speed operation)
3. **Issue #10**: Position counter synchronization (safety critical)

### Priority 2 - Motion Control
1. **Issue #2**: Low-speed timing errors (<1000 steps/s)
2. **Issue #3**: Position overshoot in short moves

### Priority 3 - Operational Issues
1. **Issue #7**: Serial reconnection reset
2. **Issue #8**: LED status management
3. **Issue #4**: Holding torque configuration

### Priority 4 - Input Validation
1. **Issue #5**: Integer overflow handling

---

## Detailed Plan of Attack

### Phase 1: MCPWM Core Fixes (CRITICAL - Must Fix First)

These issues are blocking ALL reliable motion and must be resolved before any other work.

#### Task 1.1: Fix Pulse Gaps During Acceleration (Issue #11)
**Priority**: CRITICAL  
**Files to Modify**: 
- [src/drivers/MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp)
- [src/core/AccelerationProfile.h](../src/core/AccelerationProfile.h)
- [src/core/MotionMath.h](../src/core/MotionMath.h)

**Investigation Steps**:
1. **Review MCPWM Timer Update Mechanism**
   - Check how period register is updated during motion
   - Verify if updates are synchronous (load on zero/period match)
   - Look for missing MCPWM_TIMER_UPDATE_CONF configuration
   - Confirm double-buffering is enabled

2. **Examine Velocity Update Timing**
   - Review when `setSpeed()` or frequency updates are called
   - Check if updates happen from ISR or main loop
   - Verify update rate vs. motion control loop rate
   - Look for race conditions during register updates

3. **Analyze Acceleration Calculation**
   - Review how acceleration profile calculates velocity steps
   - Check for integer rounding errors at specific velocities
   - Verify frequency-to-period conversion doesn't produce invalid values
   - Look for discrete "jumps" in velocity that cause gaps

4. **Oscilloscope Validation**
   - Set up trigger to capture gap events
   - Measure exact duration of gaps
   - Correlate gap timing with calculated velocity values
   - Compare acceleration vs deceleration code paths

**Implementation Plan**:
- [ ] Enable MCPWM synchronous updates (SYNC_EN bit)
- [ ] Configure timer reload on TEZ (timer equals zero) event
- [ ] Implement velocity update rate limiting if needed
- [ ] Add period validation before writing to register
- [ ] Test with oscilloscope to verify gap elimination

**Success Criteria**:
- Zero pulse gaps during acceleration phase
- Smooth continuous pulse train from start to target speed
- Deceleration remains smooth (should not regress)
- Works across speed range (100 to 50,000 steps/s)

---

#### Task 1.2: Remove 50kHz Frequency Cap (Issue #9)
**Priority**: CRITICAL  
**Files to Modify**: 
- [src/drivers/MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp)

**Investigation Steps**:
1. **Review MCPWM Clock Configuration**
   ```cpp
   // Check current settings:
   - MCPWM clock source (APB_CLK = 80MHz typical)
   - Prescaler value
   - Timer resolution
   - Period calculation formula
   ```

2. **Calculate Current Limitations**
   - If prescaler = 8, clock = 80MHz: Timer clock = 10MHz
   - If minimum period = 200: Max freq = 10MHz / 200 = 50kHz ← FOUND IT
   - Need to reduce minimum period or increase timer clock

3. **Check Period Register Constraints**
   - Verify minimum period value being used
   - Check for hardcoded limits
   - Review period calculation: `period = (timer_clock / target_freq) - 1`

**Implementation Plan**:
- [ ] Identify clock source and prescaler configuration
- [ ] Calculate required prescaler for 1MHz target:
  - Timer clock needed: 2MHz+ (for 50% duty at 1MHz)
  - Prescaler = 80MHz / 2MHz = 40 (or less for higher speeds)
- [ ] Modify MCPWM initialization:
  ```cpp
  mcpwm_config_t pwm_config = {
      .frequency = 1000000,  // 1MHz target
      .cmpr_a = 50.0,        // 50% duty cycle
      .counter_mode = MCPWM_UP_COUNTER,
      .duty_mode = MCPWM_DUTY_MODE_0,
  };
  ```
- [ ] Update prescaler calculation dynamically based on target speed
- [ ] Add frequency range validation
- [ ] Test frequency output with oscilloscope

**Success Criteria**:
- Able to generate pulses at 1MHz (1,000,000 steps/s)
- Clean waveform (50% duty cycle, sharp edges)
- Stable operation across full range (10 Hz to 1 MHz)
- No distortion or jitter at high frequencies

---

#### Task 1.3: Fix Position Counter Synchronization (Issue #10)
**Priority**: CRITICAL (Safety)  
**Files to Modify**: 
- [src/drivers/MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp)
- [src/core/MotorController.cpp](../src/core/MotorController.cpp)

**Investigation Steps**:
1. **Identify Position Tracking Method**
   - Check if position is calculated from time + velocity
   - Check if position is incremented on timer interrupts
   - Determine if actual MCPWM pulses are counted

2. **Review Current Position Update Logic**
   ```cpp
   // Look for code like:
   currentPosition += (speed * deltaTime);  // BAD - not synchronized
   // vs
   currentPosition = mcpwm_pulse_count;     // GOOD - actual pulses
   ```

3. **Check MCPWM Capture/Count Features**
   - ESP32 MCPWM has capture units that can count pulses
   - Review ESP-IDF documentation for MCPWM capture mode
   - Determine if already implemented or needs addition

**Implementation Plan**:
- [ ] **Option A - Use MCPWM Capture Unit** (Recommended)
  - Configure MCPWM capture to count step pulses
  - Read capture counter value as position
  - Zero overhead, hardware-synchronized
  
  ```cpp
  // Pseudo-code
  mcpwm_capture_config_t cap_conf = {
      .cap_edge = MCPWM_POS_EDGE,
      .cap_prescale = 1,
      .capture_cb = NULL,  // Polling mode
  };
  mcpwm_capture_enable(MCPWM_UNIT_0, MCPWM_SELECT_CAP0, &cap_conf);
  
  int32_t getCurrentPosition() {
      return mcpwm_capture_signal_get_value(MCPWM_UNIT_0, MCPWM_SELECT_CAP0);
  }
  ```

- [ ] **Option B - Use Timer Interrupt** (Fallback)
  - Attach ISR to MCPWM timer overflow interrupt
  - Increment position counter in ISR
  - Ensure ISR is fast enough (<1µs at 1MHz)

- [ ] **Option C - External Counter** (Last Resort)
  - Use separate hardware counter peripheral
  - Feed STEP signal to counter input
  - Read counter value as position

- [ ] Add position error detection:
  ```cpp
  int32_t target = targetPosition;
  int32_t actual = getCurrentPosition();
  int32_t error = abs(target - actual);
  
  if (error > MAX_POSITION_ERROR) {
      // STOP! Something is very wrong
      emergencyStop();
      reportError(ERROR_POSITION_LOST);
  }
  ```

- [ ] Implement safety limits:
  - Max position error threshold (e.g., 100 steps)
  - Velocity sanity check (actual vs commanded)
  - Timeout detection (position not changing)

**Success Criteria**:
- Position counter matches actual pulses sent (±1 step)
- No position drift over time
- No runaway motion at high speeds
- Position error detection triggers within 100ms
- System safely stops on position tracking failure

---

### Phase 2: Motion Control Refinement

Once MCPWM core is stable and reliable, address motion quality issues.

#### Task 2.1: Fix Low-Speed Timing Errors (Issue #2)
**Priority**: HIGH  
**Files to Modify**: 
- [src/drivers/MCPWMStepper.cpp](../src/drivers/MCPWMStepper.cpp)
- [src/core/MotionMath.h](../src/core/MotionMath.h)

**Investigation Steps**:
1. **Analyze Frequency Calculation at Low Speeds**
   - Test specific case: 3200 steps commanded at 500 steps/s
   - Calculate expected time: 3200 / 500 = 6.4 seconds
   - Measure actual time with oscilloscope
   - Count actual pulses generated
   
2. **Check for Rounding Errors**
   ```cpp
   // Example issue:
   float period = timer_clock / target_freq;  // May have rounding error
   uint32_t period_int = (uint32_t)period;    // Truncation
   // Actual freq = timer_clock / period_int (slightly different)
   ```

3. **Review Integration Method**
   - How is step count accumulated over time?
   - Is there a fractional step buffer?
   - Check for truncation errors that accumulate

**Implementation Plan**:
- [ ] Implement high-precision timing:
  ```cpp
  // Use 64-bit accumulator for fractional steps
  uint64_t stepAccumulator = 0;
  uint64_t stepIncrement = (uint64_t)(speed * TIME_PRECISION);
  
  // In timer ISR or update loop:
  stepAccumulator += stepIncrement;
  while (stepAccumulator >= THRESHOLD) {
      generatePulse();
      stepAccumulator -= THRESHOLD;
  }
  ```

- [ ] Add frequency error compensation
- [ ] Validate period calculation precision
- [ ] Test at critical speeds: 100, 500, 1000, 2000 steps/s

**Success Criteria**:
- 3200 steps at 500 steps/s = exactly 1 revolution (±2 steps)
- Position accuracy within 0.1% at all speeds <1000 steps/s
- No speed-dependent position drift

---

#### Task 2.2: Fix Position Overshoot in Short Moves (Issue #3)
**Priority**: HIGH  
**Files to Modify**: 
- [src/core/AccelerationProfile.h](../src/core/AccelerationProfile.h)
- [src/core/MotionMath.h](../src/core/MotionMath.h)

**Investigation Steps**:
1. **Test Triangular Profile Math**
   - Target: 50 steps
   - Max speed: 10,000 steps/s
   - Acceleration: 100 steps/s²
   - Calculate expected behavior:
     ```
     Accel distance: v² / (2a) = 10000² / (2×100) = 500,000 steps
     BUT target is only 50 steps!
     Therefore: Triangular profile (never reach max speed)
     Peak velocity: sqrt(2 × a × distance) = sqrt(2 × 100 × 50) = 100 steps/s
     ```
   
2. **Check Profile Calculation Code**
   - Verify triangular profile detection logic
   - Check velocity calculation: `v_peak = sqrt(2 * accel * (target/2))`
   - Review deceleration start point: `decel_start = target / 2`

3. **Examine Deceleration Timing**
   - Is deceleration starting at correct position?
   - Is deceleration rate correct (should match acceleration)?
   - Check for off-by-one errors in position comparison

**Implementation Plan**:
- [ ] Review and fix triangular profile detection:
  ```cpp
  float accelDistance = maxSpeed * maxSpeed / (2 * acceleration);
  float decelDistance = accelDistance;  // Assuming symmetric
  float totalAccelDecel = accelDistance + decelDistance;
  
  if (totalAccelDecel >= targetDistance) {
      // Triangular profile - won't reach max speed
      float peakSpeed = sqrt(acceleration * targetDistance);
      // ...
  }
  ```

- [ ] Add detailed position tracking during profile:
  ```cpp
  // Log:
  - Current position
  - Current velocity
  - Calculated deceleration start point
  - Distance remaining
  ```

- [ ] Implement safety bounds:
  - If position exceeds target, emergency deceleration
  - Add "approaching target" flag at 90% distance
  - Gentle final positioning phase

- [ ] Test with various short distances:
  - 10, 25, 50, 100, 200 steps
  - Different acceleration values

**Success Criteria**:
- 50-step move stops within ±5 steps of target
- No overshoot in any triangular profile move
- Position accuracy improves for longer moves
- Works across range of acceleration values

---

### Phase 3: Operational Issues

#### Task 3.1: Fix Serial Reconnection Issue (Issue #7)
**Priority**: MEDIUM  
**Files to Modify**: 
- [src/main.cpp](../src/main.cpp)

**Implementation Plan**:
- [ ] Detect serial connection state:
  ```cpp
  void checkSerialConnection() {
      static bool wasConnected = false;
      bool isConnected = Serial.dtr();  // Check DTR line
      
      if (isConnected && !wasConnected) {
          // New connection established
          Serial.flush();
          printStartupBanner();
          printMenu();
      }
      wasConnected = isConnected;
  }
  ```

- [ ] Add to main loop:
  ```cpp
  void loop() {
      checkSerialConnection();  // Call frequently
      // ... rest of loop
  }
  ```

- [ ] Implement serial buffer management:
  ```cpp
  if (Serial.available()) {
      Serial.flush();  // Clear any garbage
  }
  ```

**Success Criteria**:
- Reconnect serial terminal without reset
- Startup menu appears on every connection
- No commands lost or garbled

---

#### Task 3.2: Fix LED Status Management (Issue #8)
**Priority**: MEDIUM  
**Files to Modify**: 
- [src/core/StatusLED.cpp](../src/core/StatusLED.cpp)
- [src/core/StatusLED.h](../src/core/StatusLED.h)

**Implementation Plan**:
- [ ] Implement LED state priority system:
  ```cpp
  enum LEDPriority {
      PRIORITY_IDLE = 0,
      PRIORITY_STATUS_REQUEST = 1,
      PRIORITY_MOVING = 2,
      PRIORITY_ERROR = 3,
  };
  
  void setLEDState(LEDState state, LEDPriority priority, uint32_t duration_ms = 0) {
      if (priority >= currentPriority) {
          // Allow override
          currentState = state;
          currentPriority = priority;
          
          if (duration_ms > 0) {
              // Temporary state - revert after timeout
              stateTimeout = millis() + duration_ms;
          }
      }
  }
  ```

- [ ] Add state restoration:
  ```cpp
  void updateLED() {
      if (stateTimeout > 0 && millis() > stateTimeout) {
          // Timeout expired - revert to background state
          restorePreviousState();
      }
      // ... update LED based on current state
  }
  ```

- [ ] Modify status command to use temporary state:
  ```cpp
  void handleStatusCommand() {
      setLEDState(LED_STATUS_DISPLAY, PRIORITY_STATUS_REQUEST, 2000);  // 2s
      printStatus();
  }
  ```

**Success Criteria**:
- LED continues motion indication during status requests
- Status display shows briefly, then reverts
- Higher priority states (errors) override everything
- No LED state confusion or off states

---

#### Task 3.3: Fix TMC2209 Holding Current (Issue #4)
**Priority**: MEDIUM  
**Files to Modify**: 
- [src/drivers/TMC2209Driver.cpp](../src/drivers/TMC2209Driver.cpp)

**Investigation Steps**:
1. **Review TMC2209 Datasheet**
   - IHOLD register (bits 0-4 of IHOLD_IRUN)
   - TPOWERDOWN register (standstill power down delay)
   - StealthChop impact on holding torque

2. **Check Current Register Configuration**
   ```cpp
   // IHOLD_IRUN register (0x10)
   // Bits 0-4: IHOLD (standstill current)
   // Bits 8-12: IRUN (running current)
   // Bits 16-19: IHOLDDELAY (delay before reduction)
   ```

**Implementation Plan**:
- [ ] Read current IHOLD_IRUN value:
  ```cpp
  uint32_t holdRun = driver.IHOLD_IRUN();
  Serial.print("IHOLD: ");
  Serial.println(holdRun & 0x1F);
  Serial.print("IRUN: ");
  Serial.println((holdRun >> 8) & 0x1F);
  ```

- [ ] Set IHOLD to match IRUN:
  ```cpp
  void setHoldingCurrent(uint16_t current_ma) {
      uint8_t cs_value = currentToCS(current_ma);
      driver.ihold(cs_value);           // Same as run current
      driver.iholddelay(10);            // 10 * 2^18 clocks delay
      driver.TPOWERDOWN(10);            // ~2 seconds before power down
  }
  ```

- [ ] Disable freewheeling during hold:
  ```cpp
  driver.freewheel(0);  // Normal operation, not freewheeling
  ```

- [ ] Check StealthChop settings:
  ```cpp
  // StealthChop may reduce holding torque
  driver.en_pwm_mode(true);   // Enable StealthChop
  driver.pwm_autoscale(true); // Auto-tune
  ```

**Success Criteria**:
- Motor holds position firmly when enabled
- Noticeable difference between 200mA and 800mA holding torque
- Motor warm but not overheating
- Torque consistent moving and stationary

---

#### Task 3.4: Add Integer Overflow Validation (Issue #5)
**Priority**: LOW  
**Files to Modify**: 
- [src/main.cpp](../src/main.cpp) or command parsing module

**Implementation Plan**:
- [ ] Add input validation:
  ```cpp
  bool validateMoveCommand(const char* arg) {
      long value = strtol(arg, NULL, 10);
      
      if (value == LONG_MAX || value == LONG_MIN) {
          Serial.println("ERROR: Position value out of range");
          return false;
      }
      
      if (abs(value) > MAX_POSITION) {
          Serial.println("ERROR: Position exceeds maximum (±2147483647)");
          return false;
      }
      
      return true;
  }
  ```

- [ ] Apply to all numeric inputs
- [ ] Add helpful error messages

**Success Criteria**:
- Overflow values rejected with clear error
- Valid range documented
- No undefined behavior

---

### Phase 4: Testing & Validation

After all fixes are implemented:

#### Task 4.1: Comprehensive Retesting
**Files**: [docs/hardware-testing-validation.md](hardware-testing-validation.md)

**Test Plan**:
- [ ] Re-run ALL tests from hardware-testing-validation.md
- [ ] Update checkboxes to reflect results
- [ ] Focus on previously failing tests:
  - Acceleration profiles (all three)
  - Low-speed moves (<1000 steps/s)
  - Short moves (triangular profile)
  - Current control
  - High-speed operation (up to 1MHz if possible)

#### Task 4.2: Oscilloscope Validation
- [ ] Verify pulse gaps eliminated
- [ ] Confirm frequency range (10 Hz to 1 MHz)
- [ ] Check pulse width/timing meets TMC2209 specs
- [ ] Validate smooth acceleration/deceleration

#### Task 4.3: Long-Duration Testing
- [ ] 4-hour endurance test (Phase 6.1)
- [ ] 10,000-cycle position accuracy test (Phase 6.2)
- [ ] Temperature monitoring

#### Task 4.4: Documentation Update
- [ ] Update [testing_results.md](testing_results.md) with final results
- [ ] Mark all issues as FIXED or OPEN
- [ ] Document any remaining limitations
- [ ] Update README if needed

---

## Progress Tracking

Use this checklist to track overall progress:

### Phase 1: MCPWM Core Fixes
- [ ] Task 1.1: Fix pulse gaps during acceleration
- [ ] Task 1.2: Remove 50kHz frequency cap
- [ ] Task 1.3: Fix position counter synchronization
- [ ] Phase 1 validation complete

### Phase 2: Motion Control
- [ ] Task 2.1: Fix low-speed timing errors
- [ ] Task 2.2: Fix position overshoot
- [ ] Phase 2 validation complete

### Phase 3: Operational Issues
- [ ] Task 3.1: Serial reconnection
- [ ] Task 3.2: LED status management
- [ ] Task 3.3: TMC2209 holding current
- [ ] Task 3.4: Integer overflow validation
- [ ] Phase 3 validation complete

### Phase 4: Testing
- [ ] Task 4.1: Comprehensive retesting
- [ ] Task 4.2: Oscilloscope validation
- [ ] Task 4.3: Long-duration testing
- [ ] Task 4.4: Documentation update

---

## Notes

- All code changes should be tested incrementally
- Keep oscilloscope connected during Phase 1 work for immediate feedback
- Make git commits after each successful fix
- Don't proceed to Phase 2 until Phase 1 is 100% stable
- Motor can remain disconnected for Phase 1 testing (safer and faster)
- Document any new issues discovered during fixes

---

**Last Updated**: January 8, 2026  
**Status**: Ready to begin Phase 1  
**Next Action**: Start Task 1.1 - Investigate pulse gaps in MCPWMStepper.cpp
