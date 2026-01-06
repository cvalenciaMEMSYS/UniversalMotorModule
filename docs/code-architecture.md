# Universal Motor Module - Code Architecture Deep Dive

Complete technical documentation of the Universal Motor Module firmware architecture.

---

## 📁 Project Structure

```
UniversalMotorModule/
├── src/
│   └── main.cpp              # Main firmware (all code in one file)
├── include/                  # Header files (none currently)
├── lib/                      # Custom libraries (none currently)
├── platformio.ini            # Build configuration
├── Quick_Wiring_Guide.md     # Hardware setup
└── docs/                     # Technical documentation
    ├── code-architecture.md  # This file
    ├── esp32-s3-guide.md     # ESP32-S3 capabilities
    └── tmc2209-guide.md      # TMC2209 driver features
```

---

## 🏗️ Code Architecture Overview

### Design Pattern: Monolithic Arduino-style

The code follows a **single-file Arduino framework** pattern:
1. **Global definitions** (pins, constants, objects)
2. **Function declarations** (forward declarations for .cpp)
3. **setup()** - One-time initialization
4. **loop()** - Main event loop
5. **Helper functions** - Modular function implementations

This is suitable for embedded systems with:
- ✅ Single-threaded execution
- ✅ Limited memory (SRAM/Flash constraints)
- ✅ Direct hardware control
- ✅ Real-time requirements

---

## 🔧 Pin Configuration

### Hardware Abstraction Layer

```cpp
// Control Pins
#define STEP_PIN        5        // Digital output: Step pulse generator
#define DIR_PIN         6        // Digital output: Direction control
#define ENABLE_PIN      4        // Digital output: Driver enable (active LOW)

// UART Pins (Hardware Serial)
#define RX_PIN          2        // UART receive from TMC2209
#define TX_PIN          1        // UART transmit to TMC2209
#define SERIAL_PORT     Serial1  // Hardware UART peripheral

// DC Motor H-Bridge (RZ7899-MS) pins
// Note: VCC is powered by motor supply (3-25V), NOT ESP32 3.3V!
#define DC_FI_PIN       8        // Forward Input (PWM) → Pin 2 on RZ7899
#define DC_BI_PIN       9        // Backward Input (PWM) → Pin 1 on RZ7899
```

**Why these pins?**
- **GPIO 4-6**: Safe, no boot conflicts, support both input/output
- **GPIO 1-2**: Hardware UART capable, no USB interference
- **Serial1**: Dedicated UART peripheral (not Serial0/USB console)
- **GPIO 8-9**: PWM-capable for DC motor speed control, no conflicts

**RZ7899-MS H-Bridge Notes:**
- VCC (Pin 4) connects to motor power (3-25V), NOT ESP32!
- Only GPIO 8 → FI and GPIO 9 → BI connect to ESP32
- Output pins 5+6 (FO) and 7+8 (BO) should be tied together to motor

### Pin Flexibility
The ESP32-S3 **GPIO Matrix** allows UART on almost any pin:
```cpp
SERIAL_PORT.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
                                      ↑       ↑
                                Custom RX   Custom TX
```

---

## 🔌 UART Communication Architecture

### Single-Wire Half-Duplex Protocol

The TMC2209 uses **single-wire UART** (PDN_UART pin):

```
ESP32-S3                                    TMC2209 v1.3
                                   
GPIO 1 (TX_PIN) ──[1kΩ]── GPIO 2 (RX_PIN)
                               │
      PDN_UART/RX pin ←───────┘
                 
                                            TX pin = NOT CONNECTED
```

**⚠️ TESTED CONFIGURATION:**

After hardware testing, the **WORKING** method is:

**✅ Option 1: Resistor between ESP pins (CONFIRMED WORKING)**
- 1kΩ resistor connected **between** ESP32 GPIO 1 (TX) and GPIO 2 (RX)
- GPIO 2 connects directly to TMC2209 PDN_UART/RX pin
- TMC2209 TX pin left floating (not connected)
- Simple, reliable, and confirmed functional

**❌ Option 2: Dual wire using onboard resistors (NOT WORKING)**
- ESP32 GPIO 1 → TMC2209 TX pin
- ESP32 GPIO 2 → TMC2209 RX pin
- This configuration did NOT establish communication
- Do not use this method

### UART Configuration
```cpp
SERIAL_PORT.begin(115200,      // Baud rate (TMC2209 default)
                  SERIAL_8N1,  // 8 data bits, no parity, 1 stop bit
                  RX_PIN,      // Custom RX pin
                  TX_PIN);     // Custom TX pin
```

**Parameters:**
- **115200 baud** - TMC2209 default (configurable in driver)
- **8N1** - Standard UART framing
- **No flow control** - RTS/CTS not needed for slow commands

---

## 🧠 TMC2209 Driver Object

### Initialization

```cpp
#define DRIVER_ADDRESS  0b00     // 2-bit address (MS1, MS2 pins)
#define R_SENSE         0.11f    // Sense resistor (0.11Ω on v1.3)

TMC2209Stepper driver(&SERIAL_PORT, R_SENSE, DRIVER_ADDRESS);
```

**Object Breakdown:**
- **`&SERIAL_PORT`** - Pointer to hardware serial
- **`R_SENSE`** - Current sensing resistor value (hardware-dependent)
- **`DRIVER_ADDRESS`** - Multidrop address (0-3 via MS1/MS2 pins)

### Why R_SENSE Matters
Current calculation: `I_RMS = V_REF / (32 × R_SENSE × √2)`

TMC2209 v1.3 uses **0.11Ω**, which allows:
- Maximum ~2A RMS current
- Precise current measurement
- Low power dissipation in resistor

---

## ⚙️ Initialization Sequence (setup())

### 1. USB Serial Initialization
```cpp
Serial.begin(115200);
unsigned long start = millis();
while (!Serial && (millis() - start < 3000));  // 3s timeout
```

**Why the timeout?**
- ESP32-S3 USB CDC takes time to enumerate
- Without timeout, code blocks indefinitely if USB unplugged
- 3 seconds is enough for any system to detect USB

### 2. UART Initialization
```cpp
if (!SERIAL_PORT.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN)) {
    Serial.println("⚠ WARNING: UART initialization may have failed!");
}
```

**Error handling:**
- `begin()` returns false if pin assignment fails
- Warns user immediately
- Continues execution (non-fatal)

### 3. GPIO Pin Configuration
```cpp
pinMode(STEP_PIN, OUTPUT);
pinMode(DIR_PIN, OUTPUT);
pinMode(ENABLE_PIN, OUTPUT);
digitalWrite(ENABLE_PIN, LOW);  // Enable driver
```

**Pin States:**
- **STEP/DIR**: Initially LOW (motor stationary, CW direction)
- **ENABLE**: LOW = enabled, HIGH = disabled (power saving)

### 3b. DC Motor H-Bridge Initialization
```cpp
// Initialize DC Motor H-Bridge pins (RZ7899-MS)
// Note: VCC powered by motor supply (3-25V), not ESP32!
// Only control pins (FI/BI) connect to ESP32 GPIOs
pinMode(DC_FI_PIN, OUTPUT);
pinMode(DC_BI_PIN, OUTPUT);
ledcAttach(DC_FI_PIN, 20000, 8);  // 20kHz, 8-bit
ledcAttach(DC_BI_PIN, 20000, 8);
ledcWrite(DC_FI_PIN, 0);          // Stopped
ledcWrite(DC_BI_PIN, 0);
```

**DC Motor PWM Setup:**
- **20kHz frequency** - Above audible range for quiet operation
- **8-bit resolution** - 256 speed levels (0-255)
- **Both pins start at 0** - Motor in coast (stopped) state

**RZ7899-MS H-Bridge Specs:**
- Supply voltage: 3.0V - 25.0V (motor power, not logic!)
- Peak current: 6A, Continuous: 3-5A (with heatsink)
- Pinout: BI(1), FI(2), GND(3), VCC(4), FO(5,6), BO(7,8)

### 4. Driver Wake-up Delay
```cpp
delay(100);  // 100ms for power stabilization
```

**Why 100ms?**
- TMC2209 requires ~30ms after VIO power-on
- Ensures internal voltage regulators are stable
- Allows time for oscillator startup

### 5. TMC2209 Configuration
```cpp
driver.begin();                    // Initialize SPI/UART interface
driver.toff(5);                    // Enable driver (TOFF > 0)
driver.rms_current(RMS_CURRENT);   // Set motor current
driver.microsteps(MICROSTEPS);     // Set microstepping
```

**Critical Configuration:**

| Function | Purpose | Default | Range |
|----------|---------|---------|-------|
| `toff()` | Chopper off-time | 5 | 1-15 (0=disable) |
| `rms_current()` | Motor current | 800mA | 100-2000mA |
| `microsteps()` | Resolution | 16 | 1,2,4,8,16,32,64,128,256 |

**TOFF = 5 explained:**
- Chopper off-time in clock cycles
- Controls PWM frequency: `f_PWM ≈ f_CLK / (32 × TOFF)`
- TOFF=5 → ~35kHz (inaudible, efficient)

### 6. Advanced Features
```cpp
driver.pwm_autoscale(true);      // Auto-tune PWM
driver.en_spreadCycle(false);    // StealthChop mode
driver.pwm_autograd(true);       // Auto-gradient adapt
```

**StealthChop vs SpreadCycle:**

| Mode | Noise | Torque | Speed | Use Case |
|------|-------|--------|-------|----------|
| StealthChop | Silent | Good | Low-Medium | 3D printers, quiet apps |
| SpreadCycle | Audible | Excellent | High | CNC, high-load apps |

### 7. StallGuard Configuration
```cpp
driver.TCOOLTHRS(0xFFFFF);       // Enable StallGuard below this speed
driver.SGTHRS(STALL_VALUE);      // Stall detection threshold
```

**StallGuard for Sensorless Homing:**
- **TCOOLTHRS**: Speed threshold (0xFFFFF = always active)
- **SGTHRS**: Sensitivity (0 = sensitive, 255 = insensitive)
- Default 10 = medium sensitivity

### 8. CoolStep Configuration
```cpp
driver.semin(5);                 // Min StallGuard for current reduction
driver.semax(2);                 // Max StallGuard for current increase
driver.sedn(0b01);               // Current down-step speed
```

**CoolStep = Automatic Current Regulation:**
- Reduces current when load is light
- Increases current when load increases
- Saves power and reduces heat

### 9. Connection Test
```cpp
uint8_t result = driver.test_connection();
if (result == 0) {
    Serial.println("✓ TMC2209 Connection Successful!");
}
```

**Test Result Codes:**
- **0** = Success (UART working, driver responding)
- **1** = No response (check wiring, power, address)
- **2** = Wrong response (UART noise, baud rate mismatch)

---

## 🔄 Main Event Loop (loop())

### Simple Polling Architecture

```cpp
void loop() {
    if (Serial.available() > 0) {
        char command = Serial.read();
        handleCommand(command);
    }
}
```

**Design Choice: Blocking Commands**
- Most commands execute completely before returning
- No interrupt-driven motor control
- Simple, predictable behavior

**Pros:**
- ✅ Easy to understand
- ✅ No race conditions
- ✅ Predictable timing

**Cons:**
- ❌ Can't receive new commands during motion
- ❌ No background tasks
- ❌ Not suitable for multi-axis coordination

**Improvement Option:**
For advanced users, implement non-blocking motion:
```cpp
void loop() {
    handleSerialCommands();   // Check for new commands
    updateMotorState();       // Non-blocking step generation
    updateSensors();          // Read StallGuard, etc.
}
```

---

## 🎮 Command Handler Architecture

### State Machine Design

```cpp
void handleCommand(char cmd) {
    switch (cmd) {
        case '1': rotateMotor(true, 200, 1000); break;
        case '2': rotateMotor(false, 200, 1000); break;
        // ... more cases
    }
}
```

**Command Categories:**

1. **Motion Commands** (1-4, 0)
   - Blocking operations
   - Direct motor control
   - Real-time execution

2. **Configuration Commands** (5-9, c, e, r)
   - Modify driver settings
   - Non-volatile (until reset)
   - Immediate effect

3. **Diagnostic Commands** (s, d, h)
   - Read-only operations
   - Display information
   - No hardware changes

4. **DC Motor Commands** (g, G, k, K, j, J, t)
   - DC motor speed and direction control
   - PWM-based operation
   - Independent from stepper commands

---

## 🎯 Motor Control Functions

### Basic Step Generation

```cpp
void rotateMotor(bool direction, int steps, int delayTime) {
    digitalWrite(DIR_PIN, direction);  // Set direction
    for (int i = 0; i < steps; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(delayTime);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(delayTime);
    }
}
```

**Timing Diagram:**
```
STEP pin:  ___╱‾‾‾╲___╱‾‾‾╲___╱‾‾‾╲___
           ◄─►◄─►◄─►◄─►◄─►◄─►
          delayTime (e.g., 1000µs)
          
One full cycle = 2 × delayTime = 2000µs = 500Hz
```

**Speed Calculation:**
```
Steps/sec = 1,000,000 / (2 × delayTime)
RPM = (Steps/sec × 60) / (MOTOR_STEPS × MICROSTEPS)

Example (delayTime=1000µs, 16 microsteps):
- Steps/sec = 1,000,000 / 2000 = 500
- RPM = (500 × 60) / (200 × 16) = 9.375 RPM
```

### Continuous Rotation

```cpp
void continuousRotation(bool direction) {
    digitalWrite(DIR_PIN, direction);
    while (true) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(currentSpeed);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(currentSpeed);
        
        if (Serial.available() > 0) {
            char cmd = Serial.read();
            if (cmd == '0') break;  // Stop command
        }
    }
}
```

**Exit Mechanism:**
- Polls serial buffer every step
- Checks for '0' stop command
- Graceful exit (finishes current step)

**Limitation:**
- Checking serial every step slows maximum speed
- For high-speed continuous motion, use timer interrupts

---

## ⚙️ Configuration Functions

### Microstepping Adjustment

```cpp
void changeMicrostepping() {
    // Display menu, wait for input
    while (!Serial.available());
    char choice = Serial.read();
    
    int ms = 16;  // default
    switch (choice) {
        case '5': ms = 16; break;
        case '6': ms = 32; break;
        // ... etc
    }
    
    driver.microsteps(ms);  // Apply to driver
}
```

**Effect on Resolution:**
```
Full step      = 1.8° (200 steps/rev)
16 microsteps  = 0.1125° (3200 steps/rev)
256 microsteps = 0.007° (51200 steps/rev)
```

**Trade-offs:**

| Microsteps | Resolution | Max Speed | Torque | Smoothness |
|------------|-----------|-----------|--------|------------|
| 1 | Low | Highest | 100% | Poor |
| 16 | Medium | High | ~95% | Good |
| 256 | Highest | Low | ~70% | Excellent |

### Current Adjustment

```cpp
void changeCurrent() {
    while (!Serial.available());
    int current = Serial.parseInt();
    
    if (current >= 100 && current <= 2000) {
        driver.rms_current(current);
    }
}
```

**Safety Limits:**
- **Minimum: 100mA** - Below this, motor won't move
- **Maximum: 2000mA** - Limited by R_SENSE and TMC2209 capability
- **Recommended: 70-80% of motor rating** - Prevents overheating

**Current = Torque:**
```
Holding Torque ≈ Current × K_T (motor torque constant)
```

---

## 📊 Diagnostic Functions

### StallGuard Reading

```cpp
void readStallGuard() {
    uint32_t drv_status = driver.DRV_STATUS();
    int16_t sg_result = (drv_status & 0x3FF);  // Lower 10 bits
    
    Serial.print("SG Result: "); Serial.println(sg_result);
    
    if (sg_result < driver.SGTHRS()) {
        Serial.println("⚠ Motor may be stalled!");
    }
}
```

**DRV_STATUS Register (32 bits):**
```
Bit 31:     Standstill indicator
Bit 30:     Open load indicator A
Bit 29:     Open load indicator B
Bit 28-25:  Short circuit flags
Bit 24-23:  Temperature warnings
Bit 9-0:    StallGuard result (load measurement)
```

**StallGuard Interpretation:**
- **High value (>200)**: Motor running freely, low load
- **Medium (50-200)**: Normal operation
- **Low (<50)**: High load, approaching stall
- **Below SGTHRS**: Stall detected

### Full Diagnostics

```cpp
void displayDiagnostics() {
    uint32_t drv_status = driver.DRV_STATUS();
    
    Serial.print("Open Load A: "); 
    Serial.println((drv_status >> 30) & 1);
    // ... more status flags
}
```

**Critical Status Flags:**

| Flag | Meaning | Action |
|------|---------|--------|
| Overtemperature | IC > 150°C | Add heatsink, reduce current |
| Open Load | Motor disconnected | Check wiring |
| Short A/B | Phase shorted | Check motor coils |
| Standstill | Motor not moving | Check if expected |

---

## 🔄 Driver Reset Function

```cpp
void resetDriver() {
    digitalWrite(ENABLE_PIN, HIGH);  // Disable
    delay(100);
    digitalWrite(ENABLE_PIN, LOW);   // Re-enable
    delay(100);
    
    // Reinitialize with default settings
    driver.begin();
    driver.toff(5);
    driver.rms_current(RMS_CURRENT);
    driver.microsteps(MICROSTEPS);
    driver.pwm_autoscale(true);
    driver.en_spreadCycle(false);
}
```

**When to Reset:**
- After changing multiple settings
- If driver enters fault state
- To restore factory defaults
- If communication errors occur

**Reset Sequence:**
1. Disable driver (HIGH on EN pin)
2. Wait 100ms (discharge internal caps)
3. Re-enable driver (LOW on EN pin)
4. Wait 100ms (power stabilization)
5. Reconfigure all registers

---

## 🧪 Code Quality & Best Practices

### Memory Management
```cpp
// Global variables (static allocation)
TMC2209Stepper driver(...);    // ~200 bytes
bool shaft_direction = false;  // 1 byte
int currentSpeed = 1000;       // 4 bytes

Total static RAM: ~300 bytes (ESP32-S3 has 512KB!)
```

**No Dynamic Allocation:**
- ✅ No `malloc()`, `new`, or `String` objects
- ✅ Predictable memory usage
- ✅ No fragmentation
- ✅ Suitable for 24/7 operation

### Error Handling
```cpp
// UART initialization
if (!SERIAL_PORT.begin(...)) {
    Serial.println("WARNING: ...");
}

// Connection test
uint8_t result = driver.test_connection();
if (result != 0) {
    Serial.println("Connection Failed!");
}

// Input validation
if (current >= 100 && current <= 2000) {
    // Apply setting
} else {
    Serial.println("Invalid current");
}
```

**Error Handling Strategy:**
- **Informative messages** - Tell user what went wrong
- **Graceful degradation** - Continue despite non-fatal errors
- **Input validation** - Prevent invalid configurations

### Code Modularity

**Function Responsibilities:**
```
setup() ────────► Initialize hardware & driver
loop() ─────────► Check for commands
handleCommand()─► Route to appropriate function
rotateMotor() ──► Execute motion
changeCurrent()─► Modify configuration
displayDiag() ──► Read and display status
```

**Single Responsibility Principle:**
- Each function does one thing
- Easy to test individually
- Simple to modify or extend

---

## � DC Motor Control Architecture

### H-Bridge Basics (RZ7899-MS)

The RZ7899-MS is a simple H-bridge that allows bidirectional DC motor control:

```
                  VM (Motor Power)
                       │
              ┌────────┴────────┐
              │    H-Bridge     │
    FI (PWM) ─┤                 ├─ OUT1 ──┐
              │   RZ7899-MS     │         │
    BI (PWM) ─┤                 ├─ OUT2 ──┼── DC Motor
              └────────┬────────┘         │
                       │                  │
                      GND                 │
                                         ─┘
```

### PWM Configuration

The ESP32-S3 LEDC peripheral provides PWM for DC motor speed control:

```cpp
// PWM Setup in setup()
ledcAttach(DC_FI_PIN, 20000, 8);  // 20kHz PWM, 8-bit resolution
ledcAttach(DC_BI_PIN, 20000, 8);
```

**Parameters:**
- **20kHz** - Above audible range for quiet operation
- **8-bit resolution** - 256 speed levels (0-255)

### Control Logic

| Forward PWM | Backward PWM | Motor State |
|-------------|--------------|-------------|
| 0 | 0 | Coast (freewheeling) |
| 50-255 | 0 | Forward, variable speed |
| 0 | 50-255 | Backward, variable speed |
| 255 | 255 | Brake (motor locked) |

### DC Motor Functions

```cpp
void dcMotorControl(bool forward, int speed);  // Direction + speed
void dcMotorStop();                            // Coast stop
void dcMotorStatus();                          // Display status
void changeDCMotorSpeed();                     // Interactive config
```

### State Variables

```cpp
bool dcMotorEnabled = false;    // System enabled
int dcMotorSpeed = 128;         // PWM duty (0-255)
bool dcMotorRunning = false;    // Currently running
bool dcMotorForward = true;     // Direction
```

### DC Motor Command Reference

| Command | Function | Description |
|---------|----------|-------------|
| `g` | Enable DC Motor | Enables the DC motor subsystem |
| `G` | Disable DC Motor | Disables and stops DC motor |
| `k` | Run Forward | Runs DC motor forward at current speed |
| `K` | Run Backward | Runs DC motor backward at current speed |
| `j` | Stop DC Motor | Coast stop (freewheeling) |
| `J` | Brake DC Motor | Active brake (motor locked) |
| `t` | Change Speed | Interactive speed configuration (0-255) |

---

## �🚀 Performance Considerations

### Maximum Step Rate

**Theoretical Maximum:**
```cpp
void fastStep() {
    digitalWrite(STEP_PIN, HIGH);
    // No delay - limited by digitalWrite() speed
    digitalWrite(STEP_PIN, LOW);
}

ESP32 digitalWrite() ≈ 1µs
→ Max ~500,000 steps/sec theoretical
```

**Practical Maximum with delayMicroseconds(100):**
```
delayMicroseconds(100) × 2 = 200µs per step
→ 5,000 steps/sec = 93.75 RPM (16 microsteps)
```

**TMC2209 Hardware Limit:**
- Maximum step frequency: **~300kHz** (300,000 steps/sec)
- At 256 microsteps: **~1200 steps/sec** (234 RPM for 200-step motor)

### Timing Accuracy

**delayMicroseconds() Precision:**
- ±10% accuracy on ESP32
- Not suitable for ultra-precise applications
- For better precision, use hardware timers

**Improved Timing (Advanced):**
```cpp
// Use hardware timer for step generation
hw_timer_t *timer = timerBegin(0, 80, true);  // 1MHz clock
timerAttachInterrupt(timer, &onTimer, true);
timerAlarmWrite(timer, stepPeriod, true);
timerAlarmEnable(timer);
```

---

## 🔮 Future Enhancements

### Potential Improvements

1. **Non-blocking motion control**
   - Timer-based step generation
   - Allows serial commands during motion
   - Multi-axis coordination possible

2. **Acceleration/deceleration**
   - Trapezoidal velocity profiles
   - Prevents motor stalling
   - Smoother operation

3. **Position tracking**
   ```cpp
   volatile long currentPosition = 0;
   
   void onStepInterrupt() {
       currentPosition += (direction ? 1 : -1);
   }
   ```

4. **EEPROM configuration storage**
   - Save settings across power cycles
   - User profiles
   - Calibration data

5. **Multi-driver support**
   - Daisy-chain multiple TMC2209s
   - Use different DRIVER_ADDRESS values
   - Coordinated multi-axis motion

6. **StallGuard-based homing**
   ```cpp
   void homingSequence() {
       while (readStallGuard() > SGTHRS) {
           stepMotor(1);  // Move until stall
       }
       currentPosition = 0;  // Set home position
   }
   ```

---

## 📚 References

- [TMCStepper Library API](https://github.com/teemuatlut/TMCStepper)
- [TMC2209 Datasheet](https://www.trinamic.com/fileadmin/assets/Products/ICs_Documents/TMC2209_Datasheet_V103.pdf)
- [ESP32-S3 GPIO Matrix](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html)
- [Arduino Timing Functions](https://www.arduino.cc/reference/en/language/functions/time/delaymicroseconds/)

---

**Next:** See [esp32-s3-guide.md](esp32-s3-guide.md) for ESP32-S3 hardware capabilities
