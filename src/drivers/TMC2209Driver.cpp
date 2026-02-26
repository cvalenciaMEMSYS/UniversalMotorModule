/*
 * =============================================================================
 * TMC2209 DRIVER - Implementation
 * =============================================================================
 * FastAccelStepper-powered version - all motion planning delegated to library
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
    , _currentSpeed(0)
    , _stallThreshold(50) {
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
    
    // Initialize Enable pin
    pinMode(_enPin, OUTPUT);
    disable();  // Start disabled
    
    // Initialize direction pin (MCPWM will also configure it)
    pinMode(_dirPin, OUTPUT);
    digitalWrite(_dirPin, LOW);
    
    // Initialize MCPWM hardware stepper with FastAccelStepper
    // Pass enable pin so FastAccelStepper can control auto-enable/disable
    if (!_mcpwmStepper.init((gpio_num_t)_stepPin, (gpio_num_t)_dirPin, (gpio_num_t)_enPin)) {
        Serial.println("TMC2209 Driver: MCPWM initialization failed!");
        return false;
    }
    Serial.println("TMC2209 Driver: MCPWM initialized successfully");
    
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
    
    // SAFE STARTUP: Enable auto-disable mode (motor auto-enables for moves, then disables)
    // This prevents the motor from drawing current when idle
    setAutoDisable(DefaultMotorConfig::STEPPER_AUTO_DISABLE);
    Serial.print("TMC2209 Driver: Auto-disable ");
    Serial.println(DefaultMotorConfig::STEPPER_AUTO_DISABLE ? "ON" : "OFF");
    
    Serial.println("TMC2209 Driver: Ready");
    Serial.print("TMC2209 Driver: Startup current = ");
    Serial.print(_runCurrentMA);
    Serial.println(" mA");
    
    // SAFE STARTUP: Keep motor DISABLED on boot
    // User must explicitly enable with 'enable' command
    // Note: If auto-disable is ON, motor will auto-enable when a move is commanded
    disable();
    Serial.println("TMC2209 Driver: Motor DISABLED on startup (safe mode)");
    
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
    
    // Current configuration - FIX #4: Ensure holding torque is configured
    _driver->rms_current(_runCurrentMA);
    
    // IHOLD: 0-31 scale. Calculate from holdCurrentMA, or default to 50% of run current
    uint8_t iholdValue = 16;  // Default: 50% of IRUN (good holding torque)
    if (_holdCurrentMA > 0) {
        // User specified hold current - calculate as fraction of run current
        iholdValue = (_holdCurrentMA * 31) / _runCurrentMA;
        if (iholdValue > 31) iholdValue = 31;  // Cap at max
        if (iholdValue < 1) iholdValue = 1;    // Ensure at least minimal hold
    }
    _driver->ihold(iholdValue);
    
    // TPOWERDOWN: Delay before reducing to hold current
    // Value * 2^18 clock cycles @ 12MHz ≈ value * 21.8ms
    // 10 = ~220ms delay after stop before reducing current
    _driver->TPOWERDOWN(10);
    
    // IRUN delay: Time for current to reach IRUN from IHOLD
    // IHOLDDELAY = (0-15) * 2^18 clocks, higher = smoother transitions
    _driver->iholddelay(10);  // Smooth current ramp-up
    
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
    _mcpwmStepper.stop();
}

bool TMC2209Driver::isEnabled() const {
    return _enabled;
}

// =============================================================================
// MOTION CONTROL - Delegated to FastAccelStepper
// =============================================================================

void TMC2209Driver::move(int32_t steps) {
    if (!_enabled) enable();  // Auto-enable on first move
    if (steps == 0) return;
    
    // FastAccelStepper handles all motion planning
    _mcpwmStepper.moveBy(steps);
    _moving = true;
    _targetPosition = _position + steps;
}

void TMC2209Driver::moveTo(int32_t position) {
    if (!_enabled) enable();  // Auto-enable on first move
    if (position < 0) position = 0;  // Only positive positions
    
    // FastAccelStepper handles all motion planning
    _mcpwmStepper.moveTo(position);
    _moving = true;
    _targetPosition = position;
}

void TMC2209Driver::stop() {
    // Controlled stop with deceleration
    _mcpwmStepper.stop();
    _moving = false;
    _targetPosition = _mcpwmStepper.getPosition();
}

void TMC2209Driver::emergencyStop() {
    // Emergency stop - immediate halt
    _mcpwmStepper.emergencyStop();
    _moving = false;
    _position = _mcpwmStepper.getPosition();
    _targetPosition = _position;
}

bool TMC2209Driver::isMoving() const {
    // Check FastAccelStepper's state
    return _mcpwmStepper.isMoving();
}

void TMC2209Driver::update() {
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

void TMC2209Driver::setMaxSpeed(float stepsPerSecond) {
    if (stepsPerSecond <= 0) stepsPerSecond = 1;
    _maxSpeed = stepsPerSecond;
    
    // Set FastAccelStepper frequency
    _mcpwmStepper.setFrequency(stepsPerSecond);
}

void TMC2209Driver::setAcceleration(float accelStepsPerSecondSquared) {
    if (accelStepsPerSecondSquared <= 0) accelStepsPerSecondSquared = 100;
    _acceleration = accelStepsPerSecondSquared;
    
    // Set FastAccelStepper acceleration
    _mcpwmStepper.setAcceleration(accelStepsPerSecondSquared);
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
    // Validate - minimum 2 microsteps per Trinamic recommendations
    switch (microsteps) {
        case 2: case 4: case 8: case 16:
        case 32: case 64: case 128: case 256:
            break;
        default:
            if (microsteps < 2) {
                Serial.println("TMC2209: ⚠️ Fullstep (1/1) not supported - minimum 1/2 microstepping");
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
        
        Serial.print("TMC2209: Microsteps set to 1/");
        Serial.print(_microsteps);
        Serial.print(" (MRES=");
        Serial.print(mres);
        Serial.print(", readback=");
        Serial.print(actualMres);
        Serial.println(actualMres == mres ? " ✓)" : " ⚠️ MISMATCH!)");
    } else {
        Serial.println("TMC2209: In Step/Dir mode - microsteps set by MS1/MS2 pins");
    }
}

// =============================================================================
// POSITION
// =============================================================================

int32_t TMC2209Driver::getPosition() const {
    return _position;
}

void TMC2209Driver::setPosition(int32_t position) {
    _position = position;
    _mcpwmStepper.setPosition(position);
}

void TMC2209Driver::home(int8_t direction) {
    // Use StallGuard to detect home position (requires UART mode)
    if (!_uartMode) {
        Serial.println("TMC2209: Homing NOT available in Step/Dir mode!");
        Serial.println("         Use physical limit switches instead.");
        Serial.println("         Switch to UART mode with 'stepdir off' to use StallGuard homing.");
        _position = 0;
        _targetPosition = 0;
        setPosition(0);
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
    setPosition(0);
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

void TMC2209Driver::setPWMAutoscale(bool enable) {
    if (_uartMode && _driver != nullptr) {
        _driver->pwm_autoscale(enable);
        Serial.print("TMC2209: PWM autoscale ");
        Serial.println(enable ? "ENABLED (current scales with load)" : "DISABLED (full current always)");
        
        if (!enable) {
            Serial.println("  Note: Motor will draw full current even when idle - may run hotter");
        }
    } else {
        Serial.println("TMC2209: Cannot set PWM autoscale in Step/Dir mode");
    }
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
// HELPER METHODS - Microstep Conversion
// =============================================================================

uint8_t TMC2209Driver::microStepsToMRES(uint16_t ms) {
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

uint16_t TMC2209Driver::mrestoMicrosteps(uint8_t mres) {
    // Convert MRES value to microsteps
    // microsteps = 256 >> MRES
    if (mres > 8) mres = 8;
    return 256 >> mres;
}

// =============================================================================
// NEW FASTACCELSTEPPER-BASED METHODS
// =============================================================================

void TMC2209Driver::setLinearAcceleration(uint32_t steps) {
    _mcpwmStepper.setLinearAcceleration(steps);
    Serial.print("Linear acceleration set to ");
    Serial.print(steps);
    Serial.println(" steps (cubesteps)");
}

uint32_t TMC2209Driver::getLinearAcceleration() const {
    return _mcpwmStepper.getLinearAcceleration();
}

void TMC2209Driver::setHoldCurrentPercent(uint8_t percent) {
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

uint8_t TMC2209Driver::getHoldCurrentPercent() const {
    return _holdCurrentPercent;
}

void TMC2209Driver::setAutoDisable(bool enableAutoDisable) {
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

bool TMC2209Driver::isAutoDisableActive() const {
    return _mcpwmStepper.isAutoEnableActive();
}

void TMC2209Driver::runForward() {
    enable();
    _mcpwmStepper.runForward();
    _moving = true;
    Serial.println("Running forward continuously...");
}

void TMC2209Driver::runBackward() {
    enable();
    _mcpwmStepper.runBackward();
    _moving = true;
    Serial.println("Running backward continuously...");
}

void TMC2209Driver::brake() {
    _mcpwmStepper.brake();
    Serial.println("Braking...");
}

int32_t TMC2209Driver::getTargetPosition() const {
    return _mcpwmStepper.getTargetPosition();
}

int32_t TMC2209Driver::getActualSpeed() const {
    return _mcpwmStepper.getActualSpeed();
}

uint8_t TMC2209Driver::getRampState() const {
    return _mcpwmStepper.getRampState();
}

bool TMC2209Driver::isRunningContinuously() const {
    return _mcpwmStepper.isRunningContinuously();
}
