# Serial Command Protocol

The Universal Motor Module uses a human-readable serial command protocol at 115200 baud.

## Connection Settings

- **Baud Rate:** 115200
- **Data Bits:** 8
- **Parity:** None
- **Stop Bits:** 1
- **Line Ending:** Newline (`\n`) or Carriage Return (`\r`)

## Quick Reference

### All Commands

| Command | Stepper (TMC2209/2208) | DC Motor |
|---------|------------------------|----------|
| `move <n>` | Move n steps | Run for n milliseconds |
| `abs <n>` | Move to position n | Set speed -1000 to +1000 |
| `run forward` | Run continuously forward | Run forward |
| `run backward` | Run continuously backward | Run backward |
| `stop` | Emergency stop (immediate) | Stop with brake |
| `brake` | Controlled stop (decelerate) | Stop with ramp |
| `home` | StallGuard homing (TMC2209 only) | Not supported |
| `enable` | Enable driver | Enable H-bridge |
| `disable` | Disable driver (coast) | Disable (coast) |
| `set speed <n>` | Max steps/sec | Max speed % (0-100) |
| `set accel <n>` | Acceleration steps/sec² | Ramp rate %/sec |
| `set cubesteps <n>` | S-curve ramp steps (0=trapezoidal) | Not applicable |
| `set current <n>` | Motor current mA | Not applicable |
| `set ihold <n>` | Hold current % (0-100) | Not applicable |
| `set microsteps <n>` | Microstepping divisor | Not applicable |
| `set autodisable on/off` | Auto enable/disable motor | Not applicable |
| `get pos` | Query current position | Not applicable |
| `get target` | Query target position | Not applicable |
| `get speed` | Query actual speed | Not applicable |
| `get rampstate` | Query ramp generator state | Not applicable |
| `stepdir on/off` | UART ↔ Step/Dir mode | Not applicable |
| `scan` | Scan UART addresses (TMC2209) | Not applicable |
| `reboot` | Soft reset ESP32 | Soft reset ESP32 |
| `?` or `status` | Show status | Show status |
| `help` | Show commands | Show commands |
| `t` or `test` | Test UART connection | Not applicable |
| `r` or `diag` | Full diagnostics | Basic status |
| `reconfigure` | Re-apply settings | Not applicable |

---

## Command Reference

### Motion Commands

| Command | Description | Example |
|---------|-------------|---------|
| `move <steps>` | Relative move (positive or negative steps) | `move 100`, `move -50` |
| `abs <position>` | Move to absolute position (must be ≥ 0) | `abs 0`, `abs 1000` |
| `run forward` | Run continuously forward at max speed | `run forward` |
| `run backward` | Run continuously backward at max speed | `run backward` |
| `stop` | Emergency stop (immediate halt, no deceleration) | `stop` |
| `brake` | Controlled stop (decelerate using configured accel) | `brake` |
| `home` | Find home position using StallGuard | `home` |

### Motor Control

| Command | Description | Example |
|---------|-------------|---------|
| `enable` | Enable the motor driver | `enable` |
| `disable` | Disable the motor driver (no holding) | `disable` |

### Configuration Commands

| Command | Description | Example |
|---------|-------------|---------|
| `set speed <value>` | Set maximum speed (steps/sec) | `set speed 500` |
| `set accel <value>` | Set acceleration (steps/sec²) | `set accel 200` |
| `set cubesteps <n>` | Set S-curve ramp steps (0=trapezoidal) | `set cubesteps 100` |
| `set current <mA>` | Set motor run current | `set current 400` |
| `set ihold <percent>` | Set hold current (0-100% of run) | `set ihold 50` |
| `set microsteps <n>` | Set microstepping (1-256, power of 2) | `set microsteps 16` |
| `set autodisable <on/off>` | Enable auto enable/disable | `set autodisable on` |

### Query Commands

| Command | Description | Example Output |
|---------|-------------|----------------|
| `get pos` | Get current position | `Position: 15234 steps` |
| `get target` | Get target position | `Target: 20000 steps` |
| `get speed` | Get actual current speed | `Current speed: 5000 steps/s` |
| `get rampstate` | Get ramp generator state | `Ramp state: ACCELERATING` |

### UART Control Commands

| Command | Description | Example |
|---------|-------------|---------|
| `stepdir on` | Switch to Step/Dir fallback mode | `stepdir on` |
| `stepdir off` | Try to re-enable UART mode | `stepdir off` |

### Status & Diagnostics

| Command | Description | Example |
|---------|-------------|---------|
| `?` or `status` | Show current motor status | `?` |
| `help` or `h` | Show available commands | `help` |
| `t` or `test` | Test UART connection (TMC drivers) | `t` |
| `r` or `diag` | Show full diagnostics | `r` |
| `reconfigure` | Re-apply all settings after power glitch | `reconfigure` |
| `scan` | Scan for TMC2209 at all 4 UART addresses | `scan` |

### System Commands

| Command | Description | Example |
|---------|-------------|---------|
| `reboot` or `restart` | Soft reset the ESP32 | `reboot` |

## Command Details

### `move <steps>`

Performs a relative move from the current position.

```
move 100      # Move forward 100 steps
move -50      # Move backward 50 steps
```

The move uses the currently configured acceleration profile:
- **Constant:** No acceleration, instant speed
- **Trapezoidal:** Linear acceleration/deceleration
- **S-Curve:** Jerk-limited smooth motion

### `abs <position>`

Moves to an absolute position. Position 0 is home.

```
abs 0         # Return to home position
abs 1000      # Move to position 1000
```

Position must be ≥ 0. Negative positions are rejected.

### `set speed <value>`

Sets the maximum velocity in steps per second.

```
set speed 500     # Max 500 steps/sec
set speed 2000    # Max 2000 steps/sec
```

### `set current <mA>`

Sets the motor run current in milliamps.

```
set current 400   # 400mA (safe for testing)
set current 800   # 800mA (typical for small NEMA17)
```

**Note:** Hold current is automatically set to 0 for non-backdrivable mechanisms.

### `set microsteps <n>`

Sets the microstepping resolution.

```
set microsteps 1      # Full step
set microsteps 16     # 16x microstepping (default)
set microsteps 256    # 256x microstepping
```

Valid values: 1, 2, 4, 8, 16, 32, 64, 128, 256

### Acceleration Profiles

FastAccelStepper supports three acceleration modes controlled by `set accel` and `set cubesteps`:

```
# Mode 1: Constant velocity (no acceleration)
set accel 0           # Motor instantly runs at target speed

# Mode 2: Trapezoidal (linear acceleration)
set accel 1000        # 1000 steps/sec² acceleration
set cubesteps 0       # Instant jump to configured acceleration

# Mode 3: S-curve (smooth acceleration)
set accel 1000        # 1000 steps/sec² target acceleration  
set cubesteps 100     # Ramp to target accel over 100 steps
```

**How cubesteps works:**
- `cubesteps = 0` → Trapezoidal profile (instant acceleration)
- `cubesteps > 0` → S-curve profile (acceleration ramps from 0 to configured value)
- Higher values = smoother motion but slower start

### `set ihold <percent>`

Sets the hold current as a percentage of run current (0-100%).

```
set ihold 0           # No holding torque (motor can freewheel when idle)
set ihold 50          # 50% of run current when idle (default)
set ihold 100         # Full current when idle (max holding, max heat)
```

**Note:** Only effective when `autodisable off` (motor stays enabled).

### `set autodisable on/off`

Controls automatic motor enable/disable behavior.

```
set autodisable on    # Motor auto-enables for moves, auto-disables after
set autodisable off   # Motor stays enabled (manual control)
```

**When autodisable ON:**
- Motor enables just before movement
- Motor disables ~100ms after movement completes
- Reduces heat and power consumption

**When autodisable OFF:**
- Motor stays enabled with hold current
- Maintains position against external forces
- Required if you need holding torque

### `stepdir on/off` (TMC Drivers Only)

Switches between UART mode and Step/Dir fallback mode.

```
stepdir on            # Switch to Step/Dir only mode
stepdir off           # Try to re-enable UART mode
```

**When to use:**
- UART wiring not connected
- UART communication failing
- Want to use hardware potentiometer for current

**In Step/Dir mode:**
- Motor current: Set by Vref potentiometer (hardware)
- Microstepping: Set by MS1/MS2 pins (hardware)
- StallGuard: **NOT available** (TMC2209 only)
- Homing: Requires physical limit switches
- `set current` and `set microsteps` commands have no effect

---

## DC Motor Specific Commands

When a DC motor (H-bridge) is detected, commands have different meanings:

### `move <milliseconds>`

Run the motor for a specified duration in milliseconds.

```
move 1000         # Run forward for 1 second
move -500         # Run reverse for 500ms
```

The motor runs at the current speed setting. Motion uses the configured acceleration ramp.

### `abs <speed>`

Set the motor speed directly. Range: -1000 to +1000

```
abs 500           # 50% forward
abs -300          # 30% reverse
abs 0             # Stop
abs 1000          # Full speed forward
```

**Note:** Unlike steppers, this is a direct speed command, not a position.

### `set speed <percent>`

Sets the maximum speed limit (0-100%).

```
set speed 80      # Limit to 80% max speed
set speed 100     # Allow full speed
```

### `set accel <rate>`

Sets the ramping rate for speed changes (% per second).

```
set accel 200     # Ramp at 200%/sec (0→100% in 0.5s)
set accel 0       # Instant speed changes (no ramping)
```

### DC Motor Commands That Don't Apply

| Command | Behavior |
|---------|----------|
| `set current` | No effect (hardware H-bridge) |
| `set microsteps` | No effect (not a stepper) |
| `set jerk` | No effect (uses simple ramping) |
| `home` | Not supported (no position feedback) |
| `stepdir` | Not applicable |
| `reconfigure` | No effect |

### DC Motor Example Session

```
> # DC Motor detected

> set speed 80
Max speed limited to 80%

> move 2000
Running forward for 2000ms...

> abs -500
Speed set to -50% (reverse)

> stop
Motor stopped (brake)

> disable
Motor disabled (coast)
```

## Example Session

```
# Connect at 115200 baud

╔═══════════════════════════════════════════════════════════╗
║         UNIVERSAL MOTOR MODULE v1.0                       ║
╚═══════════════════════════════════════════════════════════╝

Detected driver type: TMC2209
✓ Motor controller ready!
  Driver: TMC2209

> set current 400
Current set to 400 mA

> set speed 500
Speed set to 500 steps/sec

> enable
Motor enabled

> move 200
Moving 200 steps...

> ?
═══════════════════════════════════════════════════════════════
                        MOTOR STATUS
═══════════════════════════════════════════════════════════════

  Driver:        TMC2209
  Enabled:       Yes ✓
  Position:      200
  Target:        200
  Moving:        No
  Current Speed: 0 steps/sec
  Run Current:   400 mA
...

> disable
Motor disabled
```

## Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| "Controller not initialized" | `begin()` failed | Check hardware connections |
| "Position must be >= 0" | Negative absolute position | Use `move` for negative relative moves |
| "Unknown command" | Unrecognized command | Type `help` for valid commands |
| "Connection FAILED" | UART communication error | Check wiring, power supply |
