/*
 * =============================================================================
 * PIN CONFIGURATION - Universal Motor Module
 * =============================================================================
 * 
 * All GPIO pin definitions in one place for easy modification.
 * Target: ESP32-S3 Super Mini (lolin_s3_mini)
 * 
 * =============================================================================
 */

#pragma once

#include <Arduino.h>

// =============================================================================
// HARDWARE DETECTION PINS
// =============================================================================
// Used to detect which motor driver is connected at runtime.
// GPIO 10 & 13: Output HIGH to provide VCC for detection jumpers
// GPIO 11 & 12: Input with pull-down to sense jumper connections
//
// Detection Truth Table:
//   GPIO 11 | GPIO 12 | Driver
//   --------|---------|------------------
//   LOW     | LOW     | TMC2209 (default)
//   HIGH    | LOW     | DC Motor (RZ7899)
//   LOW     | HIGH    | TMC2208
//   HIGH    | HIGH    | STSPIN220

constexpr uint8_t DETECT_VCC_1    = 10;   // Output HIGH (VCC source)
constexpr uint8_t DETECT_VCC_2    = 13;   // Output HIGH (VCC source)
constexpr uint8_t DETECT_BIT_0    = 11;   // Input pull-down (DC motor flag)
constexpr uint8_t DETECT_BIT_1    = 12;   // Input pull-down (TMC2208 flag)

// =============================================================================
// TMC2209 / TMC2208 STEPPER DRIVER PINS
// =============================================================================
// UART Configuration (TMC2209 only):
//   GPIO 1 (TX) ──[1kΩ]── GPIO 2 (RX)
//                              │
//         TMC2209 PDN_UART/RX ←┘
//   TMC2209 TX pin = floating (not connected)

constexpr uint8_t TMC_TX_PIN      = 1;    // ESP32 UART TX (through 1kΩ to RX)
constexpr uint8_t TMC_RX_PIN      = 2;    // ESP32 UART RX → TMC2209 PDN_UART/RX
constexpr uint8_t TMC_EN_PIN      = 4;    // TMC2209/2208 EN (active LOW = enabled)
constexpr uint8_t TMC_STEP_PIN    = 5;    // TMC2209/2208 STEP (rising edge = 1 step)
constexpr uint8_t TMC_DIR_PIN     = 6;    // TMC2209/2208 DIR (HIGH/LOW = direction)

// TMC2209 UART Configuration
constexpr uint32_t TMC_UART_BAUD  = 115200;
constexpr uint8_t  TMC_DRIVER_ADDR = 0b00; // MS1=floating, MS2=floating → address 0
constexpr float    TMC_R_SENSE    = 0.11f; // BigTreeTech TMC2209 v1.3 sense resistor

// =============================================================================
// DC MOTOR DRIVER PINS (RZ7899 H-Bridge)
// =============================================================================
// Two control pins for direction + PWM speed control:
//   IN1=HIGH, IN2=LOW  → Forward
//   IN1=LOW,  IN2=HIGH → Reverse
//   IN1=LOW,  IN2=LOW  → Coast (motor free)
//   IN1=HIGH, IN2=HIGH → Brake (motor locked)

constexpr uint8_t DC_IN1_PIN      = 8;    // H-bridge input 1 (PWM capable)
constexpr uint8_t DC_IN2_PIN      = 9;    // H-bridge input 2 (PWM capable)
constexpr uint8_t DC_PWM_CHANNEL  = 0;    // ESP32 LEDC channel for PWM
constexpr uint32_t DC_PWM_FREQ    = 20000; // 20kHz PWM frequency (ultrasonic)
constexpr uint8_t DC_PWM_RES      = 10;   // 10-bit resolution (0-1023)

// =============================================================================
// STATUS LED (WS2812 NeoPixel on ESP32-S3 Super Mini)
// =============================================================================
// The ESP32-S3 Super Mini has an onboard WS2812 NeoPixel LED on GPIO 48

constexpr uint8_t LED_PIN         = 48;   // WS2812 NeoPixel LED

// =============================================================================
// DEFAULT MOTOR PARAMETERS
// =============================================================================
// These are startup defaults, can be changed at runtime via commands

namespace DefaultMotorConfig {
    // Stepper defaults - SAFE STARTUP: low current, motor disabled
    constexpr uint16_t STEPPER_CURRENT_MA    = 100;   // Safe startup current (100mA)
    constexpr uint16_t STEPPER_HOLD_CURRENT  = 0;     // No hold current (for non-backdrivable)
    constexpr uint16_t STEPPER_MICROSTEPS    = 16;    // 16x microstepping
    constexpr float    STEPPER_MAX_SPEED     = 1000.0f; // Steps per second
    constexpr float    STEPPER_ACCELERATION  = 500.0f;  // Steps per second²
    constexpr bool     STEPPER_AUTO_DISABLE  = true;   // Auto-disable after moves (safe startup)
    
    // DC motor defaults
    constexpr float    DC_MAX_SPEED          = 1.0f;  // Full speed (0.0 - 1.0)
    constexpr float    DC_ACCELERATION       = 2.0f;  // Speed units per second
}

// =============================================================================
// SERIAL CONFIGURATION
// =============================================================================
constexpr uint32_t USB_SERIAL_BAUD = 115200;  // USB CDC serial baud rate
constexpr uint32_t SERIAL_TIMEOUT_MS = 3000;  // Wait for serial on startup
