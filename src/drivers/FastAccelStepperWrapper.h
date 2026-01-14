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
     * @param enPin GPIO pin for ENABLE control (optional, 255 = none)
     * @return true if initialization successful, false on error
     */
    bool init(gpio_num_t stepPin, gpio_num_t dirPin, gpio_num_t enPin = GPIO_NUM_NC);
    
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
    
    /**
     * @brief Run continuously forward
     * 
     * @note Runs at configured speed until stop/brake is called
     */
    void runForward();
    
    /**
     * @brief Run continuously backward
     * 
     * @note Runs at configured speed until stop/brake is called
     */
    void runBackward();
    
    /**
     * @brief Controlled stop with deceleration
     * 
     * @note Decelerates using configured acceleration to halt
     */
    void brake();
    
    /**
     * @brief Set linear acceleration (S-curve) steps
     * 
     * @param steps Number of steps for acceleration ramp (0=trapezoidal)
     * 
     * @note Higher values = smoother motion, slower start
     */
    void setLinearAcceleration(uint32_t steps);
    
    /**
     * @brief Get current linear acceleration setting
     * 
     * @return Current cubesteps value (0 if trapezoidal)
     */
    uint32_t getLinearAcceleration() const;
    
    /**
     * @brief Enable/disable auto-enable feature
     * 
     * @param enable true = auto enable/disable, false = manual control
     */
    void setAutoEnable(bool enable);
    
    /**
     * @brief Check if auto-enable is active
     * 
     * @return true if auto-enable is on
     */
    bool isAutoEnableActive() const;
    
    /**
     * @brief Get target position for current move
     * 
     * @return Target position in steps
     */
    int32_t getTargetPosition() const;
    
    /**
     * @brief Get actual current speed (not configured max)
     * 
     * @return Current speed in steps/sec (signed, negative = reverse)
     */
    int32_t getActualSpeed() const;
    
    /**
     * @brief Get ramp state
     * 
     * @return Ramp state (IDLE, ACCELERATING, COASTING, DECELERATING)
     */
    uint8_t getRampState() const;
    
    /**
     * @brief Check if running continuously
     * 
     * @return true if in continuous run mode
     */
    bool isRunningContinuously() const;

private:
    FastAccelStepperEngine* _engine;      ///< FastAccelStepper engine instance
    FastAccelStepper* _stepper;           ///< Stepper controller instance
    gpio_num_t _stepPin;                  ///< Step pulse output pin
    gpio_num_t _dirPin;                   ///< Direction control pin
    gpio_num_t _enPin;                    ///< Enable control pin
    bool _initialized;                    ///< Initialization state
    float _currentFrequency;              ///< Current configured frequency (Hz)
    uint32_t _linearAccelSteps;           ///< S-curve ramp steps (0=trapezoidal)
    bool _autoEnableActive;               ///< Auto enable/disable state
};

#endif // FASTACCEL_STEPPER_WRAPPER_H
