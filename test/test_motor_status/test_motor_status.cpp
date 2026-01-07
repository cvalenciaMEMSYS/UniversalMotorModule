/*
 * =============================================================================
 * TEST: MotorStatus and Error Flags
 * =============================================================================
 * 
 * Unit tests for MotorStatus structure and MotorError namespace.
 * 
 * =============================================================================
 */

#include <unity.h>
#include <Arduino.h>  // Uses mock from test/mocks/ in native build

// We need to test the MotorStatus struct and MotorError flags
// These are in IMotorDriver.h but we can extract just what we need

// Re-define locally for testing (same as in IMotorDriver.h)
namespace MotorError {
    constexpr uint8_t NONE           = 0x00;
    constexpr uint8_t OVER_TEMP      = 0x01;
    constexpr uint8_t SHORT_CIRCUIT  = 0x02;
    constexpr uint8_t OPEN_LOAD      = 0x04;
    constexpr uint8_t COMM_FAILURE   = 0x08;
    constexpr uint8_t STALL_DETECTED = 0x10;
}

struct MotorStatus {
    bool enabled = false;
    bool moving = false;
    bool stalling = false;
    
    int32_t position = 0;
    int32_t targetPosition = 0;
    
    uint16_t currentMA = 0;
    uint16_t loadValue = 0;
    
    uint8_t errorFlags = 0;
    
    float currentSpeed = 0.0f;
    
    bool hasError() const {
        return errorFlags != MotorError::NONE;
    }
};

// =============================================================================
// SETUP / TEARDOWN
// =============================================================================

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// ERROR FLAGS TESTS
// =============================================================================

void test_error_none_is_zero(void) {
    TEST_ASSERT_EQUAL(0x00, MotorError::NONE);
}

void test_error_flags_are_unique_bits(void) {
    // Each error flag should be a unique bit
    TEST_ASSERT_EQUAL(0x01, MotorError::OVER_TEMP);
    TEST_ASSERT_EQUAL(0x02, MotorError::SHORT_CIRCUIT);
    TEST_ASSERT_EQUAL(0x04, MotorError::OPEN_LOAD);
    TEST_ASSERT_EQUAL(0x08, MotorError::COMM_FAILURE);
    TEST_ASSERT_EQUAL(0x10, MotorError::STALL_DETECTED);
}

void test_error_flags_can_be_combined(void) {
    uint8_t combined = MotorError::OVER_TEMP | MotorError::SHORT_CIRCUIT;
    TEST_ASSERT_EQUAL(0x03, combined);
    
    // Check individual flags
    TEST_ASSERT_TRUE((combined & MotorError::OVER_TEMP) != 0);
    TEST_ASSERT_TRUE((combined & MotorError::SHORT_CIRCUIT) != 0);
    TEST_ASSERT_FALSE((combined & MotorError::OPEN_LOAD) != 0);
}

void test_all_errors_combined(void) {
    uint8_t all = MotorError::OVER_TEMP | MotorError::SHORT_CIRCUIT | 
                  MotorError::OPEN_LOAD | MotorError::COMM_FAILURE | 
                  MotorError::STALL_DETECTED;
    TEST_ASSERT_EQUAL(0x1F, all);
}

// =============================================================================
// MOTOR STATUS TESTS
// =============================================================================

void test_status_default_values(void) {
    MotorStatus status;
    TEST_ASSERT_FALSE(status.enabled);
    TEST_ASSERT_FALSE(status.moving);
    TEST_ASSERT_FALSE(status.stalling);
    TEST_ASSERT_EQUAL(0, status.position);
    TEST_ASSERT_EQUAL(0, status.targetPosition);
    TEST_ASSERT_EQUAL(0, status.currentMA);
    TEST_ASSERT_EQUAL(0, status.loadValue);
    TEST_ASSERT_EQUAL(0, status.errorFlags);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, status.currentSpeed);
}

void test_status_has_error_when_no_errors(void) {
    MotorStatus status;
    status.errorFlags = MotorError::NONE;
    TEST_ASSERT_FALSE(status.hasError());
}

void test_status_has_error_when_over_temp(void) {
    MotorStatus status;
    status.errorFlags = MotorError::OVER_TEMP;
    TEST_ASSERT_TRUE(status.hasError());
}

void test_status_has_error_when_short_circuit(void) {
    MotorStatus status;
    status.errorFlags = MotorError::SHORT_CIRCUIT;
    TEST_ASSERT_TRUE(status.hasError());
}

void test_status_has_error_when_open_load(void) {
    MotorStatus status;
    status.errorFlags = MotorError::OPEN_LOAD;
    TEST_ASSERT_TRUE(status.hasError());
}

void test_status_has_error_when_comm_failure(void) {
    MotorStatus status;
    status.errorFlags = MotorError::COMM_FAILURE;
    TEST_ASSERT_TRUE(status.hasError());
}

void test_status_has_error_when_stall(void) {
    MotorStatus status;
    status.errorFlags = MotorError::STALL_DETECTED;
    TEST_ASSERT_TRUE(status.hasError());
}

void test_status_has_error_with_multiple_flags(void) {
    MotorStatus status;
    status.errorFlags = MotorError::OVER_TEMP | MotorError::STALL_DETECTED;
    TEST_ASSERT_TRUE(status.hasError());
}

void test_status_position_tracking(void) {
    MotorStatus status;
    status.position = 1000;
    status.targetPosition = 2000;
    TEST_ASSERT_EQUAL(1000, status.position);
    TEST_ASSERT_EQUAL(2000, status.targetPosition);
    TEST_ASSERT_EQUAL(1000, status.targetPosition - status.position);
}

void test_status_negative_position(void) {
    MotorStatus status;
    status.position = -500;
    TEST_ASSERT_EQUAL(-500, status.position);
}

void test_status_speed_tracking(void) {
    MotorStatus status;
    status.currentSpeed = 1234.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1234.5f, status.currentSpeed);
}

void test_status_current_tracking(void) {
    MotorStatus status;
    status.currentMA = 800;
    TEST_ASSERT_EQUAL(800, status.currentMA);
}

void test_status_load_value_tracking(void) {
    MotorStatus status;
    status.loadValue = 512;
    TEST_ASSERT_EQUAL(512, status.loadValue);
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Error flags tests
    RUN_TEST(test_error_none_is_zero);
    RUN_TEST(test_error_flags_are_unique_bits);
    RUN_TEST(test_error_flags_can_be_combined);
    RUN_TEST(test_all_errors_combined);
    
    // Motor status tests
    RUN_TEST(test_status_default_values);
    RUN_TEST(test_status_has_error_when_no_errors);
    RUN_TEST(test_status_has_error_when_over_temp);
    RUN_TEST(test_status_has_error_when_short_circuit);
    RUN_TEST(test_status_has_error_when_open_load);
    RUN_TEST(test_status_has_error_when_comm_failure);
    RUN_TEST(test_status_has_error_when_stall);
    RUN_TEST(test_status_has_error_with_multiple_flags);
    RUN_TEST(test_status_position_tracking);
    RUN_TEST(test_status_negative_position);
    RUN_TEST(test_status_speed_tracking);
    RUN_TEST(test_status_current_tracking);
    RUN_TEST(test_status_load_value_tracking);
    
    return UNITY_END();
}
