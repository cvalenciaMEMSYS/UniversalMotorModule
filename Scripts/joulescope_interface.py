"""
Joulescope Interface - USB interface for Joulescope JS220 Power Analyzer

This module provides a Python interface to the Joulescope JS220 for
power and energy measurements during motor testing.

Supports both:
  - joulescope package (JS110 and older API)
  - joulescope_driver / pyjoulescope_driver (JS220 with newer API)

Author: Energy Measurement Test System
Date: January 2026
"""

from __future__ import annotations

import time
import threading
import numpy as np
from dataclasses import dataclass, field
from typing import Optional, Callable, List, Any, TYPE_CHECKING
from enum import Enum

# Type checking imports
if TYPE_CHECKING:
    from numpy.typing import NDArray

# Try to import joulescope packages - support both old and new APIs
JOULESCOPE_AVAILABLE = False
JOULESCOPE_API_VERSION = 0  # 0=none, 1=joulescope (old), 2=joulescope (v1.x), 3=pyjoulescope_driver

joulescope = None  # type: ignore
pyjoulescope_driver = None  # type: ignore

# Try joulescope v1.x first - it's the recommended package and supports JS220
try:
    import joulescope as joulescope
    if hasattr(joulescope, 'scan_require_one'):
        JOULESCOPE_API_VERSION = 2
        JOULESCOPE_AVAILABLE = True
        print("Joulescope: Using joulescope v1.x API (recommended)")
    elif hasattr(joulescope, 'scan'):
        JOULESCOPE_API_VERSION = 1
        JOULESCOPE_AVAILABLE = True
        print("Joulescope: Using joulescope v0.x API")
except ImportError:
    pass

# Fallback to pyjoulescope_driver if joulescope not available
if not JOULESCOPE_AVAILABLE:
    try:
        import pyjoulescope_driver as pyjoulescope_driver
        JOULESCOPE_AVAILABLE = True
        JOULESCOPE_API_VERSION = 3
        print("Joulescope: Using pyjoulescope_driver (low-level JS220 API)")
        print("  Note: For better compatibility, consider: pip install joulescope")
    except ImportError:
        print("WARNING: No joulescope package found.")
        print("  Install with: pip install joulescope")
        print("  For JS220: pip install pyjoulescope_driver (alternative)")


class CurrentRange(Enum):
    """Joulescope current ranges"""
    AUTO = "auto"
    R_10A = "10 A"
    R_180MA = "180 mA"
    R_18MA = "18 mA"
    R_1800UA = "1800 µA"
    R_180UA = "180 µA"
    R_18UA = "18 µA"


class VoltageRange(Enum):
    """Joulescope voltage ranges"""
    R_15V = "15 V"
    R_2V = "2 V"


@dataclass
class PowerDataPoint:
    """Single power measurement"""
    timestamp_ms: float
    voltage_v: float
    current_a: float
    power_w: float


@dataclass
class PowerData:
    """Complete power measurement dataset"""
    timestamps_ms: np.ndarray = field(default_factory=lambda: np.array([], dtype=np.float64))
    voltage_v: np.ndarray = field(default_factory=lambda: np.array([], dtype=np.float64))
    current_a: np.ndarray = field(default_factory=lambda: np.array([], dtype=np.float64))
    power_w: np.ndarray = field(default_factory=lambda: np.array([], dtype=np.float64))
    energy_j: float = 0.0
    avg_power_w: float = 0.0
    peak_power_w: float = 0.0
    avg_current_a: float = 0.0
    peak_current_a: float = 0.0
    avg_voltage_v: float = 0.0
    duration_ms: float = 0.0
    sample_count: int = 0
    
    def compute_metrics(self) -> None:
        """Compute derived metrics from raw data"""
        if len(self.timestamps_ms) > 0:
            self.sample_count = len(self.timestamps_ms)
            self.duration_ms = float(self.timestamps_ms[-1] - self.timestamps_ms[0])
            
            self.avg_voltage_v = float(np.mean(self.voltage_v))
            self.avg_current_a = float(np.mean(self.current_a))
            self.peak_current_a = float(np.max(np.abs(self.current_a)))
            
            self.avg_power_w = float(np.mean(self.power_w))
            self.peak_power_w = float(np.max(self.power_w))
            
            # Compute energy using trapezoidal integration
            if self.duration_ms > 0:
                # numpy.trapezoid is the new name (numpy >= 2.0), fallback to trapz
                time_s = self.timestamps_ms / 1000.0
                try:
                    self.energy_j = float(np.trapezoid(self.power_w, time_s))  # type: ignore
                except AttributeError:
                    # Fallback for older numpy
                    self.energy_j = float(np.trapz(self.power_w, time_s))  # type: ignore


@dataclass
class JoulescopeStatus:
    """Joulescope device status"""
    connected: bool = False
    device_serial: str = ""
    recording: bool = False
    sample_rate_hz: int = 1000
    current_range: str = "auto"
    voltage_range: str = "15 V"
    samples_collected: int = 0


class JoulescopeInterface:
    """
    Interface for Joulescope JS220 power analyzer
    
    Provides methods to configure and record power measurements
    for motor energy testing.
    """
    
    # Valid device output sampling frequencies (joulescope v1.x API)
    # Device can output at these rates; lower rates require software downsampling
    VALID_DEVICE_FREQUENCIES = [2000000, 1000000, 500000, 200000]  # Hz
    
    DEFAULT_SAMPLE_RATE = 1000  # Hz - our target output rate
    DEFAULT_DEVICE_FREQUENCY = 500000  # Hz (500kHz) - device output rate
    
    def __init__(self) -> None:
        self._device: Any = None
        self._connected: bool = False
        self._recording: bool = False
        
        self._status = JoulescopeStatus()
        self._power_data: List[PowerDataPoint] = []
        
        self._sample_rate: int = self.DEFAULT_SAMPLE_RATE
        self._device_frequency: int = self.DEFAULT_DEVICE_FREQUENCY  # Device output frequency
        self._current_range: CurrentRange = CurrentRange.AUTO
        self._voltage_range: VoltageRange = VoltageRange.R_15V
        
        self._start_time_ms: float = 0.0
        self._data_lock = threading.Lock()
        self._recorder_thread: Optional[threading.Thread] = None
        self._running: bool = False
        
        # Callbacks
        self._on_data: Optional[Callable[[PowerDataPoint], None]] = None
        self._on_response: Optional[Callable[[str], None]] = None
    
    @staticmethod
    def is_available() -> bool:
        """Check if Joulescope package is available"""
        return JOULESCOPE_AVAILABLE
    
    @staticmethod
    def get_api_version() -> int:
        """Get the API version in use (0=none, 1=v0.x, 2=v1.x, 3=pyjoulescope_driver)"""
        return JOULESCOPE_API_VERSION
    
    @staticmethod
    def list_devices() -> List[str]:
        """List available Joulescope devices"""
        if not JOULESCOPE_AVAILABLE:
            return []
        try:
            if JOULESCOPE_API_VERSION == 3 and pyjoulescope_driver is not None:
                # pyjoulescope_driver API - use DeviceNotify or info command
                # Note: This API changed between versions
                if hasattr(pyjoulescope_driver, 'scan'):
                    devices = pyjoulescope_driver.scan()  # type: ignore
                    return [str(d) for d in devices]
                elif hasattr(pyjoulescope_driver, 'device_list'):
                    devices = pyjoulescope_driver.device_list()  # type: ignore
                    return [str(d) for d in devices]
                # Fallback - just return a placeholder
                return ["JS220 (auto-detect)"]
            elif JOULESCOPE_API_VERSION == 2 and joulescope is not None:
                # joulescope v1.x - uses scan_require_one or scan
                if hasattr(joulescope, 'scan'):
                    devices = joulescope.scan()
                    return [str(d) for d in devices]
                return []
            elif joulescope is not None:
                # joulescope v0.x
                devices = joulescope.scan()
                result: List[str] = []
                for d in devices:
                    serial = getattr(d, 'device_serial_number', None)
                    if serial is not None:
                        result.append(str(serial))
                return result
            return []
        except Exception as e:
            print(f"Device scan error: {e}")
            return []
    
    def set_data_callback(self, callback: Callable[[PowerDataPoint], None]) -> None:
        """Set callback for real-time data points"""
        self._on_data = callback
    
    def set_response_callback(self, callback: Callable[[str], None]) -> None:
        """Set callback for status messages"""
        self._on_response = callback
    
    def _log(self, message: str) -> None:
        """Log message through callback"""
        if self._on_response:
            self._on_response(message)
    
    def connect(self, device_serial: Optional[str] = None) -> bool:
        """
        Connect to Joulescope device
        
        Args:
            device_serial: Specific device serial number, or None for first available
            
        Returns:
            True if connection successful
        """
        if not JOULESCOPE_AVAILABLE:
            self._log("ERROR: joulescope package not installed")
            return False
        
        if self._connected:
            self.disconnect()
        
        try:
            self._log(f"Connecting with API version {JOULESCOPE_API_VERSION}...")
            
            if JOULESCOPE_API_VERSION == 3 and pyjoulescope_driver is not None:
                # pyjoulescope_driver API (JS220)
                # Try different methods depending on API version
                try:
                    if hasattr(pyjoulescope_driver, 'scan'):
                        devices = pyjoulescope_driver.scan()  # type: ignore
                    elif hasattr(pyjoulescope_driver, 'device_list'):
                        devices = pyjoulescope_driver.device_list()  # type: ignore
                    else:
                        devices = []
                except Exception:
                    devices = []
                
                if not devices:
                    # Try auto-open without explicit scan
                    try:
                        self._device = pyjoulescope_driver.Driver()  # type: ignore
                        self._device.open()  # type: ignore
                    except Exception as e:
                        self._log(f"ERROR: Could not open JS220: {e}")
                        return False
                else:
                    device_path = devices[0] if not device_serial else device_serial
                    self._device = pyjoulescope_driver.Driver(device_path)  # type: ignore
                    self._device.open()  # type: ignore
                
                self._connected = True
                self._status.connected = True
                self._status.device_serial = str(getattr(self._device, 'serial_number', 'JS220'))
                self._log(f"Connected to JS220: {self._status.device_serial}")
                
            elif JOULESCOPE_API_VERSION == 2 and joulescope is not None:
                # joulescope v1.x API
                if hasattr(joulescope, 'scan_require_one'):
                    self._device = joulescope.scan_require_one()
                else:
                    devices = joulescope.scan()
                    if not devices:
                        self._log("ERROR: No Joulescope devices found")
                        return False
                    self._device = devices[0]
                
                if hasattr(self._device, 'open'):
                    self._device.open()
                
                self._connected = True
                self._status.connected = True
                self._status.device_serial = str(getattr(self._device, 'device_serial_number', 'unknown'))
                self._log(f"Connected to Joulescope {self._status.device_serial}")
                
            elif joulescope is not None:
                # joulescope v0.x API
                devices = joulescope.scan()
                if not devices:
                    self._log("ERROR: No Joulescope devices found")
                    return False
                
                # Select device
                selected_device: Any = None
                if device_serial:
                    for d in devices:
                        if getattr(d, 'device_serial_number', None) == device_serial:
                            selected_device = d
                            break
                    if selected_device is None:
                        self._log(f"ERROR: Device {device_serial} not found")
                        return False
                else:
                    selected_device = devices[0]
                
                self._device = selected_device
                
                # Open device
                if hasattr(self._device, 'open'):
                    self._device.open()
                
                self._connected = True
                self._status.connected = True
                self._status.device_serial = str(getattr(self._device, 'device_serial_number', 'unknown'))
                self._log(f"Connected to Joulescope {self._status.device_serial}")
            else:
                self._log("ERROR: No Joulescope API available")
                return False
            
            # Apply default configuration
            self._apply_config()
            
            return True
            
        except Exception as e:
            self._log(f"ERROR: Connection failed - {e}")
            self._connected = False
            return False
    
    def disconnect(self) -> None:
        """Disconnect from Joulescope"""
        if self._recording:
            self.stop_recording()
        
        if self._device is not None:
            try:
                # Stop streaming if active
                if hasattr(self._device, 'stop'):
                    try:
                        self._device.stop()
                    except Exception:
                        pass
                
                # Close the device properly
                if hasattr(self._device, 'close'):
                    self._device.close()
                    
                # For joulescope v1.x, ensure proper cleanup
                # The device object should be fully released
                self._log("Device closed")
            except Exception as e:
                self._log(f"Disconnect warning: {e}")
        
        self._device = None
        self._connected = False
        self._status.connected = False
        self._status.device_serial = ""
    
    @property
    def is_connected(self) -> bool:
        return self._connected and self._device is not None
    
    @property
    def is_recording(self) -> bool:
        return self._recording
    
    @property
    def connected(self) -> bool:
        """Alias for is_connected for orchestrator compatibility"""
        return self.is_connected
    
    def _apply_config(self) -> None:
        """Apply current configuration to device"""
        if self._device is None:
            return
        
        try:
            # JS220 uses different parameter names than JS110
            # Try v1.x API first (works with both JS110 and JS220 via joulescope package)
            if hasattr(self._device, 'parameter_set'):
                # Set device output sampling frequency
                # Valid values: 2000000, 1000000, 500000, 200000 Hz
                # Choose the smallest rate that is >= our target, or smallest if target is very low
                device_freq = self._device_frequency
                for freq in sorted(self.VALID_DEVICE_FREQUENCIES):
                    if freq >= self._sample_rate:
                        device_freq = freq
                        break
                else:
                    # Target rate is higher than any supported rate, use max
                    device_freq = max(self.VALID_DEVICE_FREQUENCIES)
                
                self._device_frequency = device_freq
                
                try:
                    self._device.parameter_set('sampling_frequency', device_freq)
                    self._log(f"Device sampling frequency set to {device_freq/1000:.0f} kHz")
                    print(f"[JS] Device sampling frequency set to {device_freq/1000:.0f} kHz")
                except Exception as e:
                    self._log(f"Could not set sampling frequency: {e}")
                    print(f"[JS] Could not set sampling frequency: {e}")
                
                # Current range: 'auto', '10 A', '180 mA', '18 mA', '1800 µA', '180 µA', '18 µA'
                try:
                    self._device.parameter_set('i_range', self._current_range.value)
                except Exception:
                    pass  # Parameter might not be supported
                
                # Voltage range for JS220 is typically fixed or uses different values
                # JS110: '15 V', '2 V'
                # JS220: '15V' or just auto
                # Skip voltage range setting as JS220 handles it automatically
                
        except Exception as e:
            self._log(f"Config warning: {e}")
    
    def configure(self, 
                  sample_rate: int = DEFAULT_SAMPLE_RATE,
                  current_range: CurrentRange = CurrentRange.AUTO,
                  voltage_range: VoltageRange = VoltageRange.R_15V,
                  device_frequency: Optional[int] = None) -> bool:
        """
        Configure Joulescope measurement parameters
        
        Args:
            sample_rate: Output sample rate in Hz (our target, will be downsampled from device)
            current_range: Current measurement range
            voltage_range: Voltage measurement range
            device_frequency: Device output frequency in Hz (200000, 500000, 1000000, 2000000)
                           If None, automatically selects based on sample_rate
            
        Returns:
            True if configuration successful
        """
        self._sample_rate = sample_rate
        self._current_range = current_range
        self._voltage_range = voltage_range
        
        # Set device frequency - use provided value or auto-select
        if device_frequency is not None:
            # Validate provided frequency
            if device_frequency in self.VALID_DEVICE_FREQUENCIES:
                self._device_frequency = device_frequency
            else:
                # Find nearest valid frequency
                self._device_frequency = min(self.VALID_DEVICE_FREQUENCIES, 
                                             key=lambda x: abs(x - device_frequency))
        # else: _apply_config will auto-select based on sample_rate
        
        self._status.sample_rate_hz = sample_rate
        self._status.current_range = current_range.value
        self._status.voltage_range = voltage_range.value
        
        if self.is_connected:
            self._apply_config()
        
        return True
    
    def get_status(self) -> JoulescopeStatus:
        """Get current device status"""
        return self._status
    
    def clear_data(self) -> None:
        """Clear recorded data buffer"""
        with self._data_lock:
            self._power_data.clear()
        self._status.samples_collected = 0
    
    def start_recording(self) -> bool:
        """
        Start recording power data
        
        Returns:
            True if recording started successfully
        """
        if not self.is_connected:
            self._log("ERROR: Not connected")
            return False
        
        if self._recording:
            self._log("WARNING: Already recording")
            return True
        
        # Clear previous data
        self.clear_data()
        
        self._recording = True
        self._running = True
        self._status.recording = True
        
        # Start recorder thread
        self._recorder_thread = threading.Thread(
            target=self._recording_loop_simple,
            daemon=True
        )
        self._recorder_thread.start()
        
        self._log("Recording started")
        return True
    
    def _recording_loop_simple(self) -> None:
        """Simple polling-based recording loop"""
        if self._device is None:
            self._log("ERROR: No device in recording loop")
            return
            
        sample_interval = 1.0 / self._sample_rate
        self._start_time_ms = time.time() * 1000
        
        self._log(f"Recording loop started (API v{JOULESCOPE_API_VERSION})")
        
        # API v3 (pyjoulescope_driver) - uses different streaming approach
        if JOULESCOPE_API_VERSION == 3:
            self._recording_loop_js220()
            return
        
        # API v2 (joulescope v1.x) - uses read(duration=) method
        if JOULESCOPE_API_VERSION == 2:
            self._recording_loop_v1x()
            return
        
        # Start device streaming if method available (v0.x)
        try:
            if hasattr(self._device, 'start'):
                self._device.start()
                self._log("Device streaming started")
        except Exception as e:
            self._log(f"Start error: {e}")
        
        sample_count = 0
        read_errors = 0
        while self._running and self._recording:
            loop_start = time.time()
            
            try:
                # Read data using appropriate method (v0.x API)
                if hasattr(self._device, 'read'):
                    data = self._device.read()
                    if data is not None:
                        timestamp_ms = time.time() * 1000 - self._start_time_ms
                        
                        # Extract values from data structure
                        voltage = self._extract_value(data, 'voltage', 'v')
                        current = self._extract_value(data, 'current', 'i')
                        power = voltage * current
                        
                        point = PowerDataPoint(
                            timestamp_ms=timestamp_ms,
                            voltage_v=voltage,
                            current_a=current,
                            power_w=power
                        )
                        
                        with self._data_lock:
                            self._power_data.append(point)
                        
                        if self._on_data:
                            self._on_data(point)
                        
                        sample_count += 1
                        read_errors = 0  # Reset error counter on success
                elif hasattr(self._device, 'statistics'):
                    # No read method, use statistics if available
                    timestamp_ms = time.time() * 1000 - self._start_time_ms
                    stats = self._device.statistics()
                    if stats:
                        point = self._parse_statistics(stats, timestamp_ms)
                        if point:
                            with self._data_lock:
                                self._power_data.append(point)
                            if self._on_data:
                                self._on_data(point)
                            sample_count += 1
                            read_errors = 0
                else:
                    # Fallback - try accumulators for JS220 v1.x API
                    if hasattr(self._device, 'accumulators'):
                        timestamp_ms = time.time() * 1000 - self._start_time_ms
                        try:
                            acc = self._device.accumulators
                            if hasattr(acc, 'value'):
                                values = acc.value
                                current = values.get('current', {}).get('avg', 0.0)
                                voltage = values.get('voltage', {}).get('avg', 0.0)
                                power = current * voltage
                                
                                point = PowerDataPoint(
                                    timestamp_ms=timestamp_ms,
                                    voltage_v=voltage,
                                    current_a=current,
                                    power_w=power
                                )
                                with self._data_lock:
                                    self._power_data.append(point)
                                if self._on_data:
                                    self._on_data(point)
                                sample_count += 1
                                read_errors = 0
                        except Exception:
                            pass
                    
            except Exception as e:
                read_errors += 1
                if read_errors == 1:
                    self._log(f"Read error: {e}")
            
            self._status.samples_collected = sample_count
            
            elapsed = time.time() - loop_start
            if elapsed < sample_interval:
                time.sleep(sample_interval - elapsed)
        
        self._log(f"Recording loop ended ({sample_count} samples)")
    
    def _recording_loop_v1x(self) -> None:
        """
        Recording loop for joulescope v1.x API (JS110/JS220)
        
        Uses the recommended read(contiguous_duration=..., out_format='calibrated') method
        which returns Nx2 numpy array with columns [current, voltage].
        
        This method handles all the internal streaming and guarantees contiguous samples
        with no gaps.
        """
        sample_count = 0
        
        # Read duration in seconds - larger chunks are more efficient
        # 100ms chunks had ~70ms overhead per read, giving only 57% efficiency
        # 500ms chunks should give ~87% efficiency (500ms data / 575ms total)
        read_duration = 0.5  # 500ms chunks for better efficiency
        
        # The key insight: read() returns data at the device's output_sampling_frequency
        # For JS220, default is 200 kHz. We subsample to our target rate.
        
        print(f"[JS] _recording_loop_v1x started")
        print(f"[JS] Target sample rate: {self._sample_rate} Hz")
        print(f"[JS] Read chunk duration: {read_duration*1000:.0f}ms")
        
        self._log("Starting joulescope v1.x recording...")
        self._log(f"Target sample rate: {self._sample_rate} Hz")
        self._log(f"Read chunk duration: {read_duration*1000:.0f}ms")
        
        # Calculate expected samples per chunk based on target rate
        expected_samples_per_chunk = int(self._sample_rate * read_duration)
        print(f"[JS] Expected samples per {read_duration*1000:.0f}ms chunk: {expected_samples_per_chunk}")
        
        # Initialize counters before try block so they're always bound
        read_count = 0
        consecutive_errors = 0
        
        try:
            first_read = True
            
            while self._running and self._recording:
                loop_start = time.time()
                read_count += 1
                
                try:
                    # Use contiguous_duration to ensure no missing samples
                    # out_format='calibrated' returns Nx2 numpy array: column 0=current, column 1=voltage
                    data = self._device.read(contiguous_duration=read_duration, out_format='calibrated')
                    consecutive_errors = 0  # Reset on success
                    
                    if data is not None and isinstance(data, np.ndarray):
                        chunk_samples = data.shape[0]
                        
                        # Debug first read
                        if first_read:
                            print(f"[JS] First read: shape={data.shape}, dtype={data.dtype}")
                            self._log(f"Data: shape={data.shape}, dtype={data.dtype}")
                            
                            # Calculate actual device output rate
                            actual_rate = chunk_samples / read_duration
                            print(f"[JS] Device actual output rate: {actual_rate/1000:.1f} kHz")
                            self._log(f"Device rate: {actual_rate/1000:.1f} kHz")
                            first_read = False
                        
                        # Calculate subsampling step to achieve target rate
                        # Device rate = chunk_samples / read_duration
                        device_rate = chunk_samples / read_duration
                        if self._sample_rate >= device_rate:
                            step = 1  # No subsampling needed - use all samples
                        else:
                            step = max(1, int(device_rate / self._sample_rate))
                        
                        # Calculate base timestamp for this chunk
                        chunk_duration_ms = read_duration * 1000
                        base_time_ms = time.time() * 1000 - self._start_time_ms
                        
                        samples_this_chunk = 0
                        # Process samples at target rate
                        for idx in range(0, chunk_samples, step):
                            # Linear interpolation of timestamp within chunk
                            timestamp_ms = base_time_ms - chunk_duration_ms + (idx / chunk_samples) * chunk_duration_ms
                            
                            current = float(data[idx, 0])  # Column 0 is current (A)
                            voltage = float(data[idx, 1])  # Column 1 is voltage (V)
                            power = current * voltage
                            
                            point = PowerDataPoint(
                                timestamp_ms=timestamp_ms,
                                voltage_v=voltage,
                                current_a=current,
                                power_w=power
                            )
                            
                            with self._data_lock:
                                self._power_data.append(point)
                            
                            if self._on_data:
                                self._on_data(point)
                            
                            sample_count += 1
                            samples_this_chunk += 1
                        
                        # Periodic logging (every 10 reads = ~1 second)
                        if read_count % 10 == 0:
                            print(f"[JS] Progress: {read_count} reads, {sample_count} samples total")
                            
                except Exception as e:
                    consecutive_errors += 1
                    if consecutive_errors <= 3:
                        print(f"[JS] Read error #{consecutive_errors}: {e}")
                        self._log(f"Read error: {e}")
                    if consecutive_errors >= 10:
                        print(f"[JS] Too many consecutive errors, stopping")
                        self._log("Too many read errors, stopping recording")
                        break
                    time.sleep(0.01)  # Small delay on error
                    
        except Exception as e:
            import traceback
            print(f"[JS] Recording loop exception: {e}")
            print(f"[JS] Traceback: {traceback.format_exc()}")
            self._log(f"Recording loop error: {e}")
        finally:
            print(f"[JS] Recording complete: {read_count} reads, {sample_count} samples")
            self._log(f"Recording complete: {sample_count} samples")
            # Ensure device is stopped
            if hasattr(self._device, 'stop'):
                try:
                    self._device.stop()
                except Exception:
                    pass
    
    def _recording_loop_js220(self) -> None:
        """Recording loop for JS220 with pyjoulescope_driver (low-level API)"""
        # pyjoulescope_driver uses a different streaming model
        sample_count = 0
        
        self._log("Starting pyjoulescope_driver recording...")
        
        try:
            while self._running and self._recording:
                timestamp_ms = time.time() * 1000 - self._start_time_ms
                
                # Try to get current values from device
                try:
                    current = 0.0
                    voltage = 0.0
                    
                    if hasattr(self._device, 'parameter_get'):
                        # Read statistics from parameter interface
                        try:
                            current = float(self._device.parameter_get('sensor/current/avg') or 0.0)
                            voltage = float(self._device.parameter_get('sensor/voltage/avg') or 0.0)
                        except Exception:
                            pass
                    
                    if current == 0.0 and hasattr(self._device, 'statistics_get'):
                        stats = self._device.statistics_get()
                        if stats:
                            current = float(stats.get('current', {}).get('avg', 0.0))
                            voltage = float(stats.get('voltage', {}).get('avg', 0.0))
                    
                    power = current * voltage
                    
                    point = PowerDataPoint(
                        timestamp_ms=timestamp_ms,
                        voltage_v=voltage,
                        current_a=current,
                        power_w=power
                    )
                    
                    with self._data_lock:
                        self._power_data.append(point)
                    
                    if self._on_data:
                        self._on_data(point)
                    
                    sample_count += 1
                    
                except Exception as e:
                    if sample_count == 0:
                        self._log(f"JS220 read error: {e}")
                
                time.sleep(1.0 / max(self._sample_rate, 10))  # Limit polling rate
                
        except Exception as e:
            self._log(f"JS220 recording loop error: {e}")
        
        self._log(f"JS220 recording ended ({sample_count} samples)")
    
    def _extract_value(self, data: Any, *keys: str) -> float:
        """Extract value from data structure by trying multiple keys"""
        if data is None:
            return 0.0
        
        for key in keys:
            if hasattr(data, key):
                val = getattr(data, key)
                if isinstance(val, (int, float)):
                    return float(val)
                if isinstance(val, np.ndarray) and len(val) > 0:
                    return float(np.mean(val))
            if isinstance(data, dict):
                if key in data:
                    val = data[key]
                    if isinstance(val, (int, float)):
                        return float(val)
                    if isinstance(val, dict) and 'avg' in val:
                        return float(val['avg'])
        return 0.0
    
    def _parse_statistics(self, stats: Any, timestamp_ms: float) -> Optional[PowerDataPoint]:
        """Parse statistics structure from Joulescope"""
        try:
            if isinstance(stats, dict):
                signals = stats.get('signals', {})
                v_stats = signals.get('voltage', {})
                i_stats = signals.get('current', {})
                p_stats = signals.get('power', {})
                
                return PowerDataPoint(
                    timestamp_ms=timestamp_ms,
                    voltage_v=float(v_stats.get('avg', 0.0)),
                    current_a=float(i_stats.get('avg', 0.0)),
                    power_w=float(p_stats.get('avg', 0.0))
                )
        except Exception:
            pass
        return None
    
    def stop_recording(self) -> PowerData:
        """
        Stop recording and return collected data
        
        Returns:
            PowerData with all collected measurements
        """
        self._recording = False
        self._running = False
        self._status.recording = False
        
        # Wait for thread to finish
        if self._recorder_thread and self._recorder_thread.is_alive():
            self._recorder_thread.join(timeout=2.0)
        
        # Compile data
        with self._data_lock:
            data = PowerData()
            
            if self._power_data:
                data.timestamps_ms = np.array([p.timestamp_ms for p in self._power_data], dtype=np.float64)
                data.voltage_v = np.array([p.voltage_v for p in self._power_data], dtype=np.float64)
                data.current_a = np.array([p.current_a for p in self._power_data], dtype=np.float64)
                data.power_w = np.array([p.power_w for p in self._power_data], dtype=np.float64)
                
                data.compute_metrics()
        
        self._log(f"Recording stopped ({data.sample_count} samples)")
        return data
    
    # ==================== Streaming Interface ====================
    
    def start_streaming(self, sample_rate: int = DEFAULT_SAMPLE_RATE) -> bool:
        """Start streaming mode (alias for start_recording)"""
        self._sample_rate = sample_rate
        return self.start_recording()
    
    def stop_streaming(self) -> PowerData:
        """Stop streaming mode (alias for stop_recording)"""
        return self.stop_recording()
    
    def read_sample(self) -> Optional[PowerDataPoint]:
        """
        Read single sample from buffer
        
        Returns:
            Latest PowerDataPoint or None
        """
        with self._data_lock:
            if self._power_data:
                return self._power_data[-1]
        return None


# =============================================================================
# Mock class for testing without hardware
# =============================================================================

class MockJoulescopeInterface(JoulescopeInterface):
    """
    Mock Joulescope interface for testing without hardware
    
    Generates simulated power data for development and testing.
    """
    
    def __init__(self) -> None:
        super().__init__()
        self._mock_voltage = 5.0
        self._mock_current_base = 0.05  # 50mA base
    
    @staticmethod
    def is_available() -> bool:
        return True
    
    @staticmethod
    def list_devices() -> List[str]:
        return ["MOCK-JS220-001"]
    
    def connect(self, device_serial: Optional[str] = None) -> bool:
        self._connected = True
        self._status.connected = True
        self._status.device_serial = "MOCK-JS220-001"
        self._log("Connected to MOCK Joulescope")
        return True
    
    def disconnect(self) -> None:
        if self._recording:
            self.stop_recording()
        self._connected = False
        self._status.connected = False
    
    def _recording_loop_simple(self) -> None:
        """Generate mock data"""
        sample_interval = 1.0 / self._sample_rate
        self._start_time_ms = time.time() * 1000
        
        self._log("Mock recording started")
        
        sample_count = 0
        while self._running and self._recording:
            loop_start = time.time()
            timestamp_ms = time.time() * 1000 - self._start_time_ms
            
            # Simulate motor current profile
            t_s = timestamp_ms / 1000.0
            
            # Add some realistic variation
            noise = float(np.random.normal(0, 0.002))  # 2mA noise
            
            # Simulate motor startup surge and steady state
            if t_s < 0.5:
                # Idle
                current = self._mock_current_base * 0.5 + noise
            elif t_s < 1.0:
                # Startup surge
                current = self._mock_current_base * 3 + noise
            elif t_s < 4.0:
                # Running
                current = self._mock_current_base * (1 + 0.2 * np.sin(t_s * 10)) + noise
            else:
                # Idle again
                current = self._mock_current_base * 0.5 + noise
            
            voltage = self._mock_voltage + float(np.random.normal(0, 0.01))
            power = voltage * current
            
            point = PowerDataPoint(
                timestamp_ms=timestamp_ms,
                voltage_v=voltage,
                current_a=current,
                power_w=power
            )
            
            with self._data_lock:
                self._power_data.append(point)
            
            if self._on_data:
                self._on_data(point)
            
            sample_count += 1
            
            elapsed = time.time() - loop_start
            if elapsed < sample_interval:
                time.sleep(sample_interval - elapsed)
        
        self._log(f"Mock recording stopped ({sample_count} samples)")


# =============================================================================
# Factory function
# =============================================================================

def get_joulescope_interface(use_mock: bool = False) -> JoulescopeInterface:
    """
    Get appropriate Joulescope interface
    
    Args:
        use_mock: Force use of mock interface for testing
        
    Returns:
        JoulescopeInterface or MockJoulescopeInterface
    """
    if use_mock or not JOULESCOPE_AVAILABLE:
        print("Using Mock Joulescope interface")
        return MockJoulescopeInterface()
    return JoulescopeInterface()


# =============================================================================
# Example usage and testing
# =============================================================================

if __name__ == "__main__":
    print("Joulescope Interface - Test Mode")
    print("=" * 50)
    
    # Use mock for testing without hardware
    USE_MOCK = True
    
    js = get_joulescope_interface(use_mock=USE_MOCK)
    
    # Set up callbacks
    def on_data(point: PowerDataPoint) -> None:
        print(f"  t={point.timestamp_ms:7.1f}ms  V={point.voltage_v:.3f}V  "
              f"I={point.current_a*1000:.2f}mA  P={point.power_w*1000:.2f}mW")
    
    def on_message(msg: str) -> None:
        print(f"[JS] {msg}")
    
    js.set_data_callback(on_data)
    js.set_response_callback(on_message)
    
    # List devices
    devices = js.list_devices()
    print(f"Available devices: {devices}")
    
    # Connect
    print("\nConnecting...")
    if js.connect():
        print("Connected!")
        
        # Configure
        js.configure(
            sample_rate=100,  # 100 Hz output
            current_range=CurrentRange.AUTO,
            voltage_range=VoltageRange.R_15V
        )
        
        # Get status
        status = js.get_status()
        print(f"\nStatus:")
        print(f"  Serial: {status.device_serial}")
        print(f"  Sample Rate: {status.sample_rate_hz} Hz")
        print(f"  Current Range: {status.current_range}")
        print(f"  Voltage Range: {status.voltage_range}")
        
        # Record for 5 seconds
        print("\nRecording for 5 seconds...")
        js.start_recording()
        time.sleep(5)
        
        # Stop and get data
        data = js.stop_recording()
        
        print(f"\nResults:")
        print(f"  Samples: {data.sample_count}")
        print(f"  Duration: {data.duration_ms:.1f} ms")
        print(f"  Avg Voltage: {data.avg_voltage_v:.3f} V")
        print(f"  Avg Current: {data.avg_current_a*1000:.2f} mA")
        print(f"  Peak Current: {data.peak_current_a*1000:.2f} mA")
        print(f"  Avg Power: {data.avg_power_w*1000:.2f} mW")
        print(f"  Peak Power: {data.peak_power_w*1000:.2f} mW")
        print(f"  Total Energy: {data.energy_j*1000:.2f} mJ")
        
        js.disconnect()
        print("\nDisconnected.")
    else:
        print("Failed to connect!")
