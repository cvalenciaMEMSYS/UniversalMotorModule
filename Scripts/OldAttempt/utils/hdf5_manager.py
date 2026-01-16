"""
HDF5 Manager - Data storage and retrieval for energy measurements

This module handles all HDF5 file operations for storing test data,
metadata, alignment information, computed metrics, and embedded plots.

Schema:
  Root/
    motor_info/          - Motor metadata (attributes)
    <test_id>/           - Test group
      metadata/          - Test configuration
      raw_data/          - Raw measurements
        force/           - Force data arrays
        power/           - Power data arrays  
      alignment/         - User-defined analysis windows
      computed/          - Computed metrics
      plots/             - Embedded PNG images

Author: Energy Measurement Test System
Date: January 2026
"""

import h5py
import numpy as np
from datetime import datetime
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import Optional, Dict, List, Any, Union
import io


@dataclass
class TestMetadata:
    """Test configuration metadata"""
    test_id: str = ""
    timestamp: str = ""
    
    # Motor identification
    motor_id: str = ""
    motor_model: str = ""
    
    # Profile settings
    profile: str = "constant"  # constant, trapezoidal, scurve
    speed_sps: int = 100
    accel_sps2: int = 0
    cubesteps: int = 0
    current_ma: int = 400
    voltage_v: float = 5.0
    
    # Test parameters
    test_duration_ms: int = 5000
    test_mode: str = "time"  # time or steps
    test_steps: int = 0
    
    # FD settings
    fd_mode: str = "monitor"  # monitor or backdrive
    fd_sample_rate_hz: int = 100
    fd_timeout_ms: int = 30000
    fd_force_limit_n: float = 10.0
    
    # Joulescope settings
    js_sample_rate_hz: int = 1000
    js_current_range: str = "auto"
    js_voltage_range: str = "15V"
    
    # Notes
    notes: str = ""


@dataclass
class AlignmentData:
    """User-defined alignment windows"""
    power_start_ms: float = 0.0
    power_stop_ms: float = 0.0
    force_start_ms: float = 0.0
    force_stop_ms: float = 0.0
    aligned: bool = False


@dataclass
class ComputedMetrics:
    """Computed metrics from aligned data"""
    # Force metrics
    max_force_n: float = 0.0
    avg_force_n: float = 0.0
    min_force_n: float = 0.0
    max_thrust_g: float = 0.0
    
    # Power metrics
    avg_power_w: float = 0.0
    peak_power_w: float = 0.0
    min_power_w: float = 0.0
    
    # Current metrics
    avg_current_a: float = 0.0
    peak_current_a: float = 0.0
    
    # Voltage metrics
    avg_voltage_v: float = 0.0
    
    # Energy
    total_energy_j: float = 0.0
    
    # Duration
    aligned_duration_ms: float = 0.0
    force_duration_ms: float = 0.0
    power_duration_ms: float = 0.0


@dataclass
class ForceRawData:
    """Raw force measurement data"""
    time_ms: np.ndarray = field(default_factory=lambda: np.array([]))
    force_n: np.ndarray = field(default_factory=lambda: np.array([]))
    position_mm: np.ndarray = field(default_factory=lambda: np.array([]))


@dataclass
class PowerRawData:
    """Raw power measurement data"""
    time_ms: np.ndarray = field(default_factory=lambda: np.array([]))
    voltage_v: np.ndarray = field(default_factory=lambda: np.array([]))
    current_a: np.ndarray = field(default_factory=lambda: np.array([]))
    power_w: np.ndarray = field(default_factory=lambda: np.array([]))


@dataclass
class TestRecord:
    """Complete test record"""
    metadata: TestMetadata = field(default_factory=TestMetadata)
    force_data: ForceRawData = field(default_factory=ForceRawData)
    power_data: PowerRawData = field(default_factory=PowerRawData)
    alignment: AlignmentData = field(default_factory=AlignmentData)
    computed: ComputedMetrics = field(default_factory=ComputedMetrics)
    plots: Dict[str, bytes] = field(default_factory=dict)


class HDF5Manager:
    """
    Manager for HDF5 data files
    
    Handles creation, reading, and updating of test data files.
    Each file contains data for one motor for one day.
    """
    
    def __init__(self, data_dir: Union[str, Path] = "test_data"):
        """
        Initialize HDF5 manager
        
        Args:
            data_dir: Directory for storing HDF5 files
        """
        self.data_dir = Path(data_dir)
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self._current_file: Optional[h5py.File] = None
        self._current_path: Optional[Path] = None
    
    def _get_file_path(self, motor_id: str, date: Optional[datetime] = None) -> Path:
        """Get file path for motor and date"""
        actual_date = date if date is not None else datetime.now()
        filename = f"{motor_id}_{actual_date.strftime('%Y-%m-%d')}.h5"
        return self.data_dir / filename
    
    def open(self, motor_id: str, date: Optional[datetime] = None) -> bool:
        """
        Open or create HDF5 file for motor
        
        Args:
            motor_id: Motor identifier (e.g., "M1")
            date: Date for file (default: today)
            
        Returns:
            True if successful
        """
        self.close()
        
        file_path = self._get_file_path(motor_id, date)
        
        try:
            # Open or create file
            self._current_file = h5py.File(file_path, 'a')
            self._current_path = file_path
            
            # Initialize motor info if new file
            if 'motor_info' not in self._current_file:
                self._current_file.create_group('motor_info')
                self._current_file['motor_info'].attrs['motor_id'] = motor_id
                self._current_file['motor_info'].attrs['created'] = datetime.now().isoformat()
                self._current_file['motor_info'].attrs['motor_model'] = ""
                self._current_file['motor_info'].attrs['notes'] = ""
            
            # Initialize summary if not exists
            if 'summary' not in self._current_file:
                summary = self._current_file.create_group('summary')
                summary.create_dataset('test_index', data=[], 
                                      dtype=h5py.special_dtype(vlen=str),
                                      maxshape=(None,))
            
            return True
            
        except Exception as e:
            print(f"Failed to open HDF5 file: {e}")
            return False
    
    def close(self):
        """Close current HDF5 file"""
        if self._current_file:
            try:
                self._current_file.close()
            except:
                pass
        self._current_file = None
        self._current_path = None
    
    @property
    def is_open(self) -> bool:
        return self._current_file is not None
    
    @property
    def file_path(self) -> Optional[Path]:
        return self._current_path
    
    def set_motor_info(self, motor_model: str = "", notes: str = "") -> None:
        """Set motor metadata"""
        if not self.is_open or self._current_file is None:
            return
        
        self._current_file['motor_info'].attrs['motor_model'] = motor_model
        self._current_file['motor_info'].attrs['notes'] = notes
    
    def get_motor_info(self) -> Dict[str, str]:
        """Get motor metadata"""
        if not self.is_open or self._current_file is None:
            return {}
        
        info = self._current_file['motor_info']
        return {
            'motor_id': info.attrs.get('motor_id', ''),
            'motor_model': info.attrs.get('motor_model', ''),
            'created': info.attrs.get('created', ''),
            'notes': info.attrs.get('notes', '')
        }
    
    def create_test(self, test_id: str, metadata: TestMetadata) -> bool:
        """
        Create new test entry
        
        Args:
            test_id: Unique test identifier (e.g., "M1-CV-3")
            metadata: Test configuration metadata
            
        Returns:
            True if successful
        """
        if not self.is_open or self._current_file is None:
            return False
        
        if test_id in self._current_file:
            print(f"Test {test_id} already exists")
            return False
        
        try:
            # Create test group
            test_group = self._current_file.create_group(test_id)
            
            # Store metadata as attributes
            meta_group = test_group.create_group('metadata')
            for key, value in asdict(metadata).items():
                meta_group.attrs[key] = value
            
            # Create raw data groups
            raw_group = test_group.create_group('raw_data')
            force_group = raw_group.create_group('force')
            power_group = raw_group.create_group('power')
            
            # Create placeholder datasets
            force_group.create_dataset('time_ms', data=np.array([]), 
                                       maxshape=(None,), dtype='float64')
            force_group.create_dataset('force_n', data=np.array([]), 
                                       maxshape=(None,), dtype='float64')
            force_group.create_dataset('position_mm', data=np.array([]), 
                                       maxshape=(None,), dtype='float64')
            
            power_group.create_dataset('time_ms', data=np.array([]), 
                                       maxshape=(None,), dtype='float64')
            power_group.create_dataset('voltage_v', data=np.array([]), 
                                       maxshape=(None,), dtype='float64')
            power_group.create_dataset('current_a', data=np.array([]), 
                                       maxshape=(None,), dtype='float64')
            power_group.create_dataset('power_w', data=np.array([]), 
                                       maxshape=(None,), dtype='float64')
            
            # Create alignment group
            align_group = test_group.create_group('alignment')
            align_group.attrs['power_start_ms'] = 0.0
            align_group.attrs['power_stop_ms'] = 0.0
            align_group.attrs['force_start_ms'] = 0.0
            align_group.attrs['force_stop_ms'] = 0.0
            align_group.attrs['aligned'] = False
            
            # Create computed group
            comp_group = test_group.create_group('computed')
            for key, value in asdict(ComputedMetrics()).items():
                comp_group.attrs[key] = value
            
            # Create plots group
            test_group.create_group('plots')
            
            # Update test index
            self._add_to_index(test_id)
            
            self._current_file.flush()
            return True
            
        except Exception as e:
            print(f"Failed to create test: {e}")
            return False
    
    def _add_to_index(self, test_id: str) -> None:
        """Add test ID to summary index"""
        if self._current_file is None:
            return
        index_ds = self._current_file['summary/test_index']
        if isinstance(index_ds, h5py.Dataset):
            current_ids = list(index_ds[()])
            if test_id not in current_ids:
                current_ids.append(test_id)
                index_ds.resize((len(current_ids),))
                index_ds[()] = current_ids
    
    def save_force_data(self, test_id: str, data: ForceRawData) -> bool:
        """Save force measurement data"""
        if not self.is_open or self._current_file is None or test_id not in self._current_file:
            return False
        
        try:
            force_grp = self._current_file[f'{test_id}/raw_data/force']
            if not isinstance(force_grp, h5py.Group):
                return False
            force_group = force_grp
            
            # Resize and write data
            for name, arr in [('time_ms', data.time_ms), 
                             ('force_n', data.force_n),
                             ('position_mm', data.position_mm)]:
                ds = force_group[name]
                if isinstance(ds, h5py.Dataset):
                    ds.resize((len(arr),))
                    ds[()] = arr
            
            self._current_file.flush()
            return True
            
        except Exception as e:
            print(f"Failed to save force data: {e}")
            return False
    
    def save_power_data(self, test_id: str, data: PowerRawData) -> bool:
        """Save power measurement data"""
        if not self.is_open or self._current_file is None or test_id not in self._current_file:
            return False
        
        try:
            power_grp = self._current_file[f'{test_id}/raw_data/power']
            if not isinstance(power_grp, h5py.Group):
                return False
            power_group = power_grp
            
            for name, arr in [('time_ms', data.time_ms),
                             ('voltage_v', data.voltage_v),
                             ('current_a', data.current_a),
                             ('power_w', data.power_w)]:
                ds = power_group[name]
                if isinstance(ds, h5py.Dataset):
                    ds.resize((len(arr),))
                    ds[()] = arr
            
            self._current_file.flush()
            return True
            
        except Exception as e:
            print(f"Failed to save power data: {e}")
            return False
    
    def save_alignment(self, test_id: str, alignment: AlignmentData) -> bool:
        """Save alignment window data"""
        if not self.is_open or self._current_file is None or test_id not in self._current_file:
            return False
        
        try:
            align_grp = self._current_file[f'{test_id}/alignment']
            if not isinstance(align_grp, h5py.Group):
                return False
            for key, value in asdict(alignment).items():
                align_grp.attrs[key] = value
            
            self._current_file.flush()
            return True
            
        except Exception as e:
            print(f"Failed to save alignment: {e}")
            return False
    
    def save_computed(self, test_id: str, computed: ComputedMetrics) -> bool:
        """Save computed metrics"""
        if not self.is_open or self._current_file is None or test_id not in self._current_file:
            return False
        
        try:
            comp_grp = self._current_file[f'{test_id}/computed']
            if not isinstance(comp_grp, h5py.Group):
                return False
            for key, value in asdict(computed).items():
                comp_grp.attrs[key] = value
            
            self._current_file.flush()
            return True
            
        except Exception as e:
            print(f"Failed to save computed metrics: {e}")
            return False
    
    def save_plot(self, test_id: str, plot_name: str, png_bytes: bytes) -> bool:
        """
        Save plot as PNG bytes
        
        Args:
            test_id: Test identifier
            plot_name: Plot name (e.g., 'force_vs_time', 'combined_plot')
            png_bytes: PNG image data as bytes
        """
        if not self.is_open or self._current_file is None or test_id not in self._current_file:
            return False
        
        try:
            plots_grp = self._current_file[f'{test_id}/plots']
            if not isinstance(plots_grp, h5py.Group):
                return False
            
            # Delete existing if present
            if plot_name in plots_grp:
                del plots_grp[plot_name]
            
            # Store as binary dataset
            plots_grp.create_dataset(
                plot_name, 
                data=np.frombuffer(png_bytes, dtype=np.uint8)
            )
            
            self._current_file.flush()
            return True
            
        except Exception as e:
            print(f"Failed to save plot: {e}")
            return False
    
    def load_test(self, test_id: str) -> Optional[TestRecord]:
        """
        Load complete test record
        
        Args:
            test_id: Test identifier
            
        Returns:
            TestRecord or None if not found
        """
        if not self.is_open or self._current_file is None or test_id not in self._current_file:
            return None
        
        try:
            test_grp = self._current_file[test_id]
            if not isinstance(test_grp, h5py.Group):
                return None
            test_group = test_grp
            record = TestRecord()
            
            # Load metadata
            meta_item = test_group['metadata']
            if isinstance(meta_item, h5py.Group):
                for key in asdict(record.metadata).keys():
                    if key in meta_item.attrs:
                        setattr(record.metadata, key, meta_item.attrs[key])
            
            # Load force data
            force_grp = test_group['raw_data/force']
            if isinstance(force_grp, h5py.Group):
                time_ds = force_grp['time_ms']
                force_ds = force_grp['force_n']
                pos_ds = force_grp['position_mm']
                if isinstance(time_ds, h5py.Dataset):
                    record.force_data.time_ms = np.array(time_ds[()])
                if isinstance(force_ds, h5py.Dataset):
                    record.force_data.force_n = np.array(force_ds[()])
                if isinstance(pos_ds, h5py.Dataset):
                    record.force_data.position_mm = np.array(pos_ds[()])
            
            # Load power data
            power_grp = test_group['raw_data/power']
            if isinstance(power_grp, h5py.Group):
                time_ds = power_grp['time_ms']
                volt_ds = power_grp['voltage_v']
                curr_ds = power_grp['current_a']
                pwr_ds = power_grp['power_w']
                if isinstance(time_ds, h5py.Dataset):
                    record.power_data.time_ms = np.array(time_ds[()])
                if isinstance(volt_ds, h5py.Dataset):
                    record.power_data.voltage_v = np.array(volt_ds[()])
                if isinstance(curr_ds, h5py.Dataset):
                    record.power_data.current_a = np.array(curr_ds[()])
                if isinstance(pwr_ds, h5py.Dataset):
                    record.power_data.power_w = np.array(pwr_ds[()])
            
            # Load alignment
            align_item = test_group['alignment']
            if isinstance(align_item, h5py.Group):
                for key in asdict(record.alignment).keys():
                    if key in align_item.attrs:
                        setattr(record.alignment, key, align_item.attrs[key])
            
            # Load computed
            comp_item = test_group['computed']
            if isinstance(comp_item, h5py.Group):
                for key in asdict(record.computed).keys():
                    if key in comp_item.attrs:
                        setattr(record.computed, key, comp_item.attrs[key])
            
            # Load plots
            plots_item = test_group['plots']
            if isinstance(plots_item, h5py.Group):
                for plot_name in plots_item.keys():
                    plot_ds = plots_item[plot_name]
                    if isinstance(plot_ds, h5py.Dataset):
                        plot_data = plot_ds[()]
                        record.plots[plot_name] = bytes(plot_data)
            
            return record
            
        except Exception as e:
            print(f"Failed to load test: {e}")
            return None
    
    def get_test_list(self) -> List[str]:
        """Get list of all test IDs in current file"""
        if not self.is_open or self._current_file is None:
            return []
        
        try:
            index_ds = self._current_file['summary/test_index']
            if isinstance(index_ds, h5py.Dataset):
                return list(index_ds[()])
            return []
        except Exception:
            return []
    
    def test_exists(self, test_id: str) -> bool:
        """Check if test exists in current file"""
        if not self.is_open or self._current_file is None:
            return False
        return test_id in self._current_file
    
    def delete_test(self, test_id: str) -> bool:
        """Delete test from file"""
        if not self.is_open or self._current_file is None or test_id not in self._current_file:
            return False
        
        try:
            del self._current_file[test_id]
            
            # Update index
            index_ds = self._current_file['summary/test_index']
            if isinstance(index_ds, h5py.Dataset):
                current_ids = [id for id in index_ds[()] if id != test_id]
                index_ds.resize((len(current_ids),))
                index_ds[()] = current_ids
            
            self._current_file.flush()
            return True
            
        except Exception as e:
            print(f"Failed to delete test: {e}")
            return False
    
    def get_summary(self) -> Dict[str, Any]:
        """Get summary of all tests in file"""
        if not self.is_open or self._current_file is None:
            return {}
        
        summary: Dict[str, Any] = {
            'motor_info': self.get_motor_info(),
            'test_count': len(self.get_test_list()),
            'tests': []
        }
        
        for test_id in self.get_test_list():
            try:
                test_item = self._current_file[test_id]
                if not isinstance(test_item, h5py.Group):
                    continue
                test = test_item
                
                meta_item = test['metadata']
                align_item = test['alignment']
                comp_item = test['computed']
                
                test_info: Dict[str, Any] = {
                    'test_id': test_id,
                    'timestamp': '',
                    'profile': '',
                    'voltage_v': 0,
                    'speed_sps': 0,
                    'aligned': False,
                    'max_force_n': 0,
                    'total_energy_j': 0
                }
                
                if isinstance(meta_item, h5py.Group):
                    test_info['timestamp'] = meta_item.attrs.get('timestamp', '')
                    test_info['profile'] = meta_item.attrs.get('profile', '')
                    test_info['voltage_v'] = meta_item.attrs.get('voltage_v', 0)
                    test_info['speed_sps'] = meta_item.attrs.get('speed_sps', 0)
                
                if isinstance(align_item, h5py.Group):
                    test_info['aligned'] = align_item.attrs.get('aligned', False)
                
                if isinstance(comp_item, h5py.Group):
                    test_info['max_force_n'] = comp_item.attrs.get('max_force_n', 0)
                    test_info['total_energy_j'] = comp_item.attrs.get('total_energy_j', 0)
                
                summary['tests'].append(test_info)
            except Exception:
                pass
        
        return summary


# =============================================================================
# Example usage and testing
# =============================================================================

if __name__ == "__main__":
    print("HDF5 Manager - Test Mode")
    print("=" * 50)
    
    # Create manager
    manager = HDF5Manager("test_data")
    
    # Open file for motor M1
    print("\nOpening file for M1...")
    manager.open("M1")
    print(f"File: {manager.file_path}")
    
    # Set motor info
    manager.set_motor_info(motor_model="Micro Stepper XYZ", notes="Test batch 1")
    
    # Create test metadata
    metadata = TestMetadata(
        test_id="M1-CV-3",
        timestamp=datetime.now().isoformat(),
        motor_id="M1",
        profile="constant",
        speed_sps=100,
        voltage_v=3.0,
        test_duration_ms=5000
    )
    
    # Create test
    print("\nCreating test M1-CV-3...")
    manager.create_test("M1-CV-3", metadata)
    
    # Generate some fake data
    t = np.linspace(0, 5000, 500)
    force_data = ForceRawData(
        time_ms=t,
        force_n=np.sin(t / 1000) * 2 + 1,
        position_mm=t * 0.001
    )
    
    power_data = PowerRawData(
        time_ms=t,
        voltage_v=np.ones_like(t) * 3.0 + np.random.normal(0, 0.01, len(t)),
        current_a=np.abs(np.sin(t / 1000)) * 0.1 + 0.05,
        power_w=np.abs(np.sin(t / 1000)) * 0.3 + 0.15
    )
    
    # Save data
    print("Saving force data...")
    manager.save_force_data("M1-CV-3", force_data)
    
    print("Saving power data...")
    manager.save_power_data("M1-CV-3", power_data)
    
    # Save alignment
    alignment = AlignmentData(
        power_start_ms=500,
        power_stop_ms=4500,
        force_start_ms=600,
        force_stop_ms=4400,
        aligned=True
    )
    manager.save_alignment("M1-CV-3", alignment)
    
    # Save computed metrics
    computed = ComputedMetrics(
        max_force_n=2.5,
        avg_force_n=1.5,
        max_thrust_g=255,
        avg_power_w=0.25,
        peak_power_w=0.45,
        total_energy_j=1.0
    )
    manager.save_computed("M1-CV-3", computed)
    
    # Get summary
    print("\nFile summary:")
    summary = manager.get_summary()
    print(f"  Motor: {summary['motor_info']['motor_id']}")
    print(f"  Model: {summary['motor_info']['motor_model']}")
    print(f"  Tests: {summary['test_count']}")
    
    for test in summary['tests']:
        print(f"    - {test['test_id']}: {test['profile']} @ {test['voltage_v']}V")
    
    # Load test back
    print("\nLoading test M1-CV-3...")
    record = manager.load_test("M1-CV-3")
    if record:
        print(f"  Profile: {record.metadata.profile}")
        print(f"  Force samples: {len(record.force_data.time_ms)}")
        print(f"  Power samples: {len(record.power_data.time_ms)}")
        print(f"  Aligned: {record.alignment.aligned}")
        print(f"  Max Force: {record.computed.max_force_n:.2f} N")
    
    manager.close()
    print("\nDone!")
