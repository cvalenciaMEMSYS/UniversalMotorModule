"""
Simplified Orchestrator - No Joulescope (NoJS)

This orchestrator controls:
- FD (Force-Distance) setup via TCP socket to RPi
- DUT (Device Under Test) via direct USB serial

Joulescope data is recorded separately using official software.

Author: Energy Measurement Test System
Date: January 2026
"""

import sys
import os
import time
import threading
import csv
from datetime import datetime
from dataclasses import dataclass, field
from typing import Optional, List, Callable

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGroupBox, QLabel, QLineEdit, QPushButton, QComboBox, QSpinBox,
    QDoubleSpinBox, QTextEdit, QSplitter, QFrame, QMessageBox,
    QFileDialog, QRadioButton, QButtonGroup, QGridLayout, QTabWidget,
    QCheckBox
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QColor, QPalette

import numpy as np

# Import matplotlib for plots
import matplotlib
matplotlib.use('QtAgg')
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure

# Import our modules
from fd_client_nojs import FDClientNoJS, ForceData
from dut_controller import DUTController, MotorStatus, MotionProfile, RampState


@dataclass
class TestConfig:
    """Test configuration parameters"""
    # DUT parameters
    dut_speed: int = 1000           # steps/sec
    dut_accel: int = 0              # steps/sec² (0 = auto from speed)
    dut_cubesteps: int = 0          # S-curve jerk parameter
    dut_travel_steps: int = 1000    # Steps to move forward
    dut_reverse: bool = False       # Reverse DUT direction
    
    # FD parameters  
    fd_speed: float = 10.0          # mm/s
    fd_distance: float = 10.0       # mm for DRIVE mode
    fd_mode: str = "MONITOR"        # MONITOR or DRIVE
    fd_reverse: bool = False        # Reverse FD direction
    
    # Options
    auto_retract: bool = True       # Auto-retract after test
    retract_ratio: float = 0.5      # Retract this fraction of travel


@dataclass
class TestResult:
    """Results from a single test run"""
    timestamp: str = ""
    config: Optional[TestConfig] = None
    force_data: Optional[ForceData] = None
    dut_start_pos: int = 0
    dut_end_pos: int = 0
    max_force: float = 0.0
    success: bool = False
    notes: str = ""


class SignalBridge(QObject):
    """Bridge for thread-safe signal emission"""
    log_signal = pyqtSignal(str, str)  # message, level
    status_signal = pyqtSignal(str)     # status text
    plot_signal = pyqtSignal(object)    # ForceData
    test_complete_signal = pyqtSignal(bool, str)  # success, message
    force_update_signal = pyqtSignal(float)  # force value for display


class OrchestratorNoJS(QMainWindow):
    """Main application window for simplified orchestrator"""
    
    def __init__(self):
        super().__init__()
        
        # Controllers
        self.fd_client: Optional[FDClientNoJS] = None
        self.dut_controller: Optional[DUTController] = None
        
        # State
        self.config = TestConfig()
        self.results: List[TestResult] = []
        self.current_force_data: Optional[ForceData] = None
        self.test_running = False
        self._force_poll_in_progress = False  # Prevent overlapping force polls
        
        # Signal bridge for thread safety
        self.signals = SignalBridge()
        self.signals.log_signal.connect(self._append_log)
        self.signals.status_signal.connect(self._update_status)
        self.signals.plot_signal.connect(self._update_plots)
        self.signals.test_complete_signal.connect(self._on_test_complete)
        self.signals.force_update_signal.connect(self._on_force_update)
        
        # Setup UI first (before logging)
        self._setup_ui()
        self._setup_timers()
        
        # Now we can log safely
        self.log("Orchestrator initialized", "info")
        
    def _setup_ui(self):
        """Setup the main UI"""
        self.setWindowTitle("Force-Distance Measurement (NoJS)")
        self.setMinimumSize(1400, 900)
        
        # Central widget
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QHBoxLayout(central)
        
        # Left panel - Controls
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_panel.setMaximumWidth(450)
        
        # Connection group
        left_layout.addWidget(self._create_connection_group())
        
        # DUT control group
        left_layout.addWidget(self._create_dut_group())
        
        # FD control group
        left_layout.addWidget(self._create_fd_group())
        
        # Test control group
        left_layout.addWidget(self._create_test_group())
        
        left_layout.addStretch()
        
        # Right panel - Plots and Log
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        
        # Plots in tabs
        self.plot_tabs = QTabWidget()
        right_layout.addWidget(self.plot_tabs, stretch=3)
        
        # Force vs Time plot
        self.fig_time = Figure(figsize=(8, 4))
        self.ax_time = self.fig_time.add_subplot(111)
        self.ax_time.set_xlabel("Time (ms)")
        self.ax_time.set_ylabel("Force (N)")
        self.ax_time.set_title("Force vs Time")
        self.ax_time.grid(True, alpha=0.3)
        self.canvas_time = FigureCanvas(self.fig_time)
        self.plot_tabs.addTab(self.canvas_time, "Force vs Time")
        
        # Force vs Position plot (approximated)
        self.fig_pos = Figure(figsize=(8, 4))
        self.ax_pos = self.fig_pos.add_subplot(111)
        self.ax_pos.set_xlabel("Position (mm)")
        self.ax_pos.set_ylabel("Force (N)")
        self.ax_pos.set_title("Force vs Position (Approximated)")
        self.ax_pos.grid(True, alpha=0.3)
        self.canvas_pos = FigureCanvas(self.fig_pos)
        self.plot_tabs.addTab(self.canvas_pos, "Force vs Position")
        
        # Log area
        log_group = QGroupBox("Log")
        log_layout = QVBoxLayout(log_group)
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Consolas", 9))
        self.log_text.setMaximumHeight(200)
        log_layout.addWidget(self.log_text)
        
        # Clear log button
        clear_btn = QPushButton("Clear Log")
        clear_btn.clicked.connect(lambda: self.log_text.clear())
        log_layout.addWidget(clear_btn)
        
        right_layout.addWidget(log_group, stretch=1)
        
        # Status bar
        self.status_label = QLabel("Ready")
        self.status_label.setStyleSheet("padding: 5px; background-color: #f0f0f0;")
        right_layout.addWidget(self.status_label)
        
        # Add panels to main layout
        main_layout.addWidget(left_panel)
        main_layout.addWidget(right_panel, stretch=1)
        
    def _create_connection_group(self) -> QGroupBox:
        """Create the connection controls group"""
        group = QGroupBox("Connections")
        layout = QGridLayout(group)
        
        # FD Server connection
        layout.addWidget(QLabel("FD Server:"), 0, 0)
        self.fd_host_edit = QLineEdit("192.168.1.12")
        self.fd_host_edit.setPlaceholderText("RPi IP address")
        layout.addWidget(self.fd_host_edit, 0, 1)
        
        self.fd_port_spin = QSpinBox()
        self.fd_port_spin.setRange(1, 65535)
        self.fd_port_spin.setValue(5002)
        layout.addWidget(self.fd_port_spin, 0, 2)
        
        self.fd_connect_btn = QPushButton("Connect")
        self.fd_connect_btn.clicked.connect(self._toggle_fd_connection)
        layout.addWidget(self.fd_connect_btn, 0, 3)
        
        self.fd_status_label = QLabel("●")
        self.fd_status_label.setStyleSheet("color: red; font-size: 16px;")
        layout.addWidget(self.fd_status_label, 0, 4)
        
        # DUT connection
        layout.addWidget(QLabel("DUT Port:"), 1, 0)
        self.dut_port_combo = QComboBox()
        layout.addWidget(self.dut_port_combo, 1, 1, 1, 2)
        
        refresh_btn = QPushButton("↻")
        refresh_btn.setMaximumWidth(30)
        refresh_btn.clicked.connect(self._refresh_dut_ports)
        layout.addWidget(refresh_btn, 1, 2)
        
        self.dut_connect_btn = QPushButton("Connect")
        self.dut_connect_btn.clicked.connect(self._toggle_dut_connection)
        layout.addWidget(self.dut_connect_btn, 1, 3)
        
        self.dut_status_label = QLabel("●")
        self.dut_status_label.setStyleSheet("color: red; font-size: 16px;")
        layout.addWidget(self.dut_status_label, 1, 4)
        
        # Initial port refresh
        self._refresh_dut_ports()
        
        return group
    
    def _create_dut_group(self) -> QGroupBox:
        """Create DUT control group"""
        group = QGroupBox("DUT Control (Motor Module)")
        layout = QGridLayout(group)
        
        # Speed
        layout.addWidget(QLabel("Speed (steps/s):"), 0, 0)
        self.dut_speed_spin = QSpinBox()
        self.dut_speed_spin.setRange(1, 100000)
        self.dut_speed_spin.setValue(1000)
        layout.addWidget(self.dut_speed_spin, 0, 1)
        
        # Acceleration
        layout.addWidget(QLabel("Accel (steps/s²):"), 0, 2)
        self.dut_accel_spin = QSpinBox()
        self.dut_accel_spin.setRange(0, 1000000)
        self.dut_accel_spin.setValue(0)
        self.dut_accel_spin.setSpecialValueText("Auto")
        layout.addWidget(self.dut_accel_spin, 0, 3)
        
        # Cubesteps (jerk)
        layout.addWidget(QLabel("Cubesteps (jerk):"), 1, 0)
        self.dut_cubesteps_spin = QSpinBox()
        self.dut_cubesteps_spin.setRange(0, 1000000)
        self.dut_cubesteps_spin.setValue(0)
        self.dut_cubesteps_spin.setSpecialValueText("None (Trap)")
        layout.addWidget(self.dut_cubesteps_spin, 1, 1)
        
        # Travel steps
        layout.addWidget(QLabel("Travel (steps):"), 1, 2)
        self.dut_travel_spin = QSpinBox()
        self.dut_travel_spin.setRange(1, 10000000)
        self.dut_travel_spin.setValue(1000)
        layout.addWidget(self.dut_travel_spin, 1, 3)
        
        # Options row
        options_layout = QHBoxLayout()
        
        self.dut_retract_check = QCheckBox("Auto-retract (50%)")
        self.dut_retract_check.setChecked(True)
        options_layout.addWidget(self.dut_retract_check)
        
        self.dut_reverse_check = QCheckBox("Reverse Direction")
        self.dut_reverse_check.setToolTip("Invert forward/backward direction for DUT movement")
        options_layout.addWidget(self.dut_reverse_check)
        
        layout.addLayout(options_layout, 2, 0, 1, 4)
        
        # DUT manual controls
        dut_btn_layout = QHBoxLayout()
        
        self.dut_enable_btn = QPushButton("Enable")
        self.dut_enable_btn.clicked.connect(self._dut_enable)
        dut_btn_layout.addWidget(self.dut_enable_btn)
        
        self.dut_disable_btn = QPushButton("Disable")
        self.dut_disable_btn.clicked.connect(self._dut_disable)
        dut_btn_layout.addWidget(self.dut_disable_btn)
        
        self.dut_stop_btn = QPushButton("STOP")
        self.dut_stop_btn.setStyleSheet("background-color: #ff4444; color: white; font-weight: bold;")
        self.dut_stop_btn.clicked.connect(self._dut_stop)
        dut_btn_layout.addWidget(self.dut_stop_btn)
        
        layout.addLayout(dut_btn_layout, 3, 0, 1, 4)
        
        # DUT jog controls (mirroring FD jog UI)
        jog_frame = QFrame()
        jog_layout = QHBoxLayout(jog_frame)
        jog_layout.setContentsMargins(0, 0, 0, 0)
        
        jog_layout.addWidget(QLabel("Jog (steps):"))
        
        self.dut_jog_spin = QSpinBox()
        self.dut_jog_spin.setRange(1, 100000)
        self.dut_jog_spin.setValue(100)
        jog_layout.addWidget(self.dut_jog_spin)
        
        jog_neg_btn = QPushButton("◄")
        jog_neg_btn.clicked.connect(lambda: self._dut_jog(-abs(self.dut_jog_spin.value())))
        jog_layout.addWidget(jog_neg_btn)
        
        jog_pos_btn = QPushButton("►")
        jog_pos_btn.clicked.connect(lambda: self._dut_jog(abs(self.dut_jog_spin.value())))
        jog_layout.addWidget(jog_pos_btn)
        
        layout.addWidget(jog_frame, 4, 0, 1, 4)
        
        # Manual command input for DUT (set current limits, etc.)
        cmd_frame = QFrame()
        cmd_layout = QHBoxLayout(cmd_frame)
        cmd_layout.setContentsMargins(0, 0, 0, 0)
        
        cmd_layout.addWidget(QLabel("Command:"))
        
        self.dut_cmd_edit = QLineEdit()
        self.dut_cmd_edit.setPlaceholderText("e.g., C1000 (set current limit)")
        self.dut_cmd_edit.returnPressed.connect(self._dut_send_command)
        cmd_layout.addWidget(self.dut_cmd_edit)
        
        send_cmd_btn = QPushButton("Send")
        send_cmd_btn.clicked.connect(self._dut_send_command)
        cmd_layout.addWidget(send_cmd_btn)
        
        layout.addWidget(cmd_frame, 5, 0, 1, 4)
        
        return group
    
    def _create_fd_group(self) -> QGroupBox:
        """Create FD control group"""
        group = QGroupBox("FD Control (Force-Distance Setup)")
        layout = QGridLayout(group)
        
        # Mode selection
        layout.addWidget(QLabel("Mode:"), 0, 0)
        self.fd_mode_combo = QComboBox()
        self.fd_mode_combo.addItems(["MONITOR", "DRIVE"])
        self.fd_mode_combo.currentTextChanged.connect(self._on_fd_mode_change)
        layout.addWidget(self.fd_mode_combo, 0, 1)
        
        # Speed
        layout.addWidget(QLabel("Speed (mm/s):"), 0, 2)
        self.fd_speed_spin = QDoubleSpinBox()
        self.fd_speed_spin.setRange(0.1, 25.0)
        self.fd_speed_spin.setValue(10.0)
        self.fd_speed_spin.setDecimals(1)
        layout.addWidget(self.fd_speed_spin, 0, 3)
        
        # Distance (for DRIVE mode)
        layout.addWidget(QLabel("Distance (mm):"), 1, 0)
        self.fd_distance_spin = QDoubleSpinBox()
        self.fd_distance_spin.setRange(0.1, 100.0)
        self.fd_distance_spin.setValue(10.0)
        self.fd_distance_spin.setDecimals(1)
        layout.addWidget(self.fd_distance_spin, 1, 1)
        
        # Apply settings button
        self.fd_apply_btn = QPushButton("Apply Settings")
        self.fd_apply_btn.clicked.connect(self._fd_apply_settings)
        layout.addWidget(self.fd_apply_btn, 1, 2, 1, 2)
        
        # Manual jog controls
        jog_frame = QFrame()
        jog_layout = QHBoxLayout(jog_frame)
        jog_layout.setContentsMargins(0, 0, 0, 0)
        
        jog_layout.addWidget(QLabel("Jog (mm):"))
        self.fd_jog_spin = QDoubleSpinBox()
        self.fd_jog_spin.setRange(-50.0, 50.0)
        self.fd_jog_spin.setValue(1.0)
        self.fd_jog_spin.setDecimals(1)
        jog_layout.addWidget(self.fd_jog_spin)
        
        jog_neg_btn = QPushButton("◄")
        jog_neg_btn.clicked.connect(lambda: self._fd_jog(-abs(self.fd_jog_spin.value())))
        jog_layout.addWidget(jog_neg_btn)
        
        jog_pos_btn = QPushButton("►")
        jog_pos_btn.clicked.connect(lambda: self._fd_jog(abs(self.fd_jog_spin.value())))
        jog_layout.addWidget(jog_pos_btn)
        
        layout.addWidget(jog_frame, 2, 0, 1, 4)
        
        # Zero / tare button
        zero_layout = QHBoxLayout()
        self.fd_zero_btn = QPushButton("Zero / Tare")
        self.fd_zero_btn.clicked.connect(self._fd_zero)
        zero_layout.addWidget(self.fd_zero_btn)
        
        # Reverse direction checkbox
        self.fd_reverse_check = QCheckBox("Reverse Dir")
        self.fd_reverse_check.setToolTip("Invert forward/backward direction for FD movement")
        zero_layout.addWidget(self.fd_reverse_check)
        
        # Force inversion checkbox
        self.fd_invert_check = QCheckBox("Invert Force (×-1)")
        self.fd_invert_check.setToolTip("Multiply force values by -1 when plotting and exporting")
        zero_layout.addWidget(self.fd_invert_check)
        
        # Force display
        self.fd_force_label = QLabel("Force: --- N")
        self.fd_force_label.setFont(QFont("Consolas", 12, QFont.Weight.Bold))
        zero_layout.addWidget(self.fd_force_label)
        
        layout.addLayout(zero_layout, 3, 0, 1, 4)
        
        return group
    
    def _create_test_group(self) -> QGroupBox:
        """Create test control group"""
        group = QGroupBox("Test Control")
        layout = QVBoxLayout(group)
        
        # Test naming fields
        name_layout = QGridLayout()
        
        name_layout.addWidget(QLabel("Test Name:"), 0, 0)
        self.test_name_edit = QLineEdit()
        self.test_name_edit.setPlaceholderText("e.g., DUT_Force_Test")
        self.test_name_edit.setText("test")
        name_layout.addWidget(self.test_name_edit, 0, 1)
        
        name_layout.addWidget(QLabel("Voltage (V):"), 0, 2)
        self.test_voltage_spin = QDoubleSpinBox()
        self.test_voltage_spin.setRange(0.0, 48.0)
        self.test_voltage_spin.setValue(12.0)
        self.test_voltage_spin.setDecimals(1)
        self.test_voltage_spin.setSuffix(" V")
        name_layout.addWidget(self.test_voltage_spin, 0, 3)
        
        layout.addLayout(name_layout)
        
        # Start/Stop buttons
        btn_layout = QHBoxLayout()
        
        self.start_test_btn = QPushButton("▶ Start Test")
        self.start_test_btn.setStyleSheet("background-color: #44aa44; color: white; font-weight: bold; padding: 10px;")
        self.start_test_btn.clicked.connect(self._start_test)
        btn_layout.addWidget(self.start_test_btn)
        
        self.stop_test_btn = QPushButton("■ Stop Test")
        self.stop_test_btn.setStyleSheet("background-color: #aa4444; color: white; font-weight: bold; padding: 10px;")
        self.stop_test_btn.clicked.connect(self._stop_test)
        self.stop_test_btn.setEnabled(False)
        btn_layout.addWidget(self.stop_test_btn)
        
        layout.addLayout(btn_layout)
        
        # Export button
        export_layout = QHBoxLayout()
        
        self.export_btn = QPushButton("Export Data to CSV")
        self.export_btn.clicked.connect(self._export_csv)
        export_layout.addWidget(self.export_btn)
        
        layout.addLayout(export_layout)
        
        # Test info
        self.test_info_label = QLabel("Last test: None")
        layout.addWidget(self.test_info_label)
        
        return group
    
    def _setup_timers(self):
        """Setup periodic timers"""
        # Force reading timer (when in monitor mode)
        self.force_timer = QTimer()
        self.force_timer.timeout.connect(self._update_force_display)
        self.force_timer.start(500)  # 2 Hz update
        
        # Note: DUT status polling removed - not needed and can interfere with operation
    
    # ==================== Logging ====================
    
    def log(self, message: str, level: str = "info"):
        """Thread-safe logging"""
        self.signals.log_signal.emit(message, level)
    
    def _append_log(self, message: str, level: str):
        """Append message to log (called via signal)"""
        # Guard: UI might not be initialized yet
        if not hasattr(self, 'log_text'):
            print(f"[{level.upper()}] {message}")
            return
            
        timestamp = datetime.now().strftime("%H:%M:%S")
        
        color_map = {
            "info": "#000000",
            "success": "#228B22",
            "warning": "#FF8C00",
            "error": "#DC143C"
        }
        color = color_map.get(level, "#000000")
        
        html = f'<span style="color: gray;">[{timestamp}]</span> <span style="color: {color};">{message}</span>'
        self.log_text.append(html)
        
        # Auto-scroll to bottom
        scrollbar = self.log_text.verticalScrollBar()
        if scrollbar:
            scrollbar.setValue(scrollbar.maximum())
    
    def _update_status(self, text: str):
        """Update status bar (thread-safe)"""
        self.status_label.setText(text)
    
    # ==================== Connection Management ====================
    
    def _refresh_dut_ports(self):
        """Refresh available serial ports"""
        self.dut_port_combo.clear()
        ports = DUTController.list_ports()
        self.dut_port_combo.addItems(ports)
        self.log(f"Found {len(ports)} serial ports", "info")
    
    def _toggle_fd_connection(self):
        """Connect/disconnect FD server"""
        if self.fd_client and self.fd_client.is_connected:
            self.fd_client.disconnect()
            self.fd_client = None
            self.fd_connect_btn.setText("Connect")
            self.fd_status_label.setStyleSheet("color: red; font-size: 16px;")
            self.log("FD disconnected", "info")
        else:
            host = self.fd_host_edit.text()
            port = self.fd_port_spin.value()
            
            self.log(f"Connecting to FD server at {host}:{port}...", "info")
            
            self.fd_client = FDClientNoJS()
            if self.fd_client.connect(host, port):
                self.fd_connect_btn.setText("Disconnect")
                self.fd_status_label.setStyleSheet("color: green; font-size: 16px;")
                self.log("FD connected", "success")
            else:
                self.fd_client = None
                self.log("FD connection failed", "error")
    
    def _toggle_dut_connection(self):
        """Connect/disconnect DUT"""
        if self.dut_controller and self.dut_controller.is_connected:
            self.dut_controller.disconnect()
            self.dut_controller = None
            self.dut_connect_btn.setText("Connect")
            self.dut_status_label.setStyleSheet("color: red; font-size: 16px;")
            self.log("DUT disconnected", "info")
        else:
            port = self.dut_port_combo.currentText()
            if not port:
                self.log("No port selected", "error")
                return
            
            self.log(f"Connecting to DUT on {port}...", "info")
            
            self.dut_controller = DUTController()
            self.dut_controller.set_response_callback(lambda msg: self.log(f"DUT: {msg}", "info"))
            
            if self.dut_controller.connect(port):
                self.dut_connect_btn.setText("Disconnect")
                self.dut_status_label.setStyleSheet("color: green; font-size: 16px;")
                self.log("DUT connected", "success")
            else:
                self.dut_controller = None
                self.log("DUT connection failed", "error")
    
    # ==================== DUT Controls ====================
    
    def _dut_enable(self):
        """Enable DUT motor"""
        if self.dut_controller and self.dut_controller.is_connected:
            if self.dut_controller.enable():
                self.log("DUT motor enabled", "success")
            else:
                self.log("Failed to enable DUT motor", "error")
    
    def _dut_disable(self):
        """Disable DUT motor"""
        if self.dut_controller and self.dut_controller.is_connected:
            if self.dut_controller.disable():
                self.log("DUT motor disabled", "success")
            else:
                self.log("Failed to disable DUT motor", "error")
    
    def _dut_stop(self):
        """Emergency stop DUT"""
        if self.dut_controller and self.dut_controller.is_connected:
            self.dut_controller.stop()
            self.log("DUT STOPPED", "warning")
    
    def _dut_jog(self, steps: int):
        """Manual jog DUT motor by specified steps"""
        if not self.dut_controller or not self.dut_controller.is_connected:
            self.log("DUT not connected", "error")
            return
        
        # Apply direction reversal if enabled
        if self.dut_reverse_check.isChecked():
            steps = -steps
        
        direction = "forward" if steps > 0 else "backward"
        self.log(f"Jogging DUT {abs(steps)} steps {direction}...", "info")
        
        # Apply current settings first
        self._apply_dut_settings()
        
        if self.dut_controller.move(steps, wait=False):
            self.log("DUT jog started", "success")
        else:
            self.log("DUT jog failed", "error")
    
    def _dut_send_command(self):
        """Send a manual command to the DUT (ESP32)"""
        if not self.dut_controller or not self.dut_controller.is_connected:
            self.log("DUT not connected", "error")
            return
        
        cmd = self.dut_cmd_edit.text().strip()
        if not cmd:
            return
        
        self.log(f"Sending to DUT: {cmd}", "info")
        
        try:
            response = self.dut_controller.send_raw_command(cmd)
            if response:
                # Join response lines if multiple
                resp_text = " | ".join(response) if isinstance(response, list) else str(response)
                self.log(f"DUT response: {resp_text}", "success")
            else:
                self.log("DUT: No response (command sent)", "info")
        except Exception as e:
            self.log(f"DUT command error: {e}", "error")
        
        # Clear the input
        self.dut_cmd_edit.clear()
    
    def _apply_dut_settings(self):
        """Apply current DUT settings"""
        if not self.dut_controller or not self.dut_controller.is_connected:
            return False
        
        speed = self.dut_speed_spin.value()
        accel = self.dut_accel_spin.value()
        cubesteps = self.dut_cubesteps_spin.value()
        
        self.dut_controller.set_speed(speed)
        self.dut_controller.set_accel(accel)
        if cubesteps > 0:
            self.dut_controller.set_cubesteps(cubesteps)
        
        return True
    
    # ==================== FD Controls ====================
    
    def _on_fd_mode_change(self, mode: str):
        """Handle FD mode change"""
        # Enable/disable distance based on mode
        self.fd_distance_spin.setEnabled(mode == "DRIVE")
    
    def _fd_apply_settings(self):
        """Apply FD settings"""
        if not self.fd_client or not self.fd_client.is_connected:
            self.log("FD not connected", "error")
            return
        
        mode = self.fd_mode_combo.currentText()
        speed = self.fd_speed_spin.value()
        
        # Set mode
        if self.fd_client.set_mode(mode):
            self.log(f"FD mode set to {mode}", "success")
        else:
            self.log("Failed to set FD mode", "error")
            return
        
        # Set speed
        if self.fd_client.set_speed(speed):
            self.log(f"FD speed set to {speed} mm/s", "success")
        else:
            self.log("Failed to set FD speed", "error")
    
    def _fd_jog(self, distance_mm: float):
        """Jog FD setup"""
        if not self.fd_client or not self.fd_client.is_connected:
            self.log("FD not connected", "error")
            return
        
        # Apply direction reversal if enabled
        if self.fd_reverse_check.isChecked():
            distance_mm = -distance_mm
        
        self.log(f"Jogging FD {distance_mm} mm...", "info")
        
        if self.fd_client.jog(distance_mm):
            self.log("FD jog complete", "success")
        else:
            self.log("FD jog failed", "error")
    
    def _fd_zero(self):
        """Zero/tare force sensor"""
        if not self.fd_client or not self.fd_client.is_connected:
            self.log("FD not connected", "error")
            return
        
        if self.fd_client.zero_loadcell():
            self.log("Force sensor zeroed", "success")
        else:
            self.log("Failed to zero force sensor", "error")
    
    def _update_force_display(self):
        """Update force reading display - starts async read"""
        if self._force_poll_in_progress:
            return  # Previous poll still in progress
        if not self.fd_client or not self.fd_client.is_connected or self.test_running:
            return
        
        self._force_poll_in_progress = True
        # Run the blocking read_force in a background thread
        threading.Thread(target=self._poll_force_async, daemon=True).start()
    
    def _poll_force_async(self):
        """Background thread to poll force (non-blocking)"""
        try:
            if self.fd_client and self.fd_client.is_connected:
                force = self.fd_client.read_force()
                if force is not None:
                    self.signals.force_update_signal.emit(force)
        except:
            pass
        finally:
            self._force_poll_in_progress = False
    
    def _on_force_update(self, force: float):
        """Handle force update from background thread"""
        self.fd_force_label.setText(f"Force: {force:.3f} N")
    
    # ==================== Test Control ====================
    
    def _start_test(self):
        """Start a measurement test"""
        # Verify connections
        if not self.fd_client or not self.fd_client.is_connected:
            self.log("FD not connected", "error")
            QMessageBox.warning(self, "Error", "FD server not connected!")
            return
        
        if not self.dut_controller or not self.dut_controller.is_connected:
            self.log("DUT not connected", "error")
            QMessageBox.warning(self, "Error", "DUT not connected!")
            return
        
        # Collect config
        self.config = TestConfig(
            dut_speed=self.dut_speed_spin.value(),
            dut_accel=self.dut_accel_spin.value(),
            dut_cubesteps=self.dut_cubesteps_spin.value(),
            dut_travel_steps=self.dut_travel_spin.value(),
            dut_reverse=self.dut_reverse_check.isChecked(),
            fd_speed=self.fd_speed_spin.value(),
            fd_distance=self.fd_distance_spin.value(),
            fd_mode=self.fd_mode_combo.currentText(),
            fd_reverse=self.fd_reverse_check.isChecked(),
            auto_retract=self.dut_retract_check.isChecked(),
            retract_ratio=0.5
        )
        
        # Update UI
        self.test_running = True
        self.start_test_btn.setEnabled(False)
        self.stop_test_btn.setEnabled(True)
        
        self.log("Starting test...", "info")
        self._update_status("Test in progress...")
        
        # Run test in background thread
        test_thread = threading.Thread(target=self._run_test, args=(self.config,), daemon=True)
        test_thread.start()
    
    def _run_test(self, config: TestConfig):
        """Run the test sequence (in background thread)"""
        try:
            # Safety check - should never happen due to pre-checks in _start_test
            if not self.dut_controller or not self.fd_client:
                self.signals.test_complete_signal.emit(False, "Controllers not initialized")
                return
            
            result = TestResult(
                timestamp=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                config=config
            )
            
            # Get current FD mode from UI (captured in config)
            fd_mode = config.fd_mode
            
            if fd_mode == "MONITOR":
                # === MONITOR MODE: DUT Force Test ===
                # DUT moves against FD while FD records force (stationary)
                self._run_monitor_test(config, result)
            else:
                # === DRIVE MODE: Backdrive Test ===
                # DUT is disabled, FD pushes against it to test backdrive resistance
                self._run_drive_test(config, result)
            
            result.success = True
            self.results.append(result)
            self.current_force_data = result.force_data
            
            self.signals.test_complete_signal.emit(True, "Test completed successfully")
            
        except Exception as e:
            self.signals.log_signal.emit(f"Test error: {e}", "error")
            self.signals.test_complete_signal.emit(False, str(e))
    
    def _run_monitor_test(self, config: TestConfig, result: TestResult):
        """
        MONITOR Mode Test - DUT Force Test
        DUT pushes against stationary FD while recording force
        """
        # Type assertions for Pylance (already checked in _run_test)
        assert self.dut_controller is not None
        assert self.fd_client is not None
        
        self.signals.log_signal.emit("=== MONITOR MODE: DUT Force Test ===", "info")
        
        # 1. Apply DUT settings
        self.signals.log_signal.emit("Applying DUT settings...", "info")
        self.dut_controller.set_speed(config.dut_speed)
        self.dut_controller.set_accel(config.dut_accel)
        if config.dut_cubesteps > 0:
            self.dut_controller.set_cubesteps(config.dut_cubesteps)
        
        result.dut_start_pos = self.dut_controller.get_position() or 0
        
        # 2. Configure FD for MONITOR mode
        self.signals.log_signal.emit("Configuring FD for MONITOR mode...", "info")
        self.fd_client.set_mode("MONITOR")
        
        # 3. Start FD streaming (FD stays still, just records)
        self.signals.log_signal.emit("Starting force recording...", "info")
        self.fd_client.start_stream()
        time.sleep(0.1)  # Let streaming start
        
        # 4. Enable DUT and move forward
        self.signals.log_signal.emit("Enabling DUT motor...", "info")
        self.dut_controller.enable()
        time.sleep(0.1)
        
        # Apply direction reversal
        travel_steps = config.dut_travel_steps
        if config.dut_reverse:
            travel_steps = -travel_steps
        
        self.signals.log_signal.emit(f"DUT moving forward: {config.dut_travel_steps} steps...", "info")
        self.dut_controller.move(travel_steps, wait=True, timeout=30.0)
        
        # 5. Retract DUT (always opposite direction)
        if config.auto_retract:
            retract_steps = int(travel_steps * config.retract_ratio)
            self.signals.log_signal.emit(f"DUT retracting...", "info")
            self.dut_controller.move(-retract_steps, wait=True, timeout=30.0)
        
        result.dut_end_pos = self.dut_controller.get_position() or 0
        
        # 6. Disable DUT
        self.dut_controller.disable()
        
        # 7. Stop FD streaming and get data
        self.signals.log_signal.emit("Stopping force recording...", "info")
        force_data = self.fd_client.stop_stream()
        
        if force_data and len(force_data.timestamps_ms) > 0:
            result.force_data = force_data
            result.max_force = max(force_data.force_n) if force_data.force_n else 0
            self.signals.log_signal.emit(f"Collected {len(force_data.timestamps_ms)} force samples", "success")
            self.signals.log_signal.emit(f"Max force: {result.max_force:.3f} N", "info")
            self.signals.plot_signal.emit(force_data)
        else:
            self.signals.log_signal.emit("Warning: No force data collected", "warning")
    
    def _run_drive_test(self, config: TestConfig, result: TestResult):
        """
        DRIVE Mode Test - Backdrive Test
        DUT is disabled, FD pushes against it to measure backdrive resistance
        """
        # Type assertions for Pylance (already checked in _run_test)
        assert self.dut_controller is not None
        assert self.fd_client is not None
        
        self.signals.log_signal.emit("=== DRIVE MODE: Backdrive Test ===", "info")
        
        # 1. Ensure DUT is DISABLED (no power - testing mechanical resistance)
        self.signals.log_signal.emit("Disabling DUT motor (testing backdrive resistance)...", "info")
        self.dut_controller.disable()
        
        # 2. Configure FD for DRIVE mode
        self.signals.log_signal.emit("Configuring FD for DRIVE mode...", "info")
        self.fd_client.set_mode("DRIVE")
        self.fd_client.set_speed(config.fd_speed)
        
        # 3. FD moves forward while recording force (DRIVE command does this automatically)
        # Apply direction reversal
        fd_distance = config.fd_distance
        if config.fd_reverse:
            fd_distance = -fd_distance
        
        self.signals.log_signal.emit(f"FD moving and recording: {config.fd_distance} mm at {config.fd_speed} mm/s...", "info")
        force_data = self.fd_client.move(fd_distance)
        
        if force_data and len(force_data.timestamps_ms) > 0:
            result.force_data = force_data
            result.max_force = max(force_data.force_n) if force_data.force_n else 0
            self.signals.log_signal.emit(f"Collected {len(force_data.timestamps_ms)} force samples", "success")
            self.signals.log_signal.emit(f"Max force: {result.max_force:.3f} N", "info")
            self.signals.plot_signal.emit(force_data)
        else:
            self.signals.log_signal.emit("Warning: No force data collected", "warning")
    
    def _stop_test(self):
        """Stop the current test"""
        self.log("Stopping test...", "warning")
        
        # Stop both devices
        if self.dut_controller and self.dut_controller.is_connected:
            self.dut_controller.stop()
        
        if self.fd_client and self.fd_client.is_connected:
            self.fd_client.stop_stream()
        
        self.test_running = False
        self.start_test_btn.setEnabled(True)
        self.stop_test_btn.setEnabled(False)
        self._update_status("Test stopped")
    
    def _on_test_complete(self, success: bool, message: str):
        """Handle test completion (called via signal)"""
        self.test_running = False
        self.start_test_btn.setEnabled(True)
        self.stop_test_btn.setEnabled(False)
        
        if success:
            self.log(message, "success")
            self._update_status("Test complete")
            
            if self.current_force_data:
                samples = len(self.current_force_data.timestamps_ms)
                max_f = max(self.current_force_data.force_n) if self.current_force_data.force_n else 0
                self.test_info_label.setText(f"Last test: {samples} samples, max force: {max_f:.3f} N")
        else:
            self.log(f"Test failed: {message}", "error")
            self._update_status("Test failed")
    
    # ==================== Plotting ====================
    
    def _update_plots(self, force_data: ForceData):
        """Update plots with new force data"""
        if not force_data or len(force_data.timestamps_ms) == 0:
            return
        
        # Apply force inversion if enabled
        force_values = list(force_data.force_n)
        if self.fd_invert_check.isChecked():
            force_values = [-f for f in force_values]
        
        # Force vs Time
        self.ax_time.clear()
        self.ax_time.plot(force_data.timestamps_ms, force_values, 'b-', linewidth=0.8)
        self.ax_time.set_xlabel("Time (ms)")
        self.ax_time.set_ylabel("Force (N)")
        invert_label = " (Inverted)" if self.fd_invert_check.isChecked() else ""
        self.ax_time.set_title(f"Force vs Time{invert_label} ({len(force_data.timestamps_ms)} samples)")
        self.ax_time.grid(True, alpha=0.3)
        self.fig_time.tight_layout()
        self.canvas_time.draw()
        
        # Force vs Position (approximated from time and speed)
        # Approximate position from FD speed
        fd_speed = self.fd_speed_spin.value()  # mm/s
        times_sec = np.array(force_data.timestamps_ms) / 1000.0
        positions_mm = times_sec * fd_speed
        
        self.ax_pos.clear()
        self.ax_pos.plot(positions_mm, force_values, 'r-', linewidth=0.8)
        self.ax_pos.set_xlabel("Position (mm)")
        self.ax_pos.set_ylabel("Force (N)")
        self.ax_pos.set_title(f"Force vs Position{invert_label} (Approximated @ {fd_speed} mm/s)")
        self.ax_pos.grid(True, alpha=0.3)
        self.fig_pos.tight_layout()
        self.canvas_pos.draw()
    
    # ==================== Export ====================
    
    def _export_csv(self):
        """Export force data to CSV"""
        if not self.current_force_data or len(self.current_force_data.timestamps_ms) == 0:
            QMessageBox.warning(self, "No Data", "No force data to export!")
            return
        
        # Build filename from test name and voltage
        test_name = self.test_name_edit.text().strip() or "test"
        # Sanitize test name (remove invalid filename chars)
        test_name = "".join(c for c in test_name if c.isalnum() or c in "_-")
        voltage = self.test_voltage_spin.value()
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        default_name = f"{test_name}_{voltage:.1f}V_{timestamp}.csv"
        
        filename, _ = QFileDialog.getSaveFileName(
            self, "Export Force Data", default_name, "CSV Files (*.csv)"
        )
        
        if not filename:
            return
        
        # Write CSV
        try:
            # Apply force inversion if enabled
            invert = self.fd_invert_check.isChecked()
            
            with open(filename, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(["Timestamp_ms", "Force_N"])
                for t, force in zip(self.current_force_data.timestamps_ms, self.current_force_data.force_n):
                    output_force = -force if invert else force
                    writer.writerow([t, output_force])
            
            invert_note = " (inverted)" if invert else ""
            self.log(f"Exported {len(self.current_force_data.timestamps_ms)} samples{invert_note} to {filename}", "success")
            QMessageBox.information(self, "Export Complete", f"Data exported to:\n{filename}")
            
        except Exception as e:
            self.log(f"Export error: {e}", "error")
            QMessageBox.critical(self, "Export Error", str(e))
    
    # ==================== Cleanup ====================
    
    def closeEvent(self, a0):  # type: ignore
        """Handle window close"""
        # Stop timers
        self.force_timer.stop()
        
        # Disconnect devices
        if self.fd_client:
            try:
                self.fd_client.disconnect()
            except:
                pass
        
        if self.dut_controller:
            try:
                self.dut_controller.disconnect()
            except:
                pass
        
        if a0:
            a0.accept()


def main():
    app = QApplication(sys.argv)
    
    # Set application style
    app.setStyle("Fusion")
    
    window = OrchestratorNoJS()
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
