# DC Motor Control Guide - RZ7899-MS H-Bridge

Comprehensive documentation for DC brushed motor control using the MSKSEMI RZ7899-MS H-bridge driver.

---

## 🔧 Hardware Overview

### RZ7899-MS Specifications

| Parameter | Value | Notes |
|-----------|-------|-------|
| **Supply Voltage (VCC)** | 3.0V - 25.0V | Same as motor power! No separate logic |
| **Absolute Max Voltage** | 28V | Never exceed |
| **Peak Current** | 6A | Short duration |
| **Continuous Current** | 3-5A | With heatsink for higher currents |
| **Standby Current** | ≤2µA | Very low power when idle |
| **Operating Temperature** | -25°C to +85°C | |
| **Thermal Shutdown** | 130°C | Auto protection |
| **Control Mode** | PWM or Logic | Both inputs support PWM |
| **Package** | SOP-8 / DIP-8 | SMD or through-hole |
| **Built-in Protection** | Over-current, thermal, short circuit | Auto-shutdown on fault |

### Key Features

- Wide supply voltage range (3.0V - 25.0V)
- Built-in brake function
- Thermal shutdown protection
- Short circuit protection
- Over current limit function
- Low standby current (≤2µA)
- **No separate logic voltage needed!** VCC is motor power

---

## 📌 Pin Descriptions

### RZ7899-MS Pinout (SOP8/DIP8)

```
        ┌────────────┐
   BI ─┤ 1        8 ├─ BO (Backward Output)
   FI ─┤ 2        7 ├─ BO (Backward Output)
  GND ─┤ 3        6 ├─ FO (Forward Output)
  VCC ─┤ 4        5 ├─ FO (Forward Output)
        └────────────┘
```

| Pin | Name | Function | Connection |
|-----|------|----------|------------|
| 1 | BI | Backward Input | ← ESP32 GPIO 9 |
| 2 | FI | Forward Input | ← ESP32 GPIO 8 |
| 3 | GND | Ground | ← Common ground |
| 4 | VCC | Power Supply | ← Motor power (3-25V) |
| 5 | FO | Forward Output | → DC Motor wire 1 (tie with pin 6) |
| 6 | FO | Forward Output | → DC Motor wire 1 (tie with pin 5) |
| 7 | BO | Backward Output | → DC Motor wire 2 (tie with pin 8) |
| 8 | BO | Backward Output | → DC Motor wire 2 (tie with pin 7) |

**IMPORTANT:** 
- Pins 5 & 6 (FO) must be connected together to the motor
- Pins 7 & 8 (BO) must be connected together to the motor
- VCC is motor power (3-25V), NOT 3.3V logic from ESP32!

---

## ⚡ Wiring Guide

### Complete Connection Diagram

```
ESP32-S3 Super Mini              RZ7899-MS H-Bridge
─────────────────────            ──────────────────

     GPIO 8  ────────────────────► FI (Pin 2 - Forward Input)
     
     GPIO 9  ────────────────────► BI (Pin 1 - Backward Input)
     
     GND     ────────────────────► GND (Pin 3)


Motor Power Supply (3-25V)       RZ7899-MS H-Bridge
──────────────────────────       ──────────────────

     V+ (3-25V) ─────────────────► VCC (Pin 4)
     
     GND ────────────────────────► GND (Pin 3)
         └───────────────────────► ESP32 GND (common ground!)


DC Motor                         RZ7899-MS H-Bridge
────────                         ──────────────────

     Motor wire 1 ───────────────► FO (Pins 5+6 tied together)
     
     Motor wire 2 ───────────────► BO (Pins 7+8 tied together)
```

### Wiring Checklist

- [ ] **VCC (Pin 4)** ← Motor power supply (3-25V) - NOT from ESP32!
- [ ] **GND (Pin 3)** ← Common ground with ESP32 and power supply
- [ ] **FI (Pin 2)** ← ESP32 GPIO 8
- [ ] **BI (Pin 1)** ← ESP32 GPIO 9
- [ ] **FO (Pins 5+6)** → Tie together, connect to DC motor wire 1
- [ ] **BO (Pins 7+8)** → Tie together, connect to DC motor wire 2
- [ ] **Common ground** between motor power supply and ESP32
- [ ] **Bulk capacitor** 100µF near VCC pin (recommended for power filtering)

**⚠️ IMPORTANT:** The RZ7899-MS does NOT need 3.3V logic power from ESP32! The VCC pin takes motor voltage (3-25V) directly. The input pins (FI/BI) accept 3.3V logic signals from ESP32.

---

## 🎛️ Control Logic

### Truth Table

| FI (Pin 2) | BI (Pin 1) | FO (Pins 5,6) | BO (Pins 7,8) | Motor Action |
|------------|------------|---------------|---------------|--------------|
| HIGH | LOW | HIGH | LOW | Forward |
| LOW | HIGH | LOW | HIGH | Backward |
| HIGH | HIGH | LOW | LOW | Brake (motor stops) |
| LOW | LOW | Open | Open | Coast (freewheeling) |

### PWM Speed Control

The RZ7899-MS supports PWM on both inputs for variable speed control:

```cpp
// Forward at 50% speed
ledcWrite(DC_FI_PIN, 512);  // 512/1023 = ~50%
ledcWrite(DC_BI_PIN, 0);

// Backward at 75% speed
ledcWrite(DC_FI_PIN, 0);
ledcWrite(DC_BI_PIN, 768);  // 768/1023 = ~75%

// Coast stop
ledcWrite(DC_FI_PIN, 0);
ledcWrite(DC_BI_PIN, 0);
```

**PWM Parameters Used:**
- **Frequency:** 20 kHz (above audible range)
- **Resolution:** 10-bit (0-1023 duty cycle)

---

## 💻 Code Implementation

### Pin Definitions

```cpp
// DC Motor H-Bridge (RZ7899-MS) pins
#define DC_FI_PIN       8        // Forward Input (PWM)
#define DC_BI_PIN       9        // Backward Input (PWM)
```

### Initialization

```cpp
void setup() {
    // Configure pins as outputs
    pinMode(DC_FI_PIN, OUTPUT);
    pinMode(DC_BI_PIN, OUTPUT);
    
    // Attach PWM (ESP32-S3 LEDC)
    ledcAttach(DC_FI_PIN, 20000, 10);  // 20kHz, 10-bit resolution
    ledcAttach(DC_BI_PIN, 20000, 10);
    
    // Start with motor stopped (coast)
    ledcWrite(DC_FI_PIN, 0);
    ledcWrite(DC_BI_PIN, 0);
}
```

### Control Functions

```cpp
/**
 * Control DC motor direction and speed
 * @param forward true = forward, false = backward
 * @param speed PWM duty cycle (0-1023)
 */
void dcMotorControl(bool forward, int speed) {
    speed = constrain(speed, 0, 1023);
    
    if (forward) {
        ledcWrite(DC_BI_PIN, 0);
        ledcWrite(DC_FI_PIN, speed);
    } else {
        ledcWrite(DC_FI_PIN, 0);
        ledcWrite(DC_BI_PIN, speed);
    }
}

/**
 * Stop DC motor (coast mode)
 */
void dcMotorStop() {
    ledcWrite(DC_FI_PIN, 0);
    ledcWrite(DC_BI_PIN, 0);
}

/**
 * Brake DC motor (active stop)
 */
void dcMotorBrake() {
    ledcWrite(DC_FI_PIN, 1023);
    ledcWrite(DC_BI_PIN, 1023);
}
```

---

## 🎮 Serial Commands

| Command | Action | Example Output |
|---------|--------|----------------|
| `run forward` | Run forward at current speed | `DC Motor: Running forward` |
| `run backward` | Run backward at current speed | `DC Motor: Running backward` |
| `stop` | Stop motor (coast) | `DC Motor: Coast - freewheeling` |
| `brake` | Active brake (motor locked) | `DC Motor: Brake - motor locked` |
| `move <ms>` | Run for duration (milliseconds) | Forward: positive, Reverse: negative |
| `abs <percent>` | Set speed as percentage (-100 to +100) | `DC Motor: Speed set to 80%` |
| `set speed <val>` | Set max speed limit (0-100%) | `Max speed limited to 80%` |

### Example Session

```
> run forward
DC Motor: Running forward

> set speed 80
Max speed limited to 80%

> abs 50
DC Motor: Speed set to 50% (target: 0.40)

> run backward
DC Motor: Running backward

> stop
DC Motor: Coast (IN1=0, IN2=0) - freewheeling
```

---

## 🔧 ESP32-S3 LEDC PWM Details

### Why 20 kHz?

- **Above audible range:** No annoying motor whine
- **Good efficiency:** Not too fast for the H-bridge
- **Smooth control:** Fine-grained speed adjustment

### Alternative Frequencies

| Frequency | Pros | Cons |
|-----------|------|------|
| 1 kHz | Simple, works everywhere | Audible whine |
| 10 kHz | Quiet, efficient | May have slight noise |
| 20 kHz | Inaudible, smooth | Good default choice |
| 50 kHz | Very smooth | May reduce efficiency |

### Resolution Trade-offs

| Resolution | Speed Levels | Precision |
|------------|--------------|-----------|
| 8-bit | 256 | Good for most motors |
| 10-bit | 1024 | High precision |
| 12-bit | 4096 | Very fine control |

**10-bit is used** to provide fine-grained speed control (0-1023 range).

---

## 🚨 Troubleshooting

### Motor Not Running

1. **Check power:**
   - VCC connected to motor power supply (3-25V)?
   - NOT 3.3V from ESP32 - VCC needs motor voltage!
   - All GND pins connected (common ground with ESP32)?
   - FO pins 5+6 tied together to motor?
   - BO pins 7+8 tied together to motor?

2. **Check control signals:**
   - GPIO 8 connected to FI (Pin 2)?
   - GPIO 9 connected to BI (Pin 1)?
   - No shorts between pins?

3. **Test with commands:**
   - Send `run forward` — does the motor spin?
   - Try `abs 100` to set full speed forward

### Motor Runs Wrong Direction

- Swap motor wires (FO ↔ BO)
- Or swap GPIO 8 and 9 in code

### Motor Stutters or Jerks

1. **Add capacitors:**
   - 100µF on VCC line (near chip)

2. **Check power supply:**
   - Can it handle motor current (up to 3-5A)?
   - Is voltage stable?

3. **Reduce speed:**
   - Some motors don't start well at low PWM
   - Try minimum speed of 50-80

### H-Bridge Gets Hot

1. **Check for shorts**
2. **Reduce motor current** (smaller motor or lower voltage)
3. **Add heatsink** if running at high currents (>2A continuous)
4. **Check for stalled motor** (blocked rotation)
5. **Thermal shutdown** at 130°C is normal protection

---

## ⚠️ Important Notes

### Motor Selection

The RZ7899-MS is rated for:
- **Voltage:** 3.0V - 25.0V motors
- **Current:** Up to 3-5A continuous (with heatsink), 6A peak

**Suitable motors:**
- Small to medium DC motors
- Hobby motors
- Gear motors
- Most 12V/24V DC motors under 5A

**Current limits:**
- 3A continuous without heatsink
- 5A continuous with heatsink
- 6A peak (short bursts)

### Brake vs Coast

| Mode | Behavior | Use Case |
|------|----------|----------|
| **Coast** | Motor freewheels | Smooth stopping, momentum ok |
| **Brake** | Motor locked | Quick stop, position holding |

**Default is coast** (`o` command). Brake mode can be added if needed.

### Simultaneous Operation

The Universal Motor Module is designed for **mutually exclusive** operation:
- Run the stepper motor OR the DC motor
- Not both simultaneously
- Future firmware may add mode selection via hardware pins

---

## 📚 References

- [RZ7899 Datasheet](https://www.lcsc.com/datasheet/lcsc_datasheet_1809192242_MSKSEMI-RZ7899_C90478.pdf)
- [ESP32-S3 LEDC Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/ledc.html)
- [H-Bridge Theory](https://en.wikipedia.org/wiki/H-bridge)

---

**Document Author:** GitHub Copilot  
**Last Updated:** January 2026  
**Hardware:** ESP32-S3 Super Mini + RZ7899-MS H-Bridge
