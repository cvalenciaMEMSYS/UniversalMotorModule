# FastAccelStepper Migration Plan

**Date**: January 13, 2026  
**Decision**: Replace custom MCPWMStepper with FastAccelStepper library  
**Reason**: Trying to fix MCPWM issues led down a rabbit hole - use proven library instead

---

## 🎯 Why FastAccelStepper?

### Problems with Our Current Approach
1. ❌ **Pulse gaps** during acceleration (Issue #11) - root cause of step skipping
2. ❌ **50kHz frequency cap** (Issue #9) - limits max speed
3. ❌ **Position tracking runaway** (Issue #10) - safety issue
4. ❌ **Low-speed rounding errors** (Issue #2) - extra steps
5. ❌ **Triangular profile overshoot** (Issue #3) - math bugs
6. ❌ **Complex to fix** - requires ESP-IDF migration or deep register hacking

### What FastAccelStepper Provides
From: https://github.com/gin66/FastAccelStepper

✅ **Hardware-driven pulse generation** (ESP32 MCPWM/LEDC peripherals)  
✅ **Up to 500kHz on ESP32-S3** (your max need: 200kHz)  
✅ **Multiple acceleration types**: Linear, Cubic (jerk-limited)  
✅ **Synchronous updates** (no pulse gaps)  
✅ **Hardware position tracking** (actual pulse counting)  
✅ **Mature, tested library** (used in 3D printers, CNC)  
✅ **Active development** (last update: recent)  
✅ **ESP32-S3 explicit support**

---

## 📦 Migration Scope

### Files to KEEP (No Changes)
- ✅ `src/main.cpp` - Entry point and serial handling
- ✅ `src/core/MotorController.cpp/.h` - Command processing (minor changes only)
- ✅ `src/core/StatusLED.cpp/.h` - LED feedback
- ✅ `src/drivers/DriverFactory.cpp/.h` - Hardware detection
- ✅ `src/drivers/IMotorDriver.h` - Interface
- ✅ `src/drivers/DCMotorDriver.cpp/.h` - DC motor (unrelated)
- ✅ `src/config/PinConfig.h` - Pin definitions

### Files to MODIFY (Replace MCPWMStepper with FastAccelStepper)
- 🔧 `src/drivers/TMC2209Driver.cpp/.h`
- 🔧 `src/drivers/TMC2208Driver.cpp/.h`
- 🔧 `src/core/MotorController.cpp` (simplify acceleration handling)

### Files to DELETE (Replaced by FastAccelStepper)
- ❌ `src/drivers/MCPWMStepper.cpp`
- ❌ `src/drivers/MCPWMStepper.h`
- ❌ `src/core/AccelerationProfile.h` (FastAccelStepper has better ones)
- ❌ `src/core/MotionMath.h` (FastAccelStepper handles this)

### Files to CREATE
- ✨ `src/drivers/FastAccelStepperWrapper.cpp/.h` (optional - adapter if needed)

---

## 🛠️ Implementation Steps

### Phase 1: Add Library (10 minutes)

**1.1 Update platformio.ini**

Add to `lib_deps`:
```ini
[common]
lib_deps = 
    teemuatlut/TMCStepper@^0.7.3
    gin66/FastAccelStepper@^0.30.5  ; ← ADD THIS
```

**1.2 Test compilation**
```bash
platformio lib install
platformio run
```

**Expected**: Library downloads and compiles (even if project has errors)

---

### Phase 2: Create Wrapper (Optional, 30 minutes)

**Purpose**: Isolate FastAccelStepper API from our code, easier to swap later

**Create**: `src/drivers/FastAccelStepperWrapper.h`

```cpp
#ifndef FASTACCEL_STEPPER_WRAPPER_H
#define FASTACCEL_STEPPER_WRAPPER_H

#include <FastAccelStepper.h>

/**
 * @brief Thin wrapper around FastAccelStepper for consistent API
 * 
 * Provides same methods as old MCPWMStepper but uses FastAccelStepper internally.
 */
class FastAccelStepperWrapper {
public:
    FastAccelStepperWrapper();
    ~FastAccelStepperWrapper();
    
    // Initialization
    bool init(gpio_num_t stepPin, gpio_num_t dirPin);
    
    // Frequency control
    void setFrequency(float stepsPerSecond);
    float getFrequency() const;
    
    // Direction
    void setDirection(bool forward);
    
    // Start/Stop
    void start();
    void stop();
    bool isRunning() const;
    
    // Position tracking (hardware-based)
    int32_t getPosition() const;
    void resetPosition();
    
    // Motion control (new capabilities)
    void moveTo(int32_t position);
    void moveBy(int32_t steps);
    void setAcceleration(uint32_t accel);  // steps/s²
    bool isMoving() const;
    
private:
    FastAccelStepperEngine* _engine;
    FastAccelStepper* _stepper;
    gpio_num_t _stepPin;
    gpio_num_t _dirPin;
    bool _initialized;
    float _currentFrequency;
};

#endif
```

**Create**: `src/drivers/FastAccelStepperWrapper.cpp`

```cpp
#include "FastAccelStepperWrapper.h"
#include <Arduino.h>

FastAccelStepperWrapper::FastAccelStepperWrapper()
    : _engine(nullptr)
    , _stepper(nullptr)
    , _initialized(false)
    , _currentFrequency(1000.0f) {
}

FastAccelStepperWrapper::~FastAccelStepperWrapper() {
    if (_stepper) {
        _stepper->disableOutputs();
    }
}

bool FastAccelStepperWrapper::init(gpio_num_t stepPin, gpio_num_t dirPin) {
    _stepPin = stepPin;
    _dirPin = dirPin;
    
    // Initialize FastAccelStepper engine
    _engine = new FastAccelStepperEngine();
    _engine->init();
    
    // Create stepper on MCPWM peripheral
    _stepper = _engine->stepperConnectToPin(_stepPin);
    if (_stepper == nullptr) {
        Serial.println("[FastAccel] Failed to create stepper");
        return false;
    }
    
    // Configure direction pin (FastAccelStepper doesn't manage this automatically)
    _stepper->setDirectionPin(_dirPin);
    
    // Set default speed and acceleration
    _stepper->setSpeedInHz(_currentFrequency);
    _stepper->setAcceleration(500);  // 500 steps/s² default
    
    // Enable outputs
    _stepper->enableOutputs();
    
    _initialized = true;
    Serial.println("[FastAccel] Initialized successfully");
    Serial.print("[FastAccel] Step pin: GPIO");
    Serial.print(_stepPin);
    Serial.print(", Dir pin: GPIO");
    Serial.println(_dirPin);
    
    return true;
}

void FastAccelStepperWrapper::setFrequency(float stepsPerSecond) {
    if (!_initialized || !_stepper) return;
    
    _currentFrequency = stepsPerSecond;
    _stepper->setSpeedInHz((uint32_t)stepsPerSecond);
}

float FastAccelStepperWrapper::getFrequency() const {
    return _currentFrequency;
}

void FastAccelStepperWrapper::setDirection(bool forward) {
    // Direction is handled automatically by FastAccelStepper
    // based on move commands (positive = forward, negative = reverse)
    // This method kept for API compatibility but not used
}

void FastAccelStepperWrapper::start() {
    if (!_initialized || !_stepper) return;
    _stepper->enableOutputs();
}

void FastAccelStepperWrapper::stop() {
    if (!_initialized || !_stepper) return;
    _stepper->forceStopAndNewPosition(0);  // Emergency stop
}

bool FastAccelStepperWrapper::isRunning() const {
    if (!_initialized || !_stepper) return false;
    return isMoving();
}

int32_t FastAccelStepperWrapper::getPosition() const {
    if (!_initialized || !_stepper) return 0;
    return _stepper->getCurrentPosition();
}

void FastAccelStepperWrapper::resetPosition() {
    if (!_initialized || !_stepper) return;
    _stepper->setCurrentPosition(0);
}

void FastAccelStepperWrapper::moveTo(int32_t position) {
    if (!_initialized || !_stepper) return;
    _stepper->moveTo(position);
}

void FastAccelStepperWrapper::moveBy(int32_t steps) {
    if (!_initialized || !_stepper) return;
    _stepper->move(steps);  // Relative move
}

void FastAccelStepperWrapper::setAcceleration(uint32_t accel) {
    if (!_initialized || !_stepper) return;
    _stepper->setAcceleration(accel);
}

bool FastAccelStepperWrapper::isMoving() const {
    if (!_initialized || !_stepper) return false;
    return _stepper->isRunning();
}
```

**Benefits of Wrapper:**
- Easy to swap back if needed
- Consistent API with old MCPWMStepper
- Can add logging/debugging easily
- Isolates external dependency

---

### Phase 3: Update TMC2209Driver (1 hour)

**3.1 Modify Header**

In `src/drivers/TMC2209Driver.h`:

```cpp
// OLD:
#include "MCPWMStepper.h"

// NEW:
#include "FastAccelStepperWrapper.h"
```

```cpp
// OLD:
private:
    MCPWMStepper mcpwmStepper;

// NEW:
private:
    FastAccelStepperWrapper mcpwmStepper;  // Keep same name for minimal changes
```

**3.2 Modify Implementation**

In `src/drivers/TMC2209Driver.cpp`:

**init() method** - should work with no changes (same API)

**moveBy() method** - simplify (FastAccelStepper handles acceleration):

```cpp
// OLD:
void TMC2209Driver::moveBy(int32_t steps) {
    // Complex acceleration profile handling...
    // Multiple loops, timing calculations, etc.
}

// NEW:
void TMC2209Driver::moveBy(int32_t steps) {
    if (!_initialized) return;
    
    mcpwmStepper.moveBy(steps);
    // FastAccelStepper handles acceleration automatically!
}
```

**moveTo() method** - simplify:

```cpp
void TMC2209Driver::moveTo(int32_t position) {
    if (!_initialized) return;
    
    mcpwmStepper.moveTo(position);
}
```

**isBusy() method** - use FastAccelStepper's status:

```cpp
bool TMC2209Driver::isBusy() const {
    if (!_initialized) return false;
    return mcpwmStepper.isMoving();
}
```

**getPosition() method** - use hardware counter:

```cpp
int32_t TMC2209Driver::getPosition() const {
    return mcpwmStepper.getPosition();  // Hardware-accurate!
}
```

---

### Phase 4: Update TMC2208Driver (30 minutes)

**Same changes as TMC2209Driver** - replace MCPWMStepper with FastAccelStepperWrapper

---

### Phase 5: Simplify MotorController (30 minutes)

**5.1 Remove Acceleration Profile Management**

In `src/core/MotorController.cpp`:

```cpp
// DELETE: Profile selection logic
// DELETE: Profile-specific move handling

// KEEP: Command parsing and driver dispatch

// NEW: Just call driver methods directly
void MotorController::moveBy(int32_t steps) {
    if (_driver) {
        _driver->moveBy(steps);  // Driver handles acceleration via FastAccelStepper
    }
}
```

**5.2 Simplify setAcceleration()**

```cpp
void MotorController::setAcceleration(float accel) {
    if (_driver) {
        // FastAccelStepper handles this internally
        _driver->setAcceleration((uint32_t)accel);
    }
}
```

---

### Phase 6: Testing (2 hours)

**6.1 Compilation Test**
```bash
platformio run
```
**Expected**: Clean compile with no errors

**6.2 Upload and Basic Test**
```bash
platformio run --target upload
platformio device monitor
```

**Test Commands:**
```
move 3200       # One full rotation
status          # Check position tracking
move -3200      # Reverse
```

**Expected**: Motor moves smoothly, position tracking accurate

**6.3 Oscilloscope Test - Pulse Gaps** (Issue #11)
```
set speed 1000
set accel 500
move 100000
```

**Expected**: 
- ✅ NO pulse gaps during acceleration
- ✅ Smooth continuous pulse train
- ✅ Clean waveform

**6.4 Oscilloscope Test - High Frequency** (Issue #9)
```
set speed 100000
move 10000
```

**Expected**:
- ✅ Reaches 100kHz (not capped at 50kHz)
- ✅ Clean waveform
- ✅ 50% duty cycle

**6.5 Position Tracking Test** (Issue #10)
```
set speed 50000
move 100000
status
```

**Expected**:
- ✅ Position counter matches actual steps
- ✅ No runaway behavior
- ✅ Accurate at high speeds

**6.6 Low-Speed Test** (Issue #2)
```
set speed 500
move 3200
```
**Count motor rotations manually**

**Expected**:
- ✅ Exactly 1 rotation (not more)
- ✅ No extra steps

---

### Phase 7: Cleanup (15 minutes)

**Delete old files:**
```bash
rm src/drivers/MCPWMStepper.cpp
rm src/drivers/MCPWMStepper.h
rm src/core/AccelerationProfile.h
rm src/core/MotionMath.h
```

**Update documentation:**
- Mark issues #2, #3, #9, #10, #11 as FIXED
- Update architecture diagram
- Create migration complete report

---

## 📊 Expected Results

### Issues FIXED by Migration

| Issue # | Description | Status |
|---------|-------------|--------|
| #11 | Pulse gaps during acceleration | ✅ FIXED |
| #9 | 50kHz frequency cap | ✅ FIXED |
| #10 | Position tracking runaway | ✅ FIXED |
| #1 | Step skipping (caused by #11) | ✅ FIXED |
| #2 | Low-speed extra steps | ✅ LIKELY FIXED |
| #3 | Triangular profile overshoot | ✅ LIKELY FIXED |

**Total**: 6 critical issues resolved!

### Issues REMAINING (Unrelated to Pulse Generation)

| Issue # | Description | Priority | Group |
|---------|-------------|----------|-------|
| #4 | Weak holding torque | MEDIUM | TMC2209 config |
| #7 | Serial reconnection hangs | MEDIUM | main.cpp |
| #8 | LED status overwritten | LOW | StatusLED |
| #5 | Integer overflow | LOW | Command parsing |

---

## ⏱️ Time Estimate

| Phase | Task | Time |
|-------|------|------|
| 1 | Add library | 10 min |
| 2 | Create wrapper | 30 min |
| 3 | Update TMC2209 | 1 hour |
| 4 | Update TMC2208 | 30 min |
| 5 | Simplify MotorController | 30 min |
| 6 | Testing | 2 hours |
| 7 | Cleanup | 15 min |
| **TOTAL** | **End-to-end** | **~5 hours** |

**Much better than:**
- ESP-IDF migration: 1-2 days
- Fixing MCPWM manually: Unknown complexity

---

## 🎯 Success Criteria

✅ **Compiles without errors**  
✅ **Motor moves smoothly forward/reverse**  
✅ **Zero pulse gaps on oscilloscope during acceleration**  
✅ **Reaches >50kHz (test at 100kHz minimum)**  
✅ **Position tracking accurate within ±1 step**  
✅ **No extra steps at low speeds (<1000 Hz)**  
✅ **Motion quality improved (no stuttering)**  

**If all pass** → Migration SUCCESS! 🎉

---

## 🚨 Risks & Mitigations

### Risk 1: FastAccelStepper API Different Than Expected
**Mitigation**: Wrapper layer isolates changes, easy to adapt

### Risk 2: ESP32-S3 Compatibility Issues
**Mitigation**: Library explicitly supports ESP32-S3, active development

### Risk 3: TMCStepper Library Conflicts
**Mitigation**: FastAccelStepper is pure pulse generation, TMCStepper is UART - no overlap

### Risk 4: Breaking Existing Features
**Mitigation**: Keep same IMotorDriver interface, only change internals

---

## 📝 Next Steps

1. **Review this plan** - any concerns or questions?
2. **Approve migration** - ready to start?
3. **Begin Phase 1** - add library to project
4. **Iterate phases** - test after each phase

---

**Ready to proceed?** 🚀
