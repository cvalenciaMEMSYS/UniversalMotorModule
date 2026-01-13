/*
 * =============================================================================
 * MOTOR DRIVER INTERFACE - Abstract Base Class
 * =============================================================================
 * 
 * Defines the common interface that all motor drivers must implement.
 * This allows the high-level controller to work with any motor type
 * (TMC2209, TMC2208, DC motor, etc.) through a unified API.
 * 
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
// Note: AccelerationProfile.h removed - now handled by FastAccelStepper library

// =============================================================================
// MOTOR TYPE ENUMERATION
// =============================================================================

/**
 * @brief Supported motor driver types
 */
enum class MotorType {
    STEPPER_TMC2209,   ///< TMC2209 with UART control
    STEPPER_TMC2208,   ///< TMC2208 standalone (Step/Dir only)
    DC_MOTOR,          ///< DC motor with H-bridge (e.g., RZ7899)
    UNKNOWN            ///< Unknown or not detected
};

/**
 * @brief Convert MotorType to string
 */
inline const char* motorTypeToString(MotorType type) {
    switch (type) {
        case MotorType::STEPPER_TMC2209: return "TMC2209";
        case MotorType::STEPPER_TMC2208: return "TMC2208";
        case MotorType::DC_MOTOR:        return "DC Motor";
        default:                         return "Unknown";
    }
}

// =============================================================================
// ERROR FLAGS
// =============================================================================

namespace MotorError {
    constexpr uint8_t NONE           = 0x00;
    constexpr uint8_t OVER_TEMP      = 0x01;  ///< Driver overheating
    constexpr uint8_t SHORT_CIRCUIT  = 0x02;  ///< Short to GND or VCC
    constexpr uint8_t OPEN_LOAD      = 0x04;  ///< Motor not connected
    constexpr uint8_t COMM_FAILURE   = 0x08;  ///< UART communication failed
    constexpr uint8_t STALL_DETECTED = 0x10;  ///< Motor stalled (StallGuard)
}

// =============================================================================
// MOTOR STATUS STRUCTURE
// =============================================================================

/**
 * @brief Complete motor status snapshot
 */
struct MotorStatus {
    bool enabled = false;          ///< Driver enabled (EN pin LOW)
    bool moving = false;           ///< Currently executing a move
    bool stalling = false;         ///< Stall condition detected
    
    int32_t position = 0;          ///< Current position (steps from origin)
    int32_t targetPosition = 0;    ///< Target for current move
    
    uint16_t currentMA = 0;        ///< Configured run current (mA)
    uint16_t loadValue = 0;        ///< StallGuard result (0 if N/A)
    
    uint8_t errorFlags = 0;        ///< Bit flags from MotorError namespace
    
    float currentSpeed = 0.0f;     ///< Current instantaneous speed
    
    /**
     * @brief Check if any error is present
     */
    bool hasError() const {
        return errorFlags != MotorError::NONE;
    }
    
    /**
     * @brief Print status to Serial
     */
    void print() const {
        Serial.println("Motor Status:");
        Serial.print("  Enabled: ");    Serial.println(enabled ? "Yes" : "No");
        Serial.print("  Moving: ");     Serial.println(moving ? "Yes" : "No");
        Serial.print("  Position: ");   Serial.println(position);
        Serial.print("  Target: ");     Serial.println(targetPosition);
        Serial.print("  Current: ");    Serial.print(currentMA); Serial.println(" mA");
        Serial.print("  Speed: ");      Serial.println(currentSpeed);
        
        if (loadValue > 0) {
            Serial.print("  Load (SG): "); Serial.println(loadValue);
        }
        
        if (hasError()) {
            Serial.print("  Errors: ");
            if (errorFlags & MotorError::OVER_TEMP)     Serial.print("OverTemp ");
            if (errorFlags & MotorError::SHORT_CIRCUIT) Serial.print("Short ");
            if (errorFlags & MotorError::OPEN_LOAD)     Serial.print("OpenLoad ");
            if (errorFlags & MotorError::COMM_FAILURE)  Serial.print("CommFail ");
            if (errorFlags & MotorError::STALL_DETECTED) Serial.print("Stall ");
            Serial.println();
        }
    }
};

// =============================================================================
// ABSTRACT MOTOR DRIVER INTERFACE
// =============================================================================

/**
 * @brief Abstract base class for all motor drivers
 * 
 * Implement this interface to add support for new motor driver types.
 * The high-level MotorController uses this interface to control motors
 * without knowing the specific hardware details.
 */
class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;
    
    // =========================================================================
    // Identification
    // =========================================================================
    
    /**
     * @brief Initialize the driver hardware
     * @return true if initialization successful
     */
    virtual bool init() = 0;
    
    /**
     * @brief Get the motor driver type
     */
    virtual MotorType getType() const = 0;
    
    /**
     * @brief Get human-readable driver name
     */
    virtual const char* getName() const = 0;
    
    // =========================================================================
    // Enable / Disable
    // =========================================================================
    
    /**
     * @brief Enable the motor driver
     * 
     * For steppers: EN pin LOW, motor holds position
     * For DC: H-bridge enabled
     */
    virtual void enable() = 0;
    
    /**
     * @brief Disable the motor driver
     * 
     * For steppers: EN pin HIGH, motor free (no holding current)
     * For DC: H-bridge in coast/brake mode
     */
    virtual void disable() = 0;
    
    /**
     * @brief Check if driver is currently enabled
     */
    virtual bool isEnabled() const = 0;
    
    // =========================================================================
    // Motion Control
    // =========================================================================
    
    /**
     * @brief Execute a relative move
     * @param steps Number of steps to move (positive or negative)
     * 
     * Uses the configured acceleration profile.
     * For DC motors, "steps" may represent encoder ticks or duration.
     */
    virtual void move(int32_t steps) = 0;
    
    /**
     * @brief Execute an absolute move
     * @param position Target position (must be >= 0)
     * 
     * Uses the configured acceleration profile.
     * Position 0 is defined as home.
     */
    virtual void moveTo(int32_t position) = 0;
    
    /**
     * @brief Stop with controlled deceleration
     * 
     * Uses the acceleration profile's deceleration rate.
     */
    virtual void stop() = 0;
    
    /**
     * @brief Emergency stop - immediate halt
     * 
     * No deceleration, motor stops instantly.
     * May cause missed steps or mechanical stress.
     */
    virtual void emergencyStop() = 0;
    
    /**
     * @brief Check if a move is in progress
     */
    virtual bool isMoving() const = 0;
    
    /**
     * @brief Update function - must be called in loop()
     * 
     * Processes acceleration, step generation, etc.
     * For blocking moves, this may not be needed.
     */
    virtual void update() = 0;
    
    // =========================================================================
    // Configuration
    // =========================================================================
    
    /**
     * @brief Set maximum speed
     * @param stepsPerSecond Max velocity in steps per second
     */
    virtual void setMaxSpeed(float stepsPerSecond) = 0;
    
    /**
     * @brief Set motor current
     * @param runMA Run current in milliamps
     * @param holdMA Hold current in milliamps (0 = no holding)
     */
    virtual void setCurrent(uint16_t runMA, uint16_t holdMA = 0) = 0;
    
    /**
     * @brief Set microstepping resolution
     * @param microsteps Microstep setting (1, 2, 4, 8, 16, 32, 64, 128, 256)
     * 
     * No-op for DC motors.
     */
    virtual void setMicrosteps(uint16_t microsteps) = 0;
    
    /**
     * @brief Set acceleration rate
     * @param accelStepsPerSecondSquared Acceleration in steps/s²
     * 
     * Note: Acceleration profile is now managed internally by FastAccelStepper.
     * This method sets only the acceleration rate.
     */
    virtual void setAcceleration(float accelStepsPerSecondSquared) = 0;
    
    // =========================================================================
    // Position
    // =========================================================================
    
    /**
     * @brief Get current position
     * @return Position in steps from origin
     */
    virtual int32_t getPosition() const = 0;
    
    /**
     * @brief Set/reset the position counter
     * @param position New position value
     */
    virtual void setPosition(int32_t position) = 0;
    
    /**
     * @brief Home to find origin position
     * @param direction Direction to move for homing (-1 or 1)
     * 
     * Uses StallGuard or limit switch to detect home.
     * After homing, position is set to 0.
     */
    virtual void home(int8_t direction = -1) = 0;
    
    // =========================================================================
    // Diagnostics
    // =========================================================================
    
    /**
     * @brief Get complete status snapshot
     * @return MotorStatus with all current values
     */
    virtual MotorStatus getStatus() = 0;
    
    /**
     * @brief Check if motor is stalling
     * @return true if stall detected
     * 
     * Uses StallGuard for TMC drivers, always false for DC motors.
     */
    virtual bool isStalling() = 0;
    
    /**
     * @brief Print detailed diagnostics to Serial
     */
    virtual void printDiagnostics() = 0;
    
    /**
     * @brief Test communication with driver
     * @return true if communication successful
     * 
     * For TMC2209: Tests UART connection
     * For TMC2208/DC: Always returns true (no comm to test)
     */
    virtual bool testConnection() = 0;
};
