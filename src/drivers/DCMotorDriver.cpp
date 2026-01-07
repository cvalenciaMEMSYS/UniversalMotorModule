/*
 * =============================================================================
 * DC MOTOR DRIVER - Implementation
 * =============================================================================
 * 
 * H-bridge PWM control for DC motors. Supports:
 *   - Direction control via IN1/IN2
 *   - Speed control via PWM duty cycle
 *   - Acceleration ramping
 *   - Timed moves (run for X milliseconds)
 * 
 * =============================================================================
 */

#include "DCMotorDriver.h"
#include "../core/MotionMath.h"

// =============================================================================
// CONSTRUCTORS
// =============================================================================

DCMotorDriver::DCMotorDriver()
    : DCMotorDriver(DC_IN1_PIN, DC_IN2_PIN) {
}

DCMotorDriver::DCMotorDriver(uint8_t in1Pin, uint8_t in2Pin)
    : _in1Pin(in1Pin)
    , _in2Pin(in2Pin)
    , _pwmChannel1(0)
    , _pwmChannel2(1)
    , _pwmFreq(DC_PWM_FREQ)
    , _pwmResolution(DC_PWM_RES)
    , _maxDuty((1 << DC_PWM_RES) - 1)  // 1023 for 10-bit
    , _enabled(false)
    , _currentSpeed(0)
    , _targetSpeed(0)
    , _virtualPosition(0)
    , _moving(false)
    , _moveStartTime(0)
    , _moveDuration(0)
    , _moveDirection(1)
    , _maxSpeedLimit(1.0f)
    , _lastUpdateTime(0)
    , _usingSCurve(false)
    , _scurveStartSpeed(0)
    , _scurvePeakSpeed(0)
    , _scurveTotalTime(0)
    , _scurveJerk(0)
    , _scurveMaxAccel(0) {
    
    // Initialize S-curve segment times
    for (int i = 0; i < 7; i++) {
        _scurveSegmentTime[i] = 0;
    }
    
    // Default profile: moderate acceleration
    _profile = AccelerationProfile::trapezoidal(
        DefaultMotorConfig::DC_MAX_SPEED * 1000,  // Arbitrary units
        DefaultMotorConfig::DC_ACCELERATION * 1000
    );
}

DCMotorDriver::~DCMotorDriver() {
    coast();
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool DCMotorDriver::init() {
    Serial.println("DC Motor Driver: Initializing...");
    
    // Configure PWM channels (ESP32-S3 Arduino Core 2.x API)
    // Channel setup: ledcSetup(channel, freq, resolution)
    ledcSetup(_pwmChannel1, _pwmFreq, _pwmResolution);
    ledcSetup(_pwmChannel2, _pwmFreq, _pwmResolution);
    
    // Attach pins to channels: ledcAttachPin(pin, channel)
    ledcAttachPin(_in1Pin, _pwmChannel1);
    ledcAttachPin(_in2Pin, _pwmChannel2);
    
    // Start with motor stopped (coast)
    ledcWrite(_pwmChannel1, 0);
    ledcWrite(_pwmChannel2, 0);
    
    _enabled = false;
    _currentSpeed = 0;
    _targetSpeed = 0;
    _virtualPosition = 0;
    _moving = false;
    
    Serial.println("DC Motor Driver: Ready");
    Serial.print("  PWM Frequency: ");
    Serial.print(_pwmFreq);
    Serial.println(" Hz");
    Serial.print("  PWM Resolution: ");
    Serial.print(_pwmResolution);
    Serial.print(" bits (0-");
    Serial.print(_maxDuty);
    Serial.println(")");
    
    // Enable the driver
    enable();
    
    return true;
}

// =============================================================================
// ENABLE / DISABLE
// =============================================================================

void DCMotorDriver::enable() {
    _enabled = true;
    // For H-bridge, "enabled" just means we allow speed commands
    // Actual motor state depends on current speed setting
}

void DCMotorDriver::disable() {
    coast();  // Free-wheel stop
    _enabled = false;
    _moving = false;
}

bool DCMotorDriver::isEnabled() const {
    return _enabled;
}

// =============================================================================
// MOTION CONTROL
// =============================================================================

void DCMotorDriver::move(int32_t steps) {
    // For DC motor: "steps" represents duration in milliseconds
    // Positive = forward, negative = reverse
    
    if (steps == 0) return;
    
    _moveDirection = steps > 0 ? 1 : -1;
    _moveDuration = abs(steps);
    _moveStartTime = millis();
    _moving = true;
    
    // Set target speed to max in the specified direction
    _targetSpeed = _moveDirection * _maxSpeedLimit;
    
    // Check if we should use S-curve profile for this timed move
    if (_profile.type == VelocityProfileType::S_CURVE && _profile.jerk > 0 && _profile.acceleration > 0) {
        planTimedSCurve(_moveDuration, _targetSpeed);
    } else {
        _usingSCurve = false;
        
        // If no acceleration, apply immediately
        if (_profile.acceleration <= 0) {
            _currentSpeed = _targetSpeed;
            applySpeed(_currentSpeed);
        }
    }
    
    _lastUpdateTime = millis();
}

void DCMotorDriver::moveTo(int32_t position) {
    // For DC motor: position represents speed setpoint (-1000 to +1000)
    // This allows the stepper API to control DC motor speed
    
    float speed = constrain(position, -1000, 1000) / 1000.0f;
    speed *= _maxSpeedLimit;
    
    _targetSpeed = speed;
    _virtualPosition = position;
    
    // If no acceleration, apply immediately
    if (_profile.acceleration <= 0) {
        _currentSpeed = _targetSpeed;
        applySpeed(_currentSpeed);
    }
    
    _moving = (_targetSpeed != 0);
    _lastUpdateTime = millis();
}

void DCMotorDriver::stop() {
    // Controlled stop with ramping
    _targetSpeed = 0;
    _moving = false;
    
    // If no acceleration, stop immediately
    if (_profile.acceleration <= 0) {
        _currentSpeed = 0;
        applySpeed(0);
    }
}

void DCMotorDriver::emergencyStop() {
    brake();
    _targetSpeed = 0;
    _currentSpeed = 0;
    _moving = false;
}

void DCMotorDriver::coast() {
    // Both pins LOW - motor free-wheels
    ledcWrite(_pwmChannel1, 0);
    ledcWrite(_pwmChannel2, 0);
    _currentSpeed = 0;
    _targetSpeed = 0;
}

void DCMotorDriver::brake() {
    // Both pins HIGH - motor brakes
    ledcWrite(_pwmChannel1, _maxDuty);
    ledcWrite(_pwmChannel2, _maxDuty);
    _currentSpeed = 0;
    _targetSpeed = 0;
}

bool DCMotorDriver::isMoving() const {
    return _moving || (_currentSpeed != 0);
}

void DCMotorDriver::update() {
    if (!_enabled) return;
    
    uint32_t now = millis();
    
    // Handle timed moves with S-curve
    if (_moving && _moveDuration > 0) {
        uint32_t elapsed = now - _moveStartTime;
        
        if (elapsed >= _moveDuration) {
            // Time's up - stop
            _targetSpeed = 0;
            _moving = false;
            _moveDuration = 0;
            _usingSCurve = false;
            _currentSpeed = 0;
            applySpeed(0);
        }
        else if (_usingSCurve) {
            // Compute S-curve speed based on elapsed time
            _currentSpeed = computeSCurveSpeed(elapsed);
            applySpeed(_currentSpeed);
        }
    }
    
    // Apply acceleration ramping (for non-S-curve moves)
    if (!_usingSCurve) {
        updateRamping();
    }
    
    _lastUpdateTime = now;
}

void DCMotorDriver::updateRamping() {
    if (_currentSpeed == _targetSpeed) return;
    
    uint32_t now = millis();
    float dt = (now - _lastUpdateTime) / 1000.0f;  // Convert to seconds
    
    if (dt <= 0) return;
    
    // For trapezoidal: use linear ramping
    // For constant: instant change
    if (_profile.type == VelocityProfileType::CONSTANT || _profile.acceleration <= 0) {
        _currentSpeed = _targetSpeed;
    } else {
        // Calculate speed change based on acceleration
        float accel = _profile.acceleration / 1000.0f;  // Convert from arbitrary units
        
        float speedDiff = _targetSpeed - _currentSpeed;
        float maxChange = accel * dt;
        
        if (abs(speedDiff) <= maxChange) {
            _currentSpeed = _targetSpeed;
        } else {
            _currentSpeed += (speedDiff > 0 ? maxChange : -maxChange);
        }
    }
    
    applySpeed(_currentSpeed);
}

void DCMotorDriver::planTimedSCurve(uint32_t duration, float targetSpeed) {
    // Plan a symmetric S-curve profile that fits within the given duration
    // Profile: accel ramp up -> cruise -> decel ramp down
    // 
    // For a symmetric move that starts and ends at 0 speed:
    // - 3 segments for acceleration (jerk+, const accel, jerk-)
    // - 1 segment for cruise
    // - 3 segments for deceleration (jerk-, const decel, jerk+)
    
    _usingSCurve = true;
    _scurveStartSpeed = 0;
    _scurvePeakSpeed = abs(targetSpeed);
    _scurveTotalTime = duration;
    
    // Get profile parameters (scale down from arbitrary units to 0-1 range per second)
    float jerk = _profile.jerk / 1000.0f;      // Units: speed/sec² 
    float maxAccel = _profile.acceleration / 1000.0f;  // Units: speed/sec
    
    if (jerk <= 0) jerk = 5.0f;   // Default jerk
    if (maxAccel <= 0) maxAccel = 2.0f;  // Default accel
    
    _scurveJerk = jerk;
    _scurveMaxAccel = maxAccel;
    
    // Calculate time to reach max acceleration: t_j = a_max / j
    float t_j = maxAccel / jerk;
    uint32_t t_j_ms = (uint32_t)(t_j * 1000.0f);
    
    // Velocity gained during jerk phase: v = j * t² / 2
    float jerkPhaseVel = jerk * t_j * t_j / 2.0f;
    
    // Calculate how much velocity we need to gain from start to peak
    float velocityToGain = _scurvePeakSpeed;
    
    // Total accel phase needs: 2 * jerkPhaseVel + constAccelVel = velocityToGain
    float constAccelVel = velocityToGain / 2.0f - jerkPhaseVel;  // Half for accel, half for decel
    
    float t_a = 0;
    uint32_t t_a_ms = 0;
    
    if (constAccelVel > 0) {
        t_a = constAccelVel / maxAccel;
        t_a_ms = (uint32_t)(t_a * 1000.0f);
    } else {
        // Short move - scale down jerk phases
        constAccelVel = 0;
        t_a_ms = 0;
        // Recalculate t_j for reduced velocity: jerkPhaseVel = velocityToGain / 4
        jerkPhaseVel = velocityToGain / 4.0f;
        if (jerkPhaseVel > 0) {
            t_j = sqrtf(2.0f * jerkPhaseVel / jerk);
            t_j_ms = (uint32_t)(t_j * 1000.0f);
        }
    }
    
    // Total time for accel side (3 segments)
    uint32_t accelSideTime = 2 * t_j_ms + t_a_ms;
    uint32_t decelSideTime = accelSideTime;  // Symmetric
    
    // Cruise time
    uint32_t cruiseTime = 0;
    if (duration > accelSideTime + decelSideTime) {
        cruiseTime = duration - accelSideTime - decelSideTime;
    } else {
        // Not enough time for full profile - scale everything down
        float scaleFactor = (float)duration / (float)(accelSideTime + decelSideTime);
        t_j_ms = (uint32_t)(t_j_ms * scaleFactor);
        t_a_ms = (uint32_t)(t_a_ms * scaleFactor);
        accelSideTime = 2 * t_j_ms + t_a_ms;
        decelSideTime = accelSideTime;
        cruiseTime = duration - accelSideTime - decelSideTime;
        
        // Recalculate peak speed for scaled profile
        t_j = t_j_ms / 1000.0f;
        t_a = t_a_ms / 1000.0f;
        jerkPhaseVel = jerk * t_j * t_j / 2.0f;
        constAccelVel = maxAccel * t_a;
        _scurvePeakSpeed = 2.0f * (jerkPhaseVel * 2.0f + constAccelVel);
        if (_scurvePeakSpeed > abs(targetSpeed)) {
            _scurvePeakSpeed = abs(targetSpeed);
        }
    }
    
    // Store segment durations (cumulative end times)
    _scurveSegmentTime[0] = t_j_ms;                               // Jerk+ (accel increasing)
    _scurveSegmentTime[1] = _scurveSegmentTime[0] + t_a_ms;       // Const accel
    _scurveSegmentTime[2] = _scurveSegmentTime[1] + t_j_ms;       // Jerk- (accel decreasing to 0)
    _scurveSegmentTime[3] = _scurveSegmentTime[2] + cruiseTime;   // Cruise
    _scurveSegmentTime[4] = _scurveSegmentTime[3] + t_j_ms;       // Jerk- (accel decreasing, decel starts)
    _scurveSegmentTime[5] = _scurveSegmentTime[4] + t_a_ms;       // Const decel
    _scurveSegmentTime[6] = duration;                              // Jerk+ (accel increasing to 0)
}

float DCMotorDriver::computeSCurveSpeed(uint32_t elapsedMs) {
    if (!_usingSCurve || _scurveTotalTime == 0) {
        return _targetSpeed;
    }
    
    // Find which segment we're in
    int segment = 0;
    for (int i = 0; i < 7; i++) {
        if (elapsedMs < _scurveSegmentTime[i]) {
            segment = i;
            break;
        }
        if (i == 6) segment = 6;
    }
    
    // Get segment start time and duration
    uint32_t segmentStart = (segment == 0) ? 0 : _scurveSegmentTime[segment - 1];
    uint32_t segmentDuration = _scurveSegmentTime[segment] - segmentStart;
    
    if (segmentDuration == 0) {
        // Skip zero-length segments
        if (segment <= 2) return 0;
        if (segment == 3) return _scurvePeakSpeed * _moveDirection;
        return 0;
    }
    
    float t = (elapsedMs - segmentStart) / 1000.0f;  // Time into segment (seconds)
    float segT = segmentDuration / 1000.0f;          // Segment duration (seconds)
    float progress = (float)(elapsedMs - segmentStart) / (float)segmentDuration;
    
    float speed = 0;
    
    switch (segment) {
        case 0:  // Jerk+ phase (acceleration increasing from 0)
            // v(t) = j * t² / 2, smooth start
            speed = _scurveJerk * t * t / 2.0f;
            break;
            
        case 1: {  // Constant acceleration phase
            // v(t) = v0 + a * t
            float v0 = _scurveJerk * segT * segT / 2.0f;  // Speed at end of seg 0
            speed = v0 + _scurveMaxAccel * t;
            break;
        }
            
        case 2: {  // Jerk- phase (acceleration decreasing to 0)
            // Smooth transition to cruise - use Hermite interpolation
            float v0 = computeSCurveSpeed(_scurveSegmentTime[1]);  // Speed at seg 1 end
            float v1 = _scurvePeakSpeed;
            float smoothProgress = progress * progress * (3.0f - 2.0f * progress);
            speed = abs(v0) + (v1 - abs(v0)) * smoothProgress;
            break;
        }
            
        case 3:  // Cruise phase
            speed = _scurvePeakSpeed;
            break;
            
        case 4: {  // Jerk- phase (start of deceleration)
            // Smooth transition from cruise
            float v0 = _scurvePeakSpeed;
            float v1 = computeSCurveSpeed(_scurveSegmentTime[5] - 1);  // Approximate target
            // Calculate target: symmetric to segment 2
            uint32_t seg2Duration = _scurveSegmentTime[2] - _scurveSegmentTime[1];
            float t_j = seg2Duration / 1000.0f;
            v1 = _scurvePeakSpeed - _scurveJerk * t_j * t_j / 2.0f;
            if (v1 < 0) v1 = 0;
            speed = v0 + (v1 - v0) * MotionMath::smoothSCurveProgress(progress);
            break;
        }
            
        case 5: {  // Constant deceleration phase
            float timeRemaining = (_scurveTotalTime - elapsedMs) / 1000.0f;
            // v = a * t_remaining (approximately)
            uint32_t seg6Duration = _scurveTotalTime - _scurveSegmentTime[5];
            float t_j = seg6Duration / 1000.0f;
            float endSpeed = _scurveJerk * t_j * t_j / 2.0f;
            float segDur = segmentDuration / 1000.0f;
            speed = endSpeed + _scurveMaxAccel * (segDur - t);
            break;
        }
            
        case 6: {  // Jerk+ phase (deceleration decreasing to 0)
            float timeRemaining = (_scurveTotalTime - elapsedMs) / 1000.0f;
            // v(t) = j * t_remaining² / 2, smooth end
            speed = _scurveJerk * timeRemaining * timeRemaining / 2.0f;
            break;
        }
    }
    
    // Clamp and apply direction
    if (speed < 0) speed = 0;
    if (speed > _scurvePeakSpeed) speed = _scurvePeakSpeed;
    
    return speed * _moveDirection;
}

void DCMotorDriver::applySpeed(float speed) {
    // Clamp speed to valid range
    speed = constrain(speed, -1.0f, 1.0f);
    
    // Calculate duty cycle
    uint16_t duty = (uint16_t)(abs(speed) * _maxDuty);
    
    if (speed > 0.01f) {
        // Forward: IN1 = PWM, IN2 = LOW
        ledcWrite(_pwmChannel1, duty);
        ledcWrite(_pwmChannel2, 0);
    }
    else if (speed < -0.01f) {
        // Reverse: IN1 = LOW, IN2 = PWM
        ledcWrite(_pwmChannel1, 0);
        ledcWrite(_pwmChannel2, duty);
    }
    else {
        // Stopped: coast mode
        ledcWrite(_pwmChannel1, 0);
        ledcWrite(_pwmChannel2, 0);
    }
}

// =============================================================================
// SPEED CONTROL
// =============================================================================

void DCMotorDriver::setSpeed(float speed) {
    speed = constrain(speed, -1.0f, 1.0f);
    speed *= _maxSpeedLimit;
    
    _targetSpeed = speed;
    _moving = (speed != 0);
    
    // If no acceleration, apply immediately
    if (_profile.acceleration <= 0) {
        _currentSpeed = speed;
        applySpeed(speed);
    }
}

float DCMotorDriver::getSpeed() const {
    return _currentSpeed;
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void DCMotorDriver::setMaxSpeed(float stepsPerSecond) {
    // For DC motor: interpret as max duty cycle percentage (0-100)
    _maxSpeedLimit = constrain(stepsPerSecond / 100.0f, 0.0f, 1.0f);
}

void DCMotorDriver::setCurrent(uint16_t runMA, uint16_t holdMA) {
    // DC motors: current is determined by motor + voltage, not controllable
    Serial.println("DC Motor: Current limit not applicable");
}

void DCMotorDriver::setMicrosteps(uint16_t microsteps) {
    // DC motors: no microstepping
    Serial.println("DC Motor: Microstepping not applicable");
}

void DCMotorDriver::setAccelerationProfile(const AccelerationProfile& profile) {
    _profile = profile;
}

// =============================================================================
// POSITION (Virtual for API compatibility)
// =============================================================================

int32_t DCMotorDriver::getPosition() const {
    // Return virtual position for API compatibility
    return _virtualPosition;
}

void DCMotorDriver::setPosition(int32_t position) {
    _virtualPosition = position;
}

void DCMotorDriver::home(int8_t direction) {
    // DC motors: no position feedback, just stop and reset virtual position
    stop();
    _virtualPosition = 0;
    Serial.println("DC Motor: Virtual position reset to 0");
}

// =============================================================================
// STATUS
// =============================================================================

MotorStatus DCMotorDriver::getStatus() {
    MotorStatus status;
    
    status.enabled = _enabled;
    status.moving = isMoving();
    status.stalling = false;
    
    status.position = _virtualPosition;
    status.targetPosition = (int32_t)(_targetSpeed * 1000);  // Speed as "position"
    
    status.currentMA = 0;  // Unknown
    status.loadValue = 0;  // No load sensing
    status.currentSpeed = _currentSpeed * 1000;  // Scale for display
    
    status.errorFlags = MotorError::NONE;
    
    return status;
}

void DCMotorDriver::printDiagnostics() {
    Serial.println("\n═══════════════════════════════════════════════════════════════");
    Serial.println("                   DC MOTOR DIAGNOSTICS");
    Serial.println("═══════════════════════════════════════════════════════════════\n");
    
    Serial.print("  Enabled:       ");
    Serial.println(_enabled ? "Yes" : "No");
    
    Serial.print("  Speed:         ");
    Serial.print(_currentSpeed * 100.0f, 1);
    Serial.println("%");
    
    Serial.print("  Target Speed:  ");
    Serial.print(_targetSpeed * 100.0f, 1);
    Serial.println("%");
    
    Serial.print("  Direction:     ");
    if (_currentSpeed > 0.01f) Serial.println("Forward");
    else if (_currentSpeed < -0.01f) Serial.println("Reverse");
    else Serial.println("Stopped");
    
    Serial.print("  Max Limit:     ");
    Serial.print(_maxSpeedLimit * 100.0f, 1);
    Serial.println("%");
    
    Serial.print("  PWM Frequency: ");
    Serial.print(_pwmFreq);
    Serial.println(" Hz");
    
    if (_moving && _moveDuration > 0) {
        uint32_t elapsed = millis() - _moveStartTime;
        uint32_t remaining = (_moveDuration > elapsed) ? (_moveDuration - elapsed) : 0;
        Serial.print("  Move remaining: ");
        Serial.print(remaining);
        Serial.println(" ms");
    }
    
    Serial.println("\n═══════════════════════════════════════════════════════════════\n");
}
