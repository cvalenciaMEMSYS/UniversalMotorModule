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
// Note: MotionMath.h removed - using simple acceleration ramping now

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
    , _accelerationRate(1.0f) {  // Default: 1.0 speed units/sec
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
    
    // If no acceleration, apply immediately
    if (_accelerationRate <= 0) {
        _currentSpeed = _targetSpeed;
        applySpeed(_currentSpeed);
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
    if (_accelerationRate <= 0) {
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
    if (_accelerationRate <= 0) {
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
    
    // Handle timed moves
    if (_moving && _moveDuration > 0) {
        uint32_t elapsed = now - _moveStartTime;
        
        if (elapsed >= _moveDuration) {
            // Time's up - stop
            _targetSpeed = 0;
            _moving = false;
            _moveDuration = 0;
            _currentSpeed = 0;
            applySpeed(0);
        }
    }
    
    // Apply acceleration ramping
    updateRamping();
    
    _lastUpdateTime = now;
}

void DCMotorDriver::updateRamping() {
    if (_currentSpeed == _targetSpeed) return;
    
    uint32_t now = millis();
    float dt = (now - _lastUpdateTime) / 1000.0f;  // Convert to seconds
    
    if (dt <= 0) return;
    
    // Simple linear ramping
    if (_accelerationRate <= 0) {
        _currentSpeed = _targetSpeed;
    } else {
        float speedDiff = _targetSpeed - _currentSpeed;
        float maxChange = _accelerationRate * dt;
        
        if (abs(speedDiff) <= maxChange) {
            _currentSpeed = _targetSpeed;
        } else {
            _currentSpeed += (speedDiff > 0 ? maxChange : -maxChange);
        }
    }
    
    applySpeed(_currentSpeed);
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
    if (_accelerationRate <= 0) {
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

void DCMotorDriver::setAcceleration(float accelStepsPerSecondSquared) {
    _accelerationRate = accelStepsPerSecondSquared;
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
