/*
 * =============================================================================
 * DRIVER FACTORY - Runtime Motor Driver Creation
 * =============================================================================
 * 
 * Creates the appropriate motor driver based on hardware detection.
 * Uses GPIO pins to detect which driver is connected via jumper wires.
 * 
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include "IMotorDriver.h"

// Forward declarations (actual implementations in separate files)
class TMC2209Driver;
class TMC2208Driver;
class DCMotorDriver;

/**
 * @brief Factory class for creating motor drivers at runtime
 * 
 * The Factory Pattern allows creating the correct driver type
 * without hardcoding which driver is used. The high-level controller
 * only works with the IMotorDriver interface.
 * 
 * Detection is based on GPIO pins configured as inputs with pull-downs:
 *   - GPIO 10 & 13: Output HIGH (VCC source for jumpers)
 *   - GPIO 11: If HIGH → DC Motor
 *   - GPIO 12: If HIGH → TMC2208
 *   - Both LOW → TMC2209 (default)
 */
class DriverFactory {
public:
    /**
     * @brief Detect which hardware is connected
     * @return MotorType based on GPIO detection pins
     */
    static MotorType detectHardware();
    
    /**
     * @brief Create a driver instance for the specified type
     * @param type The motor type to create
     * @return Pointer to new IMotorDriver instance (caller owns memory)
     * @return nullptr if type is UNKNOWN
     */
    static IMotorDriver* createDriver(MotorType type);
    
    /**
     * @brief Detect hardware and create appropriate driver
     * @return Pointer to initialized IMotorDriver (caller owns memory)
     * @return nullptr if detection or init fails
     */
    static IMotorDriver* createAndInitDriver();
    
    /**
     * @brief Print detection pin states for debugging
     */
    static void printDetectionInfo();
    
private:
    static bool _detectionInitialized;
    
    /**
     * @brief Initialize detection GPIO pins
     */
    static void initDetectionPins();
};
