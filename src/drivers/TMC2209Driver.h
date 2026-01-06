/*
 * =============================================================================
 * TMC2209 DRIVER - UART-Controlled Stepper Driver
 * =============================================================================
 * 
 * Implementation of IMotorDriver for the TMC2209 with UART control.
 * Features:
 *   - UART configuration of all parameters
 *   - StealthChop silent operation
 *   - StallGuard load detection
 *   - Configurable current and microstepping
 * 
 * =============================================================================
 * WIRING CONFIGURATION:
 * =============================================================================
 *   ESP32 GPIO 1 (TX) ──[1kΩ]── ESP32 GPIO 2 (RX)
 *                                      │
 *             TMC2209 PDN_UART/RX pin ←┘
 *   
 *   TMC2209 TX pin = floating (not connected)
 *   
 *   Control Pins:
 *     GPIO 4  →  TMC2209 EN   (active LOW = enabled)
 *     GPIO 5  →  TMC2209 STEP (rising edge = 1 microstep)
 *     GPIO 6  →  TMC2209 DIR  (direction)
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include <TMCStepper.h>
#include "IMotorDriver.h"
#include "../config/PinConfig.h"

/**
 * @brief TMC2209 driver implementation with UART control
 */
class TMC2209Driver : public IMotorDriver {
public:
    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
    
    /**
     * @brief Construct with default pins from PinConfig.h
     */
    TMC2209Driver();
    
    /**
     * @brief Construct with custom pins
     * @param serial Hardware serial port for UART
     * @param txPin ESP32 TX pin
     * @param rxPin ESP32 RX pin (connects to TMC2209 PDN_UART)
     * @param enPin Enable pin
     * @param stepPin Step pin
     * @param dirPin Direction pin
     * @param address Driver address (0-3 based on MS1/MS2)
     * @param rSense Sense resistor value
     */
    TMC2209Driver(HardwareSerial* serial, uint8_t txPin, uint8_t rxPin,
                  uint8_t enPin, uint8_t stepPin, uint8_t dirPin,
                  uint8_t address = 0, float rSense = 0.11f);
    
    virtual ~TMC2209Driver();
    
    // =========================================================================
    // IMotorDriver Interface Implementation
    // =========================================================================
    
    bool init() override;
    MotorType getType() const override { return MotorType::STEPPER_TMC2209; }
    const char* getName() const override { return "TMC2209"; }
    
    void enable() override;
    void disable() override;
    bool isEnabled() const override;
    
    void move(int32_t steps) override;
    void moveTo(int32_t position) override;
    void stop() override;
    void emergencyStop() override;
    bool isMoving() const override;
    void update() override;
    
    void setMaxSpeed(float stepsPerSecond) override;
    void setCurrent(uint16_t runMA, uint16_t holdMA = 0) override;
    void setMicrosteps(uint16_t microsteps) override;
    void setAccelerationProfile(const AccelerationProfile& profile) override;
    
    int32_t getPosition() const override;
    void setPosition(int32_t position) override;
    void home(int8_t direction = -1) override;
    
    MotorStatus getStatus() override;
    bool isStalling() override;
    void printDiagnostics() override;
    bool testConnection() override;
    
    // =========================================================================
    // TMC2209-Specific Methods
    // =========================================================================
    
    /**
     * @brief Set StallGuard threshold
     * @param threshold 0-255 (higher = more sensitive)
     */
    void setStallThreshold(uint8_t threshold);
    
    /**
     * @brief Get raw StallGuard result
     * @return StallGuard value 0-510
     */
    uint16_t getStallGuardResult();
    
    /**
     * @brief Enable/disable StealthChop (silent mode)
     * @param enable true for StealthChop, false for SpreadCycle
     */
    void setStealthChop(bool enable);
    
    /**
     * @brief Scan for TMC2209 drivers on all 4 addresses
     */
    void scanAddresses();
    
    /**
     * @brief Read raw register value
     * @param reg Register address
     * @return Register value
     */
    uint32_t readRegister(uint8_t reg);
    
    /**
     * @brief Reconfigure all settings (useful after power reset)
     */
    void reconfigure();
    
private:
    // Hardware configuration
    HardwareSerial* _serial;
    uint8_t _txPin, _rxPin;
    uint8_t _enPin, _stepPin, _dirPin;
    uint8_t _address;
    float _rSense;
    
    // TMCStepper library driver object
    TMC2209Stepper* _driver;
    
    // Current state
    bool _enabled;
    int32_t _position;
    int32_t _targetPosition;
    bool _moving;
    
    // Configuration
    uint16_t _runCurrentMA;
    uint16_t _holdCurrentMA;
    uint16_t _microsteps;
    float _maxSpeed;
    AccelerationProfile _profile;
    
    // Motion state (for acceleration)
    float _currentSpeed;
    uint32_t _lastStepTime;
    uint32_t _stepInterval;  // microseconds between steps
    
    // StallGuard
    uint8_t _stallThreshold;
    
    // Internal methods
    void configureDriver();
    void doStep();
    void calculateStepInterval();
    uint8_t microStepsToMRES(uint16_t ms);
    uint16_t mrestoMicrosteps(uint8_t mres);
};
