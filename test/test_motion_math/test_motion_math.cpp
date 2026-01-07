/*
 * =============================================================================
 * TEST: Motion Planning Math
 * =============================================================================
 * 
 * Unit tests for motion planning calculations:
 *   - Trapezoidal motion planning
 *   - S-curve motion planning
 *   - Velocity/step interval calculations
 * 
 * These are extracted from the driver implementations for testing.
 * 
 * =============================================================================
 */

#include <unity.h>
#include <cmath>
#include <Arduino.h>  // Uses mock from test/mocks/ in native build

// =============================================================================
// MOTION MATH FUNCTIONS (extracted for testing)
// =============================================================================

/**
 * Calculate steps to reach maximum speed from rest using trapezoidal profile.
 * Uses: v² = 2*a*d → d = v²/(2*a)
 */
int32_t calculateStepsToMaxSpeed(float maxSpeed, float acceleration) {
    if (acceleration <= 0) return 0;
    return (int32_t)((maxSpeed * maxSpeed) / (2.0f * acceleration));
}

/**
 * Calculate velocity after traveling a distance with constant acceleration.
 * Uses: v = sqrt(2*a*d)
 */
float calculateVelocityAfterDistance(float acceleration, int32_t steps) {
    if (acceleration <= 0 || steps <= 0) return 0;
    return sqrtf(2.0f * acceleration * (float)steps);
}

/**
 * Calculate velocity during deceleration phase.
 * Uses: v = sqrt(2*a*d) where d = remaining distance
 */
float calculateDecelerationVelocity(float acceleration, int32_t stepsRemaining) {
    if (acceleration <= 0 || stepsRemaining <= 0) return 0;
    return sqrtf(2.0f * acceleration * (float)stepsRemaining);
}

/**
 * Calculate step interval (microseconds) from speed (steps/sec).
 */
uint32_t calculateStepInterval(float speed) {
    if (speed <= 0) return 1000000;  // 1 second default
    uint32_t interval = (uint32_t)(1000000.0f / speed);
    if (interval < 10) interval = 10;  // Minimum 10µs
    return interval;
}

/**
 * Determine if trapezoidal profile is triangular (never reaches max speed).
 */
bool isTrapezoidalTriangular(int32_t totalSteps, float maxSpeed, float acceleration) {
    int32_t stepsToMax = calculateStepsToMaxSpeed(maxSpeed, acceleration);
    return (2 * stepsToMax >= totalSteps);
}

/**
 * Calculate S-curve jerk phase time: t_j = a_max / j
 */
float calculateJerkPhaseTime(float maxAccel, float jerk) {
    if (jerk <= 0) return 0;
    return maxAccel / jerk;
}

/**
 * Calculate distance covered during jerk phase: s = j * t³ / 6
 */
float calculateJerkPhaseDistance(float jerk, float time) {
    if (jerk <= 0 || time <= 0) return 0;
    return jerk * time * time * time / 6.0f;
}

/**
 * Calculate velocity gained during jerk phase: v = j * t² / 2
 */
float calculateJerkPhaseVelocity(float jerk, float time) {
    if (jerk <= 0 || time <= 0) return 0;
    return jerk * time * time / 2.0f;
}

/**
 * S-curve smooth progress function for jerk phases.
 * Uses: 3*p² - 2*p³ for smooth acceleration at boundaries
 */
float smoothSCurveProgress(float progress) {
    if (progress <= 0) return 0;
    if (progress >= 1) return 1;
    return progress * progress * (3.0f - 2.0f * progress);
}

/**
 * Convert microsteps to MRES register value.
 * 256 microsteps = MRES 0
 * 128 = MRES 1, 64 = MRES 2, ..., 1 = MRES 8
 */
uint8_t microStepsToMRES(uint16_t microsteps) {
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
        default:  return 4;  // Default to 16
    }
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

void test_deceleration_velocity(void) {
    // Same formula as acceleration velocity
    float velocity = calculateDecelerationVelocity(500.0f, 1000);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1000.0f, velocity);
}

void test_deceleration_velocity_at_end(void) {
    // Near end of move, remaining steps is small
    float velocity = calculateDecelerationVelocity(500.0f, 10);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, velocity);
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
    bool triangular = isTrapezoidalTriangular(500, 1000.0f, 500.0f);
    TEST_ASSERT_TRUE(triangular);
}

void test_triangular_profile_detection_exact_boundary(void) {
    // Exactly at boundary: 2000 steps, need 1000 steps to reach max
    // 2 * 1000 = 2000, so this is triangular (never cruises)
    bool triangular = isTrapezoidalTriangular(2000, 1000.0f, 500.0f);
    TEST_ASSERT_TRUE(triangular);
}

void test_trapezoidal_profile_detection_long_move(void) {
    // Long move: 5000 steps, need 1000 steps to reach max
    // 2 * 1000 = 2000 < 5000, so full trapezoidal
    bool triangular = isTrapezoidalTriangular(5000, 1000.0f, 500.0f);
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

void test_smooth_scurve_progress_symmetry(void) {
    // S-curve should be symmetric around the midpoint
    float at_quarter = smoothSCurveProgress(0.25f);
    float at_three_quarter = smoothSCurveProgress(0.75f);
    // at_quarter + at_three_quarter should equal 1.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, at_quarter + at_three_quarter);
}

void test_smooth_scurve_monotonically_increasing(void) {
    float prev = 0.0f;
    for (float p = 0.1f; p <= 1.0f; p += 0.1f) {
        float current = smoothSCurveProgress(p);
        TEST_ASSERT_TRUE(current >= prev);
        prev = current;
    }
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
    RUN_TEST(test_deceleration_velocity);
    RUN_TEST(test_deceleration_velocity_at_end);
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
    RUN_TEST(test_smooth_scurve_progress_symmetry);
    RUN_TEST(test_smooth_scurve_monotonically_increasing);
    
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
    
    return UNITY_END();
}
