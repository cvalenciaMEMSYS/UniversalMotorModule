"""
FD Server - Force-Deflection Setup TCP Server for Raspberry Pi

This server runs on the Raspberry Pi and interfaces with the Arduino
that controls the linear actuator and load cell. It provides a TCP
interface for remote control from the laptop orchestrator.

Modes:
  - MONITOR: Passive force measurement (DUT pushes against load cell)
  - BACKDRIVE: Active test (FD actuator pushes against DUT)

Author: Energy Measurement Test System
Date: January 2026

Deploy to Raspberry Pi at 192.168.1.12
Run with: python3 fd_server.py
"""

import serial
import socket
import threading
import time
import json
from dataclasses import dataclass, asdict
from typing import Optional, List, Tuple
from enum import Enum
import struct
import sys


# =============================================================================
# Logging Utilities
# =============================================================================

def log(msg: str) -> None:
    """Print log message with timestamp and FDS prefix"""
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}][FDS] {msg}", flush=True)


def log_error(msg: str) -> None:
    """Print error message with timestamp and FDS prefix"""
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}][FDS][ERROR] {msg}", file=sys.stderr, flush=True)


class FDMode(Enum):
    MONITOR = "MONITOR"      # Passive force monitoring
    BACKDRIVE = "BACKDRIVE"  # Active backdrive test


@dataclass
class FDConfig:
    """Configuration for FD setup"""
    mode: FDMode = FDMode.MONITOR
    speed_mm_s: float = 1.0          # Actuator speed for backdrive (mm/s)
    travel_mm: float = 10.0          # Max travel for backdrive
    sample_rate_hz: int = 100        # Force sampling rate
    timeout_ms: int = 30000          # Maximum test duration
    force_limit_n: float = 10.0      # Force threshold to stop
    
    def to_dict(self) -> dict:
        d = asdict(self)
        d['mode'] = self.mode.value
        return d


@dataclass
class ForceDataPoint:
    """Single force measurement"""
    timestamp_ms: float
    force_n: float
    position_mm: float


class FDServer:
    """
    TCP Server for Force-Deflection setup control
    
    Provides remote interface to Arduino-controlled FD setup.
    Supports force monitoring and backdrive testing modes.
    """
    
    SERIAL_PORT = '/dev/ttyACM0'
    SERIAL_BAUD = 115200
    TCP_PORT = 5002
    
    def __init__(self, host: str = '0.0.0.0', port: int = TCP_PORT):
        self.host = host
        self.port = port
        
        self._serial: Optional[serial.Serial] = None
        self._server_socket: Optional[socket.socket] = None
        self._client_socket: Optional[socket.socket] = None
        self._client_address = None
        
        self._running = False
        self._recording = False
        self._config = FDConfig()
        
        self._force_data: List[ForceDataPoint] = []
        self._start_time_ms: float = 0
        self._current_position_mm: float = 0.0
        
        self._serial_lock = threading.Lock()
        self._data_lock = threading.Lock()
        
    def _connect_arduino(self) -> bool:
        """Establish serial connection to Arduino"""
        log(f"Attempting Arduino connection on {self.SERIAL_PORT}...")
        try:
            self._serial = serial.Serial(
                port=self.SERIAL_PORT,
                baudrate=self.SERIAL_BAUD,
                timeout=1.0
            )
            time.sleep(2.0)  # Wait for Arduino reset
            self._serial.reset_input_buffer()
            log(f"Connected to Arduino on {self.SERIAL_PORT}")
            return True
        except serial.SerialException as e:
            log_error(f"Failed to connect to Arduino: {e}")
            return False
    
    def _send_to_arduino(self, command: str, wait_for_response: bool = True, response_timeout: float = 2.0) -> Optional[str]:
        """
        Send command to Arduino and get response.
        
        The Arduino uses a menu-based interactive protocol.
        This method waits for any ongoing output to complete and handles the response.
        """
        if not self._serial or not self._serial.is_open:
            log_error("Arduino not connected")
            return None
            
        with self._serial_lock:
            try:
                # Clear any pending data
                self._serial.reset_input_buffer()
                
                log(f"Arduino TX: {command}")
                self._serial.write(f"{command}\n".encode('utf-8'))
                self._serial.flush()
                
                if not wait_for_response:
                    return ""
                
                # Read response with timeout
                response_lines = []
                start_time = time.time()
                while time.time() - start_time < response_timeout:
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            response_lines.append(line)
                            # Reset timeout on new data
                            start_time = time.time()
                    else:
                        time.sleep(0.05)
                        # If we have response and no more data coming, break
                        if response_lines and self._serial.in_waiting == 0:
                            time.sleep(0.1)  # Extra wait to confirm no more data
                            if self._serial.in_waiting == 0:
                                break
                
                response = '\n'.join(response_lines)
                log(f"Arduino RX: {response[:200] if response else '(empty)'}")
                return response
            except Exception as e:
                log_error(f"Arduino communication error: {e}")
                return None
    
    def _read_force(self) -> Optional[Tuple[float, float]]:
        """
        Read current force from load cell.
        
        Arduino protocol: Send 'f', get "Current force value: X.XXXXX N"
        """
        if not self._serial or not self._serial.is_open:
            return None
            
        with self._serial_lock:
            try:
                self._serial.reset_input_buffer()
                self._serial.write(b"f\n")
                self._serial.flush()
                
                # Read response lines
                response = ""
                start_time = time.time()
                while time.time() - start_time < 1.0:
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        response += line + "\n"
                        # Look for force value in response
                        if "N" in line and "force" in line.lower():
                            break
                    else:
                        time.sleep(0.05)
                
                # Parse "Current force value: 0.12345 N"
                import re
                match = re.search(r'(-?[\d.]+)\s*N', response)
                if match:
                    force = float(match.group(1))
                    return (force, self._current_position_mm)
                    
            except Exception as e:
                log_error(f"Force read error: {e}")
        return None
    
    def _zero_load_cell(self) -> bool:
        """
        Tare/zero the load cell.
        
        Arduino protocol: Send 'r', wait for "Loadcells are calibrated to zero"
        """
        log("Zeroing load cell...")
        response = self._send_to_arduino("r", response_timeout=5.0)
        success = response is not None and "calibrated" in response.lower()
        log(f"Zero load cell result: {success}")
        return success
    
    def _set_arduino_speed(self, speed_mm_s: float) -> bool:
        """
        Set Arduino stepper motor speed.
        
        Arduino protocol: 
        1. Send 'a' 
        2. Wait for "Enter desired stepper motor speed..." prompt
        3. Send speed value (rounded to 1 decimal for Arduino parseFloat compatibility)
        """
        # Round to avoid issues with Arduino's parseFloat on long decimal strings
        speed_mm_s = round(speed_mm_s, 1)
        log(f"Setting Arduino speed to {speed_mm_s} mm/s...")
        
        if not self._serial or not self._serial.is_open:
            log_error("Arduino not connected")
            return False
            
        with self._serial_lock:
            try:
                self._serial.reset_input_buffer()
                
                # Step 1: Send 'a' to enter speed adjustment mode
                log("Arduino TX: a")
                self._serial.write(b"a\n")
                self._serial.flush()
                
                # Step 2: Wait for the prompt to fully arrive
                # The Arduino prints: "Enter desired stepper motor speed in mm/s. Suggested bewteen 1 and 3mm/s..."
                prompt_received = False
                prompt_text = ""
                start_time = time.time()
                while time.time() - start_time < 2.0:  # 2 second timeout
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        prompt_text += line + "\n"
                        log(f"Arduino RX (prompt): {line[:60]}...")
                        if "speed" in line.lower() and "mm/s" in line.lower():
                            prompt_received = True
                            break
                    else:
                        time.sleep(0.05)
                
                if not prompt_received:
                    log_error(f"Did not receive speed prompt. Got: {prompt_text}")
                    return False
                
                # Give Arduino a moment after printing prompt
                time.sleep(0.1)
                
                # Step 3: Send the speed value
                log(f"Arduino TX: {speed_mm_s}")
                self._serial.write(f"{speed_mm_s}\n".encode('utf-8'))
                self._serial.flush()
                
                # IMPORTANT: After setting speed, the Arduino loops back and prints
                # the MENU again. We need to wait for this menu to finish printing
                # and consume it, otherwise the next command will receive the menu.
                time.sleep(2)  # Give Arduino time to start printing menu
                
                # Wait for and consume the menu print (wait until no new data for 0.3s)
                menu_text = ""
                last_data_time = time.time()
                while time.time() - last_data_time < 0.5:
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        menu_text += line + "\n"
                        last_data_time = time.time()
                    else:
                        time.sleep(0.05)
                
                if menu_text:
                    log(f"Consumed menu output ({len(menu_text)} bytes)")
                
                log(f"Speed set to {speed_mm_s} mm/s")
                return True
                
            except Exception as e:
                log_error(f"Speed set error: {e}")
                return False
    
    def _move_actuator(self, distance_mm: float, speed_mm_s: Optional[float] = None) -> bool:
        """
        Move the linear actuator.
        
        Arduino protocol: Just send the distance as a floating-point number.
        The Arduino will respond with "Moving Xmm", then "Moved!" and a force reading.
        
        If speed is specified, it sets the Arduino speed first.
        """
        if speed_mm_s is not None and speed_mm_s != self._config.speed_mm_s:
            # Set new speed
            if self._set_arduino_speed(speed_mm_s):
                self._config.speed_mm_s = speed_mm_s
        
        actual_speed = self._config.speed_mm_s
        log(f"Moving actuator: {distance_mm}mm at {actual_speed}mm/s")
        
        # Just send the distance as a number - Arduino will parse it
        response = self._send_to_arduino(str(distance_mm), response_timeout=abs(distance_mm) / actual_speed + 5.0)
        
        # Update position tracking
        if response is not None and "Moved" in response:
            self._current_position_mm += distance_mm
            return True
        return response is not None
    
    def _move_actuator_wait_complete(self, distance_mm: float, timeout: float = 30.0) -> bool:
        """
        Move the linear actuator and wait for "Moved!" completion signal.
        
        This method specifically waits for the Arduino to send "Moved!" which
        indicates the motion has completed, rather than using a timed delay.
        
        Args:
            distance_mm: Distance to move in mm (positive = extend, negative = retract)
            timeout: Maximum time to wait for completion
            
        Returns:
            True if "Moved!" was received, False otherwise
        """
        if not self._serial or not self._serial.is_open:
            log_error("Arduino not connected for move")
            return False
            
        with self._serial_lock:
            try:
                self._serial.reset_input_buffer()
                
                # Send the distance command
                log(f"Arduino TX: {distance_mm}")
                self._serial.write(f"{distance_mm}\n".encode('utf-8'))
                self._serial.flush()
                
                # Wait for "Moved!" in response
                response_lines = []
                start_time = time.time()
                got_moved = False
                
                while time.time() - start_time < timeout and not got_moved:
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            response_lines.append(line)
                            log(f"Arduino RX: {line}")
                            
                            # Check for "Moved!" completion signal
                            if "Moved" in line:
                                got_moved = True
                                log("Motion completed: received 'Moved!' signal")
                                # Read one more line to get force reading
                                try:
                                    self._serial.timeout = 0.5
                                    extra = self._serial.readline().decode('utf-8', errors='ignore').strip()
                                    if extra:
                                        response_lines.append(extra)
                                        log(f"Arduino RX: {extra}")
                                except:
                                    pass
                    else:
                        time.sleep(0.01)  # Short sleep to prevent CPU spinning
                
                if got_moved:
                    self._current_position_mm += distance_mm
                    return True
                else:
                    log_error(f"Motion timeout after {timeout}s - 'Moved!' not received")
                    return False
                    
            except Exception as e:
                log_error(f"Move command error: {e}")
                return False
    
    def _stop_actuator(self) -> bool:
        """Stop actuator movement immediately"""
        log("Stopping actuator")
        response = self._send_to_arduino("STOP")
        return response is not None
    
    def _recording_loop(self):
        """Background thread for recording force data"""
        sample_interval = 1.0 / self._config.sample_rate_hz
        self._start_time_ms = time.time() * 1000
        
        log(f"Recording started - Mode: {self._config.mode.value}, Rate: {self._config.sample_rate_hz}Hz")
        
        # For backdrive mode, start actuator movement
        if self._config.mode == FDMode.BACKDRIVE:
            self._move_actuator(self._config.travel_mm, self._config.speed_mm_s)
        
        while self._recording:
            loop_start = time.time()
            
            # Read force
            reading = self._read_force()
            if reading:
                force_n, position_mm = reading
                timestamp_ms = time.time() * 1000 - self._start_time_ms
                
                data_point = ForceDataPoint(
                    timestamp_ms=timestamp_ms,
                    force_n=force_n,
                    position_mm=position_mm
                )
                
                with self._data_lock:
                    self._force_data.append(data_point)
                
                # Send data to client in real-time
                self._send_data_to_client(data_point)
                
                # Check force limit
                if abs(force_n) >= self._config.force_limit_n:
                    log(f"Force limit reached: {force_n:.2f} N")
                    self._recording = False
                    break
                
                # Check timeout
                if timestamp_ms >= self._config.timeout_ms:
                    log("Timeout reached")
                    self._recording = False
                    break
            
            # Maintain sample rate
            elapsed = time.time() - loop_start
            if elapsed < sample_interval:
                time.sleep(sample_interval - elapsed)
        
        # Stop actuator if in backdrive mode
        if self._config.mode == FDMode.BACKDRIVE:
            self._stop_actuator()
        
        # Send end marker
        self._send_end_marker()
        log("Recording stopped")
    
    def _send_data_to_client(self, data: ForceDataPoint):
        """Send single data point to connected client"""
        if self._client_socket:
            try:
                msg = f"DATA|{data.timestamp_ms:.1f}|{data.force_n:.4f}|{data.position_mm:.3f}\n"
                self._client_socket.send(msg.encode('utf-8'))
            except Exception as e:
                log_error(f"Failed to send data: {e}")
    
    def _send_end_marker(self):
        """Send end-of-recording marker"""
        if self._client_socket:
            try:
                with self._data_lock:
                    duration = self._force_data[-1].timestamp_ms if self._force_data else 0
                    count = len(self._force_data)
                msg = f"END|{count}|{duration:.1f}\n"
                log(f"Sending end marker: {msg.strip()}")
                self._client_socket.send(msg.encode('utf-8'))
            except Exception as e:
                log_error(f"Failed to send end marker: {e}")
    
    def _handle_command(self, command: str) -> str:
        """
        Process command from client
        
        Returns:
            Response string
        """
        parts = command.strip().upper().split()
        if not parts:
            log_error("Received empty command")
            return "ERROR|Empty command"
        
        cmd = parts[0]
        args = parts[1:] if len(parts) > 1 else []
        
        log(f"Received command: {cmd} {' '.join(args) if args else ''}")
        
        try:
            if cmd == "SET_MODE":
                if args and args[0] in ["MONITOR", "BACKDRIVE"]:
                    self._config.mode = FDMode[args[0]]
                    response = f"OK|Mode set to {args[0]}"
                    log(f"Response: {response}")
                    return response
                return "ERROR|Invalid mode (use MONITOR or BACKDRIVE)"
            
            elif cmd == "SET_SPEED":
                if args:
                    new_speed = float(args[0])
                    # Actually set the speed on the Arduino
                    if self._set_arduino_speed(new_speed):
                        self._config.speed_mm_s = new_speed
                        response = f"OK|Speed set to {self._config.speed_mm_s} mm/s"
                        log(f"Response: {response}")
                        return response
                    else:
                        return "ERROR|Failed to set Arduino speed"
                return "ERROR|Missing speed value"
            
            elif cmd == "SET_TRAVEL":
                if args:
                    self._config.travel_mm = float(args[0])
                    response = f"OK|Travel set to {self._config.travel_mm} mm"
                    log(f"Response: {response}")
                    return response
                return "ERROR|Missing travel value"
            
            elif cmd == "SET_SAMPLE_RATE":
                if args:
                    self._config.sample_rate_hz = int(args[0])
                    response = f"OK|Sample rate set to {self._config.sample_rate_hz} Hz"
                    log(f"Response: {response}")
                    return response
                return "ERROR|Missing sample rate value"
            
            elif cmd == "SET_TIMEOUT":
                if args:
                    self._config.timeout_ms = int(args[0])
                    response = f"OK|Timeout set to {self._config.timeout_ms} ms"
                    log(f"Response: {response}")
                    return response
                return "ERROR|Missing timeout value"
            
            elif cmd == "SET_FORCE_LIMIT":
                if args:
                    self._config.force_limit_n = float(args[0])
                    response = f"OK|Force limit set to {self._config.force_limit_n} N"
                    log(f"Response: {response}")
                    return response
                return "ERROR|Missing force limit value"
            
            elif cmd == "ZERO":
                if self._zero_load_cell():
                    response = "OK|Load cell zeroed"
                    log(f"Response: {response}")
                    return response
                return "ERROR|Failed to zero load cell"
            
            elif cmd == "GET_FORCE":
                # Query current force reading
                reading = self._read_force()
                if reading:
                    force, position = reading
                    response = f"FORCE|{force:.5f}|{position:.3f}"
                    log(f"Response: {response}")
                    return response
                return "ERROR|Failed to read force"
            
            elif cmd == "JOG":
                # Manual jog command: JOG <distance_mm>
                # Sends command, waits for Arduino's "Moved!" response to indicate completion
                if args:
                    distance = float(args[0])
                    log(f"JOG command: {distance}mm")
                    
                    # Calculate a generous timeout for the move
                    speed = self._config.speed_mm_s
                    max_wait = abs(distance) / speed + 10.0  # 10s buffer for safety
                    
                    # Send the distance command and wait for "Moved!" completion signal
                    result = self._move_actuator_wait_complete(distance, max_wait)
                    
                    if result:
                        response = f"OK|Jog complete {distance} mm"
                        log(f"Response: {response}")
                        return response
                    return "ERROR|Jog failed - motion did not complete"
                return "ERROR|Missing distance value"
            
            elif cmd == "START":
                if self._recording:
                    return "ERROR|Already recording"
                
                with self._data_lock:
                    self._force_data.clear()
                
                self._recording = True
                threading.Thread(target=self._recording_loop, daemon=True).start()
                response = "OK|Recording started"
                log(f"Response: {response}")
                return response
            
            elif cmd == "STOP":
                if self._recording:
                    self._recording = False
                    self._stop_actuator()
                    response = "OK|Recording stopped"
                    log(f"Response: {response}")
                    return response
                return "OK|Not recording"
            
            elif cmd == "STATUS":
                status = {
                    "recording": self._recording,
                    "config": self._config.to_dict(),
                    "samples": len(self._force_data),
                    "arduino_connected": self._serial is not None and self._serial.is_open
                }
                response = f"STATUS|{json.dumps(status)}"
                log(f"Response: STATUS|{{...}} (recording={status['recording']}, arduino={status['arduino_connected']})")
                return response
            
            elif cmd == "PING":
                log("Response: PONG")
                return "PONG"
            
            elif cmd == "GET_DATA":
                # Return all collected data (for retrieval after recording)
                with self._data_lock:
                    data_list = [
                        f"{d.timestamp_ms:.1f},{d.force_n:.4f},{d.position_mm:.3f}"
                        for d in self._force_data
                    ]
                response = "DATA_DUMP|" + ";".join(data_list)
                log(f"Response: DATA_DUMP with {len(data_list)} samples")
                return response
            
            else:
                log_error(f"Unknown command: {cmd}")
                return f"ERROR|Unknown command: {cmd}"
                
        except Exception as e:
            log_error(f"Command handling error: {e}")
            return f"ERROR|{str(e)}"
    
    def _client_handler(self, client_socket: socket.socket, address):
        """Handle connected client"""
        log(f"Client connected: {address}")
        self._client_socket = client_socket
        self._client_address = address
        
        buffer = ""
        
        try:
            while self._running:
                try:
                    data = client_socket.recv(1024).decode('utf-8')
                    if not data:
                        log("Client sent empty data, disconnecting")
                        break
                    
                    buffer += data
                    
                    # Process complete commands (newline-delimited)
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        if line.strip():
                            log(f"Processing: {line.strip()}")
                            response = self._handle_command(line)
                            log(f"Sending response: {response[:80]}{'...' if len(response) > 80 else ''}")
                            client_socket.send((response + '\n').encode('utf-8'))
                            log("Response sent successfully")
                            
                except socket.timeout:
                    continue
                except ConnectionResetError:
                    log("Connection reset by client")
                    break
                    
        finally:
            log(f"Client disconnected: {address}")
            self._client_socket = None
            self._client_address = None
            client_socket.close()
    
    def start(self):
        """Start the FD server"""
        print("=" * 50)
        print("FD Server - Force-Deflection Setup Controller")
        print("=" * 50)
        
        # Connect to Arduino
        if not self._connect_arduino():
            log("WARNING: Running without Arduino connection")
        
        # Create TCP server
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_socket.bind((self.host, self.port))
        self._server_socket.listen(1)
        self._server_socket.settimeout(1.0)
        
        self._running = True
        
        log(f"Server listening on {self.host}:{self.port}")
        log("Waiting for client connection...")
        
        try:
            while self._running:
                try:
                    client_socket, address = self._server_socket.accept()
                    client_socket.settimeout(0.5)
                    
                    # Handle client in separate thread
                    client_thread = threading.Thread(
                        target=self._client_handler,
                        args=(client_socket, address),
                        daemon=True
                    )
                    client_thread.start()
                    
                except socket.timeout:
                    continue
                    
        except KeyboardInterrupt:
            log("Keyboard interrupt - shutting down...")
        finally:
            self.stop()
    
    def stop(self):
        """Stop the server"""
        log("Stopping server...")
        self._running = False
        self._recording = False
        
        if self._stop_actuator:
            self._stop_actuator()
        
        if self._serial and self._serial.is_open:
            self._serial.close()
        
        if self._server_socket:
            self._server_socket.close()
        
        log("Server stopped")


# =============================================================================
# Main entry point
# =============================================================================

if __name__ == "__main__":
    server = FDServer()
    server.start()
