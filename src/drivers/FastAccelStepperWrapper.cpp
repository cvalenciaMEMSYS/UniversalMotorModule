/**
 * @file FastAccelStepperWrapper.cpp
 * @brief Implementation of FastAccelStepper wrapper
 */

#include "FastAccelStepperWrapper.h"
#include <Arduino.h>

FastAccelStepperWrapper::FastAccelStepperWrapper()
    : _engine(nullptr)
    , _stepper(nullptr)
    , _stepPin(GPIO_NUM_NC)
    , _dirPin(GPIO_NUM_NC)
    , _initialized(false)
    , _currentFrequency(1000.0f) {
}

FastAccelStepperWrapper::~FastAccelStepperWrapper() {
    if (_stepper) {
        _stepper->disableOutputs();
    }
    // Note: _engine is singleton, don't delete
}

bool FastAccelStepperWrapper::init(gpio_num_t stepPin, gpio_num_t dirPin) {
    _stepPin = stepPin;
    _dirPin = dirPin;
    
    Serial.println("[FastAccel] Initializing...");
    
    // Get/create FastAccelStepper engine (singleton pattern)
    static FastAccelStepperEngine engine = FastAccelStepperEngine();
    static bool engineInitialized = false;
    
    if (!engineInitialized) {
        engine.init();
        engineInitialized = true;
        Serial.println("[FastAccel] Engine initialized");
    }
    
    _engine = &engine;
    
    // Create stepper attached to step pin
    // ESP32-S3 uses MCPWM driver by default (not RMT)
    _stepper = _engine->stepperConnectToPin((uint8_t)_stepPin);
    if (_stepper == nullptr) {
        Serial.println("[FastAccel] ERROR: Failed to create stepper");
        Serial.print("[FastAccel] Step pin GPIO");
        Serial.print(_stepPin);
        Serial.println(" may not be usable or no resources available");
        return false;
    }
    
    Serial.println("[FastAccel] Stepper created successfully");
    
    // Configure direction pin
    _stepper->setDirectionPin((uint8_t)_dirPin, true, 0);  // dirHighCountsUp=true, no delay
    
    // Set default speed and acceleration
    _stepper->setSpeedInHz((uint32_t)_currentFrequency);
    _stepper->setAcceleration(500);  // 500 steps/s² default
    
    // Enable outputs
    _stepper->enableOutputs();
    
    _initialized = true;
    
    Serial.println("[FastAccel] Initialized successfully");
    Serial.print("[FastAccel] Step pin: GPIO");
    Serial.print(_stepPin);
    Serial.print(", Dir pin: GPIO");
    Serial.println(_dirPin);
    Serial.print("[FastAccel] Max speed capability: ");
    Serial.print(_stepper->getMaxSpeedInHz());
    Serial.println(" Hz");
    
    return true;
}

void FastAccelStepperWrapper::setFrequency(float stepsPerSecond) {
    if (!_initialized || !_stepper) {
        Serial.println("[FastAccel] WARNING: Not initialized, cannot set frequency");
        return;
    }
    
    // Clamp to reasonable values (10 Hz to 200 kHz)
    if (stepsPerSecond < 10.0f) {
        stepsPerSecond = 10.0f;
    }
    uint32_t maxSpeed = _stepper->getMaxSpeedInHz();
    if (stepsPerSecond > (float)maxSpeed) {
        stepsPerSecond = (float)maxSpeed;
    }
    
    _currentFrequency = stepsPerSecond;
    _stepper->setSpeedInHz((uint32_t)stepsPerSecond);
}

float FastAccelStepperWrapper::getFrequency() const {
    return _currentFrequency;
}

void FastAccelStepperWrapper::setDirection(bool forward) {
    // Direction is handled automatically by FastAccelStepper
    // based on move commands (positive = forward, negative = reverse)
    // This method kept for API compatibility but not used
    
    // If you need to invert direction globally, use:
    // _stepper->setDirectionPin(_dirPin, !forward);
}

void FastAccelStepperWrapper::start() {
    if (!_initialized || !_stepper) return;
    _stepper->enableOutputs();
}

void FastAccelStepperWrapper::stop() {
    if (!_initialized || !_stepper) return;
    _stepper->forceStopAndNewPosition(_stepper->getCurrentPosition());
}

void FastAccelStepperWrapper::emergencyStop() {
    if (!_initialized || !_stepper) return;
    // Emergency stop: force stop immediately
    _stepper->forceStop();
}

bool FastAccelStepperWrapper::isRunning() const {
    if (!_initialized || !_stepper) return false;
    return isMoving();
}

int32_t FastAccelStepperWrapper::getPosition() const {
    if (!_initialized || !_stepper) return 0;
    return _stepper->getCurrentPosition();
}

void FastAccelStepperWrapper::setPosition(int32_t position) {
    if (!_initialized || !_stepper) return;
    _stepper->setCurrentPosition(position);
}

void FastAccelStepperWrapper::resetPosition() {
    if (!_initialized || !_stepper) return;
    _stepper->setCurrentPosition(0);
}

float FastAccelStepperWrapper::getCurrentSpeed() const {
    if (!_initialized || !_stepper) return 0.0f;
    // FastAccelStepper doesn't expose current speed directly,
    // but we can return the configured frequency as an estimate
    return _currentFrequency;
}

void FastAccelStepperWrapper::moveTo(int32_t position) {
    if (!_initialized || !_stepper) return;
    _stepper->moveTo(position);
}

void FastAccelStepperWrapper::moveBy(int32_t steps) {
    if (!_initialized || !_stepper) return;
    _stepper->move(steps);  // Relative move
}

void FastAccelStepperWrapper::setAcceleration(uint32_t accel) {
    if (!_initialized || !_stepper) return;
    _stepper->setAcceleration(accel);
}

bool FastAccelStepperWrapper::isMoving() const {
    if (!_initialized || !_stepper) return false;
    return _stepper->isRunning();
}
