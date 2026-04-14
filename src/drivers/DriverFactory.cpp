/*
 * =============================================================================
 * DRIVER FACTORY - Implementation
 * =============================================================================
 */

#include "DriverFactory.h"
#include "../config/PinConfig.h"

// Include driver implementations
#include "TMC2209Driver.h"
#include "TMC2208Driver.h"
#include "DCMotorDriver.h"
#include "STSPIN220Driver.h"
#include "HarCoHBridgeDriver.h"

// Static member initialization
bool DriverFactory::_detectionInitialized = false;

void DriverFactory::initDetectionPins() {
    if (_detectionInitialized) return;
    
    // Set VCC source pins to output HIGH
    pinMode(DETECT_VCC_1, OUTPUT);
    pinMode(DETECT_VCC_2, OUTPUT);
    digitalWrite(DETECT_VCC_1, HIGH);
    digitalWrite(DETECT_VCC_2, HIGH);
    
    // Set detection input pins with internal pull-down
    pinMode(DETECT_BIT_0, INPUT_PULLDOWN);
    pinMode(DETECT_BIT_1, INPUT_PULLDOWN);
    
    // Allow pins to settle
    delay(10);
    
    _detectionInitialized = true;
}

MotorType DriverFactory::detectHardware() {
    initDetectionPins();
    
    bool bit0 = digitalRead(DETECT_BIT_0);  // DC motor flag
    bool bit1 = digitalRead(DETECT_BIT_1);  // TMC2208 flag
    
    // Decode detection bits
    //   !bit0 && !bit1 → STSPIN220 (default, no jumpers)
    //   bit0  && !bit1 → DC Motor (RZ7899)
    //   !bit0 && bit1  → HarCo H-Bridge (DRV88xx)
    //   bit0  && bit1  → TMC2209
    if (!bit0 && !bit1) {
        return MotorType::STEPPER_STSPIN220;  // Default: nothing connected
    }
    else if (bit0 && !bit1) {
        return MotorType::DC_MOTOR;
    }
    else if (!bit0 && bit1) {
        return MotorType::HARCO_HBRIDGE;
    }
    else {
        return MotorType::STEPPER_TMC2209;  // Both HIGH = TMC2209
    }
}

IMotorDriver* DriverFactory::createDriver(MotorType type) {
    switch (type) {
        case MotorType::STEPPER_TMC2209:
            return new TMC2209Driver();
            
        case MotorType::STEPPER_TMC2208:
            return new TMC2208Driver();
            
        case MotorType::DC_MOTOR:
            return new DCMotorDriver();
            
        case MotorType::STEPPER_STSPIN220:
            return new STSPIN220Driver();
            
        case MotorType::HARCO_HBRIDGE:
            return new HarCoHBridgeDriver();
            
        case MotorType::UNKNOWN:
        default:
            Serial.println("ERROR: Unknown motor type detected!");
            return nullptr;
    }
}

IMotorDriver* DriverFactory::createAndInitDriver() {
    // Detect hardware
    MotorType type = detectHardware();
    
    Serial.print("Detected driver type: ");
    Serial.println(motorTypeToString(type));
    
    // Create driver
    IMotorDriver* driver = createDriver(type);
    
    if (driver == nullptr) {
        Serial.println("ERROR: Failed to create driver!");
        return nullptr;
    }
    
    // Initialize driver
    if (!driver->init()) {
        Serial.println("ERROR: Failed to initialize driver!");
        delete driver;
        return nullptr;
    }
    
    Serial.print("Successfully initialized: ");
    Serial.println(driver->getName());
    
    return driver;
}

void DriverFactory::printDetectionInfo() {
    initDetectionPins();
    
    Serial.println("\n=== Hardware Detection ===");
    Serial.print("VCC Pin 1 (GPIO ");
    Serial.print(DETECT_VCC_1);
    Serial.print("): ");
    Serial.println(digitalRead(DETECT_VCC_1) ? "HIGH" : "LOW");
    
    Serial.print("VCC Pin 2 (GPIO ");
    Serial.print(DETECT_VCC_2);
    Serial.print("): ");
    Serial.println(digitalRead(DETECT_VCC_2) ? "HIGH" : "LOW");
    
    Serial.print("Detect Bit 0 (GPIO ");
    Serial.print(DETECT_BIT_0);
    Serial.print("): ");
    Serial.println(digitalRead(DETECT_BIT_0) ? "HIGH → DC Motor (or TMC2209 if both HIGH)" : "LOW");
    
    Serial.print("Detect Bit 1 (GPIO ");
    Serial.print(DETECT_BIT_1);
    Serial.print("): ");
    Serial.println(digitalRead(DETECT_BIT_1) ? "HIGH → HarCo H-Bridge (or TMC2209 if both HIGH)" : "LOW");
    
    Serial.print("\nDetected Type: ");
    Serial.println(motorTypeToString(detectHardware()));
    Serial.println("==========================\n");
}
