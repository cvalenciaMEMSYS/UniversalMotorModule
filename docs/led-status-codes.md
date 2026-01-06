# LED Status Codes

The Universal Motor Module uses the onboard WS2812 NeoPixel LED (GPIO 48) to provide visual feedback about system status, driver type, and acceleration profile.

## LED State Machine

The StatusLED module uses a priority-based state machine:

```
┌─────────────────────────────────────────────────────────────────┐
│                     LED STATE MACHINE                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  PRIORITY 1: Flash Override (100ms timeout)                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  flashCommandReceived() → Orange 100ms → auto-expire    │   │
│  │  playRebootAnimation() → Rainbow sweep → fade → restart │   │
│  └─────────────────────────────────────────────────────────┘   │
│                           ↓ (when expired)                      │
│  PRIORITY 2: Status-Based Display                              │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Based on _status enum:                                  │   │
│  │    INITIALIZING → Blue solid                            │   │
│  │    READY        → Driver color (solid)                  │   │
│  │    MOVING       → Driver color (blinking)               │   │
│  │    IDLE         → Driver color (dim pulsing)            │   │
│  │    ERROR        → Red solid                             │   │
│  │    WARNING      → Yellow blink                          │   │
│  │    STALL        → Red fast blink                        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  Driver Color = f(DriverType, AccelProfile)                    │
│    Base color from driver type                                  │
│    + Modifier tint from acceleration profile                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Flow in main.cpp

```
setup():
  1. statusLED.begin()              → Blue (INITIALIZING)
  2. controller.begin()             → Detect hardware
  3. statusLED.setDriverType(...)   → Set base color
  4. statusLED.setStatus(READY)     → Green (or driver color)

loop():
  1. statusLED.update()             → Refresh animations
  2. Determine target status:
     - isBusy() → MOVING
     - !isReady() → ERROR  
     - timeout > 5s → IDLE
     - else → READY
  3. setStatus(target) if changed

On command:
  1. flashCommandReceived()         → Orange 100ms
  2. processCommand()               → Execute
  3. Loop sets status back to READY
```

## Status Patterns

| Status | Pattern | Description |
|--------|---------|-------------|
| **INITIALIZING** | Blue solid | System is starting up |
| **READY** | Driver color solid | System ready for commands |
| **MOVING** | Driver color blinking | Motor is in motion |
| **COMMAND_RX** | Orange flash (100ms) | Command was received |
| **WARNING** | Yellow blink | Non-critical issue (e.g., open load) |
| **ERROR** | Red solid | Error state (driver init failed) |
| **STALL** | Red fast blink | Stall detected |
| **IDLE** | Driver color dim pulse | Idle for 5+ seconds |
| **REBOOTING** | Rainbow sweep → fade | System restarting |
| **DRIVER_OFF** | Off | Motor driver disabled |

## Driver Type → Base Color

The LED base color indicates which motor driver type is detected:

| Driver Type | Base Color | RGB Value | Visual |
|-------------|------------|-----------|--------|
| **TMC2209** | Green | (0, 255, 0) | 🟢 |
| **TMC2208** | Cyan | (0, 255, 200) | 🔵 |
| **DC Motor (RZ7899)** | Blue | (0, 100, 255) | 🔵 |
| **Unknown** | White | (255, 255, 255) | ⚪ |

## Acceleration Profile → Color Modifier

The acceleration profile modifies the base color to indicate which profile is active:

| Profile | Color Modifier | Effect |
|---------|----------------|--------|
| **CONSTANT** | None | Pure driver color |
| **TRAPEZOIDAL** | Yellow tint (+50 red) | Slightly warmer |
| **S_CURVE** | Purple tint (+50 blue) | Slightly cooler |

### Example Color Combinations

| Driver + Profile | Resulting Color | Description |
|------------------|-----------------|-------------|
| TMC2209 + CONSTANT | Pure Green | Standard stepper mode |
| TMC2209 + TRAPEZOIDAL | Yellow-Green (Lime) | Linear acceleration |
| TMC2209 + S_CURVE | Cyan-Green (Teal) | Smooth jerk-limited |
| TMC2208 + CONSTANT | Cyan | Step/Dir mode |
| DC Motor + CONSTANT | Blue | PWM control |
| DC Motor + TRAPEZOIDAL | Purple-Blue | Ramped speed |

## Reboot Animation

When the `reboot` command is issued, the LED plays a rainbow sweep animation:

1. **Rainbow Sweep** (490ms): Red → Orange → Yellow → Green → Cyan → Blue → Purple
2. **Fade to Black** (350ms): Purple dims to off
3. **System Restart**

## Brightness

Default LED brightness is 20% (50/255) since NeoPixel LEDs are very bright. This can be adjusted in the StatusLED class if needed.

## Hardware Configuration

- **LED Pin**: GPIO 48 (onboard WS2812 NeoPixel on ESP32-S3 Super Mini)
- **Driver**: Uses ESP32-S3's built-in RMT peripheral via `neopixelWrite()`
- **No external library required** for WS2812 control

## Code Reference

See [StatusLED.h](../src/core/StatusLED.h) and [StatusLED.cpp](../src/core/StatusLED.cpp) for implementation details.

### Key API

```cpp
// Set status (affects pattern)
statusLED.setStatus(SystemStatus::READY);

// Set driver type (affects base color)
statusLED.setDriverType(DriverType::TMC2209);

// Set acceleration profile (affects color shade)
statusLED.setAccelProfile(AccelProfile::TRAPEZOIDAL);

// Flash orange for command received
statusLED.flashCommandReceived();

// Play rainbow reboot animation (blocking)
statusLED.playRebootAnimation();
```
