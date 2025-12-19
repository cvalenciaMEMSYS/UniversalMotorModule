# TMC2209 v1.3 + ESP32-S3 Super Mini - Quick Wiring Guide
## Custom Pin Assignment: GPIO 1,2,4,5,6

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
3.3V       →      VIO (right)       Logic power
GND        →      GND (right)       Common ground
12-28V     →      VS (right)        Motor power
```

---

## 🔌 UART WIRING - TWO OPTIONS

### ✅ OPTION 1: Single Wire (TESTED & WORKING)

```
     ESP32-S3                           TMC2209 v1.3
    
     GPIO 1  ────┬
                 ├──── 1kΩ resistor ──── RX pin (left side)
     GPIO 2  ────┘
     
     TX pin (left side) = Leave unconnected
```

**What you need:**
- 1× 1kΩ resistor (brown-black-red or 102)
- 1× wire from resistor to TMC2209 RX pin
- 2× wires from GPIO 1 and 2 to resistor

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

## 🔧 COMPLETE WIRING CHECKLIST

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
- [ ] ESP32 GPIO 1 & 2 connected together
- [ ] Junction connects through 1kΩ resistor to TMC2209 RX pin
- [ ] TMC2209 TX pin left unconnected

**❌ Option 2 (NOT WORKING - DO NOT USE):**
- ~~ESP32 GPIO 1 → TMC2209 TX pin~~
- ~~ESP32 GPIO 2 → TMC2209 RX pin~~

### Motor Connections:
- [ ] **A1 & A2 (TMC right)** ← Motor Coil A
- [ ] **B1 & B2 (TMC right)** ← Motor Coil B

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
│  │  3.3V  ──────┼──────────────┐                              │
│  │  GND   ──────┼──────┐       │                              │
│  │  GPIO 1 ─────┼────┐ │       │                              │
│  │  GPIO 2 ─────┼──┐ │ │       │                              │
│  │  GPIO 4 ─────┼──┼─┼─┼───┐   │                              │
│  │  GPIO 5 ─────┼──┼─┼─┼─┐ │   │                              │
│  │  GPIO 6 ─────┼──┼─┼─┼─┼─┐   │                              │
│  └──────────────┘  │ │ │ │ │ │ │                              │
│                    │ │ │ │ │ │ │                              │
│          1kΩ       │ │ │ │ │ │ │                              │
│         ┌─/\/\/\─┬─┘ │ │ │ │ │ │                              │
│         │        └───┘ │ │ │ │ │                              │
│         │              │ │ │ │ │                              │
└─────────┼──────────────┼─┼─┼─┼─┼──────────────────────────────┘
          │              │ │ │ │ │
          │         ┌────┘ │ │ │ │
┌─────────┼─────────┼──────┼─┼─┼─┼──────────────────────────────┐
│  TMC2209 v1.3     │      │ │ │ │                              │
│                   │      │ │ │ │                              │
│  LEFT SIDE        │      │ │ │ │        RIGHT SIDE            │
│  ┌────────────────┼──────┼─┼─┼─┼────────────────┐             │
│  │ EN    ○◄───────┼──────┼─┼─┼─┼──────┐         │             │
│  │ MS1   ○        │      │ │ │ │      │         │             │
│  │ MS2   ○        │      │ │ │ │      │         │             │
│  │ RX    ○◄───────┘      │ │ │ │      │  ○ VS──┼──► 12-28V   │
│  │ TX    ○  (unused)     │ │ │ │      │  ○ GND─┼──► PSU GND  │
│  │ CLK   ○               │ │ │ │      │  ○ A2──┼──► Motor A  │
│  │ STEP  ○◄──────────────┼─┼─┘ │      │  ○ A1──┼──► Motor A  │
│  │ DIR   ○◄──────────────┼─┘   │      │  ○ B1──┼──► Motor B  │
│  └───────────────────────┘     │      │  ○ B2──┼──► Motor B  │
│                                 │      │  ○ VIO─┼──► 3.3V     │
│                                 └──────┤  ○ GND─┼──► ESP GND  │
│                                        └─────────┘             │
└────────────────────────────────────────────────────────────────┘
```

---

## ⚙️ ARDUINO CODE SETTINGS

Make sure your code has these pin definitions:

```cpp
#define STEP_PIN        5
#define DIR_PIN         6
#define ENABLE_PIN      4
#define RX_PIN          1
#define TX_PIN          2
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

