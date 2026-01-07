/*
 * =============================================================================
 * TMC2208 DRIVER - UART-Controlled Stepper Driver
 * =============================================================================
 * 
 * Implementation of IMotorDriver for the TMC2208 with UART control.
 * Uses the TMCStepper library (same as TMC2209).
 * 
 * Features:
 *   - UART configuration of all parameters
 *   - StealthChop2 silent operation
 *   - SpreadCycle for high-torque applications
 *   - Configurable current and microstepping
 *   - Fallback to Step/Dir-only mode if UART unavailable
 * 
 * Differences from TMC2209:
 *   - NO StallGuard4 (sensorless homing not available)
 *   - NO CoolStep (automatic current reduction)
 *   - Higher voltage range (up to 36V vs 29V)
 *   - Lower max current (2A peak vs 2.8A peak)
 * 
 * =============================================================================
 * WIRING CONFIGURATION:
 * =============================================================================
 *   ESP32 GPIO 1 (TX) ──[1kΩ]── ESP32 GPIO 2 (RX)
 *                                      │
 *             TMC2208 PDN_UART/RX pin ←┘
 *   
 *   TMC2208 TX pin = floating (not connected)
 *   
 *   Control Pins:
 *     GPIO 4  →  TMC2208 EN   (active LOW = enabled)
 *     GPIO 5  →  TMC2208 STEP (rising edge = 1 microstep)
 *     GPIO 6  →  TMC2208 DIR  (direction)
 *   
 *   Step/Dir Fallback Mode (when UART unavailable):
 *     MS1/MS2  →  Microstepping (set by jumpers/external pins)
 *     Vref     →  Current limit (set by potentiometer)
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include <TMCStepper.h>
#include "IMotorDriver.h"
#include "../config/PinConfig.h"
#include "MCPWMStepper.h"

/**
 * @brief TMC2208 driver implementation with UART control (fallback to Step/Dir)
 */
class TMC2208Driver : public IMotorDriver {
public:
    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
    
    /**
     * @brief Construct with default pins from PinConfig.h
     */
    TMC2208Driver();
    
    /**
     * @brief Construct with custom pins
     * @param serial Hardware serial port for UART
     * @param txPin ESP32 TX pin
     * @param rxPin ESP32 RX pin (connects to TMC2208 PDN_UART)
     * @param enPin Enable pin
     * @param stepPin Step pin
     * @param dirPin Direction pin
     * @param rSense Sense resistor value
     */
    TMC2208Driver(HardwareSerial* serial, uint8_t txPin, uint8_t rxPin,
                  uint8_t enPin, uint8_t stepPin, uint8_t dirPin,
                  float rSense = 0.11f);
    
    virtual ~TMC2208Driver();
    
    // =========================================================================
    // IMotorDriver Interface Implementation
    // =========================================================================
    
    bool init() override;
    MotorType getType() const override { return MotorType::STEPPER_TMC2208; }
    const char* getName() const override { return "TMC2208"; }
    
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
    bool isStalling() override { return false; }  // TMC2208 has no StallGuard
    void printDiagnostics() override;
    bool testConnection() override;
    
    // =========================================================================
    // TMC2208-Specific Methods
    // =========================================================================
    
    /**
     * @brief Enable/disable StealthChop (silent mode)
     * @param enable true for StealthChop, false for SpreadCycle
     */
    void setStealthChop(bool enable);
    
    /**
     * @brief Enable/disable PWM autoscale (automatic current reduction)
     * @param enable true = auto reduce current based on load, false = full current always
     */
    void setPWMAutoscale(bool enable);
    
    /**
     * @brief Switch to Step/Dir only mode (fallback when UART unavailable)
     * @param enabled true = Step/Dir only, false = use UART
     * 
     * In Step/Dir mode:
     * - Microstepping determined by MS1/MS2 pins (hardware)
     * - Current limit determined by Vref potentiometer (hardware)
     * - No runtime configuration changes possible
     */
    void setStepDirMode(bool enabled);
    
    /**
     * @brief Check if driver is in UART or Step/Dir mode
     * @return true if UART active, false if Step/Dir fallback
     */
    bool isUartMode() const { return _uartMode; }
    
    /**
     * @brief Reconfigure all settings via UART (useful after power reset)
     */
    void reconfigure();

private:
    // Hardware configuration
    HardwareSerial* _serial;
    uint8_t _txPin, _rxPin;
    uint8_t _enPin, _stepPin, _dirPin;
    float _rSense;
    
    // TMCStepper library driver object
    TMC2208Stepper* _driver;
    
    // MCPWM stepper for hardware PWM
    MCPWMStepper _mcpwmStepper;
    
    // UART mode flag (false = Step/Dir fallback)
    bool _uartMode;
    
    // Current state
    bool _enabled;
    int32_t _position;
    int32_t _targetPosition;
    bool _moving;
    uint32_t _lastFreqUpdate;  // Track last frequency update time
    
    // Configuration
    uint16_t _runCurrentMA;
    uint16_t _holdCurrentMA;
    uint16_t _microsteps;
    float _maxSpeed;
    AccelerationProfile _profile;
    
    // Motion state (for acceleration)
    float _currentSpeed;
    uint32_t _lastStepTime;
    uint32_t _stepInterval;
    
    // Trapezoidal/S-Curve motion planning
    int32_t _startPosition;
    int32_t _accelSteps;
    int32_t _decelSteps;
    int32_t _totalMoveSteps;
    bool _isTriangular;
    int8_t _moveDirection;
    
    // S-Curve specific
    int32_t _scurveSegmentEnd[7];
    float _scurveVelocity[8];
    float _scurveAccel[7];
    float _jerkSign[7];
    
    // Internal methods
    void configureDriver();  // UART configuration
    void updateHardwareFrequency();
    uint8_t microStepsToMRES(uint16_t ms);
    uint16_t mrestoMicrosteps(uint8_t mres);
    void planTrapezoidalMotion();
    void planSCurveMotion();
    void updateTrapezoidalSpeed();
    void updateSCurveSpeed();
};
