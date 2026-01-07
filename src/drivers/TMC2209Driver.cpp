/*
 * =============================================================================
 * TMC2209 DRIVER - Implementation
 * =============================================================================
 */

#include "TMC2209Driver.h"

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
        Serial.println("TMC2209: UART connection failed!");
        return false;
    }
    
    // Configure driver with current settings
    configureDriver();
    
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
    configureDriver();
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
    // For constant velocity, just stop
    // TODO: For acceleration profiles, initiate deceleration
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
    delayMicroseconds(2);  // Minimum pulse width
    digitalWrite(_stepPin, LOW);
    
    // Update position based on direction
    if (digitalRead(_dirPin) == HIGH) {
        _position++;
    } else {
        _position--;
    }
}

void TMC2209Driver::calculateStepInterval() {
    // Convert current speed (steps/sec) to interval (microseconds)
    float speed = _currentSpeed;
    if (speed <= 0) speed = _maxSpeed;  // Fallback to max speed
    if (speed <= 0) speed = 1;          // Safety
    
    _stepInterval = (uint32_t)(1000000.0f / speed);
    if (_stepInterval < 10) _stepInterval = 10;  // Minimum 10µs (100kHz max)
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
    
    // Steps to reach max speed: v² = 2ad → d = v²/(2a)
    int32_t stepsToMaxSpeed = (int32_t)((maxSpeed * maxSpeed) / (2.0f * accel));
    
    if (2 * stepsToMaxSpeed >= _totalMoveSteps) {
        // Triangular profile - we never reach max speed
        // Peak at halfway point
        _isTriangular = true;
        _accelSteps = _totalMoveSteps / 2;
        _decelSteps = _totalMoveSteps - _accelSteps;
    } else {
        // Full trapezoidal - accel, cruise, decel
        _isTriangular = false;
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
    
    float accel = _profile.acceleration;
    float maxSpeed = _profile.maxSpeed;
    
    if (stepsDone < _accelSteps) {
        // Accelerating phase
        // v = sqrt(2 * a * d) where d = steps done + 1 (look ahead)
        _currentSpeed = sqrtf(2.0f * accel * (float)(stepsDone + 1));
        _currentSpeed = min(_currentSpeed, maxSpeed);
    } 
    else if (stepsRemaining <= _decelSteps) {
        // Decelerating phase  
        // v = sqrt(2 * a * d) where d = steps remaining
        _currentSpeed = sqrtf(2.0f * accel * (float)stepsRemaining);
    }
    else {
        // Cruising at max speed
        _currentSpeed = maxSpeed;
    }
    
    // Enforce minimum speed to prevent stalling
    if (_currentSpeed < 50.0f) _currentSpeed = 50.0f;
    
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
    
    if (jerk <= 0 || maxAccel <= 0) {
        // Fall back to trapezoidal if jerk not configured
        planTrapezoidalMotion();
        return;
    }
    
    // Time to reach max acceleration with given jerk: t_j = a_max / j
    float t_j = maxAccel / jerk;
    
    // Distance covered during one jerk phase: d = j * t³ / 6
    float jerkPhaseDist = jerk * t_j * t_j * t_j / 6.0f;
    
    // Velocity gained during one jerk phase: v = j * t² / 2
    float jerkPhaseVel = jerk * t_j * t_j / 2.0f;
    
    // Velocity gained during constant accel phase to reach max speed
    // Total velocity from accel side: 2 * jerkPhaseVel + v_const_accel = maxSpeed
    float constAccelVel = maxSpeed - 2.0f * jerkPhaseVel;
    
    float constAccelDist = 0;
    float t_a = 0;
    
    if (constAccelVel > 0) {
        // We have a constant acceleration phase
        // Time at constant accel: t_a = v / a
        t_a = constAccelVel / maxAccel;
        // Distance: d = v0*t + 0.5*a*t² where v0 = jerkPhaseVel
        constAccelDist = jerkPhaseVel * t_a + 0.5f * maxAccel * t_a * t_a;
    } else {
        // Max speed is low - we never reach max accel
        // Simplified: use triangular jerk profile
        constAccelVel = 0;
        constAccelDist = 0;
    }
    
    // Total distance for acceleration side (3 segments)
    // Seg 0 + Seg 1 + Seg 2 (Seg 2 starts at jerkPhaseVel + constAccelVel, ends at maxSpeed)
    float accelSideDist = jerkPhaseDist + constAccelDist + jerkPhaseDist + 
                          (jerkPhaseVel + constAccelVel) * t_j;  // Extra distance in Seg 2
    
    // Deceleration side is symmetric
    float decelSideDist = accelSideDist;
    
    // Check if we have room for cruise phase
    float totalAccelDecelDist = accelSideDist + decelSideDist;
    float cruiseDist = (float)_totalMoveSteps - totalAccelDecelDist;
    
    if (cruiseDist < 0) {
        // Not enough room for full S-curve - fall back to trapezoidal
        // (A proper implementation would scale down the profile)
        planTrapezoidalMotion();
        return;
    }
    
    // Calculate segment end positions (cumulative steps from start)
    // All distances are now in steps
    int32_t pos = 0;
    
    // Segment 0: Jerk+ phase
    _scurveSegmentEnd[0] = pos + (int32_t)jerkPhaseDist;
    _scurveVelocity[0] = 50.0f;  // Start speed
    _scurveVelocity[1] = jerkPhaseVel;
    _scurveAccel[0] = 0;  // Accel starts at 0
    _jerkSign[0] = 1;     // Positive jerk
    pos = _scurveSegmentEnd[0];
    
    // Segment 1: Constant acceleration
    _scurveSegmentEnd[1] = pos + (int32_t)constAccelDist;
    _scurveVelocity[2] = jerkPhaseVel + constAccelVel;
    _scurveAccel[1] = maxAccel;
    _jerkSign[1] = 0;     // No jerk change
    pos = _scurveSegmentEnd[1];
    
    // Segment 2: Jerk- phase (approaching cruise)
    _scurveSegmentEnd[2] = pos + (int32_t)(jerkPhaseDist + (jerkPhaseVel + constAccelVel) * t_j);
    _scurveVelocity[3] = maxSpeed;
    _scurveAccel[2] = maxAccel;  // Starts at max, decreases
    _jerkSign[2] = -1;    // Negative jerk
    pos = _scurveSegmentEnd[2];
    
    // Segment 3: Cruise
    _scurveSegmentEnd[3] = pos + (int32_t)cruiseDist;
    _scurveVelocity[4] = maxSpeed;
    _scurveAccel[3] = 0;
    _jerkSign[3] = 0;
    pos = _scurveSegmentEnd[3];
    
    // Segment 4: Jerk- phase (starting decel)
    _scurveSegmentEnd[4] = pos + (int32_t)(jerkPhaseDist + maxSpeed * t_j - jerkPhaseVel * t_j);
    _scurveVelocity[5] = maxSpeed - jerkPhaseVel;
    _scurveAccel[4] = 0;  // Starts at 0, goes negative
    _jerkSign[4] = -1;
    pos = _scurveSegmentEnd[4];
    
    // Segment 5: Constant deceleration
    _scurveSegmentEnd[5] = pos + (int32_t)constAccelDist;
    _scurveVelocity[6] = jerkPhaseVel;
    _scurveAccel[5] = -maxAccel;
    _jerkSign[5] = 0;
    pos = _scurveSegmentEnd[5];
    
    // Segment 6: Jerk+ phase (stopping)
    _scurveSegmentEnd[6] = _totalMoveSteps;
    _scurveVelocity[7] = 50.0f;  // End speed
    _scurveAccel[6] = -maxAccel;  // Starts at -max, goes to 0
    _jerkSign[6] = 1;
    
    _currentSpeed = 50.0f;  // Start at minimum speed
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
        if (i == 6) segment = 6;  // In last segment
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
    
    // Linear interpolation within segment (simplified)
    // A more accurate implementation would use proper kinematic equations
    float progress = (float)stepsInSegment / (float)segmentLength;
    float v0 = _scurveVelocity[segment];
    float v1 = _scurveVelocity[segment + 1];
    
    _currentSpeed = v0 + (v1 - v0) * progress;
    
    // Enforce minimum speed
    if (_currentSpeed < 50.0f) _currentSpeed = 50.0f;
    if (_currentSpeed > _profile.maxSpeed) _currentSpeed = _profile.maxSpeed;
    
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
    
    if (_driver != nullptr) {
        _driver->rms_current(_runCurrentMA);
        _driver->ihold(_holdCurrentMA > 0 ? (_holdCurrentMA * 31 / _runCurrentMA) : 0);
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
    
    if (_driver != nullptr) {
        // Use direct CHOPCONF write for fullstep support
        uint8_t mres = microStepsToMRES(microsteps);
        uint32_t chopconf = _driver->CHOPCONF();
        chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
        _driver->CHOPCONF(chopconf);
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
    // Use StallGuard to detect home position
    Serial.println("Homing...");
    
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
    if (_driver == nullptr) return false;
    
    // StallGuard result drops when motor is loaded
    uint16_t sgResult = _driver->SG_RESULT();
    return sgResult < _stallThreshold;
}

void TMC2209Driver::printDiagnostics() {
    Serial.println("\n=== TMC2209 Diagnostics ===");
    
    if (_driver == nullptr) {
        Serial.println("ERROR: Driver not initialized!");
        return;
    }
    
    // Connection test
    Serial.print("Connection: ");
    uint8_t connResult = _driver->test_connection();
    Serial.println(connResult == 0 ? "OK" : "FAILED");
    
    // Registers
    Serial.print("GCONF:      0x"); Serial.println(_driver->GCONF(), HEX);
    Serial.print("CHOPCONF:   0x"); Serial.println(_driver->CHOPCONF(), HEX);
    Serial.print("DRV_STATUS: 0x"); Serial.println(_driver->DRV_STATUS(), HEX);
    Serial.print("IOIN:       0x"); Serial.println(_driver->IOIN(), HEX);
    
    // Configuration readback
    Serial.println("\nConfiguration:");
    Serial.print("  Current:     "); Serial.print(_driver->rms_current()); Serial.println(" mA");
    
    // Read microsteps from CHOPCONF
    uint32_t chopconf = _driver->CHOPCONF();
    uint8_t mres_raw = (chopconf >> 24) & 0x0F;
    uint16_t ms = mrestoMicrosteps(mres_raw);
    Serial.print("  Microsteps:  "); Serial.println(ms);
    
    Serial.print("  TOFF:        "); Serial.println(_driver->toff());
    Serial.print("  StealthChop: "); Serial.println(_driver->en_spreadCycle() ? "Off" : "On");
    
    // Status
    Serial.println("\nStatus:");
    Serial.print("  Enabled:     "); Serial.println(_enabled ? "Yes" : "No");
    Serial.print("  Position:    "); Serial.println(_position);
    Serial.print("  Moving:      "); Serial.println(_moving ? "Yes" : "No");
    Serial.print("  SG Result:   "); Serial.println(_driver->SG_RESULT());
    
    Serial.println("===========================\n");
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
    switch (ms) {
        case 256: return 0;
        case 128: return 1;
        case 64:  return 2;
        case 32:  return 3;
        case 16:  return 4;
        case 8:   return 5;
        case 4:   return 6;
        case 2:   return 7;
        case 1:   return 8;  // Fullstep - TMCStepper library doesn't support this via microsteps()
        default:  return 4;  // Default to 16
    }
}

uint16_t TMC2209Driver::mrestoMicrosteps(uint8_t mres) {
    if (mres >= 8) return 1;  // MRES 8 = fullstep
    return 256 >> mres;
}
