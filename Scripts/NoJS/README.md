# NoJS - Force-Distance Measurement System (Simplified)

A simplified force-distance measurement system that controls the FD (Force-Distance) setup and DUT (Device Under Test) motor module. Joulescope energy measurement is recorded separately using the official Joulescope software.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      LOCAL PC                                │
│  ┌───────────────────────────────────────────────────────┐  │
│  │             orchestrator_nojs.py                       │  │
│  │  • PyQt6 GUI                                          │  │
│  │  • FD Client (TCP to RPi)                             │  │
│  │  • DUT Controller (USB Serial)                        │  │
│  │  • Force vs Time/Position plots                       │  │
│  │  • CSV export                                         │  │
│  └────────────────┬─────────────────┬────────────────────┘  │
│                   │                 │                        │
└───────────────────┼─────────────────┼────────────────────────┘
                    │ TCP:5002        │ USB Serial
                    ▼                 ▼
┌───────────────────────────┐   ┌────────────────────────────┐
│    RASPBERRY PI           │   │         DUT                 │
│  ┌─────────────────────┐  │   │  (Universal Motor Module)   │
│  │  fd_server_nojs.py  │  │   │                            │
│  │  • TCP server       │  │   │  Serial protocol:          │
│  │  • Arduino bridge   │  │   │  • set speed/accel         │
│  └──────────┬──────────┘  │   │  • enable/disable          │
│             │ Serial      │   │  • move <steps>            │
│             ▼             │   │  • status                  │
│  ┌─────────────────────┐  │   └────────────────────────────┘
│  │   fd_arduino.ino    │  │
│  │   (Arduino Nano)    │  │
│  │  • HX711 load cell  │  │
│  │  • TMC2209 stepper  │  │
│  └─────────────────────┘  │
└───────────────────────────┘
```

## Files

| File | Location | Description |
|------|----------|-------------|
| `fd_arduino.ino` | Upload to Arduino Nano on RPi | Simplified FD control sketch |
| `fd_server_nojs.py` | Run on Raspberry Pi | TCP server bridging client to Arduino |
| `fd_client_nojs.py` | Local PC (library) | Python client library |
| `orchestrator_nojs.py` | Run on Local PC | Main GUI application |
| `dut_controller.py` | Copied from OldAttempt | DUT serial interface |

## Installation

### Raspberry Pi

1. **Upload Arduino sketch:**
   - Open `fd_arduino.ino` in Arduino IDE
   - Select "Arduino Nano" and appropriate port
   - Upload

2. **Install Python dependencies:**
   ```bash
   pip install pyserial
   ```

3. **Run server:**
   ```bash
   python fd_server_nojs.py
   ```

### Local PC

1. **Install dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

2. **Copy DUT controller:**
   ```bash
   cp ../OldAttempt/dut_controller.py .
   ```

3. **Run orchestrator:**
   ```bash
   python orchestrator_nojs.py
   ```

## Arduino Protocol (FD Setup)

### Modes
- **MONITOR**: Manual control mode, streaming on demand
- **DRIVE**: Automated test mode with auto-retract

### Commands

| Command | Description | Response |
|---------|-------------|----------|
| `M` | Switch to MONITOR mode | `Mode: MONITOR` |
| `D` | Switch to DRIVE mode | `Mode: DRIVE` |
| `V<speed>` | Set speed in mm/s (0.1-25) | `OK_SPEED|<speed>` |
| `<number>` | Manual jog (MONITOR only) | `OK_JOG` |
| `G<dist>` | Drive and retract (DRIVE only) | Stream data, then `STOPPED` |
| `START` | Start force streaming | `STREAMING` |
| `STOP` | Stop force streaming | `STOPPED`, then base64 CSV |
| `F` | Single force reading | `Force: <N> N` |
| `Z` | Tare/zero force sensor | `Tared` |

### Stream Format
```
T<timestamp_ms>,F<force_N>
T1234,F0.123
T1334,F0.145
...
```

## TCP Protocol (FD Server)

| Command | Description | Response |
|---------|-------------|----------|
| `PING` | Connection test | `PONG` |
| `STATUS` | Get current status | `OK:MODE=MONITOR,SPEED=10.0` |
| `SET_MODE:<mode>` | Set mode | `OK:MODE=<mode>` |
| `SET_SPEED:<speed>` | Set speed | `OK:SPEED=<speed>` |
| `JOG:<distance>` | Manual jog | `OK:MOVED` |
| `MOVE:<distance>` | Drive with data | `OK:DATA:<base64_csv>` |
| `START_STREAM` | Start streaming | `OK:STREAMING` |
| `STOP_STREAM` | Stop streaming | `OK:DATA:<base64_csv>` |
| `ZERO` | Tare sensor | `OK:TARED` |
| `FORCE` | Get force reading | `OK:FORCE=<value>` |

## GUI Features

### Connections
- FD Server: Enter RPi IP and port (default: 5002)
- DUT: Select COM port from dropdown

### DUT Control
- **Speed**: Steps per second (1-100,000)
- **Acceleration**: Steps/sec² (0 = auto-calculate from speed)
- **Cubesteps**: S-curve jerk parameter (0 = trapezoidal)
- **Travel Steps**: Forward movement amount
- **Auto-retract**: Retract 50% after forward move
- **Manual jog**: Move arbitrary steps

### FD Control
- **Mode**: MONITOR or DRIVE
- **Speed**: 0.1 to 25 mm/s
- **Distance**: Travel distance for DRIVE mode
- **Manual jog**: ◄ and ► buttons for positioning
- **Zero**: Tare the force sensor

### Test Flow
1. Connect both FD and DUT
2. Configure parameters
3. Click "Start Test"
   - FD moves forward and records force
   - DUT moves forward
   - DUT retracts (if enabled)
4. View plots
5. Export CSV

## Hardware Configuration

### Arduino Pins
```cpp
#define DIR_PIN     2
#define STEP_PIN    3
#define ENABLE_PIN  4
#define HX711_DOUT  6
#define HX711_SCK   7
```

### Stepper Settings
```cpp
#define STEPS_PER_REV     6400
#define SCREW_PITCH       1.0    // mm per revolution
#define CALIBRATION_FACTOR 439   // HX711 calibration
```

## Joulescope Recording

Joulescope data is recorded separately using the official Joulescope software:

1. Open Joulescope UI
2. Set up desired view (current, voltage, power, energy)
3. Configure CSV export settings
4. Start recording before test
5. Stop recording after test
6. Correlate data manually using timestamps

## Troubleshooting

### FD Connection Issues
- Verify RPi IP address
- Check if fd_server_nojs.py is running
- Ensure port 5002 is not blocked

### DUT Connection Issues
- Check USB connection
- Verify correct COM port
- Try disconnecting and reconnecting

### Force Readings
- Zero before each test series
- Check HX711 wiring
- Verify calibration factor

### Stepper Issues
- Verify TMC2209 wiring
- Check enable pin
- Reduce speed if steps are lost
