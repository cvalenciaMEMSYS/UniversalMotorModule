# TMC2209 Stepper Driver - Complete Technical Guide

Comprehensive documentation of Trinamic TMC2209 silent stepper driver capabilities, configuration, and advanced features.

---

## 🔧 Hardware Overview

### TMC2209 Specifications

| Parameter | Value | Notes |
|-----------|-------|-------|
| **Motor Voltage (VM)** | 4.75 - 29V | Optimally 12-24V |
| **Motor Current** | Up to 2A RMS | 2.8A peak |
| **Logic Voltage (VIO)** | 3.0 - 5.25V | Typically 3.3V or 5V |
| **Microstepping** | 1 to 256 | Software configurable |
| **Interface** | UART, Step/Dir | Single-wire UART |
| **StallGuard** | ✅ Yes | Sensorless homing |
| **CoolStep** | ✅ Yes | Energy optimization |
| **StealthChop** | ✅ Yes | Ultra-quiet operation |
| **SpreadCycle** | ✅ Yes | High torque mode |
| **Sense Resistor (v1.3)** | 0.11Ω | Determines max current |
| **Package** | QFN32 | SMD IC |

---

## 🏗️ BigTreeTech TMC2209 v1.3 Board

### Board Features

```
LEFT SIDE:           RIGHT SIDE:
┌─────────┐          ┌─────────┐
│ EN      │          │ VS      │  12-28V motor power
│ MS1     │          │ GND     │  Power ground
│ MS2     │          │ A2      │  Motor coil A
│ RX      │          │ A1      │  Motor coil A
│ TX      │          │ B1      │  Motor coil B
│ CLK     │          │ B2      │  Motor coil B
│ STEP    │          │ VIO     │  3.3V/5V logic
│ DIR     │          │ GND     │  Logic ground
└─────────┘          └─────────┘

TOP:
┌─────────┐
│ DIAG    │  StallGuard output
└─────────┘
```

### Pin Descriptions

#### Control Pins (Left Side)

| Pin | Function | Description |
|-----|----------|-------------|
| **EN** | Enable | LOW = enabled, HIGH = disabled (low power) |
| **MS1** | Address Bit 0 | UART address selection (leave open for 0b00) |
| **MS2** | Address Bit 1 | UART address selection (leave open for 0b00) |
| **RX** | UART Receive | Data from MCU (has 0Ω resistor to PDN_UART) |
| **TX** | UART Transmit | Data to MCU (has 1kΩ resistor to PDN_UART) |
| **CLK** | External Clock | Optional external clock input (not used) |
| **STEP** | Step Input | Rising edge = one microstep |
| **DIR** | Direction | HIGH/LOW = rotation direction |

#### Power & Motor Pins (Right Side)

| Pin | Function | Description |
|-----|----------|-------------|
| **VS** | Motor Power | 12-28V DC (add 100µF capacitor!) |
| **GND** | Power Ground | Must be common with VIO ground |
| **A1, A2** | Coil A | Bipolar motor coil A |
| **B1, B2** | Coil B | Bipolar motor coil B |
| **VIO** | Logic Power | 3.3V or 5V from MCU |
| **GND** | Logic Ground | Common ground with VS |

#### Diagnostic Pin (Top)

| Pin | Function | Description |
|-----|----------|-------------|
| **DIAG** | Diagnostics | StallGuard output (active LOW on stall) |

---

## 📡 UART Communication

### Single-Wire UART Protocol

The TMC2209 uses **PDN_UART** (single-wire half-duplex):

```
         MCU                    TMC2209 v1.3
                              
    TX ────────────┐         ┌─[0Ω]─ RX pin
                   │         │
                   ├─────────┼─► PDN_UART (internal pin)
                   │         │
    RX ────────────┘         └─[1kΩ]─ TX pin
                              
    Both MCU TX/RX           Built-in resistors
    tied together            on v1.3 board
```

### UART Address Selection

**MS1/MS2 Pin Configuration:**

| MS1 | MS2 | Address | Binary |
|-----|-----|---------|--------|
| Open | Open | 0 | 0b00 |
| GND | Open | 1 | 0b01 |
| Open | VIO | 2 | 0b10 |
| GND | VIO | 3 | 0b11 |

**Multi-Driver Setup:**
```cpp
// Driver 1 (MS1=MS2=OPEN)
TMC2209Stepper driver1(&Serial1, 0.11, 0b00);

// Driver 2 (MS1=GND, MS2=OPEN)
TMC2209Stepper driver2(&Serial1, 0.11, 0b01);

// All on same UART bus!
```

### UART Frame Format

**Write Register:**
```
[Sync] [Slave] [Register] [Data] [CRC]
 0x05    0x00    0x6C      0xXX    0xXX
```

**Read Register:**
```
Request:  [Sync] [Slave] [Register] [CRC]
          0x05    0x00    0xEC       0xXX

Response: [Sync] [Master] [Register] [Data] [CRC]
          0x05    0xFF     0x6C       0xXX    0xXX
```

**TMCStepper Library Handles All Communication!**

---

## 🎚️ Register Configuration

### Essential Registers

#### 1. GCONF (Global Configuration)
```cpp
driver.I_scale_analog(false);  // Use internal current reference
driver.internal_Rsense(false); // Use external sense resistor
driver.en_spreadCycle(false);  // StealthChop mode
driver.shaft(false);           // Normal direction
driver.index_otpw(false);      // DIAG=StallGuard (not temp)
driver.index_step(false);      // INDEX pin disabled
driver.pdn_disable(true);      // UART mode (not standalone)
driver.mstep_reg_select(true); // Microsteps via UART
driver.multistep_filt(true);   // Pulse filtering
```

#### 2. CHOPCONF (Chopper Configuration)
```cpp
driver.toff(5);                // Chopper off-time (1-15)
driver.hstrt(5);               // Hysteresis start (0-7)
driver.hend(3);                // Hysteresis end (-3 to 12)
driver.tbl(2);                 // Blank time (0-3)
driver.mres(4);                // Microsteps (0=256, 8=1)
```

**TOFF Chopper Frequency:**
```
f_PWM = f_CLK / (32 × TOFF)

TOFF=5 → 35 kHz (silent, efficient)
TOFF=3 → 58 kHz (very silent, less efficient)
TOFF=8 → 22 kHz (more power, audible)
```

#### 3. IHOLD_IRUN (Current Configuration)
```cpp
driver.irun(31);               // Run current (0-31 scale)
driver.ihold(16);              // Hold current (0-31 scale)
driver.iholddelay(6);          // Delay to hold current
```

**Current Calculation:**
```
I_RMS = (CS + 1) / 32 × (V_FS / (R_SENSE × √2))

For R_SENSE = 0.11Ω, V_FS = 0.325V:
I_RMS = (CS + 1) / 32 × 2.09 A

CS=31 (max) → 2.09 A
CS=15       → 1.05 A
CS=7        → 0.52 A
```

**Helper Function:**
```cpp
driver.rms_current(800);  // Set 800mA RMS
// Library calculates CS automatically!
```

#### 4. TPOWERDOWN
```cpp
driver.tpowerdown(20);  // Delay to standby (0-255)
// Time = TPOWERDOWN × 2^18 / f_CLK ≈ 20 × 2.6ms = 52ms
```

---

## 🔊 StealthChop™ - Silent Operation

### How It Works

**Traditional Stepper Drivers:**
- Fixed PWM frequency (~20kHz)
- Audible noise from coil vibration
- Torque ripple

**StealthChop:**
- Voltage-mode chopper
- Adapts to motor back-EMF
- Completely silent operation
- Up to 80% quieter than SpreadCycle

### Configuration

```cpp
driver.en_spreadCycle(false);  // Enable StealthChop
driver.pwm_autoscale(true);    // Auto-tune PWM amplitude
driver.pwm_autograd(true);     // Auto-tune PWM gradient
driver.pwm_freq(1);            // PWM frequency factor
driver.pwm_grad(4);            // Initial gradient
driver.pwm_ofs(36);            // Initial offset
```

**PWM Frequency:**
```cpp
driver.pwm_freq(0);  // f_PWM = 2/1024 × f_CLK = 27 kHz
driver.pwm_freq(1);  // f_PWM = 2/683 × f_CLK  = 35 kHz (default)
driver.pwm_freq(2);  // f_PWM = 2/512 × f_CLK  = 46 kHz
driver.pwm_freq(3);  // f_PWM = 2/410 × f_CLK  = 59 kHz
```

**Auto-Tuning:**
```cpp
driver.pwm_autoscale(true);  // Automatically adjusts PWM amplitude
driver.pwm_autograd(true);   // Automatically adapts to motor
```

### When to Use StealthChop

✅ **Good for:**
- 3D printers (silent printing)
- Camera gimbals
- Low/medium speed applications
- Home/office environments

❌ **Not ideal for:**
- Very high speeds (>400 RPM)
- Maximum torque requirement
- Heavily loaded systems

---

## 💪 SpreadCycle™ - High Torque Mode

### How It Works

**Constant off-time current chopper:**
- Fast decay mode
- Maximum torque delivery
- Predictable behavior
- Audible operation

### Configuration

```cpp
driver.en_spreadCycle(true);   // Enable SpreadCycle
driver.toff(5);                // Chopper off-time
driver.hstrt(5);               // Hysteresis start value
driver.hend(3);                // Hysteresis end value
driver.tbl(2);                 // Blank time
```

**Fast Decay:**
```cpp
driver.fd3(false);             // Use decay time from TFD
driver.disfdcc(false);         // Enable fast decay comparator
```

### When to Use SpreadCycle

✅ **Good for:**
- High torque applications
- High speeds (>400 RPM)
- CNC machines
- Heavy loads

❌ **Not ideal for:**
- Quiet environments
- Battery-powered devices
- Low-speed precision

---

## 🎯 StallGuard™ - Sensorless Homing

### Principle

**Load Detection Without Sensors:**
- Monitors back-EMF during stepping
- Compares to expected value
- Detects motor stall condition
- Real-time feedback

### Configuration

```cpp
// Enable StallGuard below this velocity
driver.TCOOLTHRS(0xFFFFF);  // Max value = always enabled

// Stall detection threshold
driver.SGTHRS(10);  // 0=sensitive, 255=insensitive

// Stall detection filter
driver.sfilt(true);  // Enable filtering
```

**Reading StallGuard:**
```cpp
uint32_t drv_status = driver.DRV_STATUS();
int16_t sg_result = (drv_status & 0x3FF);  // 10-bit value

if (sg_result < driver.SGTHRS()) {
    Serial.println("Motor stalled!");
}
```

**DIAG Pin Output:**
```cpp
driver.index_otpw(false);  // DIAG = StallGuard (not temp)

// DIAG pin goes LOW when SG_RESULT < SGTHRS
// Connect to MCU interrupt pin for instant detection
```

### Sensorless Homing Example

```cpp
void homingSequence() {
    driver.SGTHRS(5);  // Sensitive threshold
    driver.TCOOLTHRS(0xFFFFF);  // Enable StallGuard
    
    // Move toward home until stall
    digitalWrite(DIR_PIN, LOW);  // Home direction
    
    while (true) {
        uint32_t drv_status = driver.DRV_STATUS();
        int16_t sg = (drv_status & 0x3FF);
        
        if (sg < 5) {
            Serial.println("Home position reached!");
            break;
        }
        
        // Step motor
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(500);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(500);
    }
    
    // Back off a bit
    digitalWrite(DIR_PIN, HIGH);
    for (int i = 0; i < 100; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(1000);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(1000);
    }
    
    currentPosition = 0;  // Set home
}
```

### StallGuard Tuning

**Threshold Values:**
- **0-50:** Very sensitive (detects light touch)
- **50-100:** Medium sensitivity (normal use)
- **100-200:** Low sensitivity (heavy loads)
- **200-255:** Very insensitive (rarely triggers)

**Factors Affecting StallGuard:**
- Motor speed (works best at medium speeds)
- Motor current (higher = better detection)
- Load (adjust threshold for your application)
- Motor type (some motors work better than others)

---

## ⚡ CoolStep™ - Energy Optimization

### Adaptive Current Control

**Automatic Current Regulation:**
- Monitors motor load via StallGuard
- Reduces current when load is light
- Increases current when load is heavy
- Saves power and reduces heat

### Configuration

```cpp
// Enable CoolStep below this velocity
driver.TCOOLTHRS(0xFFFFF);  // Always active

// Lower threshold (reduce current)
driver.semin(5);  // 0=disable, 1-15=threshold

// Upper threshold (increase current)
driver.semax(2);  // 0-15

// Current increment/decrement
driver.seup(1);    // Current increment step
driver.sedn(0b01); // Current decrement speed
```

**CoolStep Algorithm:**
```
if (SG_RESULT > semin × 32) {
    // Load is light → reduce current
    current -= sedn_step;
}

if (SG_RESULT < semin × 32 / 2) {
    // Load is heavy → increase current
    current += seup_step;
}
```

### Power Savings Example

**Without CoolStep:**
```
Motor current: 1.2A RMS (constant)
Power: 1.2A × 12V × 2 coils = 28.8W
```

**With CoolStep:**
```
Light load:  0.6A RMS → 14.4W (50% savings!)
Heavy load:  1.2A RMS → 28.8W (auto-boost)
Average:     ~0.9A   → ~21.6W (25% savings)
```

---

## 🌡️ Thermal Management

### Temperature Monitoring

**Built-in Temperature Sensor:**
```cpp
uint32_t drv_status = driver.DRV_STATUS();

bool overtemp = (drv_status >> 24) & 1;      // >150°C
bool overtemp_warn = (drv_status >> 23) & 1; // >120°C

if (overtemp) {
    Serial.println("⚠ CRITICAL: Overtemperature!");
    // Driver automatically reduces current
}

if (overtemp_warn) {
    Serial.println("⚠ WARNING: High temperature");
    // Consider adding cooling
}
```

**Thermal Shutdown:**
- **Warning:** 120°C (driver reduces current by 50%)
- **Shutdown:** 150°C (driver completely disabled)
- **Auto-recovery:** Yes (when temp < 120°C)

### Cooling Strategies

1. **Heatsink**
   ```
   Recommended size: 20×20×10mm
   Material: Aluminum
   Thermal paste: Required
   Temperature drop: ~30°C
   ```

2. **Active Cooling**
   ```cpp
   #define FAN_PIN 8
   
   void checkTemperature() {
       uint32_t status = driver.DRV_STATUS();
       if ((status >> 23) & 1) {
           digitalWrite(FAN_PIN, HIGH);  // Turn on fan
       } else {
           digitalWrite(FAN_PIN, LOW);   // Fan off
       }
   }
   ```

3. **Current Reduction**
   ```cpp
   void thermalProtection() {
       if (driver_overtemp) {
           driver.rms_current(currentSetting * 0.7);
           Serial.println("Current reduced to 70%");
       }
   }
   ```

---

## 🔍 Diagnostics & Status

### DRV_STATUS Register

**Complete Status Readout:**
```cpp
void fullDiagnostics() {
    uint32_t status = driver.DRV_STATUS();
    
    // StallGuard result (0-1023)
    uint16_t sg = status & 0x3FF;
    Serial.print("StallGuard: "); Serial.println(sg);
    
    // CS_ACTUAL (actual current setting)
    uint8_t cs = driver.cs_actual();
    Serial.print("Current Scale: "); Serial.println(cs);
    
    // Standstill detection
    bool standstill = (status >> 31) & 1;
    Serial.print("Standstill: "); Serial.println(standstill);
    
    // Open load indicators
    bool ola = (status >> 30) & 1;
    bool olb = (status >> 29) & 1;
    Serial.print("Open Load A: "); Serial.println(ola);
    Serial.print("Open Load B: "); Serial.println(olb);
    
    // Short circuit detection
    bool s2ga = (status >> 28) & 1;
    bool s2gb = (status >> 27) & 1;
    bool s2vsa = (status >> 26) & 1;
    bool s2vsb = (status >> 25) & 1;
    Serial.print("Short to GND A: "); Serial.println(s2ga);
    Serial.print("Short to GND B: "); Serial.println(s2gb);
    Serial.print("Short to VS A: "); Serial.println(s2vsa);
    Serial.print("Short to VS B: "); Serial.println(s2vsb);
    
    // Temperature warnings
    bool ot = (status >> 24) & 1;
    bool otpw = (status >> 23) & 1;
    Serial.print("Overtemp: "); Serial.println(ot);
    Serial.print("Overtemp Warn: "); Serial.println(otpw);
    
    // StealthChop indicator
    bool stealth = (status >> 22) & 1;
    Serial.print("StealthChop Active: "); Serial.println(stealth);
}
```

### Error Detection

**Common Errors:**

| Error | Cause | Solution |
|-------|-------|----------|
| Open Load | Motor disconnected | Check wiring |
| Short to GND | Phase shorted to ground | Check motor coils |
| Short to VS | Phase shorted to power | Replace motor |
| Overtemp | Excessive heat | Add cooling, reduce current |
| No communication | UART issue | Check RX/TX wiring |

---

## 📊 Microstepping Modes

### Resolution Comparison

| Mode | Microsteps | Degrees/Step | Steps/Rev | MRes Value |
|------|-----------|-------------|-----------|------------|
| Full Step | 1 | 1.8° | 200 | 8 |
| Half Step | 2 | 0.9° | 400 | 7 |
| 1/4 Step | 4 | 0.45° | 800 | 6 |
| 1/8 Step | 8 | 0.225° | 1600 | 5 |
| 1/16 Step | 16 | 0.1125° | 3200 | 4 |
| 1/32 Step | 32 | 0.05625° | 6400 | 3 |
| 1/64 Step | 64 | 0.028125° | 12800 | 2 |
| 1/128 Step | 128 | 0.0140625° | 25600 | 1 |
| 1/256 Step | 256 | 0.00703125° | 51200 | 0 |

### Setting Microstepping

```cpp
// Easy way (library calculates MRes)
driver.microsteps(16);  // 1/16 stepping

// Manual way (set MRes register)
driver.mres(4);  // MRes=4 → 16 microsteps
```

**Interpolation (MicroPlyer™):**
```cpp
driver.intpol(true);  // Enable 256× interpolation

// With intpol=true:
// - Set 16 microsteps
// - Internal interpolation to 256
// - Smoother motion
// - Still only need 16× step pulses
```

---

## 🛠️ Advanced Features

### Passive Braking

```cpp
driver.en_pwm_mode(true);   // Enable voltage PWM
driver.freewheel(0);        // Normal operation
// 0 = Normal
// 1 = Freewheel (coasting)
// 2 = Short LS (passive braking)
// 3 = Short HS (passive braking)
```

**Use Case:**
Stop motor quickly without active braking.

### PWM Gradient Auto-Tuning

```cpp
driver.pwm_autograd(true);  // Enable gradient adaptation
driver.pwm_autoscale(true); // Enable amplitude adaptation

// Driver automatically tunes for optimal performance
// No manual adjustment needed!
```

### DIAG Pin Configuration

```cpp
// Option 1: StallGuard output
driver.index_otpw(false);
driver.diag_push_pull(true);
// DIAG goes LOW when stall detected

// Option 2: Overtemperature output
driver.index_otpw(true);
driver.diag_push_pull(true);
// DIAG goes LOW when overtemp warning

// Read DIAG with MCU
pinMode(DIAG_PIN, INPUT_PULLUP);
bool stalled = !digitalRead(DIAG_PIN);
```

---

## 🎓 Best Practices

### 1. Current Setting

**Rule: Use 70-80% of motor rating**
```cpp
// Motor rating: 1.4A
// Set current: 1.0A (71%)
driver.rms_current(1000);
```

**Why?**
- Prevents overheating
- Extends motor life
- StealthChop works better
- Still >90% torque

### 2. Power Supply

**Voltage Selection:**
```
Motor voltage rating: 2.8V (typical NEMA 17)
Power supply: 12-24V

Recommended: 12V or 24V
- Higher voltage = higher max speed
- Lower heat in motor windings
- Better dynamic performance
```

**Decoupling:**
```
Add 100µF electrolytic cap close to VS pin
Add 100nF ceramic cap close to VS pin

Why? Filters motor switching noise
```

### 3. Grounding

**Critical: Common Ground**
```
ESP32 GND ──┬─► TMC2209 GND (logic)
            ├─► TMC2209 GND (power)
            └─► Power supply GND

All grounds MUST be connected!
```

### 4. Microstepping Selection

**Application-Specific:**
```
3D Printer:   16 or 32 microsteps (smooth, quiet)
CNC Router:   4 or 8 microsteps (speed, power)
Camera Pan:   64 or 128 microsteps (ultra-smooth)
High Speed:   4 microsteps (max speed)
```

### 5. Mode Selection

**StealthChop vs SpreadCycle:**

| Application | Mode | Reason |
|-------------|------|--------|
| 3D Printer | StealthChop | Silent, home use |
| CNC Mill | SpreadCycle | Torque, reliability |
| Camera Gimbal | StealthChop | Smooth, quiet |
| Robot Arm | SpreadCycle | Predictable, strong |

### 6. Thermal Management

**Always:**
- Add heatsink (even small one helps!)
- Ensure airflow
- Monitor temperature
- Reduce current if hot

---

## 🧪 Troubleshooting

### Motor Not Moving

**Check:**
1. EN pin is LOW (enabled)
2. STEP pulses arriving (oscilloscope/LED)
3. Current setting > 100mA
4. TOFF > 0 (driver enabled)
5. Motor connected correctly

**Test:**
```cpp
driver.rms_current(800);
driver.toff(5);
digitalWrite(ENABLE_PIN, LOW);
// Try rotating manually
```

### Motor Stuttering

**Causes:**
- Current too low
- Speed too high for microstep setting
- Power supply inadequate
- StallGuard threshold incorrect

**Solutions:**
```cpp
driver.rms_current(1200);  // Increase current
driver.microsteps(8);      // Reduce microsteps
driver.en_spreadCycle(true); // Try SpreadCycle
```

### Communication Failure

**Check:**
1. VIO = 3.3V (logic power)
2. GND common between ESP32 and TMC2209
3. RX/TX wiring correct
4. MS1/MS2 pins (address selection)
5. 1kΩ resistor (if using Option 1)

**Test:**
```cpp
uint8_t result = driver.test_connection();
Serial.print("Result: "); Serial.println(result);
// 0 = OK
// 1 = No response
```

### Excessive Heat

**Solutions:**
1. Reduce current
   ```cpp
   driver.rms_current(600);  // Was 1000mA
   ```
2. Enable CoolStep
   ```cpp
   driver.semin(5);
   driver.semax(2);
   ```
3. Add heatsink
4. Add cooling fan

---

## 📚 Additional Resources

**Official Documents:**
- [TMC2209 Datasheet](https://www.trinamic.com/fileadmin/assets/Products/ICs_Documents/TMC2209_Datasheet_V103.pdf)
- [TMC2209 Application Note](https://www.trinamic.com/fileadmin/assets/Support/AppNotes/AN013-StealthChop_Performance.pdf)

**TMCStepper Library:**
- [GitHub Repository](https://github.com/teemuatlut/TMCStepper)
- [API Documentation](https://teemuatlut.github.io/TMCStepper/)

**Community:**
- [Trinamic Forums](https://www.trinamic.com/support/forums/)
- [Reddit r/ender3](https://www.reddit.com/r/ender3/) (many TMC2209 users)

---

## 📖 Register Quick Reference

### Most Important Registers

```cpp
// Basic Setup
driver.begin();              // Initialize UART
driver.toff(5);              // Enable driver
driver.rms_current(800);     // Set current
driver.microsteps(16);       // Set microstepping

// StealthChop (Silent)
driver.en_spreadCycle(false);
driver.pwm_autoscale(true);
driver.pwm_autograd(true);

// SpreadCycle (Torque)
driver.en_spreadCycle(true);
driver.toff(5);
driver.hstrt(5);
driver.hend(3);

// StallGuard (Sensorless)
driver.TCOOLTHRS(0xFFFFF);
driver.SGTHRS(10);

// CoolStep (Energy)
driver.semin(5);
driver.semax(2);
driver.seup(1);
driver.sedn(1);

// Diagnostics
uint32_t status = driver.DRV_STATUS();
uint16_t sg = driver.SG_RESULT();
uint8_t current = driver.cs_actual();
```

---

**Next:** Return to [README.md](../README.md) for complete project overview
