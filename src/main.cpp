/*
 * =============================================================================
 * TMC2209 Stepper Motor Control for ESP32-S3 Super Mini
 * =============================================================================
 * 
 * Full-featured stepper motor control using TMC2209 driver with UART interface.
 * Supports StealthChop (quiet), SpreadCycle (torque), StallGuard, and CoolStep.
 * 
 * =============================================================================
 * WIRING GUIDE - ESP32-S3 Super Mini to TMC2209 v1.3
 * =============================================================================
 * 
 * Control Pins:
 *   ESP32-S3 GPIO 4  →  TMC2209 EN   (Enable, active LOW)
 *   ESP32-S3 GPIO 5  →  TMC2209 STEP (Step pulses)
 *   ESP32-S3 GPIO 6  →  TMC2209 DIR  (Direction)
 * 
 * UART Pins (Option 1 - Recommended):
 *   ESP32-S3 GPIO 1 ─┬─ 1kΩ resistor ─→ TMC2209 RX pin
 *   ESP32-S3 GPIO 2 ─┘
 *   (TMC2209 TX pin left unconnected)
 * 
 * UART Pins (Option 2 - Direct):
 *   ESP32-S3 GPIO 1  →  TMC2209 TX pin
 *   ESP32-S3 GPIO 2  →  TMC2209 RX pin
 * 
 * Power Pins:
 *   ESP32-S3 3.3V    →  TMC2209 VIO (Logic power)
 *   ESP32-S3 GND     →  TMC2209 GND (Common ground)
 *   12-28V PSU (+)   →  TMC2209 VS  (Motor power)
 *   12-28V PSU (-)   →  Common GND
 * 
 * Motor Pins:
 *   TMC2209 A1, A2   →  Motor Coil A
 *   TMC2209 B1, B2   →  Motor Coil B
 * 
 * Address Pins:
 *   MS1, MS2 = Leave floating (open) for UART address 0b00
 * 
 * =============================================================================
 * SERIAL COMMANDS (115200 baud)
 * =============================================================================
 *   1 - Rotate clockwise (200 steps)
 *   2 - Rotate counter-clockwise (200 steps)
 *   3 - Continuous rotation CW (press '0' to stop)
 *   4 - Continuous rotation CCW (press '0' to stop)
 *   0 - Stop continuous rotation
 *   5 - Increase speed (reduce delay)
 *   6 - Decrease speed (increase delay)
 *   7 - Toggle StealthChop/SpreadCycle mode
 *   9 - Change microstepping (1-256)
 *   c - Change motor current (100-2000 mA)
 *   e - Toggle enable/disable
 *   r - Reset driver
 *   s - Read StallGuard value
 *   d - Display full diagnostics
 *   h - Show help menu
 *   x - Restart ESP32
 * =============================================================================
 */

#include <Arduino.h>
#include <TMCStepper.h>
#include <esp32-hal.h>

// =============================================================================
// PIN DEFINITIONS
// =============================================================================

// Control pins
#define STEP_PIN        5        // Step pulse output
#define DIR_PIN         6        // Direction control
#define ENABLE_PIN      4        // Enable (active LOW)

// UART pins for TMC2209 communication
#define RX_PIN          1        // UART receive from TMC2209
#define TX_PIN          2        // UART transmit to TMC2209
#define SERIAL_PORT     Serial1  // Hardware UART peripheral

// =============================================================================
// DRIVER CONFIGURATION
// =============================================================================

#define DRIVER_ADDRESS  0b00     // TMC2209 address (MS1=0, MS2=0)
#define R_SENSE         0.11f    // Sense resistor value (TMC2209 v1.3)

#define MOTOR_STEPS     200      // Steps per revolution (1.8° motor)
#define MICROSTEPS      16       // Default microstepping
#define RMS_CURRENT     800      // Default motor current (mA)
#define STALL_VALUE     10       // StallGuard threshold

// Speed limits
#define MIN_SPEED_DELAY 100      // Minimum delay (fastest speed) in µs
#define MAX_SPEED_DELAY 5000     // Maximum delay (slowest speed) in µs
#define DEFAULT_SPEED   1000     // Default step delay in µs

// =============================================================================
// GLOBAL OBJECTS AND VARIABLES
// =============================================================================

// TMC2209 driver object
TMC2209Stepper driver(&SERIAL_PORT, R_SENSE, DRIVER_ADDRESS);

// State variables
int currentSpeed = DEFAULT_SPEED;     // Current step delay in microseconds
bool enableState = true;              // true = enabled (EN LOW)
bool stealthChopEnabled = true;       // true = StealthChop, false = SpreadCycle

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

void handleCommand(char cmd);
void rotateMotor(bool direction, int steps, int delayTime);
void continuousRotation(bool direction);
void stepMotor(int steps);
void changeMicrostepping();
void changeCurrent();
void readStallGuard();
void displayDiagnostics();
void toggleEnable();
void resetDriver();
void printMenu();

// =============================================================================
// SETUP - One-time initialization
// =============================================================================

void setup() {
    // Initialize USB Serial with timeout for CDC enumeration
    Serial.begin(115200);
    unsigned long startTime = millis();
    while (!Serial && (millis() - startTime < 3000)) {
        // Wait up to 3 seconds for USB Serial to connect
    }
    
    Serial.println();
    Serial.println("==============================================");
    Serial.println("   TMC2209 Stepper Driver Control");
    Serial.println("   ESP32-S3 Super Mini Edition");
    Serial.println("==============================================");
    
    // Initialize UART for TMC2209 communication
    Serial.print("Initializing UART on RX=GPIO");
    Serial.print(RX_PIN);
    Serial.print(", TX=GPIO");
    Serial.print(TX_PIN);
    Serial.println("...");
    
    SERIAL_PORT.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    Serial.println("✓ UART initialized successfully");
    
    // Configure GPIO pins
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(ENABLE_PIN, OUTPUT);
    
    // Enable driver (active LOW)
    digitalWrite(ENABLE_PIN, LOW);
    enableState = true;
    
    // Initial pin states
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN, LOW);
    
    // Wait for driver power stabilization
    delay(100);
    
    // Initialize TMC2209 driver
    Serial.println("Configuring TMC2209 driver...");
    driver.begin();
    
    // Basic driver configuration
    driver.toff(5);                      // Enable driver (TOFF > 0)
    driver.rms_current(RMS_CURRENT);     // Set motor current
    driver.microsteps(MICROSTEPS);       // Set microstepping
    
    // StealthChop configuration (quiet mode)
    driver.pwm_autoscale(true);          // Enable automatic PWM scaling
    driver.pwm_autograd(true);           // Enable automatic gradient adaptation
    driver.en_spreadCycle(false);        // Disable SpreadCycle (enable StealthChop)
    stealthChopEnabled = true;
    
    // StallGuard configuration
    driver.TCOOLTHRS(0xFFFFF);           // Enable StallGuard at all speeds
    driver.SGTHRS(STALL_VALUE);          // Set stall threshold
    
    // CoolStep configuration (automatic current regulation)
    driver.semin(5);                     // Minimum StallGuard for current reduction
    driver.semax(2);                     // Maximum StallGuard for current increase
    driver.sedn(0b01);                   // Current down-step speed
    
    // Test TMC2209 connection
    Serial.println("Testing TMC2209 connection...");
    delay(10);
    
    uint8_t result = driver.test_connection();
    if (result == 0) {
        Serial.println("✓ TMC2209 Connection Successful!");
    } else if (result == 1) {
        Serial.println("✗ TMC2209 Connection Failed: No response");
        Serial.println("  Check wiring, power, and UART connections");
    } else {
        Serial.println("✗ TMC2209 Connection Failed: Invalid response");
        Serial.println("  Check baud rate and driver address");
    }
    
    // Display current configuration
    Serial.println();
    Serial.println("Current Configuration:");
    Serial.print("  RMS Current: ");
    Serial.print(RMS_CURRENT);
    Serial.println(" mA");
    Serial.print("  Microsteps: ");
    Serial.println(MICROSTEPS);
    Serial.print("  Mode: ");
    Serial.println(stealthChopEnabled ? "StealthChop (Quiet)" : "SpreadCycle (Torque)");
    Serial.println();
    Serial.println("Press 'h' for command menu");
    Serial.println("==============================================");
    Serial.println();
}

// =============================================================================
// MAIN LOOP - Command processing
// =============================================================================

void loop() {
    if (Serial.available() > 0) {
        char command = Serial.read();
        handleCommand(command);
    }
}

// =============================================================================
// COMMAND HANDLER
// =============================================================================

void handleCommand(char cmd) {
    switch (cmd) {
        // Basic movement commands
        case '1':
            Serial.println("Rotating clockwise (200 steps)...");
            rotateMotor(true, 200, currentSpeed);
            Serial.println("Done");
            break;
            
        case '2':
            Serial.println("Rotating counter-clockwise (200 steps)...");
            rotateMotor(false, 200, currentSpeed);
            Serial.println("Done");
            break;
            
        case '3':
            Serial.println("Continuous rotation CW (press '0' to stop)");
            continuousRotation(true);
            break;
            
        case '4':
            Serial.println("Continuous rotation CCW (press '0' to stop)");
            continuousRotation(false);
            break;
            
        case '0':
            Serial.println("Stopped");
            break;
            
        // Speed control
        case '5':
            if (currentSpeed > MIN_SPEED_DELAY) {
                currentSpeed -= 100;
                if (currentSpeed < MIN_SPEED_DELAY) {
                    currentSpeed = MIN_SPEED_DELAY;
                }
            }
            Serial.print("Speed increased. Delay: ");
            Serial.print(currentSpeed);
            Serial.println(" µs");
            break;
            
        case '6':
            if (currentSpeed < MAX_SPEED_DELAY) {
                currentSpeed += 100;
                if (currentSpeed > MAX_SPEED_DELAY) {
                    currentSpeed = MAX_SPEED_DELAY;
                }
            }
            Serial.print("Speed decreased. Delay: ");
            Serial.print(currentSpeed);
            Serial.println(" µs");
            break;
            
        // Chopper mode toggle
        case '7':
            stealthChopEnabled = !stealthChopEnabled;
            driver.en_spreadCycle(!stealthChopEnabled);
            if (stealthChopEnabled) {
                Serial.println("StealthChop ENABLED (Quiet mode)");
            } else {
                Serial.println("SpreadCycle ENABLED (High torque mode)");
            }
            break;
            
        // Configuration commands
        case '9':
            changeMicrostepping();
            break;
            
        case 'c':
        case 'C':
            changeCurrent();
            break;
            
        case 'e':
        case 'E':
            toggleEnable();
            break;
            
        case 'r':
        case 'R':
            resetDriver();
            break;
            
        // Diagnostic commands
        case 's':
        case 'S':
            readStallGuard();
            break;
            
        case 'd':
        case 'D':
            displayDiagnostics();
            break;
            
        case 'h':
        case 'H':
            printMenu();
            break;
            
        // System commands
        case 'x':
        case 'X':
            Serial.println("Restarting ESP32...");
            delay(100);
            esp_restart();
            break;
            
        // Ignore newlines and carriage returns
        case '\n':
        case '\r':
            break;
            
        default:
            Serial.print("Unknown command: '");
            Serial.print(cmd);
            Serial.println("' - Press 'h' for help");
            break;
    }
}

// =============================================================================
// MOTOR CONTROL FUNCTIONS
// =============================================================================

/**
 * Rotate motor a fixed number of steps
 * @param direction true = clockwise, false = counter-clockwise
 * @param steps Number of steps to rotate
 * @param delayTime Delay between step pulses in microseconds
 */
void rotateMotor(bool direction, int steps, int delayTime) {
    digitalWrite(DIR_PIN, direction ? HIGH : LOW);
    
    for (int i = 0; i < steps; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(delayTime);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(delayTime);
    }
}

/**
 * Continuous rotation until stopped
 * @param direction true = clockwise, false = counter-clockwise
 */
void continuousRotation(bool direction) {
    digitalWrite(DIR_PIN, direction ? HIGH : LOW);
    
    while (true) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(currentSpeed);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(currentSpeed);
        
        // Check for stop command
        if (Serial.available() > 0) {
            char cmd = Serial.read();
            if (cmd == '0') {
                Serial.println("Stopped");
                break;
            }
        }
    }
}

/**
 * Generate a specified number of step pulses
 * @param steps Number of steps to generate
 */
void stepMotor(int steps) {
    for (int i = 0; i < steps; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(currentSpeed);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(currentSpeed);
    }
}

// =============================================================================
// CONFIGURATION FUNCTIONS
// =============================================================================

/**
 * Interactive microstepping configuration
 */
void changeMicrostepping() {
    Serial.println();
    Serial.println("Select microstepping:");
    Serial.println("  1: 1   (full step)");
    Serial.println("  2: 2   (half step)");
    Serial.println("  3: 4   (quarter step)");
    Serial.println("  4: 8");
    Serial.println("  5: 16  (default)");
    Serial.println("  6: 32");
    Serial.println("  7: 64");
    Serial.println("  8: 128");
    Serial.println("  9: 256 (finest)");
    Serial.print("Enter choice (1-9): ");
    
    // Wait for input
    while (!Serial.available()) {
        delay(10);
    }
    
    char choice = Serial.read();
    Serial.println(choice);
    
    int microsteps = 16; // default
    
    switch (choice) {
        case '1': microsteps = 1; break;
        case '2': microsteps = 2; break;
        case '3': microsteps = 4; break;
        case '4': microsteps = 8; break;
        case '5': microsteps = 16; break;
        case '6': microsteps = 32; break;
        case '7': microsteps = 64; break;
        case '8': microsteps = 128; break;
        case '9': microsteps = 256; break;
        default:
            Serial.println("Invalid choice, keeping current setting");
            return;
    }
    
    driver.microsteps(microsteps);
    Serial.print("Microstepping set to: ");
    Serial.println(microsteps);
}

/**
 * Interactive motor current configuration
 */
void changeCurrent() {
    Serial.println();
    Serial.print("Enter RMS current in mA (100-2000): ");
    
    // Wait for input
    while (!Serial.available()) {
        delay(10);
    }
    
    int current = Serial.parseInt();
    
    // Clear any remaining characters
    while (Serial.available()) {
        Serial.read();
    }
    
    if (current >= 100 && current <= 2000) {
        driver.rms_current(current);
        Serial.println();
        Serial.print("Current set to: ");
        Serial.print(current);
        Serial.println(" mA");
    } else {
        Serial.println();
        Serial.println("Invalid current value. Must be 100-2000 mA");
    }
}

// =============================================================================
// DIAGNOSTIC FUNCTIONS
// =============================================================================

/**
 * Read and display StallGuard value
 */
void readStallGuard() {
    Serial.println();
    Serial.println("=== StallGuard Reading ===");
    
    uint32_t drv_status = driver.DRV_STATUS();
    int16_t sg_result = drv_status & 0x3FF;  // Lower 10 bits
    
    Serial.print("SG Result: ");
    Serial.println(sg_result);
    Serial.print("SG Threshold: ");
    Serial.println(driver.SGTHRS());
    Serial.print("Standstill: ");
    Serial.println((drv_status >> 31) & 1 ? "Yes" : "No");
    
    if (sg_result < driver.SGTHRS()) {
        Serial.println("⚠ Warning: Motor may be stalled!");
    }
    Serial.println();
}

/**
 * Display comprehensive driver diagnostics
 */
void displayDiagnostics() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("        TMC2209 Diagnostics");
    Serial.println("========================================");
    
    uint32_t drv_status = driver.DRV_STATUS();
    
    // StallGuard and current status
    Serial.println("--- Status ---");
    Serial.print("StallGuard Result: ");
    Serial.println(drv_status & 0x3FF);
    Serial.print("CS Actual: ");
    Serial.println((drv_status >> 16) & 0x1F);
    Serial.print("Standstill: ");
    Serial.println((drv_status >> 31) & 1);
    
    // Error flags
    Serial.println();
    Serial.println("--- Error Flags ---");
    Serial.print("Open Load A: ");
    Serial.println((drv_status >> 30) & 1);
    Serial.print("Open Load B: ");
    Serial.println((drv_status >> 29) & 1);
    Serial.print("Low-side short A: ");
    Serial.println((drv_status >> 28) & 1);
    Serial.print("Low-side short B: ");
    Serial.println((drv_status >> 27) & 1);
    Serial.print("Ground short A: ");
    Serial.println((drv_status >> 26) & 1);
    Serial.print("Ground short B: ");
    Serial.println((drv_status >> 25) & 1);
    
    // Temperature status
    Serial.println();
    Serial.println("--- Temperature ---");
    bool ot = (drv_status >> 24) & 1;
    bool otpw = (drv_status >> 23) & 1;
    Serial.print("Overtemperature: ");
    Serial.println(ot);
    Serial.print("Overtemp Warning: ");
    Serial.println(otpw);
    Serial.print("Temperature: ");
    if (ot) {
        Serial.println("✗ OVERTEMP SHUTDOWN!");
    } else if (otpw) {
        Serial.println("⚠ Warning - Getting hot");
    } else {
        Serial.println("✓ Normal");
    }
    
    // Current configuration
    Serial.println();
    Serial.println("--- Configuration ---");
    Serial.print("RMS Current: ");
    Serial.print(driver.rms_current());
    Serial.println(" mA");
    Serial.print("Microsteps: ");
    Serial.println(driver.microsteps());
    Serial.print("PWM Scale Sum: ");
    Serial.println(driver.PWM_SCALE());
    Serial.print("Mode: ");
    Serial.println(stealthChopEnabled ? "StealthChop" : "SpreadCycle");
    Serial.print("TOFF: ");
    Serial.println(driver.toff());
    Serial.print("Driver Enabled: ");
    Serial.println(enableState ? "Yes" : "No");
    
    Serial.println("========================================");
    Serial.println();
}

/**
 * Toggle driver enable state
 */
void toggleEnable() {
    enableState = !enableState;
    digitalWrite(ENABLE_PIN, enableState ? LOW : HIGH);
    
    if (enableState) {
        Serial.println("Driver ENABLED");
    } else {
        Serial.println("Driver DISABLED");
    }
}

/**
 * Reset and reinitialize the driver
 */
void resetDriver() {
    Serial.println("Resetting driver...");
    
    // Disable driver
    digitalWrite(ENABLE_PIN, HIGH);
    delay(100);
    
    // Re-enable driver
    digitalWrite(ENABLE_PIN, LOW);
    delay(100);
    
    // Reinitialize with default settings
    driver.begin();
    driver.toff(5);
    driver.rms_current(RMS_CURRENT);
    driver.microsteps(MICROSTEPS);
    driver.pwm_autoscale(true);
    driver.pwm_autograd(true);
    driver.en_spreadCycle(false);
    stealthChopEnabled = true;
    
    // Reset StallGuard and CoolStep
    driver.TCOOLTHRS(0xFFFFF);
    driver.SGTHRS(STALL_VALUE);
    driver.semin(5);
    driver.semax(2);
    driver.sedn(0b01);
    
    // Reset speed
    currentSpeed = DEFAULT_SPEED;
    enableState = true;
    
    Serial.println("Driver reset complete");
}

/**
 * Display help menu
 */
void printMenu() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("        TMC2209 Control Menu");
    Serial.println("========================================");
    Serial.println();
    Serial.println("Basic Movement:");
    Serial.println("  1 - Rotate clockwise (200 steps)");
    Serial.println("  2 - Rotate counter-clockwise (200 steps)");
    Serial.println("  3 - Continuous rotation CW");
    Serial.println("  4 - Continuous rotation CCW");
    Serial.println("  0 - Stop continuous rotation");
    Serial.println();
    Serial.println("Speed Control:");
    Serial.println("  5 - Increase speed (reduce delay)");
    Serial.println("  6 - Decrease speed (increase delay)");
    Serial.println();
    Serial.println("Driver Mode:");
    Serial.println("  7 - Toggle StealthChop/SpreadCycle");
    Serial.println();
    Serial.println("Configuration:");
    Serial.println("  9 - Change microstepping");
    Serial.println("  c - Change motor current");
    Serial.println("  e - Toggle enable/disable");
    Serial.println("  r - Reset driver");
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  s - Read StallGuard value");
    Serial.println("  d - Display full diagnostics");
    Serial.println("  h - Show this menu");
    Serial.println();
    Serial.println("System:");
    Serial.println("  x - Restart ESP32");
    Serial.println();
    Serial.println("========================================");
    Serial.println();
}
