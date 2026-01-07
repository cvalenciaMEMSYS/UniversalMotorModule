# Energy Measurement and Efficiency Test Plan

## 1. Executive Summary

This document outlines a test plan for measuring energy consumption and efficiency of actuator systems driven by the Universal Motor Module. The goal is to characterize:

1. **Driver efficiency** - Electrical power in vs. mechanical power out
2. **Actuator efficiency** - Mechanical power in vs. useful work
3. **System efficiency** - Total electrical input vs. useful mechanical output
4. **Profile optimization** - Which motion profiles are most energy-efficient for given tasks

**Document Structure:**
- **Section 2**: Quick Testing (simplified, primary focus)
- **Section 3+**: Advanced Testing (optional, for in-depth analysis)

---

## 2. Quick Testing (Primary)

### 2.1 Equipment Setup

**Power Measurement:**
- **Joulescope #1**: Measures power into driver (upstream of motor driver)
- **Joulescope #2**: Measures power into actuator (between driver and motor)

**Force/Thrust Measurement:**
- **Option A**: Load cell for precise thrust measurement
- **Option B**: Kitchen/postal scale - read max value during push test

**Expected Scale for Micro Servo Motors:**
| Parameter | Typical Range |
|-----------|---------------|
| Max Thrust | 50-500 g |
| Current Draw | 100-500 mA |
| Idle Power | 10-100 mW |
| Move Energy | 5-50 mJ per cycle |
| Backdrive Force | 20-300 g |

**Architecture:**
```
    ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
    │ Power       │     │ Motor       │     │ Actuator/   │     ┌────────┐
    │ Supply  ────┼──►  │ Driver  ────┼──►  │ Linear  ────┼──►  │ Load   │
    │             │     │             │     │ Mechanism   │     │ Cell/  │
    └─────────────┘     └─────────────┘     └─────────────┘     │ Scale  │
           │                   │                                 └────────┘
           ▼                   ▼
      Joulescope #1       Joulescope #2
      (Driver Input)      (Actuator Input)
```

### 2.2 Test Matrix

| Test ID | Profile Type | Key Measurements |
|---------|--------------|------------------|
| Q1 | Idle (enabled, no motion) | Energy consumption, heat |
| Q2 | No-load motion | Energy per cycle |
| Q3 | Constant velocity | Energy, thrust at various speeds |
| Q4 | Trapezoidal | Energy, thrust at various accel/speed |
| Q5 | S-Curve | Energy, thrust at various jerk/accel/speed |
| Q6 | Backdrive force | Force needed to backdrive mechanism |

### 2.3 Quick Test Procedures

---

#### Test Q1: Idle Characterization
**Objective**: Measure standing power consumption when motor is enabled but stationary

**Procedure**:
1. Configure motor with typical current setting (e.g., 400mA for stepper)
2. Enable motor via `ENABLE` command
3. Record Joulescope #1 and #2 readings for 30 seconds
4. Repeat at different current settings if desired

**Data to Record**:
| Motor | Current (mA) | Driver Power (mW) | Actuator Power (mW) |
|-------|--------------|-------------------|---------------------|
| Motor A | 200 | | |
| Motor A | 400 | | |
| Motor A | 800 | | |

---

#### Test Q2: No-Load Motion
**Objective**: Measure energy consumption with no external load

**Procedure**:
1. Configure profile (start with Constant)
2. Execute a standard move: `MOVE 1000` (or suitable stroke length)
3. Record energy from both Joulescopes during move
4. Repeat 3x, calculate average
5. Change to Trapezoidal profile, repeat
6. Change to S-Curve profile, repeat

**Data to Record**:
| Motor | Profile | Speed | Accel | Jerk | Driver Energy (mJ) | Actuator Energy (mJ) | Duration (ms) |
|-------|---------|-------|-------|------|-------------------|---------------------|---------------|
| Motor A | Constant | 500 | - | - | | | |
| Motor A | Trapezoidal | 500 | 1000 | - | | | |
| Motor A | S-Curve | 500 | 1000 | 5000 | | | |

---

#### Test Q3: Constant Velocity Profile - Loaded
**Objective**: Characterize energy at different speeds under load

**Procedure**:
1. Set `PROFILE CONSTANT`
2. Position actuator against load cell/scale
3. Execute move at speed 100 steps/sec
4. Record:
   - Peak thrust (g - grams force)
   - Energy consumed (both Joulescopes)
5. Increase speed: 200, 500, 1000, 2000 steps/sec
6. Repeat measurements

**Data to Record**:
| Motor | Speed (steps/s) | Max Thrust (g) | Driver Energy (mJ) | Actuator Energy (mJ) |
|-------|-----------------|----------------|-------------------|---------------------|
| Motor A | 100 | | | |
| Motor A | 200 | | | |
| Motor A | 500 | | | |
| Motor A | 1000 | | | |
| Motor A | 2000 | | | |

---

#### Test Q4: Trapezoidal Profile - Loaded
**Objective**: Characterize trapezoidal profile efficiency vs parameters

**Procedure**:
1. Set `PROFILE TRAPEZOIDAL`
2. Fix max speed at 1000 steps/sec
3. Test acceleration values: 500, 1000, 2000, 5000 steps/sec²
4. For each setting:
   - Execute move against load cell
   - Record thrust, energy, duration

**Data to Record**:
| Motor | Speed | Accel | Max Thrust (g) | Driver Energy (mJ) | Actuator Energy (mJ) | Duration (ms) |
|-------|-------|-------|----------------|-------------------|---------------------|---------------|
| Motor A | 1000 | 500 | | | | |
| Motor A | 1000 | 1000 | | | | |
| Motor A | 1000 | 2000 | | | | |
| Motor A | 1000 | 5000 | | | | |

---

#### Test Q5: S-Curve Profile - Loaded
**Objective**: Compare S-curve efficiency to Trapezoidal

**Procedure**:
1. Set `PROFILE SCURVE`
2. Use same speed/accel as best Trapezoidal case from Q4
3. Test jerk values: 1000, 5000, 10000, 20000 steps/sec³
4. Record same measurements

**Data to Record**:
| Motor | Speed | Accel | Jerk | Max Thrust (g) | Driver Energy (mJ) | Actuator Energy (mJ) | Duration (ms) |
|-------|-------|-------|------|----------------|-------------------|---------------------|---------------|
| Motor A | 1000 | 1000 | 1000 | | | | |
| Motor A | 1000 | 1000 | 5000 | | | | |
| Motor A | 1000 | 1000 | 10000 | | | | |
| Motor A | 1000 | 1000 | 20000 | | | | |

---

#### Test Q6: Backdrive Force Characterization
**Objective**: Measure force required to backdrive the linear mechanism

**Description**: This test determines the holding/braking capability of the actuator's linear mechanism (lead screw, ball screw, rack & pinion, etc.) by measuring how much external force is needed to push the mechanism back when the motor is unpowered or disabled.

**Procedure**:
1. **Disable motor**: Send `DISABLE` command
2. Position actuator at mid-stroke
3. Attach load cell or place actuator against scale
4. Slowly and steadily push against the linear output
5. Record the force at which the mechanism starts to move backward
6. Repeat 3 times, note consistency
7. **Optional**: Repeat with motor enabled but stationary (stall torque test)

**Data to Record**:
| Motor | Mechanism Type | Motor State | Backdrive Force (g) | Notes |
|-------|----------------|-------------|---------------------|-------|
| Motor A | Lead Screw | Disabled | | |
| Motor A | Lead Screw | Enabled (idle) | | |
| Motor B | Worm Gear | Disabled | | |
| Motor B | Worm Gear | Enabled (idle) | | |

**Interpretation** (for micro servo motors with <500g thrust):
- **High backdrive force (> 200g)**: Mechanism is effectively non-backdrivable for this scale
- **Medium backdrive force (50-200g)**: Partially backdrivable, may need holding current
- **Low backdrive force (< 50g)**: Backdrives easily, requires active holding current
- **Motor enabled vs disabled difference**: Shows holding torque contribution from motor

**Use Cases**:
- Gravity-loaded applications (vertical actuators)
- Safety: Can load force backdrive the mechanism if power fails?
- Efficiency: Non-backdrivable mechanisms waste less energy on holding

---

### 2.4 Quick Analysis

After completing Q1-Q6, fill in this summary:

#### Motor Characterization Summary

**Motor: ______________ (Model/Type)**

| Metric | Value |
|--------|-------|
| Idle Power (Driver) | ___ mW |
| Idle Power (Actuator) | ___ mW |
| Driver Efficiency (typical) | ___% |
| Max Thrust | ___ g |
| Backdrive Force | ___ g |

**Profile Comparison (same move distance, similar duration):**

| Profile | Energy (mJ) | Thrust (g) | Energy/Thrust (mJ/g) |
|---------|-------------|------------|----------------------|
| Constant | | | |
| Trapezoidal | | | |
| S-Curve | | | |

**Key Findings:**
1. Most efficient profile: ______________
2. Energy savings vs baseline: ______________% 
3. Thrust capability: ______________ g max
4. Backdrive resistance: ______________ g (non-backdrivable if > 200g for micro servos)
5. Typical current draw: ______________ mA (target < 500mA)
6. Recommended operating parameters: ______________

---

### 2.5 Quick Test Checklist

- [ ] Joulescopes calibrated/verified
- [ ] Load cell/scale zeroed
- [ ] Motor securely mounted
- [ ] Test Q1: Idle - completed
- [ ] Test Q2: No-load - completed  
- [ ] Test Q3: Constant loaded - completed
- [ ] Test Q4: Trapezoidal loaded - completed
- [ ] Test Q5: S-curve loaded - completed
- [ ] Test Q6: Backdrive - completed
- [ ] Summary analysis completed
- [ ] Data backed up

---

## 3. Advanced Testing (Optional)

*The following sections provide more rigorous testing methodology for in-depth characterization when needed.*

---

## 3.1 Measurement Architecture (Advanced)

### 3.1.1 Power Measurement Points

```
                    ┌─────────────────┐
    V_supply ──────►│                 │
                    │   Motor Driver  │────► Motor/Actuator ────► Load
    I_driver ──────►│   (TMC/RZ7899)  │
                    └─────────────────┘
         ▲                   ▲                    ▲
         │                   │                    │
    ┌────┴────┐         ┌────┴────┐          ┌────┴────┐
    │ P_input │         │ P_motor │          │ P_mech  │
    │ Driver  │         │ Shaft   │          │ Load    │
    │ Power   │         │ Power   │          │ Power   │
    └─────────┘         └─────────┘          └─────────┘
```

### 3.1.2 Required Measurements

| Measurement | Symbol | Units | Method |
|-------------|--------|-------|--------|
| Supply Voltage | V_in | V | Voltage sensor |
| Driver Input Current | I_in | A | Current shunt/sensor |
| Motor Voltage | V_motor | V | Differential measurement |
| Motor Current | I_motor | A | Current sensor |
| Shaft Torque | τ | N·m | Torque sensor or load cell |
| Shaft Speed | ω | rad/s | Encoder or back-EMF |
| Linear Force | F | N | Load cell |
| Linear Velocity | v | m/s | Time/position or velocity sensor |

---

## 3.2 Advanced Test Equipment

### 3.2.1 Power Measurement Hardware

#### Option A: Basic Setup (Budget ~$100-200)
- **INA226** power monitor (I²C, 0.1% accuracy)
- **ACS712** hall-effect current sensor (5A range)
- **Load cell** (5kg or 10kg) with HX711 amplifier
- **Multimeter** for calibration verification

#### Option B: Advanced Setup (Budget ~$500-1000)
- **USB power analyzer** (e.g., ChargerLAB KM003C)
- **High-precision current shunt** (0.01Ω, 0.1% tolerance)
- **Strain gauge load cell** (0.05% accuracy)
- **Oscilloscope** with current probe for dynamic analysis

#### Option C: Research Grade (Budget $2000+)
- **Precision power analyzer** (Keysight/Yokogawa)
- **Inline torque transducer**
- **High-speed DAQ** for synchronized measurements

### 3.2 Mechanical Test Fixtures

#### 3.2.1 Load Cell Fixture
```
    ┌────────────────────────────────────────┐
    │                 Fixed Mount             │
    │                     │                   │
    │              ┌──────▼──────┐           │
    │              │  Load Cell   │           │
    │              │   (5kg-50kg) │           │
    │              └──────┬──────┘           │
    │                     │                   │
    │              ┌──────▼──────┐           │
    │              │   Coupler/   │           │
    │              │   Pusher     │           │
    │              └──────┬──────┘           │
    │                     │                   │
    │              ┌──────▼──────┐           │
    │              │   Actuator   │◄─── Motor │
    │              │   End        │           │
    └────────────────────────────────────────┘
```

#### 3.2.2 Spring Load Fixture
```
    ┌─────────────────────────────────────────┐
    │  Actuator ──► [=====Spring=====] ◄── Wall│
    │                    k = N/m               │
    │                                          │
    │  Force = k × displacement               │
    │  This allows variable load testing       │
    └─────────────────────────────────────────┘
```

**Recommended Springs:**
- Light: k = 100 N/m (for low-force testing)
- Medium: k = 500 N/m (typical operating loads)
- Heavy: k = 2000 N/m (high-force stress testing)

---

## 3.3 Efficiency Calculations (Advanced)

### 4.1 Power Formulas

```
Electrical Input Power:
    P_elec = V_in × I_in [W]

Motor Mechanical Power (Rotary):
    P_mech = τ × ω [W]
    where τ = torque [N·m], ω = angular velocity [rad/s]

Motor Mechanical Power (Linear):
    P_mech = F × v [W]
    where F = force [N], v = velocity [m/s]

Driver Efficiency:
    η_driver = P_motor / P_elec × 100%

Actuator Efficiency:
    η_actuator = P_output / P_motor × 100%

System Efficiency:
    η_system = P_output / P_elec × 100%
```

### 4.2 Energy Formulas

```
Electrical Energy:
    E_elec = ∫ P_elec dt [J]

Mechanical Work:
    W_mech = ∫ F dx = ∫ τ dθ [J]

Energy Efficiency:
    η_energy = W_mech / E_elec × 100%
```

### 4.3 Stepper Motor Specific

For stepper motors, efficiency is complex due to:
- Holding current (consumes power with no motion)
- Microstepping losses
- Chopping frequency losses

```
Stepper Useful Power:
    P_useful = τ_load × ω_actual

Stepper Losses:
    P_copper = I_rms² × R_coil (copper losses)
    P_core = f(frequency, flux) (core losses)
    P_driver = (V_supply - V_motor) × I (driver losses)
```

---

## 3.4 Advanced Test Procedures

### 5.1 Baseline Measurements (No Load)

#### Test 5.1.1: Idle Power Consumption
**Objective**: Measure power when motor is enabled but stationary

**Procedure**:
1. Enable motor with holding current
2. Measure V_in, I_in for 60 seconds
3. Calculate average idle power
4. Repeat for current settings: 100mA, 200mA, 400mA, 800mA

**Data Collection Template**:
| Current Setting | V_in (V) | I_in (A) | P_idle (W) |
|-----------------|----------|----------|------------|
| 100 mA          |          |          |            |
| 200 mA          |          |          |            |
| 400 mA          |          |          |            |
| 800 mA          |          |          |            |

---

#### Test 5.1.2: No-Load Motion Power
**Objective**: Measure power during unloaded motion

**Procedure**:
1. Set motor current to rated value
2. Execute 1000-step move at various speeds
3. Record power during motion
4. Calculate energy per move

**Parameters**:
- Speeds: 100, 500, 1000, 2000, 5000 steps/sec
- Profiles: Trapezoidal, S-Curve
- Current: 400 mA

---

### 5.2 Load Characterization Tests

#### Test 5.2.1: Static Load Test (Load Cell)
**Objective**: Characterize force vs. current relationship

**Procedure**:
1. Position actuator against load cell
2. Enable motor at low current
3. Increase current gradually
4. Record force at each current level

**Data Collection**:
| Current (mA) | Force (N) | V_in (V) | I_in (A) | P_in (W) |
|--------------|-----------|----------|----------|----------|
| 100          |           |          |          |          |
| 200          |           |          |          |          |
| 400          |           |          |          |          |
| 600          |           |          |          |          |
| 800          |           |          |          |          |

---

#### Test 5.2.2: Dynamic Load Test (Spring)
**Objective**: Measure power under increasing load

**Procedure**:
1. Attach calibrated spring to actuator
2. Execute move sequence:
   - Move 10mm, hold, measure force/power
   - Move additional 10mm, hold, measure
   - Continue until max displacement or stall
3. Return to start, repeat 3 times

**Spring Configuration**:
- Spring constant k = _____ N/m
- Maximum displacement = _____ mm
- Maximum expected force = k × displacement

---

#### Test 5.2.3: Quasi-Static Efficiency Map
**Objective**: Create 2D efficiency map (speed × load)

**Procedure**:
1. For each speed in {100, 500, 1000, 2000} steps/sec:
2. For each load in {0%, 25%, 50%, 75%, 100%} of max force:
   - Execute constant-speed motion segment
   - Measure electrical power input
   - Measure mechanical power output
   - Calculate instantaneous efficiency

**Results**: Create heatmap visualization

```
        Load Force (% of max)
        0%   25%   50%   75%   100%
       ┌─────────────────────────┐
100 ▶  │ 85%  80%   75%   65%  50%│
500 ▶  │ 82%  78%   72%   60%  45%│
1000▶  │ 78%  74%   68%   55%  40%│  Speed
2000▶  │ 72%  68%   60%   48%  35%│  (steps/sec)
       └─────────────────────────┘
```

---

### 5.3 Motion Profile Comparison Tests

#### Test 5.3.1: Trapezoidal vs. S-Curve Energy
**Objective**: Compare energy consumption between profiles

**Procedure**:
1. Configure identical:
   - Total displacement: 1000 steps
   - Max speed: 500 steps/sec
   - Acceleration: 1000 steps/sec²
   - Load: Medium spring (50% compression)

2. Execute with Trapezoidal profile:
   - Measure total energy consumed
   - Measure move duration
   - Record peak current

3. Execute with S-Curve profile (same constraints):
   - Same measurements

4. Repeat 10 times each, calculate statistics

**Analysis**:
| Profile | Energy (mJ) | Duration (ms) | Peak I (A) | RMS I (A) |
|---------|-------------|---------------|------------|-----------|
| Trapezoidal | Mean ± σ | | | |
| S-Curve | Mean ± σ | | | |

---

#### Test 5.3.2: Profile Parameter Sweep
**Objective**: Find energy-optimal parameters

**Parameters to vary**:
- Max speed: 200 to 2000 steps/sec (in 200 step increments)
- Acceleration: 200 to 2000 steps/sec² (in 200 increments)
- Jerk (S-curve): 1000 to 20000 steps/sec³

**Measurements for each combination**:
- Total energy consumed
- Move duration
- Peak mechanical vibration (accelerometer optional)

**Output**: 3D surface plot of Energy vs. (Speed, Acceleration)

---

### 5.4 Regenerative Braking Analysis

#### Test 5.4.1: Back-EMF During Deceleration
**Objective**: Quantify energy recovery potential

**Procedure**:
1. Accelerate motor to high speed
2. Command stop with no holding current
3. Measure back-EMF voltage during coast-down
4. Calculate energy that could be recovered

**Note**: TMC drivers typically dissipate this in resistive braking

---

### 5.5 Thermal Efficiency Tests

#### Test 5.5.1: Thermal Derating
**Objective**: Characterize efficiency loss with temperature

**Procedure**:
1. Record ambient temperature
2. Execute efficiency test (5.2.3) immediately after power-on (cold)
3. Run continuous motion for 30 minutes
4. Re-execute efficiency test (hot)
5. Compare efficiency values

**Expected Results**: Efficiency typically drops 5-15% from cold to hot

---

## 3.5 Data Analysis Methods

### 6.1 Energy Integration

For accurate energy measurement, use trapezoidal integration:

```python
def calculate_energy(time_samples, power_samples):
    """
    Calculate total energy using trapezoidal integration.
    time_samples: array of time values in seconds
    power_samples: array of power values in watts
    Returns: energy in joules
    """
    energy = 0
    for i in range(1, len(time_samples)):
        dt = time_samples[i] - time_samples[i-1]
        avg_power = (power_samples[i] + power_samples[i-1]) / 2
        energy += avg_power * dt
    return energy
```

### 6.2 Efficiency Curve Fitting

Fit efficiency data to standard motor efficiency model:

```python
def motor_efficiency_model(load_fraction, eta_max, load_at_max):
    """
    Standard motor efficiency model.
    Efficiency peaks at partial load, drops at both extremes.
    """
    x = load_fraction / load_at_max
    return eta_max * (2*x) / (1 + x*x)
```

### 6.3 Statistical Analysis

For each test:
- Calculate mean, standard deviation
- Perform t-tests for profile comparisons
- Report 95% confidence intervals
- Document sample size (n ≥ 10 recommended)

---

## 3.6 Expected Results and Benchmarks

### 7.1 Typical TMC2209 Efficiency Values

| Condition | Expected Efficiency |
|-----------|---------------------|
| No load, low speed | 40-60% |
| 50% load, medium speed | 70-85% |
| High load, low speed | 50-70% |
| High load, high speed | 30-50% |

### 7.2 Profile Energy Comparison

Based on theoretical analysis:
- **S-Curve typically uses 5-15% less energy** than trapezoidal for same move
- **Lower acceleration = lower peak current = lower I²R losses**
- **But lower acceleration = longer move time = more energy overall**
- **Optimal point exists at moderate acceleration**

---

## 3.7 Test Data Recording

### 8.1 CSV Format

```csv
timestamp_ms,test_id,profile,speed,accel,jerk,load_percent,V_in,I_in,F_load,displacement,duration_ms,energy_mJ,efficiency
0,001,TRAP,500,1000,0,25,12.1,0.42,2.5,1000,2500,127.5,68.3
0,001,SCURVE,500,1000,5000,25,12.0,0.38,2.5,1000,2650,121.2,71.4
...
```

### 8.2 Recommended Logging Code

Add to ESP32 firmware:

```cpp
struct EnergyMeasurement {
    uint32_t timestamp_ms;
    float voltage;
    float current;
    float power;
    float energy_accumulated;
    int32_t position;
    float force;
};

// Log at 1kHz minimum for accurate power integration
void logEnergyData(EnergyMeasurement& m) {
    Serial.printf("%lu,%.3f,%.4f,%.3f,%.3f,%d,%.2f\n",
        m.timestamp_ms, m.voltage, m.current, m.power,
        m.energy_accumulated, m.position, m.force);
}
```

---

## 4. Safety Considerations

### 9.1 Electrical Safety
- Use fused power supply (2A max recommended)
- Keep fingers away from high-current paths
- Monitor driver temperature during tests

### 9.2 Mechanical Safety
- Secure load cell/spring fixtures
- Use mechanical stops to limit travel
- Avoid storing energy in springs (release controlled)
- Wear safety glasses for spring tests

### 9.3 Thermal Safety
- Do not exceed motor/driver rated current
- Allow cooling between extended tests
- Monitor for smoke/burning smell

---

## 5. Results Documentation Template

```
================================================================================
ENERGY EFFICIENCY TEST REPORT
================================================================================

Test Date: _______________
Operator: _______________
Firmware Version: _______________

Equipment Calibration:
  - Power Sensor Last Cal: _______________
  - Load Cell Last Cal: _______________
  - Spring Constant Verified: _______________

Test Configuration:
  - Motor: _______________
  - Driver: TMC2209 / TMC2208 / DC
  - Supply Voltage: ___ V
  - Nominal Current: ___ mA

--------------------------------------------------------------------------------
TEST 5.1: BASELINE MEASUREMENTS
--------------------------------------------------------------------------------
Idle Power @ 400mA: ___ W
No-Load Motion Power @ 500 steps/sec: ___ W

--------------------------------------------------------------------------------
TEST 5.2: LOAD CHARACTERIZATION
--------------------------------------------------------------------------------
[Attach efficiency map data/plot]

Peak Efficiency: __% at __% load, __ steps/sec
Operating Point Recommendation: _______________

--------------------------------------------------------------------------------
TEST 5.3: PROFILE COMPARISON
--------------------------------------------------------------------------------
                    | Trapezoidal | S-Curve | Difference
Energy per move     |    __ mJ    |  __ mJ  |   __ %
Move duration       |    __ ms    |  __ ms  |   __ %
Peak current        |    __ A     |  __ A   |   __ %

Recommendation: _______________

--------------------------------------------------------------------------------
CONCLUSIONS
--------------------------------------------------------------------------------
1. _______________
2. _______________
3. _______________

Signed: _______________ Date: _______________
================================================================================
```

---

## 11. Future Enhancements

### 11.1 Automated Test System
- ESP32 controls test sequence
- INA226 logs power continuously
- HX711 logs force continuously
- Python script correlates data
- Automatic efficiency map generation

### 11.2 Closed-Loop Optimization
- Implement real-time power monitoring
- Adaptive profile adjustment based on load
- Energy-optimal trajectory planning

### 11.3 Comparative Testing
- Multiple motor types
- Different driver configurations
- Temperature effects study

---

## Appendix A: INA226 Configuration

```cpp
#include <INA226.h>

INA226 ina226;

void setupPowerMonitor() {
    ina226.begin();
    ina226.configure(
        INA226_AVERAGES_64,      // 64 sample averaging
        INA226_BUS_CONV_TIME_1100US,
        INA226_SHUNT_CONV_TIME_1100US,
        INA226_MODE_SHUNT_BUS_CONT
    );
    ina226.calibrate(0.01, 4);  // 10mΩ shunt, 4A max
}

float getPower() {
    return ina226.readBusPower();  // Returns watts
}
```

---

## Appendix B: Load Cell Configuration

```cpp
#include <HX711.h>

HX711 scale;

void setupLoadCell() {
    scale.begin(HX711_DOUT, HX711_SCK);
    scale.set_scale(420.0);  // Calibration factor
    scale.tare();
}

float getForce() {
    return scale.get_units(10) * 9.81;  // Returns Newtons
}
```

---

## Appendix C: Statistical Functions

```python
import numpy as np
from scipy import stats

def analyze_efficiency_data(trap_data, scurve_data):
    """Compare trapezoidal vs S-curve efficiency."""
    
    trap_mean = np.mean(trap_data)
    scurve_mean = np.mean(scurve_data)
    
    # Paired t-test
    t_stat, p_value = stats.ttest_rel(trap_data, scurve_data)
    
    # Effect size (Cohen's d)
    pooled_std = np.sqrt((np.std(trap_data)**2 + np.std(scurve_data)**2) / 2)
    cohens_d = (scurve_mean - trap_mean) / pooled_std
    
    print(f"Trapezoidal: {trap_mean:.2f} ± {np.std(trap_data):.2f} mJ")
    print(f"S-Curve: {scurve_mean:.2f} ± {np.std(scurve_data):.2f} mJ")
    print(f"Difference: {(scurve_mean - trap_mean)/trap_mean*100:.1f}%")
    print(f"p-value: {p_value:.4f}")
    print(f"Effect size (Cohen's d): {cohens_d:.2f}")
```
