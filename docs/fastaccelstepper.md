# FastAccelStepper Library Reference

The Universal Motor Module uses the **FastAccelStepper** library (v0.33.9) for high-performance stepper motor control.

---

## 🚀 Why FastAccelStepper?

This project migrated from a custom MCPWM implementation to FastAccelStepper to solve:

| Problem | Solution |
|---------|----------|
| Pulse gaps during acceleration | Synchronous timer updates |
| 50kHz frequency cap | Up to 200kHz+ step rates |
| Position tracking drift | Hardware-based counters |
| Complex acceleration math | Tested library implementation |

---

## 🔧 Features Used

### Speed & Acceleration
- **Maximum speed:** 200kHz+ step rates (ESP32-S3)
- **Acceleration profiles:** Linear (trapezoidal) and S-curve
- **Constant velocity mode:** Set acceleration to 0

### S-Curve Motion (Cubic Acceleration)

S-curve motion uses cubic acceleration to smooth the start and end of acceleration phases, reducing mechanical stress and vibration.

```
set cubesteps 50   # Apply S-curve over 50 steps at accel start/end
set cubesteps 0    # Disable S-curve (linear acceleration only)
```

**How it works:**
- The `cubesteps` value defines how many steps to use for transitioning in/out of acceleration
- Higher values = smoother but slower transitions
- Typical values: 10-100 steps

### Auto-Enable/Disable

The motor can automatically enable before motion and disable after:

```
set autodisable on   # Motor disables ~100ms after motion stops
set autodisable off  # Motor stays enabled (holds position)
```

**Timing:**
- Enable delay: 100µs before motion starts
- Disable delay: 100ms after motion stops

### Position Tracking

FastAccelStepper uses hardware counters for accurate position tracking:

```
get pos        # Current actual position (steps)
get target     # Target position for current move
get speed      # Current speed (steps/second)
get rampstate  # Acceleration phase (-1=decel, 0=coast, 1=accel)
```

---

## 📊 Commands

### Motion Commands

| Command | Description |
|---------|-------------|
| `move <steps>` | Relative move with acceleration |
| `moveto <position>` | Absolute move with acceleration |
| `run forward` | Continuous rotation CW with acceleration |
| `run backward` | Continuous rotation CCW with acceleration |
| `stop` | Emergency stop (immediate) |
| `brake` | Decelerated stop (smooth) |

### Configuration Commands

| Command | Description |
|---------|-------------|
| `set speed <hz>` | Maximum speed in steps/second |
| `set accel <steps/s²>` | Acceleration rate (0 = constant velocity) |
| `set cubesteps <steps>` | S-curve transition steps (0 = disabled) |
| `set autodisable on/off` | Auto-enable/disable control |

### Query Commands

| Command | Description |
|---------|-------------|
| `get pos` | Current position |
| `get target` | Target position |
| `get speed` | Current speed |
| `get rampstate` | Acceleration state |

---

## 📈 Performance

| Metric | Value |
|--------|-------|
| Maximum step rate | 200kHz+ |
| Position accuracy | ±0 steps (hardware counter) |
| Acceleration resolution | 1 step/s² |
| Supported motors | Up to 14 on ESP32-S3 |

---

## 🔧 FastAccelStepperWrapper API

The project uses a wrapper class around FastAccelStepper:

```cpp
class FastAccelStepperWrapper {
public:
    bool init(gpio_num_t stepPin, gpio_num_t dirPin, 
              gpio_num_t enPin = GPIO_NUM_NC);
    
    // Speed & Acceleration
    void setFrequency(uint32_t hz);
    void setAcceleration(uint32_t stepsPerSecSq);
    void setLinearAcceleration(int32_t steps);  // S-curve
    
    // Motion
    void moveBy(int32_t steps);
    void moveTo(int32_t position);
    void runForward();
    void runBackward();
    void stop();
    void brake();
    
    // Status
    bool isMoving() const;
    int32_t getPosition() const;
    int32_t getTargetPosition() const;
    float getActualSpeed() const;
    int8_t getRampState() const;
    
    // Auto-enable
    void setAutoEnable(bool enabled);
};
```

---

## 📚 ESP32-S3 Implementation Details

FastAccelStepper uses the ESP32's RMT (Remote Control) peripheral for pulse generation:

- **RMT module:** Precise timing without CPU intervention
- **PCNT module:** Hardware pulse counting for position
- **Auto-selection:** Library chooses optimal driver for ESP32-S3

---

## 🔗 Resources

- [FastAccelStepper GitHub](https://github.com/gin66/FastAccelStepper)
- [Library Documentation](https://github.com/gin66/FastAccelStepper/blob/master/README.md)
- [ESP32 RMT Peripheral](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/rmt.html)

---

*Library version: 0.33.9 | Last updated: January 2026*
