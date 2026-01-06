/*
 * =============================================================================
 * TMC2209 UART Diagnostic Tool - Minimal Test Script
 * =============================================================================
 * 
 * Purpose: Isolate and debug TMC2209 UART communication issues.
 * This script performs raw communication tests and compares expected vs actual states.
 * 
 * =============================================================================
 * CONFIRMED WORKING WIRING:
 * =============================================================================
 *   GPIO 1 (TX_PIN) ──[1kΩ]── GPIO 2 (RX_PIN)
 *                                   │
 *          TMC2209 PDN_UART/RX pin ←┘
 *   
 *   TMC2209 TX pin = NOT CONNECTED (floating)
 *   
 *   Control Pins:
 *     ESP32-S3 GPIO 4  →  TMC2209 EN   (Enable, active LOW)
 *     ESP32-S3 GPIO 5  →  TMC2209 STEP (Step pulses)
 *     ESP32-S3 GPIO 6  →  TMC2209 DIR  (Direction)
 *   
 *   Power:
 *     ESP32-S3 3.3V    →  TMC2209 VIO
 *     ESP32-S3 GND     →  TMC2209 GND (must be common!)
 *     12-28V           →  TMC2209 VS
 * =============================================================================
 */

#include <Arduino.h>
#include <TMCStepper.h>

// =============================================================================
// PIN DEFINITIONS - Confirmed working configuration
// =============================================================================
#define TX_PIN          1        // ESP32 UART TX (connects through resistor)
#define RX_PIN          2        // ESP32 UART RX (connects to TMC2209 PDN_UART/RX)
#define ENABLE_PIN      4        // TMC2209 EN (active LOW = enabled)
#define STEP_PIN        5        // TMC2209 STEP
#define DIR_PIN         6        // TMC2209 DIR
#define SERIAL_PORT     Serial1  // Hardware UART1

// =============================================================================
// TMC2209 CONFIGURATION
// =============================================================================
#define DRIVER_ADDRESS  0b00     // MS1=OPEN, MS2=OPEN → address 0
#define R_SENSE         0.11f    // TMC2209 v1.3 sense resistor

// Expected default values (what we'll try to configure)
#define EXPECTED_CURRENT_MA     800
#define EXPECTED_MICROSTEPS     16

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================
TMC2209Stepper driver(&SERIAL_PORT, R_SENSE, DRIVER_ADDRESS);

// Variables for user input
int targetCurrent = EXPECTED_CURRENT_MA;
int targetMicrosteps = EXPECTED_MICROSTEPS;
bool enExpectedLow = true;  // Track expected EN state (true = LOW/enabled)

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================
void printSeparator();
void printHeader();
void testUartConnection();
void readRawRegisters();
void showExpectedVsActual();
void changeSettings();
void printHelp();
void stepTest();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    // Initialize USB Serial for debug output
    Serial.begin(115200);
    
    // Wait for serial with timeout
    unsigned long start = millis();
    while (!Serial && (millis() - start < 3000)) {
        delay(10);
    }
    delay(500);  // Extra settle time
    
    printHeader();
    
    // Initialize GPIO pins FIRST (before UART)
    Serial.println("\n[1] Configuring GPIO pins...");
    pinMode(ENABLE_PIN, OUTPUT);
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    
    digitalWrite(ENABLE_PIN, LOW);   // Enable driver (active LOW)
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN, LOW);
    Serial.println("    EN=LOW (enabled), STEP=LOW, DIR=LOW");
    
    // Initialize UART
    Serial.println("\n[2] Initializing UART...");
    Serial.print("    TX_PIN (GPIO ");
    Serial.print(TX_PIN);
    Serial.print(") → through 1kΩ resistor → RX_PIN (GPIO ");
    Serial.print(RX_PIN);
    Serial.println(")");
    Serial.print("    RX_PIN (GPIO ");
    Serial.print(RX_PIN);
    Serial.println(") → directly to TMC2209 PDN_UART/RX");
    
    SERIAL_PORT.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(100);
    Serial.println("    UART initialized at 115200 baud");
    
    // Initialize TMCStepper library
    Serial.println("\n[3] Initializing TMCStepper driver object...");
    driver.begin();
    delay(50);
    
    // Attempt basic configuration
    Serial.println("\n[4] Attempting basic driver configuration...");
    driver.toff(5);                  // Enable driver (required!)
    driver.rms_current(targetCurrent);
    driver.microsteps(targetMicrosteps);
    driver.en_spreadCycle(false);    // StealthChop mode
    driver.pwm_autoscale(true);
    driver.pdn_disable(true);        // Use UART, not PDN for enable
    driver.mstep_reg_select(true);   // Microstep via UART
    
    Serial.println("    Sent configuration commands");
    delay(100);
    
    // Run initial tests
    Serial.println("\n[5] Running initial connection test...\n");
    testUartConnection();
    
    printSeparator();
    printHelp();
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        
        // Flush remaining chars
        while (Serial.available()) Serial.read();
        
        switch (cmd) {
            case 't': case 'T':
                testUartConnection();
                break;
                
            case 'r': case 'R':
                readRawRegisters();
                break;
                
            case 's': case 'S':
                showExpectedVsActual();
                break;
                
            case 'c': case 'C':
                changeSettings();
                break;
                
            case 'm': case 'M':
                stepTest();
                break;
                
            case 'e': case 'E':
                // Toggle enable
                {
                    enExpectedLow = !enExpectedLow;
                    digitalWrite(ENABLE_PIN, enExpectedLow ? LOW : HIGH);
                    Serial.print("EN pin = ");
                    Serial.println(enExpectedLow ? "LOW (enabled)" : "HIGH (disabled)");
                }
                break;
                
            case 'h': case 'H': case '?':
                printHelp();
                break;
                
            case 'x': case 'X':
                Serial.println("Restarting ESP32...");
                delay(100);
                ESP.restart();
                break;
                
            case 'i': case 'I':
                // Reconfigure TMC2209 (useful after power reset)
                {
                    Serial.println("Reconfiguring TMC2209...");
                    driver.begin();
                    delay(50);
                    driver.toff(5);
                    driver.rms_current(targetCurrent);
                    // Microsteps: use direct CHOPCONF write for fullstep support
                    uint8_t mres;
                    switch(targetMicrosteps) {
                        case 1:   mres = 8; break;
                        case 2:   mres = 7; break;
                        case 4:   mres = 6; break;
                        case 8:   mres = 5; break;
                        case 16:  mres = 4; break;
                        case 32:  mres = 3; break;
                        case 64:  mres = 2; break;
                        case 128: mres = 1; break;
                        case 256: mres = 0; break;
                        default:  mres = 4; break; // 16
                    }
                    uint32_t chopconf = driver.CHOPCONF();
                    chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
                    driver.CHOPCONF(chopconf);
                    driver.en_spreadCycle(false);
                    driver.pwm_autoscale(true);
                    driver.pdn_disable(true);
                    driver.mstep_reg_select(true);
                    delay(50);
                    Serial.print("  Current: "); Serial.print(targetCurrent); Serial.println(" mA");
                    Serial.print("  Microsteps: "); Serial.println(targetMicrosteps);
                    Serial.println("Done! Use 's' to verify.");
                }
                break;
                
            default:
                if (cmd >= 32) {  // Printable character
                    Serial.print("Unknown command: ");
                    Serial.println(cmd);
                    printHelp();
                }
                break;
        }
    }
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

void printSeparator() {
    Serial.println("═══════════════════════════════════════════════════════════════════");
}

void printHeader() {
    printSeparator();
    Serial.println("          TMC2209 UART DIAGNOSTIC TOOL");
    Serial.println("          Minimal Test Script for Debugging");
    printSeparator();
}

void printHelp() {
    Serial.println();
    Serial.println("┌─────────────────────────────────────────────────────────────────┐");
    Serial.println("│                     AVAILABLE COMMANDS                          │");
    Serial.println("├─────────────────────────────────────────────────────────────────┤");
    Serial.println("│  t - Test UART connection (multiple methods)                    │");
    Serial.println("│  r - Read raw TMC2209 registers                                 │");
    Serial.println("│  s - Show Expected vs Actual configuration                      │");
    Serial.println("│  c - Change current/microstepping settings                      │");
    Serial.println("│  i - (Re)Initialize TMC2209 with target settings                │");
    Serial.println("│  m - Motor step test (10 steps)                                 │");
    Serial.println("│  e - Toggle EN pin (enable/disable)                             │");
    Serial.println("│  h - Show this help                                             │");
    Serial.println("│  x - Restart ESP32                                              │");
    Serial.println("└─────────────────────────────────────────────────────────────────┘");
    Serial.println();
}

void testUartConnection() {
    printSeparator();
    Serial.println("UART CONNECTION TEST");
    printSeparator();
    
    // Method 1: test_connection()
    Serial.println("\n[Method 1] TMCStepper test_connection():");
    uint8_t result = driver.test_connection();
    Serial.print("  Result: ");
    switch (result) {
        case 0:
            Serial.println("✓ OK (0) - Connection successful!");
            break;
        case 1:
            Serial.println("✗ FAIL (1) - No reply / invalid response");
            break;
        case 2:
            Serial.println("✗ FAIL (2) - Read returned 0x00000000 or 0xFFFFFFFF");
            break;
        default:
            Serial.print("? Unknown (");
            Serial.print(result);
            Serial.println(")");
            break;
    }
    
    // Method 2: Read IOIN register (has chip version info)
    Serial.println("\n[Method 2] Read IOIN register (contains DRV_STATUS and version):");
    uint32_t ioin = driver.IOIN();
    Serial.print("  IOIN = 0x");
    if (ioin < 0x10000000) Serial.print("0");
    if (ioin < 0x1000000) Serial.print("0");
    if (ioin < 0x100000) Serial.print("0");
    if (ioin < 0x10000) Serial.print("0");
    if (ioin < 0x1000) Serial.print("0");
    if (ioin < 0x100) Serial.print("0");
    if (ioin < 0x10) Serial.print("0");
    Serial.println(ioin, HEX);
    
    if (ioin == 0x00000000) {
        Serial.println("  ⚠ All zeros - likely no response from TMC2209");
    } else if (ioin == 0xFFFFFFFF) {
        Serial.println("  ⚠ All ones - likely UART wiring issue or no power");
    } else {
        uint8_t version = (ioin >> 24) & 0xFF;
        Serial.print("  Version field: 0x");
        Serial.print(version, HEX);
        if (version == 0x21) {
            Serial.println(" ✓ (TMC2209)");
        } else {
            Serial.println(" (expected 0x21 for TMC2209)");
        }
        
        // Decode IOIN bits
        Serial.println("  IOIN Bit fields:");
        Serial.print("    ENN      = "); Serial.println((ioin >> 0) & 1);
        Serial.print("    MS1      = "); Serial.println((ioin >> 2) & 1);
        Serial.print("    MS2      = "); Serial.println((ioin >> 3) & 1);
        Serial.print("    DIAG     = "); Serial.println((ioin >> 4) & 1);
        Serial.print("    PDN_UART = "); Serial.println((ioin >> 6) & 1);
        Serial.print("    STEP     = "); Serial.println((ioin >> 7) & 1);
        Serial.print("    DIR      = "); Serial.println((ioin >> 9) & 1);
    }
    
    // Method 3: Read GCONF register
    Serial.println("\n[Method 3] Read GCONF register:");
    uint32_t gconf = driver.GCONF();
    Serial.print("  GCONF = 0x");
    Serial.println(gconf, HEX);
    
    // Method 4: Read DRV_STATUS register
    Serial.println("\n[Method 4] Read DRV_STATUS register:");
    uint32_t drv_status = driver.DRV_STATUS();
    Serial.print("  DRV_STATUS = 0x");
    Serial.println(drv_status, HEX);
    
    if (drv_status == 0x00000000) {
        Serial.println("  ⚠ All zeros - motor likely not powered or EN=HIGH");
    } else if (drv_status == 0xFFFFFFFF) {
        Serial.println("  ⚠ All ones - UART communication problem");
    }
    
    // Summary
    Serial.println("\n[Summary]");
    if (ioin != 0x00000000 && ioin != 0xFFFFFFFF && 
        gconf != 0xFFFFFFFF && drv_status != 0xFFFFFFFF) {
        Serial.println("  ✓ TMC2209 is responding to UART commands");
    } else {
        Serial.println("  ✗ TMC2209 not responding properly");
        Serial.println("\n  Troubleshooting:");
        Serial.println("  1. Check VIO has 3.3V from ESP32");
        Serial.println("  2. Check GND is shared between ESP32 and TMC2209");
        Serial.println("  3. Verify 1kΩ resistor between GPIO 1 and GPIO 2");
        Serial.println("  4. Verify GPIO 2 connects to TMC2209 PDN_UART/RX");
        Serial.println("  5. TMC2209 TX pin should be floating (not connected)");
        Serial.println("  6. MS1 and MS2 should be floating (for address 0)");
    }
    Serial.println();
}

void readRawRegisters() {
    printSeparator();
    Serial.println("RAW REGISTER DUMP");
    printSeparator();
    
    Serial.println("\nReading key TMC2209 registers...\n");
    
    struct RegInfo {
        const char* name;
        uint32_t value;
    };
    
    RegInfo regs[] = {
        {"GCONF      ", driver.GCONF()},
        {"GSTAT      ", driver.GSTAT()},
        {"IOIN       ", driver.IOIN()},
        {"IHOLD_IRUN ", driver.IHOLD_IRUN()},
        {"TSTEP      ", driver.TSTEP()},
        {"MSCNT      ", driver.MSCNT()},
        {"CHOPCONF   ", driver.CHOPCONF()},
        {"DRV_STATUS ", driver.DRV_STATUS()},
        {"PWMCONF    ", driver.PWMCONF()},
        {"PWM_SCALE  ", driver.PWM_SCALE()},
        {"SG_RESULT  ", driver.SG_RESULT()},
    };
    
    Serial.println("Register       Hex Value    Decimal     Binary (LSB 8 bits)");
    Serial.println("─────────────────────────────────────────────────────────────");
    
    for (auto& reg : regs) {
        Serial.print(reg.name);
        Serial.print("  0x");
        if (reg.value < 0x10000000) Serial.print("0");
        if (reg.value < 0x1000000) Serial.print("0");
        if (reg.value < 0x100000) Serial.print("0");
        if (reg.value < 0x10000) Serial.print("0");
        if (reg.value < 0x1000) Serial.print("0");
        if (reg.value < 0x100) Serial.print("0");
        if (reg.value < 0x10) Serial.print("0");
        Serial.print(reg.value, HEX);
        
        Serial.print("  ");
        // Right-align decimal
        if (reg.value < 10) Serial.print("         ");
        else if (reg.value < 100) Serial.print("        ");
        else if (reg.value < 1000) Serial.print("       ");
        else if (reg.value < 10000) Serial.print("      ");
        else if (reg.value < 100000) Serial.print("     ");
        else if (reg.value < 1000000) Serial.print("    ");
        else if (reg.value < 10000000) Serial.print("   ");
        else if (reg.value < 100000000) Serial.print("  ");
        else if (reg.value < 1000000000) Serial.print(" ");
        Serial.print(reg.value, DEC);
        
        Serial.print("   ");
        // Binary LSB 8 bits
        for (int i = 7; i >= 0; i--) {
            Serial.print((reg.value >> i) & 1);
        }
        
        // Flag suspicious values
        if (reg.value == 0x00000000) Serial.print(" ⚠ zeros");
        if (reg.value == 0xFFFFFFFF) Serial.print(" ⚠ all-ones");
        
        Serial.println();
    }
    Serial.println();
}

void showExpectedVsActual() {
    printSeparator();
    Serial.println("EXPECTED vs ACTUAL CONFIGURATION");
    printSeparator();
    
    Serial.println("\n┌────────────────────┬───────────────┬───────────────┬─────────┐");
    Serial.println("│ Parameter          │ Expected      │ Actual        │ Match   │");
    Serial.println("├────────────────────┼───────────────┼───────────────┼─────────┤");
    
    // RMS Current
    uint16_t actualCurrent = driver.rms_current();
    Serial.print("│ RMS Current (mA)   │ ");
    Serial.print(targetCurrent);
    if (targetCurrent < 1000) Serial.print(" ");
    if (targetCurrent < 100) Serial.print(" ");
    Serial.print("          │ ");
    Serial.print(actualCurrent);
    if (actualCurrent < 1000) Serial.print(" ");
    if (actualCurrent < 100) Serial.print(" ");
    Serial.print("          │ ");
    Serial.println(abs((int)actualCurrent - targetCurrent) < 50 ? "  ✓    │" : "  ✗    │");
    
    // Microsteps - Read MRES directly from CHOPCONF for accurate value
    uint32_t chopconf = driver.CHOPCONF();
    uint8_t mres_raw = (chopconf >> 24) & 0x0F;
    uint16_t actualMicrosteps = (mres_raw >= 8) ? 1 : (256 >> mres_raw);
    bool mstepRegSelect = driver.mstep_reg_select();
    Serial.print("│ Microsteps         │ ");
    Serial.print(targetMicrosteps);
    if (targetMicrosteps < 100) Serial.print(" ");
    if (targetMicrosteps < 10) Serial.print(" ");
    Serial.print("           │ ");
    Serial.print(actualMicrosteps);
    if (actualMicrosteps < 100) Serial.print(" ");
    if (actualMicrosteps < 10) Serial.print(" ");
    Serial.print("           │ ");
    Serial.println(actualMicrosteps == targetMicrosteps ? "  ✓    │" : "  ✗    │");
    
    // Show mstep_reg_select status (important for UART microstep control)
    Serial.print("│ mstep_reg_select   │ 1 (UART)      │ ");
    Serial.print(mstepRegSelect ? "1 (UART)     " : "0 (MS pins)  ");
    Serial.print(" │ ");
    Serial.println(mstepRegSelect ? "  ✓    │" : "  ✗    │");
    
    // TOFF (must be > 0 for driver to work)
    uint8_t actualToff = driver.toff();
    Serial.print("│ TOFF (1-15 = on)   │ 5             │ ");
    Serial.print(actualToff);
    if (actualToff < 10) Serial.print(" ");
    Serial.print("            │ ");
    Serial.println(actualToff > 0 ? "  ✓    │" : "  ✗    │");
    
    // StealthChop mode
    bool spreadCycle = driver.en_spreadCycle();
    Serial.print("│ StealthChop        │ ON            │ ");
    Serial.print(!spreadCycle ? "ON           " : "OFF          ");
    Serial.print(" │ ");
    Serial.println(!spreadCycle ? "  ✓    │" : "  ✗    │");
    
    // PDN Disable
    bool pdnDisable = driver.pdn_disable();
    Serial.print("│ PDN_DISABLE        │ 1 (UART)      │ ");
    Serial.print(pdnDisable ? "1 (UART)     " : "0 (PDN)      ");
    Serial.print(" │ ");
    Serial.println(pdnDisable ? "  ✓    │" : "  ✗    │");
    
    // EN Pin state - compare against expected (tracked by global enExpectedLow)
    int enState = digitalRead(ENABLE_PIN);
    bool enActualLow = (enState == LOW);
    bool enMatch = (enActualLow == enExpectedLow);
    
    Serial.print("│ EN Pin             │ ");
    Serial.print(enExpectedLow ? "LOW (enabled)" : "HIGH (disab)");
    Serial.print(" │ ");
    Serial.print(enActualLow ? "LOW (enabled)" : "HIGH (disab)");
    Serial.print(" │ ");
    Serial.println(enMatch ? "  ✓    │" : "  ✗    │");
    
    // Also show what TMC2209 sees via IOIN.ENN
    uint32_t ioin = driver.IOIN();
    bool tmcSeesEnLow = !((ioin >> 0) & 1);  // ENN bit: 0 = LOW (enabled)
    Serial.print("│ IOIN.ENN (TMC sees)│ ");
    Serial.print(enExpectedLow ? "0 (enabled) " : "1 (disabled)");
    Serial.print("  │ ");
    Serial.print(tmcSeesEnLow ? "0 (enabled) " : "1 (disabled)");
    Serial.print("  │ ");
    Serial.println((tmcSeesEnLow == enExpectedLow) ? "  ✓    │" : "  ✗    │");
    
    Serial.println("└────────────────────┴───────────────┴───────────────┴─────────┘");
    
    // DRV_STATUS decode
    Serial.println("\n[DRV_STATUS Flags]");
    uint32_t status = driver.DRV_STATUS();
    Serial.print("  Raw: 0x");
    Serial.println(status, HEX);
    
    Serial.print("  stst (standstill)     : "); Serial.println((status >> 31) & 1 ? "Yes" : "No");
    Serial.print("  olb (open load B)     : "); Serial.println((status >> 30) & 1 ? "Yes ⚠" : "No");
    Serial.print("  ola (open load A)     : "); Serial.println((status >> 29) & 1 ? "Yes ⚠" : "No");
    Serial.print("  s2gb (short to GND B) : "); Serial.println((status >> 28) & 1 ? "Yes ⚠" : "No");
    Serial.print("  s2ga (short to GND A) : "); Serial.println((status >> 27) & 1 ? "Yes ⚠" : "No");
    Serial.print("  otpw (over-temp warn) : "); Serial.println((status >> 26) & 1 ? "Yes ⚠" : "No");
    Serial.print("  ot (over-temp)        : "); Serial.println((status >> 25) & 1 ? "Yes ⚠" : "No");
    Serial.print("  t120 (temp > 120°C)   : "); Serial.println((status >> 0) & 1 ? "Yes ⚠" : "No");
    Serial.print("  t143 (temp > 143°C)   : "); Serial.println((status >> 1) & 1 ? "Yes ⚠" : "No");
    Serial.print("  t150 (temp > 150°C)   : "); Serial.println((status >> 2) & 1 ? "Yes ⚠" : "No");
    Serial.print("  t157 (temp > 157°C)   : "); Serial.println((status >> 3) & 1 ? "Yes ⚠" : "No");
    
    Serial.println();
}

void changeSettings() {
    Serial.println("\nChange settings:");
    Serial.println("  1. Change current (enter: c1000 for 1000mA)");
    Serial.println("  2. Change microsteps (enter: m32 for 32 microsteps)");
    Serial.print("\nEnter setting (e.g., 'c800' or 'm16'): ");
    
    // Wait for input
    while (!Serial.available()) delay(10);
    
    String input = Serial.readStringUntil('\n');
    input.trim();
    Serial.println(input);
    
    if (input.length() < 2) {
        Serial.println("Invalid input");
        return;
    }
    
    char type = input.charAt(0);
    int value = input.substring(1).toInt();
    
    if (type == 'c' || type == 'C') {
        if (value >= 100 && value <= 2000) {
            targetCurrent = value;
            driver.rms_current(targetCurrent);
            Serial.print("Set RMS current to ");
            Serial.print(targetCurrent);
            Serial.println(" mA");
            
            // Verify
            delay(50);
            Serial.print("Read back: ");
            Serial.print(driver.rms_current());
            Serial.println(" mA");
        } else {
            Serial.println("Current must be 100-2000 mA");
        }
    } else if (type == 'm' || type == 'M') {
        if (value == 1 || value == 2 || value == 4 || value == 8 || 
            value == 16 || value == 32 || value == 64 || value == 128 || value == 256) {
            targetMicrosteps = value;
            
            // Ensure UART controls microsteps (not MS1/MS2 pins)
            driver.mstep_reg_select(true);
            delay(10);
            
            // Calculate MRES value: MRES = log2(256/microsteps)
            // MRES=0 → 256, MRES=4 → 16, MRES=8 → 1 (fullstep)
            uint8_t mres;
            switch(value) {
                case 256: mres = 0; break;
                case 128: mres = 1; break;
                case 64:  mres = 2; break;
                case 32:  mres = 3; break;
                case 16:  mres = 4; break;
                case 8:   mres = 5; break;
                case 4:   mres = 6; break;
                case 2:   mres = 7; break;
                case 1:   mres = 8; break;  // Fullstep - library doesn't support this!
                default:  mres = 4; break;
            }
            
            // Write MRES directly to CHOPCONF (bits 24-27)
            // This bypasses TMCStepper library bug that rejects MRES=8
            uint32_t chopconf = driver.CHOPCONF();
            chopconf = (chopconf & 0xF0FFFFFF) | ((uint32_t)mres << 24);
            driver.CHOPCONF(chopconf);
            delay(10);
            
            // Verify
            uint8_t actual_mres = (driver.CHOPCONF() >> 24) & 0x0F;
            uint16_t actual_microsteps = (actual_mres >= 8) ? 1 : (256 >> actual_mres);
            
            Serial.print("Microsteps set to ");
            Serial.print(actual_microsteps);
            Serial.print(" (MRES=");
            Serial.print(actual_mres);
            Serial.println(")");
        } else {
            Serial.println("Microsteps must be: 1, 2, 4, 8, 16, 32, 64, 128, or 256");
        }
    } else {
        Serial.println("Unknown setting type");
    }
    Serial.println();
}

void stepTest() {
    Serial.println("\nManual step test...");
    Serial.print("  EN pin: ");
    Serial.println(digitalRead(ENABLE_PIN) == LOW ? "LOW (enabled)" : "HIGH (disabled)");
    
    // Ensure enabled
    digitalWrite(ENABLE_PIN, LOW);
    delay(10);
    
    Serial.println("  Sending 100 step pulses (400µs each)...");
    
    for (int i = 0; i < 100; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(2000);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(2000);
        Serial.print(".");
    }
    Serial.println(" Done!");
    
    // Read MSCNT to see if microstep counter changed
    delay(10);
    uint16_t mscnt = driver.MSCNT();
    Serial.print("  MSCNT (microstep counter): ");
    Serial.println(mscnt);
    Serial.println("  (Should change if steps were received by TMC2209)");
    Serial.println();
}
