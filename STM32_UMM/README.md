# STM32 UMM — Minimal H-Bridge Motor Controller

Stripped-down port of the UniversalMotorModule HarCo H-bridge driver for STM32. Same serial command interface as the main UMM.

## Hardware

| Item | Value |
|------|-------|
| Board | Nucleo-L031K6 (STM32L031K6, Cortex-M0+) |
| Flash | 32 KB |
| RAM | 8 KB |
| Framework | Arduino (STM32duino) |

### Pin Mapping

| Function | Arduino Pin | STM32 Pin | Timer | Notes |
|----------|-------------|-----------|-------|-------|
| IN1 | D3 | PB0 | TIM2_CH3 | PWM @ 20kHz |
| IN2 | D6 | PB1 | TIM2_CH4 | PWM @ 20kHz |
| nSLEEP | D2 | PA12 | — | Digital |
| EN | D4 | PB7 | — | Digital, DRV8839 only |

### Supported Modules

| Module | Color | EN Pin | Define |
|--------|-------|--------|--------|
| DRV8837 | Black | No | `MODULE_DRV8837` |
| DRV8832 | Green | No | `MODULE_DRV8832` |
| DRV8210P | Blue | No | `MODULE_DRV8210P` |
| DRV8839 | White | Yes | `MODULE_DRV8839` |

Uncomment the matching `#define` at the top of `src/main.cpp`.

## Build & Flash

```bash
cd STM32_UMM
pio run                 # Build
pio run -t upload       # Flash via ST-Link
pio device monitor      # Serial monitor (115200 baud)
```

## Serial Commands

Commands match the main UMM implementation. Timed moves print `Complete` when finished.

| Command | Action |
|---------|--------|
| `move <ms>` | Timed move at 100% speed (positive=fwd, negative=rev) |
| `abs <ms> <speed%>` | Timed move at specified speed (0-100%), sign = direction. naming is not consistent with main UMM but DC motors don't have absolute positioning, I expect this command to not be really used |
| `run f` | Forward at max speed indefinitely |
| `run b` | Reverse at max speed indefinitely |
| `stop` | Emergency stop (coast) |
| `brake` | Brake (both IN pins HIGH) |
| `wake` | Set nSLEEP HIGH (active) |
| `sleep` | Set nSLEEP LOW (sleeping) |
| `enable` | Activate EN pin |
| `disable` | Deactivate EN pin |
| `diag` | Position (net ms moved) & status |
| `help` | List commands |

### Position Tracking

The `diag` command reports `Position` as net time moved in milliseconds — positive = forward, negative = reverse. This maps 1:1 to stepper "steps" where each ms of motor runtime equals one unit.

## PWM

Uses HardwareTimer on TIM2 at 20kHz (ultrasonic). Both IN1 (CH3) and IN2 (CH4) share the same timer, so frequency is guaranteed identical. Speed is always 100% duty — the `move` command controls duration.
