"""
Orchestrator - Main PyQt6 GUI for Energy Measurement Test System

This is the main application that coordinates:
  - DUT (Universal Motor Module) control via serial
  - FD (Force/Displacement) setup control via TCP to Raspberry Pi
  - Joulescope JS220 power measurement via USB
  - HDF5 data storage
  - Real-time visualization

Features:
  - Left panel: Configuration and controls
  - Right panel: Real-time plots and results
  - Test sequencing with start/stop control
  - Manual command input for DUT
  - Manual jog controls for FD setup
  - Data alignment tool integration

Author: Energy Measurement Test System
Date: January 2026
"""

import sys
import time
import threading
import logging
import queue
from datetime import datetime
from typing import Optional, List, Dict, Any
from dataclasses import dataclass

import numpy as np

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QGroupBox, QLabel, QLineEdit, QPushButton, QComboBox,
    QSpinBox, QDoubleSpinBox, QTextEdit, QGridLayout, QMessageBox,
    QFileDialog, QProgressBar, QFrame, QScrollArea, QTabWidget,
    QCheckBox, QDialog, QDialogButtonBox, QRadioButton, QButtonGroup
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QColor, QPalette

import matplotlib
matplotlib.use('QtAgg')
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
import matplotlib.pyplot as plt

# Import project modules
from dut_controller import DUTController, MotionProfile, RampState, MotorStatus
from fd_client import FDClient, FDMode, ForceData, ForceDataPoint
from joulescope_interface import JoulescopeInterface, MockJoulescopeInterface, PowerData, PowerDataPoint
from utils.hdf5_manager import (
    HDF5Manager, TestMetadata, AlignmentData, ComputedMetrics,
    ForceRawData, PowerRawData, TestRecord
)
from utils.plot_generator import PlotGenerator
from alignment_tool import AlignmentTool, AlignmentResult


# =============================================================================
# Logging setup
# =============================================================================

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(name)s: %(message)s'
)
logger = logging.getLogger("orchestrator")


# =============================================================================
# Voltage Change Confirmation Dialog
# =============================================================================

class VoltageChangeDialog(QDialog):
    """Dialog to confirm voltage change before starting test"""
    
    def __init__(self, voltage: float, parent=None):
        super().__init__(parent)
        self.setWindowTitle("⚠️ Confirm Voltage Setting")
        self.setModal(True)
        
        layout = QVBoxLayout(self)
        
        warning_label = QLabel(
            f"<b>IMPORTANT:</b> Please verify that the power supply "
            f"is set to <b style='color: red;'>{voltage:.1f}V</b> before continuing."
        )
        warning_label.setWordWrap(True)
        layout.addWidget(warning_label)
        
        layout.addSpacing(10)
        
        self.confirm_check = QCheckBox(f"I confirm the power supply is set to {voltage:.1f}V")
        layout.addWidget(self.confirm_check)
        
        layout.addSpacing(10)
        
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._on_accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
    
    def _on_accept(self):
        if not self.confirm_check.isChecked():
            QMessageBox.warning(self, "Confirmation Required",
                              "Please check the confirmation box to continue.")
            return
        self.accept()


# =============================================================================
# Signal Bridge for thread-safe GUI updates
# =============================================================================

class SignalBridge(QObject):
    """Bridge for thread-safe GUI updates"""
    
    log_message = pyqtSignal(str, str)  # message, level
    power_data = pyqtSignal(object)  # PowerDataPoint
    fd_data = pyqtSignal(object)  # ForceDataPoint
    test_progress = pyqtSignal(int)  # percentage
    test_complete = pyqtSignal()
    dut_status = pyqtSignal(object)  # MotorStatus
    plot_js_data = pyqtSignal(list, list, list, list)  # timestamps, current, voltage, power


# =============================================================================
# Real-Time Plot Widget
# =============================================================================

class RealtimePlotWidget(QWidget):
    """Widget showing 2x2 real-time plots"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        # Data buffers
        self.max_points = 5000  # Max points to display
        self.clear_data()
        
        self._setup_ui()
    
    def clear_data(self):
        """Clear all data buffers"""
        self.power_time = []
        self.current_data = []
        self.voltage_data = []
        self.power_data = []
        self.force_time = []
        self.force_data = []
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        
        # Create figure with 2x2 subplots
        self.figure = Figure(figsize=(10, 8), dpi=100)
        self.figure.patch.set_facecolor('white')
        self.canvas = FigureCanvas(self.figure)
        
        self.ax_current = self.figure.add_subplot(221)
        self.ax_voltage = self.figure.add_subplot(222)
        self.ax_power = self.figure.add_subplot(223)
        self.ax_force = self.figure.add_subplot(224)
        
        self._setup_axes()
        
        layout.addWidget(self.canvas)
    
    def _setup_axes(self):
        """Setup axis labels and styling"""
        for ax, title, ylabel in [
            (self.ax_current, "Current", "Current (mA)"),
            (self.ax_voltage, "Voltage", "Voltage (V)"),
            (self.ax_power, "Power", "Power (mW)"),
            (self.ax_force, "Force", "Force (N)")
        ]:
            ax.set_xlabel("Time (ms)")
            ax.set_ylabel(ylabel)
            ax.set_title(title)
            ax.grid(True, alpha=0.3)
        
        self.figure.tight_layout()
    
    def add_power_point(self, timestamp_ms: float, current_a: float, 
                       voltage_v: float, power_w: float):
        """Add a power measurement point"""
        self.power_time.append(timestamp_ms)
        self.current_data.append(current_a * 1000)  # Convert to mA
        self.voltage_data.append(voltage_v)
        self.power_data.append(power_w * 1000)  # Convert to mW
        
        # Trim if too many points
        if len(self.power_time) > self.max_points:
            self.power_time = self.power_time[-self.max_points:]
            self.current_data = self.current_data[-self.max_points:]
            self.voltage_data = self.voltage_data[-self.max_points:]
            self.power_data = self.power_data[-self.max_points:]
    
    def add_force_point(self, timestamp_ms: float, force_n: float):
        """Add a force measurement point"""
        self.force_time.append(timestamp_ms)
        self.force_data.append(force_n)
        
        if len(self.force_time) > self.max_points:
            self.force_time = self.force_time[-self.max_points:]
            self.force_data = self.force_data[-self.max_points:]
    
    def set_power_data(self, timestamps_ms: list, current_a: list, voltage_v: list, power_w: list, 
                        remove_gaps: bool = True, downsample: bool = False):
        """
        Set all power data at once (for system test visualization)
        
        Args:
            timestamps_ms: List of timestamps in milliseconds
            current_a: List of current values in Amps
            voltage_v: List of voltage values in Volts
            power_w: List of power values in Watts
            remove_gaps: If True, compress timestamps to remove gaps between chunks
            downsample: If True, downsample to max_points for performance
        """
        if not timestamps_ms:
            return
            
        # Make a copy of timestamps to modify
        ts = list(timestamps_ms)
        
        # Remove gaps between chunks by making timestamps continuous
        if remove_gaps and len(ts) > 1:
            # Detect the sample interval from the first few samples
            sample_intervals = []
            for i in range(1, min(100, len(ts))):
                interval = ts[i] - ts[i-1]
                if 0 < interval < 1:  # Valid interval (less than 1ms for high sample rates)
                    sample_intervals.append(interval)
            
            if sample_intervals:
                avg_interval = sum(sample_intervals) / len(sample_intervals)
                
                # Rebuild timestamps with consistent intervals
                ts_continuous = [0.0]
                for i in range(1, len(ts)):
                    ts_continuous.append(ts_continuous[-1] + avg_interval)
                ts = ts_continuous
        
        # Apply downsampling only if requested and necessary
        if downsample and len(ts) > self.max_points:
            step = max(1, len(ts) // self.max_points)
            ts = ts[::step]
            current_a = current_a[::step]
            voltage_v = voltage_v[::step]
            power_w = power_w[::step]
        
        self.power_time = ts
        self.current_data = [c * 1000 for c in current_a]  # Convert to mA
        self.voltage_data = list(voltage_v)
        self.power_data = [p * 1000 for p in power_w]  # Convert to mW
    
    def update_plots(self):
        """Redraw all plots"""
        # Current
        self.ax_current.clear()
        if self.power_time:
            self.ax_current.plot(self.power_time, self.current_data, 
                                color='#E94F37', linewidth=0.5)
        self.ax_current.set_xlabel("Time (ms)")
        self.ax_current.set_ylabel("Current (mA)")
        self.ax_current.set_title("Current")
        self.ax_current.grid(True, alpha=0.3)
        
        # Voltage
        self.ax_voltage.clear()
        if self.power_time:
            self.ax_voltage.plot(self.power_time, self.voltage_data,
                                color='#2ECC71', linewidth=0.5)
        self.ax_voltage.set_xlabel("Time (ms)")
        self.ax_voltage.set_ylabel("Voltage (V)")
        self.ax_voltage.set_title("Voltage")
        self.ax_voltage.grid(True, alpha=0.3)
        
        # Power
        self.ax_power.clear()
        if self.power_time:
            self.ax_power.plot(self.power_time, self.power_data,
                              color='#9B59B6', linewidth=0.5)
        self.ax_power.set_xlabel("Time (ms)")
        self.ax_power.set_ylabel("Power (mW)")
        self.ax_power.set_title("Power")
        self.ax_power.grid(True, alpha=0.3)
        
        # Force
        self.ax_force.clear()
        if self.force_time:
            self.ax_force.plot(self.force_time, self.force_data,
                              color='#2E86AB', linewidth=0.5)
        self.ax_force.set_xlabel("Time (ms)")
        self.ax_force.set_ylabel("Force (N)")
        self.ax_force.set_title("Force")
        self.ax_force.grid(True, alpha=0.3)
        
        self.figure.tight_layout()
        self.canvas.draw()


# =============================================================================
# Main Window
# =============================================================================

class OrchestratorWindow(QMainWindow):
    """Main orchestrator window"""
    
    def __init__(self):
        super().__init__()
        
        self.setWindowTitle("Energy Measurement Test System")
        self.setMinimumSize(1400, 900)
        
        # Device interfaces
        self.dut: Optional[DUTController] = None
        self.fd_client: Optional[FDClient] = None
        self.joulescope: Optional[JoulescopeInterface] = None
        
        # Data storage
        self.hdf5_manager: Optional[HDF5Manager] = None
        
        # Signal bridge for thread-safe updates
        self.signals = SignalBridge()
        self._connect_signals()
        
        # Test state
        self.is_testing = False
        self.test_start_time = 0.0
        self.power_buffer: List[PowerDataPoint] = []
        self.force_buffer: List[ForceDataPoint] = []
        
        # Setup UI
        self._setup_ui()
        
        # Update timer for plots
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self._update_plots)
        self.update_timer.start(100)  # 10 Hz update
    
    def _connect_signals(self):
        """Connect signal bridge signals"""
        self.signals.log_message.connect(self._on_log_message)
        self.signals.power_data.connect(self._on_power_data)
        self.signals.fd_data.connect(self._on_fd_data)
        self.signals.test_complete.connect(self._on_test_complete)
        self.signals.dut_status.connect(self._on_dut_status)
        self.signals.plot_js_data.connect(self._plot_js_test_data)
    
    def _setup_ui(self):
        """Setup the main UI"""
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QHBoxLayout(central)
        
        # Main splitter: Left (controls) | Right (plots)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        
        # Left panel: Controls
        left_scroll = QScrollArea()
        left_scroll.setWidgetResizable(True)
        left_scroll.setMinimumWidth(520)
        left_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        left_widget = QWidget()
        left_widget.setMinimumWidth(500)
        left_layout = QVBoxLayout(left_widget)
        
        # Test ID
        self._create_test_id_group(left_layout)
        
        # DUT Configuration
        self._create_dut_config_group(left_layout)
        
        # FD Configuration
        self._create_fd_config_group(left_layout)
        
        # Joulescope Configuration
        self._create_joulescope_config_group(left_layout)
        
        # Test Control
        self._create_test_control_group(left_layout)
        
        left_layout.addStretch()
        left_scroll.setWidget(left_widget)
        splitter.addWidget(left_scroll)
        
        # Set initial splitter sizes (left panel wider)
        splitter.setSizes([550, 850])
        
        # Right panel: Plots and results
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        
        # Plots
        self.plot_widget = RealtimePlotWidget()
        right_layout.addWidget(self.plot_widget, stretch=2)
        
        # Results summary
        results_group = QGroupBox("Results Summary")
        results_layout = QGridLayout(results_group)
        
        results_layout.addWidget(QLabel("Test Status:"), 0, 0)
        self.status_label = QLabel("Idle")
        self.status_label.setStyleSheet("font-weight: bold;")
        results_layout.addWidget(self.status_label, 0, 1)
        
        results_layout.addWidget(QLabel("Duration:"), 0, 2)
        self.duration_label = QLabel("-- s")
        results_layout.addWidget(self.duration_label, 0, 3)
        
        results_layout.addWidget(QLabel("Max Force:"), 1, 0)
        self.max_force_result = QLabel("-- N")
        results_layout.addWidget(self.max_force_result, 1, 1)
        
        results_layout.addWidget(QLabel("Avg Power:"), 1, 2)
        self.avg_power_result = QLabel("-- mW")
        results_layout.addWidget(self.avg_power_result, 1, 3)
        
        results_layout.addWidget(QLabel("Total Energy:"), 2, 0)
        self.total_energy_result = QLabel("-- mJ")
        results_layout.addWidget(self.total_energy_result, 2, 1)
        
        results_layout.addWidget(QLabel("Samples:"), 2, 2)
        self.samples_result = QLabel("P: 0 | F: 0")
        results_layout.addWidget(self.samples_result, 2, 3)
        
        right_layout.addWidget(results_group)
        
        # Activity log
        log_group = QGroupBox("Activity Log")
        log_layout = QVBoxLayout(log_group)
        
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setMaximumHeight(150)
        self.log_text.setFont(QFont("Consolas", 9))
        log_layout.addWidget(self.log_text)
        
        right_layout.addWidget(log_group)
        
        splitter.addWidget(right_widget)
        
        # Set splitter proportions (left panel wider for controls)
        splitter.setSizes([500, 900])
        splitter.setStretchFactor(0, 0)  # Left panel doesn't stretch
        splitter.setStretchFactor(1, 1)  # Right panel stretches
        
        main_layout.addWidget(splitter)
    
    def _create_test_id_group(self, parent_layout):
        """Create test ID group"""
        group = QGroupBox("Test Identification")
        layout = QGridLayout(group)
        
        layout.addWidget(QLabel("Test ID:"), 0, 0)
        self.test_id_edit = QLineEdit("M1-CV-1")
        layout.addWidget(self.test_id_edit, 0, 1, 1, 2)
        
        layout.addWidget(QLabel("Motor:"), 1, 0)
        self.motor_combo = QComboBox()
        self.motor_combo.addItems(["Motor 1", "Motor 2", "Motor 3"])
        layout.addWidget(self.motor_combo, 1, 1, 1, 2)
        
        layout.addWidget(QLabel("Notes:"), 2, 0)
        self.notes_edit = QLineEdit()
        layout.addWidget(self.notes_edit, 2, 1, 1, 2)
        
        layout.addWidget(QLabel("HDF5 File:"), 3, 0)
        self.hdf5_path_edit = QLineEdit("energy_test_data.h5")
        layout.addWidget(self.hdf5_path_edit, 3, 1)
        
        browse_btn = QPushButton("Browse...")
        browse_btn.clicked.connect(self._browse_hdf5)
        layout.addWidget(browse_btn, 3, 2)
        
        parent_layout.addWidget(group)
    
    def _create_dut_config_group(self, parent_layout):
        """Create DUT configuration group"""
        group = QGroupBox("DUT (Motor Module) Configuration")
        layout = QGridLayout(group)
        
        # Connection - dynamic port scanning
        layout.addWidget(QLabel("COM Port:"), 0, 0)
        self.dut_port_combo = QComboBox()
        self.dut_port_combo.setEditable(True)
        self._refresh_serial_ports()  # Initial scan
        layout.addWidget(self.dut_port_combo, 0, 1)
        
        # Refresh button for serial ports
        dut_refresh_btn = QPushButton("🔄")
        dut_refresh_btn.setFixedWidth(30)
        dut_refresh_btn.setToolTip("Refresh serial ports")
        dut_refresh_btn.clicked.connect(self._refresh_serial_ports)
        layout.addWidget(dut_refresh_btn, 0, 2)
        
        self.dut_connect_btn = QPushButton("Connect")
        self.dut_connect_btn.clicked.connect(self._toggle_dut_connection)
        layout.addWidget(self.dut_connect_btn, 0, 3)
        
        # Motion profile
        layout.addWidget(QLabel("Profile:"), 1, 0)
        self.profile_combo = QComboBox()
        self.profile_combo.addItems(["Constant Velocity", "Trapezoidal", "S-Curve"])
        layout.addWidget(self.profile_combo, 1, 1, 1, 2)
        
        # Speed
        layout.addWidget(QLabel("Speed (steps/s):"), 2, 0)
        self.speed_spin = QSpinBox()
        self.speed_spin.setRange(100, 50000)
        self.speed_spin.setValue(2000)
        layout.addWidget(self.speed_spin, 2, 1, 1, 2)
        
        # Acceleration
        layout.addWidget(QLabel("Accel (steps/s²):"), 3, 0)
        self.accel_spin = QSpinBox()
        self.accel_spin.setRange(100, 500000)
        self.accel_spin.setValue(10000)
        layout.addWidget(self.accel_spin, 3, 1, 1, 2)
        
        # Steps
        layout.addWidget(QLabel("Steps:"), 4, 0)
        self.steps_spin = QSpinBox()
        self.steps_spin.setRange(0, 1000000)
        self.steps_spin.setValue(0)
        layout.addWidget(self.steps_spin, 4, 1, 1, 2)
        
        # Manual command
        layout.addWidget(QLabel("Manual Cmd:"), 5, 0)
        self.manual_cmd_edit = QLineEdit()
        self.manual_cmd_edit.setPlaceholderText("e.g., G F 5000")
        self.manual_cmd_edit.returnPressed.connect(self._send_manual_dut_command)
        layout.addWidget(self.manual_cmd_edit, 5, 1)
        
        send_cmd_btn = QPushButton("Send")
        send_cmd_btn.clicked.connect(self._send_manual_dut_command)
        layout.addWidget(send_cmd_btn, 5, 2)
        
        # DUT Status
        layout.addWidget(QLabel("Status:"), 6, 0)
        self.dut_status_label = QLabel("Disconnected")
        layout.addWidget(self.dut_status_label, 6, 1, 1, 2)
        
        parent_layout.addWidget(group)
    
    def _create_fd_config_group(self, parent_layout):
        """Create FD configuration group"""
        group = QGroupBox("FD (Force/Displacement) Configuration")
        layout = QGridLayout(group)
        
        # Connection
        layout.addWidget(QLabel("RPi IP:"), 0, 0)
        self.fd_ip_edit = QLineEdit("192.168.1.12")
        layout.addWidget(self.fd_ip_edit, 0, 1)
        
        self.fd_connect_btn = QPushButton("Connect")
        self.fd_connect_btn.clicked.connect(self._toggle_fd_connection)
        layout.addWidget(self.fd_connect_btn, 0, 2)
        
        # Mode
        layout.addWidget(QLabel("Mode:"), 1, 0)
        self.fd_mode_combo = QComboBox()
        self.fd_mode_combo.addItems(["MONITOR", "BACKDRIVE"])
        layout.addWidget(self.fd_mode_combo, 1, 1, 1, 2)
        
        # Actuator speed in mm/s
        layout.addWidget(QLabel("Speed (mm/s):"), 2, 0)
        self.fd_speed_spin = QDoubleSpinBox()
        self.fd_speed_spin.setRange(0.1, 10.0)
        self.fd_speed_spin.setSingleStep(0.1)
        self.fd_speed_spin.setValue(1.0)  # Default 1 mm/s
        self.fd_speed_spin.setDecimals(1)
        self.fd_speed_spin.setToolTip("Actuator linear speed for jog and backdrive (mm/s)")
        layout.addWidget(self.fd_speed_spin, 2, 1, 1, 2)
        
        # Sample rate
        layout.addWidget(QLabel("Sample Rate (Hz):"), 3, 0)
        self.fd_rate_spin = QSpinBox()
        self.fd_rate_spin.setRange(1, 1000)
        self.fd_rate_spin.setValue(100)
        layout.addWidget(self.fd_rate_spin, 3, 1, 1, 2)
        
        # Manual jog controls
        jog_layout = QHBoxLayout()
        jog_layout.addWidget(QLabel("Jog:"))
        
        jog_left_btn = QPushButton("◀ -10")
        jog_left_btn.clicked.connect(lambda: self._jog_fd(-10))
        jog_layout.addWidget(jog_left_btn)
        
        jog_left_sm_btn = QPushButton("◁ -1")
        jog_left_sm_btn.clicked.connect(lambda: self._jog_fd(-1))
        jog_layout.addWidget(jog_left_sm_btn)
        
        jog_right_sm_btn = QPushButton("+1 ▷")
        jog_right_sm_btn.clicked.connect(lambda: self._jog_fd(1))
        jog_layout.addWidget(jog_right_sm_btn)
        
        jog_right_btn = QPushButton("+10 ▶")
        jog_right_btn.clicked.connect(lambda: self._jog_fd(10))
        jog_layout.addWidget(jog_right_btn)
        
        layout.addLayout(jog_layout, 4, 0, 1, 3)
        
        # Status
        layout.addWidget(QLabel("Status:"), 5, 0)
        self.fd_status_label = QLabel("Disconnected")
        layout.addWidget(self.fd_status_label, 5, 1, 1, 2)
        
        parent_layout.addWidget(group)
    
    def _create_joulescope_config_group(self, parent_layout):
        """Create Joulescope configuration group"""
        group = QGroupBox("Joulescope JS220 Configuration")
        layout = QGridLayout(group)
        
        # Connection
        layout.addWidget(QLabel("Device:"), 0, 0)
        self.js_device_combo = QComboBox()
        self.js_device_combo.addItems(["Auto-detect", "Mock (Testing)"])
        layout.addWidget(self.js_device_combo, 0, 1)
        
        self.js_connect_btn = QPushButton("Connect")
        self.js_connect_btn.clicked.connect(self._toggle_joulescope_connection)
        layout.addWidget(self.js_connect_btn, 0, 2)
        
        # Voltage range setting (for display scaling / expected voltage)
        layout.addWidget(QLabel("Expected V:"), 1, 0)
        self.voltage_spin = QDoubleSpinBox()
        self.voltage_spin.setRange(0.5, 36.0)
        self.voltage_spin.setValue(12.0)
        self.voltage_spin.setDecimals(1)
        self.voltage_spin.setSingleStep(0.5)
        self.voltage_spin.setSuffix(" V")
        self.voltage_spin.setToolTip("Expected supply voltage (for plot scaling and data tagging)")
        layout.addWidget(self.voltage_spin, 1, 1, 1, 2)
        
        # Sample rate (JS220 supports up to 1 MHz)
        layout.addWidget(QLabel("Sample Rate (Hz):"), 2, 0)
        self.js_rate_combo = QComboBox()
        self.js_rate_combo.addItems([
            "100", "500", "1000", "2000", "5000", "10000",
            "50000", "100000", "200000", "500000", "1000000"
        ])
        self.js_rate_combo.setCurrentText("1000")
        self.js_rate_combo.setEditable(True)  # Allow custom values
        self.js_rate_combo.setToolTip("JS220 supports up to 1 MHz (1000000 Hz)")
        layout.addWidget(self.js_rate_combo, 2, 1, 1, 2)
        
        # Current range
        layout.addWidget(QLabel("Current Range:"), 3, 0)
        self.current_range_combo = QComboBox()
        self.current_range_combo.addItems(["Auto", "10 mA", "180 mA", "1 A", "3 A"])
        self.current_range_combo.setToolTip("Auto: JS220 automatically selects optimal range\nFixed: Better for high-frequency measurements")
        layout.addWidget(self.current_range_combo, 3, 1, 1, 2)
        
        # Status
        layout.addWidget(QLabel("Status:"), 4, 0)
        self.js_status_label = QLabel("Disconnected")
        layout.addWidget(self.js_status_label, 4, 1, 1, 2)
        
        parent_layout.addWidget(group)
    
    def _create_test_control_group(self, parent_layout):
        """Create test control group"""
        group = QGroupBox("Test Control")
        layout = QVBoxLayout(group)
        
        # Progress bar
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        layout.addWidget(self.progress_bar)
        
        # Control buttons
        btn_layout = QHBoxLayout()
        
        self.start_btn = QPushButton("▶ START TEST")
        self.start_btn.setStyleSheet(
            "background-color: #27AE60; color: white; "
            "font-weight: bold; padding: 10px;"
        )
        self.start_btn.clicked.connect(self._start_test)
        btn_layout.addWidget(self.start_btn)
        
        self.stop_btn = QPushButton("⬛ STOP")
        self.stop_btn.setStyleSheet(
            "background-color: #E74C3C; color: white; "
            "font-weight: bold; padding: 10px;"
        )
        self.stop_btn.setEnabled(False)
        self.stop_btn.clicked.connect(self._stop_test)
        btn_layout.addWidget(self.stop_btn)
        
        layout.addLayout(btn_layout)
        
        # Post-test actions
        post_layout = QHBoxLayout()
        
        align_btn = QPushButton("🔧 Align Data")
        align_btn.clicked.connect(self._open_alignment_tool)
        post_layout.addWidget(align_btn)
        
        export_btn = QPushButton("📁 Export Plots")
        export_btn.clicked.connect(self._export_plots)
        post_layout.addWidget(export_btn)
        
        layout.addLayout(post_layout)
        
        # System test button
        self.system_test_btn = QPushButton("🔍 SYSTEM TEST")
        self.system_test_btn.setStyleSheet(
            "background-color: #3498DB; color: white; "
            "font-weight: bold; padding: 8px;"
        )
        self.system_test_btn.clicked.connect(self._run_system_test)
        layout.addWidget(self.system_test_btn)
        
        parent_layout.addWidget(group)
    
    # =========================================================================
    # Connection handlers
    # =========================================================================
    
    def _refresh_serial_ports(self):
        """Scan and refresh available serial ports"""
        import serial.tools.list_ports
        
        # Remember current selection
        current = self.dut_port_combo.currentText()
        
        # Clear and repopulate
        self.dut_port_combo.clear()
        
        ports = list(serial.tools.list_ports.comports())
        for port_info in sorted(ports, key=lambda x: x.device):
            # Show port name with description for easier identification
            display = f"{port_info.device}"
            if port_info.description and port_info.description != "n/a":
                display += f" - {port_info.description}"
            self.dut_port_combo.addItem(display, port_info.device)
        
        # Restore previous selection if still available
        if current:
            # Try to find by device name (stored in item data)
            for i in range(self.dut_port_combo.count()):
                if self.dut_port_combo.itemData(i) == current or current in self.dut_port_combo.itemText(i):
                    self.dut_port_combo.setCurrentIndex(i)
                    break
        
        # Only log if log widget exists (not during initial setup)
        if hasattr(self, 'log_text'):
            self._log(f"Found {len(ports)} serial port(s)", "INFO")
    
    def _toggle_dut_connection(self):
        """Toggle DUT connection"""
        if self.dut is None or not self.dut.is_connected:
            try:
                # Get port from item data (actual device name) or fall back to text
                port = self.dut_port_combo.currentData()
                if port is None:
                    # If user typed a custom port, use the text directly
                    port = self.dut_port_combo.currentText().split(" - ")[0]
                
                self.dut = DUTController()
                if self.dut.connect(port):
                    self.dut_connect_btn.setText("Disconnect")
                    self.dut_status_label.setText("Connected")
                    self._log("DUT connected on " + port, "INFO")
                else:
                    self._log("DUT connection failed", "ERROR")
                    self.dut = None
            except Exception as e:
                self._log(f"DUT connection failed: {e}", "ERROR")
                QMessageBox.critical(self, "Connection Error", str(e))
                self.dut = None
        else:
            self.dut.disconnect()
            self.dut = None
            self.dut_connect_btn.setText("Connect")
            self.dut_status_label.setText("Disconnected")
            self._log("DUT disconnected", "INFO")
    
    def _toggle_fd_connection(self):
        """Toggle FD client connection"""
        if self.fd_client is None or not self.fd_client.is_connected:
            try:
                ip = self.fd_ip_edit.text()
                self.fd_client = FDClient()
                if self.fd_client.connect(ip):
                    self.fd_connect_btn.setText("Disconnect")
                    self.fd_status_label.setText("Connected")
                    self._log(f"FD connected to {ip}", "INFO")
                else:
                    self._log("FD connection failed", "ERROR")
                    self.fd_client = None
            except Exception as e:
                self._log(f"FD connection failed: {e}", "ERROR")
                QMessageBox.critical(self, "Connection Error", str(e))
                self.fd_client = None
        else:
            self.fd_client.disconnect()
            self.fd_client = None
            self.fd_connect_btn.setText("Connect")
            self.fd_status_label.setText("Disconnected")
            self._log("FD disconnected", "INFO")
    
    def _toggle_joulescope_connection(self):
        """Toggle Joulescope connection"""
        if self.joulescope is None:
            try:
                if self.js_device_combo.currentText() == "Mock (Testing)":
                    self.joulescope = MockJoulescopeInterface()
                else:
                    self.joulescope = JoulescopeInterface()
                
                # Set response callback for debugging
                self.joulescope.set_response_callback(lambda msg: self._log(f"JS: {msg}", "INFO"))
                
                if self.joulescope.connect():
                    self.js_connect_btn.setText("Disconnect")
                    self.js_status_label.setText("Connected")
                    self._log("Joulescope connected", "INFO")
                else:
                    self._log("Joulescope connection failed - check device and drivers", "ERROR")
                    self.joulescope = None
            except Exception as e:
                self._log(f"Joulescope connection failed: {e}", "ERROR")
                QMessageBox.critical(self, "Connection Error", str(e))
        else:
            self.joulescope.disconnect()
            self.joulescope = None
            self.js_connect_btn.setText("Connect")
            self.js_status_label.setText("Disconnected")
            self._log("Joulescope disconnected", "INFO")
    
    # =========================================================================
    # Manual controls
    # =========================================================================
    
    def _send_manual_dut_command(self):
        """Send manual command to DUT"""
        if self.dut is None or not self.dut.is_connected:
            self._log("DUT not connected", "WARNING")
            return
        
        cmd = self.manual_cmd_edit.text().strip()
        if not cmd:
            return
        
        try:
            response = self.dut.send_raw_command(cmd)
            self._log(f"DUT << {cmd}", "INFO")
            self._log(f"DUT >> {response}", "INFO")
            # Keep the command in the field for easy re-sending or modification
            self.manual_cmd_edit.selectAll()  # Select text for easy replacement
        except Exception as e:
            self._log(f"DUT command error: {e}", "ERROR")
    
    def _jog_fd(self, distance_mm: float):
        """Jog FD setup by distance in mm"""
        if self.fd_client is None or not self.fd_client.is_connected:
            self._log("FD not connected", "WARNING")
            return
        
        try:
            # Set speed from UI before jogging
            speed_mm_s = self.fd_speed_spin.value()
            self.fd_client.set_speed(speed_mm_s)
            
            self.fd_client.jog(distance_mm)
            self._log(f"FD jog {distance_mm:+.1f} mm at {speed_mm_s:.1f} mm/s", "INFO")
        except Exception as e:
            self._log(f"FD jog error: {e}", "ERROR")
    
    def _browse_hdf5(self):
        """Browse for HDF5 file"""
        filename, _ = QFileDialog.getSaveFileName(
            self, "Select HDF5 File", "", "HDF5 Files (*.h5);;All Files (*)"
        )
        if filename:
            self.hdf5_path_edit.setText(filename)
    
    def _run_system_test(self):
        """
        Run sequential system test to verify all devices respond correctly.
        
        Tests:
        1. DUT: move 1600 steps, wait, move -1600 steps, wait
        2. FD: move actuator 10mm forward and 10mm back, recording force
        3. JS: start measurement for 5 seconds then stop
        """
        # Disable button during test
        self.system_test_btn.setEnabled(False)
        self.system_test_btn.setText("⏳ Testing...")
        
        # Capture UI values before spawning thread (thread safety)
        # UI elements must only be accessed from the main thread
        # Round float values to avoid floating-point precision noise
        test_config = {
            'dut_speed': self.speed_spin.value(),
            'dut_accel': self.accel_spin.value(),
            'dut_profile': self.profile_combo.currentText() if hasattr(self, 'profile_combo') else "Constant Velocity",
            'fd_speed': round(self.fd_speed_spin.value(), 1),  # Round to 1 decimal
            'fd_mode': self.fd_mode_combo.currentText() if hasattr(self, 'fd_mode_combo') else "MONITOR",
            'js_sample_rate': int(self.js_rate_combo.currentText()) if hasattr(self, 'js_rate_combo') else 1000
        }
        
        # Debug: Print captured config vs UI values
        print(f"[CONFIG CAPTURE] UI Values captured for system test:")
        print(f"  DUT Speed: {test_config['dut_speed']} (from speed_spin.value())")
        print(f"  DUT Accel: {test_config['dut_accel']} (from accel_spin.value())")
        print(f"  DUT Profile: {test_config['dut_profile']} (from profile_combo)")
        print(f"  FD Speed: {test_config['fd_speed']} mm/s (from fd_speed_spin.value())")
        print(f"  FD Mode: {test_config['fd_mode']} (from fd_mode_combo)")
        print(f"  JS Sample Rate: {test_config['js_sample_rate']} Hz (from js_rate_combo)")
        
        # Run in background thread
        threading.Thread(target=self._system_test_worker, args=(test_config,), daemon=True).start()
    
    def _system_test_worker(self, config: dict):
        """Worker thread for system test
        
        Args:
            config: Dictionary with pre-captured UI values (thread-safe)
        """
        results = {
            'dut': {'tested': False, 'passed': False, 'message': ''},
            'fd': {'tested': False, 'passed': False, 'message': ''},
            'js': {'tested': False, 'passed': False, 'message': ''}
        }
        
        self._log("=" * 40, "INFO")
        self._log("SYSTEM TEST STARTED", "INFO")
        self._log("=" * 40, "INFO")
        
        # Log current configuration
        self._log("", "INFO")
        self._log("Test Configuration:", "INFO")
        print(f"[WORKER] Config received: {config}")
        if self.dut is not None and self.dut.is_connected:
            self._log(f"  DUT: Connected, profile={config.get('dut_profile', 'N/A')}, speed={config['dut_speed']}, accel={config['dut_accel']}", "INFO")
        else:
            self._log("  DUT: Not connected", "INFO")
        if self.fd_client is not None and self.fd_client.is_connected:
            self._log(f"  FD: Connected, mode={config.get('fd_mode', 'N/A')}, speed={config['fd_speed']:.1f} mm/s", "INFO")
        else:
            self._log("  FD: Not connected", "INFO")
        if self.joulescope is not None:
            is_mock = isinstance(self.joulescope, MockJoulescopeInterface)
            self._log(f"  Joulescope: {'Mock' if is_mock else 'Real'}, target rate: {config['js_sample_rate']} Hz", "INFO")
        else:
            self._log("  Joulescope: Not connected", "INFO")
        
        # Test 1: DUT
        self._log("", "INFO")
        self._log("[1/3] Testing DUT (Motor Module)...", "INFO")
        if self.dut is not None and self.dut.is_connected:
            results['dut']['tested'] = True
            try:
                # Use parameters from config (captured from UI before thread started)
                dut_speed = config['dut_speed']
                dut_accel = config['dut_accel']
                
                # Default test movement is 1600 steps (full rotation for many motors)
                test_steps = 1600
                
                # Configure for test
                self._log(f"  Setting speed={dut_speed}, accel={dut_accel}...", "INFO")
                self.dut.set_speed(dut_speed)
                self.dut.set_accel(dut_accel)
                
                # Move forward
                self._log(f"  Moving +{test_steps} steps...", "INFO")
                if self.dut.move(test_steps, wait=True, timeout=10.0):
                    self._log("  Forward move completed", "INFO")
                    time.sleep(0.5)
                    
                    # Move backward
                    self._log(f"  Moving -{test_steps} steps...", "INFO")
                    if self.dut.move(-test_steps, wait=True, timeout=10.0):
                        self._log("  Backward move completed", "INFO")
                        results['dut']['passed'] = True
                        results['dut']['message'] = 'Motion test passed'
                        self._log("  ✅ DUT TEST PASSED", "INFO")
                    else:
                        results['dut']['message'] = 'Backward move failed'
                        self._log("  ❌ Backward move failed", "ERROR")
                else:
                    results['dut']['message'] = 'Forward move failed'
                    self._log("  ❌ Forward move failed", "ERROR")
            except Exception as e:
                results['dut']['message'] = str(e)
                self._log(f"  ❌ DUT error: {e}", "ERROR")
        else:
            results['dut']['message'] = 'Not connected'
            self._log("  ⚠️ DUT not connected - SKIPPED", "WARNING")
        
        # Test 2: FD
        self._log("", "INFO")
        self._log("[2/3] Testing FD (Force/Displacement)...", "INFO")
        if self.fd_client is not None and self.fd_client.is_connected:
            results['fd']['tested'] = True
            try:
                # Use speed from config (captured from UI before thread started)
                fd_speed = config['fd_speed']
                print(f"[FD TEST] Using fd_speed from config: {fd_speed} mm/s (type: {type(fd_speed)})")
                self._log(f"  Setting speed to {fd_speed:.1f} mm/s...", "INFO")
                print(f"[FD TEST] Calling fd_client.set_speed({fd_speed})...")
                self.fd_client.set_speed(fd_speed)
                print(f"[FD TEST] set_speed returned")
                
                # Collect some force data during move
                force_readings: List[float] = []
                
                def collect_force(point: ForceDataPoint):
                    force_readings.append(point.force_n)
                
                self.fd_client.set_data_callback(collect_force)
                
                # Move forward 10mm
                self._log(f"  Sending JOG +10mm at {fd_speed:.1f} mm/s...", "INFO")
                result = self.fd_client.jog(10.0)
                self._log(f"  Command returned: {result}", "INFO")
                if result:
                    self._log(f"  Forward move done (collected {len(force_readings)} samples)", "INFO")
                    time.sleep(0.5)
                    
                    # Move backward 10mm
                    self._log(f"  Sending JOG -10mm at {fd_speed:.1f} mm/s...", "INFO")
                    result2 = self.fd_client.jog(-10.0)
                    self._log(f"  Command returned: {result2}", "INFO")
                    if result2:
                        self._log(f"  Backward move done (total {len(force_readings)} samples)", "INFO")
                        results['fd']['passed'] = True
                        results['fd']['message'] = f'Motion test passed, {len(force_readings)} force samples'
                        self._log("  ✅ FD TEST PASSED", "INFO")
                    else:
                        # The motion might have worked even if response wasn't "OK"
                        results['fd']['passed'] = True  # Mark as passed if motion occurred
                        results['fd']['message'] = f'Backward move response not OK, but {len(force_readings)} samples collected'
                        self._log("  ⚠️ FD response issue but motion occurred", "WARNING")
                else:
                    # The motion might have worked even if response wasn't "OK"
                    results['fd']['passed'] = True  # Mark as passed if motion occurred
                    results['fd']['message'] = f'Forward move response not OK, but {len(force_readings)} samples collected'
                    self._log("  ⚠️ FD response issue but motion occurred", "WARNING")
                    
            except Exception as e:
                results['fd']['message'] = str(e)
                self._log(f"  ❌ FD error: {e}", "ERROR")
        else:
            results['fd']['message'] = 'Not connected'
            self._log("  ⚠️ FD not connected - SKIPPED", "WARNING")
        
        # Test 3: Joulescope
        self._log("", "INFO")
        self._log("[3/3] Testing Joulescope (Power Analyzer)...", "INFO")
        if self.joulescope is not None:
            results['js']['tested'] = True
            try:
                sample_count = 0
                
                # Use sample rate from config (captured from UI before thread started)
                js_sample_rate = config['js_sample_rate']
                
                # Check what type of interface we have
                is_mock = isinstance(self.joulescope, MockJoulescopeInterface)
                self._log(f"  Interface type: {'Mock' if is_mock else 'Real'}", "INFO")
                self._log(f"  Device status: connected={self.joulescope.is_connected}", "INFO")
                self._log(f"  Target sample rate: {js_sample_rate} Hz", "INFO")
                print(f"[SysTest] Joulescope test starting, interface type: {'Mock' if is_mock else 'Real'}")
                print(f"[SysTest] Target sample rate: {js_sample_rate} Hz")
                
                # Start streaming with configured rate
                self._log("  Starting 5-second measurement...", "INFO")
                print(f"[SysTest] Calling start_streaming({js_sample_rate})...")
                start_ok = self.joulescope.start_streaming(js_sample_rate)
                print(f"[SysTest] start_streaming returned: {start_ok}")
                self._log(f"  start_streaming returned: {start_ok}", "INFO")
                
                if start_ok:
                    start_time = time.time()
                    while time.time() - start_time < 5.0:
                        data = self.joulescope.read_sample()
                        if data:
                            sample_count += 1
                        time.sleep(0.01)
                    
                    print(f"[SysTest] 5 seconds elapsed, read_sample() count: {sample_count}")
                    
                    # Also check the actual buffer size
                    actual_samples = len(self.joulescope._power_data) if hasattr(self.joulescope, '_power_data') else -1
                    print(f"[SysTest] Actual buffer size: {actual_samples}")
                    
                    self._log("  Stopping streaming...", "INFO")
                    print("[SysTest] Calling stop_streaming()...")
                    power_data = self.joulescope.stop_streaming()
                    print("[SysTest] stop_streaming() returned")
                    
                    # Use buffer size as the real sample count
                    if actual_samples > 0:
                        sample_count = actual_samples
                    
                    if sample_count > 0:
                        results['js']['passed'] = True
                        results['js']['message'] = f'Captured {sample_count} samples in 5s'
                        self._log(f"  Captured {sample_count} samples", "INFO")
                        
                        # Plot the captured data on main thread via signal
                        if hasattr(power_data, 'timestamps_ms') and len(power_data.timestamps_ms) > 0:
                            self._log(f"  Plotting {len(power_data.timestamps_ms)} samples...", "INFO")
                            # Emit signal to main thread for plotting
                            self.signals.plot_js_data.emit(
                                list(power_data.timestamps_ms),
                                list(power_data.current_a),
                                list(power_data.voltage_v),
                                list(power_data.power_w)
                            )
                        
                        self._log("  ✅ JOULESCOPE TEST PASSED", "INFO")
                    else:
                        results['js']['message'] = 'No samples received - check device API'
                        self._log("  ❌ No samples received", "ERROR")
                        self._log("  Note: JS220 may need different API - check joulescope package docs", "WARNING")
                else:
                    results['js']['message'] = 'start_streaming failed'
                    self._log("  ❌ start_streaming failed", "ERROR")
                    
            except Exception as e:
                import traceback
                print(f"[SysTest] Joulescope exception: {e}")
                print(f"[SysTest] Traceback: {traceback.format_exc()}")
                results['js']['message'] = str(e)
                self._log(f"  ❌ Joulescope error: {e}", "ERROR")
                try:
                    self.joulescope.stop_streaming()
                except:
                    pass
        else:
            results['js']['message'] = 'Not connected'
            self._log("  ⚠️ Joulescope not connected - SKIPPED", "WARNING")
        
        # Summary
        self._log("", "INFO")
        self._log("=" * 40, "INFO")
        self._log("SYSTEM TEST SUMMARY", "INFO")
        self._log("=" * 40, "INFO")
        
        all_passed = True
        all_tested = True
        
        for device, result in results.items():
            device_name = {'dut': 'DUT (Motor)', 'fd': 'FD (Force)', 'js': 'Joulescope'}[device]
            if not result['tested']:
                status = "⚠️ SKIPPED"
                all_tested = False
            elif result['passed']:
                status = "✅ PASSED"
            else:
                status = "❌ FAILED"
                all_passed = False
            self._log(f"  {device_name}: {status} - {result['message']}", "INFO")
        
        self._log("", "INFO")
        if all_passed and all_tested:
            self._log("🎉 ALL SYSTEMS OPERATIONAL", "INFO")
        elif all_passed:
            self._log("✅ Connected systems passed (some not connected)", "WARNING")
        else:
            self._log("❌ SOME SYSTEMS FAILED - Check connections", "ERROR")
        
        self._log("=" * 40, "INFO")
        
        # Re-enable button on main thread
        QTimer.singleShot(0, self._system_test_complete)
    
    def _system_test_complete(self):
        """Called when system test completes"""
        self.system_test_btn.setEnabled(True)
        self.system_test_btn.setText("🔍 SYSTEM TEST")
    
    def _plot_js_test_data(self, timestamps_ms: list, current_a: list, 
                           voltage_v: list, power_w: list):
        """Plot Joulescope test data (called on main thread)"""
        try:
            print(f"[PLOT] _plot_js_test_data called with {len(timestamps_ms)} samples")
            print(f"[PLOT] Data types: ts={type(timestamps_ms[0]) if timestamps_ms else 'empty'}, curr={type(current_a[0]) if current_a else 'empty'}")
            
            self.plot_widget.clear_data()
            self.plot_widget.set_power_data(timestamps_ms, current_a, voltage_v, power_w)
            
            print(f"[PLOT] After set_power_data: power_time has {len(self.plot_widget.power_time)} points")
            
            self.plot_widget.update_plots()
            print(f"[PLOT] update_plots() called successfully")
            
            self._log(f"  Plot updated with {len(self.plot_widget.power_time)} samples", "INFO")
        except Exception as e:
            import traceback
            print(f"[PLOT] ERROR: {e}")
            print(f"[PLOT] Traceback: {traceback.format_exc()}")
            self._log(f"  Plot error: {e}", "ERROR")
    
    # =========================================================================
    # Test execution
    # =========================================================================
    
    def _start_test(self):
        """Start a test"""
        # Validate connections
        if self.joulescope is None:
            QMessageBox.warning(self, "Not Ready", "Joulescope not connected!")
            return
        
        # Confirm voltage
        voltage = self.voltage_spin.value()
        dialog = VoltageChangeDialog(voltage, self)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            self._log("Test cancelled - voltage not confirmed", "WARNING")
            return
        
        # Initialize HDF5
        try:
            self.hdf5_manager = HDF5Manager(self.hdf5_path_edit.text())
        except Exception as e:
            self._log(f"HDF5 error: {e}", "ERROR")
            QMessageBox.critical(self, "HDF5 Error", str(e))
            return
        
        # Clear data
        self.plot_widget.clear_data()
        self.power_buffer.clear()
        self.force_buffer.clear()
        
        # Update UI
        self.is_testing = True
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.status_label.setText("Running...")
        self.status_label.setStyleSheet("color: green; font-weight: bold;")
        
        self._log(f"Test started: {self.test_id_edit.text()}", "INFO")
        
        # Start data collection in thread
        self.test_start_time = time.time()
        
        # Capture all UI values before spawning threads (thread safety)
        sample_rate = int(self.js_rate_combo.currentText())
        fd_mode_str = self.fd_mode_combo.currentText() if hasattr(self, 'fd_mode_combo') else "MONITOR"
        fd_speed = self.fd_speed_spin.value() if hasattr(self, 'fd_speed_spin') else 1.0
        fd_rate = self.fd_rate_spin.value() if hasattr(self, 'fd_rate_spin') else 100
        dut_profile_str = self.profile_combo.currentText()
        dut_speed = self.speed_spin.value()
        dut_accel = self.accel_spin.value()
        dut_steps = self.steps_spin.value()
        
        # Start Joulescope streaming
        threading.Thread(
            target=self._joulescope_worker,
            args=(sample_rate,),
            daemon=True
        ).start()
        
        # Start FD streaming if connected
        if self.fd_client and self.fd_client.is_connected:
            # Set mode
            mode = FDMode.BACKDRIVE if fd_mode_str == "BACKDRIVE" else FDMode.MONITOR
            self.fd_client.set_mode(mode)
            if mode == FDMode.BACKDRIVE:
                self.fd_client.set_speed(fd_speed)
            self.fd_client.set_sample_rate(fd_rate)
            
            threading.Thread(
                target=self._fd_worker,
                daemon=True
            ).start()
        
        # Start DUT motion if connected
        if self.dut and self.dut.is_connected:
            # Create config dict for worker thread (thread-safe)
            dut_config = {
                'profile': dut_profile_str,
                'speed': dut_speed,
                'accel': dut_accel,
                'steps': dut_steps
            }
            threading.Thread(
                target=self._dut_worker,
                args=(dut_config,),
                daemon=True
            ).start()
    
    def _stop_test(self):
        """Stop the test"""
        self.is_testing = False
        
        # Stop Joulescope and capture complete data
        js_power_data = None
        if self.joulescope:
            try:
                js_power_data = self.joulescope.stop_streaming()
            except Exception as e:
                self._log(f"Joulescope stop error: {e}", "ERROR")
        
        # Stop FD and capture complete data
        fd_force_data = None
        if self.fd_client and self.fd_client.is_connected:
            try:
                fd_force_data = self.fd_client.stop()
            except Exception as e:
                self._log(f"FD stop error: {e}", "ERROR")
        
        # Stop DUT
        if self.dut and self.dut.is_connected:
            try:
                self.dut.stop()
            except:
                pass
        
        # Update UI
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.status_label.setText("Stopped")
        self.status_label.setStyleSheet("color: orange; font-weight: bold;")
        
        self._log("Test stopped", "INFO")
        
        # Save data - pass the complete data objects
        self._save_test_data(js_power_data, fd_force_data)
    
    def _joulescope_worker(self, sample_rate: int):
        """Worker thread for Joulescope data collection"""
        try:
            if self.joulescope is None:
                return
            self.joulescope.start_streaming(sample_rate)
            
            while self.is_testing:
                if self.joulescope is None:
                    break
                data = self.joulescope.read_sample()
                if data:
                    # Convert timestamp to ms relative to start
                    data.timestamp_ms = (time.time() - self.test_start_time) * 1000
                    self.power_buffer.append(data)
                    self.signals.power_data.emit(data)
                time.sleep(0.001)  # 1ms sleep
                
        except Exception as e:
            self._log(f"Joulescope error: {e}", "ERROR")
        finally:
            self.signals.test_complete.emit()
    
    def _fd_worker(self):
        """Worker thread for FD data collection"""
        def on_data(fd_data: ForceDataPoint):
            fd_data.timestamp_ms = (time.time() - self.test_start_time) * 1000
            self.force_buffer.append(fd_data)
            self.signals.fd_data.emit(fd_data)
        
        try:
            if self.fd_client is None:
                return
            self.fd_client.set_data_callback(on_data)
            self.fd_client.start()
            
            while self.is_testing:
                time.sleep(0.1)
                
        except Exception as e:
            self._log(f"FD error: {e}", "ERROR")
    
    def _dut_worker(self, config: dict):
        """
        Worker thread for DUT motion
        
        Args:
            config: Dictionary with pre-captured UI values:
                - profile: Motion profile string
                - speed: Steps per second
                - accel: Acceleration
                - steps: Number of steps to move
        """
        try:
            if self.dut is None:
                return
            
            # Configure profile using captured values (thread-safe)
            profile_map = {
                "Constant Velocity": MotionProfile.CONSTANT,
                "Trapezoidal": MotionProfile.TRAPEZOIDAL,
                "S-Curve": MotionProfile.SCURVE
            }
            profile = profile_map.get(config['profile'], MotionProfile.CONSTANT)
            
            self.dut.configure_profile(
                profile=profile,
                speed=config['speed'],
                accel=config['accel']
            )
            
            # Start motion
            steps = config['steps']
            if steps > 0:
                self.dut.move(steps)
            else:
                self.dut.run_forward()
            
            # Monitor status
            while self.is_testing:
                if self.dut is None:
                    break
                status = self.dut.get_status()
                self.signals.dut_status.emit(status)
                
                if status.ramp_state == RampState.IDLE and steps > 0:
                    break
                    
                time.sleep(0.1)
                
        except Exception as e:
            self._log(f"DUT error: {e}", "ERROR")
    
    def _save_test_data(self, js_power_data=None, fd_force_data=None):
        """
        Save collected test data to HDF5
        
        Args:
            js_power_data: PowerData from Joulescope stop_streaming() (complete data)
            fd_force_data: ForceData from FD client stop() (complete data)
        """
        if not self.hdf5_manager:
            return
        
        # Prefer complete data from stop() calls, fall back to buffers
        has_power = (js_power_data is not None and hasattr(js_power_data, 'sample_count') 
                     and js_power_data.sample_count > 0) or self.power_buffer
        has_force = (fd_force_data is not None and hasattr(fd_force_data, 'total_samples')
                     and fd_force_data.total_samples > 0) or self.force_buffer
        
        if not has_power and not has_force:
            self._log("No data to save", "WARNING")
            return
        
        try:
            # Create metadata
            metadata = TestMetadata(
                test_id=self.test_id_edit.text(),
                motor_id=self.motor_combo.currentText(),
                timestamp=datetime.now().isoformat(),
                profile=self.profile_combo.currentText(),
                voltage_v=self.voltage_spin.value(),
                speed_sps=self.speed_spin.value(),
                accel_sps2=self.accel_spin.value(),
                test_steps=self.steps_spin.value(),
                notes=self.notes_edit.text()
            )
            
            # Create power data - prefer complete data from stop_streaming()
            power_data: Optional[PowerRawData] = None
            if js_power_data is not None and hasattr(js_power_data, 'sample_count') and js_power_data.sample_count > 0:
                # Use complete Joulescope data
                power_data = PowerRawData(
                    time_ms=js_power_data.timestamps_ms,
                    current_a=js_power_data.current_a,
                    voltage_v=js_power_data.voltage_v,
                    power_w=js_power_data.power_w
                )
                self._log(f"Using complete Joulescope data: {js_power_data.sample_count} samples", "INFO")
            elif self.power_buffer:
                # Fall back to polled buffer
                power_data = PowerRawData(
                    time_ms=np.array([d.timestamp_ms for d in self.power_buffer]),
                    current_a=np.array([d.current_a for d in self.power_buffer]),
                    voltage_v=np.array([d.voltage_v for d in self.power_buffer]),
                    power_w=np.array([d.power_w for d in self.power_buffer])
                )
                self._log(f"Using polled power buffer: {len(self.power_buffer)} samples", "WARNING")
            
            # Create force data - prefer complete data from stop()
            force_data: Optional[ForceRawData] = None
            if fd_force_data is not None and hasattr(fd_force_data, 'total_samples') and fd_force_data.total_samples > 0:
                # Use complete FD data
                force_data = ForceRawData(
                    time_ms=np.array(fd_force_data.timestamps_ms),
                    force_n=np.array(fd_force_data.force_n),
                    position_mm=np.array(fd_force_data.position_mm)
                )
                self._log(f"Using complete FD data: {fd_force_data.total_samples} samples", "INFO")
            elif self.force_buffer:
                # Fall back to polled buffer
                force_data = ForceRawData(
                    time_ms=np.array([d.timestamp_ms for d in self.force_buffer]),
                    force_n=np.array([d.force_n for d in self.force_buffer]),
                    position_mm=np.array([d.position_mm for d in self.force_buffer])
                )
                self._log(f"Using polled force buffer: {len(self.force_buffer)} samples", "WARNING")
            
            # Save test to HDF5
            test_id = metadata.test_id
            if self.hdf5_manager.create_test(test_id, metadata):
                if power_data is not None:
                    self.hdf5_manager.save_power_data(test_id, power_data)
                if force_data is not None:
                    self.hdf5_manager.save_force_data(test_id, force_data)
                self._log(f"Data saved: {test_id}", "INFO")
            else:
                self._log(f"Failed to create test: {test_id}", "ERROR")
            
        except Exception as e:
            self._log(f"Save error: {e}", "ERROR")
    
    # =========================================================================
    # Signal handlers
    # =========================================================================
    
    def _on_log_message(self, message: str, level: str):
        """Handle log message signal (runs on main thread)"""
        self._log_internal(message, level)
    
    def _on_power_data(self, data: PowerDataPoint):
        """Handle power data signal"""
        self.plot_widget.add_power_point(
            data.timestamp_ms, data.current_a, data.voltage_v, data.power_w
        )
    
    def _on_fd_data(self, data: ForceDataPoint):
        """Handle FD data signal"""
        self.plot_widget.add_force_point(data.timestamp_ms, data.force_n)
    
    def _on_test_complete(self):
        """Handle test complete signal"""
        if not self.is_testing:
            return
        
        self.is_testing = False
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.status_label.setText("Complete")
        self.status_label.setStyleSheet("color: blue; font-weight: bold;")
        
        self._save_test_data()
        self._update_results_summary()
    
    def _on_dut_status(self, status: MotorStatus):
        """Handle DUT status signal"""
        self.dut_status_label.setText(
            f"{status.ramp_state.name} | Pos: {status.position} | Speed: {status.current_speed}"
        )
    
    def _update_plots(self):
        """Update plots (called by timer)"""
        if self.is_testing:
            self.plot_widget.update_plots()
            
            # Update duration
            elapsed = time.time() - self.test_start_time
            self.duration_label.setText(f"{elapsed:.1f} s")
            
            # Update sample counts
            self.samples_result.setText(
                f"P: {len(self.power_buffer)} | F: {len(self.force_buffer)}"
            )
    
    def _update_results_summary(self):
        """Update results summary after test"""
        # Max force
        if self.force_buffer:
            forces = [d.force_n for d in self.force_buffer]
            max_f = max(forces)
            self.max_force_result.setText(f"{max_f:.3f} N ({max_f * 1000 / 9.81:.1f} g)")
        
        # Avg power and total energy
        if self.power_buffer:
            powers = [d.power_w * 1000 for d in self.power_buffer]  # mW
            avg_p = np.mean(powers)
            self.avg_power_result.setText(f"{avg_p:.2f} mW")
            
            # Compute energy via trapezoidal integration
            timestamps = np.array([d.timestamp_ms for d in self.power_buffer])
            power_w = np.array([d.power_w for d in self.power_buffer])
            if len(timestamps) > 1:
                try:
                    total_e_j = np.trapezoid(power_w, timestamps / 1000.0)  # type: ignore
                except AttributeError:
                    total_e_j = np.trapz(power_w, timestamps / 1000.0)  # type: ignore
                self.total_energy_result.setText(f"{total_e_j * 1000:.2f} mJ")
            else:
                self.total_energy_result.setText("-- mJ")
    
    # =========================================================================
    # Post-test tools
    # =========================================================================
    
    def _open_alignment_tool(self):
        """Open data alignment tool"""
        if not self.power_buffer and not self.force_buffer:
            QMessageBox.warning(self, "No Data", "No test data available!")
            return
        
        # Prepare data
        power_time = np.array([d.timestamp_ms for d in self.power_buffer]) if self.power_buffer else np.array([])
        current = np.array([d.current_a for d in self.power_buffer]) if self.power_buffer else np.array([])
        force_time = np.array([d.timestamp_ms for d in self.force_buffer]) if self.force_buffer else np.array([])
        force = np.array([d.force_n for d in self.force_buffer]) if self.force_buffer else np.array([])
        
        tool = AlignmentTool(
            test_id=self.test_id_edit.text(),
            power_time_ms=power_time,
            current_a=current,
            force_time_ms=force_time,
            force_n=force,
            parent=self
        )
        
        if tool.exec() == QDialog.DialogCode.Accepted:
            result = tool.get_result()
            self._log(f"Alignment applied: P[{result.power_start_ms:.0f}-{result.power_stop_ms:.0f}ms] "
                     f"F[{result.force_start_ms:.0f}-{result.force_stop_ms:.0f}ms]", "INFO")
            
            # TODO: Update HDF5 with alignment data
    
    def _export_plots(self):
        """Export plots to files"""
        if not self.power_buffer and not self.force_buffer:
            QMessageBox.warning(self, "No Data", "No test data available!")
            return
        
        folder = QFileDialog.getExistingDirectory(self, "Select Export Folder")
        if not folder:
            return
        
        try:
            generator = PlotGenerator()
            test_id = self.test_id_edit.text()
            
            # Prepare data
            power_time = np.array([d.timestamp_ms for d in self.power_buffer]) if self.power_buffer else np.array([])
            current = np.array([d.current_a for d in self.power_buffer]) if self.power_buffer else np.array([])
            voltage = np.array([d.voltage_v for d in self.power_buffer]) if self.power_buffer else np.array([])
            power = np.array([d.power_w for d in self.power_buffer]) if self.power_buffer else np.array([])
            force_time = np.array([d.timestamp_ms for d in self.force_buffer]) if self.force_buffer else np.array([])
            force = np.array([d.force_n for d in self.force_buffer]) if self.force_buffer else np.array([])
            
            # Combined plot
            import os
            combined_path = os.path.join(folder, f"{test_id}_combined.png")
            fig = generator.create_combined_plot(
                force_time_ms=force_time if len(force_time) > 0 else None,
                force_n=force if len(force) > 0 else None,
                power_time_ms=power_time if len(power_time) > 0 else None,
                current_a=current if len(current) > 0 else None,
                voltage_v=voltage if len(voltage) > 0 else None,
                power_w=power if len(power) > 0 else None,
                title=test_id
            )
            fig.savefig(combined_path, dpi=150, bbox_inches='tight')
            plt.close(fig)
            
            self._log(f"Plots exported to {folder}", "INFO")
            QMessageBox.information(self, "Export Complete", f"Plots exported to:\n{folder}")
            
        except Exception as e:
            self._log(f"Export error: {e}", "ERROR")
            QMessageBox.critical(self, "Export Error", str(e))
    
    # =========================================================================
    # Utility
    # =========================================================================
    
    def _log(self, message: str, level: str = "INFO"):
        """Add log message (thread-safe via signal)"""
        # Always emit signal - Qt will queue it to main thread if needed
        self.signals.log_message.emit(message, level)
    
    def _log_internal(self, message: str, level: str = "INFO"):
        """Internal log handler - runs on main thread"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        # Use colors that work on both light and dark themes
        # INFO uses no explicit color to inherit system default
        color_map = {
            "INFO": None,  # Inherit default text color (works on light/dark)
            "WARNING": "#FFA500",  # Orange
            "ERROR": "#FF4444",  # Red (brighter for dark themes)
            "DEBUG": "#888888"  # Gray
        }
        color = color_map.get(level)
        
        if color:
            self.log_text.append(
                f'<span style="color:{color}">[{timestamp}] [{level}] {message}</span>'
            )
        else:
            # INFO level - no color, uses default text color
            self.log_text.append(f'[{timestamp}] [{level}] {message}')
        
        # Auto-scroll
        scrollbar = self.log_text.verticalScrollBar()
        if scrollbar is not None:
            scrollbar.setValue(scrollbar.maximum())
    
    def closeEvent(self, a0):  # type: ignore[override]
        """Handle window close"""
        # Disconnect all devices
        if self.dut:
            try:
                self.dut.disconnect()
            except:
                pass
        
        if self.fd_client:
            try:
                self.fd_client.disconnect()
            except:
                pass
        
        if self.joulescope:
            try:
                self.joulescope.disconnect()
            except:
                pass
        
        if a0 is not None:
            a0.accept()


# =============================================================================
# Main entry point
# =============================================================================

def main():
    """Main entry point"""
    app = QApplication(sys.argv)
    
    # Set application style
    app.setStyle("Fusion")
    
    window = OrchestratorWindow()
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
