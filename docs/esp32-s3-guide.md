# ESP32-S3 Super Mini - Comprehensive Guide

Deep dive into the ESP32-S3 microcontroller capabilities for stepper motor control and embedded applications.

---

## 🔧 Hardware Overview

### ESP32-S3 Super Mini Specifications

| Feature | Specification | Notes |
|---------|--------------|-------|
| **CPU** | Dual-core Xtensa LX7 @ 240MHz | 32-bit RISC architecture |
| **RAM** | 512 KB SRAM | Fast internal memory |
| **Flash** | 4 MB (typical) | External SPI flash |
| **WiFi** | 802.11 b/g/n | 2.4 GHz only |
| **Bluetooth** | BLE 5.0 | Low energy mode |
| **USB** | USB-OTG (Native) | No USB-UART bridge needed |
| **ADC** | 2× 12-bit SAR ADC | 20 channels total |
| **DAC** | None | Use PWM instead |
| **GPIO** | 45 pins (36 usable) | Flexible pin muxing |
| **UART** | 3 controllers | Hardware serial ports |
| **SPI** | 4 controllers | High-speed peripherals |
| **I2C** | 2 controllers | Sensor communication |
| **PWM** | 8 channels | LED Control (LEDC) |
| **Timers** | 4× 64-bit general timers | Microsecond precision |
| **RTC** | Real-time clock | Low-power timekeeping |

---

## 📌 GPIO Matrix & Pin Capabilities

### What Makes ESP32-S3 Special

Unlike traditional MCUs with fixed peripheral pins, the ESP32-S3 has a **GPIO Matrix** that allows routing almost any peripheral to any pin:

```
┌─────────────────────────────────────────┐
│        GPIO Matrix (Crossbar)           │
│                                         │
│  UART0 ─────┬──────────────┬─► GPIO 1  │
│  UART1 ─────┤              ├─► GPIO 2  │
│  SPI2  ─────┤   Flexible   ├─► GPIO 5  │
│  I2C0  ─────┤   Routing    ├─► GPIO 6  │
│  LEDC  ─────┴──────────────┴─► GPIO 45 │
│                                         │
└─────────────────────────────────────────┘
```

**Example:**
```cpp
// UART can be on almost ANY pin!
Serial1.begin(115200, SERIAL_8N1, 
              1,  // RX on GPIO 1
              2); // TX on GPIO 2

// Or different pins:
Serial1.begin(115200, SERIAL_8N1,
              17, // RX on GPIO 17
              18); // TX on GPIO 18
```

### GPIO Pin Categories

#### ✅ Safe General Purpose Pins
**Best for stepper control:**
```
GPIO: 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21
```

**Characteristics:**
- ✅ No boot mode conflicts
- ✅ Full input/output capability
- ✅ Internal pull-up/pull-down available
- ✅ Safe for any digital I/O

**Pins Used in This Project:**
| GPIO | Function | Notes |
|------|----------|-------|
| 1 | UART1 RX | TMC2209 communication |
| 2 | UART1 TX | TMC2209 communication |
| 4 | STEP | Stepper step signal |
| 5 | DIR | Stepper direction |
| 6 | ENABLE | Stepper driver enable |
| 8 | DC Motor FI | H-bridge Forward Input |
| 9 | DC Motor BI | H-bridge Backward Input |

#### ⚠️ Strapping Pins (Use with Caution)
```
GPIO 0:  Boot mode (LOW = download, HIGH = normal)
GPIO 3:  JTAG enable
GPIO 45: VDD_SPI voltage (1.8V/3.3V select)
GPIO 46: Boot mode
```

**Can be used AFTER boot**, but:
- Don't connect to hardware that pulls LOW during power-on
- May interfere with firmware updates
- Test boot behavior before deploying

#### ❌ Reserved/Avoid Pins
```
GPIO 19, 20:  USB D-/D+ (breaks USB if used!)
GPIO 43, 44:  UART0 TX/RX (Serial console)
GPIO 26-32:   SPI flash (used for program storage)
GPIO 33-37:   Octal SPI (on some modules)
```

**ESP32-S3 Super Mini Specifics:**
```
GPIO 48:  Addressable RGB LED (WS2812)
GPIO 35:  Often connected to 5V rail (check schematic!)
```

---

## ⚡ UART Peripherals

### Three Independent UART Controllers

| UART | Default Pins | Usage in Project | Notes |
|------|-------------|------------------|-------|
| **UART0** | GPIO 43 (TX), 44 (RX) | USB Serial console | Used by Serial object |
| **UART1** | GPIO 1 (RX), 2 (TX) | TMC2209 communication | Configured in this project |
| **UART2** | Configurable | Available for expansion | Not used |

> **Note:** With UART1 using GPIO 1 and 2, GPIO 8 and 9 remain available for other purposes such as DC motor H-bridge control (FI/BI signals).

### UART1 Configuration for TMC2209

```cpp
#define SERIAL_PORT Serial1  // Use UART1 controller

void setup() {
    // Initialize on custom pins
    SERIAL_PORT.begin(
        115200,      // Baud rate
        SERIAL_8N1,  // 8 data, no parity, 1 stop
        1,           // RX pin (GPIO 1)
        2            // TX pin (GPIO 2)
    );
}
```

**UART Parameters Explained:**

| Parameter | Value | Meaning |
|-----------|-------|---------|
| Baud Rate | 115200 | Bits per second |
| Data Bits | 8 | Standard byte size |
| Parity | N (None) | No error checking bit |
| Stop Bits | 1 | Standard framing |

### Hardware UART Features

1. **FIFO Buffers**
   - 128 bytes TX FIFO
   - 128 bytes RX FIFO
   - Reduces CPU interrupts

2. **DMA Support**
   - Direct Memory Access transfers
   - Zero CPU overhead for large data
   - Not used in simple stepper control

3. **Auto-Baud Detection**
   - Can auto-detect baud rate
   - Useful for unknown devices

4. **Interrupts**
   ```cpp
   SERIAL_PORT.onReceive([]() {
       // Called when data arrives
   });
   ```

---

## 🎛️ GPIO Digital I/O

### Output Modes

```cpp
pinMode(STEP_PIN, OUTPUT);        // Push-pull output
pinMode(ENABLE_PIN, OUTPUT);      // Can source/sink current

digitalWrite(STEP_PIN, HIGH);     // 3.3V output
digitalWrite(STEP_PIN, LOW);      // 0V output
```

**Output Characteristics:**
- **Voltage Levels:** 0V (LOW), 3.3V (HIGH)
- **Current Capability:** 40mA source/sink per pin (12mA recommended)
- **Total Current:** 200mA max across all pins
- **Speed:** ~2MHz toggle rate with digitalWrite()

### Input Modes

```cpp
pinMode(pin, INPUT);              // High impedance
pinMode(pin, INPUT_PULLUP);       // Internal 45kΩ pull-up
pinMode(pin, INPUT_PULLDOWN);     // Internal 45kΩ pull-down

int value = digitalRead(pin);     // Read state
```

**Use Cases:**
- `INPUT_PULLUP`: For switches (button to GND)
- `INPUT_PULLDOWN`: For open-collector devices
- `INPUT`: For external pull resistors

### Advanced GPIO Features

```cpp
// Interrupt on pin change
attachInterrupt(digitalPinToInterrupt(pin), ISR, RISING);

void ISR() {
    // Interrupt service routine
    // Keep short! No Serial.print(), delay(), etc.
}
```

**Interrupt Modes:**
- `RISING` - LOW to HIGH transition
- `FALLING` - HIGH to LOW transition
- `CHANGE` - Any state change
- `LOW` - Continuous while LOW
- `HIGH` - Continuous while HIGH

---

## ⏱️ Timing & Timers

### Software Timing Functions

#### delay() - Millisecond Delays
```cpp
delay(100);  // Block for 100ms
```
- ✅ Simple to use
- ❌ Blocks all code execution
- ❌ Wastes CPU cycles

#### delayMicroseconds() - Microsecond Delays
```cpp
delayMicroseconds(1000);  // Block for 1ms
```
- ✅ Higher precision
- ❌ Still blocking
- ❌ ±10% accuracy on ESP32-S3
- ⚠️ Not accurate below 3µs

#### millis() - Non-blocking Timing
```cpp
unsigned long start = millis();
if (millis() - start > 1000) {
    // 1 second elapsed
}
```
- ✅ Non-blocking
- ✅ Good for timeouts
- ⚠️ Overflows every ~49 days

#### micros() - Microsecond Counter
```cpp
unsigned long start = micros();
if (micros() - start > 1000) {
    // 1ms elapsed
}
```
- ✅ Higher resolution
- ✅ Non-blocking
- ⚠️ Overflows every ~71 minutes

### Hardware Timers (Advanced)

**Four 64-bit General Purpose Timers:**

```cpp
hw_timer_t *timer = NULL;

void IRAM_ATTR onTimer() {
    // Timer interrupt handler
    digitalWrite(STEP_PIN, !digitalRead(STEP_PIN));
}

void setup() {
    // Timer 0, prescaler 80 (1MHz), count up
    timer = timerBegin(0, 80, true);
    
    // Attach interrupt function
    timerAttachInterrupt(timer, &onTimer, true);
    
    // Trigger every 1000 ticks (1ms at 1MHz)
    timerAlarmWrite(timer, 1000, true);
    
    // Enable timer
    timerAlarmEnable(timer);
}
```

**Timer Configuration:**
| Parameter | Value | Meaning |
|-----------|-------|---------|
| Timer ID | 0-3 | Which timer to use |
| Prescaler | 80 | APB_CLK (80MHz) ÷ 80 = 1MHz |
| Count Up | true | Increment (false = decrement) |
| Alarm Value | 1000 | Trigger at this count |
| Auto-reload | true | Restart after trigger |

**Benefits for Stepper Control:**
- ✅ Precise timing (no jitter)
- ✅ Background execution
- ✅ CPU free for other tasks
- ✅ Constant step rate

---

## 🔌 Power & Current Considerations

### Power Supply Requirements

**ESP32-S3 Super Mini:**
```
Input Voltage:    5V (USB) or 3.3V (direct)
Operating Range:  3.0V - 3.6V (internal)
Typical Current:  80mA (WiFi off)
                  180mA (WiFi active)
Peak Current:     500mA (WiFi TX)
```

**For TMC2209 Integration:**
```
Logic Power (VIO):   3.3V @ 50mA (from ESP32)
Motor Power (VS):    12-28V @ 2A+ (separate PSU)

┌──────────────────────────────────────┐
│  USB 5V                              │
│    ↓                                 │
│  [LDO Regulator] → 3.3V              │
│    ├─→ ESP32-S3                      │
│    └─→ TMC2209 VIO pin               │
│                                      │
│  Separate PSU                        │
│    └─→ 12-24V → TMC2209 VS pin       │
│                                      │
│  Common GND !! (critical)            │
└──────────────────────────────────────┘
```

### GPIO Current Limits

**Per Pin:**
- **Recommended:** 12mA continuous
- **Absolute Maximum:** 40mA
- **Total (all pins):** 200mA

**For Stepper Driver:**
- STEP, DIR, ENABLE pins: <1mA each (TMC2209 inputs)
- Safe to drive directly from ESP32 GPIO

**Warning:** Never drive motors directly!
```cpp
// ❌ NEVER DO THIS
digitalWrite(MOTOR_PIN, HIGH);  // Can't source motor current!

// ✅ DO THIS
digitalWrite(STEP_PIN, HIGH);   // Signal to driver IC
// Driver IC provides motor current
```

---

## 📡 Communication Peripherals

### I2C (Two Controllers)

```cpp
#include <Wire.h>

// Default I2C on GPIO 8 (SDA), 9 (SCL) - but we use these for DC motor!
// Use custom pins instead:
Wire.begin(21, 22);  // SDA=21, SCL=22

// Read from device
Wire.beginTransmission(0x50);  // Device address
Wire.write(0x00);              // Register
Wire.endTransmission();
Wire.requestFrom(0x50, 1);
uint8_t data = Wire.read();
```

**Use Cases:**
- External sensors (temperature, accelerometer)
- OLED displays
- EEPROMs
- Multi-driver coordination

### SPI (Four Controllers)

```cpp
#include <SPI.h>

// Default SPI pins
SPI.begin();  // SCK=36, MISO=37, MOSI=35, SS=34

// Custom pins
SPI.begin(SCK, MISO, MOSI, SS);

// Transfer data
digitalWrite(SS_PIN, LOW);
uint8_t response = SPI.transfer(0x42);
digitalWrite(SS_PIN, HIGH);
```

**Use Cases:**
- SD card logging
- High-speed sensors
- Display panels
- Not used with TMC2209 UART mode

---

## 🎚️ ADC (Analog to Digital Converter)

### Two 12-bit SAR ADCs

```cpp
int adcValue = analogRead(GPIO_PIN);
// Returns: 0-4095 (12-bit)

// Convert to voltage
float voltage = adcValue * (3.3 / 4095.0);
```

**Specifications:**
- **Resolution:** 12-bit (4096 levels)
- **Range:** 0 - 3.3V (with attenuation)
- **Channels:** 20 total (ADC1 + ADC2)
- **Speed:** ~40kHz sample rate

**Attenuation Settings:**
```cpp
analogSetAttenuation(ADC_11db);  // 0-3.3V range
analogSetAttenuation(ADC_6db);   // 0-2.2V range
analogSetAttenuation(ADC_2_5db); // 0-1.5V range
analogSetAttenuation(ADC_0db);   // 0-1.0V range
```

**Use Cases for Stepper Systems:**
- Current sensing (shunt resistor)
- Temperature monitoring (thermistor)
- Position feedback (potentiometer)
- Supply voltage monitoring

**ADC2 Limitation:**
⚠️ ADC2 cannot be used when WiFi is active!
```cpp
// These pins use ADC2 (avoid if using WiFi):
// GPIO 11, 12, 13, 14
```

---

## 🔄 PWM (Pulse Width Modulation)

### LED Control (LEDC) Peripheral

**8 Independent Channels:**

```cpp
// Configure PWM channel
ledcSetup(
    0,        // Channel 0-7
    5000,     // Frequency (Hz)
    8         // Resolution (bits)
);

// Attach to pin
ledcAttachPin(GPIO_PIN, 0);  // Channel 0

// Set duty cycle
ledcWrite(0, 128);  // 50% duty (0-255 for 8-bit)
```

**PWM Parameters:**

| Parameter | Range | Use Case |
|-----------|-------|----------|
| Frequency | 1Hz - 40MHz | Servo: 50Hz, LED: 5kHz, Motor: 20kHz |
| Resolution | 1-16 bits | Higher res = lower max frequency |
| Duty Cycle | 0-100% | 0=off, 50=half, 100=full |

**Frequency vs Resolution Trade-off:**
```
Max Frequency = 80MHz / (2^resolution)

8-bit  (0-255):   312 kHz max
10-bit (0-1023):  78 kHz max
16-bit (0-65535): 1.2 kHz max
```

**Use Cases:**
- LED dimming
- Servo control (50Hz)
- Cooling fan speed
- DAC replacement (low-pass filter)

---

## 🧠 CPU & Performance

### Dual-Core Architecture

```
┌─────────────────────────────────────┐
│  ESP32-S3                           │
│                                     │
│  ┌──────────┐      ┌──────────┐    │
│  │ Core 0   │      │ Core 1   │    │
│  │ 240 MHz  │      │ 240 MHz  │    │
│  │          │      │          │    │
│  │ Protocol │      │   App    │    │
│  │  Stack   │      │   Code   │    │
│  └──────────┘      └──────────┘    │
│       │                 │           │
│       └────────┬────────┘           │
│                │                    │
│           Shared RAM                │
│           Shared Flash              │
└─────────────────────────────────────┘
```

**Default Arduino Setup:**
- **Core 0:** WiFi, BLE, protocol stack
- **Core 1:** User code (setup/loop)

**Manual Core Assignment:**
```cpp
void Task1(void *pvParameters) {
    while(1) {
        // Task code
        vTaskDelay(10);
    }
}

void setup() {
    xTaskCreatePinnedToCore(
        Task1,     // Function
        "Task1",   // Name
        10000,     // Stack size
        NULL,      // Parameters
        1,         // Priority
        NULL,      // Handle
        0          // Core (0 or 1)
    );
}
```

### Performance Benchmarks

**digitalWrite() Speed:**
```cpp
// Toggle test (ESP32-S3 @ 240MHz)
digitalWrite(pin, HIGH);
digitalWrite(pin, LOW);

Measured: ~0.5µs per call
Max toggle rate: ~1 MHz
```

**Integer Math:**
```cpp
int result = a * b + c;  // ~10 nanoseconds
float result = a * b;    // ~50 nanoseconds (no FPU)
```

**Memory Access:**
- **IRAM (internal):** Single cycle
- **DRAM (internal):** Single cycle
- **Flash (external):** ~40 cycles (cache helps)

---

## 💾 Memory Architecture

### Memory Map

```
┌─────────────────────────────────────┐
│  Address     Size      Purpose      │
├─────────────────────────────────────┤
│  0x4037_0000  512 KB   Internal RAM │  ← Fast!
│  0x3FC8_8000  384 KB   DRAM0        │
│  0x3FC0_0000  16 KB    RTC Slow     │  ← Retains in deep sleep
│  0x4200_0000  4 MB     External     │  ← Program storage
│                        Flash        │
└─────────────────────────────────────┘
```

**Memory Types:**

1. **IRAM (Instruction RAM)**
   - Fastest execution
   - Place ISRs here
   ```cpp
   void IRAM_ATTR myISR() { }
   ```

2. **DRAM (Data RAM)**
   - Fast variable access
   - 512 KB total
   - Shared between cores

3. **Flash**
   - Program storage
   - Slower access
   - 4 MB typical
   - Can store data (SPIFFS/LittleFS)

4. **RTC Memory**
   - Survives deep sleep
   - 16 KB
   ```cpp
   RTC_DATA_ATTR int bootCount = 0;
   ```

### Memory Usage Example

```cpp
// Stack (local variables): ~4KB default
void myFunction() {
    int localVar;  // On stack
}

// Heap (dynamic allocation): Remaining RAM
String *str = new String("Hello");  // Heap

// Global variables: Static in DRAM
int globalVar = 0;  // DRAM

// Program code: Flash (cached to IRAM)
void myCode() { }  // Flash → IRAM cache
```

---

## 🌐 WiFi Capabilities

### WiFi Modes

```cpp
#include <WiFi.h>

// Station mode (connect to router)
WiFi.mode(WIFI_STA);
WiFi.begin("SSID", "password");

// Access point mode (create hotspot)
WiFi.mode(WIFI_AP);
WiFi.softAP("ESP32-Motor", "password");

// Both simultaneously
WiFi.mode(WIFI_AP_STA);
```

**Use Cases for Stepper Control:**
- Web interface for motor control
- MQTT for remote commands
- OTA firmware updates
- Remote diagnostics
- Multi-device coordination

**Example: Web Server Control**
```cpp
#include <WebServer.h>

WebServer server(80);

void handleRotate() {
    int steps = server.arg("steps").toInt();
    rotateMotor(true, steps, 1000);
    server.send(200, "text/plain", "OK");
}

void setup() {
    WiFi.begin("SSID", "pass");
    server.on("/rotate", handleRotate);
    server.begin();
}

void loop() {
    server.handleClient();
}
```

---

## 🔋 Power Modes

### Power Consumption Optimization

| Mode | Current | Wake-up | Use Case |
|------|---------|---------|----------|
| **Active** | 80-180mA | N/A | Normal operation |
| **Modem Sleep** | 20-30mA | Instant | WiFi off, CPU on |
| **Light Sleep** | 0.8mA | <10ms | CPU off, RTC on |
| **Deep Sleep** | 10µA | 100ms+ | Long idle periods |

**Modem Sleep (WiFi off):**
```cpp
WiFi.disconnect(true);
WiFi.mode(WIFI_OFF);
// Current drops from 180mA → 80mA
```

**Light Sleep:**
```cpp
esp_sleep_enable_timer_wakeup(1000000);  // 1 second
esp_light_sleep_start();
// Wakes up after 1s
```

**Deep Sleep:**
```cpp
esp_sleep_enable_timer_wakeup(10 * 1000000);  // 10s
esp_deep_sleep_start();
// ESP32 resets after wake-up (runs setup() again)
```

**RTC GPIO Wake-up:**
```cpp
esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 1);  // Wake on HIGH
esp_deep_sleep_start();
```

---

## 🛡️ Watchdog Timers

### Built-in Safety Features

**Task Watchdog Timer (TWDT):**
```cpp
// Enable watchdog (5 second timeout)
esp_task_wdt_init(5, true);
esp_task_wdt_add(NULL);  // Monitor current task

void loop() {
    // Reset watchdog (prevent reboot)
    esp_task_wdt_reset();
    
    // Your code here
}
```

**Interrupt Watchdog Timer (IWDT):**
- Automatically monitors interrupt handlers
- Ensures ISRs don't block too long
- Can't be disabled (hardware protection)

**Use Case:**
Prevent system lockup if motor control code hangs.

---

## 🧪 Debugging Features

### Built-in JTAG Debugger

**ESP32-S3 has built-in USB JTAG:**
- No external debugger needed!
- Set breakpoints in VSCode
- Step through code
- Inspect variables

**PlatformIO Configuration:**
```ini
[env:esp32-s3]
debug_tool = esp-builtin
debug_speed = 12000
```

### Serial Debugging

```cpp
Serial.printf("Value: %d, Voltage: %.2f\n", value, voltage);

// Multiple serial ports
Serial.println("Debug console");    // UART0
Serial1.println("TMC2209 command"); // UART1
```

**Advanced: Remote Logging**
```cpp
#include <RemoteDebug.h>

RemoteDebug Debug;

void setup() {
    Debug.begin("ESP32-Motor");
}

void loop() {
    debugI("Motor speed: %d", speed);  // Info level
    debugE("Error detected!");         // Error level
    Debug.handle();
}
```

---

## 🚀 ESP32-S3 vs Other MCUs

### Comparison Table

| Feature | ESP32-S3 | Arduino Uno | STM32F103 | Raspberry Pi Pico |
|---------|----------|-------------|-----------|-------------------|
| CPU | 2×240MHz | 1×16MHz | 1×72MHz | 2×133MHz |
| RAM | 512 KB | 2 KB | 20 KB | 264 KB |
| Flash | 4 MB | 32 KB | 64 KB | 2 MB |
| WiFi | ✅ | ❌ | ❌ | ❌ |
| Bluetooth | ✅ | ❌ | ❌ | ❌ |
| USB | Native | Via UART | Via UART | Native |
| Price | ~$3 | ~$25 | ~$2 | ~$4 |
| Power | 180mA | 50mA | 40mA | 30mA |

**Why ESP32-S3 for Stepper Control?**
- ✅ Fast enough for high step rates
- ✅ WiFi for remote control
- ✅ Abundant RAM for buffers
- ✅ Multiple UARTs for multi-driver
- ✅ Cheap and widely available

---

## 📚 Additional Resources

**Official Documentation:**
- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)

**Arduino Core:**
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [Arduino API Reference](https://www.arduino.cc/reference/en/)

**Community:**
- [ESP32 Forum](https://www.esp32.com/)
- [Reddit r/esp32](https://www.reddit.com/r/esp32/)

---

**Next:** See [tmc2209-guide.md](tmc2209-guide.md) for TMC2209 driver capabilities
