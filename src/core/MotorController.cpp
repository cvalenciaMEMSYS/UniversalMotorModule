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
        int32_t steps = command.substring(5).toInt();
        moveBy(steps);
    }
    else if (command.startsWith("abs ")) {
        // Absolute move: "abs 1000"
        int32_t position = command.substring(4).toInt();
        if (position >= 0) {
            moveTo(position);
        } else {
            Serial.println("ERROR: Position must be >= 0");
        }
    }
    else if (command == "home") {
        Serial.println("Homing...");
        home();
    }
    else if (command == "stop") {
        Serial.println("Emergency stop!");
        stop();
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
        setSpeed(speed);
        Serial.print("Speed set to ");
        Serial.print(speed);
        Serial.println(" steps/sec");
    }
    else if (param == "current") {
        uint16_t current = valueStr.toInt();
        setCurrent(current);
        Serial.print("Current set to ");
        Serial.print(current);
        Serial.println(" mA");
    }
    else if (param == "microsteps") {
        uint16_t ms = valueStr.toInt();
        setMicrosteps(ms);
        Serial.print("Microsteps set to ");
        Serial.println(ms);
    }
    else if (param == "accel" || param == "acceleration") {
        float accel = valueStr.toFloat();
        setAcceleration(accel);
        Serial.print("Acceleration set to ");
        Serial.print(accel);
        Serial.println(" steps/sec²");
    }
    else if (param == "jerk") {
        float jerk = valueStr.toFloat();
        setJerk(jerk);
        Serial.print("Jerk set to ");
        Serial.print(jerk);
        Serial.println(" steps/sec³");
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
    Serial.println("│    home              Find home position (TMC2209 only)      │");
    Serial.println("│    stop              Emergency stop                         │");
    Serial.println("│                                                             │");
    Serial.println("│  Control:                                                   │");
    Serial.println("│    enable            Enable motor driver                    │");
    Serial.println("│    disable           Disable motor driver                   │");
    Serial.println("│                                                             │");
    Serial.println("│  Configuration:                                             │");
    Serial.println("│    set speed <val>   Set max speed (steps/sec)              │");
    Serial.println("│    set current <mA>  Set motor current (UART mode only)     │");
    Serial.println("│    set microsteps <n> Set microstepping (1-256, UART only)  │");
    Serial.println("│    set accel <val>   Set acceleration (steps/sec²)          │");
    Serial.println("│    set jerk <val>    Set jerk for S-curve (steps/sec³)      │");
    Serial.println("│                                                             │");
    Serial.println("│  UART Control (TMC2209/TMC2208):                            │");
    Serial.println("│    stepdir on        Switch to Step/Dir fallback mode       │");
    Serial.println("│    stepdir off       Re-enable UART mode                    │");
    Serial.println("│    reconfigure       Re-apply UART settings                 │");
    Serial.println("│    stealthchop       Silent mode (may not work at fullstep) │");
    Serial.println("│    spreadcycle       High-torque mode (works at fullstep)   │");
    Serial.println("│    pwmautoscale on/off  Toggle automatic current reduction  │");
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
        _driver->setAcceleration(_acceleration);
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
    Serial.println("\n═══════════════════════════════════════════════════════════════");
    Serial.println("                        MOTOR STATUS");
    Serial.println("═══════════════════════════════════════════════════════════════\n");
    
    if (_driver == nullptr) {
        Serial.println("  ERROR: No driver initialized!");
        return;
    }
    
    MotorStatus status = _driver->getStatus();
    
    Serial.print("  Driver:        ");
    Serial.println(_driver->getName());
    
    Serial.print("  Enabled:       ");
    Serial.println(status.enabled ? "Yes ✓" : "No");
    
    Serial.print("  Position:      ");
    Serial.println(status.position);
    
    Serial.print("  Target:        ");
    Serial.println(status.targetPosition);
    
    Serial.print("  Moving:        ");
    Serial.println(status.moving ? "Yes" : "No");
    
    Serial.print("  Current Speed: ");
    Serial.print(status.currentSpeed);
    Serial.println(" steps/sec");
    
    Serial.print("  Run Current:   ");
    Serial.print(status.currentMA);
    Serial.println(" mA");
    
    if (status.loadValue > 0) {
        Serial.print("  Load (SG):     ");
        Serial.println(status.loadValue);
    }
    
    Serial.println("\n  Motion Control:");
    Serial.print("    Type:         FastAccelStepper");
    Serial.println();
    Serial.print("    Max Speed:    ");
    Serial.print(_maxSpeed);
    Serial.println(" steps/sec");
    Serial.print("    Acceleration: ");
    Serial.print(_acceleration);
    Serial.println(" steps/sec²");
    
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
    
    Serial.println("\n═══════════════════════════════════════════════════════════════\n");
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
