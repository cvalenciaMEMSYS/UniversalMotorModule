# Changelog

All notable changes to the Universal Motor Module firmware.

## [2.0.0] - January 2026

### 🚀 Major Changes

**FastAccelStepper Migration**
- Replaced custom MCPWM pulse generation with FastAccelStepper library v0.33.9
- Achieves up to 200kHz+ step rates (previously capped at 50kHz)
- Hardware-based position tracking using ESP32 pulse counter
- Eliminates pulse gaps during acceleration changes

### ✨ New Features

**Motion Commands**
- `run forward` - Continuous rotation with acceleration
- `run backward` - Continuous rotation with acceleration  
- `brake` - Smooth decelerated stop

**Configuration Commands**
- `set cubesteps <N>` - S-curve (cubic) acceleration over N steps
- `set ihold <0-100>` - Hold current as percentage of run current
- `set autodisable on/off` - Auto-enable/disable for power saving
- `set accel 0` - Constant velocity mode (no acceleration)

**Query Commands**
- `get pos` - Current position
- `get target` - Target position for current move
- `get speed` - Current speed in steps/second
- `get rampstate` - Acceleration state (-1=decel, 0=coast, 1=accel)

### 🐛 Bugs Fixed

- **Step skipping** - Fixed by synchronous timer updates
- **Low-speed extra steps** - Fixed with hardware pulse generation
- **Position overshoot** - Fixed with tested acceleration algorithms
- **50kHz frequency cap** - Now supports 200kHz+
- **Position tracking drift** - Now uses hardware counter

### 📚 Documentation

- Consolidated architecture docs into single `architecture.md`
- Consolidated ESP32-S3 docs into single `esp32-s3-hardware.md`
- Removed obsolete MCPWM migration documentation
- Added `fastaccelstepper.md` library reference
- Added `troubleshooting.md` common issues guide
- Updated `command-protocol.md` with new commands

### 🗑️ Removed

- Custom MCPWM pulse generation code (`MCPWMStepper.cpp/.h`)
- Custom acceleration math (`AccelerationProfile.h`, `MotionMath.h`)
- Obsolete migration planning documents

---

## [1.0.0] - January 2026

### Initial Release

- TMC2209 stepper driver support via UART
- TMC2208 stepper driver support (fallback mode)
- DC motor support via RZ7899 H-bridge
- Auto-detection of motor driver type
- WS2812 RGB status LED
- Serial command interface
- StealthChop/SpreadCycle modes
- StallGuard diagnostics
- Basic motion control (move, moveto, stop)
- Current and microstepping configuration

---

## Version Naming

- **Major** (X.0.0): Breaking changes, new architecture
- **Minor** (0.X.0): New features, backward compatible
- **Patch** (0.0.X): Bug fixes, minor improvements
