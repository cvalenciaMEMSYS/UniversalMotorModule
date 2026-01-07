/*
 * =============================================================================
 * TEST: AccelerationProfile
 * =============================================================================
 * 
 * Unit tests for acceleration profile factory methods and utility functions.
 * 
 * =============================================================================
 */

#include <unity.h>
#include <Arduino.h>  // Uses mock from test/mocks/ in native build
#include "../../src/core/AccelerationProfile.h"

// =============================================================================
// SETUP / TEARDOWN
// =============================================================================

void setUp(void) {
    // Reset any state before each test
}

void tearDown(void) {
    // Cleanup after each test
}

// =============================================================================
// CONSTANT PROFILE TESTS
// =============================================================================

void test_constant_profile_type_is_constant(void) {
    AccelerationProfile profile = AccelerationProfile::constant(1000);
    TEST_ASSERT_EQUAL(VelocityProfileType::CONSTANT, profile.type);
}

void test_constant_profile_has_zero_acceleration(void) {
    AccelerationProfile profile = AccelerationProfile::constant(1000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile.acceleration);
}

void test_constant_profile_has_zero_jerk(void) {
    AccelerationProfile profile = AccelerationProfile::constant(1000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile.jerk);
}

void test_constant_profile_stores_max_speed(void) {
    AccelerationProfile profile = AccelerationProfile::constant(500);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, profile.maxSpeed);
}

void test_constant_profile_has_no_acceleration(void) {
    AccelerationProfile profile = AccelerationProfile::constant(1000);
    TEST_ASSERT_FALSE(profile.hasAcceleration());
}

void test_constant_profile_name(void) {
    AccelerationProfile profile = AccelerationProfile::constant(1000);
    TEST_ASSERT_EQUAL_STRING("Constant", profile.getTypeName());
}

// =============================================================================
// TRAPEZOIDAL PROFILE TESTS
// =============================================================================

void test_trapezoidal_profile_type_is_trapezoidal(void) {
    AccelerationProfile profile = AccelerationProfile::trapezoidal(1000, 500);
    TEST_ASSERT_EQUAL(VelocityProfileType::TRAPEZOIDAL, profile.type);
}

void test_trapezoidal_profile_stores_max_speed(void) {
    AccelerationProfile profile = AccelerationProfile::trapezoidal(800, 300);
    TEST_ASSERT_EQUAL_FLOAT(800.0f, profile.maxSpeed);
}

void test_trapezoidal_profile_stores_acceleration(void) {
    AccelerationProfile profile = AccelerationProfile::trapezoidal(1000, 250);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, profile.acceleration);
}

void test_trapezoidal_profile_has_zero_jerk(void) {
    AccelerationProfile profile = AccelerationProfile::trapezoidal(1000, 500);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile.jerk);
}

void test_trapezoidal_profile_has_acceleration(void) {
    AccelerationProfile profile = AccelerationProfile::trapezoidal(1000, 500);
    TEST_ASSERT_TRUE(profile.hasAcceleration());
}

void test_trapezoidal_profile_name(void) {
    AccelerationProfile profile = AccelerationProfile::trapezoidal(1000, 500);
    TEST_ASSERT_EQUAL_STRING("Trapezoidal", profile.getTypeName());
}

void test_trapezoidal_with_zero_accel_has_no_acceleration(void) {
    AccelerationProfile profile = AccelerationProfile::trapezoidal(1000, 0);
    TEST_ASSERT_FALSE(profile.hasAcceleration());
}

// =============================================================================
// S-CURVE PROFILE TESTS
// =============================================================================

void test_scurve_profile_type_is_scurve(void) {
    AccelerationProfile profile = AccelerationProfile::sCurve(1000, 500, 5000);
    TEST_ASSERT_EQUAL(VelocityProfileType::S_CURVE, profile.type);
}

void test_scurve_profile_stores_max_speed(void) {
    AccelerationProfile profile = AccelerationProfile::sCurve(1500, 500, 5000);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, profile.maxSpeed);
}

void test_scurve_profile_stores_acceleration(void) {
    AccelerationProfile profile = AccelerationProfile::sCurve(1000, 750, 5000);
    TEST_ASSERT_EQUAL_FLOAT(750.0f, profile.acceleration);
}

void test_scurve_profile_stores_jerk(void) {
    AccelerationProfile profile = AccelerationProfile::sCurve(1000, 500, 10000);
    TEST_ASSERT_EQUAL_FLOAT(10000.0f, profile.jerk);
}

void test_scurve_profile_has_acceleration(void) {
    AccelerationProfile profile = AccelerationProfile::sCurve(1000, 500, 5000);
    TEST_ASSERT_TRUE(profile.hasAcceleration());
}

void test_scurve_profile_name(void) {
    AccelerationProfile profile = AccelerationProfile::sCurve(1000, 500, 5000);
    TEST_ASSERT_EQUAL_STRING("S-Curve", profile.getTypeName());
}

// =============================================================================
// EDGE CASES
// =============================================================================

void test_default_profile_values(void) {
    AccelerationProfile profile;
    TEST_ASSERT_EQUAL(VelocityProfileType::CONSTANT, profile.type);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, profile.maxSpeed);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, profile.acceleration);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, profile.jerk);
}

void test_profile_with_negative_speed_still_stores(void) {
    // Factory methods don't validate - that's the caller's job
    AccelerationProfile profile = AccelerationProfile::constant(-100);
    TEST_ASSERT_EQUAL_FLOAT(-100.0f, profile.maxSpeed);
}

void test_profile_with_zero_speed(void) {
    AccelerationProfile profile = AccelerationProfile::constant(0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile.maxSpeed);
}

void test_profile_with_very_large_values(void) {
    AccelerationProfile profile = AccelerationProfile::sCurve(100000.0f, 50000.0f, 1000000.0f);
    TEST_ASSERT_EQUAL_FLOAT(100000.0f, profile.maxSpeed);
    TEST_ASSERT_EQUAL_FLOAT(50000.0f, profile.acceleration);
    TEST_ASSERT_EQUAL_FLOAT(1000000.0f, profile.jerk);
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Constant profile tests
    RUN_TEST(test_constant_profile_type_is_constant);
    RUN_TEST(test_constant_profile_has_zero_acceleration);
    RUN_TEST(test_constant_profile_has_zero_jerk);
    RUN_TEST(test_constant_profile_stores_max_speed);
    RUN_TEST(test_constant_profile_has_no_acceleration);
    RUN_TEST(test_constant_profile_name);
    
    // Trapezoidal profile tests
    RUN_TEST(test_trapezoidal_profile_type_is_trapezoidal);
    RUN_TEST(test_trapezoidal_profile_stores_max_speed);
    RUN_TEST(test_trapezoidal_profile_stores_acceleration);
    RUN_TEST(test_trapezoidal_profile_has_zero_jerk);
    RUN_TEST(test_trapezoidal_profile_has_acceleration);
    RUN_TEST(test_trapezoidal_profile_name);
    RUN_TEST(test_trapezoidal_with_zero_accel_has_no_acceleration);
    
    // S-Curve profile tests
    RUN_TEST(test_scurve_profile_type_is_scurve);
    RUN_TEST(test_scurve_profile_stores_max_speed);
    RUN_TEST(test_scurve_profile_stores_acceleration);
    RUN_TEST(test_scurve_profile_stores_jerk);
    RUN_TEST(test_scurve_profile_has_acceleration);
    RUN_TEST(test_scurve_profile_name);
    
    // Edge cases
    RUN_TEST(test_default_profile_values);
    RUN_TEST(test_profile_with_negative_speed_still_stores);
    RUN_TEST(test_profile_with_zero_speed);
    RUN_TEST(test_profile_with_very_large_values);
    
    return UNITY_END();
}
