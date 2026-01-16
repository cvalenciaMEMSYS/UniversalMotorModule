# Energy Measurement Test System

Automated data gathering system for micro stepper motor energy measurements.

## Overview

This system coordinates three measurement devices to capture force and power data simultaneously:

1. **DUT (Device Under Test)** - Universal Motor Module via serial USB
2. **FD Setup (Force/Displacement)** - Raspberry Pi + Arduino via TCP
3. **Joulescope JS220** - Power analyzer via USB

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        orchestrator.py (Main GUI)                       │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────────────────┐│
│  │ Left Panel   │  │ Right Panel  │  │ Test Sequencer                 ││
│  │ - Config     │  │ - Real-time  │  │ - Start/Stop                   ││
│  │ - Controls   │  │   Plots      │  │ - Data collection              ││
│  │ - Manual Jog │  │ - Results    │  │ - HDF5 storage                 ││
│  └──────────────┘  └──────────────┘  └────────────────────────────────┘│
└───────────────┬────────────────┬────────────────┬──────────────────────┘
                │                │                │
    ┌───────────▼───┐  ┌────────▼────────┐  ┌───▼─────────────────┐
    │dut_controller │  │ fd_client.py    │  │ joulescope_interface│
    │    .py        │  │                 │  │       .py           │
    │ Serial 115200 │  │ TCP 192.168.1.12│  │ USB                 │
    └───────┬───────┘  └────────┬────────┘  └──────────┬──────────┘
            │                   │                      │
    ┌───────▼───────┐  ┌────────▼────────┐  ┌─────────▼──────────┐
    │ Universal     │  │ Raspberry Pi    │  │ Joulescope JS220   │
    │ Motor Module  │  │ (fd_server.py)  │  │                    │
    │ ESP32-S3      │  │      │          │  │ V, I, P, E meas.   │
    └───────────────┘  │      ▼          │  └────────────────────┘
                       │   Arduino       │
                       │   Load Cell     │
                       └─────────────────┘
```

## Files

| File | Description | Location |
|------|-------------|----------|
| `orchestrator.py` | Main PyQt6 GUI application | Laptop |
| `dut_controller.py` | DUT serial interface | Laptop |
| `fd_client.py` | FD TCP client | Laptop |
| `fd_server.py` | FD TCP server | Raspberry Pi |
| `joulescope_interface.py` | Joulescope USB interface | Laptop |
| `alignment_tool.py` | Data alignment GUI | Laptop |
| `utils/hdf5_manager.py` | HDF5 data storage | Laptop |
| `utils/plot_generator.py` | Plot generation | Laptop |

## Installation

### Laptop (Main System)

```bash
cd Scripts
pip install -r requirements.txt
```

### Raspberry Pi

```bash
# Copy fd_server.py to Raspberry Pi
scp fd_server.py pi@192.168.1.12:/home/pi/

# On Raspberry Pi:
pip install pyserial numpy
python fd_server.py
```

## Usage

### 1. Start the FD Server (Raspberry Pi)

```bash
python fd_server.py --port 5002
```

### 2. Launch the Main GUI (Laptop)

```bash
python orchestrator.py
```

### 3. Configure Test

1. **Test ID**: Set a unique identifier (e.g., `M1-CV-3`)
2. **Motor**: Select motor from dropdown
3. **DUT**: Connect to COM port, set motion profile
4. **FD**: Connect to Raspberry Pi IP, select MONITOR or BACKDRIVE mode
5. **Joulescope**: Connect, set voltage, sample rate

### 4. Run Test

1. Click **START TEST**
2. Confirm voltage setting in dialog
3. Watch real-time plots
4. Click **STOP** when done (or auto-stop after motion)

### 5. Post-Processing

- **Align Data**: Open alignment tool to set start/stop markers
- **Export Plots**: Save PNG plots to folder

## Data Format

Data is stored in HDF5 format:

```
root/
├── motor_info
│   ├── name
│   └── specs
└── <test_id>/
    ├── metadata
    │   ├── test_id
    │   ├── timestamp
    │   ├── profile_type
    │   ├── voltage
    │   ├── speed_sps
    │   └── ...
    ├── raw_data/
    │   ├── force
    │   │   ├── timestamp_ms
    │   │   ├── force_n
    │   │   └── position_mm
    │   └── power
    │       ├── timestamp_ms
    │       ├── current_a
    │       ├── voltage_v
    │       ├── power_w
    │       └── energy_j
    ├── alignment/
    │   ├── power_start_ms
    │   ├── power_stop_ms
    │   ├── force_start_ms
    │   └── force_stop_ms
    ├── computed/
    │   ├── max_force_n
    │   ├── avg_power_w
    │   ├── total_energy_j
    │   └── ...
    └── plots/
        ├── combined_png
        └── ...
```

## Motion Profiles

| Profile | Description | Parameters |
|---------|-------------|------------|
| Constant Velocity | Fixed speed, no ramps | Speed |
| Trapezoidal | Linear acceleration ramps | Speed, Acceleration |
| S-Curve | Smooth jerk-limited ramps | Speed, Acceleration |

## FD Modes

| Mode | Description |
|------|-------------|
| MONITOR | Passive measurement only |
| BACKDRIVE | Active resistance against DUT |

## Troubleshooting

### DUT Not Responding
- Check COM port selection
- Verify baud rate is 115200
- Try power cycling the motor module

### FD Connection Refused
- Verify Raspberry Pi IP address
- Check that fd_server.py is running
- Ensure port 5002 is not blocked

### Joulescope Not Found
- Install Joulescope driver
- Use "Mock (Testing)" for development without hardware
- Check USB connection

### Data Alignment Issues
- Use Auto-Detect for threshold-based detection
- Manually click to place markers
- Check that start < stop for both traces

## Testing Without Hardware

Use the mock interfaces for development:

1. Select "Mock (Testing)" for Joulescope
2. Run without DUT connection (skip motor motion)
3. Run without FD connection (no force data)

The mock Joulescope generates realistic sine-wave data for testing the GUI.
