/*
 * =============================================================================
 * STATUS LED - WS2812 NeoPixel Status Indicator
 * =============================================================================
 * 
 * Uses the onboard WS2812 NeoPixel LED to show system status.
 * 
 * LED Color Scheme:
 * 
 *   Status Patterns:
 *     - INITIALIZING:  Blue solid - system starting up
 *     - READY:         Driver color (solid) - ready for commands
 *     - MOVING:        Driver color (blinking) - motor in motion
 *     - COMMAND_RX:    Orange flash (100ms) - command received
 *     - WARNING:       Yellow blink - non-critical issue
 *     - ERROR:         Red solid - error state
 *     - STALL:         Red fast blink - stall detected
 *     - IDLE:          Driver color (dim pulse) - waiting for input
 *     - REBOOTING:     Rainbow sweep → fade to black
 * 
 *   Driver Type → Base Color:
 *     - TMC2209:       Green (0, 255, 0)
 *     - TMC2208:       Cyan (0, 255, 200)
 *     - DC Motor:      Blue (0, 100, 255)
 *     - Unknown:       White (255, 255, 255)
 * 
 *   Acceleration Profile → Color Modifier:
 *     - CONSTANT:      Pure driver color (no modifier)
 *     - TRAPEZOIDAL:   Yellow tint (+50 red)
 *     - S_CURVE:       Purple tint (+50 blue)
 * 
 * Architecture: State-based with explicit color tracking and animation system.
 * Previous color is saved before animations and restored when animation ends.
 * 
 * See docs/led-status-codes.md for full documentation.
 * 
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include "../config/PinConfig.h"

// Include ESP32-S3 RMT-based NeoPixel driver
#include "esp32-hal-rgb-led.h"

/**
 * @brief System status for LED indication
 */
enum class SystemStatus {
    INITIALIZING,     // Blue - system starting up
    READY,            // Driver color solid - ready for commands
    MOVING,           // Driver color blink - motor in motion
    COMMAND_RX,       // Orange flash - command received
    DRIVER_OFF,       // Off - driver disabled
    WARNING,          // Yellow blink - non-critical issue
    ERROR,            // Red solid - error state
    STALL,            // Red blink - stall detected
    IDLE,             // Driver color dim pulse - waiting for input
    REBOOTING         // Rainbow sweep then fade
};

/**
 * @brief Driver type for LED color selection
 */
enum class DriverType {
    UNKNOWN,
    TMC2209,
    TMC2208,
    DC_MOTOR
};

/**
 * @brief Acceleration profile for LED shade modifier
 */
enum class AccelProfile {
    CONSTANT,         // Pure driver color
    TRAPEZOIDAL,      // Yellow tint
    S_CURVE           // Purple tint
};

/**
 * @brief Animation types for LED effects
 */
enum class AnimationType {
    ANIM_NONE,        // No animation - solid color
    ANIM_FLASH,       // Quick single color flash, then restore
    ANIM_BLINK,       // Ongoing alternating flash
    ANIM_PULSE,       // Ongoing breathing effect
    ANIM_RAINBOW      // Rainbow sweep (blocking)
};

/**
 * @brief LED color state
 */
struct LEDColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    
    LEDColor() : r(0), g(0), b(0) {}
    LEDColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
};

// Forward declaration to avoid circular include
enum class MotorType;

/**
 * @brief Convert MotorType to DriverType for LED display
 */
inline DriverType motorTypeToDriverType(MotorType type) {
    switch (static_cast<int>(type)) {
        case 0: return DriverType::TMC2209;   // STEPPER_TMC2209
        case 1: return DriverType::TMC2208;   // STEPPER_TMC2208
        case 2: return DriverType::DC_MOTOR;  // DC_MOTOR
        default: return DriverType::UNKNOWN;
    }
}

/**
 * @brief Status LED controller for WS2812 NeoPixel
 * 
 * State-based architecture with animation support.
 * Previous color is tracked and restored after temporary animations.
 */
class StatusLED {
public:
    StatusLED();
    
    /**
     * @brief Initialize the LED
     */
    void begin();
    
    /**
     * @brief Update LED state - call in loop()
     * Processes animations and applies colors.
     */
    void update();
    
    /**
     * @brief Set the current system status
     * Updates the base color and saves it for restoration after animations.
     * @param status New status to display
     */
    void setStatus(SystemStatus status);
    
    /**
     * @brief Get current status
     */
    SystemStatus getStatus() const { return _status; }
    
    /**
     * @brief Set the driver type (affects base color)
     * @param type Driver type
     */
    void setDriverType(DriverType type);
    
    /**
     * @brief Set the acceleration profile (affects color shade)
     * @param profile Acceleration profile
     */
    void setAccelProfile(AccelProfile profile);
    
    /**
     * @brief Flash orange to indicate command received (100ms)
     * Saves current color and restores it after flash.
     */
    void flashCommandReceived();
    
    /**
     * @brief Play rainbow reboot animation (blocking)
     */
    void playRebootAnimation();
    
    /**
     * @brief Set custom color directly
     * This becomes the "base" color that will be restored after animations.
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * @brief Flash a color briefly then restore previous color
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     * @param durationMs Flash duration (default 100ms)
     */
    void flash(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs = 100);
    
    /**
     * @brief Start pulsing the current color
     */
    void startPulse();
    
    /**
     * @brief Stop pulsing and return to solid color
     */
    void stopPulse();
    
    /**
     * @brief Turn LED off
     */
    void off();
    
private:
    // Current system status
    SystemStatus _status;
    DriverType _driverType;
    AccelProfile _accelProfile;
    uint8_t _brightness;
    
    // Color state tracking
    LEDColor _baseColor;        // The "normal" color to return to after animations
    LEDColor _displayedColor;   // What's currently being shown on the LED
    
    // Animation state
    AnimationType _animation;
    uint32_t _animationStart;
    uint32_t _animationEnd;
    LEDColor _animationColor;   // Color used during animation (e.g., flash color)
    
    // Blink/pulse state
    uint32_t _lastUpdate;
    bool _blinkState;
    uint8_t _pulseValue;
    int8_t _pulseDirection;
    
    // Pending status (queued during flash animation)
    SystemStatus _pendingStatus;
    bool _hasPendingStatus;
    
    // Get driver-specific color with profile modifier applied
    LEDColor getDriverColor();
    
    // Get color for a given status
    LEDColor getStatusColor(SystemStatus status);
    
    // Apply color to LED hardware
    void applyColor(const LEDColor& color);
    void applyColor(uint8_t r, uint8_t g, uint8_t b);
    
    // Animation processors
    void processFlash();
    void processBlink();
    void processPulse();
};

// Global instance
extern StatusLED statusLED;
