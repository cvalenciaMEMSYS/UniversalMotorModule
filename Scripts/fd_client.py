"""
FD Client - TCP Client for Force-Deflection Setup

This module runs on the laptop and communicates with the FD Server
on the Raspberry Pi to control the force measurement setup.

Author: Energy Measurement Test System
Date: January 2026
"""

import socket
import threading
import time
import json
from dataclasses import dataclass, field
from typing import Optional, Callable, List, Tuple
from enum import Enum


class FDMode(Enum):
    MONITOR = "MONITOR"      # Passive force monitoring
    BACKDRIVE = "BACKDRIVE"  # Active backdrive test


@dataclass
class ForceDataPoint:
    """Single force measurement"""
    timestamp_ms: float
    force_n: float
    position_mm: float


@dataclass
class ForceData:
    """Complete force measurement dataset"""
    timestamps_ms: List[float] = field(default_factory=list)
    force_n: List[float] = field(default_factory=list)
    position_mm: List[float] = field(default_factory=list)
    total_samples: int = 0
    duration_ms: float = 0.0
    
    def clear(self):
        self.timestamps_ms.clear()
        self.force_n.clear()
        self.position_mm.clear()
        self.total_samples = 0
        self.duration_ms = 0.0
    
    def add_point(self, point: ForceDataPoint):
        self.timestamps_ms.append(point.timestamp_ms)
        self.force_n.append(point.force_n)
        self.position_mm.append(point.position_mm)
        self.total_samples = len(self.timestamps_ms)
        if self.timestamps_ms:
            self.duration_ms = self.timestamps_ms[-1]


@dataclass 
class FDStatus:
    """FD setup status"""
    connected: bool = False
    recording: bool = False
    mode: FDMode = FDMode.MONITOR
    sample_rate_hz: int = 100
    timeout_ms: int = 30000
    force_limit_n: float = 10.0
    speed_mm_s: float = 0.5
    travel_mm: float = 10.0
    arduino_connected: bool = False
    samples_collected: int = 0


class FDClient:
    """
    TCP Client for Force-Deflection setup control
    
    Connects to FD Server on Raspberry Pi and provides
    methods to configure and control force measurements.
    """
    
    DEFAULT_HOST = "192.168.1.12"
    DEFAULT_PORT = 5002
    TIMEOUT = 15.0  # Default timeout - increased for RPi responsiveness
    STATUS_TIMEOUT = 20.0  # Longer timeout for STATUS command (RPi can be slow)
    CONNECT_RETRIES = 3  # Number of connection attempts
    
    def __init__(self):
        self._socket: Optional[socket.socket] = None
        self._connected = False
        self._host = ""
        self._port = 0
        
        self._recording = False
        self._force_data = ForceData()
        self._status = FDStatus()
        
        self._receiver_thread: Optional[threading.Thread] = None
        self._running = False
        self._buffer = ""
        
        self._data_lock = threading.Lock()
        
        # Synchronization for command/response
        self._sync_lock = threading.Lock()  # Prevents receiver loop from reading during commands
        self._receiver_paused = False
        
        # Callbacks
        self._on_data: Optional[Callable[[ForceDataPoint], None]] = None
        self._on_recording_complete: Optional[Callable[[ForceData], None]] = None
        self._on_response: Optional[Callable[[str], None]] = None
    
    def set_data_callback(self, callback: Callable[[ForceDataPoint], None]):
        """Set callback for real-time data points"""
        self._on_data = callback
    
    def set_recording_complete_callback(self, callback: Callable[[ForceData], None]):
        """Set callback for when recording completes"""
        self._on_recording_complete = callback
    
    def set_response_callback(self, callback: Callable[[str], None]):
        """Set callback for raw response lines (for logging)"""
        self._on_response = callback
    
    def connect(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT) -> bool:
        """
        Connect to FD Server on Raspberry Pi
        
        Args:
            host: IP address of Raspberry Pi (default: 192.168.1.12)
            port: TCP port (default: 5002)
            
        Returns:
            True if connection successful
        """
        if self._connected:
            self.disconnect()
        
        last_error = None
        for attempt in range(self.CONNECT_RETRIES):
            try:
                self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self._socket.settimeout(self.TIMEOUT)
                self._socket.connect((host, port))
                
                self._host = host
                self._port = port
                self._connected = True
                
                # Start receiver thread
                self._running = True
                self._receiver_thread = threading.Thread(target=self._receiver_loop, daemon=True)
                self._receiver_thread.start()
                
                # Give server a moment to be ready
                time.sleep(0.2)
                
                # Verify connection with retries
                for ping_attempt in range(3):
                    response = self._send_command("PING", timeout=3.0)
                    if response and "PONG" in response:
                        self._status.connected = True
                        # Get initial status
                        self.get_status()
                        return True
                    time.sleep(0.3)
                
                # PING failed, try next connection attempt
                self.disconnect()
                    
            except Exception as e:
                last_error = e
                if self._socket:
                    try:
                        self._socket.close()
                    except:
                        pass
                self._socket = None
                self._connected = False
                time.sleep(0.5)  # Brief delay before retry
        
        print(f"FD Client connection failed after {self.CONNECT_RETRIES} attempts: {last_error}")
        self._connected = False
        return False
    
    def disconnect(self):
        """Disconnect from FD Server"""
        self._running = False
        
        if self._receiver_thread and self._receiver_thread.is_alive():
            self._receiver_thread.join(timeout=1.0)
        
        if self._socket:
            try:
                self._socket.close()
            except:
                pass
        
        self._socket = None
        self._connected = False
        self._status.connected = False
        self._host = ""
        self._port = 0
    
    @property
    def is_connected(self) -> bool:
        return self._connected and self._socket is not None
    
    @property
    def is_recording(self) -> bool:
        return self._recording
    
    @property
    def host(self) -> str:
        return self._host
    
    def _receiver_loop(self):
        """Background thread to receive data from server.
        
        This loop handles DATA| and END| messages during recording.
        It pauses when _send_command is waiting for a synchronous response.
        """
        while self._running and self._socket:
            # Check if we should pause for synchronous command
            if self._receiver_paused:
                time.sleep(0.01)  # Brief sleep while paused
                continue
                
            try:
                # Use lock to coordinate with _send_command
                with self._sync_lock:
                    if not self._running or self._socket is None:
                        break
                    self._socket.settimeout(0.1)  # Short timeout for responsiveness
                    try:
                        data = self._socket.recv(4096).decode('utf-8')
                        if not data:
                            break
                        
                        self._buffer += data
                        
                        # Process complete lines
                        while '\n' in self._buffer:
                            line, self._buffer = self._buffer.split('\n', 1)
                            if line.strip():
                                self._process_message(line.strip())
                    except socket.timeout:
                        pass  # Normal timeout, continue loop
                        
            except Exception as e:
                if self._running:
                    print(f"FD Client receiver error: {e}")
                break
        
        if self._running:
            self._connected = False
            self._status.connected = False
    
    def _process_message(self, message: str):
        """Process incoming message from server"""
        if self._on_response:
            self._on_response(message)
        
        if message.startswith("DATA|"):
            # Real-time data point
            try:
                parts = message.split('|')
                point = ForceDataPoint(
                    timestamp_ms=float(parts[1]),
                    force_n=float(parts[2]),
                    position_mm=float(parts[3]) if len(parts) > 3 else 0.0
                )
                
                with self._data_lock:
                    self._force_data.add_point(point)
                
                if self._on_data:
                    self._on_data(point)
                    
            except Exception as e:
                print(f"Data parse error: {e}")
        
        elif message.startswith("END|"):
            # Recording complete
            try:
                parts = message.split('|')
                with self._data_lock:
                    self._force_data.total_samples = int(parts[1])
                    self._force_data.duration_ms = float(parts[2])
                
                self._recording = False
                self._status.recording = False
                
                if self._on_recording_complete:
                    with self._data_lock:
                        self._on_recording_complete(self._force_data)
                        
            except Exception as e:
                print(f"End marker parse error: {e}")
    
    def _send_command(self, command: str, timeout: Optional[float] = None) -> Optional[str]:
        """Send command and wait for response.
        
        This method pauses the receiver loop and acquires the sync lock
        to ensure we can read the response without race conditions.
        """
        if not self.is_connected or self._socket is None:
            print(f"[FD] _send_command({command}): not connected or no socket")
            return None
        
        actual_timeout = timeout if timeout is not None else self.TIMEOUT
        print(f"[FD] _send_command({command}): timeout={actual_timeout}s")
        
        # Pause receiver loop and acquire sync lock
        self._receiver_paused = True
        time.sleep(0.02)  # Give receiver loop time to pause
        
        try:
            with self._sync_lock:
                self._socket.settimeout(actual_timeout)
                
                # Send command
                print(f"[FD] Sending: {command}")
                self._socket.send((command + '\n').encode('utf-8'))
                
                # Give server a moment to process and respond
                time.sleep(0.05)  # 50ms delay to ensure server processes
                
                # Wait for response (non-DATA response)
                buffer = ""
                start_time = time.time()
                while time.time() - start_time < actual_timeout:
                    try:
                        data = self._socket.recv(4096).decode('utf-8')
                        if data:
                            buffer += data
                            print(f"[FD] Received chunk ({len(data)} bytes): {data.strip()[:80]}...")
                            
                            # Process complete lines
                            while '\n' in buffer:
                                line, buffer = buffer.split('\n', 1)
                                line = line.strip()
                                if line and not line.startswith("DATA|"):
                                    print(f"[FD] Response line: {line}")
                                    return line
                    except socket.timeout:
                        elapsed = time.time() - start_time
                        print(f"[FD] Socket timeout after {elapsed:.1f}s (buffer: {len(buffer)} bytes)")
                        # Check if we have partial data in buffer
                        if buffer.strip():
                            line = buffer.strip().split('\n')[0]
                            if line and not line.startswith("DATA|"):
                                print(f"[FD] Using buffered response: {line}")
                                return line
                        break
                
                print(f"[FD] No response received")
                return None
                
        except Exception as e:
            print(f"[FD] Client send error: {e}")
            return None
        finally:
            # Always resume receiver loop
            self._receiver_paused = False
    
    # ==================== Configuration Commands ====================
    
    def set_mode(self, mode: FDMode) -> bool:
        """Set operating mode (MONITOR or BACKDRIVE)"""
        response = self._send_command(f"SET_MODE {mode.value}")
        if response and "OK" in response:
            self._status.mode = mode
            return True
        return False
    
    def set_speed(self, mm_per_sec: float) -> bool:
        """Set actuator speed for backdrive mode (mm/s)"""
        # Round to 1 decimal to avoid Arduino parseFloat issues with long decimals
        mm_per_sec = round(mm_per_sec, 1)
        response = self._send_command(f"SET_SPEED {mm_per_sec}")
        if response and "OK" in response:
            self._status.speed_mm_s = mm_per_sec
            return True
        return False
    
    def set_travel(self, mm: float) -> bool:
        """Set maximum travel distance for backdrive mode (mm)"""
        response = self._send_command(f"SET_TRAVEL {mm}")
        if response and "OK" in response:
            self._status.travel_mm = mm
            return True
        return False
    
    def set_sample_rate(self, hz: int) -> bool:
        """Set force sampling rate (Hz)"""
        response = self._send_command(f"SET_SAMPLE_RATE {hz}")
        if response and "OK" in response:
            self._status.sample_rate_hz = hz
            return True
        return False
    
    def set_timeout(self, ms: int) -> bool:
        """Set maximum recording duration (ms)"""
        response = self._send_command(f"SET_TIMEOUT {ms}")
        if response and "OK" in response:
            self._status.timeout_ms = ms
            return True
        return False
    
    def set_force_limit(self, newtons: float) -> bool:
        """Set force threshold to stop recording (N)"""
        response = self._send_command(f"SET_FORCE_LIMIT {newtons}")
        if response and "OK" in response:
            self._status.force_limit_n = newtons
            return True
        return False
    
    # ==================== Control Commands ====================
    
    def zero(self) -> bool:
        """Zero/tare the load cell"""
        response = self._send_command("ZERO", timeout=5.0)
        return response is not None and "OK" in response
    
    def get_force(self) -> Optional[Tuple[float, float]]:
        """
        Get current force reading from load cell.
        
        Returns:
            Tuple of (force_n, position_mm) or None on error
        """
        response = self._send_command("GET_FORCE", timeout=2.0)
        if response and response.startswith("FORCE|"):
            try:
                parts = response.split('|')
                force = float(parts[1])
                position = float(parts[2]) if len(parts) > 2 else 0.0
                return (force, position)
            except (ValueError, IndexError):
                pass
        return None
    
    def jog(self, distance_mm: float, wait_complete: bool = True) -> bool:
        """
        Manually jog the actuator
        
        Args:
            distance_mm: Distance to move (positive = extend, negative = retract)
            wait_complete: If True, wait for motion to complete (server blocks)
            
        Returns:
            True if command acknowledged (response contains OK or DONE)
        """
        print(f"[FD] jog({distance_mm}mm) called")
        
        # Use configured speed if set, otherwise use default
        current_speed = getattr(self._status, 'speed_mm_s', 1.0) or 1.0
        motion_time = abs(distance_mm) / current_speed
        timeout = motion_time + 10.0  # Add 5s buffer for safety
        print(f"[FD] jog timeout: {timeout:.1f}s (speed={current_speed:.1f} mm/s, motion ~{motion_time:.1f}s)")
        
        response = self._send_command(f"JOG {distance_mm}", timeout=timeout)
        print(f"[FD] jog response: {response}")
        if self._on_response:
            self._on_response(f"JOG response: {response}")
        # Accept OK, DONE, or complete as success indicators
        success = response is not None and any(s in response.upper() for s in ['OK', 'DONE', 'ACK', 'COMPLETE'])
        print(f"[FD] jog returning: {success}")
        return success
    
    def start(self) -> bool:
        """Start force recording"""
        if self._recording:
            return False
        
        with self._data_lock:
            self._force_data.clear()
        
        response = self._send_command("START")
        if response and "OK" in response:
            self._recording = True
            self._status.recording = True
            return True
        return False
    
    def stop(self) -> ForceData:
        """
        Stop force recording and return collected data
        
        Returns:
            ForceData object with all recorded measurements
        """
        if self._recording:
            self._send_command("STOP")
            self._recording = False
            self._status.recording = False
        
        # Wait a moment for any final data
        time.sleep(0.2)
        
        with self._data_lock:
            return ForceData(
                timestamps_ms=list(self._force_data.timestamps_ms),
                force_n=list(self._force_data.force_n),
                position_mm=list(self._force_data.position_mm),
                total_samples=self._force_data.total_samples,
                duration_ms=self._force_data.duration_ms
            )
    
    def get_status(self) -> FDStatus:
        """Get current FD setup status"""
        response = self._send_command("STATUS", timeout=self.STATUS_TIMEOUT)
        
        if response and response.startswith("STATUS|"):
            try:
                json_str = response.split('|', 1)[1]
                status_dict = json.loads(json_str)
                
                self._status.recording = status_dict.get('recording', False)
                self._status.arduino_connected = status_dict.get('arduino_connected', False)
                self._status.samples_collected = status_dict.get('samples', 0)
                
                config = status_dict.get('config', {})
                self._status.mode = FDMode(config.get('mode', 'MONITOR'))
                self._status.sample_rate_hz = config.get('sample_rate_hz', 100)
                self._status.timeout_ms = config.get('timeout_ms', 30000)
                self._status.force_limit_n = config.get('force_limit_n', 10.0)
                self._status.speed_mm_s = config.get('speed_mm_s', 0.5)
                self._status.travel_mm = config.get('travel_mm', 10.0)
                
            except Exception as e:
                print(f"Status parse error: {e}")
        
        return self._status
    
    def get_data(self) -> ForceData:
        """Get current recorded data"""
        with self._data_lock:
            return ForceData(
                timestamps_ms=list(self._force_data.timestamps_ms),
                force_n=list(self._force_data.force_n),
                position_mm=list(self._force_data.position_mm),
                total_samples=self._force_data.total_samples,
                duration_ms=self._force_data.duration_ms
            )


# =============================================================================
# Example usage and testing
# =============================================================================

if __name__ == "__main__":
    print("FD Client - Test Mode")
    print("=" * 50)
    
    client = FDClient()
    
    # Set up callbacks
    def on_data(point: ForceDataPoint):
        print(f"  Data: t={point.timestamp_ms:.1f}ms, F={point.force_n:.3f}N")
    
    def on_complete(data: ForceData):
        print(f"\nRecording complete: {data.total_samples} samples, {data.duration_ms:.1f}ms")
    
    client.set_data_callback(on_data)
    client.set_recording_complete_callback(on_complete)
    
    print(f"Connecting to {FDClient.DEFAULT_HOST}:{FDClient.DEFAULT_PORT}...")
    
    if client.connect():
        print("Connected!")
        
        # Get status
        status = client.get_status()
        print(f"\nStatus:")
        print(f"  Mode: {status.mode.value}")
        print(f"  Sample Rate: {status.sample_rate_hz} Hz")
        print(f"  Arduino: {'Connected' if status.arduino_connected else 'Disconnected'}")
        
        # Configure for monitor mode
        print("\nConfiguring for monitor mode...")
        client.set_mode(FDMode.MONITOR)
        client.set_sample_rate(100)
        client.set_timeout(5000)  # 5 seconds
        client.set_force_limit(5.0)  # 5 N
        
        # Zero load cell
        print("Zeroing load cell...")
        client.zero()
        
        # Start recording
        print("\nStarting 5-second recording...")
        client.start()
        
        # Wait for recording to complete
        time.sleep(6)
        
        # Get final data
        data = client.stop()
        print(f"\nCollected {len(data.timestamps_ms)} samples")
        
        if data.force_n:
            print(f"Max force: {max(data.force_n):.3f} N")
            print(f"Min force: {min(data.force_n):.3f} N")
        
        client.disconnect()
        print("\nDisconnected.")
    else:
        print("Failed to connect!")
        print("Make sure FD Server is running on Raspberry Pi")
