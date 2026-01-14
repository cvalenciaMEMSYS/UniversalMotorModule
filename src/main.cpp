/*
 * =============================================================================
 * UNIVERSAL MOTOR MODULE - Main Entry Point
 * =============================================================================
 * 
 * A modular motor control system that automatically detects and initializes
 * the appropriate motor driver based on hardware configuration.
 * 
 * Supported Drivers:
 *   - TMC2209: UART-controlled stepper with StealthChop and StallGuard
 *   - TMC2208: Step/Dir stepper (standalone mode)
 *   - DC Motor: RZ7899 H-bridge with PWM speed control
 * 
 * Hardware Detection:
 *   GPIO 10, 13 = VCC source (output HIGH)
 *   GPIO 11, 12 = Detection inputs (internal pull-down)
 *   
 *   Jumper Configuration:
 *     No jumper        → TMC2209 (default)
 *     11 to VCC        → DC Motor
 *     12 to VCC        → TMC2208
 * 
 * Serial Commands (115200 baud):
 *   move <steps>       - Relative move
 *   abs <position>     - Absolute move
 *   set speed <val>    - Set max speed
 *   set current <mA>   - Set motor current
 *   enable / disable   - Control motor driver
 *   stop               - Emergency stop
 *   ? or status        - Show status
 *   help               - Show all commands
 * 
 * =============================================================================
 */

#include <Arduino.h>
#include "core/MotorController.h"
#include "core/StatusLED.h"
#include "config/PinConfig.h"
#include "drivers/IMotorDriver.h"  // For MotorType enum

// Global motor controller instance
MotorController controller;

// Input buffer for serial commands
String inputBuffer = "";
bool inputComplete = false;
uint32_t lastCharTime = 0;  // For timeout-based command detection
uint32_t lastSerialActivity = 0;  // Track actual serial data reception

// Idle timeout for LED
uint32_t lastActivityTime = 0;
constexpr uint32_t IDLE_TIMEOUT_MS = 5000;  // 5 seconds to idle

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    // Initialize status LED first (shows we're alive)
    statusLED.begin();
    statusLED.setStatus(SystemStatus::INITIALIZING);
    
    // Initialize USB Serial
    Serial.begin(USB_SERIAL_BAUD);
    
    // Wait for serial connection with timeout
    unsigned long startTime = millis();
    while (!Serial && (millis() - startTime) < SERIAL_TIMEOUT_MS) {
        statusLED.update();
        delay(10);
    }
    delay(500);  // Extra settle time
    
    // Initialize motor controller
    // This will auto-detect hardware and create the appropriate driver
    if (!controller.begin()) {
        Serial.println("\n\n!!! MOTOR CONTROLLER INITIALIZATION FAILED !!!");
        Serial.println("Check hardware connections and try again.\n");
        statusLED.setStatus(SystemStatus::ERROR);
        // Continue anyway to allow diagnostic commands
    } else {
        // Set LED driver type based on detected hardware
        IMotorDriver* driver = controller.getDriver();
        if (driver) {
            statusLED.setDriverType(motorTypeToDriverType(driver->getType()));
        }
        
        // Small delay to ensure RMT/NeoPixel peripheral is ready
        delay(50);
        
        statusLED.setStatus(SystemStatus::READY);
        // Force multiple updates to ensure LED is set
        for (int i = 0; i < 3; i++) {
            statusLED.update();
            delay(10);
        }
        lastActivityTime = millis();
    }
}

// =============================================================================
// MAIN LOOP
// =============================================================================

void loop() {
    // Update motor controller first (never block this, handles ongoing motion)
    controller.update();
    
    // Process serial input - Serial.available() is safe even if USB disconnected
    while (Serial.available()) {
        char c = Serial.read();
        lastSerialActivity = millis();  // Track actual data reception
        
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                inputComplete = true;
            }
        } else if (c >= 32 && c < 127) {
            // Only add printable ASCII characters
            inputBuffer += c;
            lastCharTime = millis();  // Track when last char was received
        }
    }
    
    // Timeout-based command completion (if no newline received within 100ms)
    if (inputBuffer.length() > 0 && !inputComplete && (millis() - lastCharTime > 100)) {
        inputComplete = true;
    }
    
    // Process complete command
    if (inputComplete) {
        // Reset activity timer BEFORE processing
        lastActivityTime = millis();
        
        // Echo the received command
        Serial.print("> ");
        Serial.println(inputBuffer);
        
        // Flash orange to indicate command received
        statusLED.flashCommandReceived();
        
        // Handle reboot command directly
        if (inputBuffer.equalsIgnoreCase("reboot") || inputBuffer.equalsIgnoreCase("restart")) {
            Serial.println("Rebooting...");
            statusLED.playRebootAnimation();  // Rainbow sweep then fade
            ESP.restart();
        } else {
            controller.processCommand(inputBuffer);
        }
        
        inputBuffer = "";
        inputComplete = false;
    }
    
    // NOW update LED status based on motor state
    SystemStatus currentLedStatus = statusLED.getStatus();
    SystemStatus targetStatus = SystemStatus::READY;  // Default
    
    // Priority: ERROR > MOVING > IDLE > READY
    if (controller.hasError()) {
        targetStatus = SystemStatus::ERROR;
    } else if (controller.isBusy()) {
        targetStatus = SystemStatus::MOVING;
        lastActivityTime = millis();
    } else if (!controller.isReady()) {
        targetStatus = SystemStatus::ERROR;
    } else if (millis() - lastActivityTime > IDLE_TIMEOUT_MS) {
        targetStatus = SystemStatus::IDLE;
    } else {
        targetStatus = SystemStatus::READY;
    }
    
    // Only update status if it changed
    if (currentLedStatus != targetStatus) {
        statusLED.setStatus(targetStatus);
    }
    
    // Update status LED (handles flash timing, blink, pulse)
    statusLED.update();
}
