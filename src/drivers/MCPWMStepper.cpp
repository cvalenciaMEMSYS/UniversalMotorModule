/**
 * @file MCPWMStepper.cpp
 * @brief Implementation of hardware PWM stepper driver
 * 
 * @author Universal Motor Module Team
 * @date 2025
 */

#include "MCPWMStepper.h"

// =============================================================================
// Constructor / Destructor
// =============================================================================

MCPWMStepper::MCPWMStepper()
    : _stepPin(GPIO_NUM_NC)
    , _dirPin(GPIO_NUM_NC)
    , _initialized(false)
    , _running(false)
    , _currentFrequency(1000.0f)  // Default 1kHz
{
}

MCPWMStepper::~MCPWMStepper() {
    if (_initialized) {
        stop();
        // Note: mcpwm_stop() doesn't fully deinitialize, but that's okay
        // for a single-use driver. A full app reset will clean up hardware.
    }
}

// =============================================================================
// Initialization
// =============================================================================

bool MCPWMStepper::init(gpio_num_t stepPin, gpio_num_t dirPin) {
    if (_initialized) {
        Serial.println("[MCPWM] Already initialized");
        return false;
    }

    _stepPin = stepPin;
    _dirPin = dirPin;

    // Configure direction pin as regular GPIO output
    pinMode(_dirPin, OUTPUT);
    digitalWrite(_dirPin, LOW);

    // Configure MCPWM GPIO
    mcpwm_gpio_init(MCPWM_UNIT, MCPWM_OUTPUT, _stepPin);

    // Configure MCPWM with initial settings
    mcpwm_config_t pwm_config;
    pwm_config.frequency = (uint32_t)_currentFrequency;
    pwm_config.cmpr_a = DUTY_CYCLE;
    pwm_config.cmpr_b = 0.0f;  // Not used
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;  // Active high
    pwm_config.counter_mode = MCPWM_UP_COUNTER;

    esp_err_t err = mcpwm_init(MCPWM_UNIT, MCPWM_TIMER, &pwm_config);
    if (err != ESP_OK) {
        Serial.print("[MCPWM] Init failed: ");
        Serial.println(err);
        return false;
    }

    // Start with PWM stopped
    mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER);

    _initialized = true;
    _running = false;

    Serial.println("[MCPWM] Initialized successfully");
    Serial.print("[MCPWM] Step pin: GPIO");
    Serial.print(_stepPin);
    Serial.print(", Dir pin: GPIO");
    Serial.println(_dirPin);

    return true;
}

// =============================================================================
// Frequency Control
// =============================================================================

void MCPWMStepper::setFrequency(float stepsPerSecond) {
    if (!_initialized) {
        Serial.println("[MCPWM] Not initialized, cannot set frequency");
        return;
    }

    // Clamp to valid range
    _currentFrequency = clampFrequency(stepsPerSecond);

    // Update MCPWM frequency
    mcpwm_set_frequency(MCPWM_UNIT, MCPWM_TIMER, (uint32_t)_currentFrequency);

    // Maintain 50% duty cycle
    mcpwm_set_duty(MCPWM_UNIT, MCPWM_TIMER, MCPWM_OPERATOR, DUTY_CYCLE);
    mcpwm_set_duty_type(MCPWM_UNIT, MCPWM_TIMER, MCPWM_OPERATOR, MCPWM_DUTY_MODE_0);
}

float MCPWMStepper::clampFrequency(float freq) const {
    if (freq < MIN_FREQUENCY) return MIN_FREQUENCY;
    if (freq > MAX_FREQUENCY) return MAX_FREQUENCY;
    return freq;
}

float MCPWMStepper::getFrequency() const {
    return _currentFrequency;
}

// =============================================================================
// Direction Control
// =============================================================================

void MCPWMStepper::setDirection(bool forward) {
    if (!_initialized) {
        return;
    }

    digitalWrite(_dirPin, forward ? HIGH : LOW);

    // Small delay to ensure direction is stable before stepping
    // (typical stepper drivers need 200-1000ns setup time)
    delayMicroseconds(1);
}

// =============================================================================
// Start / Stop Control
// =============================================================================

void MCPWMStepper::start() {
    if (!_initialized) {
        Serial.println("[MCPWM] Not initialized, cannot start");
        return;
    }

    if (_running) {
        return;  // Already running
    }

    mcpwm_start(MCPWM_UNIT, MCPWM_TIMER);
    _running = true;
}

void MCPWMStepper::stop() {
    if (!_initialized) {
        return;
    }

    if (!_running) {
        return;  // Already stopped
    }

    mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER);
    _running = false;
}

bool MCPWMStepper::isRunning() const {
    return _running;
}
