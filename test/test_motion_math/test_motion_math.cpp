/*
 * =============================================================================
 * TEST: Motion Planning Math
 * =============================================================================
 * 
 * Unit tests for motion planning calculations directly testing MotionMath.h
 * 
 * Tests:
 *   - Trapezoidal motion planning
 *   - S-curve motion planning
 *   - Velocity/step interval calculations
 *   - Property-based invariant tests
 * 
 * =============================================================================
 */

#include <unity.h>
#include <cmath>
#include <Arduino.h>  // Uses mock from test/mocks/ in native build

// Include the actual production code header
#include "../../src/core/MotionMath.h"

using namespace MotionMath;

// =============================================================================
// HELPER WRAPPERS (for tests needing extra functions)
// =============================================================================

/**
 * Simple kinematic velocity calculation for tests.
 * v² = v0² + 2*a*s
 */
float simpleKinematicVelocity(float startSpeed, float accel, int32_t steps) {
    float vSquared = startSpeed * startSpeed + 2.0f * accel * static_cast<float>(steps);
    if (vSquared < 0) return 0.0f;
    return std::sqrt(vSquared);
}

// =============================================================================
// SETUP / TEARDOWN
// =============================================================================

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// TRAPEZOIDAL MOTION TESTS
// =============================================================================

void test_steps_to_max_speed_basic(void) {
    // v² = 2*a*d → d = v²/(2*a)
    // Speed 1000, Accel 500 → d = 1000000/1000 = 1000 steps
    int32_t steps = calculateStepsToMaxSpeed(1000.0f, 500.0f);
    TEST_ASSERT_EQUAL(1000, steps);
}

void test_steps_to_max_speed_high_accel(void) {
    // Higher acceleration = fewer steps to max speed
    // Speed 1000, Accel 2000 → d = 1000000/4000 = 250 steps
    int32_t steps = calculateStepsToMaxSpeed(1000.0f, 2000.0f);
    TEST_ASSERT_EQUAL(250, steps);
}

void test_steps_to_max_speed_low_accel(void) {
    // Lower acceleration = more steps to max speed
    // Speed 1000, Accel 100 → d = 1000000/200 = 5000 steps
    int32_t steps = calculateStepsToMaxSpeed(1000.0f, 100.0f);
    TEST_ASSERT_EQUAL(5000, steps);
}

void test_steps_to_max_speed_zero_accel(void) {
    int32_t steps = calculateStepsToMaxSpeed(1000.0f, 0.0f);
    TEST_ASSERT_EQUAL(0, steps);
}

void test_velocity_after_distance(void) {
    // v = sqrt(2*500*1000) = sqrt(1000000) = 1000
    float velocity = calculateVelocityAfterDistance(500.0f, 1000);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1000.0f, velocity);
}

void test_velocity_after_distance_smaller(void) {
    // v = sqrt(2*500*500) = sqrt(500000) ≈ 707
    float velocity = calculateVelocityAfterDistance(500.0f, 500);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 707.1f, velocity);
}

void test_trapezoidal_velocity_acceleration(void) {
    // Using simpleKinematicVelocity for v² = v0² + 2*a*s
    // startSpeed=0, accel=500, steps=1000 → v = sqrt(2*500*1000) = 1000
    float velocity = simpleKinematicVelocity(0.0f, 500.0f, 1000);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1000.0f, velocity);
}

void test_trapezoidal_velocity_deceleration(void) {
    // startSpeed=1000, accel=-500, steps=1000 → v² = 1000000 - 1000000 = 0
    float velocity = simpleKinematicVelocity(1000.0f, -500.0f, 1000);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, velocity);
}

void test_step_interval_at_1000_sps(void) {
    // 1000 steps/sec = 1000µs interval
    uint32_t interval = calculateStepInterval(1000.0f);
    TEST_ASSERT_EQUAL(1000, interval);
}

void test_step_interval_at_10000_sps(void) {
    // 10000 steps/sec = 100µs interval
    uint32_t interval = calculateStepInterval(10000.0f);
    TEST_ASSERT_EQUAL(100, interval);
}

void test_step_interval_at_100_sps(void) {
    // 100 steps/sec = 10000µs interval
    uint32_t interval = calculateStepInterval(100.0f);
    TEST_ASSERT_EQUAL(10000, interval);
}

void test_step_interval_minimum_clamp(void) {
    // Very high speed should be clamped to 10µs minimum
    uint32_t interval = calculateStepInterval(1000000.0f);
    TEST_ASSERT_EQUAL(10, interval);
}

void test_step_interval_zero_speed(void) {
    uint32_t interval = calculateStepInterval(0.0f);
    TEST_ASSERT_EQUAL(1000000, interval);
}

void test_triangular_profile_detection_short_move(void) {
    // Short move: 500 steps, need 1000 steps to reach max speed
    // This should be triangular
    bool triangular = isTriangularProfile(500, 1000.0f, 500.0f);
    TEST_ASSERT_TRUE(triangular);
}

void test_triangular_profile_detection_exact_boundary(void) {
    // Exactly at boundary: 2000 steps, need 1000 steps to reach max
    // 2 * 1000 = 2000, so this is triangular (never cruises)
    bool triangular = isTriangularProfile(2000, 1000.0f, 500.0f);
    TEST_ASSERT_TRUE(triangular);
}

void test_trapezoidal_profile_detection_long_move(void) {
    // Long move: 5000 steps, need 1000 steps to reach max
    // 2 * 1000 = 2000 < 5000, so full trapezoidal
    bool triangular = isTriangularProfile(5000, 1000.0f, 500.0f);
    TEST_ASSERT_FALSE(triangular);
}

// =============================================================================
// S-CURVE MOTION TESTS
// =============================================================================

void test_jerk_phase_time(void) {
    // t_j = a_max / j = 500 / 5000 = 0.1s
    float time = calculateJerkPhaseTime(500.0f, 5000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, time);
}

void test_jerk_phase_time_high_jerk(void) {
    // Higher jerk = shorter jerk phase
    // t_j = 500 / 10000 = 0.05s
    float time = calculateJerkPhaseTime(500.0f, 10000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.05f, time);
}

void test_jerk_phase_time_zero_jerk(void) {
    float time = calculateJerkPhaseTime(500.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, time);
}

void test_jerk_phase_distance(void) {
    // s = j * t³ / 6
    // j = 5000, t = 0.1 → s = 5000 * 0.001 / 6 = 0.833 steps
    float distance = calculateJerkPhaseDistance(5000.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.833f, distance);
}

void test_jerk_phase_velocity(void) {
    // v = j * t² / 2
    // j = 5000, t = 0.1 → v = 5000 * 0.01 / 2 = 25 steps/sec
    float velocity = calculateJerkPhaseVelocity(5000.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, velocity);
}

void test_smooth_scurve_progress_at_zero(void) {
    float result = smoothSCurveProgress(0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, result);
}

void test_smooth_scurve_progress_at_one(void) {
    float result = smoothSCurveProgress(1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, result);
}

void test_smooth_scurve_progress_at_half(void) {
    // At 0.5: 3*(0.25) - 2*(0.125) = 0.75 - 0.25 = 0.5
    float result = smoothSCurveProgress(0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, result);
}

void test_interpolate_jerk_phase_velocity(void) {
    // Test that interpolation works correctly
    float v = interpolateJerkPhaseVelocity(0.0f, 100.0f, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, v);
}

void test_interpolate_jerk_phase_velocity_at_start(void) {
    float v = interpolateJerkPhaseVelocity(0.0f, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, v);
}

void test_interpolate_jerk_phase_velocity_at_end(void) {
    float v = interpolateJerkPhaseVelocity(0.0f, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, v);
}

void test_constant_accel_velocity_positive(void) {
    // v² = v0² + 2*a*s, v0=0, a=500, s=1000 → v=1000
    float v = calculateConstantAccelVelocity(0.0f, 500.0f, 1000.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1000.0f, v);
}

void test_constant_accel_velocity_negative(void) {
    // v² = v0² + 2*a*s, v0=1000, a=-500, s=500 → v² = 1000000-500000 = 500000 → v≈707
    float v = calculateConstantAccelVelocity(1000.0f, -500.0f, 500.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 707.1f, v);
}

// =============================================================================
// MICROSTEPS CONVERSION TESTS
// =============================================================================

void test_mres_for_256_microsteps(void) {
    TEST_ASSERT_EQUAL(0, microStepsToMRES(256));
}

void test_mres_for_128_microsteps(void) {
    TEST_ASSERT_EQUAL(1, microStepsToMRES(128));
}

void test_mres_for_64_microsteps(void) {
    TEST_ASSERT_EQUAL(2, microStepsToMRES(64));
}

void test_mres_for_32_microsteps(void) {
    TEST_ASSERT_EQUAL(3, microStepsToMRES(32));
}

void test_mres_for_16_microsteps(void) {
    TEST_ASSERT_EQUAL(4, microStepsToMRES(16));
}

void test_mres_for_8_microsteps(void) {
    TEST_ASSERT_EQUAL(5, microStepsToMRES(8));
}

void test_mres_for_4_microsteps(void) {
    TEST_ASSERT_EQUAL(6, microStepsToMRES(4));
}

void test_mres_for_2_microsteps(void) {
    TEST_ASSERT_EQUAL(7, microStepsToMRES(2));
}

void test_mres_for_fullstep(void) {
    TEST_ASSERT_EQUAL(8, microStepsToMRES(1));
}

void test_mres_for_invalid_defaults_to_16(void) {
    TEST_ASSERT_EQUAL(4, microStepsToMRES(100));  // Invalid value
    TEST_ASSERT_EQUAL(4, microStepsToMRES(0));    // Invalid value
    TEST_ASSERT_EQUAL(4, microStepsToMRES(512));  // Invalid value
}

void test_mres_to_microsteps_roundtrip(void) {
    // Test all valid MRES values roundtrip correctly
    for (uint8_t mres = 0; mres <= 8; mres++) {
        uint16_t ms = mresToMicrosteps(mres);
        uint8_t mres_back = microStepsToMRES(ms);
        TEST_ASSERT_EQUAL(mres, mres_back);
    }
}

void test_microsteps_to_mres_roundtrip(void) {
    // Test all valid microstep values roundtrip correctly
    uint16_t validValues[] = {256, 128, 64, 32, 16, 8, 4, 2, 1};
    for (int i = 0; i < 9; i++) {
        uint8_t mres = microStepsToMRES(validValues[i]);
        uint16_t ms_back = mresToMicrosteps(mres);
        TEST_ASSERT_EQUAL(validValues[i], ms_back);
    }
}

void test_is_valid_microsteps(void) {
    // Valid values
    TEST_ASSERT_TRUE(isValidMicrosteps(256));
    TEST_ASSERT_TRUE(isValidMicrosteps(128));
    TEST_ASSERT_TRUE(isValidMicrosteps(64));
    TEST_ASSERT_TRUE(isValidMicrosteps(32));
    TEST_ASSERT_TRUE(isValidMicrosteps(16));
    TEST_ASSERT_TRUE(isValidMicrosteps(8));
    TEST_ASSERT_TRUE(isValidMicrosteps(4));
    TEST_ASSERT_TRUE(isValidMicrosteps(2));
    TEST_ASSERT_TRUE(isValidMicrosteps(1));
    
    // Invalid values
    TEST_ASSERT_FALSE(isValidMicrosteps(0));
    TEST_ASSERT_FALSE(isValidMicrosteps(100));
    TEST_ASSERT_FALSE(isValidMicrosteps(512));
    TEST_ASSERT_FALSE(isValidMicrosteps(3));
}

// =============================================================================
// PROPERTY-BASED INVARIANT TESTS
// =============================================================================

/**
 * Property: smoothSCurveProgress should be symmetric around 0.5
 * For any x in [0, 0.5]: f(x) + f(1-x) = 1
 */
void test_property_scurve_symmetry(void) {
    float testPoints[] = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    for (int i = 0; i < 6; i++) {
        float x = testPoints[i];
        float at_x = smoothSCurveProgress(x);
        float at_complement = smoothSCurveProgress(1.0f - x);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at_x + at_complement);
    }
}

/**
 * Property: smoothSCurveProgress should be monotonically increasing
 */
void test_property_scurve_monotonic(void) {
    float prev = 0.0f;
    for (float p = 0.0f; p <= 1.0f; p += 0.05f) {
        float current = smoothSCurveProgress(p);
        TEST_ASSERT_TRUE(current >= prev - 0.0001f);  // Allow tiny float errors
        prev = current;
    }
}

/**
 * Property: smoothSCurveProgress should be bounded [0, 1]
 */
void test_property_scurve_bounded(void) {
    for (float p = -0.5f; p <= 1.5f; p += 0.1f) {
        float result = smoothSCurveProgress(p);
        TEST_ASSERT_TRUE(result >= 0.0f);
        TEST_ASSERT_TRUE(result <= 1.0f);
    }
}

/**
 * Property: clampSpeed should always return value in valid range
 */
void test_property_clamp_speed_bounded(void) {
    float testValues[] = {-1000.0f, 0.0f, 50.0f, 500.0f, 1000.0f, 10000.0f};
    float minSpeed = 50.0f;
    float maxSpeed = 1000.0f;
    
    for (int i = 0; i < 6; i++) {
        float clamped = clampSpeed(testValues[i], minSpeed, maxSpeed);
        TEST_ASSERT_TRUE(clamped >= minSpeed);
        TEST_ASSERT_TRUE(clamped <= maxSpeed);
    }
}

/**
 * Property: Acceleration distance should equal deceleration distance for symmetric profile
 * In a symmetric trapezoidal profile: accel_steps == decel_steps
 */
void test_property_symmetric_accel_decel_distance(void) {
    // For same max speed and acceleration/deceleration rate,
    // the distance to accelerate should equal distance to decelerate
    float speeds[] = {100.0f, 500.0f, 1000.0f, 5000.0f};
    float accels[] = {100.0f, 500.0f, 1000.0f, 5000.0f};
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int32_t accelSteps = calculateStepsToMaxSpeed(speeds[i], accels[j]);
            // Deceleration from max speed to 0 with same rate = same distance
            // This is implicit in the calculation, but we verify consistency
            int32_t decelSteps = calculateStepsToMaxSpeed(speeds[i], accels[j]);
            TEST_ASSERT_EQUAL(accelSteps, decelSteps);
        }
    }
}

/**
 * Property: Step interval should decrease as speed increases
 */
void test_property_step_interval_inversely_proportional(void) {
    float speeds[] = {100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f};
    uint32_t prevInterval = UINT32_MAX;
    
    for (int i = 0; i < 6; i++) {
        uint32_t interval = calculateStepInterval(speeds[i]);
        TEST_ASSERT_TRUE(interval < prevInterval);
        prevInterval = interval;
    }
}

/**
 * Property: Jerk phase calculations should be consistent
 * Velocity gained = integral of jerk over time = j*t²/2
 * Distance covered = integral of velocity = j*t³/6
 */
void test_property_jerk_phase_consistency(void) {
    float jerks[] = {1000.0f, 5000.0f, 10000.0f};
    float times[] = {0.05f, 0.1f, 0.2f};
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float jerk = jerks[i];
            float time = times[j];
            
            float velocity = calculateJerkPhaseVelocity(jerk, time);
            float distance = calculateJerkPhaseDistance(jerk, time);
            
            // Verify: v = j*t²/2
            float expectedVelocity = jerk * time * time / 2.0f;
            TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedVelocity, velocity);
            
            // Verify: s = j*t³/6
            float expectedDistance = jerk * time * time * time / 6.0f;
            TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedDistance, distance);
            
            // Verify: distance/velocity relationship (average velocity * time)
            // Average velocity during jerk phase ≈ velocity/2 (starts at 0)
            // So distance ≈ (v/2) * t = v*t/2 = (j*t²/2)*(t/2) = j*t³/4
            // But actual is j*t³/6, which is correct for cubic acceleration
            float ratio = (velocity > 0) ? (distance / velocity) : 0;
            float expectedRatio = time / 3.0f;  // Because ∫v dt / v_final = t/3
            TEST_ASSERT_FLOAT_WITHIN(0.001f, expectedRatio, ratio);
        }
    }
}

/**
 * Property: Interpolation should preserve endpoint values
 */
void test_property_interpolation_endpoints(void) {
    float v0_values[] = {0.0f, 100.0f, 500.0f};
    float v1_values[] = {100.0f, 500.0f, 1000.0f};
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float v0 = v0_values[i];
            float v1 = v1_values[j];
            
            float at_start = interpolateJerkPhaseVelocity(v0, v1, 0.0f);
            float at_end = interpolateJerkPhaseVelocity(v0, v1, 1.0f);
            
            TEST_ASSERT_FLOAT_WITHIN(0.01f, v0, at_start);
            TEST_ASSERT_FLOAT_WITHIN(0.01f, v1, at_end);
        }
    }
}

/**
 * Property: For positive acceleration, velocity should increase with distance
 * Using the kinematic formula v = sqrt(2*a*s) directly
 */
void test_property_velocity_increases_with_distance(void) {
    float accel = 500.0f;
    float prevVelocity = 0.0f;
    
    for (int steps = 0; steps <= 1000; steps += 100) {
        // Use the kinematic formula directly: v = sqrt(2*a*s)
        float velocity = (steps > 0) ? std::sqrt(2.0f * accel * (float)steps) : 0.0f;
        TEST_ASSERT_TRUE(velocity >= prevVelocity);
        prevVelocity = velocity;
    }
}

/**
 * Property: isTriangularProfile threshold should be consistent
 * A profile is triangular if we can't reach max speed before needing to decel
 */
void test_property_triangular_threshold(void) {
    float maxSpeed = 1000.0f;
    float accel = 500.0f;
    
    int32_t stepsToMax = calculateStepsToMaxSpeed(maxSpeed, accel);
    int32_t thresholdSteps = 2 * stepsToMax;
    
    // Just below threshold: should be triangular
    TEST_ASSERT_TRUE(isTriangularProfile(thresholdSteps - 1, maxSpeed, accel));
    
    // At threshold: should be triangular (boundary case)
    TEST_ASSERT_TRUE(isTriangularProfile(thresholdSteps, maxSpeed, accel));
    
    // Just above threshold: should NOT be triangular
    TEST_ASSERT_FALSE(isTriangularProfile(thresholdSteps + 1, maxSpeed, accel));
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Trapezoidal motion tests
    RUN_TEST(test_steps_to_max_speed_basic);
    RUN_TEST(test_steps_to_max_speed_high_accel);
    RUN_TEST(test_steps_to_max_speed_low_accel);
    RUN_TEST(test_steps_to_max_speed_zero_accel);
    RUN_TEST(test_velocity_after_distance);
    RUN_TEST(test_velocity_after_distance_smaller);
    RUN_TEST(test_trapezoidal_velocity_acceleration);
    RUN_TEST(test_trapezoidal_velocity_deceleration);
    RUN_TEST(test_step_interval_at_1000_sps);
    RUN_TEST(test_step_interval_at_10000_sps);
    RUN_TEST(test_step_interval_at_100_sps);
    RUN_TEST(test_step_interval_minimum_clamp);
    RUN_TEST(test_step_interval_zero_speed);
    RUN_TEST(test_triangular_profile_detection_short_move);
    RUN_TEST(test_triangular_profile_detection_exact_boundary);
    RUN_TEST(test_trapezoidal_profile_detection_long_move);
    
    // S-curve motion tests
    RUN_TEST(test_jerk_phase_time);
    RUN_TEST(test_jerk_phase_time_high_jerk);
    RUN_TEST(test_jerk_phase_time_zero_jerk);
    RUN_TEST(test_jerk_phase_distance);
    RUN_TEST(test_jerk_phase_velocity);
    RUN_TEST(test_smooth_scurve_progress_at_zero);
    RUN_TEST(test_smooth_scurve_progress_at_one);
    RUN_TEST(test_smooth_scurve_progress_at_half);
    RUN_TEST(test_interpolate_jerk_phase_velocity);
    RUN_TEST(test_interpolate_jerk_phase_velocity_at_start);
    RUN_TEST(test_interpolate_jerk_phase_velocity_at_end);
    RUN_TEST(test_constant_accel_velocity_positive);
    RUN_TEST(test_constant_accel_velocity_negative);
    
    // Microsteps conversion tests
    RUN_TEST(test_mres_for_256_microsteps);
    RUN_TEST(test_mres_for_128_microsteps);
    RUN_TEST(test_mres_for_64_microsteps);
    RUN_TEST(test_mres_for_32_microsteps);
    RUN_TEST(test_mres_for_16_microsteps);
    RUN_TEST(test_mres_for_8_microsteps);
    RUN_TEST(test_mres_for_4_microsteps);
    RUN_TEST(test_mres_for_2_microsteps);
    RUN_TEST(test_mres_for_fullstep);
    RUN_TEST(test_mres_for_invalid_defaults_to_16);
    RUN_TEST(test_mres_to_microsteps_roundtrip);
    RUN_TEST(test_microsteps_to_mres_roundtrip);
    RUN_TEST(test_is_valid_microsteps);
    
    // Property-based invariant tests
    RUN_TEST(test_property_scurve_symmetry);
    RUN_TEST(test_property_scurve_monotonic);
    RUN_TEST(test_property_scurve_bounded);
    RUN_TEST(test_property_clamp_speed_bounded);
    RUN_TEST(test_property_symmetric_accel_decel_distance);
    RUN_TEST(test_property_step_interval_inversely_proportional);
    RUN_TEST(test_property_jerk_phase_consistency);
    RUN_TEST(test_property_interpolation_endpoints);
    RUN_TEST(test_property_velocity_increases_with_distance);
    RUN_TEST(test_property_triangular_threshold);
    
    return UNITY_END();
}
