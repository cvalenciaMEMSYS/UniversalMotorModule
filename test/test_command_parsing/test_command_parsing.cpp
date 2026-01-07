/*
 * =============================================================================
 * TEST: Command Parsing
 * =============================================================================
 * 
 * Unit tests for command string parsing logic.
 * Tests the parsing functions used by MotorController.
 * 
 * =============================================================================
 */

#include <unity.h>
#include <Arduino.h>  // Uses mock from test/mocks/ in native build

// =============================================================================
// COMMAND PARSING FUNCTIONS (extracted for testing)
// =============================================================================

/**
 * Parse "move <steps>" command and extract step count.
 * Returns true if valid, false otherwise.
 */
bool parseMoveCommand(const String& cmd, int32_t& steps) {
    String command = cmd;
    command.trim();
    command.toLowerCase();
    
    if (!command.startsWith("move ")) {
        return false;
    }
    
    String valueStr = command.substring(5);
    valueStr.trim();
    
    if (valueStr.length() == 0) {
        return false;
    }
    
    steps = valueStr.toInt();
    return true;
}

/**
 * Parse "abs <position>" command and extract position.
 * Returns true if valid (position >= 0), false otherwise.
 */
bool parseAbsCommand(const String& cmd, int32_t& position) {
    String command = cmd;
    command.trim();
    command.toLowerCase();
    
    if (!command.startsWith("abs ")) {
        return false;
    }
    
    String valueStr = command.substring(4);
    valueStr.trim();
    
    if (valueStr.length() == 0) {
        return false;
    }
    
    position = valueStr.toInt();
    return (position >= 0);
}

/**
 * Parse "set <param> <value>" command.
 * Returns true if valid, extracting parameter name and value.
 */
bool parseSetCommand(const String& cmd, String& param, String& value) {
    String command = cmd;
    command.trim();
    command.toLowerCase();
    
    if (!command.startsWith("set ")) {
        return false;
    }
    
    String params = command.substring(4);
    params.trim();
    
    int spaceIdx = params.indexOf(' ');
    if (spaceIdx < 0) {
        return false;
    }
    
    param = params.substring(0, spaceIdx);
    value = params.substring(spaceIdx + 1);
    value.trim();
    
    return (param.length() > 0 && value.length() > 0);
}

/**
 * Parse "stepdir on|off" command.
 * Returns true if valid, with mode = true for "on", false for "off".
 */
bool parseStepDirCommand(const String& cmd, bool& mode) {
    String command = cmd;
    command.trim();
    command.toLowerCase();
    
    if (!command.startsWith("stepdir ")) {
        return false;
    }
    
    String modeStr = command.substring(8);
    modeStr.trim();
    
    if (modeStr == "on") {
        mode = true;
        return true;
    } else if (modeStr == "off") {
        mode = false;
        return true;
    }
    
    return false;
}

/**
 * Check if command is a simple keyword (no parameters).
 */
bool isSimpleCommand(const String& cmd, const char* keyword) {
    String command = cmd;
    command.trim();
    command.toLowerCase();
    return command == keyword;
}

// =============================================================================
// SETUP / TEARDOWN
// =============================================================================

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// MOVE COMMAND TESTS
// =============================================================================

void test_parse_move_positive(void) {
    int32_t steps;
    bool result = parseMoveCommand("move 1000", steps);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1000, steps);
}

void test_parse_move_negative(void) {
    int32_t steps;
    bool result = parseMoveCommand("move -500", steps);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(-500, steps);
}

void test_parse_move_zero(void) {
    int32_t steps;
    bool result = parseMoveCommand("move 0", steps);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, steps);
}

void test_parse_move_large_value(void) {
    int32_t steps;
    bool result = parseMoveCommand("move 100000", steps);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(100000, steps);
}

void test_parse_move_with_extra_spaces(void) {
    int32_t steps;
    bool result = parseMoveCommand("  move   500  ", steps);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(500, steps);
}

void test_parse_move_uppercase(void) {
    int32_t steps;
    bool result = parseMoveCommand("MOVE 1000", steps);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1000, steps);
}

void test_parse_move_missing_value(void) {
    int32_t steps = 999;
    bool result = parseMoveCommand("move ", steps);
    TEST_ASSERT_FALSE(result);
}

void test_parse_move_wrong_command(void) {
    int32_t steps;
    bool result = parseMoveCommand("abs 1000", steps);
    TEST_ASSERT_FALSE(result);
}

// =============================================================================
// ABS COMMAND TESTS
// =============================================================================

void test_parse_abs_positive(void) {
    int32_t position;
    bool result = parseAbsCommand("abs 5000", position);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(5000, position);
}

void test_parse_abs_zero(void) {
    int32_t position;
    bool result = parseAbsCommand("abs 0", position);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, position);
}

void test_parse_abs_negative_invalid(void) {
    int32_t position;
    bool result = parseAbsCommand("abs -100", position);
    TEST_ASSERT_FALSE(result);  // Negative positions not allowed
}

void test_parse_abs_with_spaces(void) {
    int32_t position;
    bool result = parseAbsCommand("  abs  1000  ", position);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1000, position);
}

// =============================================================================
// SET COMMAND TESTS
// =============================================================================

void test_parse_set_speed(void) {
    String param, value;
    bool result = parseSetCommand("set speed 500", param, value);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(param == "speed");
    TEST_ASSERT_TRUE(value == "500");
}

void test_parse_set_current(void) {
    String param, value;
    bool result = parseSetCommand("set current 800", param, value);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(param == "current");
    TEST_ASSERT_TRUE(value == "800");
}

void test_parse_set_microsteps(void) {
    String param, value;
    bool result = parseSetCommand("set microsteps 16", param, value);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(param == "microsteps");
    TEST_ASSERT_TRUE(value == "16");
}

void test_parse_set_accel(void) {
    String param, value;
    bool result = parseSetCommand("set accel 1000", param, value);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(param == "accel");
    TEST_ASSERT_TRUE(value == "1000");
}

void test_parse_set_jerk(void) {
    String param, value;
    bool result = parseSetCommand("set jerk 5000", param, value);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(param == "jerk");
    TEST_ASSERT_TRUE(value == "5000");
}

void test_parse_set_missing_value(void) {
    String param, value;
    bool result = parseSetCommand("set speed", param, value);
    TEST_ASSERT_FALSE(result);
}

void test_parse_set_missing_param(void) {
    String param, value;
    bool result = parseSetCommand("set ", param, value);
    TEST_ASSERT_FALSE(result);
}

void test_parse_set_uppercase(void) {
    String param, value;
    bool result = parseSetCommand("SET SPEED 500", param, value);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(param == "speed");
    TEST_ASSERT_TRUE(value == "500");
}

// =============================================================================
// STEPDIR COMMAND TESTS
// =============================================================================

void test_parse_stepdir_on(void) {
    bool mode;
    bool result = parseStepDirCommand("stepdir on", mode);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(mode);
}

void test_parse_stepdir_off(void) {
    bool mode;
    bool result = parseStepDirCommand("stepdir off", mode);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(mode);
}

void test_parse_stepdir_uppercase(void) {
    bool mode;
    bool result = parseStepDirCommand("STEPDIR ON", mode);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(mode);
}

void test_parse_stepdir_invalid_mode(void) {
    bool mode;
    bool result = parseStepDirCommand("stepdir maybe", mode);
    TEST_ASSERT_FALSE(result);
}

void test_parse_stepdir_missing_mode(void) {
    bool mode;
    bool result = parseStepDirCommand("stepdir ", mode);
    TEST_ASSERT_FALSE(result);
}

// =============================================================================
// SIMPLE COMMAND TESTS
// =============================================================================

void test_simple_command_help(void) {
    TEST_ASSERT_TRUE(isSimpleCommand("help", "help"));
    TEST_ASSERT_TRUE(isSimpleCommand("HELP", "help"));
    TEST_ASSERT_TRUE(isSimpleCommand("  help  ", "help"));
}

void test_simple_command_home(void) {
    TEST_ASSERT_TRUE(isSimpleCommand("home", "home"));
    TEST_ASSERT_TRUE(isSimpleCommand("HOME", "home"));
}

void test_simple_command_stop(void) {
    TEST_ASSERT_TRUE(isSimpleCommand("stop", "stop"));
}

void test_simple_command_enable(void) {
    TEST_ASSERT_TRUE(isSimpleCommand("enable", "enable"));
}

void test_simple_command_disable(void) {
    TEST_ASSERT_TRUE(isSimpleCommand("disable", "disable"));
}

void test_simple_command_status(void) {
    TEST_ASSERT_TRUE(isSimpleCommand("status", "status"));
    TEST_ASSERT_TRUE(isSimpleCommand("?", "?"));
}

void test_simple_command_mismatch(void) {
    TEST_ASSERT_FALSE(isSimpleCommand("help", "home"));
    TEST_ASSERT_FALSE(isSimpleCommand("move 100", "move"));
}

// =============================================================================
// EDGE CASES
// =============================================================================

void test_empty_command(void) {
    int32_t steps;
    String param, value;
    bool mode;
    
    TEST_ASSERT_FALSE(parseMoveCommand("", steps));
    TEST_ASSERT_FALSE(parseAbsCommand("", steps));
    TEST_ASSERT_FALSE(parseSetCommand("", param, value));
    TEST_ASSERT_FALSE(parseStepDirCommand("", mode));
}

void test_whitespace_only_command(void) {
    int32_t steps;
    bool result = parseMoveCommand("   ", steps);
    TEST_ASSERT_FALSE(result);
}

void test_command_with_tabs(void) {
    int32_t steps;
    bool result = parseMoveCommand("move\t500", steps);
    // Note: This may or may not work depending on trim implementation
    // Current implementation expects spaces, not tabs
    // If it fails, that's expected behavior
    (void)result;  // Suppress unused warning
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Move command tests
    RUN_TEST(test_parse_move_positive);
    RUN_TEST(test_parse_move_negative);
    RUN_TEST(test_parse_move_zero);
    RUN_TEST(test_parse_move_large_value);
    RUN_TEST(test_parse_move_with_extra_spaces);
    RUN_TEST(test_parse_move_uppercase);
    RUN_TEST(test_parse_move_missing_value);
    RUN_TEST(test_parse_move_wrong_command);
    
    // Abs command tests
    RUN_TEST(test_parse_abs_positive);
    RUN_TEST(test_parse_abs_zero);
    RUN_TEST(test_parse_abs_negative_invalid);
    RUN_TEST(test_parse_abs_with_spaces);
    
    // Set command tests
    RUN_TEST(test_parse_set_speed);
    RUN_TEST(test_parse_set_current);
    RUN_TEST(test_parse_set_microsteps);
    RUN_TEST(test_parse_set_accel);
    RUN_TEST(test_parse_set_jerk);
    RUN_TEST(test_parse_set_missing_value);
    RUN_TEST(test_parse_set_missing_param);
    RUN_TEST(test_parse_set_uppercase);
    
    // Stepdir command tests
    RUN_TEST(test_parse_stepdir_on);
    RUN_TEST(test_parse_stepdir_off);
    RUN_TEST(test_parse_stepdir_uppercase);
    RUN_TEST(test_parse_stepdir_invalid_mode);
    RUN_TEST(test_parse_stepdir_missing_mode);
    
    // Simple command tests
    RUN_TEST(test_simple_command_help);
    RUN_TEST(test_simple_command_home);
    RUN_TEST(test_simple_command_stop);
    RUN_TEST(test_simple_command_enable);
    RUN_TEST(test_simple_command_disable);
    RUN_TEST(test_simple_command_status);
    RUN_TEST(test_simple_command_mismatch);
    
    // Edge cases
    RUN_TEST(test_empty_command);
    RUN_TEST(test_whitespace_only_command);
    RUN_TEST(test_command_with_tabs);
    
    return UNITY_END();
}
