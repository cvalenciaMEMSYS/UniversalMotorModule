/*
 * =============================================================================
 * MOTION MATH - Pure Motion Planning Functions
 * =============================================================================
 * 
 * Stateless mathematical functions for motion control calculations.
 * These are extracted from driver implementations for:
 *   - Easy unit testing
 *   - Code reuse across drivers
 *   - Clear separation of concerns
 * 
 * =============================================================================
 */

#pragma once

#include <cstdint>
#include <cmath>

namespace MotionMath {

// =============================================================================
// STEP INTERVAL CALCULATIONS
// =============================================================================

/**
 * @brief Calculate step interval (microseconds) from speed (steps/sec)
 * @param speed Steps per second
 * @return Interval in microseconds, clamped to minimum 10µs
 */
inline uint32_t calculateStepInterval(float speed) {
    if (speed <= 0) return 1000000;  // 1 second default (1 Hz)
    uint32_t interval = static_cast<uint32_t>(1000000.0f / speed);
    if (interval < 10) interval = 10;  // Minimum 10µs (100kHz max step rate)
    return interval;
}

// =============================================================================
// TRAPEZOIDAL MOTION PLANNING
// =============================================================================

/**
 * @brief Calculate steps needed to accelerate from rest to target speed
 * 
 * Uses kinematic equation: v² = 2*a*d → d = v²/(2*a)
 * 
 * @param maxSpeed Target speed (steps/sec)
 * @param acceleration Acceleration rate (steps/sec²)
 * @return Number of steps to reach max speed
 */
inline int32_t calculateStepsToMaxSpeed(float maxSpeed, float acceleration) {
    if (acceleration <= 0) return 0;
    return static_cast<int32_t>((maxSpeed * maxSpeed) / (2.0f * acceleration));
}

/**
 * @brief Calculate velocity after traveling a distance with constant acceleration
 * 
 * Uses kinematic equation: v = sqrt(2*a*d)
 * 
 * @param acceleration Acceleration rate (steps/sec²)
 * @param steps Distance traveled (steps)
 * @return Final velocity (steps/sec)
 */
inline float calculateVelocityAfterDistance(float acceleration, int32_t steps) {
    if (acceleration <= 0 || steps <= 0) return 0;
    return std::sqrt(2.0f * acceleration * static_cast<float>(steps));
}

/**
 * @brief Calculate current velocity during deceleration phase
 * 
 * Uses: v = sqrt(2*a*remaining) to find velocity that will reach zero at target
 * 
 * @param acceleration Deceleration rate (steps/sec²)
 * @param stepsRemaining Distance to target (steps)
 * @return Required velocity (steps/sec)
 */
inline float calculateDecelerationVelocity(float acceleration, int32_t stepsRemaining) {
    return calculateVelocityAfterDistance(acceleration, stepsRemaining);
}

/**
 * @brief Determine if a trapezoidal move is actually triangular
 * 
 * Triangular = move is too short to reach max speed, so it's accel→decel with no cruise
 * 
 * @param totalSteps Total distance of move
 * @param maxSpeed Target max speed
 * @param acceleration Acceleration rate
 * @return true if triangular (no cruise phase), false if full trapezoidal
 */
inline bool isTriangularProfile(int32_t totalSteps, float maxSpeed, float acceleration) {
    int32_t stepsToMax = calculateStepsToMaxSpeed(maxSpeed, acceleration);
    return (2 * stepsToMax >= totalSteps);
}

/**
 * @brief Calculate trapezoidal velocity at a given position
 * 
 * @param stepsDone Steps completed from start
 * @param stepsRemaining Steps remaining to target
 * @param accelSteps Steps in acceleration phase
 * @param decelSteps Steps in deceleration phase
 * @param acceleration Acceleration rate (steps/sec²)
 * @param maxSpeed Maximum cruise speed (steps/sec)
 * @param minSpeed Minimum speed limit (steps/sec)
 * @return Current velocity (steps/sec)
 */
inline float calculateTrapezoidalVelocity(
    int32_t stepsDone,
    int32_t stepsRemaining,
    int32_t accelSteps,
    int32_t decelSteps,
    float acceleration,
    float maxSpeed,
    float minSpeed = 50.0f
) {
    float speed;
    
    if (stepsDone < accelSteps) {
        // Accelerating phase: v = sqrt(2*a*d)
        speed = std::sqrt(2.0f * acceleration * static_cast<float>(stepsDone + 1));
        speed = (speed < maxSpeed) ? speed : maxSpeed;
    } 
    else if (stepsRemaining <= decelSteps) {
        // Decelerating phase: v = sqrt(2*a*remaining)
        speed = std::sqrt(2.0f * acceleration * static_cast<float>(stepsRemaining));
    }
    else {
        // Cruising at max speed
        speed = maxSpeed;
    }
    
    // Enforce minimum speed to prevent stalling
    return (speed > minSpeed) ? speed : minSpeed;
}

// =============================================================================
// S-CURVE MOTION PLANNING
// =============================================================================

/**
 * @brief Calculate jerk phase duration
 * 
 * Time to ramp acceleration from 0 to maxAccel: t_j = a_max / j
 * 
 * @param maxAccel Maximum acceleration (steps/sec²)
 * @param jerk Jerk limit (steps/sec³)
 * @return Jerk phase time (seconds)
 */
inline float calculateJerkPhaseTime(float maxAccel, float jerk) {
    if (jerk <= 0) return 0;
    return maxAccel / jerk;
}

/**
 * @brief Calculate distance covered during jerk phase
 * 
 * Uses: s = j * t³ / 6 (from integration of jerk → accel → velocity → position)
 * 
 * @param jerk Jerk value (steps/sec³)
 * @param time Duration of jerk phase (seconds)
 * @return Distance traveled (steps)
 */
inline float calculateJerkPhaseDistance(float jerk, float time) {
    if (jerk <= 0 || time <= 0) return 0;
    return jerk * time * time * time / 6.0f;
}

/**
 * @brief Calculate velocity gained during jerk phase
 * 
 * Uses: v = j * t² / 2 (from integration of jerk → accel → velocity)
 * 
 * @param jerk Jerk value (steps/sec³)
 * @param time Duration of jerk phase (seconds)
 * @return Velocity gained (steps/sec)
 */
inline float calculateJerkPhaseVelocity(float jerk, float time) {
    if (jerk <= 0 || time <= 0) return 0;
    return jerk * time * time / 2.0f;
}

/**
 * @brief Smooth S-curve interpolation function
 * 
 * Maps linear progress [0,1] to smooth S-curve progress [0,1].
 * Uses Hermite smoothstep: 3p² - 2p³
 * 
 * Properties:
 *   - f(0) = 0, f(1) = 1
 *   - f'(0) = 0, f'(1) = 0 (zero derivative at endpoints)
 *   - Symmetric around p = 0.5
 * 
 * @param progress Linear progress (0.0 to 1.0)
 * @return Smoothed progress (0.0 to 1.0)
 */
inline float smoothSCurveProgress(float progress) {
    if (progress <= 0) return 0;
    if (progress >= 1) return 1;
    return progress * progress * (3.0f - 2.0f * progress);
}

/**
 * @brief Interpolate velocity during S-curve jerk phase
 * 
 * Uses smooth interpolation between start and end velocities.
 * 
 * @param v0 Starting velocity
 * @param v1 Ending velocity
 * @param progress Position progress through segment (0.0 to 1.0)
 * @return Interpolated velocity
 */
inline float interpolateJerkPhaseVelocity(float v0, float v1, float progress) {
    float smooth = smoothSCurveProgress(progress);
    return v0 + (v1 - v0) * smooth;
}

/**
 * @brief Calculate velocity during constant acceleration phase
 * 
 * Uses: v² = v0² + 2*a*d → v = sqrt(v0² + 2*a*d)
 * 
 * @param v0 Initial velocity
 * @param acceleration Acceleration rate (positive or negative)
 * @param distance Distance traveled
 * @return Current velocity
 */
inline float calculateConstantAccelVelocity(float v0, float acceleration, float distance) {
    float v_squared = v0 * v0 + 2.0f * acceleration * distance;
    if (v_squared <= 0) return v0;  // Handle numerical issues
    return std::sqrt(v_squared);
}

// =============================================================================
// MICROSTEP CONVERSION
// =============================================================================

/**
 * @brief Convert microstep setting to TMC MRES register value
 * 
 * MRES encoding:
 *   256 microsteps = 0
 *   128 = 1, 64 = 2, 32 = 3, 16 = 4, 8 = 5, 4 = 6, 2 = 7, 1 = 8
 * 
 * @param microsteps Microstep setting (1, 2, 4, 8, 16, 32, 64, 128, 256)
 * @return MRES register value (0-8), defaults to 4 (16 microsteps) for invalid input
 */
inline uint8_t microStepsToMRES(uint16_t microsteps) {
    switch (microsteps) {
        case 256: return 0;
        case 128: return 1;
        case 64:  return 2;
        case 32:  return 3;
        case 16:  return 4;
        case 8:   return 5;
        case 4:   return 6;
        case 2:   return 7;
        case 1:   return 8;
        default:  return 4;  // Default to 16 microsteps
    }
}

/**
 * @brief Convert TMC MRES register value to microstep setting
 * 
 * @param mres MRES register value (0-8)
 * @return Microstep setting (1-256)
 */
inline uint16_t mresToMicrosteps(uint8_t mres) {
    switch (mres) {
        case 0: return 256;
        case 1: return 128;
        case 2: return 64;
        case 3: return 32;
        case 4: return 16;
        case 5: return 8;
        case 6: return 4;
        case 7: return 2;
        case 8: return 1;
        default: return 16;  // Default to 16 microsteps
    }
}

// =============================================================================
// VALIDATION HELPERS
// =============================================================================

/**
 * @brief Validate microstep value is a valid power of 2
 * 
 * @param microsteps Value to validate
 * @return true if valid (1, 2, 4, 8, 16, 32, 64, 128, 256)
 */
inline bool isValidMicrosteps(uint16_t microsteps) {
    switch (microsteps) {
        case 1: case 2: case 4: case 8: case 16:
        case 32: case 64: case 128: case 256:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Clamp speed to valid range
 * 
 * @param speed Requested speed
 * @param minSpeed Minimum allowed speed
 * @param maxSpeed Maximum allowed speed
 * @return Clamped speed value
 */
inline float clampSpeed(float speed, float minSpeed, float maxSpeed) {
    if (speed < minSpeed) return minSpeed;
    if (speed > maxSpeed) return maxSpeed;
    return speed;
}

} // namespace MotionMath
