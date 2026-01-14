# Energy Measurement Test Plan v2.0

## 1. Overview

### 1.1 Objectives

1. **Measure actuator consumption** - Power and energy at various operating conditions
2. **Measure actuator thrust/torque** - Force output at stall
3. **Measure actuator backdrive force** - Force required to backdrive the mechanism
4. *(Optional)* **Measure driver + motor consumption** - Total system efficiency

### 1.2 Target Hardware

- **Motors**: Micro stepper motors (~4g weight)
- **Expected Performance**:
  - Max Thrust: 50-500 g
  - Current Draw: 100-500 mA
  - Idle Power: 10-100 mW

---

## 2. Test Setup

### 2.1 Equipment

| Component | Description |
|-----------|-------------|
| **FD** | Custom Force Deflection setup - Linear actuator with load cell, Arduino + Raspberry Pi controlled |
| **DUT** | Device Under Test - Universal Motor Module with micro stepper |
| **JS** | Joulescope - Precision power analyzer |
| **Laptop** | Data collection (RealVNC for FD, USB for Joulescope) |

### 2.2 Setup Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        TEST SETUP                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│    ┌──────────┐       Force       ┌──────────┐                     │
│    │    FD    │ ────────────────► │   DUT    │                     │
│    │ (Custom  │                   │  (Motor  │                     │
│    │  Force   │◄───────────────── │  Module) │                     │
│    │ Deflect) │    Backdrive      │          │                     │
│    └────┬─────┘                   └────┬─────┘                     │
│         │                              │                           │
│    RealVNC                        JST Connector                    │
│         │                              │                           │
│         ▼                              ▼                           │
│    ┌──────────┐                  ┌──────────┐                      │
│    │  Rasp.   │                  │Joulescope│                      │
│    │   Pi     │                  │   (JS)   │                      │
│    └────┬─────┘                  └────┬─────┘                      │
│         │                             │                            │
│         │ Force Data                  │ V, I Data                  │
│         │                             │                            │
│         └──────────┬──────────────────┘                            │
│                    ▼                                               │
│              ┌──────────┐                                          │
│              │  Laptop  │                                          │
│              │ (Data    │                                          │
│              │ Logging) │                                          │
│              └──────────┘                                          │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

### 2.3 Connection Details

| Signal Path | Connection |
|-------------|------------|
| DUT Power | Adjustable PSU → DC connector on DUT (3V/4V/5V/6V) |
| DUT V/I Measurement | JST inline → Joulescope sense inputs |
| Joulescope Data | USB → Laptop |
| FD Control | WiFi → RealVNC → Raspberry Pi |
| FD Force Data | Raspberry Pi → Laptop (network) |

### 2.4 Pre-Test Checklist

- [ ] FD load cell calibrated and zeroed
- [ ] Joulescope connected and verified
- [ ] DUT motor securely mounted to bench
- [ ] DUT actuator aligned with FD load cell
- [ ] Power supply set to starting voltage (3V)
- [ ] Data logging software ready
- [ ] RealVNC connected to FD Raspberry Pi

---

## 3. Test Parameters

### 3.1 Speed Levels

| Code | Name | Value (steps/s) | Purpose |
|------|------|-----------------|---------|
| **VL** | Very Low | 100 | Low-speed precision |
| **L** | Low | 500 | Typical slow operation |
| **M** | Medium | 2,000 | Normal operation |
| **H** | High | 5,000 | Fast operation |
| **VH** | Very High | 10,000 | Maximum speed test |

### 3.2 Voltage Levels

| Voltage | Typical Use Case |
|---------|------------------|
| **3V** | Battery-powered minimum |
| **4V** | Li-ion single cell |
| **5V** | USB/Logic level |
| **6V** | Maximum realistic with EH |

### 3.3 Motion Profile Parameters

| Profile | Accel (steps/s²) | Jerk (cubesteps) | Description |
|---------|------------------|------------------|-------------|
| **Constant V** | — | — | Instant speed, no ramp |
| **Trapezoidal** | 2,500 | — | Linear acceleration ramp |
| **S-Curve** | 2,500 | 30 | Smooth jerk-limited ramp |

> **Note**: Fixed accel and jerk values chosen as intermediate starting points. If significant efficiency differences are found, a follow-up detailed optimization test will be conducted.

### 3.4 Test Count Summary

| Profile | Speeds | Voltages | Tests per Motor |
|---------|--------|----------|-----------------|
| Constant V | 5 | 4 | 20 |
| Trapezoidal | 5 | 4 | 20 |
| S-Curve | 5 | 4 | 20 |
| Backdrive | — | — | 1 |
| **Total** | — | — | **61** |

**Initial Testing**: 3 motors × 61 tests = **183 total tests**
**Full Testing**: 13 motors × 61 tests = **793 total tests**

---

## 4. Test Procedures

### 4.1 General Procedure (All Profile Tests)

1. **Setup**
   - Fix motor to desk/mount
   - Attach DUT actuator output to FD load cell
   - Connect Joulescope inline with DUT power

2. **Configure Motor**
   - Set motion profile (Constant/Trapezoidal/S-Curve)
   - Set speed parameter
   - Set voltage via PSU

3. **Execute Stall Test**
   - Command motor to move against FD (move to stall)
   - FD applies resistance until motor stalls

4. **Record Data**
   - Joulescope: Driver power, actuator energy
   - FD: Maximum thrust force
   - Timer: Move duration

5. **Repeat**
   - Cycle through all voltage levels at current speed
   - Move to next speed level
   - Repeat until all combinations complete

### 4.2 Backdrive Test Procedure

1. **Position at Mid-Stroke**
   - Move actuator to middle of travel

2. **Disable Motor**
   - Command: `disable`

3. **Apply Reverse Force**
   - Use FD to push against actuator output
   - Slowly increase force

4. **Record Backdrive Force**
   - Note force at which mechanism starts moving backward


---

## 5. Results Matrices

### 5.1 Test ID Format

Test IDs follow the format: `[Motor]-[Profile][Speed]-[Voltage]`

Examples:
- `M1-CV-3` = Motor 1, Constant V, Very Low speed, 3V
- `M2-TH-5` = Motor 2, Trapezoidal, High speed, 5V
- `M3-SL-6` = Motor 3, S-Curve, Low speed, 6V

Profile codes: **C** = Constant, **T** = Trapezoidal, **S** = S-Curve
Speed codes: **V** = Very Low, **L** = Low, **M** = Medium, **H** = High, **X** = Very High

---

## Motor 1: ____________________

### Constant Velocity Profile (set accel 0)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-CV-3 | 3V | | | | | |
| M1-CV-4 | 4V | | | | | |
| M1-CV-5 | 5V | | | | | |
| M1-CV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-CL-3 | 3V | | | | | |
| M1-CL-4 | 4V | | | | | |
| M1-CL-5 | 5V | | | | | |
| M1-CL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-CM-3 | 3V | | | | | |
| M1-CM-4 | 4V | | | | | |
| M1-CM-5 | 5V | | | | | |
| M1-CM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-CH-3 | 3V | | | | | |
| M1-CH-4 | 4V | | | | | |
| M1-CH-5 | 5V | | | | | |
| M1-CH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-CX-3 | 3V | | | | | |
| M1-CX-4 | 4V | | | | | |
| M1-CX-5 | 5V | | | | | |
| M1-CX-6 | 6V | | | | | |

---

### Trapezoidal Profile (set accel 2500)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-TV-3 | 3V | | | | | |
| M1-TV-4 | 4V | | | | | |
| M1-TV-5 | 5V | | | | | |
| M1-TV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-TL-3 | 3V | | | | | |
| M1-TL-4 | 4V | | | | | |
| M1-TL-5 | 5V | | | | | |
| M1-TL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-TM-3 | 3V | | | | | |
| M1-TM-4 | 4V | | | | | |
| M1-TM-5 | 5V | | | | | |
| M1-TM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-TH-3 | 3V | | | | | |
| M1-TH-4 | 4V | | | | | |
| M1-TH-5 | 5V | | | | | |
| M1-TH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-TX-3 | 3V | | | | | |
| M1-TX-4 | 4V | | | | | |
| M1-TX-5 | 5V | | | | | |
| M1-TX-6 | 6V | | | | | |

---

### S-Curve Profile (set accel 2500, set cubesteps 30)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-SV-3 | 3V | | | | | |
| M1-SV-4 | 4V | | | | | |
| M1-SV-5 | 5V | | | | | |
| M1-SV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-SL-3 | 3V | | | | | |
| M1-SL-4 | 4V | | | | | |
| M1-SL-5 | 5V | | | | | |
| M1-SL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-SM-3 | 3V | | | | | |
| M1-SM-4 | 4V | | | | | |
| M1-SM-5 | 5V | | | | | |
| M1-SM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-SH-3 | 3V | | | | | |
| M1-SH-4 | 4V | | | | | |
| M1-SH-5 | 5V | | | | | |
| M1-SH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M1-SX-3 | 3V | | | | | |
| M1-SX-4 | 4V | | | | | |
| M1-SX-5 | 5V | | | | | |
| M1-SX-6 | 6V | | | | | |

---

### Backdrive Test

| ID | Motor State | Backdrive Force (g) | Notes |
|----|-------------|---------------------|-------|
| M1-BD-OFF | Disabled | | |
| M1-BD-ON | Enabled (holding) | | |

---

### Motor 1 Summary

| Metric | Value |
|--------|-------|
| Motor Model | |
| Weight | ~4g |
| Idle Power (5V) | ___ mW |
| Max Thrust (best) | ___ g |
| Backdrive Force (disabled) | ___ g |
| Best Profile | |
| Optimal Voltage | |
| Optimal Speed | |

---

## Motor 2: ____________________

### Constant Velocity Profile (set accel 0)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-CV-3 | 3V | | | | | |
| M2-CV-4 | 4V | | | | | |
| M2-CV-5 | 5V | | | | | |
| M2-CV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-CL-3 | 3V | | | | | |
| M2-CL-4 | 4V | | | | | |
| M2-CL-5 | 5V | | | | | |
| M2-CL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-CM-3 | 3V | | | | | |
| M2-CM-4 | 4V | | | | | |
| M2-CM-5 | 5V | | | | | |
| M2-CM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-CH-3 | 3V | | | | | |
| M2-CH-4 | 4V | | | | | |
| M2-CH-5 | 5V | | | | | |
| M2-CH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-CX-3 | 3V | | | | | |
| M2-CX-4 | 4V | | | | | |
| M2-CX-5 | 5V | | | | | |
| M2-CX-6 | 6V | | | | | |

---

### Trapezoidal Profile (set accel 2500)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-TV-3 | 3V | | | | | |
| M2-TV-4 | 4V | | | | | |
| M2-TV-5 | 5V | | | | | |
| M2-TV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-TL-3 | 3V | | | | | |
| M2-TL-4 | 4V | | | | | |
| M2-TL-5 | 5V | | | | | |
| M2-TL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-TM-3 | 3V | | | | | |
| M2-TM-4 | 4V | | | | | |
| M2-TM-5 | 5V | | | | | |
| M2-TM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-TH-3 | 3V | | | | | |
| M2-TH-4 | 4V | | | | | |
| M2-TH-5 | 5V | | | | | |
| M2-TH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-TX-3 | 3V | | | | | |
| M2-TX-4 | 4V | | | | | |
| M2-TX-5 | 5V | | | | | |
| M2-TX-6 | 6V | | | | | |

---

### S-Curve Profile (set accel 2500, set cubesteps 30)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-SV-3 | 3V | | | | | |
| M2-SV-4 | 4V | | | | | |
| M2-SV-5 | 5V | | | | | |
| M2-SV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-SL-3 | 3V | | | | | |
| M2-SL-4 | 4V | | | | | |
| M2-SL-5 | 5V | | | | | |
| M2-SL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-SM-3 | 3V | | | | | |
| M2-SM-4 | 4V | | | | | |
| M2-SM-5 | 5V | | | | | |
| M2-SM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-SH-3 | 3V | | | | | |
| M2-SH-4 | 4V | | | | | |
| M2-SH-5 | 5V | | | | | |
| M2-SH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M2-SX-3 | 3V | | | | | |
| M2-SX-4 | 4V | | | | | |
| M2-SX-5 | 5V | | | | | |
| M2-SX-6 | 6V | | | | | |

---

### Backdrive Test

| ID | Motor State | Backdrive Force (g) | Notes |
|----|-------------|---------------------|-------|
| M2-BD-OFF | Disabled | | |
| M2-BD-ON | Enabled (holding) | | |

---

### Motor 2 Summary

| Metric | Value |
|--------|-------|
| Motor Model | |
| Weight | ~4g |
| Idle Power (5V) | ___ mW |
| Max Thrust (best) | ___ g |
| Backdrive Force (disabled) | ___ g |
| Best Profile | |
| Optimal Voltage | |
| Optimal Speed | |

---

## Motor 3: ____________________

### Constant Velocity Profile (set accel 0)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-CV-3 | 3V | | | | | |
| M3-CV-4 | 4V | | | | | |
| M3-CV-5 | 5V | | | | | |
| M3-CV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-CL-3 | 3V | | | | | |
| M3-CL-4 | 4V | | | | | |
| M3-CL-5 | 5V | | | | | |
| M3-CL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-CM-3 | 3V | | | | | |
| M3-CM-4 | 4V | | | | | |
| M3-CM-5 | 5V | | | | | |
| M3-CM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-CH-3 | 3V | | | | | |
| M3-CH-4 | 4V | | | | | |
| M3-CH-5 | 5V | | | | | |
| M3-CH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-CX-3 | 3V | | | | | |
| M3-CX-4 | 4V | | | | | |
| M3-CX-5 | 5V | | | | | |
| M3-CX-6 | 6V | | | | | |

---

### Trapezoidal Profile (set accel 2500)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-TV-3 | 3V | | | | | |
| M3-TV-4 | 4V | | | | | |
| M3-TV-5 | 5V | | | | | |
| M3-TV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-TL-3 | 3V | | | | | |
| M3-TL-4 | 4V | | | | | |
| M3-TL-5 | 5V | | | | | |
| M3-TL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-TM-3 | 3V | | | | | |
| M3-TM-4 | 4V | | | | | |
| M3-TM-5 | 5V | | | | | |
| M3-TM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-TH-3 | 3V | | | | | |
| M3-TH-4 | 4V | | | | | |
| M3-TH-5 | 5V | | | | | |
| M3-TH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-TX-3 | 3V | | | | | |
| M3-TX-4 | 4V | | | | | |
| M3-TX-5 | 5V | | | | | |
| M3-TX-6 | 6V | | | | | |

---

### S-Curve Profile (set accel 2500, set cubesteps 30)

#### Speed VL (100 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-SV-3 | 3V | | | | | |
| M3-SV-4 | 4V | | | | | |
| M3-SV-5 | 5V | | | | | |
| M3-SV-6 | 6V | | | | | |

#### Speed L (500 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-SL-3 | 3V | | | | | |
| M3-SL-4 | 4V | | | | | |
| M3-SL-5 | 5V | | | | | |
| M3-SL-6 | 6V | | | | | |

#### Speed M (2000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-SM-3 | 3V | | | | | |
| M3-SM-4 | 4V | | | | | |
| M3-SM-5 | 5V | | | | | |
| M3-SM-6 | 6V | | | | | |

#### Speed H (5000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-SH-3 | 3V | | | | | |
| M3-SH-4 | 4V | | | | | |
| M3-SH-5 | 5V | | | | | |
| M3-SH-6 | 6V | | | | | |

#### Speed VH (10000 steps/s)

| ID | Voltage | Thrust (g) | Driver Power (mW) | Actuator Power (mW) | Duration (ms) | Actuator Energy (mJ) |
|----|---------|------------|-------------------|---------------------|---------------|----------------------|
| M3-SX-3 | 3V | | | | | |
| M3-SX-4 | 4V | | | | | |
| M3-SX-5 | 5V | | | | | |
| M3-SX-6 | 6V | | | | | |

---

### Backdrive Test

| ID | Motor State | Backdrive Force (g) | Notes |
|----|-------------|---------------------|-------|
| M3-BD-OFF | Disabled | | |
| M3-BD-ON | Enabled (holding) | | |

---

### Motor 3 Summary

| Metric | Value |
|--------|-------|
| Motor Model | |
| Weight | ~4g |
| Idle Power (5V) | ___ mW |
| Max Thrust (best) | ___ g |
| Backdrive Force (disabled) | ___ g |
| Best Profile | |
| Optimal Voltage | |
| Optimal Speed | |

---

## 6. Overall Analysis

### 6.1 Profile Comparison (Best Results per Motor)

| Motor | Best Profile | Best Voltage | Best Speed | Max Thrust (g) | Min Energy (mJ) |
|-------|--------------|--------------|------------|----------------|-----------------|
| Motor 1 | | | | | |
| Motor 2 | | | | | |
| Motor 3 | | | | | |

### 6.2 Key Findings

1. **Most efficient profile overall**: _______________
2. **Energy savings vs. Constant V**: _______________% 
3. **Optimal voltage for thrust**: _______________V
4. **Optimal voltage for efficiency**: _______________V
5. **Speed sweet spot**: _______________ steps/s
6. **Backdrive behavior**: _______________

### 6.3 Recommendations

Based on test results:

| Application | Recommended Settings |
|-------------|---------------------|
| Max Thrust | Profile: ___, Voltage: ___V, Speed: ___ |
| Max Efficiency | Profile: ___, Voltage: ___V, Speed: ___ |
| Balanced | Profile: ___, Voltage: ___V, Speed: ___ |

---

## 7. Test Checklist

### Motor 1: _______________

- [ ] Setup verified
- [ ] Constant V tests complete (20)
- [ ] Trapezoidal tests complete (20)
- [ ] S-Curve tests complete (20)
- [ ] Backdrive test complete (1)
- [ ] Summary filled in

### Motor 2: _______________

- [ ] Setup verified
- [ ] Constant V tests complete (20)
- [ ] Trapezoidal tests complete (20)
- [ ] S-Curve tests complete (20)
- [ ] Backdrive test complete (1)
- [ ] Summary filled in

### Motor 3: _______________

- [ ] Setup verified
- [ ] Constant V tests complete (20)
- [ ] Trapezoidal tests complete (20)
- [ ] S-Curve tests complete (20)
- [ ] Backdrive test complete (1)
- [ ] Summary filled in

### Analysis

- [ ] Profile comparison complete
- [ ] Key findings documented
- [ ] Recommendations made
- [ ] Data backed up

---

## Appendix A: Commands Reference

### Profile Configuration

```
set accel 0           # Constant velocity (instant speed)
set accel 2500        # Trapezoidal (2500 steps/s²)
set cubesteps 30      # S-Curve jerk parameter
```

### Speed Configuration

```
set speed 100         # VL - Very Low
set speed 500         # L  - Low
set speed 2000        # M  - Medium
set speed 5000        # H  - High
set speed 10000       # VH - Very High
```

### Motion Commands

```
move <steps>          # Relative move
run forward           # Continuous rotation CW
run backward          # Continuous rotation CCW
stop                  # Immediate stop
brake                 # Decelerated stop
```

### Query Commands

```
status                # Full system status
get pos               # Current position
get speed             # Current speed
diag                  # TMC2209 diagnostics
```

---

## Appendix B: Motor Catalog (For Future Testing)

| # | Motor Model | Weight (g) | Mechanism | Status |
|---|-------------|------------|-----------|--------|
| 1 | | ~4g | | ☐ Not tested |
| 2 | | ~4g | | ☐ Not tested |
| 3 | | ~4g | | ☐ Not tested |
| 4 | | ~4g | | ☐ Not tested |
| 5 | | ~4g | | ☐ Not tested |
| 6 | | ~4g | | ☐ Not tested |
| 7 | | ~4g | | ☐ Not tested |
| 8 | | ~4g | | ☐ Not tested |
| 9 | | ~4g | | ☐ Not tested |
| 10 | | ~4g | | ☐ Not tested |
| 11 | | ~4g | | ☐ Not tested |
| 12 | | ~4g | | ☐ Not tested |
| 13 | | ~4g | | ☐ Not tested |

---

*Document Version: 2.0*
*Last Updated: January 2026*
*Test Parameters: 61 tests per motor, 3 profiles, 5 speeds, 4 voltages*
