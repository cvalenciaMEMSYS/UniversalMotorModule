/*
 * =============================================================================
 * MOTOR CONTROLLER - High-Level Motor Control
 * =============================================================================
 * 
 * High-level controller that provides a unified interface for motor control.
 * Handles:
 *   - Serial command parsing
 *   - Automatic hardware detection
 *   - Motion coordination
 *   - Status reporting
 * 
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include "../drivers/IMotorDriver.h"
#include "../drivers/DriverFactory.h"
// Note: AccelerationProfile.h removed - now handled by FastAccelStepper library

// FIX #5: Input validation limits
namespace MotorLimits {
    constexpr int32_t MAX_MOVE_STEPS = 1000000;       // ±1M steps max per command
    constexpr int32_t MAX_POSITION = 100000000;       // 100M steps max absolute position
    constexpr float MIN_SPEED = 1.0f;                 // 1 step/s minimum
    constexpr float MAX_SPEED = 200000.0f;            // 200kHz maximum
    constexpr float MIN_ACCELERATION = 0.0f;          // 0 = constant velocity (no accel)
    constexpr float MAX_ACCELERATION = 1000000.0f;    // 1M steps/s² maximum
    constexpr uint16_t MIN_CURRENT_MA = 100;          // 100mA minimum
    constexpr uint16_t MAX_CURRENT_MA = 3000;         // 3A maximum
    constexpr uint16_t MIN_MICROSTEPS = 1;            // Full step
    constexpr uint16_t MAX_MICROSTEPS = 256;          // 256 microsteps
}

/**
 * @brief High-level motor controller
 * 
 * Abstracts away the specific motor driver and provides a consistent
 * interface for motor control via serial commands or programmatic API.
 */
class MotorController {
public:
    MotorController();
    ~MotorController();
    
    // =========================================================================
    // Lifecycle
    // =========================================================================
    
    /**
     * @brief Initialize the controller
     * 
     * Detects hardware, creates appropriate driver, initializes communication.
     * 
     * @return true if initialization successful
     */
    bool begin();
    
    /**
     * @brief Update function - call in loop()
     * 
     * Processes ongoing motion, handles acceleration, etc.
     */
    void update();
    
    /**
     * @brief Check if controller is ready
     */
    bool isReady() const;
    
    /**
     * @brief Check if motor is currently busy
     */
    bool isBusy() const;
    
    // =========================================================================
    // Command Interface
    // =========================================================================
    
    /**
     * @brief Process a command string
     * @param cmd Command string (e.g., "move 100", "set speed 500")
     */
    void processCommand(const String& cmd);
    
    /**
     * @brief Print available commands
     */
    void printHelp();
    
    // =========================================================================
    // Motion Control
    // =========================================================================
    
    /**
     * @brief Move relative to current position
     * @param steps Number of steps (positive or negative)
     */
    void moveBy(int32_t steps);
    
    /**
     * @brief Move to absolute position
     * @param position Target position (>= 0)
     */
    void moveTo(int32_t position);
    
    /**
     * @brief Find home position
     */
    void home();
    
    /**
     * @brief Stop motor (with deceleration if configured)
     */
    void stop();
    
    // =========================================================================
    // Configuration
    // =========================================================================
    
    /**
     * @brief Set maximum speed
     * @param stepsPerSec Steps per second
     */
    void setSpeed(float stepsPerSec);
    
    /**
     * @brief Set motor current
     * @param mA Current in milliamps
     */
    void setCurrent(uint16_t mA);
    
    /**
     * @brief Set microstepping
     * @param ms Microstep value (1, 2, 4, 8, 16, 32, 64, 128, 256)
     */
    void setMicrosteps(uint16_t ms);
    
    /**
     * @brief Set acceleration
     * @param accel Acceleration in steps/sec²
     */
    void setAcceleration(float accel);
    
    /**
     * @brief Set jerk (for S-curve profiles)
     * @param jerk Jerk in steps/sec³
     */
    void setJerk(float jerk);
    
    /**
     * @brief Enable motor driver
     */
    void enableMotor();
    
    /**
     * @brief Disable motor driver
     */
    void disableMotor();
    
    // =========================================================================
    // Status
    // =========================================================================
    
    /**
     * @brief Print current status
     */
    void printStatus();
    
    /**
     * @brief Get the underlying driver (for advanced operations)
     */
    IMotorDriver* getDriver();
    
    /**
     * @brief Check if any critical error is active
     * Critical errors: OVER_TEMP, SHORT_CIRCUIT, COMM_FAILURE
     */
    bool hasError() const;
    
    /**
     * @brief Get current error flags
     */
    uint8_t getErrorFlags() const;

private:
    IMotorDriver* _driver;
    bool _initialized;
    bool _wasMoving;  // Track movement state to detect completion
    
    // Motion configuration (simple, FastAccelStepper handles profiles)
    float _maxSpeed;        // steps/s
    float _acceleration;    // steps/s²
    
    // Error monitoring
    uint8_t _errorFlags;               // Cached error flags from last poll
    uint32_t _lastErrorPollTime;       // Last time we polled for errors
    static constexpr uint32_t ERROR_POLL_INTERVAL_MS = 500;
    
    // Command parsing helpers
    void parseSetCommand(const String& params);
};
