/*
 * =============================================================================
 * TMC2208 DRIVER - Implementation
 * =============================================================================
 * FastAccelStepper-powered version - all motion planning delegated to library
 * 
 * Key differences from TMC2209:
 *   - NO StallGuard (sensorless homing not available)
 *   - NO CoolStep (automatic current reduction)
 *   - Same UART configuration for current, microstepping, StealthChop
 */

#include "TMC2208Driver.h"

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
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
    , _acceleration(1000.0f)  // Default acceleration
    , _holdCurrentPercent(50) // Default 50% hold current
    , _currentSpeed(0) {
}

TMC2208Driver::~TMC2208Driver() {
    if (_driver != nullptr) {
        delete _driver;
        _driver = nullptr;
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool TMC2208Driver::init() {
    Serial.println("TMC2208 Driver: Initializing...");
    
    // Initialize Enable pin
    pinMode(_enPin, OUTPUT);
    disable();  // Start disabled
    
    // Initialize direction pin (MCPWM will also configure it)
    pinMode(_dirPin, OUTPUT);
    digitalWrite(_dirPin, LOW);
    
    // Initialize MCPWM hardware stepper with FastAccelStepper
    // Pass enable pin so FastAccelStepper can control auto-enable/disable
    if (!_mcpwmStepper.init((gpio_num_t)_stepPin, (gpio_num_t)_dirPin, (gpio_num_t)_enPin)) {
        Serial.println("TMC2208 Driver: MCPWM initialization failed!");
        return false;
    }
    Serial.println("TMC2208 Driver: MCPWM initialized successfully");
    
    // Set default speed and acceleration for FastAccelStepper
    _mcpwmStepper.setFrequency(_maxSpeed);
    _mcpwmStepper.setAcceleration(_acceleration);
    
    _enabled = false;
    
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
    
    // SAFE STARTUP: Enable auto-disable mode (motor auto-enables for moves, then disables)
    // This prevents the motor from drawing current when idle
    setAutoDisable(DefaultMotorConfig::STEPPER_AUTO_DISABLE);
    Serial.print("TMC2208 Driver: Auto-disable ");
    Serial.println(DefaultMotorConfig::STEPPER_AUTO_DISABLE ? "ON" : "OFF");
    
    Serial.println("TMC2208 Driver: Ready");
    Serial.println("  Note: TMC2208 has NO StallGuard - use limit switches for homing");
    Serial.print("TMC2208 Driver: Startup current = ");
    Serial.print(_runCurrentMA);
    Serial.println(" mA");
    
    // SAFE STARTUP: Keep motor DISABLED on boot
    // User must explicitly enable with 'enable' command
    // Note: If auto-disable is ON, motor will auto-enable when a move is commanded
    disable();
    Serial.println("TMC2208 Driver: Motor DISABLED on startup (safe mode)");
    
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
    _mcpwmStepper.stop();
}

bool TMC2208Driver::isEnabled() const {
    return _enabled;
}

// =============================================================================
// MOTION CONTROL - Delegated to FastAccelStepper
// =============================================================================

void TMC2208Driver::move(int32_t steps) {
    if (!_enabled) return;
    if (steps == 0) return;
    
    // FastAccelStepper handles all motion planning
    _mcpwmStepper.moveBy(steps);
    _moving = true;
    _targetPosition = _position + steps;
}

void TMC2208Driver::moveTo(int32_t position) {
    if (!_enabled) return;
    if (position < 0) position = 0;  // Only positive positions
    
    // FastAccelStepper handles all motion planning
    _mcpwmStepper.moveTo(position);
    _moving = true;
    _targetPosition = position;
}

void TMC2208Driver::stop() {
    // Controlled stop with deceleration
    _mcpwmStepper.stop();
    _moving = false;
    _targetPosition = _mcpwmStepper.getPosition();
}

void TMC2208Driver::emergencyStop() {
    // Emergency stop - immediate halt
    _mcpwmStepper.emergencyStop();
    _moving = false;
    _position = _mcpwmStepper.getPosition();
    _targetPosition = _position;
}

bool TMC2208Driver::isMoving() const {
    // Check FastAccelStepper's state
    return _mcpwmStepper.isMoving();
}

void TMC2208Driver::update() {
    // FastAccelStepper runs in background via interrupts
    // Just update our position tracking
    if (_enabled) {
        _position = _mcpwmStepper.getPosition();
        _moving = _mcpwmStepper.isMoving();
        
        // Update current speed for status reporting
        if (_moving) {
            _currentSpeed = _mcpwmStepper.getCurrentSpeed();
        } else {
            _currentSpeed = 0;
        }
    }
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void TMC2208Driver::setMaxSpeed(float stepsPerSecond) {
    if (stepsPerSecond <= 0) stepsPerSecond = 1;
    _maxSpeed = stepsPerSecond;
    
    // Set FastAccelStepper frequency
    _mcpwmStepper.setFrequency(stepsPerSecond);
}

void TMC2208Driver::setAcceleration(float accelStepsPerSecondSquared) {
    if (accelStepsPerSecondSquared <= 0) accelStepsPerSecondSquared = 100;
    _acceleration = accelStepsPerSecondSquared;
    
    // Set FastAccelStepper acceleration
    _mcpwmStepper.setAcceleration(accelStepsPerSecondSquared);
}

void TMC2208Driver::setCurrent(uint16_t runMA, uint16_t holdMA) {
    _runCurrentMA = runMA;
    _holdCurrentMA = holdMA;
    
    if (_uartMode && _driver != nullptr) {
        _driver->rms_current(_runCurrentMA);
        _driver->ihold(_holdCurrentMA > 0 ? (_holdCurrentMA * 31 / _runCurrentMA) : 0);
        Serial.print("TMC2208: Current set to ");
        Serial.print(_runCurrentMA);
        Serial.println(" mA RMS");
    } else {
        Serial.println("TMC2208: In Step/Dir mode - current set by Vref potentiometer");
    }
}

void TMC2208Driver::setMicrosteps(uint16_t microsteps) {
    // Validate - minimum 2 microsteps per Trinamic recommendations
    switch (microsteps) {
        case 2: case 4: case 8: case 16:
        case 32: case 64: case 128: case 256:
            break;
        default:
            if (microsteps < 2) {
                Serial.println("TMC2208: ⚠️ Fullstep (1/1) not supported - minimum 1/2 microstepping");
                Serial.println("         Fullstep is incompatible with StealthChop and causes unreliable operation.");
                microsteps = 2;
            } else {
                microsteps = 16;  // Default
            }
    }
    
    _microsteps = microsteps;
    
    if (_uartMode && _driver != nullptr) {
        // CRITICAL: Ensure UART controls microsteps (not MS1/MS2 pins)
        _driver->mstep_reg_select(true);
        
        // Set MRES in CHOPCONF register
        uint8_t mres = microStepsToMRES(microsteps);
        uint32_t chopconf = _driver->CHOPCONF();
        chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
        _driver->CHOPCONF(chopconf);
        
        // Read back to verify
        delay(10);
        uint32_t readback = _driver->CHOPCONF();
        uint8_t actualMres = (readback >> 24) & 0x0F;
        
        Serial.print("TMC2208: Microsteps set to 1/");
        Serial.print(_microsteps);
        Serial.print(" (MRES=");
        Serial.print(mres);
        Serial.print(", readback=");
        Serial.print(actualMres);
        Serial.println(actualMres == mres ? " ✓)" : " ⚠️ MISMATCH!)");
    } else {
        Serial.println("TMC2208: In Step/Dir mode - microsteps set by MS1/MS2 pins");
    }
}

// =============================================================================
// POSITION
// =============================================================================

int32_t TMC2208Driver::getPosition() const {
    return _position;
}

void TMC2208Driver::setPosition(int32_t position) {
    _position = position;
    _mcpwmStepper.setPosition(position);
}

void TMC2208Driver::home(int8_t direction) {
    // TMC2208 has no StallGuard - homing requires external limit switch
    Serial.println("TMC2208: Homing NOT supported (no StallGuard)");
    Serial.println("         Use external limit switches for homing");
    _position = 0;
    _targetPosition = 0;
    setPosition(0);
}

// =============================================================================
// DIAGNOSTICS
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
        
        // Check UART communication (test_connection returns non-zero on failure)
        if (_driver->test_connection() != 0) {
            status.errorFlags |= MotorError::COMM_FAILURE;
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
        
        // Connection test
        Serial.print("  Connection:   ");
        uint8_t connResult = _driver->test_connection();
        Serial.println(connResult == 0 ? "OK ✓" : "FAILED ✗");
        
        // Registers
        uint32_t drv_status = _driver->DRV_STATUS();
        uint32_t chopconf = _driver->CHOPCONF();
        
        Serial.print("  DRV_STATUS:   0x");
        Serial.println(drv_status, HEX);
        
        Serial.print("  CHOPCONF:     0x");
        Serial.println(chopconf, HEX);
        
        // Read back actual microstep setting from CHOPCONF
        uint8_t actualMres = (chopconf >> 24) & 0x0F;
        uint16_t actualMicrosteps = mrestoMicrosteps(actualMres);
        Serial.print("  Microsteps:   1/");
        Serial.print(actualMicrosteps);
        Serial.print(" (MRES=");
        Serial.print(actualMres);
        Serial.print(", configured=1/");
        Serial.print(_microsteps);
        Serial.println(")");
        
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

void TMC2208Driver::setPWMAutoscale(bool enable) {
    if (_uartMode && _driver != nullptr) {
        _driver->pwm_autoscale(enable);
        Serial.print("TMC2208: PWM autoscale ");
        Serial.println(enable ? "ENABLED (current scales with load)" : "DISABLED (full current always)");
        
        if (!enable) {
            Serial.println("  Note: Motor will draw full current even when idle - may run hotter");
        }
    } else {
        Serial.println("TMC2208: Cannot set PWM autoscale in Step/Dir mode");
    }
}

// =============================================================================
// HELPER METHODS - Microstep Conversion
// =============================================================================

uint8_t TMC2208Driver::microStepsToMRES(uint16_t ms) {
    // Convert microsteps to MRES value
    // MRES = 8 - log2(microsteps)
    switch (ms) {
        case 256: return 0;
        case 128: return 1;
        case 64:  return 2;
        case 32:  return 3;
        case 16:  return 4;
        case 8:   return 5;
        case 4:   return 6;
        case 2:   return 7;
        case 1:   return 8;  // Fullstep (not recommended)
        default:  return 4;  // Default to 16 microsteps
    }
}

uint16_t TMC2208Driver::mrestoMicrosteps(uint8_t mres) {
    // Convert MRES value to microsteps
    // microsteps = 256 >> MRES
    if (mres > 8) mres = 8;
    return 256 >> mres;
}

// =============================================================================
// NEW FASTACCELSTEPPER-BASED METHODS
// =============================================================================

void TMC2208Driver::setLinearAcceleration(uint32_t steps) {
    _mcpwmStepper.setLinearAcceleration(steps);
    Serial.print("Linear acceleration set to ");
    Serial.print(steps);
    Serial.println(" steps (cubesteps)");
}

uint32_t TMC2208Driver::getLinearAcceleration() const {
    return _mcpwmStepper.getLinearAcceleration();
}

void TMC2208Driver::setHoldCurrentPercent(uint8_t percent) {
    if (percent > 100) percent = 100;
    _holdCurrentPercent = percent;
    
    if (_driver != nullptr && _uartMode) {
        // Convert percent to IHOLD register value (0-31)
        uint8_t iholdValue = (percent * 31) / 100;
        _driver->ihold(iholdValue);
        
        Serial.print("Hold current set to ");
        Serial.print(percent);
        Serial.print("% (IHOLD=");
        Serial.print(iholdValue);
        Serial.println(")");
    }
}

uint8_t TMC2208Driver::getHoldCurrentPercent() const {
    return _holdCurrentPercent;
}

void TMC2208Driver::setAutoDisable(bool enableAutoDisable) {
    _mcpwmStepper.setAutoEnable(enableAutoDisable);
    
    if (enableAutoDisable) {
        // Auto-disable ON: Motor will be auto-enabled before moves and auto-disabled after
        // Keep current state - library will handle enable/disable on next move
        Serial.println("Auto-disable ON");
    } else {
        // Auto-disable OFF: Motor should stay enabled
        // Re-enable immediately (in case it was disabled by auto-disable)
        enable();
        Serial.println("Auto-disable OFF - motor enabled");
    }
}

bool TMC2208Driver::isAutoDisableActive() const {
    return _mcpwmStepper.isAutoEnableActive();
}

void TMC2208Driver::runForward() {
    enable();
    _mcpwmStepper.runForward();
    _moving = true;
    Serial.println("Running forward continuously...");
}

void TMC2208Driver::runBackward() {
    enable();
    _mcpwmStepper.runBackward();
    _moving = true;
    Serial.println("Running backward continuously...");
}

void TMC2208Driver::brake() {
    _mcpwmStepper.brake();
    Serial.println("Braking...");
}

int32_t TMC2208Driver::getTargetPosition() const {
    return _mcpwmStepper.getTargetPosition();
}

int32_t TMC2208Driver::getActualSpeed() const {
    return _mcpwmStepper.getActualSpeed();
}

uint8_t TMC2208Driver::getRampState() const {
    return _mcpwmStepper.getRampState();
}

bool TMC2208Driver::isRunningContinuously() const {
    return _mcpwmStepper.isRunningContinuously();
}