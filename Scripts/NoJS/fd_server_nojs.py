#!/usr/bin/env python3
"""
FD Server NoJS - Force-Deflection Setup Server (Simplified)
============================================================

Runs on Raspberry Pi, provides TCP interface to the FD Arduino.
Designed to work with fd_arduino.ino (NoJS version).

Protocol:
    PING                    -> PONG
    STATUS                  -> STATUS|{json}
    SET_MODE <MONITOR|DRIVE> -> OK
    SET_SPEED <mm/s>        -> OK
    JOG <distance_mm>       -> OK|Jog complete
    MOVE <distance_mm>      -> DATA|<base64_csv>
    START_STREAM            -> OK (then streams data)
    STOP_STREAM             -> DATA|<base64_csv>
    ZERO                    -> OK
    FORCE                   -> FORCE|<value>

Usage:
    python fd_server_nojs.py [--port 5002] [--serial /dev/ttyACM0]
"""

import socket
import serial
import threading
import time
import json
import base64
import argparse
from dataclasses import dataclass, field
from typing import Optional, List
from enum import Enum
from datetime import datetime

# =============================================================================
# Configuration
# =============================================================================

DEFAULT_PORT = 5002
DEFAULT_SERIAL = "/dev/ttyACM0"
SERIAL_BAUD = 115200
SOCKET_TIMEOUT = 60.0  # Client socket timeout
SERIAL_TIMEOUT = 2.0   # Arduino response timeout

# =============================================================================
# Data Classes
# =============================================================================

class FDMode(Enum):
    MONITOR = "MONITOR"
    DRIVE = "DRIVE"

@dataclass
class ForceDataPoint:
    timestamp_ms: float
    force_n: float

@dataclass
class FDConfig:
    mode: FDMode = FDMode.MONITOR
    speed_mm_s: float = 2.0

@dataclass
class StreamBuffer:
    """Buffer for collecting force data during streaming"""
    data: List[ForceDataPoint] = field(default_factory=list)
    start_time: float = 0.0
    
    def clear(self):
        self.data.clear()
        self.start_time = time.time()
    
    def add(self, timestamp_ms: float, force_n: float):
        self.data.append(ForceDataPoint(timestamp_ms, force_n))
    
    def to_csv(self) -> str:
        """Convert buffer to CSV string"""
        lines = ["timestamp_ms,force_n"]
        for point in self.data:
            lines.append(f"{point.timestamp_ms:.1f},{point.force_n:.5f}")
        return "\n".join(lines)
    
    def to_base64(self) -> str:
        """Convert buffer to base64-encoded CSV"""
        csv = self.to_csv()
        return base64.b64encode(csv.encode('utf-8')).decode('utf-8')

# =============================================================================
# Logging
# =============================================================================

def log(msg: str):
    """Log with timestamp"""
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{timestamp}][FDS] {msg}")

def log_error(msg: str):
    """Log error with timestamp"""
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{timestamp}][FDS][ERROR] {msg}")

# =============================================================================
# FD Server Class
# =============================================================================

class FDServerNoJS:
    """Simplified FD Server for NoJS approach"""
    
    def __init__(self, port: int = DEFAULT_PORT, serial_port: str = DEFAULT_SERIAL):
        self.port = port
        self.serial_port = serial_port
        
        # Serial connection
        self._serial: Optional[serial.Serial] = None
        self._serial_lock = threading.Lock()
        
        # TCP socket
        self._server_socket: Optional[socket.socket] = None
        self._client_socket: Optional[socket.socket] = None
        
        # State
        self._config = FDConfig()
        self._stream_buffer = StreamBuffer()
        self._streaming = False
        self._running = False
    
    def connect_arduino(self) -> bool:
        """Connect to Arduino via serial"""
        try:
            log(f"Connecting to Arduino on {self.serial_port}...")
            self._serial = serial.Serial(
                self.serial_port, 
                SERIAL_BAUD, 
                timeout=SERIAL_TIMEOUT
            )
            time.sleep(2)  # Wait for Arduino reset
            
            # Drain startup messages
            while self._serial.in_waiting > 0:
                line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                log(f"Arduino startup: {line}")
            
            log("Arduino connected")
            return True
            
        except Exception as e:
            log_error(f"Arduino connection failed: {e}")
            return False
    
    def _send_to_arduino(self, command: str, timeout: float = SERIAL_TIMEOUT) -> Optional[str]:
        """Send command to Arduino and get response"""
        if not self._serial or not self._serial.is_open:
            log_error("Arduino not connected")
            return None
        
        with self._serial_lock:
            try:
                self._serial.reset_input_buffer()
                log(f"Arduino TX: {command}")
                self._serial.write(f"{command}\n".encode('utf-8'))
                self._serial.flush()
                
                # Read response with timeout
                response_lines = []
                start_time = time.time()
                terminal_prefixes = ['OK', 'ERROR', 'STOPPED', 'STREAMING']
                got_terminal = False
                
                while time.time() - start_time < timeout:
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            response_lines.append(line)
                            # Check for terminal responses
                            if any(line.startswith(x) for x in terminal_prefixes):
                                got_terminal = True
                                break
                    else:
                        time.sleep(0.01)
                        # Only early-break if we got a terminal response
                        # Don't break just because buffer is empty - movement may be in progress
                
                response = '\n'.join(response_lines)
                log(f"Arduino RX: {response[:100]}{'...' if len(response) > 100 else ''}")
                return response
                
            except Exception as e:
                log_error(f"Arduino communication error: {e}")
                return None
    
    def _collect_stream_data(self, timeout: float = 30.0) -> bool:
        """
        Collect streaming force data from Arduino until STOPPED is received.
        Used for DRIVE mode where Arduino auto-streams during movement.
        """
        if not self._serial or not self._serial.is_open:
            return False
        
        self._stream_buffer.clear()
        start_time = time.time()
        
        log("Collecting stream data...")
        
        with self._serial_lock:
            while time.time() - start_time < timeout:
                if self._serial.in_waiting > 0:
                    line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                    
                    # Parse force data: T<ms>,F<force>
                    if line.startswith('T') and ',F' in line:
                        try:
                            parts = line.split(',')
                            ts = float(parts[0][1:])  # Remove 'T' prefix
                            force = float(parts[1][1:])  # Remove 'F' prefix
                            self._stream_buffer.add(ts, force)
                        except ValueError:
                            pass  # Ignore malformed lines
                    
                    # Check for end of stream
                    elif line.startswith('STOPPED'):
                        log(f"Stream ended: {line}")
                        return True
                    elif line.startswith('ABORT'):
                        log(f"Stream aborted: {line}")
                        return True
                    elif line.startswith('RETRACTING'):
                        log("Retracting...")
                    elif line.startswith('DRIVE_START'):
                        log("Drive started")
                else:
                    time.sleep(0.01)
        
        log_error("Stream collection timeout")
        return False
    
    def _stream_monitor_mode(self) -> str:
        """
        Stream force data in MONITOR mode until client sends STOP_STREAM.
        Returns base64-encoded CSV of all collected data.
        """
        self._stream_buffer.clear()
        self._streaming = True
        
        # Send START to Arduino
        response = self._send_to_arduino("START", timeout=2.0)
        if not response or "STREAMING" not in response:
            return "ERROR|Failed to start streaming"
        
        log("Streaming started (MONITOR mode)")
        
        # Collect data until client sends STOP_STREAM
        # The stop signal will come from the client handler
        while self._streaming:
            if self._serial and self._serial.in_waiting > 0:
                with self._serial_lock:
                    try:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        if line.startswith('T') and ',F' in line:
                            parts = line.split(',')
                            ts = float(parts[0][1:])
                            force = float(parts[1][1:])
                            self._stream_buffer.add(ts, force)
                    except:
                        pass
            else:
                time.sleep(0.01)
        
        # Send STOP to Arduino
        self._send_to_arduino("STOP", timeout=2.0)
        
        log(f"Streaming stopped: {len(self._stream_buffer.data)} samples")
        return f"DATA|{self._stream_buffer.to_base64()}"
    
    def _handle_command(self, command: str) -> str:
        """Process a command from the client"""
        command = command.strip()
        if not command:
            return "ERROR|Empty command"
        
        parts = command.split(maxsplit=1)
        cmd = parts[0].upper()
        args = parts[1] if len(parts) > 1 else ""
        
        log(f"Command: {cmd} {args}")
        
        # === Connection commands ===
        if cmd == "PING":
            return "PONG"
        
        if cmd == "STATUS":
            status = {
                "mode": self._config.mode.value,
                "speed_mm_s": self._config.speed_mm_s,
                "streaming": self._streaming,
                "arduino_connected": self._serial is not None and self._serial.is_open
            }
            return f"STATUS|{json.dumps(status)}"
        
        # === Mode commands ===
        if cmd == "SET_MODE":
            mode_str = args.upper()
            if mode_str == "MONITOR":
                response = self._send_to_arduino("M")
                if response and "OK_MONITOR" in response:
                    self._config.mode = FDMode.MONITOR
                    return "OK"
                return "ERROR|Failed to set MONITOR mode"
            elif mode_str == "DRIVE":
                response = self._send_to_arduino("D")
                if response and "OK_DRIVE" in response:
                    self._config.mode = FDMode.DRIVE
                    return "OK"
                return "ERROR|Failed to set DRIVE mode"
            return "ERROR|Invalid mode (use MONITOR or DRIVE)"
        
        if cmd == "SET_SPEED":
            try:
                speed = float(args)
                response = self._send_to_arduino(f"V{speed}")
                if response and "OK_SPEED" in response:
                    self._config.speed_mm_s = speed
                    return "OK"
                return f"ERROR|{response or 'No response'}"
            except ValueError:
                return "ERROR|Invalid speed value"
        
        # === Movement commands ===
        if cmd == "JOG":
            try:
                distance = float(args)
                # Calculate timeout based on distance and speed
                timeout = abs(distance) / self._config.speed_mm_s + 10.0
                response = self._send_to_arduino(str(distance), timeout=timeout)
                if response and "OK_JOG" in response:
                    return "OK|Jog complete"
                return f"ERROR|{response or 'No response'}"
            except ValueError:
                return "ERROR|Invalid distance value"
        
        if cmd == "MOVE":
            # DRIVE mode: move forward while streaming, then retract
            if self._config.mode != FDMode.DRIVE:
                return "ERROR|MOVE requires DRIVE mode"
            
            if not self._serial:
                return "ERROR|Arduino not connected"
            
            try:
                distance = float(args)
                
                # Send GO command to Arduino
                self._serial.reset_input_buffer()
                log(f"Arduino TX: G{distance}")
                self._serial.write(f"G{distance}\n".encode('utf-8'))
                self._serial.flush()
                
                # Collect streaming data until STOPPED
                timeout = abs(distance) / self._config.speed_mm_s * 3 + 30.0
                if self._collect_stream_data(timeout):
                    return f"DATA|{self._stream_buffer.to_base64()}"
                return "ERROR|Movement/streaming failed"
                
            except ValueError:
                return "ERROR|Invalid distance value"
        
        # === Streaming commands (MONITOR mode) ===
        if cmd == "START_STREAM":
            if self._config.mode != FDMode.MONITOR:
                return "ERROR|START_STREAM requires MONITOR mode"
            
            # Start streaming in background
            response = self._send_to_arduino("START", timeout=2.0)
            if response and "STREAMING" in response:
                self._streaming = True
                self._stream_buffer.clear()
                
                # Start background collection thread
                threading.Thread(target=self._background_stream_collect, daemon=True).start()
                return "OK"
            return "ERROR|Failed to start streaming"
        
        if cmd == "STOP_STREAM":
            self._streaming = False
            time.sleep(0.2)  # Let background thread finish
            
            # Send STOP to Arduino
            self._send_to_arduino("STOP", timeout=2.0)
            
            log(f"Streaming stopped: {len(self._stream_buffer.data)} samples")
            return f"DATA|{self._stream_buffer.to_base64()}"
        
        # === Utility commands ===
        if cmd == "ZERO":
            response = self._send_to_arduino("Z", timeout=5.0)
            if response and "OK_ZERO" in response:
                return "OK"
            return f"ERROR|{response or 'No response'}"
        
        if cmd == "FORCE":
            response = self._send_to_arduino("F", timeout=2.0)
            if response and "FORCE|" in response:
                return response
            return f"ERROR|{response or 'No response'}"
        
        return f"ERROR|Unknown command: {cmd}"
    
    def _background_stream_collect(self):
        """Background thread to collect streaming data in MONITOR mode"""
        log("Background stream collection started")
        
        while self._streaming:
            if self._serial and self._serial.in_waiting > 0:
                try:
                    with self._serial_lock:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                    
                    if line.startswith('T') and ',F' in line:
                        parts = line.split(',')
                        ts = float(parts[0][1:])
                        force = float(parts[1][1:])
                        self._stream_buffer.add(ts, force)
                except Exception as e:
                    log_error(f"Stream parse error: {e}")
            else:
                time.sleep(0.01)
        
        log("Background stream collection ended")
    
    def _handle_client(self, client_socket: socket.socket, client_addr):
        """Handle a connected client"""
        log(f"Client connected: {client_addr}")
        self._client_socket = client_socket
        client_socket.settimeout(SOCKET_TIMEOUT)
        
        buffer = ""
        
        try:
            while self._running:
                try:
                    data = client_socket.recv(1024)
                    if not data:
                        log("Client disconnected")
                        break
                    
                    buffer += data.decode('utf-8')
                    
                    # Process complete lines
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            response = self._handle_command(line)
                            client_socket.sendall(f"{response}\n".encode('utf-8'))
                            
                except socket.timeout:
                    continue
                except ConnectionResetError:
                    log("Client connection reset")
                    break
                    
        except Exception as e:
            log_error(f"Client handler error: {e}")
        finally:
            client_socket.close()
            self._client_socket = None
            log("Client handler ended")
    
    def start(self):
        """Start the server"""
        # Connect to Arduino
        if not self.connect_arduino():
            log_error("Failed to connect to Arduino, continuing without it")
        
        # Create TCP socket
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_socket.bind(('0.0.0.0', self.port))
        self._server_socket.listen(1)
        self._server_socket.settimeout(1.0)
        
        self._running = True
        log(f"Server listening on port {self.port}")
        
        try:
            while self._running:
                try:
                    client_socket, client_addr = self._server_socket.accept()
                    self._handle_client(client_socket, client_addr)
                except socket.timeout:
                    continue
        except KeyboardInterrupt:
            log("Shutdown requested")
        finally:
            self._running = False
            if self._server_socket:
                self._server_socket.close()
            if self._serial:
                self._serial.close()
            log("Server stopped")
    
    def stop(self):
        """Stop the server"""
        self._running = False
        self._streaming = False

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="FD Server NoJS")
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help=f"TCP port (default: {DEFAULT_PORT})")
    parser.add_argument('--serial', type=str, default=DEFAULT_SERIAL, help=f"Serial port (default: {DEFAULT_SERIAL})")
    args = parser.parse_args()
    
    print("=" * 50)
    print("FD Server NoJS - Force-Deflection Controller")
    print("=" * 50)
    
    server = FDServerNoJS(port=args.port, serial_port=args.serial)
    server.start()

if __name__ == "__main__":
    main()
