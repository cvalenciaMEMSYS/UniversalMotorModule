# Universal Motor Module - Quick Wiring Guide
## TMC2209 v1.3 + ESP32-S3 Super Mini (GPIO 1,2,4,5,6)

---

## 📌 PIN ASSIGNMENT SUMMARY

```
ESP32-S3          TMC2209 v1.3
GPIO Pin          Board Pin         Function
--------          ---------         --------
GPIO 4     →      EN (left)         Enable (LOW=enabled)
GPIO 5     →      STEP (left)       Step pulses
GPIO 6     →      DIR (left)        Direction
GPIO 1     →      RX/TX (left)      UART (see options below)
GPIO 2     →      RX/TX (left)      UART (see options below)
GPIO 8     →      FI (Pin 2)        Forward Input (DC motor H-bridge)
GPIO 9     →      BI (Pin 1)        Backward Input (DC motor H-bridge)
3.3V       →      VIO (right)       Logic power
GND        →      GND (right)       Common ground
12-28V     →      VS (right)        Motor power

Note: RZ7899-MS VCC (Pin 4) connects to motor power (3-25V), NOT ESP32!
```

---

## 🔌 UART WIRING - TWO OPTIONS

### ✅ OPTION 1: Single Wire (TESTED & WORKING)

```
     ESP32-S3                                TMC2209 v1.3
    
     GPIO 1 (TX_PIN) ──[1kΩ]── GPIO 2 (RX_PIN)
                                   │
     TMC2209 PDN_UART/RX pin ←─────┘ (left side)
     
     TX pin (left side) = Leave unconnected
```

**What you need:**
- 1× 1kΩ resistor (brown-black-red or 102) between GPIO 1 and GPIO 2
- 1× wire from GPIO 2 directly to TMC2209 PDN_UART/RX pin
- TMC2209 TX pin left floating

**⚠️ IMPORTANT:** This method was confirmed working after testing. The dual-wire method (Option 2) did NOT work.

---

### ❌ OPTION 2: Dual Wire (NOT WORKING - DO NOT USE)

```
     ESP32-S3                           TMC2209 v1.3
    
     GPIO 1  ─────────────────────────── TX pin (left side)
     
     GPIO 2  ─────────────────────────── RX pin (left side)
```

**What you need:**
- 2× wires
- NO external resistor needed

**⚠️ NOTE:** This method was tested and did NOT work. Use Option 1 instead.

---

## � DC MOTOR WIRING (RZ7899-MS H-Bridge)

The RZ7899-MS is a powerful H-bridge driver for DC brushed motors with PWM speed control.

### Specifications
- **Supply Voltage (VCC):** 3.0V - 25.0V (same as motor power!)
- **Peak Current:** 6A (short duration)
- **Continuous Current:** 3-5A (with heatsink for higher currents)
- **Standby Current:** ≤2µA
- **Thermal Shutdown:** 130°C

### Pinout (SOP8/DIP8)
```
        ┌────────────┐
   BI ─┤ 1        8 ├─ BO (Backward Output)
   FI ─┤ 2        7 ├─ BO (Backward Output)
  GND ─┤ 3        6 ├─ FO (Forward Output)
  VCC ─┤ 4        5 ├─ FO (Forward Output)
        └────────────┘
```

### Pin Connections

```
     ESP32-S3                           RZ7899-MS H-Bridge
    
     GPIO 8  ──────────────────────────► FI (Pin 2 - Forward Input)
     
     GPIO 9  ──────────────────────────► BI (Pin 1 - Backward Input)
     
     GND     ──────────────────────────► GND (Pin 3)


     Motor Power Supply (3-25V)         RZ7899-MS H-Bridge
    
     V+ (3-25V) ──────────────────────► VCC (Pin 4) - NOT 3.3V from ESP32!
     
     GND ─────────────────────────────► GND (Pin 3)
         └────────────────────────────► ESP32 GND (common ground!)


     DC Motor                           RZ7899-MS H-Bridge
    
     Motor wire 1 ────────────────────► FO (Pins 5+6 tied together)
     
     Motor wire 2 ────────────────────► BO (Pins 7+8 tied together)
```

**⚠️ IMPORTANT:** VCC is motor power (3-25V), NOT logic power from ESP32! The input pins accept 3.3V logic signals.

### Control Logic (Truth Table)

| FI (Pin 2) | BI (Pin 1) | Motor Action |
|------------|------------|--------------|
| HIGH | LOW | Forward rotation |
| LOW | HIGH | Backward rotation |
| HIGH | HIGH | Brake (motor stops) |
| LOW | LOW | Coast (freewheeling) |

**Note:** This module uses coast mode (both LOW) for stopping by default.

---

## �🔧 COMPLETE WIRING CHECKLIST

### Power Connections:
- [ ] **VS (TMC right top)** ← 12-28V power supply (+)
- [ ] **VIO (TMC right bottom)** ← ESP32 3.3V pin
- [ ] **GND (TMC right, both pins)** ← Common ground with ESP32 and power supply
- [ ] **100µF capacitor** across VS and GND (near driver) - IMPORTANT!

### Control Connections:
- [ ] **EN (TMC left)** ← ESP32 GPIO 4
- [ ] **STEP (TMC left)** ← ESP32 GPIO 5
- [ ] **DIR (TMC left)** ← ESP32 GPIO 6

### UART Connection (Use Option 1 ONLY):
**✅ Option 1 (WORKING):**
- [ ] 1kΩ resistor connected between ESP32 GPIO 1 and GPIO 2
- [ ] ESP32 GPIO 2 connects directly to TMC2209 PDN_UART/RX pin
- [ ] TMC2209 TX pin left unconnected

**❌ Option 2 (NOT WORKING - DO NOT USE):**
- ~~ESP32 GPIO 1 → TMC2209 TX pin~~
- ~~ESP32 GPIO 2 → TMC2209 RX pin~~

### Motor Connections:
- [ ] **A1 & A2 (TMC right)** ← Motor Coil A
- [ ] **B1 & B2 (TMC right)** ← Motor Coil B

### DC Motor H-Bridge (RZ7899-MS):
- [ ] **FI (Pin 2)** ← ESP32 GPIO 8
- [ ] **BI (Pin 1)** ← ESP32 GPIO 9
- [ ] **VCC (Pin 4)** ← Motor power supply (3-25V) - NOT 3.3V from ESP32!
- [ ] **GND (Pin 3)** ← Common ground with ESP32 and motor power supply
- [ ] **FO (Pins 5+6)** → Tie together, connect to DC motor wire 1
- [ ] **BO (Pins 7+8)** → Tie together, connect to DC motor wire 2
- [ ] **100µF capacitor** near VCC pin (recommended)

### Optional:
- [ ] MS1 & MS2 = Leave OPEN (floating) for address 0b00
- [ ] DIAG (top) = Optional for StallGuard detection

---

## 🎯 VISUAL WIRING DIAGRAM (Option 1 - Recommended)

```
┌────────────────────────────────────────────────────────────────┐
│  ESP32-S3 SUPER MINI                                           │
│  ┌──────────────┐                                              │
│  │              │                                              │
│  │  3.3V  ──────┼───────────────────────────┐                 │
│  │  GND   ──────┼────────────────────┐      │                 │
│  │  GPIO 1 ─────┼──┬─/\/\/\─┐ (1kΩ)  │      │                 │
│  │  GPIO 2 ─────┼──┘        │        │      │                 │
│  │  GPIO 4 ─────┼───────────┼────┐   │      │                 │
│  │  GPIO 5 ─────┼───────────┼──┐ │   │      │                 │
│  │  GPIO 6 ─────┼───────────┼┐ │ │   │      │                 │
│  └──────────────┘           ││ │ │   │      │                 │
│                             ││ │ │   │      │                 │
│  (GPIO 2 connects to TMC)   ││ │ │   │      │                 │
│                    ┌────────┘│ │ │   │      │                 │
│                    │         │ │ │   │      │                 │
└────────────────────┼─────────┼─┼─┼───┼──────┼─────────────────┘
                     │         │ │ │   │      │
                     │         │ │ │   │      │
┌────────────────────┼─────────┼─┼─┼───┼──────┼─────────────────┐
│  TMC2209 v1.3      │         │ │ │   │      │                 │
│                    │         │ │ │   │      │                 │
│  LEFT SIDE         │         │ │ │   │      │  RIGHT SIDE     │
│  ┌─────────────────┼─────────┼─┼─┼───┼──────┼───────────┐     │
│  │ EN    ○◄────────┼─────────┼─┼─┘   │      │           │     │
│  │ MS1   ○         │         │ │     │      │           │     │
│  │ MS2   ○         │         │ │     │      │           │     │
│  │ RX    ○◄────────┘ (GPIO 2)│ │     │      │  ○ VS────┼──► 12-28V
│  │ TX    ○  (NOT CONNECTED)  │ │     │      │  ○ GND───┼──► PSU GND
│  │ CLK   ○                   │ │     │      │  ○ A2────┼──► Motor A
│  │ STEP  ○◄──────────────────┘ │     │      │  ○ A1────┼──► Motor A
│  │ DIR   ○◄────────────────────┘     │      │  ○ B1────┼──► Motor B
│  └───────────────────────────────────┤      │  ○ B2────┼──► Motor B
│                                      │      │  ○ VIO───┼──► 3.3V
│                                      └──────┤  ○ GND───┼──► ESP GND
│                                             └───────────┘     │
└────────────────────────────────────────────────────────────────┘

Note: 1kΩ resistor is between GPIO 1 and GPIO 2.
      GPIO 2 connects DIRECTLY to TMC2209 PDN_UART/RX pin.
      GPIO 1 does NOT connect to TMC2209 (only to resistor).
```

---

## ⚙️ ARDUINO CODE SETTINGS

Make sure your code has these pin definitions:

```cpp
#define STEP_PIN        5
#define DIR_PIN         6
#define ENABLE_PIN      4
#define RX_PIN          2        // UART receive from TMC2209
#define TX_PIN          1        // UART transmit to TMC2209

// DC Motor H-Bridge (RZ7899-MS)
#define DC_FI_PIN       8        // Forward Input
#define DC_BI_PIN       9        // Backward Input
```

---

## 🧪 TESTING PROCEDURE

### 1. Power Check (BEFORE connecting motor):
```cpp
// Upload sketch and open Serial Monitor (115200 baud)
// Look for: "✓ TMC2209 Connection Successful!"
```

### 2. If connection fails:
- Check all GND connections (must be common!)
- Verify VIO has 3.3V
- Try swapping UART Option 1 ↔ Option 2
- Check 1kΩ resistor value (if using Option 1)

### 3. Motor Test:
- Press '1' in Serial Monitor → Motor should rotate clockwise
- Press '2' → Motor should rotate counter-clockwise
- Press 'h' → Show all available commands

### 4. Configuration Test:
- Press 'd' → Display full diagnostics
- Check "RMS Current" matches your motor specs
- Verify no error flags (shorts, overtemperature)

### 5. DC Motor Test (if using H-bridge):
- Press 'f' → DC motor should spin forward
- Press 'b' → DC motor should spin backward
- Press 'o' → DC motor should stop (coast)
- Press 'p', enter '200' → Set higher speed

---

## 🚨 TROUBLESHOOTING

| Problem | Solution |
|---------|----------|
| No UART communication | Try Option 2 wiring instead of Option 1 |
| Motor not moving | Check EN pin is LOW, verify current setting |
| Motor stuttering | Increase current with 'c' command |
| Motor too noisy | Press '7' to enable StealthChop mode |
| Overheating | Add heatsink, reduce current, add cooling |

---

## 📝 NOTES

- **Current Setting**: Start with 70-80% of your motor's rated current
  - Example: 1.4A motor → Set 1000mA RMS (press 'c' and enter 1000)
  
- **Microstepping**: Default is 16 microsteps (good balance)
  - Change with '9' command if needed
  
- **Mode Selection**:
  - StealthChop ('7') = Ultra quiet, good for low speeds
  - SpreadCycle ('8') = Maximum torque, audible noise
  
- **EN Pin**: LOW = driver enabled, HIGH = driver disabled
  - Motor has no holding torque when disabled (freewheels)

---

## ✅ FINAL CHECKLIST BEFORE POWERING ON

- [ ] All GND pins connected to common ground
- [ ] VIO connected to 3.3V
- [ ] VS connected to 12-28V (with 100µF cap)
- [ ] Motor coils A and B properly identified and connected
- [ ] UART pins wired according to chosen option
- [ ] EN, STEP, DIR pins connected correctly
- [ ] No shorts between pins (multimeter check)
- [ ] Power supply can handle motor current draw

**Ready to test!** 🚀

