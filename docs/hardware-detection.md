# Hardware Detection Circuit

The Universal Motor Module uses a simple GPIO-based detection circuit to automatically identify which motor driver is connected at runtime.

## Detection Pins

| GPIO | Function | Description |
|------|----------|-------------|
| GPIO 10 | VCC Source | Output HIGH (3.3V) - provides power for jumper detection |
| GPIO 13 | VCC Source | Output HIGH (3.3V) - provides power for jumper detection |
| GPIO 11 | Detection Bit 0 | Input with internal pull-down - senses DC motor selection |
| GPIO 12 | Detection Bit 1 | Input with internal pull-down - senses TMC2208 selection |

## Detection Truth Table

| Jumper Configuration | GPIO 11 | GPIO 12 | Detected Driver |
|----------------------|---------|---------|-----------------|
| None (default)       | LOW     | LOW     | **TMC2209** (UART stepper) |
| 10↔11 or 13↔11       | HIGH    | LOW     | **DC Motor** (RZ7899 H-bridge) |
| 10↔12 or 13↔12       | LOW     | HIGH    | **TMC2208** (Step/Dir stepper) |
| Both jumpered        | HIGH    | HIGH    | **STSPIN220** (Step/Dir stepper) |

## How It Works

1. On startup, GPIO 10 and GPIO 13 are configured as outputs and set HIGH (3.3V)
2. GPIO 11 and GPIO 12 are configured as inputs with ESP32-S3's internal pull-down resistors (~45kΩ)
3. When no jumper is present, the internal pull-down keeps the pin LOW
4. When a jumper connects a detection pin to a VCC source pin, the detection pin reads HIGH
5. The driver factory reads these pins and instantiates the appropriate driver class

## Wiring Examples

### TMC2209 (Default - No Jumper Needed)
```
GPIO 10 ─── [NC]
GPIO 11 ─── [NC] (internal pull-down keeps LOW)
GPIO 12 ─── [NC] (internal pull-down keeps LOW)
GPIO 13 ─── [NC]

Result: GPIO 11=LOW, GPIO 12=LOW → TMC2209 detected
```

### DC Motor (RZ7899)
```
GPIO 10 ───┐
           ├─── Jumper wire
GPIO 11 ───┘

GPIO 12 ─── [NC] (internal pull-down keeps LOW)
GPIO 13 ─── [NC]

Result: GPIO 11=HIGH, GPIO 12=LOW → DC Motor detected
```

### TMC2208
```
GPIO 10 ─── [NC]
GPIO 11 ─── [NC] (internal pull-down keeps LOW)

GPIO 12 ───┐
           ├─── Jumper wire
GPIO 13 ───┘

Result: GPIO 11=LOW, GPIO 12=HIGH → TMC2208 detected
```

### STSPIN220
```
GPIO 10 ───┐
           ├─── Jumper wire
GPIO 11 ───┘

GPIO 12 ───┐
           ├─── Jumper wire
GPIO 13 ───┘

Result: GPIO 11=HIGH, GPIO 12=HIGH → STSPIN220 detected
```

## Notes

- The ESP32-S3 internal pull-down resistors are approximately 45kΩ
- No external resistors are required for this detection circuit
- The detection only happens once during `controller.begin()`
- To change drivers, update the jumper configuration and reset the ESP32
