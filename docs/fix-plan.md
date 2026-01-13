# Universal Motor Module - Phased Fix Plan

**Created**: January 13, 2026  
**Last Updated**: January 13, 2026  
**Status**: ⚠️ Phase 1 - BLOCKED - New API not available in Arduino framework  
**Current Task**: Implementing workaround approach with legacy API

---

## ⚠️ CRITICAL UPDATE - API Migration Blocked

**Problem**: The new ESP-IDF MCPWM API is not available in Arduino-ESP32 framework.

**Impact**:
- Cannot use synchronous period updates (`update_period_on_empty` flag)
- Cannot use 1MHz timer resolution directly
- Cannot use hardware capture channel for position tracking

**Status**: Files reverted to backup. Implementing **Option B - Workaround Approach**.

**See**: [mcpwm-api-blocked.md](mcpwm-api-blocked.md) for detailed analysis and options.

---

## 🎯 Phase 1: MCPWM Core Fixes (REVISED)

**Objective**: Reduce pulse generation issues using legacy API workarounds.

**Estimated Time**: 1-2 days (workaround approach)

### ✅ Progress Tracker

#### Task 1.1: Reduce Pulse Gaps During Acceleration (WORKAROUND)
**Status**: 🔄 In Progress (Workaround Implementation)  
**Priority**: CRITICAL  
**Files**: `src/drivers/MCPWMStepper.cpp`, `src/core/AccelerationProfile.h`, `src/core/MotorController.cpp`

**Approach**: Since we cannot use synchronous updates, minimize frequency changes:

**Workaround Strategy**:
1. **Pre-calculate velocity profile** before motion starts
2. **Buffer velocity values** at intervals (e.g., 10-20ms)
3. **Update frequency less often** (not every loop cycle)
4. **Add stabilization delay** after frequency changes
5. **Use larger acceleration steps** (coarser but less updates)

**Implementation Plan**:
- [ ] Create `BufferedAccelerationProfile` class
- [ ] Pre-calculate entire velocity curve before motion
- [ ] Update MCPWM frequency at fixed intervals only
- [ ] Add `delayMicroseconds(10)` after frequency change
- [ ] Test with oscilloscope - measure gap reduction

**Expected Results**:
- Gaps reduced from 4 to 0-2 (not perfect, but better)
- Smoother motion overall
- Some coarseness in acceleration curve acceptable

**Success Criteria** (REVISED):
- [ ] Pulse gaps reduced significantly (70-90% reduction)
- [ ] No complete stops during acceleration
- [ ] Motor motion noticeably smoother
- [ ] Oscilloscope shows improvement

**Fixes Issues**: Partially addresses #11, #1.1, #1.2, #1.3

**Notes**:
- This is a **workaround**, not a perfect fix
- Perfect fix requires ESP-IDF framework migration
- Acceptable for now to unblock other work

---

#### Task 1.2: Address 50kHz Frequency Limit (INVESTIGATION)
**Status**: ⏸️ Blocked (Requires new API or register access)  
**Priority**: CRITICAL  
**Files**: `src/drivers/MCPWMStepper.cpp`

**Problem**: Legacy API limits frequency, new API not available

**Options**:
- [ ] Update getCurrentPosition() to read from capture
- [ ] Add position error detection (target vs actual)
- [ ] Implement emergency stop on error > 100 steps
- [ ] Add velocity sanity check
- [ ] Test at various speeds (100, 1000, 10000, 50000 steps/s)
- [ ] Test runaway scenario (high commanded speed)

**Success Criteria**:
- [ ] Position counter matches actual pulses (±1 step tolerance)
- [ ] No position drift over time
- [ ] No runaway motion at high speeds
- [ ] Position error detection triggers within 100ms
- [ ] System safely stops on position tracking failure

**Fixes Issues**: #10 (runaway motor, position counter issues)

**Notes**:
- Currently position increments based on commanded speed, not actual pulses
- At 50kHz: position jumps from 6400 to 68000 in <1 second
- Critical for safety and position accuracy

---

### Phase 1 Validation Checkpoint 🛑

**Before proceeding to Phase 2, verify**:
- [ ] All three tasks above completed
- [ ] Oscilloscope shows clean pulses with no gaps
- [ ] Can run at speeds from 100 to 100,000+ steps/s smoothly
- [ ] Position tracking is accurate and stable
- [ ] Motor can complete 10 consecutive 1-revolution moves with <5 step error

**If any test fails, stop and fix before Phase 2.**

---

## 🎯 Phase 2: Motion Control Refinement

**Objective**: Fix motion quality and accuracy issues.

**Status**: ⏸️ Waiting for Phase 1 Completion  
**Estimated Time**: 1-2 days

### Task 2.1: Fix Low-Speed Timing Errors (<1000 steps/s)
**Status**: ⏸️ Not Started  
**Priority**: HIGH  
**Files**: `src/drivers/MCPWMStepper.cpp`, `src/core/MotionMath.h`

**Fixes Issues**: #2 (extra steps at low speeds)

---

### Task 2.2: Fix Position Overshoot in Short Moves
**Status**: ⏸️ Not Started  
**Priority**: HIGH  
**Files**: `src/core/AccelerationProfile.h`, `src/core/MotionMath.h`

**Fixes Issues**: #3 (position overshoot)

---

### Phase 2 Validation Checkpoint 🛑

**Verify**:
- [ ] Low-speed moves are accurate (3200 steps @ 500 steps/s = 1 revolution)
- [ ] Short moves don't overshoot (50 steps with high accel)
- [ ] All acceleration profiles work correctly
- [ ] Complex motion patterns complete without error

---

## 🎯 Phase 3: Operational Improvements

**Objective**: Fix user experience and operational issues.

**Status**: ⏸️ Waiting for Phase 2 Completion  
**Estimated Time**: 1 day

### Task 3.1: Serial Reconnection Without Reset
**Status**: ⏸️ Not Started  
**Fixes Issues**: #7 (serial reset requirement)

---

### Task 3.2: LED Status Priority System
**Status**: ⏸️ Not Started  
**Fixes Issues**: #8 (LED status overwritten)

---

### Task 3.3: TMC2209 Holding Current Configuration
**Status**: ⏸️ Not Started  
**Fixes Issues**: #4 (weak holding torque)

---

### Task 3.4: Input Validation for Overflow
**Status**: ⏸️ Not Started  
**Fixes Issues**: #5 (integer overflow)

---

### Phase 3 Validation Checkpoint 🛑

**Verify**:
- [ ] Serial reconnection works smoothly
- [ ] LED status system is intuitive
- [ ] Motor holding torque is strong
- [ ] All commands have proper input validation

---

## 🎯 Phase 4: Comprehensive Testing & Documentation

**Objective**: Validate all fixes and update documentation.

**Status**: ⏸️ Waiting for Phase 3 Completion  
**Estimated Time**: 1 day

### Task 4.1: Rerun All Hardware Tests
**Status**: ⏸️ Not Started

---

### Task 4.2: Oscilloscope Validation
**Status**: ⏸️ Not Started

---

### Task 4.3: Long-Duration Testing
**Status**: ⏸️ Not Started

---

### Task 4.4: Update Documentation
**Status**: ⏸️ Not Started

---

## 📊 Overall Progress

### Summary
- **Phase 1**: 🔄 In Progress (Task 1.1 active)
- **Phase 2**: ⏸️ Blocked (waiting for Phase 1)
- **Phase 3**: ⏸️ Blocked (waiting for Phase 2)
- **Phase 4**: ⏸️ Blocked (waiting for Phase 3)

### Task Completion
- **Phase 1**: 0/3 tasks complete (0%)
- **Phase 2**: 0/2 tasks complete (0%)
- **Phase 3**: 0/4 tasks complete (0%)
- **Phase 4**: 0/4 tasks complete (0%)
- **Overall**: 0/13 tasks complete (0%)

---

## 🔑 Key Guidelines

1. **Don't skip phases** - Each phase must be 100% validated before proceeding
2. **Keep oscilloscope connected** during Phase 1
3. **Git commit after each task** completion
4. **Motor can stay disconnected** for Phase 1 (safer, faster)
5. **Test incrementally** - verify after each change
6. **Document discoveries** - add notes as you find things

---

## 📝 Work Log

### January 13, 2026
- Created fix plan document
- Started Phase 1, Task 1.1
- Ready to investigate pulse gaps in MCPWMStepper.cpp

---

## ⚠️ Critical Reminders

- **STOP if Phase 1 validation fails** - later fixes won't work without stable pulse generation
- **Test with oscilloscope frequently** - visual confirmation is essential
- **Monitor for new issues** - fixing one thing can expose another
- **Keep backup branches** - tag working states in git

---

**Next Action**: Begin investigating MCPWMStepper.cpp for pulse gap root cause
