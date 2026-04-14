/*
 * =============================================================================
 * HARCO H-BRIDGE DRIVER - Implementation
 * =============================================================================
 * 
 * H-bridge PWM control for HarCo custom modules (DRV88xx series).
 * Plugs into the stepper driver socket using remapped pins.
 * 
 * Supports:
 *   - Direction control via IN1/IN2 on stepper socket
 *   - Speed control via PWM duty cycle
 *   - nSLEEP pin for power management (all modules)
 *   - Optional Enable pin (DRV8839 only)
 *   - Acceleration ramping
 *   - Timed moves (run for X milliseconds)
 * 
 * =============================================================================
 */

#include "HarCoHBridgeDriver.h"

// =============================================================================
// CONSTRUCTORS
// =============================================================================

HarCoHBridgeDriver::HarCoHBridgeDriver(bool hasEnablePin)
    : HarCoHBridgeDriver(HARCO_IN1_PIN, HARCO_IN2_PIN, HARCO_SLEEP_PIN,
                          hasEnablePin ? HARCO_EN_PIN : (uint8_t)255) {
}

HarCoHBridgeDriver::HarCoHBridgeDriver(uint8_t in1Pin, uint8_t in2Pin, uint8_t sleepPin, uint8_t enPin)
    : _in1Pin(in1Pin)
    , _in2Pin(in2Pin)
    , _sleepPin(sleepPin)
    , _enPin(enPin)
    , _hasEnablePin(enPin != 255)
    , _pwmChannel1(2)      // Use channels 2,3 to avoid conflict with onboard DC motor channels 0,1
    , _pwmChannel2(3)
    , _pwmFreq(DC_PWM_FREQ)
    , _pwmResolution(DC_PWM_RES)
    , _maxDuty((1 << DC_PWM_RES) - 1)
    , _enabled(false)
    , _awake(false)
    , _sleepInverted(false)
    , _enableInverted(false)
    , _currentSpeed(0)
    , _targetSpeed(0)
    , _virtualPosition(0)
    , _moving(false)
    , _moveStartTime(0)
    , _moveDuration(0)
    , _moveDirection(1)
    , _maxSpeedLimit(1.0f)
    , _lastUpdateTime(0)
    , _accelerationRate(1.0f) {
}

HarCoHBridgeDriver::~HarCoHBridgeDriver() {
    coast();
    setSleep(false);  // Put module to sleep on cleanup
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool HarCoHBridgeDriver::init() {
    Serial.println("HarCo H-Bridge Driver: Initializing...");
    
    // Configure nSLEEP pin — wake up the module
    pinMode(_sleepPin, OUTPUT);
    digitalWrite(_sleepPin, _sleepInverted ? LOW : HIGH);  // Awake
    _awake = true;
    
    // Configure Enable pin if present (DRV8839)
    if (_hasEnablePin) {
        pinMode(_enPin, OUTPUT);
        digitalWrite(_enPin, _enableInverted ? HIGH : LOW);  // Enabled
        Serial.print("  Enable Pin: GPIO ");
        Serial.println(_enPin);
    }
    
    // Brief delay for module to wake up after nSLEEP assertion
    delay(5);
    
    // Configure PWM channels (ESP32-S3 Arduino Core 2.x API)
    ledcSetup(_pwmChannel1, _pwmFreq, _pwmResolution);
    ledcSetup(_pwmChannel2, _pwmFreq, _pwmResolution);
    
    // Attach pins to channels
    ledcAttachPin(_in1Pin, _pwmChannel1);
    ledcAttachPin(_in2Pin, _pwmChannel2);
    
    // Start with motor stopped (coast)
    ledcWrite(_pwmChannel1, 0);
    ledcWrite(_pwmChannel2, 0);
    
    _enabled = true;
    _currentSpeed = 0;
    _targetSpeed = 0;
    _virtualPosition = 0;
    _moving = false;
    
    Serial.println("HarCo H-Bridge Driver: Ready");
    Serial.print("  IN1 Pin: GPIO ");
    Serial.println(_in1Pin);
    Serial.print("  IN2 Pin: GPIO ");
    Serial.println(_in2Pin);
    Serial.print("  nSLEEP Pin: GPIO ");
    Serial.println(_sleepPin);
    Serial.print("  Has Enable: ");
    Serial.println(_hasEnablePin ? "Yes" : "No");
    Serial.print("  PWM Frequency: ");
    Serial.print(_pwmFreq);
    Serial.println(" Hz");
    Serial.print("  PWM Resolution: ");
    Serial.print(_pwmResolution);
    Serial.print(" bits (0-");
    Serial.print(_maxDuty);
    Serial.println(")");
    Serial.println("  Vm = Vcc = 3.3V");
    Serial.println("HarCo H-Bridge Driver: Ready - motor coasting");
    
    return true;
}

// =============================================================================
// ENABLE / DISABLE
// =============================================================================

void HarCoHBridgeDriver::enable() {
    _enabled = true;
    
    // Wake module if sleeping
    if (!_awake) {
        setSleep(true);
    }
    
    // Assert Enable pin if present
    if (_hasEnablePin) {
        digitalWrite(_enPin, _enableInverted ? HIGH : LOW);  // Enabled
    }
    
    Serial.println("HarCo: Enabled");
}

void HarCoHBridgeDriver::disable() {
    coast();
    _enabled = false;
    _moving = false;
    
    // Deassert Enable pin if present
    if (_hasEnablePin) {
        digitalWrite(_enPin, _enableInverted ? LOW : HIGH);  // Disabled
    }
    
    Serial.println("HarCo: Disabled (coasting)");
}

bool HarCoHBridgeDriver::isEnabled() const {
    return _enabled;
}

// =============================================================================
// SLEEP CONTROL
// =============================================================================

void HarCoHBridgeDriver::setSleep(bool awake) {
    _awake = awake;
    digitalWrite(_sleepPin, (awake != _sleepInverted) ? HIGH : LOW);
    
    if (awake) {
        delay(5);  // Wake-up time for DRV88xx modules
        Serial.printf("HarCo: nSLEEP=%s (awake)%s\n", 
                      (awake != _sleepInverted) ? "HIGH" : "LOW",
                      _sleepInverted ? " [inverted]" : "");
    } else {
        _currentSpeed = 0;
        _targetSpeed = 0;
        _moving = false;
        Serial.printf("HarCo: nSLEEP=%s (sleeping)%s\n",
                      (awake != _sleepInverted) ? "HIGH" : "LOW",
                      _sleepInverted ? " [inverted]" : "");
    }
}

bool HarCoHBridgeDriver::isAwake() const {
    return _awake;
}

// =============================================================================
// MOTION CONTROL
// =============================================================================

void HarCoHBridgeDriver::move(int32_t steps) {
    // "steps" represents duration in milliseconds
    if (steps == 0) return;
    
    _moveDirection = steps > 0 ? 1 : -1;
    _moveDuration = abs(steps);
    _moveStartTime = millis();
    _moving = true;
    
    _targetSpeed = _moveDirection * _maxSpeedLimit;
    
    if (_accelerationRate <= 0) {
        _currentSpeed = _targetSpeed;
        applySpeed(_currentSpeed);
    }
    
    _lastUpdateTime = millis();
}

void HarCoHBridgeDriver::moveTo(int32_t position) {
    // position represents speed percentage (-100 to +100)
    float speed = constrain(position, -100, 100) / 100.0f;
    speed *= _maxSpeedLimit;
    
    _targetSpeed = speed;
    _virtualPosition = position;
    
    if (_accelerationRate <= 0) {
        _currentSpeed = _targetSpeed;
        applySpeed(_currentSpeed);
    }
    
    _moving = (_targetSpeed != 0);
    _lastUpdateTime = millis();
    
    Serial.printf("HarCo: Speed set to %d%% (target: %.2f)\n", position, _targetSpeed);
}

void HarCoHBridgeDriver::stop() {
    _targetSpeed = 0;
    _moving = false;
    
    if (_accelerationRate <= 0) {
        _currentSpeed = 0;
        applySpeed(0);
    }
}

void HarCoHBridgeDriver::emergencyStop() {
    brake();
    _targetSpeed = 0;
    _currentSpeed = 0;
    _moving = false;
}

void HarCoHBridgeDriver::runForward() {
    _targetSpeed = _maxSpeedLimit;
    _moving = true;
    _moveDuration = 0;  // Run indefinitely
    
    if (_accelerationRate <= 0) {
        _currentSpeed = _targetSpeed;
        applySpeed(_currentSpeed);
    }
    
    _lastUpdateTime = millis();
    Serial.println("HarCo: Running forward");
}

void HarCoHBridgeDriver::runBackward() {
    _targetSpeed = -_maxSpeedLimit;
    _moving = true;
    _moveDuration = 0;
    
    if (_accelerationRate <= 0) {
        _currentSpeed = _targetSpeed;
        applySpeed(_currentSpeed);
    }
    
    _lastUpdateTime = millis();
    Serial.println("HarCo: Running backward");
}

void HarCoHBridgeDriver::coast() {
    ledcWrite(_pwmChannel1, 0);
    ledcWrite(_pwmChannel2, 0);
    _currentSpeed = 0;
    _targetSpeed = 0;
    _moving = false;
    Serial.println("HarCo: Coast (IN1=0, IN2=0) - freewheeling");
}

void HarCoHBridgeDriver::brake() {
    ledcWrite(_pwmChannel1, _maxDuty);
    ledcWrite(_pwmChannel2, _maxDuty);
    _currentSpeed = 0;
    _targetSpeed = 0;
    _moving = false;
    Serial.printf("HarCo: Brake (IN1=%d, IN2=%d) - motor locked\n", _maxDuty, _maxDuty);
}

bool HarCoHBridgeDriver::isMoving() const {
    return _moving || (_currentSpeed != 0);
}

void HarCoHBridgeDriver::update() {
    uint32_t now = millis();
    
    // Handle timed moves
    if (_moving && _moveDuration > 0) {
        uint32_t elapsed = now - _moveStartTime;
        
        if (elapsed >= _moveDuration) {
            _targetSpeed = 0;
            _moving = false;
            _moveDuration = 0;
            _currentSpeed = 0;
            applySpeed(0);
        }
    }
    
    updateRamping();
    _lastUpdateTime = now;
}

void HarCoHBridgeDriver::updateRamping() {
    if (_currentSpeed == _targetSpeed) return;
    
    uint32_t now = millis();
    float dt = (now - _lastUpdateTime) / 1000.0f;
    
    if (dt <= 0) return;
    
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

void HarCoHBridgeDriver::applySpeed(float speed) {
    speed = constrain(speed, -1.0f, 1.0f);
    
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

void HarCoHBridgeDriver::setSpeed(float speed) {
    speed = constrain(speed, -1.0f, 1.0f);
    speed *= _maxSpeedLimit;
    
    _targetSpeed = speed;
    _moving = (speed != 0);
    
    if (_accelerationRate <= 0) {
        _currentSpeed = speed;
        applySpeed(speed);
    }
}

float HarCoHBridgeDriver::getSpeed() const {
    return _currentSpeed;
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void HarCoHBridgeDriver::setMaxSpeed(float stepsPerSecond) {
    _maxSpeedLimit = constrain(stepsPerSecond / 100.0f, 0.0f, 1.0f);
}

void HarCoHBridgeDriver::setCurrent(uint16_t runMA, uint16_t holdMA) {
    (void)runMA;
    (void)holdMA;
    Serial.println("HarCo: Current limit is hardware-determined (Vm = 3.3V)");
}

void HarCoHBridgeDriver::setMicrosteps(uint16_t microsteps) {
    (void)microsteps;
    Serial.println("HarCo: Microstepping not applicable (DC motor)");
}

void HarCoHBridgeDriver::setAcceleration(float accelStepsPerSecondSquared) {
    _accelerationRate = accelStepsPerSecondSquared;
}

// =============================================================================
// POSITION (Virtual for API compatibility)
// =============================================================================

int32_t HarCoHBridgeDriver::getPosition() const {
    return _virtualPosition;
}

void HarCoHBridgeDriver::setPosition(int32_t position) {
    _virtualPosition = position;
}

void HarCoHBridgeDriver::home(int8_t direction) {
    (void)direction;
    stop();
    _virtualPosition = 0;
    Serial.println("HarCo: Virtual position reset to 0");
}

// =============================================================================
// STATUS
// =============================================================================

MotorStatus HarCoHBridgeDriver::getStatus() {
    MotorStatus status;
    
    status.enabled = _enabled;
    status.moving = isMoving();
    status.stalling = false;
    
    status.position = _virtualPosition;
    status.targetPosition = (int32_t)(_targetSpeed * 100);
    
    status.currentMA = 0;
    status.loadValue = 0;
    status.currentSpeed = _currentSpeed * 100;
    
    status.errorFlags = MotorError::NONE;
    
    return status;
}

void HarCoHBridgeDriver::printDiagnostics() {
    Serial.println("\n═══════════════════════════════════════════════════════════════");
    Serial.println("               HARCO H-BRIDGE DIAGNOSTICS");
    Serial.println("═══════════════════════════════════════════════════════════════\n");
    
    Serial.print("  Enabled:       ");
    Serial.println(_enabled ? "Yes" : "No");
    
    Serial.print("  Awake:         ");
    Serial.println(_awake ? "Yes (nSLEEP=HIGH)" : "No (nSLEEP=LOW)");
    
    Serial.print("  Has Enable:    ");
    Serial.println(_hasEnablePin ? "Yes (DRV8839)" : "No");
    
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
    
    Serial.println("  Supply:        Vm = Vcc = 3.3V");
    
    Serial.print("  IN1 (GPIO ");
    Serial.print(_in1Pin);
    Serial.println(")");
    Serial.print("  IN2 (GPIO ");
    Serial.print(_in2Pin);
    Serial.println(")");
    Serial.print("  nSLEEP (GPIO ");
    Serial.print(_sleepPin);
    Serial.print("): ");
    Serial.print(_awake ? "AWAKE" : "SLEEPING");
    if (_sleepInverted) Serial.print(" [inverted]");
    Serial.println();
    if (_hasEnablePin) {
        Serial.print("  EN (GPIO ");
        Serial.print(_enPin);
        Serial.print("): ");
        Serial.print(_enabled ? "ENABLED" : "DISABLED");
        if (_enableInverted) Serial.print(" [inverted]");
        Serial.println();
    }
    
    if (_moving && _moveDuration > 0) {
        uint32_t elapsed = millis() - _moveStartTime;
        uint32_t remaining = (_moveDuration > elapsed) ? (_moveDuration - elapsed) : 0;
        Serial.print("  Move remaining: ");
        Serial.print(remaining);
        Serial.println(" ms");
    }
    
    Serial.println("\n═══════════════════════════════════════════════════════════════\n");
}

// =============================================================================
// QUERY METHODS
// =============================================================================

int32_t HarCoHBridgeDriver::getTargetPosition() const {
    return (int32_t)(_targetSpeed * 100);
}

int32_t HarCoHBridgeDriver::getActualSpeed() const {
    return (int32_t)(_currentSpeed * 100);
}

uint8_t HarCoHBridgeDriver::getRampState() const {
    if (!_enabled || (!_moving && _currentSpeed == 0)) {
        return 0;  // IDLE
    }
    if (_currentSpeed == _targetSpeed) {
        return 1;  // COAST (constant speed)
    }
    if (abs(_currentSpeed) < abs(_targetSpeed)) {
        return 2;  // ACCELERATE
    }
    return 4;  // DECELERATE
}

// =============================================================================
// LOGIC INVERSION
// =============================================================================

void HarCoHBridgeDriver::invertSleepLogic(bool inverted) {
    _sleepInverted = inverted;
    // Re-apply current sleep state with new logic
    setSleep(_awake);
    Serial.printf("HarCo: Sleep logic %s (awake = %s)\n",
                  inverted ? "INVERTED" : "NORMAL",
                  inverted ? "LOW" : "HIGH");
}

void HarCoHBridgeDriver::invertEnableLogic(bool inverted) {
    _enableInverted = inverted;
    // Re-apply current enable state with new logic
    if (_hasEnablePin) {
        if (_enabled) {
            digitalWrite(_enPin, _enableInverted ? HIGH : LOW);
        } else {
            digitalWrite(_enPin, _enableInverted ? LOW : HIGH);
        }
    }
    Serial.printf("HarCo: Enable logic %s (enabled = %s)\n",
                  inverted ? "INVERTED" : "NORMAL",
                  inverted ? "HIGH" : "LOW");
}

bool HarCoHBridgeDriver::isSleepInverted() const {
    return _sleepInverted;
}

bool HarCoHBridgeDriver::isEnableInverted() const {
    return _enableInverted;
}

bool HarCoHBridgeDriver::isRunningContinuously() const {
    return _moving && _moveDuration == 0;
}
