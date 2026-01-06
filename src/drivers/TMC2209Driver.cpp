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
    , _stallThreshold(50) {
    
    // Initialize with default constant velocity profile
    _profile = AccelerationProfile::constant(_maxSpeed);
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
    
    // Calculate target
    _targetPosition = _position + steps;
    
    // Set direction
    digitalWrite(_dirPin, steps > 0 ? HIGH : LOW);
    
    _moving = true;
    calculateStepInterval();
    _lastStepTime = micros();
    
    // If constant velocity (no acceleration), we'll step at fixed rate
    // For acceleration profiles, update() handles ramping
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
        
        // TODO: Update step interval for acceleration profiles
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
    // Convert speed (steps/sec) to interval (microseconds)
    if (_maxSpeed > 0) {
        _stepInterval = (uint32_t)(1000000.0f / _maxSpeed);
        if (_stepInterval < 10) _stepInterval = 10;  // Minimum 10µs
    } else {
        _stepInterval = 1000;  // Default 1ms
    }
    _currentSpeed = _maxSpeed;
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
