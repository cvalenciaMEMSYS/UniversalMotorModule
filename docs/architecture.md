# Universal Motor Module - Architecture

Complete technical documentation of the firmware architecture.

---

## 📁 Project Structure

```
UniversalMotorModule/
├── src/
│   ├── main.cpp                  # Entry point, main loop
│   ├── config/
│   │   └── PinConfig.h           # Hardware pin definitions
│   ├── core/
│   │   ├── MotorController.h/.cpp # Command processing & motor control
│   │   └── StatusLED.h/.cpp      # WS2812 RGB LED feedback
│   └── drivers/
│       ├── IMotorDriver.h        # Abstract interface for all motors
│       ├── DriverFactory.cpp/h   # Auto-detection and driver creation
│       ├── FastAccelStepperWrapper.h/.cpp # FastAccelStepper library wrapper
│       ├── TMC2209Driver.h/.cpp  # TMC2209 UART + Step/Dir driver
│       ├── TMC2208Driver.h/.cpp  # TMC2208 UART + Step/Dir driver
│       └── DCMotorDriver.h/.cpp  # H-Bridge DC motor driver
├── platformio.ini                # Build configuration
├── Quick_Wiring_Guide_Custom_Pins.md  # Hardware setup
└── docs/                         # Documentation
```

---

## 🏗️ System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        MAIN APPLICATION                          │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  main.cpp - Entry Point                                     │ │
│  │  • setup() - Initialize hardware and subsystems            │ │
│  │  • loop() - Process serial commands and update systems     │ │
│  └────────────────────────────────────────────────────────────┘ │
└───────────────────────────┬──────────────────────────────────────┘
                            │
        ┌───────────────────┴──────────────────┬──────────────────┐
        │                                       │                   │
        ▼                                       ▼                   ▼
┌─────────────────┐                ┌──────────────────┐  ┌─────────────────┐
│  SERIAL I/O    │                │  MOTOR CONTROLLER │  │  STATUS LED     │
│                 │                │                  │  │                 │
│  Serial @ 115200│──commands──────>  • Command parser│  │  • WS2812 LED   │
│  • USB CDC      │                │  • Motion state  │  │  • Color codes  │
│  • Echo back    │<───status──────   • Error handling│  │  • Animations   │
│  • Help menu    │                │  • Profile mgmt  │  │                 │
└─────────────────┘                └────────┬─────────┘  └─────────────────┘
                                            │
                        ┌───────────────────┴──────────────────┐
                        │                                       │
                        ▼                                       ▼
            ┌──────────────────────┐              ┌─────────────────────────┐
            │  HARDWARE DETECTION  │              │   DRIVER ABSTRACTION    │
            │                      │              │                         │
            │  DriverFactory       │              │  IMotorDriver           │
            │  • Read GPIO 11, 12  │              │  • Pure virtual interface│
            │  • Auto-detect driver│              │  • Common API           │
            │  • Create instance   │              │                         │
            └──────────────────────┘              └─────────┬───────────────┘
                                                            │
                        ┌───────────────────────────────────┼─────────────────────────────┐
                        │                                   │                             │
                        ▼                                   ▼                             ▼
            ┌───────────────────────┐      ┌──────────────────────┐      ┌────────────────────┐
            │  TMC2209 DRIVER       │      │  TMC2208 DRIVER      │      │  DC MOTOR DRIVER   │
            │                       │      │                      │      │                    │
            │  • UART comms (RX/TX) │      │  • Step/Dir fallback │      │  • H-bridge RZ7899 │
            │  • TMCStepper lib     │      │  • Limited features  │      │  • PWM speed ctrl  │
            │  • Current control    │      │  • Vref current      │      │  • Direction pins  │
            │  • Microstepping      │      │  • MS pins           │      │                    │
            │  • StallGuard         │      │                      │      │                    │
            │  • StealthChop        │      │                      │      │                    │
            └───────────┬───────────┘      └──────────┬───────────┘      └────────┬───────────┘
                        │                             │                           │
                        └────────────┬────────────────┘                           │
                                     │                                            │
                                     ▼                                            ▼
                        ┌────────────────────────┐                  ┌──────────────────────┐
                        │  FastAccelStepper      │                  │  PWM GENERATION      │
                        │  Wrapper               │                  │                      │
                        │  • ESP32 RMT driver    │                  │  Arduino PWM API     │
                        │  • Hardware counters   │                  │  • ledcWrite()       │
                        │  • Acceleration/decel  │                  │  • Motor A/B pins    │
                        │  • Position tracking   │                  │                      │
                        │  • Auto-enable control │                  └──────────────────────┘
                        └────────────────────────┘
```

---

## 📦 Component Details

### 1. Main Application (`main.cpp`)

**Responsibilities:**
- Initialize all hardware subsystems at startup
- Main event loop for serial command processing
- Coordinate between MotorController and StatusLED

**Execution Flow:**
1. `setup()` - Initialize Serial, GPIO, RGB LED, DriverFactory
2. `loop()` - Check serial input, route to MotorController, update LED

---

### 2. Motor Controller (`core/MotorController.h/.cpp`)

**Responsibilities:**
- Parse serial commands (e.g., "move 1000", "set speed 500")
- Dispatch to appropriate driver methods
- Track motor state (moving, idle, error)
- Manage configuration (speed, acceleration, current)
- Status reporting

**Key Commands:**
| Category | Commands |
|----------|----------|
| Motion | `move`, `moveto`, `home`, `stop`, `run forward`, `run backward`, `brake` |
| Config | `set speed`, `set accel`, `set current`, `set microsteps`, `set cubesteps`, `set ihold`, `set autodisable` |
| Query | `status`, `get pos`, `get target`, `get speed`, `get rampstate` |
| Mode | `enable`, `disable`, `stealthchop`, `spreadcycle` |

---

### 3. Status LED (`core/StatusLED.h/.cpp`)

**Responsibilities:**
- WS2812 RGB LED control (GPIO 48)
- Visual state indication:
  - Blue = Initializing
  - Green = Ready
  - Cyan/Magenta/Yellow = Driver type
  - Pulsing = Moving
  - Red = Error

---

### 4. Driver Factory (`drivers/DriverFactory.h/.cpp`)

**Responsibilities:**
- Read GPIO 11, 12 (detection pins)
- Create appropriate driver instance:
  - No jumper → TMC2209 (default)
  - GPIO 11 HIGH → DC Motor
  - GPIO 12 HIGH → TMC2208

---

### 5. Driver Interface (`drivers/IMotorDriver.h`)

**Abstract interface defining motor driver API:**

```cpp
class IMotorDriver {
public:
    // Lifecycle
    virtual bool init() = 0;
    virtual MotorType getType() const = 0;
    
    // Enable/Disable
    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual bool isEnabled() const = 0;
    
    // Motion Control
    virtual void move(int32_t steps) = 0;
    virtual void moveTo(int32_t position) = 0;
    virtual void stop() = 0;
    virtual void emergencyStop() = 0;
    virtual bool isMoving() const = 0;
    
    // Configuration
    virtual void setMaxSpeed(float speed) = 0;
    virtual void setAcceleration(float accel) = 0;
    virtual void setCurrent(uint16_t mA) = 0;
    
    // Position
    virtual int32_t getPosition() const = 0;
    virtual void setPosition(int32_t pos) = 0;
    
    // FastAccelStepper Extensions
    virtual void runForward() {}
    virtual void runBackward() {}
    virtual void brake() {}
    virtual void setLinearAcceleration(int32_t steps) {}
    virtual void setHoldCurrentPercent(uint8_t percent) {}
    virtual void setAutoDisable(bool enabled) {}
    virtual int32_t getTargetPosition() const { return 0; }
    virtual float getActualSpeed() const { return 0.0f; }
    virtual int8_t getRampState() const { return 0; }
};
```

---

### 6. FastAccelStepper Wrapper (`drivers/FastAccelStepperWrapper.h/.cpp`)

**Purpose:** Wrap the FastAccelStepper library for use with our driver abstraction.

**Key Features:**
- Hardware-based pulse generation (ESP32 RMT)
- Position tracking via hardware counters
- Built-in acceleration/deceleration
- S-curve motion (cubic acceleration)
- Auto-enable/disable support

**API:**
```cpp
class FastAccelStepperWrapper {
public:
    bool init(gpio_num_t stepPin, gpio_num_t dirPin, gpio_num_t enPin = GPIO_NUM_NC);
    
    // Speed & Acceleration
    void setFrequency(uint32_t hz);
    void setAcceleration(uint32_t stepsPerSecSq);
    void setLinearAcceleration(int32_t steps);
    
    // Motion
    void moveBy(int32_t steps);
    void moveTo(int32_t position);
    void runForward();
    void runBackward();
    void stop();
    void brake();
    
    // Position
    int32_t getPosition() const;
    int32_t getTargetPosition() const;
    float getActualSpeed() const;
    int8_t getRampState() const;
    
    // Auto-enable
    void setAutoEnable(bool enabled);
};
```

---

### 7. TMC2209 Driver (`drivers/TMC2209Driver.h/.cpp`)

**Features:**
- UART communication (RX=GPIO 17, TX=GPIO 18)
- Current control (0-2000mA)
- Microstepping (1-256)
- StealthChop (silent) / SpreadCycle (torque)
- StallGuard (sensorless homing)
- CoolStep (automatic current adjustment)
- Hold current (IHOLD register)

**Uses:** FastAccelStepperWrapper for pulse generation

---

### 8. TMC2208 Driver (`drivers/TMC2208Driver.h/.cpp`)

**Features:**
- UART communication (optional)
- Step/Dir fallback mode
- Current via Vref (hardware)
- Microstepping via MS pins

**Uses:** FastAccelStepperWrapper for pulse generation

---

### 9. DC Motor Driver (`drivers/DCMotorDriver.h/.cpp`)

**Features:**
- RZ7899 H-bridge control
- PWM speed control (0-255)
- Forward/reverse/brake/coast
- Enable/disable

**Uses:** Arduino ledcWrite() for PWM

---

## 🔌 Pin Configuration

```cpp
// Stepper Control
#define STEP_PIN        5        // Step pulses → FastAccelStepper
#define DIR_PIN         6        // Direction → FastAccelStepper
#define ENABLE_PIN      4        // Enable (active LOW)

// UART (TMC Drivers)
#define RX_PIN          2        // UART receive
#define TX_PIN          1        // UART transmit

// DC Motor H-Bridge
#define DC_FI_PIN       8        // Forward Input (PWM)
#define DC_BI_PIN       9        // Backward Input (PWM)

// Hardware Detection
#define DETECT_PIN_1    11       // Driver detection
#define DETECT_PIN_2    12       // Driver detection

// Status LED
#define LED_PIN         48       // WS2812 RGB LED
```

---

## 🚀 Pulse Generation: FastAccelStepper

**Why FastAccelStepper?**

Previous custom MCPWM implementation had critical issues:
- Pulse gaps during acceleration changes
- 50kHz frequency cap
- Position tracking drift

**FastAccelStepper v0.33.9 provides:**
- Up to 200kHz+ step rates
- Hardware-based position tracking
- Synchronous acceleration updates (no gaps)
- S-curve motion profiles
- Auto-enable/disable support
- Well-tested library used in 3D printers & CNC

**ESP32-S3 Driver:** Uses RMT peripheral for precise timing

---

## 📊 Libraries Used

| Library | Version | Purpose |
|---------|---------|---------|
| FastAccelStepper | 0.33.9 | Hardware pulse generation |
| TMCStepper | 0.7.3 | TMC2209/2208 UART communication |
| Arduino ESP32 | 3.x | Framework |

---

## 🔄 Data Flow

```
User Input (Serial)
        │
        ▼
    main.cpp
        │
        ▼
  MotorController
   (parse command)
        │
        ▼
   IMotorDriver*
   (polymorphic)
        │
   ┌────┼────┐
   │    │    │
   ▼    ▼    ▼
TMC2209 TMC2208 DCMotor
   │    │
   └────┘
      │
      ▼
FastAccelStepperWrapper
      │
      ▼
   Hardware
 (ESP32 RMT)
```

---

## 📚 Related Documentation

- [Command Protocol](command-protocol.md) - Full command reference
- [TMC2209 Guide](tmc2209-guide.md) - Driver features
- [ESP32-S3 Hardware](esp32-s3-hardware.md) - MCU capabilities
- [Hardware Detection](hardware-detection.md) - Auto-detection logic
- [Quick Wiring Guide](../Quick_Wiring_Guide_Custom_Pins.md) - Hardware connections

---

*Last updated: January 2026 - FastAccelStepper v0.33.9*
