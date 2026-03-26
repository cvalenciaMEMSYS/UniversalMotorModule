/*
 * =============================================================================
 * STSPIN220 DRIVER - Simple Step/Dir Stepper Driver
 * =============================================================================
 * 
 * Implementation of IMotorDriver for the STSPIN220 (Pololu breakout).
 * This is the simplest stepper driver — no UART, no runtime configuration.
 * 
 * Features:
 *   - Step/Dir motion via FastAccelStepper (same as TMC drivers)
 *   - No enable pin — driver auto-enables/disables internally
 *   - Microstepping set by MODE1/MODE2 hardware pins (not software)
 *   - Current limit set by Vref potentiometer (not software)
 *   - No StallGuard, no StealthChop, no communication
 * 
 * Detection:
 *   GPIO 11 HIGH + GPIO 12 HIGH → STSPIN220 detected
 * 
 * =============================================================================
 * WIRING CONFIGURATION (Pololu STSPIN220 Breakout):
 * =============================================================================
 *   ESP32 GPIO 5  →  STEP (step clock input)
 *   ESP32 GPIO 6  →  DIR  (direction input)
 *   GND           →  GND  (common ground)
 *   Motor Power   →  VMOT (4.5–10V motor supply)
 *   3.3V          →  VCC  (logic supply, optional if using VMOT)
 *   
 *   STBY/RST      →  Pulled HIGH on Pololu board (always active)
 *   MODE1/MODE2   →  Set microstepping via jumpers (default: full step)
 *   Vref pot      →  Adjust current limit
 * 
 *   NOTE: No EN pin is used. The STSPIN220 automatically enters
 *         low-power standby when no step pulses are received.
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include "IMotorDriver.h"
#include "../config/PinConfig.h"
#include "FastAccelStepperWrapper.h"

/**
 * @brief STSPIN220 driver implementation (Step/Dir only, no communication)
 */
class STSPIN220Driver : public IMotorDriver {
public:
    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
    
    /**
     * @brief Construct with default pins from PinConfig.h
     */
    STSPIN220Driver();
    
    /**
     * @brief Construct with custom pins
     * @param stepPin Step pulse pin
     * @param dirPin Direction pin
     */
    STSPIN220Driver(uint8_t stepPin, uint8_t dirPin);
    
    virtual ~STSPIN220Driver();
    
    // =========================================================================
    // IMotorDriver Interface Implementation
    // =========================================================================
    
    bool init() override;
    MotorType getType() const override { return MotorType::STEPPER_STSPIN220; }
    const char* getName() const override { return "STSPIN220"; }
    
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
    void setAcceleration(float accelStepsPerSecondSquared) override;
    
    int32_t getPosition() const override;
    void setPosition(int32_t position) override;
    void home(int8_t direction = -1) override;
    
    MotorStatus getStatus() override;
    bool isStalling() override { return false; }
    void printDiagnostics() override;
    bool testConnection() override { return true; }
    
    // =========================================================================
    // FastAccelStepper-Based Methods
    // =========================================================================
    
    void setLinearAcceleration(uint32_t steps) override;
    uint32_t getLinearAcceleration() const override;
    void setHoldCurrentPercent(uint8_t percent) override;
    uint8_t getHoldCurrentPercent() const override;
    void setAutoDisable(bool enable) override;
    bool isAutoDisableActive() const override;
    void runForward() override;
    void runBackward() override;
    void brake() override;
    int32_t getTargetPosition() const override;
    int32_t getActualSpeed() const override;
    uint8_t getRampState() const override;
    bool isRunningContinuously() const override;

private:
    // Pin configuration
    uint8_t _stepPin;
    uint8_t _dirPin;
    
    // FastAccelStepper wrapper for motion
    FastAccelStepperWrapper _stepper;
    
    // State tracking
    bool _enabled;          // Logical enabled state (no physical pin)
    int32_t _position;
    int32_t _targetPosition;
    bool _moving;
    
    // Configuration (cached, even if not sent to hardware)
    float _maxSpeed;
    float _acceleration;
};
