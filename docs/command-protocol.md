# Serial Command Protocol

The Universal Motor Module uses a human-readable serial command protocol at 115200 baud.

## Connection Settings

- **Baud Rate:** 115200
- **Data Bits:** 8
- **Parity:** None
- **Stop Bits:** 1
- **Line Ending:** Newline (`\n`) or Carriage Return (`\r`)

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

### Status & Diagnostics

| Command | Description | Example |
|---------|-------------|---------|
| `?` or `status` | Show current motor status | `?` |
| `help` or `h` | Show available commands | `help` |
| `t` or `test` | Test UART connection (TMC drivers) | `t` |
| `r` or `diag` | Show full diagnostics | `r` |
| `reconfigure` | Re-apply all settings after power glitch | `reconfigure` |

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
