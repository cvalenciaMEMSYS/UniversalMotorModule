/*
 * =============================================================================
 * STSPIN220 DRIVER - Implementation
 * =============================================================================
 * Simplest stepper driver — Step/Dir only, no communication.
 * All motion planning delegated to FastAccelStepper library.
 * 
 * Key characteristics:
 *   - No UART, no SPI, no communication of any kind
 *   - No enable pin — driver auto-manages power internally
 *   - Current limit set by Vref potentiometer (hardware)
 *   - Microstepping set by MODE1/MODE2 pins (hardware)
 *   - No StallGuard, no StealthChop, no diagnostics
 */

#include "STSPIN220Driver.h"

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

STSPIN220Driver::STSPIN220Driver()
    : STSPIN220Driver(TMC_STEP_PIN, TMC_DIR_PIN) {
}

STSPIN220Driver::STSPIN220Driver(uint8_t stepPin, uint8_t dirPin)
    : _stepPin(stepPin)
    , _dirPin(dirPin)
    , _enabled(true)       // Always "enabled" — no EN pin
    , _position(0)
    , _targetPosition(0)
    , _moving(false)
    , _maxSpeed(DefaultMotorConfig::STEPPER_MAX_SPEED)
    , _acceleration(DefaultMotorConfig::STEPPER_ACCELERATION) {
}

STSPIN220Driver::~STSPIN220Driver() {
    // FastAccelStepperWrapper cleans up in its own destructor
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool STSPIN220Driver::init() {
    Serial.println("STSPIN220 Driver: Initializing...");
    
    // Initialize direction pin
    pinMode(_dirPin, OUTPUT);
    digitalWrite(_dirPin, LOW);
    
    // Initialize FastAccelStepper — NO enable pin (GPIO_NUM_NC)
    // The STSPIN220 auto-manages its own enable/standby state
    if (!_stepper.init((gpio_num_t)_stepPin, (gpio_num_t)_dirPin, GPIO_NUM_NC)) {
        Serial.println("STSPIN220 Driver: FastAccelStepper initialization failed!");
        return false;
    }
    Serial.println("STSPIN220 Driver: FastAccelStepper initialized ✓");
    
    // Set default speed and acceleration
    _stepper.setFrequency(_maxSpeed);
    _stepper.setAcceleration(_acceleration);
    
    _enabled = true;  // Always "enabled" — no EN pin to control
    
    Serial.println("STSPIN220 Driver: Ready");
    Serial.println("  Note: No UART — current and microstepping are hardware-configured");
    Serial.println("  Note: No EN pin — driver auto-manages standby internally");
    
    return true;
}

// =============================================================================
// ENABLE / DISABLE (No-ops — STSPIN220 has no controllable enable pin)
// =============================================================================

void STSPIN220Driver::enable() {
    _enabled = true;
    // No physical EN pin to toggle — STSPIN220 auto-enables on step pulses
}

void STSPIN220Driver::disable() {
    // Stop any motion first
    if (_moving) {
        emergencyStop();
    }
    _enabled = false;
    // No physical pin to disable — STSPIN220 enters standby automatically
    Serial.println("STSPIN220: Logical disable (driver auto-manages standby)");
}

bool STSPIN220Driver::isEnabled() const {
    return _enabled;
}

// =============================================================================
// MOTION CONTROL - Delegated to FastAccelStepper
// =============================================================================

void STSPIN220Driver::move(int32_t steps) {
    _enabled = true;  // Ensure logical enable
    if (steps == 0) return;
    
    _stepper.moveBy(steps);
    _moving = true;
    _targetPosition = _position + steps;
}

void STSPIN220Driver::moveTo(int32_t position) {
    _enabled = true;  // Ensure logical enable
    if (position < 0) position = 0;
    
    _stepper.moveTo(position);
    _moving = true;
    _targetPosition = position;
}

void STSPIN220Driver::stop() {
    // Controlled stop with deceleration
    _stepper.stop();
    _moving = false;
    _targetPosition = _stepper.getPosition();
}

void STSPIN220Driver::emergencyStop() {
    // Immediate halt — no deceleration
    _stepper.emergencyStop();
    _moving = false;
    _position = _stepper.getPosition();
    _targetPosition = _position;
}

bool STSPIN220Driver::isMoving() const {
    return _stepper.isMoving();
}

void STSPIN220Driver::update() {
    // FastAccelStepper runs via hardware interrupts
    // Just sync position tracking
    _position = _stepper.getPosition();
    _moving = _stepper.isMoving();
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void STSPIN220Driver::setMaxSpeed(float stepsPerSecond) {
    if (stepsPerSecond <= 0) stepsPerSecond = 1;
    _maxSpeed = stepsPerSecond;
    _stepper.setFrequency(stepsPerSecond);
}

void STSPIN220Driver::setAcceleration(float accelStepsPerSecondSquared) {
    if (accelStepsPerSecondSquared <= 0) accelStepsPerSecondSquared = 100;
    _acceleration = accelStepsPerSecondSquared;
    _stepper.setAcceleration(accelStepsPerSecondSquared);
}

void STSPIN220Driver::setCurrent(uint16_t runMA, uint16_t holdMA) {
    (void)runMA;
    (void)holdMA;
    Serial.println("STSPIN220: Current is hardware-configured (Vref potentiometer)");
}

void STSPIN220Driver::setMicrosteps(uint16_t microsteps) {
    (void)microsteps;
    Serial.println("STSPIN220: Microstepping is hardware-configured (MODE1/MODE2 pins)");
}

// =============================================================================
// POSITION
// =============================================================================

int32_t STSPIN220Driver::getPosition() const {
    return _position;
}

void STSPIN220Driver::setPosition(int32_t position) {
    _position = position;
    _stepper.setPosition(position);
}

void STSPIN220Driver::home(int8_t direction) {
    (void)direction;
    Serial.println("STSPIN220: Sensorless homing not available (no StallGuard)");
    Serial.println("           Use external limit switches for homing");
    _position = 0;
    _targetPosition = 0;
    setPosition(0);
}

// =============================================================================
// DIAGNOSTICS
// =============================================================================

MotorStatus STSPIN220Driver::getStatus() {
    MotorStatus status;
    
    status.enabled = _enabled;
    status.moving = _moving;
    status.stalling = false;  // No stall detection
    
    status.position = _position;
    status.targetPosition = _targetPosition;
    
    status.currentMA = 0;     // Unknown — hardware-configured
    status.loadValue = 0;     // No StallGuard
    status.currentSpeed = _stepper.getCurrentSpeed();
    
    status.errorFlags = MotorError::NONE;  // No way to detect errors
    
    return status;
}

void STSPIN220Driver::printDiagnostics() {
    Serial.println("\n═══════════════════════════════════════════════════════════════");
    Serial.println("                STSPIN220 DIAGNOSTICS");
    Serial.println("═══════════════════════════════════════════════════════════════\n");
    
    Serial.println("  Driver:       STSPIN220 (Step/Dir only)");
    Serial.println("  Communication: None");
    
    Serial.print("  Position:     ");
    Serial.print(_position);
    Serial.println(" steps");
    
    Serial.print("  Target:       ");
    Serial.print(_targetPosition);
    Serial.println(" steps");
    
    Serial.print("  Moving:       ");
    Serial.println(_moving ? "Yes" : "No");
    
    Serial.print("  Speed:        ");
    Serial.print(_stepper.getCurrentSpeed());
    Serial.print(" / ");
    Serial.print((int)_maxSpeed);
    Serial.println(" steps/s (current / max)");
    
    Serial.print("  Acceleration: ");
    Serial.print((int)_acceleration);
    Serial.println(" steps/s²");
    
    uint32_t linAccel = _stepper.getLinearAcceleration();
    Serial.print("  Cubesteps:    ");
    Serial.print(linAccel);
    Serial.println(linAccel > 0 ? " (S-curve)" : " (trapezoidal)");
    
    Serial.println("\n  Note: No UART — cannot read driver registers");
    Serial.println("  Note: Current limit set by Vref potentiometer");
    Serial.println("  Note: Microstepping set by MODE1/MODE2 pins");
    Serial.println("  Note: No StallGuard — use limit switches for homing");
    
    Serial.println("\n═══════════════════════════════════════════════════════════════\n");
}

// =============================================================================
// FASTACCELSTEPPER-BASED METHODS
// =============================================================================

void STSPIN220Driver::setLinearAcceleration(uint32_t steps) {
    _stepper.setLinearAcceleration(steps);
    Serial.print("Linear acceleration set to ");
    Serial.print(steps);
    Serial.println(" steps (cubesteps)");
}

uint32_t STSPIN220Driver::getLinearAcceleration() const {
    return _stepper.getLinearAcceleration();
}

void STSPIN220Driver::setHoldCurrentPercent(uint8_t percent) {
    (void)percent;
    Serial.println("STSPIN220: Hold current is hardware-configured");
}

uint8_t STSPIN220Driver::getHoldCurrentPercent() const {
    return 0;  // Unknown
}

void STSPIN220Driver::setAutoDisable(bool enable) {
    (void)enable;
    // No EN pin to auto-control — STSPIN220 manages standby internally
    // No-op, but don't print a warning since this is called during init
}

bool STSPIN220Driver::isAutoDisableActive() const {
    return false;  // No EN pin, so auto-disable concept doesn't apply
}

void STSPIN220Driver::runForward() {
    _enabled = true;
    _stepper.runForward();
    _moving = true;
    Serial.println("Running forward continuously...");
}

void STSPIN220Driver::runBackward() {
    _enabled = true;
    _stepper.runBackward();
    _moving = true;
    Serial.println("Running backward continuously...");
}

void STSPIN220Driver::brake() {
    _stepper.brake();
    Serial.println("Braking...");
}

int32_t STSPIN220Driver::getTargetPosition() const {
    return _stepper.getTargetPosition();
}

int32_t STSPIN220Driver::getActualSpeed() const {
    return _stepper.getActualSpeed();
}

uint8_t STSPIN220Driver::getRampState() const {
    return _stepper.getRampState();
}

bool STSPIN220Driver::isRunningContinuously() const {
    return _stepper.isRunningContinuously();
}
