/*
 * =============================================================================
 * HARCO H-BRIDGE DRIVER - Custom H-Bridge Module Control (DRV88xx Series)
 * =============================================================================
 * 
 * Implementation of IMotorDriver for HarCo custom H-bridge modules plugged
 * into the stepper driver socket. Uses PWM for speed control via IN1/IN2.
 * 
 * Supported modules (all tested at Vm = Vcc = 3.3V):
 *   DRV8837  (black)  — IN1, IN2, nSLEEP
 *   DRV8832  (green)  — IN1, IN2, nSLEEP
 *   DRV8210P (blue)   — IN1, IN2, nSLEEP
 *   DRV8839  (white)  — IN1, IN2, nSLEEP, Enable
 * 
 * Control modes:
 *   IN1=HIGH, IN2=LOW  → Forward  (PWM on IN1)
 *   IN1=LOW,  IN2=HIGH → Reverse  (PWM on IN2)
 *   IN1=LOW,  IN2=LOW  → Coast    (motor free)
 *   IN1=HIGH, IN2=HIGH → Brake    (motor locked)
 * 
 * =============================================================================
 * WIRING CONFIGURATION (stepper socket remapping):
 * =============================================================================
 *   GPIO 5 (STEP) →  H-bridge IN1 (PWM capable)
 *   GPIO 6 (DIR)  →  H-bridge IN2 (PWM capable)
 *   GPIO 4 (EN)   →  H-bridge Enable (active LOW, DRV8839 only)
 *   GPIO 3 (CLK)  →  H-bridge nSLEEP (active HIGH, all modules)
 *
 *   Note: GPIO 3 requires a jumper from D3 to CLK on the stepper socket.
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include "IMotorDriver.h"
#include "../config/PinConfig.h"

/**
 * @brief HarCo custom H-bridge driver for DRV88xx series modules
 */
class HarCoHBridgeDriver : public IMotorDriver {
public:
    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
    
    /**
     * @brief Construct with default pins from PinConfig.h
     * @param hasEnablePin true if module has Enable pin (DRV8839)
     */
    HarCoHBridgeDriver(bool hasEnablePin = true);
    
    /**
     * @brief Construct with custom pins
     * @param in1Pin H-bridge IN1 (PWM capable)
     * @param in2Pin H-bridge IN2 (PWM capable)
     * @param sleepPin nSLEEP pin (active HIGH)
     * @param enPin Enable pin (active LOW, set 255 to disable)
     */
    HarCoHBridgeDriver(uint8_t in1Pin, uint8_t in2Pin, uint8_t sleepPin, uint8_t enPin = 255);
    
    virtual ~HarCoHBridgeDriver();
    
    // =========================================================================
    // IMotorDriver Interface Implementation
    // =========================================================================
    
    bool init() override;
    MotorType getType() const override { return MotorType::HARCO_HBRIDGE; }
    const char* getName() const override { return "HarCo H-Bridge"; }
    
    void enable() override;
    void disable() override;
    bool isEnabled() const override;
    
    void move(int32_t steps) override;
    void moveTo(int32_t position) override;
    void stop() override;
    void emergencyStop() override;
    bool isMoving() const override;
    void update() override;
    
    void runForward() override;
    void runBackward() override;
    void brake() override;
    
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
    
    int32_t getTargetPosition() const override;
    int32_t getActualSpeed() const override;
    uint8_t getRampState() const override;
    bool isRunningContinuously() const override;
    
    // =========================================================================
    // HarCo-Specific Methods
    // =========================================================================
    
    /**
     * @brief Set motor speed directly
     * @param speed -1.0 to +1.0 (negative = reverse)
     */
    void setSpeed(float speed);
    
    /**
     * @brief Get current motor speed
     * @return Current speed -1.0 to +1.0
     */
    float getSpeed() const;
    
    /**
     * @brief Coast (motor free-wheels)
     */
    void coast();
    
    /**
     * @brief Set nSLEEP pin state
     * @param awake true = nSLEEP HIGH (awake), false = nSLEEP LOW (sleep)
     */
    void setSleep(bool awake);
    
    /**
     * @brief Check if module is awake (nSLEEP HIGH)
     */
    bool isAwake() const;
    
    /**
     * @brief Invert nSLEEP logic (for modules with inverted sleep pin)
     * @param inverted true = LOW means awake, false = HIGH means awake (default)
     */
    void invertSleepLogic(bool inverted);
    
    /**
     * @brief Invert Enable logic
     * @param inverted true = HIGH means enabled, false = LOW means enabled (default)
     */
    void invertEnableLogic(bool inverted);
    
    /**
     * @brief Check if sleep logic is currently inverted
     */
    bool isSleepInverted() const;
    
    /**
     * @brief Check if enable logic is currently inverted
     */
    bool isEnableInverted() const;

private:
    // Hardware configuration
    uint8_t _in1Pin, _in2Pin;
    uint8_t _sleepPin;
    uint8_t _enPin;        // 255 = no enable pin
    bool _hasEnablePin;
    
    // PWM configuration
    uint8_t _pwmChannel1, _pwmChannel2;
    uint32_t _pwmFreq;
    uint8_t _pwmResolution;
    uint16_t _maxDuty;
    
    // Current state
    bool _enabled;
    bool _awake;
    bool _sleepInverted;       // If true, LOW = awake
    bool _enableInverted;      // If true, HIGH = enabled
    float _currentSpeed;       // -1.0 to +1.0
    float _targetSpeed;
    int32_t _virtualPosition;
    bool _moving;
    
    // Timed move state
    uint32_t _moveStartTime;
    uint32_t _moveDuration;
    int8_t _moveDirection;
    
    // Acceleration ramping
    float _accelerationRate;
    float _maxSpeedLimit;
    uint32_t _lastUpdateTime;
    
    // Internal methods
    void applySpeed(float speed);
    void updateRamping();
};
