/*
 * =============================================================================
 * ACCELERATION PROFILE - Velocity Profile Definitions
 * =============================================================================
 * 
 * Defines acceleration/velocity profiles for motor motion control.
 * The high-level controller defines the profile parameters, and the
 * low-level driver executes the appropriate motion based on motor type.
 * 
 * =============================================================================
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Velocity profile types for motor acceleration
 * 
 * These define how velocity changes over time during a move:
 * - CONSTANT: No acceleration, instant speed changes (simple but can cause missed steps)
 * - TRAPEZOIDAL: Linear acceleration to max speed, cruise, linear deceleration
 * - S_CURVE: Smooth jerk-limited acceleration (most gentle on mechanics)
 */
enum class VelocityProfileType {
    CONSTANT,      ///< No acceleration curve - instant velocity (default fallback)
    TRAPEZOIDAL,   ///< Linear accel/decel with cruising phase
    S_CURVE        ///< Jerk-limited smooth acceleration
};

/**
 * @brief Acceleration profile parameters
 * 
 * Contains all parameters needed to define a motion profile.
 * Acceleration and deceleration use the same value for simplicity.
 */
struct AccelerationProfile {
    VelocityProfileType type = VelocityProfileType::CONSTANT;
    
    float maxSpeed = 1000.0f;      ///< Maximum velocity (steps/sec for steppers, 0-1 for DC)
    float acceleration = 500.0f;   ///< Acceleration rate (steps/sec², same for decel)
    float jerk = 1000.0f;          ///< Jerk limit for S-curve (steps/sec³)
    
    // =========================================================================
    // Factory methods for common profiles
    // =========================================================================
    
    /**
     * @brief Create a constant velocity profile (no acceleration)
     * @param speed Maximum speed
     * @return AccelerationProfile with CONSTANT type
     */
    static AccelerationProfile constant(float speed) {
        AccelerationProfile p;
        p.type = VelocityProfileType::CONSTANT;
        p.maxSpeed = speed;
        p.acceleration = 0;
        p.jerk = 0;
        return p;
    }
    
    /**
     * @brief Create a trapezoidal velocity profile
     * @param speed Maximum speed
     * @param accel Acceleration/deceleration rate
     * @return AccelerationProfile with TRAPEZOIDAL type
     */
    static AccelerationProfile trapezoidal(float speed, float accel) {
        AccelerationProfile p;
        p.type = VelocityProfileType::TRAPEZOIDAL;
        p.maxSpeed = speed;
        p.acceleration = accel;
        p.jerk = 0;
        return p;
    }
    
    /**
     * @brief Create an S-curve velocity profile
     * @param speed Maximum speed
     * @param accel Acceleration/deceleration rate
     * @param jerkLimit Jerk limit
     * @return AccelerationProfile with S_CURVE type
     */
    static AccelerationProfile sCurve(float speed, float accel, float jerkLimit) {
        AccelerationProfile p;
        p.type = VelocityProfileType::S_CURVE;
        p.maxSpeed = speed;
        p.acceleration = accel;
        p.jerk = jerkLimit;
        return p;
    }
    
    // =========================================================================
    // Utility methods
    // =========================================================================
    
    /**
     * @brief Check if this profile uses acceleration
     * @return true if type is TRAPEZOIDAL or S_CURVE
     */
    bool hasAcceleration() const {
        return type != VelocityProfileType::CONSTANT && acceleration > 0;
    }
    
    /**
     * @brief Get profile type as string
     * @return Human-readable profile type name
     */
    const char* getTypeName() const {
        switch (type) {
            case VelocityProfileType::CONSTANT:    return "Constant";
            case VelocityProfileType::TRAPEZOIDAL: return "Trapezoidal";
            case VelocityProfileType::S_CURVE:     return "S-Curve";
            default:                                return "Unknown";
        }
    }
    
    /**
     * @brief Print profile parameters to Serial
     */
    void print() const {
        Serial.print("Profile: ");
        Serial.println(getTypeName());
        Serial.print("  Max Speed: ");
        Serial.println(maxSpeed);
        if (hasAcceleration()) {
            Serial.print("  Acceleration: ");
            Serial.println(acceleration);
            if (type == VelocityProfileType::S_CURVE) {
                Serial.print("  Jerk: ");
                Serial.println(jerk);
            }
        }
    }
};
