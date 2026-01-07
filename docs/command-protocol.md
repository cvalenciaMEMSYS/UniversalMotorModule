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
| `home` | StallGuard homing (TMC2209 only) | Not supported |
| `stop` | Emergency stop | Stop with brake |
| `enable` | Enable driver | Enable H-bridge |
| `disable` | Disable driver (coast) | Disable (coast) |
| `set speed <n>` | Max steps/sec | Max speed % (0-100) |
| `set current <n>` | Motor current mA | Not applicable |
| `set microsteps <n>` | Microstepping divisor | Not applicable |
| `set accel <n>` | Acceleration steps/sec² | Ramp rate %/sec |
| `set jerk <n>` | S-curve jerk | S-curve jerk |
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
| `home` | Find home position using StallGuard | `home` |
| `stop` | Emergency stop (immediate halt) | `stop` |

### Motor Control

| Command | Description | Example |
|---------|-------------|---------|
| `enable` | Enable the motor driver | `enable` |
| `disable` | Disable the motor driver (no holding) | `disable` |

### Configuration Commands

| Command | Description | Example |
|---------|-------------|---------|
| `set speed <value>` | Set maximum speed (steps/sec) | `set speed 500` |
| `set current <mA>` | Set motor run current | `set current 400` |
| `set microsteps <n>` | Set microstepping (1, 2, 4, 8, 16, 32, 64, 128, 256) | `set microsteps 16` |
| `set accel <value>` | Set acceleration (steps/sec²) | `set accel 200` |
| `set jerk <value>` | Set jerk for S-curve profile (steps/sec³) | `set jerk 1000` |

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

When you set acceleration, the profile type changes automatically:

```
# No acceleration (default)
set accel 0           # Constant velocity profile

# With acceleration
set accel 500         # Trapezoidal profile: 500 steps/sec²

# S-curve (jerk-limited)
set accel 500
set jerk 1000         # S-curve profile: accel=500, jerk=1000
```

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
