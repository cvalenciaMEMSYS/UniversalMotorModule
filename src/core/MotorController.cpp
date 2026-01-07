/*
 * =============================================================================
 * MOTOR CONTROLLER - Implementation
 * =============================================================================
 */

#include "MotorController.h"
#include "../config/PinConfig.h"
#include "StatusLED.h"

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

MotorController::MotorController()
    : _driver(nullptr)
    , _initialized(false)
    , _wasMoving(false)
    , _errorFlags(MotorError::NONE)
    , _lastErrorPollTime(0) {
    // Default profile: constant velocity
    _profile = AccelerationProfile::constant(DefaultMotorConfig::STEPPER_MAX_SPEED);
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
    
    // Apply default profile
    _driver->setAccelerationProfile(_profile);
    
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
            // Movement just finished
            Serial.println("Complete");
        }
        _wasMoving = isCurrentlyMoving;
        
        // Poll for errors periodically
        uint32_t now = millis();
        if (now - _lastErrorPollTime >= ERROR_POLL_INTERVAL_MS) {
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
        // Scan for drivers (TMC2209 specific)
        // Cast to TMC2209Driver if applicable
        if (_driver->getType() == MotorType::STEPPER_TMC2209) {
            // Ideally we'd have a way to access TMC-specific methods
            // For now, just note this
            Serial.println("Address scanning available via TMC2209Driver");
        }
    }
    else if (command == "reconfigure" || command == "reconfig") {
        // Reconfigure driver (useful after power glitch)
        if (_driver->getType() == MotorType::STEPPER_TMC2209) {
            Serial.println("Reconfiguring TMC2209...");
            // Access TMC-specific reconfigure via init
            _driver->init();
        } else {
            Serial.println("Reconfigure not needed for this driver");
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
    Serial.println("│    home              Find home position                     │");
    Serial.println("│    stop              Emergency stop                         │");
    Serial.println("│                                                             │");
    Serial.println("│  Control:                                                   │");
    Serial.println("│    enable            Enable motor driver                    │");
    Serial.println("│    disable           Disable motor driver                   │");
    Serial.println("│                                                             │");
    Serial.println("│  Configuration:                                             │");
    Serial.println("│    set speed <val>   Set max speed (steps/sec)              │");
    Serial.println("│    set current <mA>  Set motor current                      │");
    Serial.println("│    set microsteps <n> Set microstepping (1-256)             │");
    Serial.println("│    set accel <val>   Set acceleration (steps/sec²)          │");
    Serial.println("│    set jerk <val>    Set jerk for S-curve (steps/sec³)      │");
    Serial.println("│                                                             │");
    Serial.println("│  Status/Debug:                                              │");
    Serial.println("│    ? or status       Show current status                    │");
    Serial.println("│    t or test         Test driver connection                 │");
    Serial.println("│    r or diag         Full diagnostics                       │");
    Serial.println("│    reconfigure       Re-apply settings (after power glitch) │");
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
    _profile.maxSpeed = stepsPerSec;
    if (_driver != nullptr) {
        _driver->setMaxSpeed(stepsPerSec);
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
    _profile.acceleration = accel;
    
    // Update profile type based on accel value
    if (accel > 0) {
        // If jerk is 0, use TRAPEZOIDAL; otherwise stay in S_CURVE
        if (_profile.jerk == 0) {
            _profile.type = VelocityProfileType::TRAPEZOIDAL;
        }
    } else {
        // accel = 0: revert to CONSTANT (also reset jerk to prevent confusion)
        _profile.type = VelocityProfileType::CONSTANT;
        _profile.jerk = 0;
    }
    
    if (_driver != nullptr) {
        _driver->setAccelerationProfile(_profile);
    }
    
    // Update LED to reflect profile change
    statusLED.setAccelProfile(static_cast<AccelProfile>(_profile.type));
}

void MotorController::setJerk(float jerk) {
    _profile.jerk = jerk;
    
    // Update profile type based on jerk value
    if (jerk > 0) {
        _profile.type = VelocityProfileType::S_CURVE;
    } else {
        // jerk = 0: revert to TRAPEZOIDAL if accel > 0, otherwise CONSTANT
        if (_profile.acceleration > 0) {
            _profile.type = VelocityProfileType::TRAPEZOIDAL;
        } else {
            _profile.type = VelocityProfileType::CONSTANT;
        }
    }
    
    if (_driver != nullptr) {
        _driver->setAccelerationProfile(_profile);
    }
    
    // Update LED to reflect profile change
    statusLED.setAccelProfile(static_cast<AccelProfile>(_profile.type));
}

void MotorController::enableMotor() {
    if (_driver != nullptr) {
        _driver->enable();
    }
}

void MotorController::disableMotor() {
    if (_driver != nullptr) {
        _driver->disable();
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
    
    Serial.println("\n  Profile:");
    Serial.print("    Type:         ");
    Serial.println(_profile.getTypeName());
    Serial.print("    Max Speed:    ");
    Serial.print(_profile.maxSpeed);
    Serial.println(" steps/sec");
    Serial.print("    Acceleration: ");
    Serial.print(_profile.acceleration);
    Serial.println(" steps/sec²");
    Serial.print("    Jerk:         ");
    Serial.print(_profile.jerk);
    Serial.println(" steps/sec³");
    
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
