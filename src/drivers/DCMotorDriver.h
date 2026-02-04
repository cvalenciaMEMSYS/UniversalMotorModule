/*
 * =============================================================================
 * DC MOTOR DRIVER - H-Bridge PWM Control (RZ7899 Compatible)
 * =============================================================================
 * 
 * Implementation of IMotorDriver for DC motors with H-bridge control.
 * Uses PWM for speed control and direction control via IN1/IN2 pins.
 * 
 * Control modes:
 *   IN1=HIGH, IN2=LOW  → Forward  (PWM on IN1)
 *   IN1=LOW,  IN2=HIGH → Reverse  (PWM on IN2)
 *   IN1=LOW,  IN2=LOW  → Coast    (motor free)
 *   IN1=HIGH, IN2=HIGH → Brake    (motor locked)
 * 
 * =============================================================================
 * WIRING CONFIGURATION:
 * =============================================================================
 *   GPIO 7  →  H-bridge IN1 (PWM capable)
 *   GPIO 8  →  H-bridge IN2 (PWM capable)
 *   
 *   For RZ7899 or similar dual H-bridge:
 *     VCC  →  Motor supply voltage (6-12V typical)
 *     GND  →  Common ground with ESP32
 *     OUT1 →  Motor terminal 1
 *     OUT2 →  Motor terminal 2
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include "IMotorDriver.h"
#include "../config/PinConfig.h"

/**
 * @brief DC Motor driver with H-bridge and PWM control
 */
class DCMotorDriver : public IMotorDriver {
public:
    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
    
    /**
     * @brief Construct with default pins from PinConfig.h
     */
    DCMotorDriver();
    
    /**
     * @brief Construct with custom pins
     * @param in1Pin H-bridge IN1 (PWM capable)
     * @param in2Pin H-bridge IN2 (PWM capable)
     */
    DCMotorDriver(uint8_t in1Pin, uint8_t in2Pin);
    
    virtual ~DCMotorDriver();
    
    // =========================================================================
    // IMotorDriver Interface Implementation
    // =========================================================================
    
    bool init() override;
    MotorType getType() const override { return MotorType::DC_MOTOR; }
    const char* getName() const override { return "DC Motor"; }
    
    void enable() override;
    void disable() override;
    bool isEnabled() const override;
    
    /**
     * @brief Run motor for specified duration
     * @param steps For DC motor: duration in milliseconds (positive=forward, negative=reverse)
     */
    void move(int32_t steps) override;
    
    /**
     * @brief Set target "position" (speed setpoint for DC motors)
     * @param position For DC motor: -1000 to +1000 speed value
     */
    void moveTo(int32_t position) override;
    
    void stop() override;
    void emergencyStop() override;
    bool isMoving() const override;
    void update() override;
    
    void runForward() override;
    void runBackward() override;
    void brake() override;
    
    /**
     * @brief Set maximum speed (duty cycle 0-100%)
     * @param stepsPerSecond For DC motor: max duty cycle percentage
     */
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
    
    // Query methods for status/debugging
    int32_t getTargetPosition() const override;
    int32_t getActualSpeed() const override;
    uint8_t getRampState() const override;
    bool isRunningContinuously() const override;
    
    // =========================================================================
    // DC Motor-Specific Methods
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

private:
    // Hardware configuration
    uint8_t _in1Pin, _in2Pin;
    
    // PWM configuration (ESP32-S3 Arduino Core 2.x style)
    uint8_t _pwmChannel1, _pwmChannel2;
    uint32_t _pwmFreq;
    uint8_t _pwmResolution;
    uint16_t _maxDuty;  // Based on resolution
    
    // Current state
    bool _enabled;
    float _currentSpeed;      // -1.0 to +1.0
    float _targetSpeed;       // Target for ramping
    int32_t _virtualPosition; // Virtual position counter (for API compatibility)
    bool _moving;             // True if running a timed move
    
    // Timed move state
    uint32_t _moveStartTime;
    uint32_t _moveDuration;
    int8_t _moveDirection;
    
    // Acceleration ramping (simple for DC motors)
    float _accelerationRate;  // Acceleration in normalized speed units per second
    float _maxSpeedLimit;     // Max allowed speed (0-1.0)
    uint32_t _lastUpdateTime;
    
    // Internal methods
    void applySpeed(float speed);
    void updateRamping();
};
