# MCPWM API Migration - Blocked by Arduino Framework

**Date**: January 13, 2026  
**Status**: ❌ BLOCKED - New ESP-IDF MCPWM API not available in Arduino framework  
**Action**: Reverted to backup files

---

## Problem

The new ESP-IDF MCPWM API (`driver/mcpwm_timer.h`, `driver/mcpwm_oper.h`, etc.) is not available in the Arduino-ESP32 framework version being used.

**Compilation Error:**
```
src/drivers/MCPWMStepper.h:24:10: fatal error: driver/mcpwm_timer.h: No such file or directory
 #include "driver/mcpwm_timer.h"
          ^~~~~~~~~~~~~~~~~~~~~~
```

**Current Framework:**
- Platform: `espressif32 @ 6.12.0`
- Framework: `arduino-arduinoespressif32 @ 3.20017.241212`
- Arduino Core Version: 3.x

**Issue**: Arduino-ESP32 core still uses legacy MCPWM API wrapper, new ESP-IDF API headers not exposed.

---

## Options Forward

### Option A: Switch to ESP-IDF Framework ⚠️ (Major Change)

**Approach**: Change `framework = arduino` to `framework = espidf` in platformio.ini

**Pros:**
- ✅ Full access to new MCPWM API
- ✅ All synchronization flags available
- ✅ Better long-term solution
- ✅ Hardware position tracking possible

**Cons:**
- ❌ Requires rewriting all Arduino code (`Serial`, `digitalWrite`, etc.)
- ❌ TMCStepper library may not work (Arduino-specific)
- ❌ Significant time investment (1-2 days)
- ❌ Breaks existing working code

**Verdict**: Not recommended right now - too disruptive

---

### Option B: Workaround with Legacy API ✅ (Recommended)

**Approach**: Fix pulse gaps using legacy API limitations

**Strategy**: Since we can't use synchronous updates, we minimize frequency changes:

1. **Pre-calculate entire velocity profile** before motion starts
2. **Update frequency less often** (e.g., every 10-20ms instead of every loop)
3. **Use larger acceleration steps** (less frequent period updates)
4. **Add small delay after frequency change** (let timer stabilize)

**Implementation:**

```cpp
// In AccelerationProfile.h or MotorController.cpp

class BufferedAccelerationProfile {
private:
    float* _velocityBuffer;  // Pre-calculated velocities
    uint32_t _bufferSize;
    uint32_t _updateIntervalMs;  // Update every 10-20ms
    uint32_t _lastUpdateMs;
    
public:
    // Pre-calculate entire profile before starting motion
    void calculateProfile(float targetSpeed, float accel, int32_t steps) {
        _bufferSize = calculateBufferSize(accel, _updateIntervalMs);
        _velocityBuffer = new float[_bufferSize];
        
        // Fill buffer with velocity values at update intervals
        for (uint32_t i = 0; i < _bufferSize; i++) {
            float time = i * (_updateIntervalMs / 1000.0f);
            _velocityBuffer[i] = min(accel * time, targetSpeed);
        }
    }
    
    // Update frequency only at intervals, not every loop
    float getVelocityForUpdate() {
        uint32_t now = millis();
        if (now - _lastUpdateMs >= _updateIntervalMs) {
            _lastUpdateMs = now;
            return getNextBufferedVelocity();
        }
        return -1;  // No update needed
    }
};
```

**Key Changes:**
1. Update `MCPWMStepper::setFrequency()` less frequently
2. Add delay after frequency change: `delayMicroseconds(10);`
3. Use coarser acceleration steps to reduce update rate

**Pros:**
- ✅ Works with existing Arduino framework
- ✅ Minimal code changes
- ✅ Can implement quickly (2-4 hours)
- ✅ Reduces but doesn't eliminate pulse gaps

**Cons:**
- ⚠️ Not a perfect fix (may still have small gaps)
- ⚠️ Acceleration less smooth (coarser steps)
- ⚠️ Doesn't fix 50kHz cap
- ⚠️ Still needs hardware position tracking workaround

---

### Option C: Upgrade Arduino-ESP32 Core (Experimental)

**Approach**: Update to latest Arduino-ESP32 development branch

**Requirements:**
- Check if `arduino-esp32 @ 3.1.0` or newer has new API
- Modify `platformio.ini`:
  ```ini
  platform = espressif32
  platform_packages = 
      framework-arduinoespressif32 @ https://github.com/espressif/arduino-esp32.git#master
  ```

**Pros:**
- ✅ May have new MCPWM API exposed
- ✅ Keeps Arduino compatibility
- ✅ Best of both worlds

**Cons:**
- ❌ Unstable (development branch)
- ❌ May break existing code
- ❌ No guarantee new API is exposed
- ❌ Requires testing/validation

**Verdict**: Worth trying, but risky

---

## Recommended Path

### Immediate Action (Today)
1. ✅ **Files reverted** to working backup
2. **Answer your questions** about pending issues documentation
3. **Implement Option B workaround** (buffered profile updates)
   - Reduces frequency update rate
   - Adds stabilization delays
   - Pre-calculates acceleration profile

### Phase 1 Revised Plan
- ✅ Task 1.1: Pulse gaps - **WORKAROUND** (buffered updates)
- ❌ Task 1.2: 50kHz cap - **BLOCKED** (needs new API or register hacking)
- ❌ Task 1.3: Position tracking - **WORKAROUND** (software counter with validation)

### Long-Term Solution (When Ready)
- **Migrate to ESP-IDF framework** when:
  - Current Arduino code is stable
  - Have time for full rewrite (1-2 days)
  - Can rewrite Arduino dependencies (Serial, etc.)
  - Can port or replace TMCStepper library

---

## Documentation of Pending Issues

**Q: "Is it documented that those fixes are still pending?"**

**A:** Yes, documented in multiple places:

1. **[fix-plan.md](fix-plan.md)** - Tracks all phases and tasks
   - Phase 1 tasks marked with status
   - Phase 2 and 3 tasks listed
   
2. **[testing_results.md](testing_results.md)** - Lists all 11 issues
   - Issues #1-11 documented with severity
   - Root causes analyzed
   
3. **[mcpwm-api-migration.md](mcpwm-api-migration.md)** - JUST CREATED
   - Section: "Known Risks & Mitigations"
   - Section: "Checklist for Next Steps"
   - Lists what's fixed vs pending

4. **[mcpwm-migration-testing.md](mcpwm-migration-testing.md)** - JUST CREATED
   - Testing procedures for verification
   - Success criteria

**HOWEVER**: These docs assumed the new API would work! Since it doesn't, I need to update them with the workaround plan.

---

## Files to Update

1. **[fix-plan.md](fix-plan.md)**
   - Update Task 1.1 status to "WORKAROUND"
   - Note API migration blocked
   - Document Option B approach

2. **[mcpwm-api-migration.md](mcpwm-api-migration.md)**
   - Add "BLOCKED" section at top
   - Explain Arduino framework limitation
   - Document workaround approach

3. **New file**: [mcpwm-workaround-plan.md](mcpwm-workaround-plan.md)
   - Detailed Option B implementation
   - Buffered profile approach
   - Expected improvements

---

## Next Steps

1. **Update documentation** with BLOCKED status
2. **Implement Option B workaround**:
   - Buffered velocity updates
   - Reduced update frequency
   - Stabilization delays
3. **Test with oscilloscope** - see if gaps are reduced
4. **Proceed to Phase 2** if workaround acceptable
5. **Plan ESP-IDF migration** for future (when stable)

---

**End of Document**
