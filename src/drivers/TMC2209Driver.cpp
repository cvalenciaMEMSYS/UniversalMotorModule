/*
 * =============================================================================
 * TMC2209 DRIVER - Implementation
 * =============================================================================
 */

#include "TMC2209Driver.h"
#include "../core/MotionMath.h"

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

TMC2209Driver::TMC2209Driver()
    : TMC2209Driver(&Serial1, TMC_TX_PIN, TMC_RX_PIN, 
                    TMC_EN_PIN, TMC_STEP_PIN, TMC_DIR_PIN,
                    TMC_DRIVER_ADDR, TMC_R_SENSE) {
}

TMC2209Driver::TMC2209Driver(HardwareSerial* serial, uint8_t txPin, uint8_t rxPin,
                             uint8_t enPin, uint8_t stepPin, uint8_t dirPin,
                             uint8_t address, float rSense)
    : _serial(serial)
    , _txPin(txPin)
    , _rxPin(rxPin)
    , _enPin(enPin)
    , _stepPin(stepPin)
    , _dirPin(dirPin)
    , _address(address)
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
    , _stepInterval(1000)  // 1ms default
    , _startPosition(0)
    , _accelSteps(0)
    , _decelSteps(0)
    , _totalMoveSteps(0)
    , _isTriangular(false)
    , _moveDirection(1)
    , _stallThreshold(50) {
    
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

TMC2209Driver::~TMC2209Driver() {
    if (_driver != nullptr) {
        delete _driver;
        _driver = nullptr;
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool TMC2209Driver::init() {
    Serial.println("TMC2209 Driver: Initializing...");
    
    // Configure GPIO pins
    pinMode(_enPin, OUTPUT);
    pinMode(_stepPin, OUTPUT);
    pinMode(_dirPin, OUTPUT);
    
    digitalWrite(_enPin, HIGH);   // Start disabled
    digitalWrite(_stepPin, LOW);
    digitalWrite(_dirPin, LOW);
    
    _enabled = false;
    
    // Initialize UART
    _serial->begin(TMC_UART_BAUD, SERIAL_8N1, _rxPin, _txPin);
    delay(100);
    
    // Create TMCStepper driver object
    if (_driver != nullptr) {
        delete _driver;
    }
    _driver = new TMC2209Stepper(_serial, _rSense, _address);
    _driver->begin();
    delay(50);
    
    // Test connection
    if (!testConnection()) {
        Serial.println("TMC2209: ⚠️ UART connection failed!");
        Serial.println("         Driver can still work in Step/Dir mode.");
        Serial.println("         Send 'stepdir on' command to enable fallback mode.");
        Serial.println("         In Step/Dir mode: current set by Vref, microsteps by MS1/MS2 pins.");
        Serial.println("         Note: StallGuard homing will NOT work in Step/Dir mode.");
        // DON'T auto-switch to Step/Dir - let user decide
        _uartMode = true;  // Keep trying UART until user switches
    } else {
        Serial.println("TMC2209 Driver: UART connected ✓");
        _uartMode = true;
        
        // Configure driver with current settings
        configureDriver();
    }
    
    Serial.println("TMC2209 Driver: Ready");
    
    // Enable driver
    enable();
    
    return true;
}

void TMC2209Driver::configureDriver() {
    if (_driver == nullptr) return;
    
    // Basic configuration
    _driver->toff(5);                     // Enable driver
    _driver->en_spreadCycle(false);       // StealthChop mode
    _driver->pwm_autoscale(true);         // Auto current scaling
    _driver->pdn_disable(true);           // Use UART, not PDN for current
    _driver->mstep_reg_select(true);      // Microsteps via UART
    
    // Current
    _driver->rms_current(_runCurrentMA);
    _driver->ihold(_holdCurrentMA > 0 ? (_holdCurrentMA * 31 / _runCurrentMA) : 0);
    
    // Microsteps - use direct CHOPCONF write for fullstep (MRES=8) support
    uint8_t mres = microStepsToMRES(_microsteps);
    uint32_t chopconf = _driver->CHOPCONF();
    chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
    _driver->CHOPCONF(chopconf);
    
    // StallGuard
    _driver->SGTHRS(_stallThreshold);
    _driver->TCOOLTHRS(0xFFFFF);  // Enable at all speeds
    
    delay(50);
}

void TMC2209Driver::reconfigure() {
    if (_uartMode) {
        configureDriver();
    } else {
        Serial.println("TMC2209: Cannot reconfigure in Step/Dir mode");
    }
}

void TMC2209Driver::setStepDirMode(bool enabled) {
    if (enabled) {
        _uartMode = false;
        Serial.println("TMC2209: Switched to Step/Dir only mode");
        Serial.println("  - Microstepping: determined by MS1/MS2 pins");
        Serial.println("  - Current limit: determined by Vref potentiometer");
        Serial.println("  - StallGuard: NOT available (use limit switches)");
        Serial.println("  - CoolStep: NOT available");
        Serial.println("  - Runtime configuration: NOT available");
    } else {
        // Try to re-enable UART
        if (testConnection()) {
            _uartMode = true;
            configureDriver();
            Serial.println("TMC2209: UART mode re-enabled ✓");
        } else {
            Serial.println("TMC2209: UART still unavailable - staying in Step/Dir mode");
        }
    }
}

// =============================================================================
// ENABLE / DISABLE
// =============================================================================

void TMC2209Driver::enable() {
    digitalWrite(_enPin, LOW);  // Active LOW
    _enabled = true;
}

void TMC2209Driver::disable() {
    digitalWrite(_enPin, HIGH);
    _enabled = false;
    _moving = false;
}

bool TMC2209Driver::isEnabled() const {
    return _enabled;
}

// =============================================================================
// MOTION CONTROL
// =============================================================================

void TMC2209Driver::move(int32_t steps) {
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
        // No acceleration - fixed speed
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

void TMC2209Driver::moveTo(int32_t position) {
    if (position < 0) position = 0;  // Only positive positions
    int32_t delta = position - _position;
    move(delta);
}

void TMC2209Driver::stop() {
    // Emergency stop - immediate halt without deceleration
    // For controlled deceleration, use setMaxSpeed(0) instead
    _moving = false;
    _currentSpeed = 0;
}

void TMC2209Driver::emergencyStop() {
    _moving = false;
    _currentSpeed = 0;
    // Target stays where we stopped
    _targetPosition = _position;
}

bool TMC2209Driver::isMoving() const {
    return _moving;
}

void TMC2209Driver::update() {
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
        // CONSTANT profile: speed stays fixed, no update needed
    }
}

void TMC2209Driver::doStep() {
    // Generate step pulse
    digitalWrite(_stepPin, HIGH);
    delayMicroseconds(2);  // Minimum pulse width (TMC2209 requires ~100ns, 2µs is 20x margin)
    digitalWrite(_stepPin, LOW);
    
    // Update position based on move direction
    _position += _moveDirection;
}

void TMC2209Driver::calculateStepInterval() {
    // Convert current speed (steps/sec) to interval (microseconds)
    float speed = _currentSpeed;
    if (speed <= 0) speed = _maxSpeed;  // Fallback to max speed
    if (speed <= 0) speed = 1;          // Safety
    
    _stepInterval = MotionMath::calculateStepInterval(speed);
}

// =============================================================================
// MOTION PLANNING - TRAPEZOIDAL
// =============================================================================

void TMC2209Driver::planTrapezoidalMotion() {
    float accel = _profile.acceleration;
    float maxSpeed = _profile.maxSpeed;
    
    if (accel <= 0) {
        // No acceleration configured - treat as constant
        _accelSteps = 0;
        _decelSteps = 0;
        _currentSpeed = maxSpeed;
        _isTriangular = false;
        return;
    }
    
    // Steps to reach max speed using kinematic equation
    int32_t stepsToMaxSpeed = MotionMath::calculateStepsToMaxSpeed(maxSpeed, accel);
    
    // Check if this will be a triangular profile (never reaches max speed)
    _isTriangular = MotionMath::isTriangularProfile(_totalMoveSteps, maxSpeed, accel);
    
    if (_isTriangular) {
        // Triangular profile - peak at halfway point
        _accelSteps = _totalMoveSteps / 2;
        _decelSteps = _totalMoveSteps - _accelSteps;
    } else {
        // Full trapezoidal - accel, cruise, decel
        _accelSteps = stepsToMaxSpeed;
        _decelSteps = stepsToMaxSpeed;
    }
    
    // Start from minimum speed (not zero, to avoid division issues)
    _currentSpeed = 50.0f;  // Minimum starting speed
}

void TMC2209Driver::updateTrapezoidalSpeed() {
    // Calculate how many steps we've done and how many remain
    int32_t stepsDone = abs(_position - _startPosition);
    int32_t stepsRemaining = abs(_targetPosition - _position);
    
    // Use shared motion math for velocity calculation
    _currentSpeed = MotionMath::calculateTrapezoidalVelocity(
        stepsDone,
        stepsRemaining,
        _accelSteps,
        _decelSteps,
        _profile.acceleration,
        _profile.maxSpeed,
        50.0f  // minSpeed
    );
    
    // Update step interval based on new speed
    calculateStepInterval();
}

// =============================================================================
// MOTION PLANNING - S-CURVE (7-segment)
// =============================================================================

void TMC2209Driver::planSCurveMotion() {
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
        // Fall back to trapezoidal if jerk not configured
        planTrapezoidalMotion();
        return;
    }
    
    // Time to reach max acceleration with given jerk
    float t_j = MotionMath::calculateJerkPhaseTime(maxAccel, jerk);
    
    // Distance covered during one jerk phase
    float jerkPhaseDist = MotionMath::calculateJerkPhaseDistance(jerk, t_j);
    
    // Velocity gained during one jerk phase
    float jerkPhaseVel = MotionMath::calculateJerkPhaseVelocity(jerk, t_j);
    
    // Velocity gained during constant accel phase to reach max speed
    // We need: minSpeed + 2*jerkPhaseVel + constAccelVel = maxSpeed
    float constAccelVel = maxSpeed - minSpeed - 2.0f * jerkPhaseVel;
    
    float constAccelDist = 0;
    float t_a = 0;
    
    // Check if we can reach max accel and have a constant accel phase
    if (constAccelVel > 0) {
        // We have a constant acceleration phase
        t_a = constAccelVel / maxAccel;
        // Distance during constant accel: s = v0*t + 0.5*a*t²
        float v_at_seg1_start = minSpeed + jerkPhaseVel;
        constAccelDist = v_at_seg1_start * t_a + 0.5f * maxAccel * t_a * t_a;
    } else {
        // Short move or low max speed - scale down the profile
        // Reduce max accel and jerk proportionally
        float velocityBudget = maxSpeed - minSpeed;
        if (velocityBudget <= 0) {
            // Can't even accelerate - use constant speed
            _profile.type = VelocityProfileType::CONSTANT;
            _accelSteps = 0;
            _decelSteps = 0;
            _currentSpeed = maxSpeed > minSpeed ? maxSpeed : minSpeed;
            _isTriangular = false;
            return;
        }
        // No constant accel phase - just jerk phases
        jerkPhaseVel = velocityBudget / 2.0f;
        // Recalculate t_j for this reduced velocity gain: v = j*t²/2 -> t = sqrt(2v/j)
        t_j = std::sqrt(2.0f * jerkPhaseVel / jerk);
        jerkPhaseDist = MotionMath::calculateJerkPhaseDistance(jerk, t_j);
        constAccelVel = 0;
        constAccelDist = 0;
    }
    
    // Distance for segment 2 (jerk-): starts at (minSpeed + jerkPhaseVel + constAccelVel), ends at maxSpeed
    // During this phase: v(t) = v0 + a0*t - j*t²/2, where a0 = maxAccel (or scaled accel)
    float v_at_seg2_start = minSpeed + jerkPhaseVel + constAccelVel;
    float seg2Dist = v_at_seg2_start * t_j + 0.5f * maxAccel * t_j * t_j - jerk * t_j * t_j * t_j / 6.0f;
    
    // Total distance for acceleration side (3 segments)
    float accelSideDist = jerkPhaseDist + constAccelDist + seg2Dist;
    
    // Deceleration side is symmetric
    float decelSideDist = accelSideDist;
    
    // Check if we have room for cruise phase
    float totalAccelDecelDist = accelSideDist + decelSideDist;
    float cruiseDist = (float)_totalMoveSteps - totalAccelDecelDist;
    
    if (cruiseDist < 0) {
        // Not enough room for full S-curve - scale down the profile
        // Use available distance and reduce max speed proportionally
        float availablePerSide = (float)_totalMoveSteps / 2.0f;
        float scaleFactor = availablePerSide / accelSideDist;
        
        if (scaleFactor < 0.1f) {
            // Very short move - fall back to trapezoidal
            planTrapezoidalMotion();
            return;
        }
        
        // Scale velocities and distances
        jerkPhaseVel *= scaleFactor;
        constAccelVel *= scaleFactor;
        jerkPhaseDist *= scaleFactor * scaleFactor * scaleFactor;  // Distance scales as t³ for jerk phase
        constAccelDist *= scaleFactor * scaleFactor;  // Distance scales as t² for const accel
        seg2Dist = availablePerSide - jerkPhaseDist - constAccelDist;
        accelSideDist = availablePerSide;
        decelSideDist = availablePerSide;
        cruiseDist = 0;
        maxSpeed = minSpeed + 2.0f * jerkPhaseVel + constAccelVel;
    }
    
    // Calculate segment end positions (cumulative steps from start)
    int32_t pos = 0;
    
    // Segment 0: Jerk+ phase (a increases from 0)
    _scurveSegmentEnd[0] = pos + (int32_t)jerkPhaseDist;
    _scurveVelocity[0] = minSpeed;
    _scurveVelocity[1] = minSpeed + jerkPhaseVel;
    _scurveAccel[0] = 0;
    _jerkSign[0] = jerk;  // Store actual jerk value
    pos = _scurveSegmentEnd[0];
    
    // Segment 1: Constant acceleration
    _scurveSegmentEnd[1] = pos + (int32_t)constAccelDist;
    _scurveVelocity[2] = minSpeed + jerkPhaseVel + constAccelVel;
    _scurveAccel[1] = maxAccel;
    _jerkSign[1] = 0;
    pos = _scurveSegmentEnd[1];
    
    // Segment 2: Jerk- phase (a decreases to 0)
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
    
    // Segment 4: Jerk- phase (a decreases from 0, starts decel)
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
    
    // Segment 6: Jerk+ phase (a increases back to 0)
    _scurveSegmentEnd[6] = _totalMoveSteps;
    _scurveVelocity[7] = minSpeed;
    _scurveAccel[6] = -maxAccel;
    _jerkSign[6] = jerk;
    
    _currentSpeed = minSpeed;
    _isTriangular = false;
}

void TMC2209Driver::updateSCurveSpeed() {
    int32_t stepsDone = abs(_position - _startPosition);
    
    // Find which segment we're in
    int segment = 0;
    for (int i = 0; i < 7; i++) {
        if (stepsDone < _scurveSegmentEnd[i]) {
            segment = i;
            break;
        }
        if (i == 6) segment = 6;
    }
    
    // Calculate position within segment
    int32_t segmentStart = (segment == 0) ? 0 : _scurveSegmentEnd[segment - 1];
    int32_t stepsInSegment = stepsDone - segmentStart;
    int32_t segmentLength = _scurveSegmentEnd[segment] - segmentStart;
    
    if (segmentLength <= 0) {
        _currentSpeed = _scurveVelocity[segment];
        calculateStepInterval();
        return;
    }
    
    float v0 = _scurveVelocity[segment];
    int nextSeg = (segment + 1 < 8) ? segment + 1 : 7;
    float v1 = _scurveVelocity[nextSeg];
    float a0 = _scurveAccel[segment];
    float j = _jerkSign[segment];  // Jerk value (can be +j, -j, or 0)
    
    // Use proper kinematic equations based on segment type
    if (j != 0) {
        // Jerk phase: use smooth S-curve interpolation
        float progress = static_cast<float>(stepsInSegment) / static_cast<float>(segmentLength);
        _currentSpeed = MotionMath::interpolateJerkPhaseVelocity(v0, v1, progress);
    }
    else if (a0 != 0) {
        // Constant acceleration phase: v² = v0² + 2*a*s
        _currentSpeed = MotionMath::calculateConstantAccelVelocity(v0, a0, static_cast<float>(stepsInSegment));
    }
    else {
        // Cruise phase: constant velocity
        _currentSpeed = v0;
    }
    
    // Enforce minimum and maximum speed limits
    _currentSpeed = MotionMath::clampSpeed(_currentSpeed, 50.0f, _profile.maxSpeed);
    
    calculateStepInterval();
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void TMC2209Driver::setMaxSpeed(float stepsPerSecond) {
    if (stepsPerSecond <= 0) stepsPerSecond = 1;
    _maxSpeed = stepsPerSecond;
    _profile.maxSpeed = stepsPerSecond;
    calculateStepInterval();
}

void TMC2209Driver::setCurrent(uint16_t runMA, uint16_t holdMA) {
    _runCurrentMA = runMA;
    _holdCurrentMA = holdMA;
    
    if (_uartMode && _driver != nullptr) {
        _driver->rms_current(_runCurrentMA);
        _driver->ihold(_holdCurrentMA > 0 ? (_holdCurrentMA * 31 / _runCurrentMA) : 0);
        Serial.print("TMC2209: Current set to ");
        Serial.print(_runCurrentMA);
        Serial.println(" mA RMS");
    } else {
        Serial.println("TMC2209: In Step/Dir mode - current set by Vref potentiometer");
    }
}

void TMC2209Driver::setMicrosteps(uint16_t microsteps) {
    // Validate
    switch (microsteps) {
        case 1: case 2: case 4: case 8: case 16:
        case 32: case 64: case 128: case 256:
            break;
        default:
            microsteps = 16;  // Default
    }
    
    _microsteps = microsteps;
    
    if (_uartMode && _driver != nullptr) {
        // Use direct CHOPCONF write for fullstep support
        uint8_t mres = microStepsToMRES(microsteps);
        uint32_t chopconf = _driver->CHOPCONF();
        chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
        _driver->CHOPCONF(chopconf);
        
        Serial.print("TMC2209: Microsteps set to 1/");
        Serial.println(_microsteps);
    } else {
        Serial.println("TMC2209: In Step/Dir mode - microsteps set by MS1/MS2 pins");
    }
}

void TMC2209Driver::setAccelerationProfile(const AccelerationProfile& profile) {
    _profile = profile;
    _maxSpeed = profile.maxSpeed;
    calculateStepInterval();
}

// =============================================================================
// POSITION
// =============================================================================

int32_t TMC2209Driver::getPosition() const {
    return _position;
}

void TMC2209Driver::setPosition(int32_t position) {
    _position = position;
}

void TMC2209Driver::home(int8_t direction) {
    // Use StallGuard to detect home position (requires UART mode)
    if (!_uartMode) {
        Serial.println("TMC2209: Homing NOT available in Step/Dir mode!");
        Serial.println("         Use physical limit switches instead.");
        Serial.println("         Switch to UART mode with 'stepdir off' to use StallGuard homing.");
        _position = 0;
        _targetPosition = 0;
        return;
    }
    
    Serial.println("Homing (using StallGuard)...");
    
    // Move in specified direction until stall
    move(direction * 10000);  // Large number of steps
    
    while (isMoving()) {
        update();
        
        if (isStalling()) {
            emergencyStop();
            Serial.println("Stall detected - home found!");
            break;
        }
        
        yield();  // Prevent watchdog timeout
    }
    
    // Set current position as home (0)
    _position = 0;
    _targetPosition = 0;
}

// =============================================================================
// DIAGNOSTICS
// =============================================================================

MotorStatus TMC2209Driver::getStatus() {
    MotorStatus status;
    
    status.enabled = _enabled;
    status.moving = _moving;
    status.position = _position;
    status.targetPosition = _targetPosition;
    status.currentMA = _runCurrentMA;
    status.currentSpeed = _currentSpeed;
    
    if (_driver != nullptr) {
        // Read StallGuard result
        status.loadValue = _driver->SG_RESULT();
        status.stalling = isStalling();
        
        // Read DRV_STATUS for errors
        uint32_t drvStatus = _driver->DRV_STATUS();
        
        status.errorFlags = MotorError::NONE;
        
        if ((drvStatus >> 26) & 1) {  // ot (over-temp)
            status.errorFlags |= MotorError::OVER_TEMP;
        }
        if (((drvStatus >> 27) & 1) || ((drvStatus >> 28) & 1)) {  // s2ga, s2gb
            status.errorFlags |= MotorError::SHORT_CIRCUIT;
        }
        if (((drvStatus >> 29) & 1) || ((drvStatus >> 30) & 1)) {  // ola, olb
            status.errorFlags |= MotorError::OPEN_LOAD;
        }
        
        // Check UART communication
        if (_driver->test_connection() != 0) {
            status.errorFlags |= MotorError::COMM_FAILURE;
        }
    }
    
    return status;
}

bool TMC2209Driver::isStalling() {
    if (!_uartMode || _driver == nullptr) return false;
    
    // StallGuard result drops when motor is loaded
    uint16_t sgResult = _driver->SG_RESULT();
    return sgResult < _stallThreshold;
}

void TMC2209Driver::printDiagnostics() {
    Serial.println("\n═══════════════════════════════════════════════════════════════");
    Serial.println("                TMC2209 DIAGNOSTICS");
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
        
        // Connection test
        Serial.print("  Connection:   ");
        uint8_t connResult = _driver->test_connection();
        Serial.println(connResult == 0 ? "OK ✓" : "FAILED ✗");
        
        // Registers
        Serial.print("  GCONF:        0x"); Serial.println(_driver->GCONF(), HEX);
        Serial.print("  CHOPCONF:     0x"); Serial.println(_driver->CHOPCONF(), HEX);
        Serial.print("  DRV_STATUS:   0x"); Serial.println(_driver->DRV_STATUS(), HEX);
        Serial.print("  IOIN:         0x"); Serial.println(_driver->IOIN(), HEX);
        
        // Configuration readback
        Serial.println("\n  --- Configuration ---");
        Serial.print("  Current:      "); Serial.print(_driver->rms_current()); Serial.println(" mA");
        
        // Read microsteps from CHOPCONF
        uint32_t chopconf = _driver->CHOPCONF();
        uint8_t mres_raw = (chopconf >> 24) & 0x0F;
        uint16_t ms = mrestoMicrosteps(mres_raw);
        Serial.print("  Microsteps:   1/"); Serial.println(ms);
        
        Serial.print("  TOFF:         "); Serial.println(_driver->toff());
        Serial.print("  StealthChop:  "); Serial.println(_driver->en_spreadCycle() ? "Off (SpreadCycle)" : "On (Silent)");
        Serial.print("  SG Result:    "); Serial.println(_driver->SG_RESULT());
        Serial.print("  Stalling:     "); Serial.println(isStalling() ? "⚠️ YES!" : "No");
    } else {
        Serial.println("\n  Note: No UART diagnostics available in Step/Dir mode");
    }
    
    Serial.println("\n═══════════════════════════════════════════════════════════════\n");
}

bool TMC2209Driver::testConnection() {
    if (_driver == nullptr) return false;
    return _driver->test_connection() == 0;
}

// =============================================================================
// TMC2209-SPECIFIC METHODS
// =============================================================================

void TMC2209Driver::setStallThreshold(uint8_t threshold) {
    _stallThreshold = threshold;
    if (_driver != nullptr) {
        _driver->SGTHRS(threshold);
    }
}

uint16_t TMC2209Driver::getStallGuardResult() {
    if (_driver == nullptr) return 0;
    return _driver->SG_RESULT();
}

void TMC2209Driver::setStealthChop(bool enable) {
    if (_driver == nullptr) return;
    _driver->en_spreadCycle(!enable);  // Inverted: spreadCycle OFF = StealthChop ON
}

void TMC2209Driver::scanAddresses() {
    Serial.println("\nScanning for TMC2209 drivers...");
    
    for (uint8_t addr = 0; addr < 4; addr++) {
        TMC2209Stepper scanner(_serial, _rSense, addr);
        scanner.begin();
        delay(20);
        
        uint8_t result = scanner.test_connection();
        Serial.print("  Address ");
        Serial.print(addr);
        Serial.print(": ");
        
        if (result == 0) {
            Serial.println("TMC2209 found ✓");
        } else {
            Serial.println("No response");
        }
    }
    Serial.println();
}

uint32_t TMC2209Driver::readRegister(uint8_t reg) {
    // This is a simplified approach - for full register access
    // you would use _driver's internal methods
    // For now, provide common registers
    if (_driver == nullptr) return 0;
    
    switch (reg) {
        case 0x00: return _driver->GCONF();
        case 0x01: return _driver->GSTAT();
        case 0x06: return _driver->IOIN();
        case 0x6C: return _driver->CHOPCONF();
        case 0x6F: return _driver->DRV_STATUS();
        default:   return 0;
    }
}

// =============================================================================
// HELPER METHODS
// =============================================================================

uint8_t TMC2209Driver::microStepsToMRES(uint16_t ms) {
    return MotionMath::microStepsToMRES(ms);
}

uint16_t TMC2209Driver::mrestoMicrosteps(uint8_t mres) {
    return MotionMath::mresToMicrosteps(mres);
}
