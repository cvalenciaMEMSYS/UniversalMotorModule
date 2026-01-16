"""
DUT Controller - Serial interface to Universal Motor Module (UMM)

This module provides a Python interface to control the UMM via serial port.
Supports TMC2209/TMC2208 stepper drivers and DC motors.

Author: Energy Measurement Test System
Date: January 2026
"""

import serial
import serial.tools.list_ports
import threading
import time
import re
from dataclasses import dataclass
from typing import Optional, Callable, List
from enum import Enum


class MotionProfile(Enum):
    """Motion profile types"""
    CONSTANT = "constant"       # No acceleration, instant speed
    TRAPEZOIDAL = "trapezoidal" # Linear acceleration ramp
    SCURVE = "scurve"          # Jerk-limited smooth motion


class RampState(Enum):
    """Ramp generator states"""
    IDLE = "IDLE"
    ACCELERATING = "ACCELERATING"
    CRUISING = "CRUISING"
    DECELERATING = "DECELERATING"
    UNKNOWN = "UNKNOWN"


@dataclass
class MotorStatus:
    """Motor status data structure"""
    driver_type: str = "Unknown"
    enabled: bool = False
    position: int = 0
    target: int = 0
    moving: bool = False
    current_speed: int = 0
    run_current_ma: int = 0
    configured_speed: int = 0
    configured_accel: int = 0
    configured_cubesteps: int = 0
    ramp_state: RampState = RampState.UNKNOWN


class DUTController:
    """
    Controller class for Universal Motor Module (UMM)
    
    Provides methods to configure and control stepper motors via serial.
    Handles command/response protocol with acknowledgments.
    """
    
    BAUD_RATE = 115200
    TIMEOUT = 2.0  # seconds
    RESPONSE_TIMEOUT = 5.0  # seconds for motion commands
    
    def __init__(self):
        self._serial: Optional[serial.Serial] = None
        self._connected = False
        self._port = ""
        self._lock = threading.Lock()
        self._response_buffer: List[str] = []
        self._reader_thread: Optional[threading.Thread] = None
        self._running = False
        self._status = MotorStatus()
        self._on_response: Optional[Callable[[str], None]] = None
        
    @staticmethod
    def list_ports() -> List[str]:
        """List available serial ports"""
        ports = serial.tools.list_ports.comports()
        return [port.device for port in ports]
    
    def set_response_callback(self, callback: Callable[[str], None]):
        """Set callback for raw response lines (for logging/display)"""
        self._on_response = callback
        
    def connect(self, port: str) -> bool:
        """
        Connect to DUT on specified serial port
        
        Args:
            port: Serial port name (e.g., 'COM3' or '/dev/ttyUSB0')
            
        Returns:
            True if connection successful, False otherwise
        """
        if self._connected:
            self.disconnect()
            
        try:
            self._serial = serial.Serial(
                port=port,
                baudrate=self.BAUD_RATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.TIMEOUT
            )
            
            # Wait for device to initialize
            time.sleep(2.0)
            
            # Clear any startup messages
            self._serial.reset_input_buffer()
            
            # Start reader thread
            self._running = True
            self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
            self._reader_thread.start()
            
            self._connected = True
            self._port = port
            
            # Verify connection with status command
            response = self._send_command("status", timeout=3.0)
            if response is None:
                self.disconnect()
                return False
                
            return True
            
        except serial.SerialException as e:
            print(f"Connection error: {e}")
            self._connected = False
            return False
    
    def disconnect(self):
        """Disconnect from DUT"""
        self._running = False
        
        if self._reader_thread and self._reader_thread.is_alive():
            self._reader_thread.join(timeout=1.0)
            
        if self._serial and self._serial.is_open:
            try:
                self._serial.close()
            except:
                pass
                
        self._serial = None
        self._connected = False
        self._port = ""
        
    @property
    def is_connected(self) -> bool:
        """Check if connected to DUT"""
        return self._connected and self._serial is not None and self._serial.is_open
    
    @property
    def port(self) -> str:
        """Get current port name"""
        return self._port
    
    def _reader_loop(self):
        """Background thread to read serial responses"""
        while self._running and self._serial and self._serial.is_open:
            try:
                if self._serial.in_waiting > 0:
                    line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        with self._lock:
                            self._response_buffer.append(line)
                        if self._on_response:
                            self._on_response(line)
                else:
                    time.sleep(0.01)
            except Exception as e:
                if self._running:
                    print(f"Reader error: {e}")
                break
    
    def _send_command(self, command: str, timeout: Optional[float] = None) -> Optional[List[str]]:
        """
        Send command and collect response lines
        
        Args:
            command: Command string to send
            timeout: Response timeout in seconds
            
        Returns:
            List of response lines, or None on error
        """
        if not self.is_connected or self._serial is None:
            return None
            
        actual_timeout = timeout if timeout is not None else self.TIMEOUT
        
        with self._lock:
            self._response_buffer.clear()
            
        # Send command
        try:
            cmd_bytes = (command + '\n').encode('utf-8')
            self._serial.write(cmd_bytes)
            self._serial.flush()
        except Exception as e:
            print(f"Send error: {e}")
            return None
        
        # Wait for response
        time.sleep(0.1)  # Give device time to respond
        
        start_time = time.time()
        while time.time() - start_time < actual_timeout:
            with self._lock:
                if self._response_buffer:
                    # Check for completion indicators
                    buffer_text = '\n'.join(self._response_buffer)
                    if any(end in buffer_text for end in ['>', 'OK', 'Error', 'completed', 'IDLE']):
                        return list(self._response_buffer)
            time.sleep(0.05)
        
        # Return whatever we got
        with self._lock:
            return list(self._response_buffer) if self._response_buffer else None
    
    def send_raw_command(self, command: str, timeout: float = 2.0) -> Optional[List[str]]:
        """
        Send a raw command string to DUT (for manual control)
        
        Args:
            command: Any command string
            timeout: Response timeout
            
        Returns:
            List of response lines
        """
        return self._send_command(command, timeout)
    
    # ==================== Configuration Commands ====================
    
    def configure_profile(self, profile: MotionProfile, speed: int, 
                         accel: int = 0, cubesteps: int = 0) -> bool:
        """
        Configure motion profile
        
        Args:
            profile: Motion profile type
            speed: Maximum speed in steps/second
            accel: Acceleration in steps/second² (0 for constant velocity)
            cubesteps: S-curve jerk parameter (0 for trapezoidal)
            
        Returns:
            True if configuration successful
        """
        success = True
        
        # Set speed
        response = self._send_command(f"set speed {speed}")
        success = success and response is not None
        
        # Set acceleration based on profile
        if profile == MotionProfile.CONSTANT:
            response = self._send_command("set accel 0")
        else:
            response = self._send_command(f"set accel {accel}")
        success = success and response is not None
        
        # Set cubesteps for S-curve
        if profile == MotionProfile.SCURVE:
            response = self._send_command(f"set cubesteps {cubesteps}")
            success = success and response is not None
        elif profile == MotionProfile.TRAPEZOIDAL:
            response = self._send_command("set cubesteps 0")
            success = success and response is not None
            
        if success:
            self._status.configured_speed = speed
            self._status.configured_accel = accel
            self._status.configured_cubesteps = cubesteps
            
        return success
    
    def set_speed(self, speed: int) -> bool:
        """Set maximum speed in steps/second"""
        response = self._send_command(f"set speed {speed}")
        if response:
            self._status.configured_speed = speed
        return response is not None
    
    def set_accel(self, accel: int) -> bool:
        """Set acceleration in steps/second²"""
        response = self._send_command(f"set accel {accel}")
        if response:
            self._status.configured_accel = accel
        return response is not None
    
    def set_cubesteps(self, cubesteps: int) -> bool:
        """Set S-curve jerk parameter"""
        response = self._send_command(f"set cubesteps {cubesteps}")
        if response:
            self._status.configured_cubesteps = cubesteps
        return response is not None
    
    def set_current(self, ma: int) -> bool:
        """Set motor run current in milliamps"""
        response = self._send_command(f"set current {ma}")
        if response:
            self._status.run_current_ma = ma
        return response is not None
    
    def set_ihold(self, percent: int) -> bool:
        """Set hold current as percentage of run current (0-100)"""
        response = self._send_command(f"set ihold {percent}")
        return response is not None
    
    def set_microsteps(self, microsteps: int) -> bool:
        """Set microstepping divisor (1, 2, 4, 8, 16, 32, 64, 128, 256)"""
        response = self._send_command(f"set microsteps {microsteps}")
        return response is not None
    
    def set_autodisable(self, enabled: bool) -> bool:
        """Enable or disable auto enable/disable behavior"""
        state = "on" if enabled else "off"
        response = self._send_command(f"set autodisable {state}")
        return response is not None
    
    # ==================== Motor Control Commands ====================
    
    def enable(self) -> bool:
        """Enable motor driver"""
        response = self._send_command("enable")
        if response:
            self._status.enabled = True
        return response is not None
    
    def disable(self) -> bool:
        """Disable motor driver (motor will coast)"""
        response = self._send_command("disable")
        if response:
            self._status.enabled = False
        return response is not None
    
    def stop(self) -> bool:
        """Emergency stop (immediate halt)"""
        response = self._send_command("stop")
        if response:
            self._status.moving = False
        return response is not None
    
    def brake(self) -> bool:
        """Controlled stop with deceleration"""
        response = self._send_command("brake")
        return response is not None
    
    # ==================== Motion Commands ====================
    
    def move(self, steps: int, wait: bool = True, timeout: float = 30.0) -> bool:
        """
        Relative move by specified number of steps
        
        Args:
            steps: Number of steps (positive or negative)
            wait: If True, wait for motion to complete
            timeout: Maximum wait time in seconds
            
        Returns:
            True if move command accepted (and completed if wait=True)
        """
        response = self._send_command(f"move {steps}", timeout=2.0)
        if response is None:
            return False
            
        self._status.moving = True
        
        if wait:
            return self.wait_for_stop(timeout)
        return True
    
    def move_absolute(self, position: int, wait: bool = True, timeout: float = 30.0) -> bool:
        """
        Move to absolute position
        
        Args:
            position: Target position (must be >= 0)
            wait: If True, wait for motion to complete
            timeout: Maximum wait time
            
        Returns:
            True if move successful
        """
        if position < 0:
            print("Error: Position must be >= 0")
            return False
            
        response = self._send_command(f"abs {position}", timeout=2.0)
        if response is None:
            return False
            
        self._status.moving = True
        
        if wait:
            return self.wait_for_stop(timeout)
        return True
    
    def run_forward(self) -> bool:
        """Run continuously forward at configured speed"""
        response = self._send_command("run forward")
        if response:
            self._status.moving = True
        return response is not None
    
    def run_backward(self) -> bool:
        """Run continuously backward at configured speed"""
        response = self._send_command("run backward")
        if response:
            self._status.moving = True
        return response is not None
    
    def run_timed(self, duration_ms: int, forward: bool = True) -> bool:
        """
        Run motor for a specified duration then stop
        
        Args:
            duration_ms: Duration in milliseconds
            forward: Direction (True for forward)
            
        Returns:
            True if run completed
        """
        # Start continuous run
        if forward:
            if not self.run_forward():
                return False
        else:
            if not self.run_backward():
                return False
        
        # Wait for duration
        time.sleep(duration_ms / 1000.0)
        
        # Stop
        return self.stop()
    
    def home(self, timeout: float = 30.0) -> bool:
        """
        Find home position using StallGuard (TMC2209 only)
        
        Args:
            timeout: Maximum homing time
            
        Returns:
            True if homing successful
        """
        response = self._send_command("home", timeout=timeout)
        return response is not None and "Error" not in '\n'.join(response)
    
    # ==================== Query Commands ====================
    
    def get_position(self) -> Optional[int]:
        """Get current position in steps"""
        response = self._send_command("get pos")
        if response:
            for line in response:
                match = re.search(r'Position:\s*(-?\d+)', line)
                if match:
                    pos = int(match.group(1))
                    self._status.position = pos
                    return pos
        return None
    
    def get_target(self) -> Optional[int]:
        """Get target position in steps"""
        response = self._send_command("get target")
        if response:
            for line in response:
                match = re.search(r'Target:\s*(-?\d+)', line)
                if match:
                    target = int(match.group(1))
                    self._status.target = target
                    return target
        return None
    
    def get_speed(self) -> Optional[int]:
        """Get current actual speed in steps/second"""
        response = self._send_command("get speed")
        if response:
            for line in response:
                match = re.search(r'speed:\s*(-?\d+)', line, re.IGNORECASE)
                if match:
                    speed = int(match.group(1))
                    self._status.current_speed = speed
                    return speed
        return None
    
    def get_rampstate(self) -> RampState:
        """Get current ramp generator state"""
        response = self._send_command("get rampstate")
        if response:
            for line in response:
                for state in RampState:
                    if state.value in line.upper():
                        self._status.ramp_state = state
                        return state
        return RampState.UNKNOWN
    
    def is_moving(self) -> bool:
        """Check if motor is currently moving"""
        speed = self.get_speed()
        if speed is not None:
            self._status.moving = speed != 0
            return speed != 0
        
        state = self.get_rampstate()
        moving = state not in [RampState.IDLE, RampState.UNKNOWN]
        self._status.moving = moving
        return moving
    
    def wait_for_stop(self, timeout: float = 30.0) -> bool:
        """
        Wait for motor to stop moving
        
        Args:
            timeout: Maximum wait time in seconds
            
        Returns:
            True if motor stopped, False if timeout
        """
        start_time = time.time()
        
        # Initial delay to let motion start
        time.sleep(0.2)
        
        while time.time() - start_time < timeout:
            if not self.is_moving():
                self._status.moving = False
                return True
            time.sleep(0.1)
            
        return False
    
    def get_status(self) -> MotorStatus:
        """
        Get comprehensive motor status
        
        Returns:
            MotorStatus dataclass with all status information
        """
        response = self._send_command("status", timeout=3.0)
        
        if response:
            status_text = '\n'.join(response)
            
            # Parse driver type
            match = re.search(r'Driver[:\s]+(\w+)', status_text)
            if match:
                self._status.driver_type = match.group(1)
            
            # Parse enabled state
            self._status.enabled = 'Yes' in status_text and 'Enabled' in status_text
            
            # Parse position
            match = re.search(r'Position:\s*(-?\d+)', status_text)
            if match:
                self._status.position = int(match.group(1))
            
            # Parse target
            match = re.search(r'Target:\s*(-?\d+)', status_text)
            if match:
                self._status.target = int(match.group(1))
            
            # Parse moving state
            self._status.moving = 'Moving:' in status_text and 'Yes' in status_text.split('Moving:')[1][:20]
            
            # Parse current speed
            match = re.search(r'Current Speed:\s*(-?\d+)', status_text)
            if match:
                self._status.current_speed = int(match.group(1))
            
            # Parse run current
            match = re.search(r'Run Current:\s*(\d+)', status_text)
            if match:
                self._status.run_current_ma = int(match.group(1))
        
        return self._status
    
    def get_diagnostics(self) -> Optional[List[str]]:
        """Get full diagnostics output"""
        return self._send_command("diag", timeout=3.0)
    
    def reconfigure(self) -> bool:
        """Re-apply all settings (useful after power glitch)"""
        response = self._send_command("reconfigure")
        return response is not None
    
    def reboot(self) -> bool:
        """Soft reset the ESP32"""
        response = self._send_command("reboot")
        if response:
            self._connected = False
            time.sleep(3.0)  # Wait for reboot
        return response is not None


# =============================================================================
# Example usage and testing
# =============================================================================

if __name__ == "__main__":
    print("DUT Controller - Test Mode")
    print("=" * 50)
    
    # List available ports
    ports = DUTController.list_ports()
    print(f"Available ports: {ports}")
    
    if not ports:
        print("No serial ports found!")
        exit(1)
    
    # Create controller
    dut = DUTController()
    
    # Set up response callback for logging
    def log_response(line):
        print(f"  > {line}")
    
    dut.set_response_callback(log_response)
    
    # Connect to first available port (or specify your port)
    port = ports[0]  # Change this to your port
    print(f"\nConnecting to {port}...")
    
    if dut.connect(port):
        print("Connected!")
        
        # Get status
        print("\nGetting status...")
        status = dut.get_status()
        print(f"Driver: {status.driver_type}")
        print(f"Enabled: {status.enabled}")
        print(f"Position: {status.position}")
        
        # Configure for constant velocity test
        print("\nConfiguring constant velocity profile...")
        dut.configure_profile(MotionProfile.CONSTANT, speed=100)
        dut.set_current(400)
        
        # Enable motor
        print("\nEnabling motor...")
        dut.enable()
        
        # Do a short move
        print("\nMoving 100 steps...")
        dut.move(100, wait=True)
        
        # Get position
        pos = dut.get_position()
        print(f"Position after move: {pos}")
        
        # Disable motor
        print("\nDisabling motor...")
        dut.disable()
        
        # Disconnect
        dut.disconnect()
        print("\nDisconnected.")
    else:
        print("Failed to connect!")
