/**
 * @file MCPWMStepper.h
 * @brief Hardware PWM-based stepper driver using ESP32-S3 MCPWM peripheral
 * 
 * @details
 * This class provides high-speed, CPU-independent step pulse generation using
 * the ESP32-S3's Motor Control PWM (MCPWM) peripheral. Benefits include:
 * - Precise timing up to 100kHz step rates (vs ~8kHz software limit)
 * - Zero CPU overhead for pulse generation
 * - Hardware-guaranteed 50% duty cycle
 * - Smooth frequency updates for acceleration profiles
 * 
 * @note Requires ESP-IDF MCPWM driver (available in Arduino-ESP32 framework)
 * 
 * @author Universal Motor Module Team
 * @date 2025
 */

#ifndef MCPWM_STEPPER_H
#define MCPWM_STEPPER_H

#include <Arduino.h>
#include "driver/mcpwm.h"

/**
 * @class MCPWMStepper
 * @brief Hardware PWM generator for stepper motor step pulses
 * 
 * Uses ESP32-S3 MCPWM Unit 0, Timer 0, Generator A for step pulse output.
 * Direction control remains on separate GPIO via direct writes.
 */
class MCPWMStepper {
public:
    /**
     * @brief Constructor
     */
    MCPWMStepper();

    /**
     * @brief Destructor - stops PWM and releases resources
     */
    ~MCPWMStepper();

    /**
     * @brief Initialize MCPWM peripheral with specified pins
     * 
     * @param stepPin GPIO pin for STEP output (connected to stepper driver)
     * @param dirPin GPIO pin for DIRECTION control (controlled separately)
     * @return true if initialization successful, false on error
     * 
     * @note Call this once during driver initialization
     * @note DIR pin is configured but controlled via setDirection(), not MCPWM
     */
    bool init(gpio_num_t stepPin, gpio_num_t dirPin);

    /**
     * @brief Set step pulse frequency
     * 
     * @param stepsPerSecond Desired frequency in Hz (clamped to 10-100000 Hz)
     * 
     * @details
     * - Minimum: 10 Hz (avoid motor stall)
     * - Maximum: 100 kHz (hardware limit, practical limit ~50 kHz)
     * - Automatically maintains 50% duty cycle
     * - Can be called while PWM is running for smooth acceleration
     * 
     * @note Frequency updates are atomic and glitch-free
     */
    void setFrequency(float stepsPerSecond);

    /**
     * @brief Set motor direction
     * 
     * @param forward true = forward, false = reverse
     * 
     * @note Should be called while PWM is stopped to avoid glitches
     * @note Implementation uses direct GPIO write, not MCPWM
     */
    void setDirection(bool forward);

    /**
     * @brief Start PWM output (begin generating step pulses)
     * 
     * @note No effect if already running
     * @note Resumes at last set frequency
     */
    void start();

    /**
     * @brief Stop PWM output (cease generating step pulses)
     * 
     * @note Stops output immediately
     * @note Frequency setting is preserved
     */
    void stop();

    /**
     * @brief Check if PWM is currently running
     * 
     * @return true if generating pulses, false if stopped
     */
    bool isRunning() const;

    /**
     * @brief Get current configured frequency
     * 
     * @return Current frequency in steps/second
     */
    float getFrequency() const;

private:
    gpio_num_t _stepPin;      ///< Step pulse output pin
    gpio_num_t _dirPin;       ///< Direction control pin
    bool _initialized;        ///< Initialization state
    bool _running;            ///< PWM running state
    float _currentFrequency;  ///< Current step frequency (Hz)

    static constexpr float MIN_FREQUENCY = 10.0f;      ///< Minimum safe frequency (Hz)
    static constexpr float MAX_FREQUENCY = 100000.0f;  ///< Maximum hardware frequency (Hz)
    static constexpr float DUTY_CYCLE = 50.0f;         ///< Fixed 50% duty cycle

    // MCPWM peripheral configuration
    static constexpr mcpwm_unit_t MCPWM_UNIT = MCPWM_UNIT_0;
    static constexpr mcpwm_timer_t MCPWM_TIMER = MCPWM_TIMER_0;
    static constexpr mcpwm_io_signals_t MCPWM_OUTPUT = MCPWM0A;
    static constexpr mcpwm_operator_t MCPWM_OPERATOR = MCPWM_OPR_A;

    /**
     * @brief Clamp frequency to valid range
     * 
     * @param freq Input frequency
     * @return Clamped frequency between MIN_FREQUENCY and MAX_FREQUENCY
     */
    float clampFrequency(float freq) const;
};

#endif // MCPWM_STEPPER_H
