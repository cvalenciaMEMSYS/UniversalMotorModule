/*
 * =============================================================================
 * STATUS LED - Implementation
 * =============================================================================
 * 
 * State-based architecture with explicit color tracking.
 * 
 * Key design:
 * - _baseColor: The "normal" color for the current status (restored after animations)
 * - _displayedColor: What's actually shown on the LED right now
 * - _animation: Current animation type (NONE = solid/static)
 * - _animationColor: Color used during the animation (e.g., flash color)
 * 
 * When flash() is called:
 * 1. _baseColor stays unchanged (it's what we return to)
 * 2. _animationColor = flash color
 * 3. _animation = ANIM_FLASH
 * 4. update() shows _animationColor until timer expires
 * 5. When timer expires, _animation = ANIM_NONE, show _baseColor
 * 
 * =============================================================================
 */

#include "StatusLED.h"

// Global instance
StatusLED statusLED;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

StatusLED::StatusLED()
    : _status(SystemStatus::INITIALIZING)
    , _driverType(DriverType::UNKNOWN)
    , _accelProfile(AccelProfile::CONSTANT)
    , _brightness(50)  // 50/255 = ~20% brightness (NeoPixels are bright!)
    , _baseColor(0, 0, 50)          // Start with blue
    , _displayedColor(0, 0, 0)
    , _animation(AnimationType::ANIM_NONE)
    , _animationStart(0)
    , _animationEnd(0)
    , _animationColor(0, 0, 0)
    , _lastUpdate(0)
    , _blinkState(false)
    , _pulseValue(50)
    , _pulseDirection(1)
    , _pendingStatus(SystemStatus::READY)
    , _hasPendingStatus(false) {
}

// =============================================================================
// PUBLIC METHODS
// =============================================================================

void StatusLED::begin() {
    // Set initial color to blue (initializing)
    _baseColor = LEDColor(0, 0, _brightness);
    applyColor(_baseColor);
}

void StatusLED::update() {
    uint32_t now = millis();
    
    // Process current animation
    switch (_animation) {
        case AnimationType::ANIM_FLASH:
            processFlash();
            break;
            
        case AnimationType::ANIM_BLINK:
            processBlink();
            break;
            
        case AnimationType::ANIM_PULSE:
            processPulse();
            break;
            
        case AnimationType::ANIM_NONE:
        default:
            // No animation - just show base color if it changed
            if (_displayedColor.r != _baseColor.r ||
                _displayedColor.g != _baseColor.g ||
                _displayedColor.b != _baseColor.b) {
                applyColor(_baseColor);
            }
            break;
    }
}

void StatusLED::setStatus(SystemStatus status) {
    // If currently flashing, queue this status for later
    if (_animation == AnimationType::ANIM_FLASH) {
        _pendingStatus = status;
        _hasPendingStatus = true;
        return;  // Don't change anything while flashing
    }
    
    _status = status;
    
    // Calculate new base color for this status
    LEDColor newColor = getStatusColor(status);
    _baseColor = newColor;
    
    // Set appropriate animation mode
    switch (status) {
        case SystemStatus::MOVING:
        case SystemStatus::STALL:
        case SystemStatus::WARNING:
            // These statuses blink - start with OFF state
            _animation = AnimationType::ANIM_BLINK;
            _blinkState = false;  // Start OFF so user sees transition
            _lastUpdate = millis();
            applyColor(0, 0, 0);  // Start with OFF
            break;
            
        case SystemStatus::IDLE:
            // Idle pulses - initialize pulse state
            _animation = AnimationType::ANIM_PULSE;
            _pulseValue = 50;
            _pulseDirection = 1;
            _lastUpdate = millis();
            break;
            
        default:
            // Most statuses are solid (unless we're currently flashing)
            if (_animation != AnimationType::ANIM_FLASH) {
                _animation = AnimationType::ANIM_NONE;
            }
            break;
    }
    
    // Apply immediately if not flashing
    if (_animation != AnimationType::ANIM_FLASH) {
        applyColor(_baseColor);
    }
}

void StatusLED::setDriverType(DriverType type) {
    _driverType = type;
}

void StatusLED::setAccelProfile(AccelProfile profile) {
    _accelProfile = profile;
    
    // Recalculate base color to apply new profile tint immediately
    // (unless we're in a flash animation - let it finish first)
    if (_animation != AnimationType::ANIM_FLASH) {
        _baseColor = getStatusColor(_status);
    }
}

void StatusLED::flashCommandReceived() {
    // Orange flash for 100ms
    flash(_brightness, _brightness / 3, 0, 100);
}

void StatusLED::playRebootAnimation() {
    // Stop any current animation
    _animation = AnimationType::ANIM_NONE;
    
    // Rainbow sweep: R -> O -> Y -> G -> C -> B -> P
    const uint8_t rainbow[][3] = {
        {255, 0, 0},      // Red
        {255, 127, 0},    // Orange
        {255, 255, 0},    // Yellow
        {0, 255, 0},      // Green
        {0, 255, 255},    // Cyan
        {0, 0, 255},      // Blue
        {128, 0, 255}     // Purple
    };
    
    // Sweep through rainbow colors
    for (int i = 0; i < 7; i++) {
        applyColor(rainbow[i][0], rainbow[i][1], rainbow[i][2]);
        delay(70);
    }
    
    // Fade to black
    for (int brightness = 255; brightness >= 0; brightness -= 15) {
        float scale = brightness / 255.0f;
        applyColor((uint8_t)(128 * scale), 0, (uint8_t)(255 * scale));
        delay(20);
    }
    
    // Final off
    applyColor(0, 0, 0);
    delay(100);
}

void StatusLED::setColor(uint8_t r, uint8_t g, uint8_t b) {
    // Set as new base color
    _baseColor = LEDColor(r, g, b);
    _animation = AnimationType::ANIM_NONE;
    applyColor(_baseColor);
}

void StatusLED::flash(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs) {
    // Save flash color and timing
    _animationColor = LEDColor(r, g, b);
    _animationStart = millis();
    _animationEnd = _animationStart + durationMs;
    _animation = AnimationType::ANIM_FLASH;
    
    // Immediately show flash color
    applyColor(_animationColor);
}

void StatusLED::startPulse() {
    _animation = AnimationType::ANIM_PULSE;
    _pulseValue = 50;
    _pulseDirection = 1;
    _lastUpdate = millis();
}

void StatusLED::stopPulse() {
    _animation = AnimationType::ANIM_NONE;
    applyColor(_baseColor);
}

void StatusLED::off() {
    _animation = AnimationType::ANIM_NONE;
    applyColor(0, 0, 0);
}

// =============================================================================
// PRIVATE METHODS
// =============================================================================

LEDColor StatusLED::getDriverColor() {
    LEDColor color;
    
    // Base colors for each driver type
    switch (_driverType) {
        case DriverType::TMC2209:
            color = LEDColor(0, _brightness, 0);  // Green
            break;
        case DriverType::TMC2208:
            color = LEDColor(0, _brightness, (uint8_t)(_brightness * 0.8f));  // Cyan
            break;
        case DriverType::DC_MOTOR:
            color = LEDColor(0, (uint8_t)(_brightness * 0.4f), _brightness);  // Blue
            break;
        case DriverType::UNKNOWN:
        default:
            color = LEDColor(_brightness, _brightness, _brightness);  // White
            break;
    }
    
    // Apply acceleration profile modifier
    switch (_accelProfile) {
        case AccelProfile::TRAPEZOIDAL:
            // Yellow tint: add red
            color.r = min(255, color.r + 50);
            break;
        case AccelProfile::S_CURVE:
            // Purple tint: add blue
            color.b = min(255, color.b + 50);
            break;
        case AccelProfile::CONSTANT:
        default:
            // No modifier
            break;
    }
    
    return color;
}

LEDColor StatusLED::getStatusColor(SystemStatus status) {
    switch (status) {
        case SystemStatus::INITIALIZING:
            return LEDColor(0, 0, _brightness);  // Blue
            
        case SystemStatus::READY:
        case SystemStatus::MOVING:
        case SystemStatus::IDLE:
            return getDriverColor();  // Driver-specific color
            
        case SystemStatus::COMMAND_RX:
            return LEDColor(_brightness, _brightness / 2, 0);  // Orange
            
        case SystemStatus::DRIVER_OFF:
            return LEDColor(0, 0, 0);  // Off
            
        case SystemStatus::WARNING:
            return LEDColor(_brightness, _brightness / 2, 0);  // Yellow
            
        case SystemStatus::ERROR:
        case SystemStatus::STALL:
            return LEDColor(_brightness, 0, 0);  // Red
            
        case SystemStatus::REBOOTING:
            return LEDColor(0, 0, 0);  // Handled by playRebootAnimation
            
        default:
            return LEDColor(_brightness, _brightness, _brightness);  // White
    }
}

void StatusLED::applyColor(const LEDColor& color) {
    applyColor(color.r, color.g, color.b);
}

void StatusLED::applyColor(uint8_t r, uint8_t g, uint8_t b) {
    // Skip if color hasn't changed (reduces RMT peripheral load)
    if (r == _displayedColor.r && g == _displayedColor.g && b == _displayedColor.b) {
        return;
    }
    
    // Update tracked state
    _displayedColor = LEDColor(r, g, b);
    
    // Write to hardware
    neopixelWrite(LED_PIN, r, g, b);
}

void StatusLED::processFlash() {
    uint32_t now = millis();
    
    if (now >= _animationEnd) {
        // Flash ended
        _animation = AnimationType::ANIM_NONE;
        
        // Apply pending status if any was queued during flash
        if (_hasPendingStatus) {
            _hasPendingStatus = false;
            setStatus(_pendingStatus);  // This will set appropriate animation
        } else {
            // No pending status - recalculate base color in case profile changed during flash
            _baseColor = getStatusColor(_status);
            applyColor(_baseColor);
        }
    }
    // If flash still active, color is already set
}

void StatusLED::processBlink() {
    uint32_t now = millis();
    uint16_t interval = 150;  // Default blink interval
    
    // Adjust blink speed based on status
    if (_status == SystemStatus::MOVING) {
        interval = 80;  // Fast blink for movement
    } else if (_status == SystemStatus::STALL) {
        interval = 100;  // Fast blink for stall
    } else if (_status == SystemStatus::WARNING) {
        interval = 500;  // Slow blink for warning
    }
    
    if ((now - _lastUpdate) >= interval) {
        _blinkState = !_blinkState;
        _lastUpdate = now;
    }
    
    if (_blinkState) {
        applyColor(_baseColor);
    } else {
        applyColor(0, 0, 0);
    }
}

void StatusLED::processPulse() {
    uint32_t now = millis();
    
    // Update pulse value every 50ms for smooth animation
    // (20ms was too fast and caused RMT timing issues on some boards)
    if ((now - _lastUpdate) >= 50) {
        _pulseValue += _pulseDirection * 5;  // Larger step to compensate for slower update
        
        if (_pulseValue >= 100) {
            _pulseValue = 100;
            _pulseDirection = -1;
        } else if (_pulseValue <= 10) {
            _pulseValue = 10;
            _pulseDirection = 1;
        }
        
        _lastUpdate = now;
        
        // Scale base color by pulse value and apply
        float scale = _pulseValue / 100.0f;
        applyColor(
            (uint8_t)(_baseColor.r * scale),
            (uint8_t)(_baseColor.g * scale),
            (uint8_t)(_baseColor.b * scale)
        );
    }
}
