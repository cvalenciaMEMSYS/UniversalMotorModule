/*
 * =============================================================================
 * MOTOR CONTROLLER - Implementation
 * =============================================================================
 */

#include "MotorController.h"
#include "../config/PinConfig.h"
#include "StatusLED.h"
#include "../drivers/TMC2209Driver.h"
#include "../drivers/TMC2208Driver.h"
#include <FastAccelStepper.h>  // For RAMP_STATE_* constants

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

MotorController::MotorController()
    : _driver(nullptr)
    , _initialized(false)
    , _wasMoving(false)
    , _errorFlags(MotorError::NONE)
    , _lastErrorPollTime(0) {
    // Initialize default motion parameters
    _maxSpeed = DefaultMotorConfig::STEPPER_MAX_SPEED;
    _acceleration = 1000.0f;
}

MotorController::~MotorController() {
    if (_driver != nullptr) {
        delete _driver;
        _driver = nullptr;
    }
}

// =============================================================================
// LIFECYCLE
// =============================================================================

bool MotorController::begin() {
    Serial.println("\n╔═══════════════════════════════════════════════════════════╗");
    Serial.println("║         UNIVERSAL MOTOR MODULE v1.0                       ║");
    Serial.println("╚═══════════════════════════════════════════════════════════╝\n");
    
    // Print detection info
    DriverFactory::printDetectionInfo();
    
    // Create and initialize driver
    _driver = DriverFactory::createAndInitDriver();
    
    if (_driver == nullptr) {
        Serial.println("\n❌ ERROR: Failed to initialize motor driver!");
        Serial.println("   Check wiring and hardware detection jumpers.");
        _initialized = false;
        return false;
    }
    
    // Apply default motion parameters
    _driver->setMaxSpeed(_maxSpeed);
    _driver->setAcceleration(_acceleration);
    
    _initialized = true;
    
    Serial.println("\n✓ Motor controller ready!");
    Serial.print("  Driver: ");
    Serial.println(_driver->getName());
    
    printHelp();
    
    return true;
}

void MotorController::update() {
    if (_driver != nullptr) {
        _driver->update();
        
        // Detect movement completion
        bool isCurrentlyMoving = _driver->isMoving();
        if (_wasMoving && !isCurrentlyMoving) {
            // Movement just finished - check for errors immediately
            Serial.println("Complete");
            MotorStatus status = _driver->getStatus();
            _errorFlags = status.errorFlags;
            _lastErrorPollTime = millis();  // Reset poll timer
        }
        _wasMoving = isCurrentlyMoving;
        
        // Poll for errors periodically when NOT moving
        // UART reads block the stepping loop causing visible jerks during motion
        uint32_t now = millis();
        if (!isCurrentlyMoving && (now - _lastErrorPollTime >= ERROR_POLL_INTERVAL_MS)) {
            _lastErrorPollTime = now;
            MotorStatus status = _driver->getStatus();
            _errorFlags = status.errorFlags;
        }
    }
}

bool MotorController::isReady() const {
    return _initialized && _driver != nullptr;
}

bool MotorController::isBusy() const {
    if (_driver == nullptr) return false;
    return _driver->isMoving();
}

// =============================================================================
// COMMAND INTERFACE
// =============================================================================

void MotorController::processCommand(const String& cmd) {
    if (!isReady()) {
        Serial.println("ERROR: Controller not initialized!");
        return;
    }
    
    String command = cmd;
    command.trim();
    command.toLowerCase();
    
    // Empty command
    if (command.length() == 0) {
        return;
    }
    
    // === MOTION COMMANDS ===
    
    if (command.startsWith("move ")) {
        // Relative move: "move 100" or "move -50"
        String valueStr = command.substring(5);
        valueStr.trim();
        
        // FIX #5: Check for valid numeric input
        bool validNumber = true;
        for (size_t i = 0; i < valueStr.length(); i++) {
            char c = valueStr.charAt(i);
            if (i == 0 && c == '-') continue;  // Allow leading minus
            if (c < '0' || c > '9') {
                validNumber = false;
                break;
            }
        }
        
        if (!validNumber || valueStr.length() == 0) {
            Serial.println("ERROR: Invalid step value (must be integer)");
            return;
        }
        
        int32_t steps = valueStr.toInt();
        
        // FIX #5: Validate range to prevent overflow
        if (abs(steps) > MotorLimits::MAX_MOVE_STEPS) {
            Serial.printf("ERROR: Move distance %ld exceeds limit ±%ld steps\n", 
                          (long)steps, (long)MotorLimits::MAX_MOVE_STEPS);
            return;
        }
        
        moveBy(steps);
    }
    else if (command.startsWith("abs ")) {
        // Absolute move: "abs 1000" (for steppers: position, for DC: speed %)
        String valueStr = command.substring(4);
        valueStr.trim();
        
        // Check if DC motor - allows negative values for reverse direction
        bool isDCMotor = (_driver != nullptr && _driver->getType() == MotorType::DC_MOTOR);
        
        // Validate input - allow leading minus for DC motors
        bool validNumber = true;
        for (size_t i = 0; i < valueStr.length(); i++) {
            char c = valueStr.charAt(i);
            if (i == 0 && c == '-' && isDCMotor) continue;  // Allow negative for DC
            if (c < '0' || c > '9') {
                validNumber = false;
                break;
            }
        }
        
        if (!validNumber || valueStr.length() == 0) {
            if (isDCMotor) {
                Serial.println("ERROR: Invalid speed value (must be -100 to +100)");
            } else {
                Serial.println("ERROR: Invalid position value (must be non-negative integer)");
            }
            return;
        }
        
        int32_t position = valueStr.toInt();
        
        // Validate range - different for DC vs stepper
        if (isDCMotor) {
            if (position < -100 || position > 100) {
                Serial.println("ERROR: Speed must be -100 to +100");
                return;
            }
        } else {
            if (position < 0 || position > MotorLimits::MAX_POSITION) {
                Serial.printf("ERROR: Position must be 0 to %ld\n", (long)MotorLimits::MAX_POSITION);
                return;
            }
        }
        
        moveTo(position);
    }
    else if (command == "home") {
        Serial.println("Homing...");
        home();
    }
    else if (command == "stop") {
        Serial.println("Emergency stop!");
        stop();
    }
    else if (command == "brake") {
        Serial.println("Braking with deceleration...");
        if (_driver != nullptr) {
            _driver->brake();
        }
    }
    else if (command == "run forward" || command == "runforward" || command == "run f") {
        if (_driver != nullptr) {
            _driver->runForward();
        }
    }
    else if (command == "run backward" || command == "runbackward" || command == "run b") {
        if (_driver != nullptr) {
            _driver->runBackward();
        }
    }
    
    // === QUERY COMMANDS ===
    
    else if (command == "get pos" || command == "getpos") {
        if (_driver != nullptr) {
            Serial.print("Position: ");
            Serial.print(_driver->getPosition());
            Serial.println(" steps");
        }
    }
    else if (command == "get target" || command == "gettarget") {
        if (_driver != nullptr) {
            Serial.print("Target: ");
            Serial.print(_driver->getTargetPosition());
            Serial.println(" steps");
        }
    }
    else if (command == "get speed" || command == "getspeed") {
        if (_driver != nullptr) {
            Serial.print("Current speed: ");
            Serial.print(_driver->getActualSpeed());
            Serial.println(" steps/s");
        }
    }
    else if (command == "get rampstate" || command == "getrampstate") {
        if (_driver != nullptr) {
            uint8_t state = _driver->getRampState();
            Serial.print("Ramp state: ");
            switch (state & RAMP_STATE_MASK) {
                case RAMP_STATE_IDLE:        Serial.print("IDLE"); break;
                case RAMP_STATE_COAST:       Serial.print("COASTING"); break;
                case RAMP_STATE_ACCELERATE:  Serial.print("ACCELERATING"); break;
                case RAMP_STATE_DECELERATE:  Serial.print("DECELERATING"); break;
                case RAMP_STATE_REVERSE:     Serial.print("REVERSING"); break;
                default:                     Serial.print("UNKNOWN"); break;
            }
            if (state & RAMP_DIRECTION_COUNT_UP) {
                Serial.println(" (direction: FORWARD)");
            } else if (state & RAMP_DIRECTION_COUNT_DOWN) {
                Serial.println(" (direction: BACKWARD)");
            } else {
                Serial.println();
            }
        }
    }
    
    // === ENABLE/DISABLE ===
    
    else if (command == "enable") {
        enableMotor();
        Serial.println("Motor enabled");
    }
    else if (command == "disable") {
        disableMotor();
        Serial.println("Motor disabled");
    }
    
    // === CONFIGURATION ===
    
    else if (command.startsWith("set ")) {
        parseSetCommand(command.substring(4));
    }
    
    // === STATUS/DEBUG ===
    
    else if (command == "?" || command == "status") {
        printStatus();
    }
    else if (command == "help" || command == "h") {
        printHelp();
    }
    else if (command == "t" || command == "test") {
        // Connection test
        if (_driver->testConnection()) {
            Serial.println("✓ Connection OK");
        } else {
            Serial.println("✗ Connection FAILED");
        }
    }
    else if (command == "r" || command == "diag") {
        // Full diagnostics
        _driver->printDiagnostics();
    }
    else if (command == "scan") {
        // Scan for TMC2209 drivers at all 4 addresses
        if (_driver->getType() == MotorType::STEPPER_TMC2209) {
            auto* tmc = static_cast<TMC2209Driver*>(_driver);
            tmc->scanAddresses();
        } else {
            Serial.println("Scan command only available for TMC2209 driver");
        }
    }
    else if (command == "reconfigure" || command == "reconfig") {
        // Reconfigure driver (useful after power glitch)
        MotorType type = _driver->getType();
        if (type == MotorType::STEPPER_TMC2209) {
            Serial.println("Reconfiguring TMC2209...");
            auto* tmc = static_cast<TMC2209Driver*>(_driver);
            tmc->reconfigure();
        } else if (type == MotorType::STEPPER_TMC2208) {
            Serial.println("Reconfiguring TMC2208...");
            auto* tmc = static_cast<TMC2208Driver*>(_driver);
            tmc->reconfigure();
        } else {
            Serial.println("Reconfigure not needed for this driver");
        }
    }
    else if (command.startsWith("stepdir ")) {
        // Step/Dir fallback mode toggle (TMC drivers only)
        String mode = command.substring(8);
        mode.trim();
        
        MotorType type = _driver->getType();
        if (type == MotorType::STEPPER_TMC2209) {
            // Cast to TMC2209Driver to access setStepDirMode
            auto* tmc = static_cast<TMC2209Driver*>(_driver);
            if (mode == "on") {
                tmc->setStepDirMode(true);
            } else if (mode == "off") {
                tmc->setStepDirMode(false);
            } else {
                Serial.println("Usage: stepdir on|off");
            }
        }
        else if (type == MotorType::STEPPER_TMC2208) {
            // Cast to TMC2208Driver to access setStepDirMode
            auto* tmc = static_cast<TMC2208Driver*>(_driver);
            if (mode == "on") {
                tmc->setStepDirMode(true);
            } else if (mode == "off") {
                tmc->setStepDirMode(false);
            } else {
                Serial.println("Usage: stepdir on|off");
            }
        }
        else {
            Serial.println("stepdir command only applies to TMC2209/TMC2208 drivers");
        }
    }
    else if (command.equalsIgnoreCase("stealthchop")) {
        // Enable StealthChop mode (silent)
        MotorType type = _driver->getType();
        if (type == MotorType::STEPPER_TMC2209) {
            auto* tmc = static_cast<TMC2209Driver*>(_driver);
            tmc->setStealthChop(true);
        } else if (type == MotorType::STEPPER_TMC2208) {
            auto* tmc = static_cast<TMC2208Driver*>(_driver);
            tmc->setStealthChop(true);
        } else {
            Serial.println("StealthChop only applies to TMC drivers");
        }
    }
    else if (command.equalsIgnoreCase("spreadcycle")) {
        // Enable SpreadCycle mode (high torque, works with fullstep)
        MotorType type = _driver->getType();
        if (type == MotorType::STEPPER_TMC2209) {
            auto* tmc = static_cast<TMC2209Driver*>(_driver);
            tmc->setStealthChop(false);
        } else if (type == MotorType::STEPPER_TMC2208) {
            auto* tmc = static_cast<TMC2208Driver*>(_driver);
            tmc->setStealthChop(false);
        } else {
            Serial.println("SpreadCycle only applies to TMC drivers");
        }
    }
    else if (command.startsWith("pwmautoscale ")) {
        // Toggle PWM autoscale (automatic current reduction)
        String modeStr = command.substring(13);
        modeStr.trim();
        modeStr.toLowerCase();
        
        bool enable;
        if (modeStr == "on" || modeStr == "1" || modeStr == "true") {
            enable = true;
        } else if (modeStr == "off" || modeStr == "0" || modeStr == "false") {
            enable = false;
        } else {
            Serial.println("Usage: pwmautoscale on|off");
            return;
        }
        
        MotorType type = _driver->getType();
        if (type == MotorType::STEPPER_TMC2209) {
            auto* tmc = static_cast<TMC2209Driver*>(_driver);
            tmc->setPWMAutoscale(enable);
        } else if (type == MotorType::STEPPER_TMC2208) {
            auto* tmc = static_cast<TMC2208Driver*>(_driver);
            tmc->setPWMAutoscale(enable);
        } else {
            Serial.println("PWM autoscale only applies to TMC drivers");
        }
    }
    
    // === UNKNOWN ===
    
    else {
        Serial.print("Unknown command: ");
        Serial.println(command);
        Serial.println("Type 'help' for available commands");
    }
}

void MotorController::parseSetCommand(const String& params) {
    // Format: "set <param> <value>"
    // Examples: "set speed 500", "set current 400", "set microsteps 16"
    
    int spaceIdx = params.indexOf(' ');
    if (spaceIdx < 0) {
        Serial.println("Usage: set <param> <value>");
        Serial.println("  Parameters: speed, current, microsteps, accel, jerk");
        return;
    }
    
    String param = params.substring(0, spaceIdx);
    String valueStr = params.substring(spaceIdx + 1);
    valueStr.trim();
    
    if (param == "speed") {
        float speed = valueStr.toFloat();
        
        // FIX #5: Validate speed range
        if (speed < MotorLimits::MIN_SPEED || speed > MotorLimits::MAX_SPEED) {
            Serial.printf("ERROR: Speed must be %.0f to %.0f steps/sec\n", 
                          MotorLimits::MIN_SPEED, MotorLimits::MAX_SPEED);
            return;
        }
        
        setSpeed(speed);
        Serial.print("Speed set to ");
        Serial.print(speed);
        Serial.println(" steps/sec");
    }
    else if (param == "current") {
        uint16_t current = valueStr.toInt();
        
        // FIX #5: Validate current range
        if (current < MotorLimits::MIN_CURRENT_MA || current > MotorLimits::MAX_CURRENT_MA) {
            Serial.printf("ERROR: Current must be %d to %d mA\n", 
                          MotorLimits::MIN_CURRENT_MA, MotorLimits::MAX_CURRENT_MA);
            return;
        }
        
        setCurrent(current);
        Serial.print("Current set to ");
        Serial.print(current);
        Serial.println(" mA");
    }
    else if (param == "microsteps") {
        uint16_t ms = valueStr.toInt();
        
        // FIX #5: Validate microsteps (must be power of 2: 1, 2, 4, 8, 16, 32, 64, 128, 256)
        bool validMs = (ms >= MotorLimits::MIN_MICROSTEPS && 
                        ms <= MotorLimits::MAX_MICROSTEPS &&
                        (ms & (ms - 1)) == 0);  // Power of 2 check
        if (!validMs) {
            Serial.println("ERROR: Microsteps must be power of 2 (1, 2, 4, 8, 16, 32, 64, 128, 256)");
            return;
        }
        
        setMicrosteps(ms);
        Serial.print("Microsteps set to ");
        Serial.println(ms);
    }
    else if (param == "accel" || param == "acceleration") {
        float accel = valueStr.toFloat();
        
        // FIX #5: Validate acceleration range
        if (accel < MotorLimits::MIN_ACCELERATION || accel > MotorLimits::MAX_ACCELERATION) {
            Serial.printf("ERROR: Acceleration must be %.0f to %.0f steps/sec²\n", 
                          MotorLimits::MIN_ACCELERATION, MotorLimits::MAX_ACCELERATION);
            return;
        }
        
        setAcceleration(accel);
        Serial.print("Acceleration set to ");
        Serial.print(accel);
        Serial.println(" steps/sec²");
    }
    else if (param == "jerk") {
        // Jerk is no longer supported - replaced by cubesteps
        Serial.println("'set jerk' is deprecated. Use 'set cubesteps <n>' for S-curve motion.");
        Serial.println("  set cubesteps 0    = Trapezoidal (instant acceleration)");
        Serial.println("  set cubesteps 100  = S-curve (smooth acceleration over 100 steps)");
    }
    else if (param == "cubesteps") {
        uint32_t steps = valueStr.toInt();
        
        // Validate range
        if (steps > 10000) {
            Serial.println("ERROR: Cubesteps must be 0 to 10000");
            return;
        }
        
        if (_driver != nullptr) {
            _driver->setLinearAcceleration(steps);
        }
    }
    else if (param == "ihold") {
        uint8_t percent = valueStr.toInt();
        
        // Validate range
        if (percent > 100) {
            Serial.println("ERROR: Hold current percent must be 0 to 100");
            return;
        }
        
        if (_driver != nullptr) {
            _driver->setHoldCurrentPercent(percent);
        }
    }
    else if (param == "autodisable") {
        valueStr.toLowerCase();
        bool enable = (valueStr == "on" || valueStr == "1" || valueStr == "true");
        
        if (_driver != nullptr) {
            _driver->setAutoDisable(enable);
        }
    }
    else {
        Serial.print("Unknown parameter: ");
        Serial.println(param);
    }
}

void MotorController::printHelp() {
    Serial.println("\n┌─────────────────────────────────────────────────────────────┐");
    Serial.println("│                     AVAILABLE COMMANDS                      │");
    Serial.println("├─────────────────────────────────────────────────────────────┤");
    Serial.println("│  Motion:                                                    │");
    Serial.println("│    move <steps>      Relative move (+ or -)                 │");
    Serial.println("│    abs <position>    Move to absolute position (>= 0)       │");
    Serial.println("│    run forward       Run continuously forward               │");
    Serial.println("│    run backward      Run continuously backward              │");
    Serial.println("│    stop              Emergency stop (immediate)             │");
    Serial.println("│    brake             Controlled stop (decelerate)           │");
    Serial.println("│    home              Find home position (TMC2209 only)      │");
    Serial.println("│                                                             │");
    Serial.println("│  Control:                                                   │");
    Serial.println("│    enable            Enable motor driver                    │");
    Serial.println("│    disable           Disable motor driver                   │");
    Serial.println("│                                                             │");
    Serial.println("│  Configuration:                                             │");
    Serial.println("│    set speed <val>   Max speed (steps/sec)                  │");
    Serial.println("│    set accel <val>   Acceleration (steps/sec²)              │");
    Serial.println("│    set cubesteps <n> S-curve ramp steps (0=trapezoidal)     │");
    Serial.println("│    set current <mA>  Motor run current (UART only)          │");
    Serial.println("│    set ihold <%>     Hold current percent (0-100%)          │");
    Serial.println("│    set microsteps <n> Microstepping (1-256, UART only)      │");
    Serial.println("│    set autodisable on/off  Auto enable/disable motor        │");
    Serial.println("│                                                             │");
    Serial.println("│  Query:                                                     │");
    Serial.println("│    get pos           Current position (steps)               │");
    Serial.println("│    get target        Target position                        │");
    Serial.println("│    get speed         Actual current speed                   │");
    Serial.println("│    get rampstate     Ramp generator state                   │");
    Serial.println("│                                                             │");
    Serial.println("│  UART Control (TMC2209/TMC2208):                            │");
    Serial.println("│    stepdir on/off    Toggle Step/Dir fallback mode          │");
    Serial.println("│    reconfigure       Re-apply UART settings                 │");
    Serial.println("│    stealthchop       Silent mode                            │");
    Serial.println("│    spreadcycle       High-torque mode                       │");
    Serial.println("│    pwmautoscale on/off  Auto current reduction              │");
    Serial.println("│                                                             │");
    Serial.println("│  Status/Debug:                                              │");
    Serial.println("│    ? or status       Show current status                    │");
    Serial.println("│    t or test         Test driver connection                 │");
    Serial.println("│    r or diag         Full diagnostics                       │");
    Serial.println("│    help              Show this help                         │");
    Serial.println("└─────────────────────────────────────────────────────────────┘\n");
}

// =============================================================================
// MOTION CONTROL
// =============================================================================

void MotorController::moveBy(int32_t steps) {
    if (_driver != nullptr) {
        _driver->move(steps);
    }
}

void MotorController::moveTo(int32_t position) {
    if (_driver != nullptr) {
        _driver->moveTo(position);
    }
}

void MotorController::home() {
    if (_driver != nullptr) {
        _driver->home();
    }
}

void MotorController::stop() {
    if (_driver != nullptr) {
        _driver->emergencyStop();
    }
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void MotorController::setSpeed(float stepsPerSec) {
    _maxSpeed = stepsPerSec;
    if (_driver != nullptr) {
        _driver->setMaxSpeed(_maxSpeed);
    }
}

void MotorController::setCurrent(uint16_t mA) {
    if (_driver != nullptr) {
        _driver->setCurrent(mA, 0);  // 0 hold current for non-backdrivable
    }
}

void MotorController::setMicrosteps(uint16_t ms) {
    if (_driver != nullptr) {
        _driver->setMicrosteps(ms);
    }
}

void MotorController::setAcceleration(float accel) {
    _acceleration = accel;
    if (_driver != nullptr) {
        // FastAccelStepper doesn't support 0 acceleration directly
        // For "constant velocity" (no ramp), use very high acceleration
        if (accel <= 0.0f) {
            // Use 10M steps/s² - effectively instant velocity change
            _driver->setAcceleration(10000000.0f);
            Serial.println("Constant velocity mode (very high acceleration)");
        } else {
            _driver->setAcceleration(_acceleration);
        }
    }
}

void MotorController::setJerk(float jerk) {
    // Note: Jerk is handled internally by FastAccelStepper
    // This method is kept for compatibility but does nothing
    (void)jerk;  // Suppress unused parameter warning
}

void MotorController::enableMotor() {
    if (_driver != nullptr) {
        _driver->enable();
        statusLED.setMotorEnabled(true);
    }
}

void MotorController::disableMotor() {
    if (_driver != nullptr) {
        _driver->disable();
        statusLED.setMotorEnabled(false);
    }
}

// =============================================================================
// STATUS
// =============================================================================

void MotorController::printStatus() {
    Serial.println("\n╔═══════════════════════════════════════════════════════════╗");
    Serial.println("║                      MOTOR STATUS                         ║");
    Serial.println("╚═══════════════════════════════════════════════════════════╝\n");
    
    if (_driver == nullptr) {
        Serial.println("  ERROR: No driver initialized!");
        return;
    }
    
    MotorStatus status = _driver->getStatus();
    
    // Driver info
    Serial.print("  Driver:       ");
    Serial.print(_driver->getName());
    
    // Check UART mode for TMC drivers
    MotorType type = _driver->getType();
    if (type == MotorType::STEPPER_TMC2209) {
        auto* tmc = static_cast<TMC2209Driver*>(_driver);
        Serial.print(tmc->isUartMode() ? " (UART mode)" : " (Step/Dir mode)");
    } else if (type == MotorType::STEPPER_TMC2208) {
        auto* tmc = static_cast<TMC2208Driver*>(_driver);
        Serial.print(tmc->isUartMode() ? " (UART mode)" : " (Step/Dir mode)");
    }
    Serial.println();
    
    // Motion state
    Serial.print("  State:        ");
    if (_driver->isRunningContinuously()) {
        Serial.println("CONTINUOUS RUN");
    } else if (status.moving) {
        Serial.println("MOVING");
    } else {
        Serial.println("STOPPED");
    }
    
    // Position
    Serial.print("  Position:     ");
    Serial.print(status.position);
    Serial.println(" steps");
    
    Serial.print("  Target:       ");
    Serial.print(_driver->getTargetPosition());
    Serial.println(" steps");
    
    // Speed
    Serial.print("  Speed:        ");
    Serial.print(_driver->getActualSpeed());
    Serial.print(" / ");
    Serial.print((int)_maxSpeed);
    Serial.println(" steps/s (current / max)");
    
    // Ramp state
    uint8_t rampState = _driver->getRampState();
    Serial.print("  Ramp:         ");
    switch (rampState & RAMP_STATE_MASK) {
        case RAMP_STATE_IDLE:        Serial.print("IDLE"); break;
        case RAMP_STATE_COAST:       Serial.print("COASTING"); break;
        case RAMP_STATE_ACCELERATE:  Serial.print("ACCELERATING"); break;
        case RAMP_STATE_DECELERATE:  Serial.print("DECELERATING"); break;
        case RAMP_STATE_REVERSE:     Serial.print("REVERSING"); break;
        default:                     Serial.print("UNKNOWN"); break;
    }
    Serial.println();
    
    // Current settings
    Serial.print("  Current:      ");
    Serial.print(status.currentMA);
    Serial.print("mA run, ");
    Serial.print(_driver->getHoldCurrentPercent());
    Serial.println("% hold");
    
    // Motion parameters
    Serial.print("  Accel:        ");
    Serial.print((int)_acceleration);
    Serial.print(" steps/s², cubesteps: ");
    Serial.print(_driver->getLinearAcceleration());
    uint32_t linAccel = _driver->getLinearAcceleration();
    Serial.println(linAccel > 0 ? " (S-curve)" : " (trapezoidal)");
    
    // Auto-disable
    Serial.print("  Auto-disable: ");
    Serial.println(_driver->isAutoDisableActive() ? "ON" : "OFF");
    
    if (status.loadValue > 0) {
        Serial.print("  Load (SG):    ");
        Serial.println(status.loadValue);
    }
    
    if (status.hasError()) {
        Serial.println("\n  ⚠ ERRORS:");
        if (status.errorFlags & MotorError::OVER_TEMP)
            Serial.println("    - Over temperature!");
        if (status.errorFlags & MotorError::SHORT_CIRCUIT)
            Serial.println("    - Short circuit detected!");
        if (status.errorFlags & MotorError::OPEN_LOAD)
            Serial.println("    - Open load (motor disconnected?)");
        if (status.errorFlags & MotorError::COMM_FAILURE)
            Serial.println("    - Communication failure!");
        if (status.errorFlags & MotorError::STALL_DETECTED)
            Serial.println("    - Stall detected!");
    }
    
    Serial.println();
}

IMotorDriver* MotorController::getDriver() {
    return _driver;
}

bool MotorController::hasError() const {
    // Critical errors that should trigger ERROR LED state
    // Note: OPEN_LOAD is NOT included - motor may be intentionally disconnected
    constexpr uint8_t criticalErrors = 
        MotorError::OVER_TEMP | 
        MotorError::SHORT_CIRCUIT | 
        MotorError::COMM_FAILURE;
    
    return (_errorFlags & criticalErrors) != 0;
}

uint8_t MotorController::getErrorFlags() const {
    return _errorFlags;
}
