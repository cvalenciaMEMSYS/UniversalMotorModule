# Troubleshooting Guide

Common issues and solutions for the Universal Motor Module.

---

## 🔌 Connection Issues

### "TMC2209 Connection Failed"

**Symptoms:** Serial shows connection error, motor doesn't respond

**Solutions:**
1. **Check GND connections** - All grounds must be common
2. **Verify VIO power** - TMC2209 needs 3.3V on VIO pin
3. **Check UART wiring:**
   - 1kΩ resistor between GPIO 1 (TX) and GPIO 2 (RX)
   - GPIO 2 connects to TMC2209 PDN_UART pin
   - TMC2209 TX pin left floating
4. **Check driver address** - Default is 0b00 (MS1=LOW, MS2=LOW)

### USB Serial Not Working

**Symptoms:** No serial output, device not detected

**Solutions:**
1. ESP32-S3 USB takes 1-3 seconds to enumerate - wait after power on
2. Press reset button after upload
3. Verify `platformio.ini` has `-DARDUINO_USB_CDC_ON_BOOT=1`
4. Try different USB cable (some are charge-only)

---

## ⚙️ Motor Issues

### Motor Not Moving

**Symptoms:** Commands accepted but motor doesn't move

**Checklist:**
1. **Enable pin** - Should be LOW for enabled. Check with `status`
2. **Motor current** - Set with `set current <mA>` (try 800-1200)
3. **Motor power** - VS needs 12-28V (separate from logic power)
4. **Test connection** - Send `diag` to check TMC2209 status
5. **Try basic move** - `enable` then `move 100`

### Motor Stuttering/Vibrating

**Symptoms:** Motor vibrates in place, doesn't rotate smoothly

**Solutions:**
1. **Increase current** - Motor may be underpowered
   ```
   set current 1200
   ```
2. **Reduce speed** - Start slow, increase gradually
   ```
   set speed 1000
   set accel 500
   ```
3. **Check coil wiring** - Phases may be swapped
4. **Switch to SpreadCycle** - More torque at high speeds
   ```
   spreadcycle
   ```

### Motor Loses Steps

**Symptoms:** Position drifts, motor doesn't reach target

**Solutions:**
1. **Reduce speed/acceleration** - Motor can't keep up
2. **Increase current** - More torque needed
3. **Check mechanical load** - Too much resistance
4. **Verify with oscilloscope** - Should see clean pulse train

### Weak Holding Torque

**Symptoms:** Motor can be easily turned by hand when stopped

**Solutions:**
1. **Set hold current:**
   ```
   set ihold 75   # 75% of run current for holding
   ```
2. **Disable auto-disable:**
   ```
   set autodisable off
   ```
3. **Check standstill** - Use `status` to verify motor is enabled

---

## 🌡️ Thermal Issues

### Driver Overheating

**Symptoms:** Motor stops, driver gets very hot, thermal warnings

**Solutions:**
1. **Add heatsink** - TMC2209 generates heat under load
2. **Reduce current** - Only use what's needed
   ```
   set current 800
   ```
3. **Add cooling** - Fan for continuous operation
4. **Check for shorts** - Use `diag` to check status flags
5. **Reduce microstepping** - Higher microsteps = more heat

### Thermal Shutdown

**Symptoms:** Driver stops, recovers after cooling

**Check with:**
```
diag
```

Look for "Overtemp Warning" or "Overtemp Prewarning" flags.

---

## 📡 Communication Issues

### UART Errors

**Symptoms:** Intermittent communication, wrong values read

**Solutions:**
1. **Check baud rate** - Must be 115200
2. **Verify wiring** - Use 1kΩ resistor method
3. **Try shorter wires** - Long wires pick up noise
4. **Add decoupling capacitor** - 100nF near TMC2209 VIO

### Commands Not Recognized

**Symptoms:** "Unknown command" errors

**Check:**
1. Commands are case-sensitive
2. Use `help` to see available commands
3. Spaces matter: `set speed 1000` not `setspeed1000`

---

## 💡 Status LED Issues

### LED Not Showing Correct Color

**LED Colors:**
| Color | Meaning |
|-------|---------|
| Blue | Initializing |
| Green | Ready |
| Cyan | TMC2209 detected |
| Magenta | TMC2208 detected |
| Yellow | DC Motor mode |
| Pulsing | Motor moving |
| Red | Error |

**If LED is wrong color:**
1. Check GPIO 48 connection
2. Verify driver detection pins (GPIO 11, 12)
3. Send `status` to see actual detected driver

---

## 🔧 Configuration Issues

### Settings Not Saving

**Note:** Settings are NOT saved to flash. After power cycle, defaults are restored.

**Workaround:** Create a startup script or add initialization to your code.

### Speed Too Slow/Fast

**Speed is in steps/second:**
```
set speed 3200    # 1 revolution/sec at 16 microsteps
set speed 32000   # 10 revolutions/sec
```

**Calculate:** `steps/sec = (RPM × steps_per_rev × microsteps) / 60`

### Acceleration Too Slow

**Acceleration is in steps/second²:**
```
set accel 10000   # Moderate acceleration
set accel 50000   # Fast acceleration
set accel 0       # Constant velocity (no accel)
```

---

## 🛠️ Debug Commands

Use these to diagnose issues:

```
status      # Show motor state, position, settings
diag        # TMC2209 diagnostics (errors, temperature)
help        # List all available commands
get pos     # Current position
get speed   # Current actual speed
get rampstate  # Acceleration phase
```

---

## 📞 Still Having Issues?

1. Check the [command-protocol.md](command-protocol.md) for correct syntax
2. Review [Quick_Wiring_Guide](../Quick_Wiring_Guide_Custom_Pins.md) for connections
3. Use oscilloscope to verify STEP pin pulses
4. Check TMC2209 datasheet for register details

---

*Last updated: January 2026*
