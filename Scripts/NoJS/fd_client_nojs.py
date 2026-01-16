"""
FD Client NoJS - Force-Deflection Client Library (Simplified)
==============================================================

Client library to communicate with fd_server_nojs.py.
Provides a clean API for controlling the FD setup.

Usage:
    from fd_client_nojs import FDClientNoJS, ForceData
    
    client = FDClientNoJS()
    if client.connect("192.168.1.12"):
        client.set_mode("DRIVE")
        client.set_speed(5.0)
        data = client.move(10.0)  # Returns ForceData
        print(f"Captured {data.sample_count} samples")
        client.disconnect()
"""

import socket
import base64
import json
from dataclasses import dataclass, field
from typing import Optional, List, Callable
from enum import Enum
import time

# =============================================================================
# Constants
# =============================================================================

DEFAULT_PORT = 5002
DEFAULT_TIMEOUT = 30.0
LONG_TIMEOUT = 120.0  # For movement commands

# =============================================================================
# Data Classes
# =============================================================================

class FDMode(Enum):
    MONITOR = "MONITOR"
    DRIVE = "DRIVE"

@dataclass
class ForceData:
    """Container for force measurement data"""
    timestamps_ms: List[float] = field(default_factory=list)
    force_n: List[float] = field(default_factory=list)
    
    @property
    def sample_count(self) -> int:
        return len(self.timestamps_ms)
    
    @property
    def duration_ms(self) -> float:
        if len(self.timestamps_ms) >= 2:
            return self.timestamps_ms[-1] - self.timestamps_ms[0]
        return 0.0
    
    @property
    def max_force(self) -> float:
        return max(self.force_n) if self.force_n else 0.0
    
    @property
    def min_force(self) -> float:
        return min(self.force_n) if self.force_n else 0.0
    
    @property
    def avg_force(self) -> float:
        return sum(self.force_n) / len(self.force_n) if self.force_n else 0.0
    
    def to_csv(self) -> str:
        """Convert to CSV string"""
        lines = ["timestamp_ms,force_n"]
        for ts, force in zip(self.timestamps_ms, self.force_n):
            lines.append(f"{ts:.1f},{force:.5f}")
        return "\n".join(lines)
    
    @staticmethod
    def from_base64_csv(data: str) -> 'ForceData':
        """Parse from base64-encoded CSV"""
        result = ForceData()
        try:
            csv_bytes = base64.b64decode(data)
            csv_str = csv_bytes.decode('utf-8')
            lines = csv_str.strip().split('\n')
            
            # Skip header
            for line in lines[1:]:
                parts = line.split(',')
                if len(parts) >= 2:
                    result.timestamps_ms.append(float(parts[0]))
                    result.force_n.append(float(parts[1]))
        except Exception as e:
            print(f"[FD] Error parsing force data: {e}")
        
        return result

@dataclass
class FDStatus:
    """FD setup status"""
    mode: FDMode = FDMode.MONITOR
    speed_mm_s: float = 2.0
    streaming: bool = False
    arduino_connected: bool = False
    connected: bool = False

# =============================================================================
# FD Client Class
# =============================================================================

class FDClientNoJS:
    """
    Client for communicating with FD Server NoJS
    
    Example:
        client = FDClientNoJS()
        if client.connect("192.168.1.12"):
            client.set_mode("DRIVE")
            data = client.move(10.0)
            print(f"Captured {data.sample_count} samples")
    """
    
    def __init__(self):
        self._socket: Optional[socket.socket] = None
        self._host: str = ""
        self._port: int = DEFAULT_PORT
        self._status = FDStatus()
        self._on_log: Optional[Callable[[str], None]] = None
    
    @property
    def is_connected(self) -> bool:
        return self._socket is not None and self._status.connected
    
    @property
    def status(self) -> FDStatus:
        return self._status
    
    def set_log_callback(self, callback: Callable[[str], None]):
        """Set callback for log messages"""
        self._on_log = callback
    
    def _log(self, msg: str):
        """Log a message"""
        print(f"[FD] {msg}")
        if self._on_log:
            self._on_log(msg)
    
    # =========================================================================
    # Connection
    # =========================================================================
    
    def connect(self, host: str, port: int = DEFAULT_PORT, timeout: float = 10.0) -> bool:
        """
        Connect to FD server
        
        Args:
            host: Server IP address
            port: Server port (default 5002)
            timeout: Connection timeout in seconds
            
        Returns:
            True if connection successful
        """
        self._host = host
        self._port = port
        
        try:
            self._log(f"Connecting to {host}:{port}...")
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(timeout)
            self._socket.connect((host, port))
            
            # Test connection
            if self.ping():
                self._status.connected = True
                self._log(f"Connected to FD server at {host}")
                
                # Get initial status
                self.get_status()
                return True
            else:
                self._log("Ping failed after connect")
                self._socket.close()
                self._socket = None
                return False
                
        except Exception as e:
            self._log(f"Connection failed: {e}")
            if self._socket:
                try:
                    self._socket.close()
                except:
                    pass
            self._socket = None
            return False
    
    def disconnect(self):
        """Disconnect from server"""
        if self._socket:
            try:
                self._socket.close()
            except:
                pass
            self._socket = None
        self._status.connected = False
        self._log("Disconnected")
    
    # =========================================================================
    # Low-level communication
    # =========================================================================
    
    def _send_command(self, command: str, timeout: float = DEFAULT_TIMEOUT) -> Optional[str]:
        """
        Send command and get response
        
        Args:
            command: Command string to send
            timeout: Response timeout in seconds
            
        Returns:
            Response string or None on error
        """
        if not self._socket:
            self._log("Not connected")
            return None
        
        try:
            # Set timeout for this command
            self._socket.settimeout(timeout)
            
            # Send command
            self._socket.sendall(f"{command}\n".encode('utf-8'))
            
            # Read response (single line)
            buffer = ""
            while '\n' not in buffer:
                chunk = self._socket.recv(4096)
                if not chunk:
                    self._log("Connection closed by server")
                    self._status.connected = False
                    return None
                buffer += chunk.decode('utf-8')
            
            response = buffer.strip()
            return response
            
        except socket.timeout:
            self._log(f"Command timeout: {command}")
            return None
        except Exception as e:
            self._log(f"Command error: {e}")
            self._status.connected = False
            return None
    
    # =========================================================================
    # Basic commands
    # =========================================================================
    
    def ping(self) -> bool:
        """Test connection"""
        response = self._send_command("PING", timeout=5.0)
        return response == "PONG"
    
    def get_status(self) -> FDStatus:
        """Get current status from server"""
        response = self._send_command("STATUS", timeout=5.0)
        
        if response and response.startswith("STATUS|"):
            try:
                json_str = response.split('|', 1)[1]
                data = json.loads(json_str)
                
                self._status.mode = FDMode(data.get('mode', 'MONITOR'))
                self._status.speed_mm_s = data.get('speed_mm_s', 2.0)
                self._status.streaming = data.get('streaming', False)
                self._status.arduino_connected = data.get('arduino_connected', False)
                
            except Exception as e:
                self._log(f"Status parse error: {e}")
        
        return self._status
    
    # =========================================================================
    # Configuration commands
    # =========================================================================
    
    def set_mode(self, mode: str) -> bool:
        """
        Set operating mode
        
        Args:
            mode: "MONITOR" or "DRIVE"
            
        Returns:
            True if successful
        """
        response = self._send_command(f"SET_MODE {mode.upper()}")
        if response == "OK":
            self._status.mode = FDMode(mode.upper())
            self._log(f"Mode set to {mode}")
            return True
        self._log(f"Failed to set mode: {response}")
        return False
    
    def set_speed(self, mm_per_sec: float) -> bool:
        """
        Set movement speed
        
        Args:
            mm_per_sec: Speed in mm/s (0.1 to 25.0)
            
        Returns:
            True if successful
        """
        response = self._send_command(f"SET_SPEED {mm_per_sec:.2f}")
        if response == "OK":
            self._status.speed_mm_s = mm_per_sec
            self._log(f"Speed set to {mm_per_sec} mm/s")
            return True
        self._log(f"Failed to set speed: {response}")
        return False
    
    # =========================================================================
    # Utility commands
    # =========================================================================
    
    def zero_loadcell(self) -> bool:
        """Zero/tare the load cell"""
        self._log("Zeroing load cell...")
        response = self._send_command("ZERO", timeout=10.0)
        if response == "OK":
            self._log("Load cell zeroed")
            return True
        self._log(f"Zero failed: {response}")
        return False
    
    def read_force(self) -> Optional[float]:
        """
        Read current force
        
        Returns:
            Force in Newtons, or None on error
        """
        response = self._send_command("FORCE", timeout=5.0)
        if response and response.startswith("FORCE|"):
            try:
                force = float(response.split('|')[1])
                return force
            except:
                pass
        return None
    
    def jog(self, distance_mm: float) -> bool:
        """
        Manual jog for positioning
        
        Args:
            distance_mm: Distance to move (positive = extend, negative = retract)
            
        Returns:
            True if successful
        """
        self._log(f"Jogging {distance_mm} mm...")
        
        # Calculate timeout based on distance and speed
        timeout = abs(distance_mm) / max(self._status.speed_mm_s, 0.1) + 15.0
        
        response = self._send_command(f"JOG {distance_mm:.2f}", timeout=timeout)
        if response and "OK" in response:
            self._log("Jog complete")
            return True
        self._log(f"Jog failed: {response}")
        return False
    
    # =========================================================================
    # Monitor mode commands
    # =========================================================================
    
    def start_stream(self) -> bool:
        """
        Start force streaming (MONITOR mode)
        
        Returns:
            True if streaming started
        """
        if self._status.mode != FDMode.MONITOR:
            self._log("start_stream requires MONITOR mode")
            return False
        
        response = self._send_command("START_STREAM", timeout=5.0)
        if response == "OK":
            self._status.streaming = True
            self._log("Streaming started")
            return True
        self._log(f"Failed to start stream: {response}")
        return False
    
    def stop_stream(self) -> ForceData:
        """
        Stop force streaming and return data (MONITOR mode)
        
        Returns:
            ForceData with all collected samples
        """
        self._log("Stopping stream...")
        response = self._send_command("STOP_STREAM", timeout=30.0)
        self._status.streaming = False
        
        if response and response.startswith("DATA|"):
            data_b64 = response.split('|', 1)[1]
            force_data = ForceData.from_base64_csv(data_b64)
            self._log(f"Stream stopped: {force_data.sample_count} samples")
            return force_data
        
        self._log(f"Stop stream failed: {response}")
        return ForceData()
    
    # =========================================================================
    # Drive mode commands
    # =========================================================================
    
    def move(self, distance_mm: float) -> ForceData:
        """
        Move and collect force data (DRIVE mode)
        
        Moves forward the specified distance while streaming force data,
        then automatically retracts. Returns all collected force data.
        
        Args:
            distance_mm: Distance to move in mm (must be positive)
            
        Returns:
            ForceData with force measurements during forward motion
        """
        if self._status.mode != FDMode.DRIVE:
            self._log("move requires DRIVE mode")
            return ForceData()
        
        if distance_mm <= 0:
            self._log("Distance must be positive for DRIVE mode")
            return ForceData()
        
        self._log(f"Moving {distance_mm} mm (DRIVE mode)...")
        
        # Calculate timeout: forward motion + retraction + buffer
        timeout = (distance_mm / self._status.speed_mm_s) * 3 + 30.0
        
        response = self._send_command(f"MOVE {distance_mm:.2f}", timeout=timeout)
        
        if response and response.startswith("DATA|"):
            data_b64 = response.split('|', 1)[1]
            force_data = ForceData.from_base64_csv(data_b64)
            self._log(f"Move complete: {force_data.sample_count} samples, max force: {force_data.max_force:.3f} N")
            return force_data
        
        self._log(f"Move failed: {response}")
        return ForceData()


# =============================================================================
# Test
# =============================================================================

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python fd_client_nojs.py <host> [port]")
        print("Example: python fd_client_nojs.py 192.168.1.12")
        sys.exit(1)
    
    host = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT
    
    client = FDClientNoJS()
    
    if client.connect(host, port):
        print(f"\nStatus: {client.status}")
        
        # Test sequence
        print("\n--- Testing MONITOR mode ---")
        client.set_mode("MONITOR")
        force = client.read_force()
        print(f"Current force: {force} N")
        
        print("\n--- Testing DRIVE mode ---")
        client.set_mode("DRIVE")
        client.set_speed(2.0)
        
        print("Jogging +5mm...")
        client.jog(5.0)
        
        print("Jogging -5mm...")
        client.jog(-5.0)
        
        print("\n--- Testing MOVE with force capture ---")
        client.set_mode("DRIVE")
        data = client.move(5.0)
        print(f"Captured {data.sample_count} samples")
        print(f"Duration: {data.duration_ms:.1f} ms")
        print(f"Max force: {data.max_force:.3f} N")
        
        client.disconnect()
    else:
        print("Connection failed")
        sys.exit(1)
