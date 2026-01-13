/**
 * @file FastAccelStepperWrapper.h
 * @brief Thin wrapper around FastAccelStepper library for consistent API
 * 
 * Provides same methods as old MCPWMStepper but uses FastAccelStepper internally.
 * This wrapper isolates the external library and makes future changes easier.
 * 
 * @author Universal Motor Module Team
 * @date 2026-01-13
 */

#ifndef FASTACCEL_STEPPER_WRAPPER_H
#define FASTACCEL_STEPPER_WRAPPER_H

#include <FastAccelStepper.h>

/**
 * @class FastAccelStepperWrapper
 * @brief Wrapper around FastAccelStepper providing MCPWMStepper-compatible API
 * 
 * This class provides the same interface as the old MCPWMStepper but uses
 * FastAccelStepper library internally. Benefits:
 * - No pulse gaps during acceleration (synchronous updates)
 * - Supports up to 200kHz on ESP32-S3 (vs 50kHz cap)
 * - Hardware position tracking (no runaway)
 * - Built-in acceleration profiles (no math bugs)
 */
class FastAccelStepperWrapper {
public:
    /**
     * @brief Constructor
     */
    FastAccelStepperWrapper();
    
    /**
     * @brief Destructor - clean up resources
     */
    ~FastAccelStepperWrapper();
    
    /**
     * @brief Initialize the stepper with pin assignments
     * 
     * @param stepPin GPIO pin for STEP output (hardware PWM pin)
     * @param dirPin GPIO pin for DIRECTION control
     * @return true if initialization successful, false on error
     */
    bool init(gpio_num_t stepPin, gpio_num_t dirPin);
    
    /**
     * @brief Set step pulse frequency (speed)
     * 
     * @param stepsPerSecond Desired frequency in Hz (10-200000 Hz)
     * 
     * @note Changes take effect immediately, no pulse gaps
     * @note FastAccelStepper handles this with hardware precision
     */
    void setFrequency(float stepsPerSecond);
    
    /**
     * @brief Get current configured frequency
     * 
     * @return Current frequency in steps/second
     */
    float getFrequency() const;
    
    /**
     * @brief Set motor direction
     * 
     * @param forward true = forward, false = reverse
     * 
     * @note FastAccelStepper manages direction automatically based on move commands
     * @note This method kept for API compatibility but not actively used
     */
    void setDirection(bool forward);
    
    /**
     * @brief Start PWM output (enable motor)
     */
    void start();
    
    /**
     * @brief Stop PWM output (disable motor)
     */
    void stop();
    
    /**
     * @brief Emergency stop - immediate halt
     * 
     * @note Stops motor immediately, bypasses deceleration
     */
    void emergencyStop();
    
    /**
     * @brief Check if stepper is currently moving
     * 
     * @return true if generating pulses, false if stopped
     */
    bool isRunning() const;
    
    /**
     * @brief Get actual pulse count from hardware
     * 
     * @return Number of steps taken since start/reset
     * 
     * @note Hardware-accurate position tracking (no drift)
     */
    int32_t getPosition() const;
    
    /**
     * @brief Set position counter value
     * 
     * @param position New position value (steps)
     * 
     * @note Updates internal position counter, does not move motor
     */
    void setPosition(int32_t position);
    
    /**
     * @brief Reset position counter to zero
     */
    void resetPosition();
    
    /**
     * @brief Get current speed
     * 
     * @return Current speed in steps/s
     */
    float getCurrentSpeed() const;
    
    /**
     * @brief Move to absolute position
     * 
     * @param position Target position (steps)
     * 
     * @note FastAccelStepper handles acceleration automatically
     */
    void moveTo(int32_t position);
    
    /**
     * @brief Move relative to current position
     * 
     * @param steps Number of steps (positive or negative)
     * 
     * @note FastAccelStepper handles acceleration automatically
     */
    void moveBy(int32_t steps);
    
    /**
     * @brief Set acceleration rate
     * 
     * @param accel Acceleration in steps/s²
     * 
     * @note Changes take effect on next move command
     */
    void setAcceleration(uint32_t accel);
    
    /**
     * @brief Check if motor is currently moving
     * 
     * @return true if motion in progress, false if stopped
     */
    bool isMoving() const;

private:
    FastAccelStepperEngine* _engine;      ///< FastAccelStepper engine instance
    FastAccelStepper* _stepper;           ///< Stepper controller instance
    gpio_num_t _stepPin;                  ///< Step pulse output pin
    gpio_num_t _dirPin;                   ///< Direction control pin
    bool _initialized;                    ///< Initialization state
    float _currentFrequency;              ///< Current configured frequency (Hz)
};

#endif // FASTACCEL_STEPPER_WRAPPER_H
