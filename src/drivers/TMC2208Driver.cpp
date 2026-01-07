/*
 * =============================================================================
 * TMC2208 DRIVER - Implementation
 * =============================================================================
 * 
 * UART-controlled stepper driver with Step/Dir fallback mode.
 * Uses TMCStepper library (TMC2208Stepper class).
 * 
 * Key differences from TMC2209:
 *   - NO StallGuard (sensorless homing not available)
 *   - NO CoolStep (automatic current reduction)
 *   - Same UART configuration for current, microstepping, StealthChop
 * 
 * =============================================================================
 */

#include "TMC2208Driver.h"

// =============================================================================
// CONSTRUCTORS
// =============================================================================

TMC2208Driver::TMC2208Driver()
    : TMC2208Driver(&Serial1, TMC_TX_PIN, TMC_RX_PIN,
                    TMC_EN_PIN, TMC_STEP_PIN, TMC_DIR_PIN,
                    TMC_R_SENSE) {
}

TMC2208Driver::TMC2208Driver(HardwareSerial* serial, uint8_t txPin, uint8_t rxPin,
                             uint8_t enPin, uint8_t stepPin, uint8_t dirPin,
                             float rSense)
    : _serial(serial)
    , _txPin(txPin)
    , _rxPin(rxPin)
    , _enPin(enPin)
    , _stepPin(stepPin)
    , _dirPin(dirPin)
    , _rSense(rSense)
    , _driver(nullptr)
    , _uartMode(true)  // Start assuming UART will work
    , _enabled(false)
    , _position(0)
    , _targetPosition(0)
    , _moving(false)
    , _runCurrentMA(DefaultMotorConfig::STEPPER_CURRENT_MA)
    , _holdCurrentMA(DefaultMotorConfig::STEPPER_HOLD_CURRENT)
    , _microsteps(DefaultMotorConfig::STEPPER_MICROSTEPS)
    , _maxSpeed(DefaultMotorConfig::STEPPER_MAX_SPEED)
    , _currentSpeed(0)
    , _lastStepTime(0)
    , _stepInterval(1000)
    , _startPosition(0)
    , _accelSteps(0)
    , _decelSteps(0)
    , _totalMoveSteps(0)
    , _isTriangular(false)
    , _moveDirection(1) {
    
    // Initialize with default constant velocity profile
    _profile = AccelerationProfile::constant(_maxSpeed);
    
    // Initialize S-curve arrays
    for (int i = 0; i < 7; i++) {
        _scurveSegmentEnd[i] = 0;
        _scurveAccel[i] = 0;
        _jerkSign[i] = 0;
    }
    for (int i = 0; i < 8; i++) {
        _scurveVelocity[i] = 0;
    }
}

TMC2208Driver::~TMC2208Driver() {
    if (_driver != nullptr) {
        delete _driver;
        _driver = nullptr;
    }
    disable();
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool TMC2208Driver::init() {
    Serial.println("TMC2208 Driver: Initializing...");
    
    // Configure control pins
    pinMode(_enPin, OUTPUT);
    pinMode(_stepPin, OUTPUT);
    pinMode(_dirPin, OUTPUT);
    
    // Start disabled
    digitalWrite(_enPin, HIGH);  // HIGH = disabled
    digitalWrite(_stepPin, LOW);
    digitalWrite(_dirPin, LOW);
    
    _enabled = false;
    _position = 0;
    _targetPosition = 0;
    _moving = false;
    
    // Initialize UART
    _serial->begin(TMC_UART_BAUD, SERIAL_8N1, _rxPin, _txPin);
    delay(100);
    
    // Create TMCStepper driver object
    if (_driver != nullptr) {
        delete _driver;
    }
    _driver = new TMC2208Stepper(_serial, _rSense);
    _driver->begin();
    delay(50);
    
    // Test UART connection
    if (!testConnection()) {
        Serial.println("TMC2208: ⚠️ UART connection failed!");
        Serial.println("         Driver can still work in Step/Dir mode.");
        Serial.println("         Send 'stepdir on' command to enable fallback mode.");
        Serial.println("         In Step/Dir mode: current set by Vref, microsteps by MS1/MS2 pins.");
        // DON'T auto-switch to Step/Dir - let user decide
        _uartMode = true;  // Keep trying UART until user switches
    } else {
        Serial.println("TMC2208 Driver: UART connected ✓");
        _uartMode = true;
        
        // Configure driver with current settings
        configureDriver();
    }
    
    Serial.println("TMC2208 Driver: Ready");
    Serial.println("  Note: TMC2208 has NO StallGuard - use limit switches for homing");
    
    // Enable the driver
    enable();
    
    return true;
}

void TMC2208Driver::configureDriver() {
    if (_driver == nullptr || !_uartMode) return;
    
    Serial.println("TMC2208: Configuring via UART...");
    
    // Basic configuration
    _driver->toff(5);                     // Enable driver
    _driver->en_spreadCycle(false);       // StealthChop mode (silent)
    _driver->pwm_autoscale(true);         // Auto current scaling
    _driver->pdn_disable(true);           // Use UART, not PDN for current
    _driver->mstep_reg_select(true);      // Microsteps via UART
    
    // Current
    _driver->rms_current(_runCurrentMA);
    _driver->ihold(_holdCurrentMA > 0 ? (_holdCurrentMA * 31 / _runCurrentMA) : 0);
    
    // Microsteps - use direct CHOPCONF write for all values
    uint8_t mres = microStepsToMRES(_microsteps);
    uint32_t chopconf = _driver->CHOPCONF();
    chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
    _driver->CHOPCONF(chopconf);
    
    delay(50);
    
    Serial.print("  Current: ");
    Serial.print(_runCurrentMA);
    Serial.println(" mA RMS");
    Serial.print("  Microsteps: 1/");
    Serial.println(_microsteps);
}

void TMC2208Driver::reconfigure() {
    if (_uartMode) {
        configureDriver();
    } else {
        Serial.println("TMC2208: Cannot reconfigure in Step/Dir mode");
    }
}

// =============================================================================
// ENABLE / DISABLE
// =============================================================================

void TMC2208Driver::enable() {
    digitalWrite(_enPin, LOW);  // Active LOW
    _enabled = true;
}

void TMC2208Driver::disable() {
    digitalWrite(_enPin, HIGH);
    _enabled = false;
    _moving = false;
}

bool TMC2208Driver::isEnabled() const {
    return _enabled;
}

// =============================================================================
// MOTION CONTROL
// =============================================================================

void TMC2208Driver::move(int32_t steps) {
    if (steps == 0) return;
    
    // Store move parameters
    _startPosition = _position;
    _targetPosition = _position + steps;
    _totalMoveSteps = abs(steps);
    _moveDirection = steps > 0 ? 1 : -1;
    
    // Set direction pin
    digitalWrite(_dirPin, steps > 0 ? HIGH : LOW);
    
    // Plan motion based on profile type
    if (_profile.type == VelocityProfileType::CONSTANT) {
        _accelSteps = 0;
        _decelSteps = 0;
        _currentSpeed = _profile.maxSpeed;
    } 
    else if (_profile.type == VelocityProfileType::TRAPEZOIDAL) {
        planTrapezoidalMotion();
    }
    else if (_profile.type == VelocityProfileType::S_CURVE) {
        planSCurveMotion();
    }
    
    _moving = true;
    calculateStepInterval();
    _lastStepTime = micros();
}

void TMC2208Driver::moveTo(int32_t position) {
    if (position < 0) position = 0;
    int32_t delta = position - _position;
    move(delta);
}

void TMC2208Driver::stop() {
    _moving = false;
    _currentSpeed = 0;
}

void TMC2208Driver::emergencyStop() {
    _moving = false;
    _currentSpeed = 0;
}

bool TMC2208Driver::isMoving() const {
    return _moving;
}

void TMC2208Driver::update() {
    if (!_moving || !_enabled) return;
    
    uint32_t now = micros();
    
    // Check if it's time for next step
    if ((now - _lastStepTime) >= _stepInterval) {
        // Check if we've reached target
        if (_position == _targetPosition) {
            _moving = false;
            _currentSpeed = 0;
            return;
        }
        
        // Do a step
        doStep();
        _lastStepTime = now;
        
        // Update speed based on profile type
        if (_profile.type == VelocityProfileType::TRAPEZOIDAL) {
            updateTrapezoidalSpeed();
        } else if (_profile.type == VelocityProfileType::S_CURVE) {
            updateSCurveSpeed();
        }
    }
}

void TMC2208Driver::doStep() {
    // Generate step pulse
    digitalWrite(_stepPin, HIGH);
    delayMicroseconds(2);  // Minimum pulse width (TMC2208 requires ~100ns, 2µs is 20x margin)
    digitalWrite(_stepPin, LOW);
    
    // Update position based on move direction
    _position += _moveDirection;
}

void TMC2208Driver::calculateStepInterval() {
    float speed = _currentSpeed;
    if (speed <= 0) speed = _maxSpeed;
    if (speed <= 0) speed = 1;
    
    _stepInterval = (uint32_t)(1000000.0f / speed);
    if (_stepInterval < 10) _stepInterval = 10;
}

// =============================================================================
// MOTION PLANNING - TRAPEZOIDAL
// =============================================================================

void TMC2208Driver::planTrapezoidalMotion() {
    float accel = _profile.acceleration;
    float maxSpeed = _profile.maxSpeed;
    
    if (accel <= 0) {
        _accelSteps = 0;
        _decelSteps = 0;
        _currentSpeed = maxSpeed;
        _isTriangular = false;
        return;
    }
    
    int32_t stepsToMaxSpeed = (int32_t)((maxSpeed * maxSpeed) / (2.0f * accel));
    
    if (2 * stepsToMaxSpeed >= _totalMoveSteps) {
        _isTriangular = true;
        _accelSteps = _totalMoveSteps / 2;
        _decelSteps = _totalMoveSteps - _accelSteps;
    } else {
        _isTriangular = false;
        _accelSteps = stepsToMaxSpeed;
        _decelSteps = stepsToMaxSpeed;
    }
    
    _currentSpeed = 50.0f;
}

void TMC2208Driver::updateTrapezoidalSpeed() {
    int32_t stepsDone = abs(_position - _startPosition);
    int32_t stepsRemaining = abs(_targetPosition - _position);
    
    float accel = _profile.acceleration;
    float maxSpeed = _profile.maxSpeed;
    
    if (stepsDone < _accelSteps) {
        _currentSpeed = sqrtf(2.0f * accel * (float)(stepsDone + 1));
        _currentSpeed = min(_currentSpeed, maxSpeed);
    } 
    else if (stepsRemaining <= _decelSteps) {
        _currentSpeed = sqrtf(2.0f * accel * (float)stepsRemaining);
    }
    else {
        _currentSpeed = maxSpeed;
    }
    
    if (_currentSpeed < 50.0f) _currentSpeed = 50.0f;
    
    calculateStepInterval();
}

// =============================================================================
// MOTION PLANNING - S-CURVE (7-segment)
// =============================================================================

void TMC2208Driver::planSCurveMotion() {
    // 7-segment S-curve profile:
    // Seg 0: Jerk+ (acceleration increasing from 0)
    // Seg 1: Constant acceleration (at max accel)
    // Seg 2: Jerk- (acceleration decreasing to 0, reaching max velocity)
    // Seg 3: Cruise (constant velocity)
    // Seg 4: Jerk- (acceleration decreasing, starting decel)
    // Seg 5: Constant deceleration (at max decel)
    // Seg 6: Jerk+ (acceleration increasing back to 0, stopping)
    
    float jerk = _profile.jerk;
    float maxAccel = _profile.acceleration;
    float maxSpeed = _profile.maxSpeed;
    float minSpeed = 50.0f;  // Minimum speed to prevent stalling
    
    if (jerk <= 0 || maxAccel <= 0) {
        planTrapezoidalMotion();
        return;
    }
    
    // Time to reach max acceleration with given jerk: t_j = a_max / j
    float t_j = maxAccel / jerk;
    
    // Distance covered during one jerk phase: s = j * t³ / 6
    float jerkPhaseDist = jerk * t_j * t_j * t_j / 6.0f;
    
    // Velocity gained during one jerk phase: v = j * t² / 2  
    float jerkPhaseVel = jerk * t_j * t_j / 2.0f;
    
    // Velocity gained during constant accel phase to reach max speed
    float constAccelVel = maxSpeed - minSpeed - 2.0f * jerkPhaseVel;
    
    float constAccelDist = 0;
    float t_a = 0;
    
    if (constAccelVel > 0) {
        t_a = constAccelVel / maxAccel;
        float v_at_seg1_start = minSpeed + jerkPhaseVel;
        constAccelDist = v_at_seg1_start * t_a + 0.5f * maxAccel * t_a * t_a;
    } else {
        // Short move or low max speed - scale down the profile
        float velocityBudget = maxSpeed - minSpeed;
        if (velocityBudget <= 0) {
            _profile.type = VelocityProfileType::CONSTANT;
            _accelSteps = 0;
            _decelSteps = 0;
            _currentSpeed = maxSpeed > minSpeed ? maxSpeed : minSpeed;
            _isTriangular = false;
            return;
        }
        jerkPhaseVel = velocityBudget / 2.0f;
        t_j = sqrtf(2.0f * jerkPhaseVel / jerk);
        jerkPhaseDist = jerk * t_j * t_j * t_j / 6.0f;
        constAccelVel = 0;
        constAccelDist = 0;
    }
    
    // Distance for segment 2
    float v_at_seg2_start = minSpeed + jerkPhaseVel + constAccelVel;
    float seg2Dist = v_at_seg2_start * t_j + 0.5f * maxAccel * t_j * t_j - jerk * t_j * t_j * t_j / 6.0f;
    
    float accelSideDist = jerkPhaseDist + constAccelDist + seg2Dist;
    float decelSideDist = accelSideDist;
    float totalAccelDecelDist = accelSideDist + decelSideDist;
    float cruiseDist = (float)_totalMoveSteps - totalAccelDecelDist;
    
    if (cruiseDist < 0) {
        // Scale down the profile for short moves
        float availablePerSide = (float)_totalMoveSteps / 2.0f;
        float scaleFactor = availablePerSide / accelSideDist;
        
        if (scaleFactor < 0.1f) {
            planTrapezoidalMotion();
            return;
        }
        
        jerkPhaseVel *= scaleFactor;
        constAccelVel *= scaleFactor;
        jerkPhaseDist *= scaleFactor * scaleFactor * scaleFactor;
        constAccelDist *= scaleFactor * scaleFactor;
        seg2Dist = availablePerSide - jerkPhaseDist - constAccelDist;
        accelSideDist = availablePerSide;
        decelSideDist = availablePerSide;
        cruiseDist = 0;
        maxSpeed = minSpeed + 2.0f * jerkPhaseVel + constAccelVel;
    }
    
    int32_t pos = 0;
    
    // Segment 0: Jerk+ phase
    _scurveSegmentEnd[0] = pos + (int32_t)jerkPhaseDist;
    _scurveVelocity[0] = minSpeed;
    _scurveVelocity[1] = minSpeed + jerkPhaseVel;
    _scurveAccel[0] = 0;
    _jerkSign[0] = jerk;
    pos = _scurveSegmentEnd[0];
    
    // Segment 1: Constant acceleration
    _scurveSegmentEnd[1] = pos + (int32_t)constAccelDist;
    _scurveVelocity[2] = minSpeed + jerkPhaseVel + constAccelVel;
    _scurveAccel[1] = maxAccel;
    _jerkSign[1] = 0;
    pos = _scurveSegmentEnd[1];
    
    // Segment 2: Jerk- phase
    _scurveSegmentEnd[2] = pos + (int32_t)seg2Dist;
    _scurveVelocity[3] = maxSpeed;
    _scurveAccel[2] = maxAccel;
    _jerkSign[2] = -jerk;
    pos = _scurveSegmentEnd[2];
    
    // Segment 3: Cruise
    _scurveSegmentEnd[3] = pos + (int32_t)cruiseDist;
    _scurveVelocity[4] = maxSpeed;
    _scurveAccel[3] = 0;
    _jerkSign[3] = 0;
    pos = _scurveSegmentEnd[3];
    
    // Segment 4: Jerk- phase (starting decel)
    _scurveSegmentEnd[4] = pos + (int32_t)seg2Dist;
    _scurveVelocity[5] = minSpeed + jerkPhaseVel + constAccelVel;
    _scurveAccel[4] = 0;
    _jerkSign[4] = -jerk;
    pos = _scurveSegmentEnd[4];
    
    // Segment 5: Constant deceleration
    _scurveSegmentEnd[5] = pos + (int32_t)constAccelDist;
    _scurveVelocity[6] = minSpeed + jerkPhaseVel;
    _scurveAccel[5] = -maxAccel;
    _jerkSign[5] = 0;
    pos = _scurveSegmentEnd[5];
    
    // Segment 6: Jerk+ phase (stopping)
    _scurveSegmentEnd[6] = _totalMoveSteps;
    _scurveVelocity[7] = minSpeed;
    _scurveAccel[6] = -maxAccel;
    _jerkSign[6] = jerk;
    
    _currentSpeed = minSpeed;
    _isTriangular = false;
}

void TMC2208Driver::updateSCurveSpeed() {
    int32_t stepsDone = abs(_position - _startPosition);
    
    int segment = 0;
    for (int i = 0; i < 7; i++) {
        if (stepsDone < _scurveSegmentEnd[i]) {
            segment = i;
            break;
        }
        if (i == 6) segment = 6;
    }
    
    int32_t segmentStart = (segment == 0) ? 0 : _scurveSegmentEnd[segment - 1];
    int32_t stepsInSegment = stepsDone - segmentStart;
    int32_t segmentLength = _scurveSegmentEnd[segment] - segmentStart;
    
    if (segmentLength <= 0) {
        _currentSpeed = _scurveVelocity[segment];
        calculateStepInterval();
        return;
    }
    
    float v0 = _scurveVelocity[segment];
    int nextSeg = min(segment + 1, 7);
    float v1 = _scurveVelocity[nextSeg];
    float a0 = _scurveAccel[segment];
    float j = _jerkSign[segment];
    
    // Use proper kinematic equations based on segment type
    if (j != 0) {
        // Jerk phase: use smooth S-curve interpolation
        float progress = (float)stepsInSegment / (float)segmentLength;
        float smoothProgress = progress * progress * (3.0f - 2.0f * progress);
        _currentSpeed = v0 + (v1 - v0) * smoothProgress;
    }
    else if (a0 != 0) {
        // Constant acceleration phase: v² = v0² + 2*a*s
        float v_squared = v0 * v0 + 2.0f * a0 * (float)stepsInSegment;
        if (v_squared > 0) {
            _currentSpeed = sqrtf(v_squared);
        } else {
            _currentSpeed = v0;
        }
    }
    else {
        // Cruise phase: constant velocity
        _currentSpeed = v0;
    }
    
    if (_currentSpeed < 50.0f) _currentSpeed = 50.0f;
    if (_currentSpeed > _profile.maxSpeed) _currentSpeed = _profile.maxSpeed;
    
    calculateStepInterval();
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void TMC2208Driver::setMaxSpeed(float stepsPerSecond) {
    if (stepsPerSecond <= 0) stepsPerSecond = 1;
    _maxSpeed = stepsPerSecond;
    _profile.maxSpeed = stepsPerSecond;
}

void TMC2208Driver::setCurrent(uint16_t runMA, uint16_t holdMA) {
    _runCurrentMA = runMA;
    _holdCurrentMA = holdMA > 0 ? holdMA : runMA / 2;
    
    if (_uartMode && _driver != nullptr) {
        _driver->rms_current(_runCurrentMA);
        _driver->ihold(_holdCurrentMA * 31 / _runCurrentMA);
        Serial.print("TMC2208: Current set to ");
        Serial.print(_runCurrentMA);
        Serial.println(" mA RMS");
    } else {
        Serial.println("TMC2208: In Step/Dir mode - current set by Vref potentiometer");
    }
}

void TMC2208Driver::setMicrosteps(uint16_t microsteps) {
    _microsteps = microsteps;
    
    if (_uartMode && _driver != nullptr) {
        uint8_t mres = microStepsToMRES(microsteps);
        uint32_t chopconf = _driver->CHOPCONF();
        chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
        _driver->CHOPCONF(chopconf);
        
        Serial.print("TMC2208: Microsteps set to 1/");
        Serial.println(_microsteps);
    } else {
        Serial.println("TMC2208: In Step/Dir mode - microsteps set by MS1/MS2 pins");
    }
}

void TMC2208Driver::setAccelerationProfile(const AccelerationProfile& profile) {
    _profile = profile;
    _maxSpeed = profile.maxSpeed;
}

// =============================================================================
// TMC2208-SPECIFIC METHODS
// =============================================================================

void TMC2208Driver::setStealthChop(bool enable) {
    if (_uartMode && _driver != nullptr) {
        _driver->en_spreadCycle(!enable);  // false = StealthChop, true = SpreadCycle
        Serial.print("TMC2208: ");
        Serial.println(enable ? "StealthChop enabled (silent)" : "SpreadCycle enabled (high torque)");
    } else {
        Serial.println("TMC2208: Cannot change mode in Step/Dir fallback");
    }
}

void TMC2208Driver::setStepDirMode(bool enabled) {
    if (enabled) {
        _uartMode = false;
        Serial.println("TMC2208: Switched to Step/Dir only mode");
        Serial.println("  - Microstepping: determined by MS1/MS2 pins");
        Serial.println("  - Current limit: determined by Vref potentiometer");
        Serial.println("  - Runtime configuration: NOT available");
    } else {
        // Try to re-enable UART
        if (testConnection()) {
            _uartMode = true;
            configureDriver();
            Serial.println("TMC2208: UART mode re-enabled ✓");
        } else {
            Serial.println("TMC2208: UART still unavailable - staying in Step/Dir mode");
        }
    }
}

uint8_t TMC2208Driver::microStepsToMRES(uint16_t ms) {
    switch (ms) {
        case 256: return 0;
        case 128: return 1;
        case 64:  return 2;
        case 32:  return 3;
        case 16:  return 4;
        case 8:   return 5;
        case 4:   return 6;
        case 2:   return 7;
        case 1:   return 8;  // Full step
        default:  return 4;  // Default to 16
    }
}

uint16_t TMC2208Driver::mrestoMicrosteps(uint8_t mres) {
    switch (mres) {
        case 0: return 256;
        case 1: return 128;
        case 2: return 64;
        case 3: return 32;
        case 4: return 16;
        case 5: return 8;
        case 6: return 4;
        case 7: return 2;
        case 8: return 1;
        default: return 16;
    }
}

// =============================================================================
// CONNECTION TEST
// =============================================================================

bool TMC2208Driver::testConnection() {
    if (_driver == nullptr) return false;
    
    // Read IOIN register - should return a valid value
    uint32_t ioin = _driver->IOIN();
    
    // If version field is 0 or 0xFF, likely no connection
    uint8_t version = (ioin >> 24) & 0xFF;
    
    // TMC2208 should return version 0x20
    return (version == 0x20);
}

// =============================================================================
// POSITION
// =============================================================================

int32_t TMC2208Driver::getPosition() const {
    return _position;
}

void TMC2208Driver::setPosition(int32_t position) {
    _position = position;
}

void TMC2208Driver::home(int8_t direction) {
    // TMC2208 has no StallGuard - homing requires external limit switch
    Serial.println("TMC2208: Homing not supported (no StallGuard)");
    Serial.println("  Use external limit switch for homing");
    _position = 0;
    _targetPosition = 0;
}

// =============================================================================
// STATUS
// =============================================================================

MotorStatus TMC2208Driver::getStatus() {
    MotorStatus status;
    
    status.enabled = _enabled;
    status.moving = _moving;
    status.stalling = false;  // TMC2208 has NO StallGuard
    
    status.position = _position;
    status.targetPosition = _targetPosition;
    
    status.currentMA = _runCurrentMA;  // From configuration
    status.loadValue = 0;  // No StallGuard on TMC2208
    status.currentSpeed = _currentSpeed;
    
    status.errorFlags = MotorError::NONE;
    
    // If UART available, try to read error flags
    if (_uartMode && _driver != nullptr) {
        uint32_t drv_status = _driver->DRV_STATUS();
        
        if (drv_status & 0x00000001) {  // OT (overtemperature)
            status.errorFlags |= MotorError::OVER_TEMP;
        }
        if (drv_status & 0x00001000) {  // Open load A
            status.errorFlags |= MotorError::OPEN_LOAD;
        }
        if (drv_status & 0x00002000) {  // Open load B
            status.errorFlags |= MotorError::OPEN_LOAD;
        }
    }
    
    return status;
}

void TMC2208Driver::printDiagnostics() {
    Serial.println("\n═══════════════════════════════════════════════════════════════");
    Serial.println("                TMC2208 DIAGNOSTICS");
    Serial.println("═══════════════════════════════════════════════════════════════\n");
    
    Serial.print("  Mode:         ");
    Serial.println(_uartMode ? "UART" : "Step/Dir Fallback");
    
    Serial.print("  Enabled:      ");
    Serial.println(_enabled ? "Yes" : "No");
    
    Serial.print("  Position:     ");
    Serial.println(_position);
    
    Serial.print("  Moving:       ");
    Serial.println(_moving ? "Yes" : "No");
    
    Serial.print("  Speed:        ");
    Serial.print(_currentSpeed);
    Serial.println(" steps/sec");
    
    if (_uartMode && _driver != nullptr) {
        Serial.println("\n  --- UART Diagnostics ---");
        
        uint32_t drv_status = _driver->DRV_STATUS();
        uint32_t chopconf = _driver->CHOPCONF();
        
        Serial.print("  DRV_STATUS:   0x");
        Serial.println(drv_status, HEX);
        
        Serial.print("  CHOPCONF:     0x");
        Serial.println(chopconf, HEX);
        
        Serial.print("  StealthChop:  ");
        Serial.println(_driver->stealth() ? "Active" : "Inactive");
        
        Serial.print("  Standstill:   ");
        Serial.println((drv_status & 0x80000000) ? "Yes" : "No");
        
        Serial.print("  Overtemp:     ");
        Serial.println((drv_status & 0x00000001) ? "⚠️ YES!" : "No");
        
        Serial.print("  OT Warning:   ");
        Serial.println((drv_status & 0x00000002) ? "⚠️ YES!" : "No");
        
        // Note: TMC2208 has NO StallGuard
        Serial.println("\n  Note: TMC2208 has NO StallGuard - use limit switches for homing");
    } else {
        Serial.println("\n  Note: No UART diagnostics available in Step/Dir mode");
    }
    
    Serial.println("\n═══════════════════════════════════════════════════════════════\n");
}
