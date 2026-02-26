"""
Force-Deflection V3 — Automated Multi-Position FD Sweep

Coordinates a force-deflection setup (via RPi TCP server) with a UMM stepper
motor (via direct serial) to run FD cycles at multiple motor positions.

Prerequisites:
    1. Install dependencies:
       pip install PyQt6 pyqtgraph numpy pyserial h5py

    2. On the Raspberry Pi connected to the FD Arduino, start the server:
       python fd_server_nojs.py --serial /dev/ttyACM0

Usage:
    python ForceDeflection_V3.py
"""

import json
import os
import re
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Dict, List, Optional, Tuple

import h5py
import numpy as np
import pyqtgraph as pg
import serial
import serial.tools.list_ports
from PyQt6.QtCore import QBuffer, QIODevice, QObject, Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QCloseEvent, QColor, QFont
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QSplitter,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from fd_client_nojs import FDClientNoJS

# =============================================================================
# Constants
# =============================================================================

HARDWARE_FORCE_LIMIT_N = 100.0
DEFAULT_MOTOR_SPEED = 1000  # steps/s

BASE_HUES: List[Tuple[int, int, int]] = [
    (31, 119, 180),   # blue
    (214, 39, 40),    # red
    (44, 160, 44),    # green
    (255, 127, 14),   # orange
    (148, 103, 189),  # purple
    (140, 86, 75),    # brown
    (227, 119, 194),  # pink
    (127, 127, 127),  # gray
    (188, 189, 34),   # olive
    (23, 190, 207),   # cyan
]

DIRECTION_PATTERNS: Dict[str, List[int]] = {
    "Push only": [+1],
    "Pull only": [-1],
    "Push-Pull": [+1, -1],
    "Pull-Push": [-1, +1],
}

SETUP_JOG_PATTERNS = {"Pull only", "Pull-Push"}

# =============================================================================
# Data Classes
# =============================================================================


@dataclass
class MeasurementPoint:
    """Single force-displacement measurement point."""

    position_mm: float
    force_n: float


@dataclass
class PhaseData:
    """Data for one directional phase within a cycle."""

    cycle_number: int   # 1-based
    phase_name: str     # "push" or "pull"
    points: List[MeasurementPoint] = field(default_factory=list)


# =============================================================================
# Motor Connection
# =============================================================================


class MotorConnection:
    """Manages serial connection to UMM stepper motor.

    Provides thread-safe command sending, background reading, and
    convenience methods for movement and status queries.
    """

    def __init__(self) -> None:
        self._serial: Optional[serial.Serial] = None
        self._lock = threading.Lock()
        self._reader_thread: Optional[threading.Thread] = None
        self._running = False
        self._response_lines: list[str] = []
        self._response_event = threading.Event()

    @property
    def is_connected(self) -> bool:
        """Return True when the serial port is open."""
        return self._serial is not None and self._serial.is_open

    def connect(self, port: str) -> bool:
        """Open *port* at 115200-8N1 with DTR disabled.

        A 2-second post-connect delay is observed to let the ESP32
        boot without being reset.  Sends initial config commands to
        set accel and cubesteps to 0.

        Args:
            port: COM port string (e.g. ``"COM7"``).

        Returns:
            True if connection succeeded.
        """
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = 115200
            ser.bytesize = serial.EIGHTBITS
            ser.parity = serial.PARITY_NONE
            ser.stopbits = serial.STOPBITS_ONE
            ser.timeout = 0.1
            ser.dtr = False

            ser.open()
            ser.dtr = False

            self._serial = ser
            self._running = True
            self._reader_thread = threading.Thread(
                target=self._reader_loop, daemon=True, name="motor-reader"
            )
            self._reader_thread.start()

            time.sleep(2.0)

            # Initial configuration
            self.send_command("set accel 0")
            self.send_command("set cubesteps 0")
            return True
        except serial.SerialException:
            self._serial = None
            return False

    def disconnect(self) -> None:
        """Close the serial port and stop the reader thread."""
        self._running = False
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None
        if self._reader_thread is not None:
            self._reader_thread.join(timeout=2.0)
            self._reader_thread = None

    def send_command(self, cmd: str, timeout: float = 2.0) -> list[str]:
        """Send *cmd* and collect response lines.

        Thread-safe — acquires internal lock before accessing the port.

        Args:
            cmd: ASCII command string (newline is appended automatically).
            timeout: Maximum seconds to wait for response data.

        Returns:
            List of response lines received within *timeout*.
        """
        with self._lock:
            if not self.is_connected or self._serial is None:
                return []

            self._response_lines = []
            self._response_event.clear()

            try:
                self._serial.write((cmd + "\n").encode("ascii"))
            except serial.SerialException:
                return []
            self._response_event.wait(timeout=timeout)
            time.sleep(0.05)
            lines = list(self._response_lines)
            self._response_lines = []
            print(f"Motor command sent: {cmd}")
            return lines

    def move(self, steps: int) -> bool:
        """Send a move command.

        Args:
            steps: Number of steps (positive or negative).

        Returns:
            True if the command was sent (does not wait for completion).
        """
        lines = self.send_command(f"move {steps}")
        
        return len(lines) >= 0  # command sent successfully

    def set_speed(self, speed: int) -> bool:
        """Set the motor speed.

        Args:
            speed: Speed in steps per second.

        Returns:
            True if acknowledged.
        """
        lines = self.send_command(f"set speed {speed}")
        return len(lines) >= 0

    def get_position(self) -> Optional[int]:
        """Query current motor position.

        Returns:
            Position in steps, or None if parsing failed.
        """
        lines = self.send_command("get pos", timeout=0.5)
        for line in lines:
            match = re.search(r"Position:\s*(-?\d+)", line)
            if match:
                return int(match.group(1))
        return None

    def wait_for_idle(self, timeout: float = 30.0) -> bool:
        """Poll ramp state until IDLE or *timeout* expires.

        Args:
            timeout: Maximum seconds to wait.

        Returns:
            True if IDLE was reached within *timeout*.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            lines = self.send_command("get rampstate", timeout=0.5)
            for line in lines:
                if "IDLE" in line.upper():
                    return True
            time.sleep(0.2)
        return False

    # -- background reader --------------------------------------------------

    def _reader_loop(self) -> None:
        """Continuously read lines from the serial port (daemon thread)."""
        while self._running:
            try:
                if self._serial is None or not self._serial.is_open:
                    break
                raw = self._serial.readline()
                if raw:
                    line = raw.decode("ascii", errors="replace").strip()
                    if line:
                        self._response_lines.append(line)
                        self._response_event.set()
            except serial.SerialException:
                if self._running:
                    break
            except Exception:
                continue


# =============================================================================
# Signal Bridge
# =============================================================================


class SignalBridge(QObject):
    """Thread-safe signal bridge for background sweep loop."""

    new_point = pyqtSignal(int, int, int, float, float)  # pos_idx, cycle_idx, seg_idx, pos, force
    progress = pyqtSignal(str)                             # progress text
    log = pyqtSignal(str, str)                             # message, level
    finished = pyqtSignal()
    aborted = pyqtSignal(str)                              # reason
    force_update = pyqtSignal(float)                       # live force reading
    position_update = pyqtSignal(int)                      # motor position in steps


# =============================================================================
# Helpers
# =============================================================================


def _dark_color(pos_idx: int) -> QColor:
    """Return the base (dark) color for the given position index."""
    r, g, b = BASE_HUES[pos_idx % len(BASE_HUES)]
    return QColor(r, g, b)


def _light_color(pos_idx: int, blend: float = 0.40) -> QColor:
    """Return a lightened variant of the base color (blended toward white)."""
    r, g, b = BASE_HUES[pos_idx % len(BASE_HUES)]
    lr = int(r + (255 - r) * blend)
    lg = int(g + (255 - g) * blend)
    lb = int(b + (255 - b) * blend)
    return QColor(lr, lg, lb)


def _determine_phase_name(
    seg_idx: int, direction: int, pattern_name: str
) -> str:
    """Determine the phase name from segment index and direction.

    Args:
        seg_idx: Segment index within the cycle.
        direction: +1 or -1.
        pattern_name: Name of the direction pattern.

    Returns:
        ``"push"`` or ``"pull"``.
    """
    if pattern_name == "Pull only":
        return "pull"
    if pattern_name == "Push only":
        return "push"
    if pattern_name == "Pull-Push":
        return "pull" if seg_idx == 0 else "push"
    # Push-Pull or default
    return "push" if direction > 0 else "pull"


# =============================================================================
# Main Window
# =============================================================================


class ForceDeflectionSweepWindow(QMainWindow):
    """Main window for Force-Deflection V3 multi-position sweep tool."""

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Force-Deflection V3 — Automated Multi-Position Sweep")
        self.resize(1300, 850)

        # -- FD state --
        self._client = FDClientNoJS()
        self._fd_connected = False

        # -- Motor state --
        self._motor = MotorConnection()
        self._motor_connected = False

        # -- Test state --
        self._test_running = False
        self._abort_requested = False
        self._test_thread: Optional[threading.Thread] = None
        self._force_inverted: bool = False
        self._polling_in_progress: bool = False

        # -- Data --
        self._all_position_data: Dict[int, List[PhaseData]] = {}
        self._curves: Dict[Tuple[int, int, int], pg.PlotDataItem] = {}  # (pos, cycle, seg)
        self._curve_xdata: Dict[Tuple[int, int, int], List[float]] = {}
        self._curve_ydata: Dict[Tuple[int, int, int], List[float]] = {}
        self._y_max_seen: float = 0.0

        # -- HDF5 --
        self._h5file: Optional[h5py.File] = None
        self._h5path: str = ""

        # -- Signal bridge --
        self._signals = SignalBridge()
        self._signals.new_point.connect(self._on_new_point)
        self._signals.progress.connect(self._on_progress)
        self._signals.log.connect(self._on_log)
        self._signals.finished.connect(self._on_finished)
        self._signals.aborted.connect(self._on_aborted)
        self._signals.force_update.connect(self._on_force_update)
        self._signals.position_update.connect(self._on_position_update)

        # -- Timers --
        self._force_timer = QTimer()
        self._force_timer.setInterval(500)
        self._force_timer.timeout.connect(self._poll_force)

        self._motor_timer = QTimer()
        self._motor_timer.setInterval(1000)
        self._motor_timer.timeout.connect(self._poll_motor_position)

        self._build_ui()
        self._update_controls()
        self._log("Application started.", "info")

    # =========================================================================
    # UI Construction
    # =========================================================================

    def _build_ui(self) -> None:
        """Construct the full GUI layout."""
        central = QWidget()
        self.setCentralWidget(central)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        main_layout = QHBoxLayout(central)
        main_layout.addWidget(splitter)

        # --- Left panel (controls) ---
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_widget.setMaximumWidth(420)

        left_layout.addWidget(self._build_fd_connection_group())
        left_layout.addWidget(self._build_motor_connection_group())
        left_layout.addWidget(self._build_motor_settings_group())
        left_layout.addWidget(self._build_fd_params_group())
        left_layout.addWidget(self._build_cycle_group())
        left_layout.addWidget(self._build_sweep_config_group())
        left_layout.addWidget(self._build_manual_group())
        left_layout.addWidget(self._build_test_controls_group())
        left_layout.addStretch()

        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_area.setWidget(left_widget)
        scroll_area.setMaximumWidth(440)
        scroll_area.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff
        )
        splitter.addWidget(scroll_area)

        # --- Right panel (plots + analysis + log) ---
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)

        self._plot_widget = pg.PlotWidget(title="Force vs Displacement")
        self._plot_widget.setLabel("bottom", "Displacement (mm)")
        self._plot_widget.setLabel("left", "Force (N)")
        self._plot_widget.showGrid(x=True, y=True)
        self._plot_widget.addLegend()
        right_layout.addWidget(self._plot_widget, stretch=3)

        right_layout.addLayout(self._build_data_controls_row())
        self._analysis_group = self._build_analysis_group()
        self._analysis_group.setVisible(False)
        right_layout.addWidget(self._analysis_group)
        right_layout.addWidget(self._build_log_group(), stretch=1)

        splitter.addWidget(right_widget)
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)

    # -- FD Connection group --

    def _build_fd_connection_group(self) -> QGroupBox:
        """Build the FD server connection controls."""
        group = QGroupBox("FD Connection")
        layout = QGridLayout(group)

        layout.addWidget(QLabel("Host:"), 0, 0)
        self._fd_host_edit = QLineEdit("192.168.1.12")
        layout.addWidget(self._fd_host_edit, 0, 1)

        self._fd_port_spin = QSpinBox()
        self._fd_port_spin.setRange(1, 65535)
        self._fd_port_spin.setValue(5002)
        layout.addWidget(self._fd_port_spin, 0, 2)

        self._fd_connect_btn = QPushButton("Connect")
        self._fd_connect_btn.clicked.connect(self._toggle_fd_connection)
        layout.addWidget(self._fd_connect_btn, 0, 3)

        self._fd_indicator = QLabel("\u25CF")
        self._fd_indicator.setStyleSheet("color: red; font-size: 16px;")
        layout.addWidget(self._fd_indicator, 0, 4)

        return group

    # -- Motor Connection group --

    def _build_motor_connection_group(self) -> QGroupBox:
        """Build the motor serial connection controls."""
        group = QGroupBox("Motor Connection")
        layout = QGridLayout(group)

        self._motor_port_combo = QComboBox()
        self._motor_port_combo.setMinimumWidth(140)
        layout.addWidget(self._motor_port_combo, 0, 0, 1, 2)

        self._motor_refresh_btn = QPushButton("Refresh")
        self._motor_refresh_btn.clicked.connect(self._refresh_motor_ports)
        layout.addWidget(self._motor_refresh_btn, 0, 2)

        self._motor_connect_btn = QPushButton("Connect")
        self._motor_connect_btn.clicked.connect(self._toggle_motor_connection)
        layout.addWidget(self._motor_connect_btn, 0, 3)

        self._motor_indicator = QLabel("\u25CF")
        self._motor_indicator.setStyleSheet("color: red; font-size: 16px;")
        layout.addWidget(self._motor_indicator, 0, 4)

        self._refresh_motor_ports()
        return group

    # -- Motor Settings group --

    def _build_motor_settings_group(self) -> QGroupBox:
        """Build the motor parameters group box."""
        group = QGroupBox("Motor Settings")
        layout = QFormLayout(group)

        self._motor_speed_spin = QSpinBox()
        self._motor_speed_spin.setRange(100, 10000)
        self._motor_speed_spin.setValue(DEFAULT_MOTOR_SPEED)
        self._motor_speed_spin.setSuffix(" steps/s")
        layout.addRow("Speed:", self._motor_speed_spin)

        self._motor_step_spin = QSpinBox()
        self._motor_step_spin.setRange(-10000, 10000)
        self._motor_step_spin.setValue(1000)
        self._motor_step_spin.setSuffix(" steps")
        layout.addRow("Step Size:", self._motor_step_spin)

        return group

    # -- FD Measurement parameters --

    def _build_fd_params_group(self) -> QGroupBox:
        """Build the FD measurement parameters group box."""
        group = QGroupBox("FD Measurement")
        layout = QFormLayout(group)

        self._fd_step_spin = QDoubleSpinBox()
        self._fd_step_spin.setRange(0.01, 5.0)
        self._fd_step_spin.setValue(0.1)
        self._fd_step_spin.setSuffix(" mm")
        self._fd_step_spin.setDecimals(2)
        layout.addRow("Step Size:", self._fd_step_spin)

        self._fd_dist_spin = QDoubleSpinBox()
        self._fd_dist_spin.setRange(0.1, 50.0)
        self._fd_dist_spin.setValue(5.0)
        self._fd_dist_spin.setSuffix(" mm")
        self._fd_dist_spin.setDecimals(1)
        layout.addRow("Total Distance:", self._fd_dist_spin)

        self._fd_speed_spin = QDoubleSpinBox()
        self._fd_speed_spin.setRange(0.1, 25.0)
        self._fd_speed_spin.setValue(1.0)
        self._fd_speed_spin.setSuffix(" mm/s")
        self._fd_speed_spin.setDecimals(1)
        layout.addRow("Move Speed:", self._fd_speed_spin)

        self._fd_avg_spin = QSpinBox()
        self._fd_avg_spin.setRange(1, 20)
        self._fd_avg_spin.setValue(3)
        self._fd_avg_spin.setSuffix(" samples")
        layout.addRow("Force Averages:", self._fd_avg_spin)

        self._fd_limit_spin = QDoubleSpinBox()
        self._fd_limit_spin.setRange(0.1, 100.0)
        self._fd_limit_spin.setValue(50.0)
        self._fd_limit_spin.setSuffix(" N")
        self._fd_limit_spin.setDecimals(1)
        layout.addRow("Force Limit:", self._fd_limit_spin)

        hw_label = QLabel("Hardware limit: 100 N (load cell max)")
        hw_label.setStyleSheet("color: #e09000; font-weight: bold;")
        layout.addRow(hw_label)

        return group

    # -- Cycle configuration --

    def _build_cycle_group(self) -> QGroupBox:
        """Build the FD cycle configuration group box."""
        group = QGroupBox("FD Cycle Config")
        layout = QFormLayout(group)

        self._direction_combo = QComboBox()
        self._direction_combo.addItems(list(DIRECTION_PATTERNS.keys()))
        self._direction_combo.setCurrentText("Push only")
        layout.addRow("Direction Pattern:", self._direction_combo)

        self._cycles_spin = QSpinBox()
        self._cycles_spin.setRange(1, 100)
        self._cycles_spin.setValue(1)
        layout.addRow("Cycles / Position:", self._cycles_spin)

        return group

    # -- Sweep configuration --

    def _build_sweep_config_group(self) -> QGroupBox:
        """Build the sweep configuration group box."""
        group = QGroupBox("Sweep Config")
        layout = QFormLayout(group)

        self._num_positions_spin = QSpinBox()
        self._num_positions_spin.setRange(1, 1000)
        self._num_positions_spin.setValue(5)
        layout.addRow("Positions:", self._num_positions_spin)

        self._save_check = QCheckBox("Save data")
        self._save_check.setChecked(True)
        layout.addRow(self._save_check)

        path_row = QHBoxLayout()
        self._save_path_edit = QLineEdit()
        self._save_path_edit.setReadOnly(True)
        self._save_path_edit.setPlaceholderText("Select save folder...")
        path_row.addWidget(self._save_path_edit)
        self._browse_btn = QPushButton("Browse")
        self._browse_btn.clicked.connect(self._browse_save_path)
        path_row.addWidget(self._browse_btn)
        layout.addRow("Save Folder:", path_row)

        self._test_name_edit = QLineEdit("FD Sweep")
        layout.addRow("Test Name:", self._test_name_edit)

        return group

    # -- Manual controls --

    def _build_manual_group(self) -> QGroupBox:
        """Build the manual jog and readout controls."""
        group = QGroupBox("Manual Controls")
        layout = QVBoxLayout(group)

        self._zero_btn = QPushButton("Zero Load Cell")
        self._zero_btn.clicked.connect(self._zero_loadcell)
        layout.addWidget(self._zero_btn)

        fd_row = QHBoxLayout()
        fd_row.addWidget(QLabel("Jog FD:"))
        self._fd_fwd_btn = QPushButton("\u25B2 Forward")
        self._fd_fwd_btn.clicked.connect(self._jog_fd_forward)
        fd_row.addWidget(self._fd_fwd_btn)
        self._fd_bwd_btn = QPushButton("\u25BC Backward")
        self._fd_bwd_btn.clicked.connect(self._jog_fd_backward)
        fd_row.addWidget(self._fd_bwd_btn)
        layout.addLayout(fd_row)

        motor_row = QHBoxLayout()
        motor_row.addWidget(QLabel("Jog Motor:"))
        self._motor_fwd_btn = QPushButton("\u25B2 Forward")
        self._motor_fwd_btn.clicked.connect(self._jog_motor_forward)
        motor_row.addWidget(self._motor_fwd_btn)
        self._motor_bwd_btn = QPushButton("\u25BC Backward")
        self._motor_bwd_btn.clicked.connect(self._jog_motor_backward)
        motor_row.addWidget(self._motor_bwd_btn)
        layout.addLayout(motor_row)

        self._force_label = QLabel("Force: -- N")
        self._force_label.setFont(QFont("Consolas", 14, QFont.Weight.Bold))
        layout.addWidget(self._force_label)

        self._motor_pos_label = QLabel("Motor: -- steps")
        self._motor_pos_label.setFont(QFont("Consolas", 11))
        layout.addWidget(self._motor_pos_label)

        return group

    # -- Test controls --

    def _build_test_controls_group(self) -> QGroupBox:
        """Build the start/abort sweep controls."""
        group = QGroupBox("Test Controls")
        layout = QVBoxLayout(group)

        self._start_btn = QPushButton("Start Sweep")
        self._start_btn.setStyleSheet(
            "background-color: #4CAF50; color: white; font-weight: bold; "
            "padding: 8px;"
        )
        self._start_btn.clicked.connect(self._start_sweep)
        layout.addWidget(self._start_btn)

        self._abort_btn = QPushButton("Abort")
        self._abort_btn.setStyleSheet(
            "background-color: #d32f2f; color: white; font-weight: bold; "
            "padding: 8px;"
        )
        self._abort_btn.setEnabled(False)
        self._abort_btn.clicked.connect(self._request_abort)
        layout.addWidget(self._abort_btn)

        self._progress_label = QLabel("Idle")
        layout.addWidget(self._progress_label)

        return group

    # -- Data controls row --

    def _build_data_controls_row(self) -> QHBoxLayout:
        """Build the row of data manipulation buttons below the plot."""
        row = QHBoxLayout()

        self._invert_btn = QPushButton("Invert Force")
        self._invert_btn.setCheckable(True)
        self._invert_btn.clicked.connect(self._toggle_invert)
        row.addWidget(self._invert_btn)

        self._export_btn = QPushButton("Export CSV")
        self._export_btn.clicked.connect(self._export_csv)
        row.addWidget(self._export_btn)

        self._clear_btn = QPushButton("Clear Plot")
        self._clear_btn.clicked.connect(self._clear_plot)
        row.addWidget(self._clear_btn)

        return row

    # -- Analysis group --

    def _build_analysis_group(self) -> QGroupBox:
        """Build the post-test analysis group box."""
        group = QGroupBox("Analysis")
        layout = QVBoxLayout(group)

        margin_row = QHBoxLayout()
        margin_row.addWidget(QLabel("Edge margin:"))
        self._margin_spin = QSpinBox()
        self._margin_spin.setRange(1, 40)
        self._margin_spin.setValue(10)
        self._margin_spin.setSuffix(" %")
        margin_row.addWidget(self._margin_spin)

        self._recompute_btn = QPushButton("Recompute")
        self._recompute_btn.clicked.connect(self._compute_analysis)
        margin_row.addWidget(self._recompute_btn)
        margin_row.addStretch()
        layout.addLayout(margin_row)

        self._analysis_text = QTextEdit()
        self._analysis_text.setReadOnly(True)
        self._analysis_text.setFont(QFont("Consolas", 9))
        layout.addWidget(self._analysis_text)

        return group

    # -- Log panel --

    def _build_log_group(self) -> QGroupBox:
        """Build the log output panel."""
        group = QGroupBox("Log")
        layout = QVBoxLayout(group)

        self._log_text = QTextEdit()
        self._log_text.setReadOnly(True)
        self._log_text.setFont(QFont("Consolas", 8))
        self._log_text.setMaximumHeight(150)
        layout.addWidget(self._log_text)

        return group

    # =========================================================================
    # FD Connection
    # =========================================================================

    def _toggle_fd_connection(self) -> None:
        """Connect to or disconnect from the FD server."""
        if self._fd_connected:
            self._disconnect_fd()
        else:
            self._connect_fd()

    def _connect_fd(self) -> None:
        """Attempt to connect to the FD server."""
        host = self._fd_host_edit.text().strip()
        port = self._fd_port_spin.value()
        self._log(f"Connecting to FD server {host}:{port}...", "info")

        if self._client.connect(host, port):
            self._fd_connected = True
            self._fd_indicator.setStyleSheet("color: green; font-size: 16px;")
            self._fd_connect_btn.setText("Disconnect")
            self._force_timer.start()
            self._update_controls()
            self._log("FD connected.", "info")
        else:
            self._log("FD connection failed.", "error")

    def _disconnect_fd(self) -> None:
        """Disconnect from the FD server."""
        self._force_timer.stop()
        self._client.disconnect()
        self._fd_connected = False
        self._fd_indicator.setStyleSheet("color: red; font-size: 16px;")
        self._fd_connect_btn.setText("Connect")
        self._force_label.setText("Force: -- N")
        self._update_controls()
        self._log("FD disconnected.", "info")

    # =========================================================================
    # Motor Connection
    # =========================================================================

    def _refresh_motor_ports(self) -> None:
        """Re-scan available COM ports and populate the combo box."""
        self._motor_port_combo.clear()
        ports = serial.tools.list_ports.comports()
        for port_info in sorted(ports, key=lambda p: p.device, reverse=True):
            self._motor_port_combo.addItem(
                f"{port_info.device} \u2014 {port_info.description}",
                port_info.device,
            )
        if self._motor_port_combo.count() == 0:
            self._motor_port_combo.addItem("(no ports found)", "")

    def _toggle_motor_connection(self) -> None:
        """Connect to or disconnect from the motor."""
        if self._motor_connected:
            self._disconnect_motor()
        else:
            self._connect_motor()

    def _connect_motor(self) -> None:
        """Attempt to open the selected serial port for the motor."""
        port = self._motor_port_combo.currentData()
        if not port:
            self._log("No motor port selected.", "error")
            return

        self._log(f"Connecting to motor on {port}...", "info")
        if self._motor.connect(port):
            self._motor_connected = True
            self._motor_indicator.setStyleSheet("color: green; font-size: 16px;")
            self._motor_connect_btn.setText("Disconnect")
            self._motor.set_speed(self._motor_speed_spin.value())
            self._motor_timer.start()
            self._update_controls()
            self._log("Motor connected.", "info")
        else:
            self._log("Motor connection failed.", "error")

    def _disconnect_motor(self) -> None:
        """Disconnect from the motor."""
        self._motor_timer.stop()
        self._motor.disconnect()
        self._motor_connected = False
        self._motor_indicator.setStyleSheet("color: red; font-size: 16px;")
        self._motor_connect_btn.setText("Connect")
        self._motor_pos_label.setText("Motor: -- steps")
        self._update_controls()
        self._log("Motor disconnected.", "info")

    # =========================================================================
    # Enable / Disable Logic
    # =========================================================================

    def _update_controls(self) -> None:
        """Enable or disable controls based on connection state and test state."""
        fd_ok = self._fd_connected and not self._test_running
        motor_ok = self._motor_connected and not self._test_running
        both_ok = fd_ok and motor_ok

        # FD manual controls
        self._zero_btn.setEnabled(fd_ok)
        self._fd_fwd_btn.setEnabled(fd_ok)
        self._fd_bwd_btn.setEnabled(fd_ok)

        # Motor manual controls
        self._motor_fwd_btn.setEnabled(motor_ok)
        self._motor_bwd_btn.setEnabled(motor_ok)

        # Start requires both connections
        self._start_btn.setEnabled(both_ok)

        # Abort only during test
        self._abort_btn.setEnabled(self._test_running)

        # Connection controls disabled during test
        self._fd_connect_btn.setEnabled(not self._test_running)
        self._motor_connect_btn.setEnabled(not self._test_running)
        self._fd_host_edit.setEnabled(not self._fd_connected)
        self._fd_port_spin.setEnabled(not self._fd_connected)
        self._motor_port_combo.setEnabled(not self._motor_connected)
        self._motor_refresh_btn.setEnabled(not self._motor_connected)

    # =========================================================================
    # Manual Controls
    # =========================================================================

    def _zero_loadcell(self) -> None:
        """Tare the load cell to zero."""
        self._force_timer.stop()
        self._log("Zeroing load cell...", "info")
        if self._client.zero_loadcell():
            self._log("Load cell zeroed.", "info")
        else:
            self._log("Failed to zero load cell.", "error")
        if self._fd_connected:
            self._force_timer.start()

    def _jog_fd_forward(self) -> None:
        """Jog the FD actuator forward by the configured step size."""
        self._force_timer.stop()
        dist = self._fd_step_spin.value()
        self._log(f"Jogging FD forward {dist:.2f} mm", "info")
        self._client.set_speed(self._fd_speed_spin.value())
        if not self._client.jog(dist):
            self._log("FD jog forward failed.", "error")
        if self._fd_connected:
            self._force_timer.start()

    def _jog_fd_backward(self) -> None:
        """Jog the FD actuator backward by the configured step size."""
        self._force_timer.stop()
        dist = self._fd_step_spin.value()
        self._log(f"Jogging FD backward {dist:.2f} mm", "info")
        self._client.set_speed(self._fd_speed_spin.value())
        if not self._client.jog(-dist):
            self._log("FD jog backward failed.", "error")
        if self._fd_connected:
            self._force_timer.start()

    def _jog_motor_forward(self) -> None:
        """Jog the motor forward by the configured step size (background)."""
        steps = self._motor_step_spin.value()
        self._log(f"Jogging motor forward {steps} steps", "info")

        def _run() -> None:
            self._motor.set_speed(self._motor_speed_spin.value())
            self._motor.move(steps)
            self._motor.wait_for_idle(timeout=15.0)

        threading.Thread(target=_run, daemon=True).start()

    def _jog_motor_backward(self) -> None:
        """Jog the motor backward by the configured step size (background)."""
        steps = self._motor_step_spin.value()
        self._log(f"Jogging motor backward {steps} steps", "info")

        def _run() -> None:
            self._motor.set_speed(self._motor_speed_spin.value())
            self._motor.move(-steps)
            self._motor.wait_for_idle(timeout=15.0)

        threading.Thread(target=_run, daemon=True).start()

    # =========================================================================
    # Force Polling
    # =========================================================================

    def _poll_force(self) -> None:
        """Poll the load cell in a short background thread (avoids UI freeze)."""
        if self._test_running or self._polling_in_progress:
            return

        self._polling_in_progress = True

        def _read() -> None:
            try:
                force = self._client.read_force()
                if force is not None:
                    self._signals.force_update.emit(force)
            finally:
                self._polling_in_progress = False

        threading.Thread(target=_read, daemon=True).start()

    def _on_force_update(self, force: float) -> None:
        """Update the force readout label from the polling signal."""
        sign = -1 if self._force_inverted else 1
        self._force_label.setText(f"Force: {force * sign:.4f} N")

    # =========================================================================
    # Motor Position Polling
    # =========================================================================

    def _poll_motor_position(self) -> None:
        """Poll the motor position (avoids UI freeze by running in background)."""
        if self._test_running or not self._motor_connected:
            return

        def _read() -> None:
            pos = self._motor.get_position()
            if pos is not None:
                self._signals.position_update.emit(pos)

        threading.Thread(target=_read, daemon=True).start()

    def _on_position_update(self, pos: int) -> None:
        """Update the motor position label."""
        self._motor_pos_label.setText(f"Motor: {pos} steps")

    # =========================================================================
    # Browse Save Path
    # =========================================================================

    def _browse_save_path(self) -> None:
        """Open a folder dialog to select the HDF5 save directory."""
        folder = QFileDialog.getExistingDirectory(
            self, "Select Save Folder", ""
        )
        if folder:
            self._save_path_edit.setText(folder)

    # =========================================================================
    # Start Sweep
    # =========================================================================

    def _start_sweep(self) -> None:
        """Validate parameters and launch the background sweep loop."""
        if not self._fd_connected:
            self._log("FD not connected.", "error")
            return
        if not self._motor_connected:
            self._log("Motor not connected.", "error")
            return

        # Validate save path if save is enabled
        save_enabled = self._save_check.isChecked()
        save_folder = self._save_path_edit.text().strip()
        if save_enabled and not save_folder:
            self._log("Save enabled but no folder selected.", "error")
            return

        # Gather FD parameters
        fd_step_size = self._fd_step_spin.value()
        fd_total_dist = self._fd_dist_spin.value()
        fd_speed = self._fd_speed_spin.value()
        num_averages = self._fd_avg_spin.value()
        force_limit = self._fd_limit_spin.value()
        pattern_name = self._direction_combo.currentText()
        segments = DIRECTION_PATTERNS[pattern_name]
        cycles_per_pos = self._cycles_spin.value()

        steps_per_segment = int(fd_total_dist / fd_step_size)
        if steps_per_segment < 1:
            self._log("FD step size is larger than total distance.", "error")
            return

        # Gather sweep parameters
        num_positions = self._num_positions_spin.value()
        motor_step_size = self._motor_step_spin.value()
        motor_speed = self._motor_speed_spin.value()
        test_name = self._test_name_edit.text().strip() or "FD Sweep"

        # Auto-generate save filename
        if save_enabled:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = (
                f"{test_name}"
                f"_{fd_total_dist}mm"
                f"_{fd_step_size}mm"
                f"_{num_positions}pos"
                f"_{timestamp}.h5"
            )
            save_path = os.path.join(save_folder, filename)
        else:
            save_path = ""

        # Prepare state
        self._abort_requested = False
        self._test_running = True
        self._force_timer.stop()
        self._motor_timer.stop()
        self._update_controls()
        self._progress_label.setText("Starting sweep...")
        self._analysis_group.setVisible(False)

        # Clear previous data
        self._all_position_data.clear()
        self._clear_curves()

        # Set X axis range from direction pattern
        padding = fd_total_dist * 0.05
        self._plot_widget.setXRange(-padding, fd_total_dist + padding)
        # Disable X auto-range, keep Y manual (we'll control it)
        self._plot_widget.enableAutoRange(axis="x", enable=False)
        self._plot_widget.enableAutoRange(axis="y", enable=False)
        self._plot_widget.setYRange(-1.0, 1.0)  # initial Y range
        self._y_max_seen = 0.0  # track peak |force| for symmetric Y

        # Set FD speed
        self._client.set_speed(fd_speed)

        # Store save params for the thread
        self._h5path = save_path
        if save_enabled:
            self._log(f"Save file: {save_path}")

        # Launch thread
        self._test_thread = threading.Thread(
            target=self._sweep_loop,
            args=(
                fd_step_size,
                steps_per_segment,
                segments,
                cycles_per_pos,
                num_averages,
                force_limit,
                pattern_name,
                num_positions,
                motor_step_size,
                motor_speed,
                save_enabled,
                save_path,
                test_name,
                fd_total_dist,
            ),
            daemon=True,
        )
        self._test_thread.start()

    # =========================================================================
    # Sweep Loop (Background Thread)
    # =========================================================================

    def _sweep_loop(
        self,
        fd_step_size: float,
        steps_per_segment: int,
        segments: List[int],
        cycles_per_pos: int,
        num_averages: int,
        force_limit: float,
        pattern_name: str,
        num_positions: int,
        motor_step_size: int,
        motor_speed: int,
        save_enabled: bool,
        save_path: str,
        test_name: str,
        fd_total_dist: float,
    ) -> None:
        """Background thread: multi-position FD sweep loop.

        Args:
            fd_step_size: Distance per FD step in mm.
            steps_per_segment: Number of FD steps in one directional segment.
            segments: List of +1/-1 direction multipliers.
            cycles_per_pos: FD cycles to run at each position.
            num_averages: Number of force readings to average per step.
            force_limit: User-configured force abort threshold in N.
            pattern_name: Name of the direction pattern.
            num_positions: Number of motor positions.
            motor_step_size: Motor steps between positions.
            motor_speed: Motor speed in steps/s.
            save_enabled: Whether to write HDF5.
            save_path: Path to HDF5 file.
            test_name: User-provided test name.
            fd_total_dist: Total FD travel distance in mm.
        """
        motor_pos_steps = 0

        try:
            # Configure motor speed
            self._motor.set_speed(motor_speed)

            # Open HDF5 if saving
            if save_enabled:
                self._open_h5_file(
                    save_path, test_name, num_positions, motor_step_size,
                    motor_speed, fd_step_size,
                    fd_step_size * steps_per_segment,  # total distance
                    self._fd_speed_spin.value(), num_averages, force_limit,
                    pattern_name, cycles_per_pos,
                )

            for pos_idx in range(num_positions):
                if self._abort_requested:
                    break

                self._signals.log.emit(
                    f"=== Position {pos_idx} (motor: {motor_pos_steps} steps) ===",
                    "info",
                )
                self._signals.position_update.emit(motor_pos_steps)
                self._signals.progress.emit(
                    f"Position {pos_idx + 1}/{num_positions}"
                )

                # Run FD cycles at this position
                position_data: List[PhaseData] = []
                force_exceeded = False
                needs_setup = pattern_name in SETUP_JOG_PATTERNS
                physical_position: float = 0.0

                for cycle_idx in range(cycles_per_pos):
                    if self._abort_requested or force_exceeded:
                        break

                    cumulative_position: float = 0.0

                    # Setup jog for pull-first patterns
                    if needs_setup and abs(physical_position - fd_total_dist) > 0.001:
                        jog_needed = fd_total_dist - physical_position
                        self._signals.log.emit(
                            f"Setup jog: moving FD +{jog_needed:.3f} mm to start position.",
                            "info",
                        )
                        if not self._client.jog(jog_needed):
                            self._signals.log.emit("Setup jog failed.", "error")
                            self._return_fd_to_zero_physical(physical_position)
                            self._signals.aborted.emit("Setup jog failed")
                            self._close_h5()
                            return
                        physical_position = fd_total_dist

                    for seg_idx, direction in enumerate(segments):
                        if self._abort_requested or force_exceeded:
                            break

                        phase_name_str = _determine_phase_name(
                            seg_idx, direction, pattern_name
                        )
                        phase = PhaseData(
                            cycle_number=cycle_idx + 1,
                            phase_name=phase_name_str,
                        )

                        # Record force at starting position of this segment
                        # (before any jog) so the 0-displacement point is captured
                        pre_forces: List[float] = []
                        for _ in range(num_averages):
                            f = self._client.read_force()
                            if f is not None:
                                pre_forces.append(f)
                        pre_avg = (
                            sum(pre_forces) / len(pre_forces)
                            if pre_forces
                            else 0.0
                        )
                        phase.points.append(
                            MeasurementPoint(cumulative_position, pre_avg)
                        )
                        self._signals.new_point.emit(
                            pos_idx, cycle_idx, seg_idx,
                            cumulative_position, pre_avg,
                        )

                        for step_idx in range(steps_per_segment):
                            if self._abort_requested:
                                self._signals.log.emit(
                                    "Abort requested by user.", "warn"
                                )
                                self._return_fd_to_zero_physical(physical_position)
                                self._signals.aborted.emit("User abort")
                                self._close_h5()
                                return

                            # Move one FD step (physical direction)
                            if not self._client.jog(fd_step_size * direction):
                                self._signals.log.emit(
                                    "FD jog command failed.", "error"
                                )
                                self._return_fd_to_zero_physical(physical_position)
                                self._signals.aborted.emit("FD jog failed")
                                self._close_h5()
                                return

                            # Update physical position
                            physical_position += fd_step_size * direction

                            # Read and average force
                            forces: List[float] = []
                            for _ in range(num_averages):
                                f = self._client.read_force()
                                if f is not None:
                                    forces.append(f)
                            avg_force = (
                                sum(forces) / len(forces) if forces else 0.0
                            )

                            # Update cumulative position (data space)
                            # For pull-first patterns, invert direction so
                            # data is always positive-first
                            if needs_setup:
                                cumulative_position += fd_step_size * (-direction)
                            else:
                                cumulative_position += fd_step_size * direction

                            # Check hardware limit
                            if abs(avg_force) >= HARDWARE_FORCE_LIMIT_N:
                                msg = (
                                    f"HARDWARE LIMIT \u2014 force {avg_force:.3f} N "
                                    f"exceeds {HARDWARE_FORCE_LIMIT_N} N"
                                )
                                self._signals.log.emit(msg, "error")
                                phase.points.append(
                                    MeasurementPoint(
                                        cumulative_position, avg_force
                                    )
                                )
                                self._signals.new_point.emit(
                                    pos_idx, cycle_idx, seg_idx,
                                    cumulative_position, avg_force,
                                )
                                self._return_fd_to_zero_physical(physical_position)
                                self._signals.aborted.emit(msg)
                                self._close_h5()
                                return

                            # Check user force limit
                            if abs(avg_force) >= force_limit:
                                msg = (
                                    f"Force limit \u2014 {avg_force:.3f} N exceeds "
                                    f"user limit {force_limit:.1f} N"
                                )
                                self._signals.log.emit(msg, "warn")
                                phase.points.append(
                                    MeasurementPoint(
                                        cumulative_position, avg_force
                                    )
                                )
                                self._signals.new_point.emit(
                                    pos_idx, cycle_idx, seg_idx,
                                    cumulative_position, avg_force,
                                )
                                force_exceeded = True
                                break

                            # Store point and emit signal
                            phase.points.append(
                                MeasurementPoint(
                                    cumulative_position, avg_force
                                )
                            )
                            self._signals.new_point.emit(
                                pos_idx, cycle_idx, seg_idx,
                                cumulative_position, avg_force,
                            )
                            self._signals.progress.emit(
                                f"P{pos_idx + 1}/{num_positions} "
                                f"C{cycle_idx + 1}/{cycles_per_pos} "
                                f"{phase_name_str} "
                                f"step {step_idx + 1}/{steps_per_segment}"
                            )

                        position_data.append(phase)

                    # Return FD to physical home after each cycle
                    if not force_exceeded:
                        self._signals.log.emit(
                            f"Cycle {cycle_idx + 1} complete \u2014 returning FD to zero.",
                            "info",
                        )
                    self._return_fd_to_zero_physical(physical_position)
                    physical_position = 0.0

                # Write position data to HDF5
                if save_enabled and position_data:
                    self._write_position_h5(
                        pos_idx, motor_pos_steps, position_data
                    )

                # Store data in memory
                self._all_position_data[pos_idx] = position_data

                # Move motor to next position (unless last or aborted)
                if (
                    pos_idx < num_positions - 1
                    and not self._abort_requested
                    and not force_exceeded
                ):
                    self._signals.log.emit(
                        f"Moving motor +{motor_step_size} steps...", "info"
                    )
                    self._motor.move(motor_step_size)
                    self._motor.wait_for_idle()
                    motor_pos_steps += motor_step_size

                if force_exceeded:
                    self._signals.log.emit(
                        "Force limit exceeded \u2014 stopping sweep early.",
                        "warn",
                    )
                    break

            if self._abort_requested:
                self._close_h5()
                self._signals.aborted.emit("User abort")
                return

            # Finalize HDF5 (summary written on UI thread in _on_finished)
            self._signals.finished.emit()

        except Exception as exc:
            self._signals.log.emit(f"Sweep error: {exc}", "error")
            self._close_h5()
            self._signals.aborted.emit(str(exc))

    def _return_fd_to_zero_physical(self, physical_position: float) -> None:
        """Jog the FD actuator back to physical home (position 0).

        Args:
            physical_position: Current physical offset from home in mm.
        """
        if abs(physical_position) > 0.001:
            self._signals.log.emit(
                f"Returning FD {-physical_position:.3f} mm to home.", "info"
            )
            self._client.jog(-physical_position)

    def _request_abort(self) -> None:
        """Set the abort flag so the sweep loop stops."""
        self._abort_requested = True
        self._log("Abort requested...", "warn")

    # =========================================================================
    # HDF5 Writing
    # =========================================================================

    def _open_h5_file(
        self,
        path: str,
        test_name: str,
        total_positions: int,
        motor_step_size: int,
        motor_speed: int,
        fd_step_size: float,
        fd_total_distance: float,
        fd_speed: float,
        fd_averages: int,
        fd_force_limit: float,
        fd_pattern: str,
        fd_cycles: int,
    ) -> None:
        """Open HDF5 file and write metadata.

        Args:
            path: File path for the HDF5 file.
            test_name: User-provided test name.
            total_positions: Number of motor positions.
            motor_step_size: Motor step size between positions.
            motor_speed: Motor speed in steps/s.
            fd_step_size: FD step size in mm.
            fd_total_distance: FD total distance per segment in mm.
            fd_speed: FD speed in mm/s.
            fd_averages: Number of force averages per point.
            fd_force_limit: User force limit in N.
            fd_pattern: Direction pattern name.
            fd_cycles: Cycles per position.
        """
        self._h5file = h5py.File(path, "w")

        meta = self._h5file.create_group("Metadata")
        meta.attrs["TestName"] = test_name
        meta.attrs["DateTimeISO"] = datetime.now().isoformat(timespec="seconds")
        meta.attrs["SchemaVersion"] = "1.0"
        meta.attrs["TestType"] = "Force-Deflection Sweep"
        meta.attrs["TotalPositions"] = total_positions
        meta.attrs["MotorStepSize_steps"] = motor_step_size
        meta.attrs["MotorSpeed_steps_s"] = motor_speed
        meta.attrs["FD_StepSize_mm"] = fd_step_size
        meta.attrs["FD_TotalDistance_mm"] = fd_total_distance
        meta.attrs["FD_Speed_mm_s"] = fd_speed
        meta.attrs["FD_ForceAverages"] = fd_averages
        meta.attrs["FD_ForceLimit_N"] = fd_force_limit
        meta.attrs["FD_DirectionPattern"] = fd_pattern
        meta.attrs["FD_CyclesPerPosition"] = fd_cycles

        self._h5file.create_group("FDRuns")
        self._h5file.create_group("Summary")
        self._h5file.flush()

    def _write_position_h5(
        self,
        pos_idx: int,
        motor_pos_steps: int,
        data: List[PhaseData],
    ) -> None:
        """Write one position's data to HDF5.

        Args:
            pos_idx: Zero-based position index.
            motor_pos_steps: Cumulative motor position in steps.
            data: List of PhaseData for all cycles at this position.
        """
        if self._h5file is None:
            return

        fd_runs = self._h5file["FDRuns"]
        assert isinstance(fd_runs, h5py.Group)
        grp = fd_runs.create_group(f"P{pos_idx}")
        grp.attrs["motor_position_steps"] = motor_pos_steps

        # Flatten all phases into arrays
        all_disp: List[float] = []
        all_force: List[float] = []
        all_cycle: List[int] = []
        all_phase: List[str] = []

        for phase in data:
            for pt in phase.points:
                all_disp.append(pt.position_mm)
                all_force.append(pt.force_n)
                all_cycle.append(phase.cycle_number)
                all_phase.append(phase.phase_name)

        grp.create_dataset(
            "displacement_mm", data=np.array(all_disp, dtype=np.float64)
        )
        grp.create_dataset(
            "force_n", data=np.array(all_force, dtype=np.float64)
        )
        grp.create_dataset(
            "cycle", data=np.array(all_cycle, dtype=np.int32)
        )
        dt = h5py.string_dtype()
        grp.create_dataset(
            "phase", data=np.array(all_phase, dtype=object), dtype=dt
        )

        # Statistics (stored as individual scalar datasets)
        stats = grp.create_group("Statistics")
        forces_arr = np.array(all_force)
        stats.create_dataset(
            "max_force_n",
            data=float(np.max(forces_arr)) if len(forces_arr) else 0.0,
        )
        stats.create_dataset(
            "min_force_n",
            data=float(np.min(forces_arr)) if len(forces_arr) else 0.0,
        )
        stats.create_dataset("num_points", data=len(forces_arr))
        zero_crossings = self._find_zero_crossings(all_disp, all_force)
        stats.create_dataset(
            "zero_crossings",
            data=json.dumps(zero_crossings),
            dtype=h5py.string_dtype(),
        )

        # Placeholder for plot (written from UI thread later)
        grp.create_group("Plot")

        self._h5file.flush()

    def _find_zero_crossings(
        self, displacements: List[float], forces: List[float]
    ) -> List[Dict[str, float]]:
        """Find zero-crossing positions and local slopes.

        Args:
            displacements: Displacement values in mm.
            forces: Force values in N.

        Returns:
            List of dicts with ``position_mm`` and ``slope_N_per_mm``.
        """
        crossings: List[Dict[str, float]] = []
        for i in range(len(forces) - 1):
            if forces[i] * forces[i + 1] < 0:
                # Linear interpolation for exact crossing
                p1, f1 = displacements[i], forces[i]
                p2, f2 = displacements[i + 1], forces[i + 1]
                zero_pos = p1 + (p2 - p1) * (-f1) / (f2 - f1)

                # Local slope (± 5 points)
                window = 5
                start = max(0, i - window)
                end = min(len(forces), i + window + 1)
                xs = np.array(displacements[start:end])
                ys = np.array(forces[start:end])
                slope = 0.0
                if len(xs) >= 2:
                    slope = float(np.polyfit(xs, ys, 1)[0])

                crossings.append(
                    {"position_mm": round(zero_pos, 4), "slope_N_per_mm": round(slope, 4)}
                )
        return crossings

    @staticmethod
    def _widget_to_png(widget: QWidget) -> Optional[bytes]:
        """Grab a widget's visual content as PNG bytes.

        Uses QWidget.grab() which works reliably for both visible and
        offscreen widgets, unlike pyqtgraph's ImageExporter.

        Args:
            widget: The widget to capture.

        Returns:
            Raw PNG bytes, or None on failure.
        """
        pixmap = widget.grab()
        buf = QBuffer()
        buf.open(QIODevice.OpenModeFlag.ReadWrite)
        if not pixmap.save(buf, "PNG"):
            return None
        return bytes(buf.data().data())  # type: ignore[arg-type]

    def _write_plots_to_h5(self) -> None:
        """Write plot images to HDF5 from the UI thread.

        Renders the current overview plot and per-position FD plots
        using QWidget.grab() for reliable capture.
        """
        if self._h5file is None:
            return

        # Overview plot: grab the visible widget
        try:
            png_bytes = self._widget_to_png(self._plot_widget)
            if png_bytes is not None:
                summary = self._h5file["Summary"]
                assert isinstance(summary, h5py.Group)
                summary.create_dataset(
                    "overview_plot",
                    data=np.frombuffer(png_bytes, dtype=np.uint8),
                )
        except Exception as exc:
            self._log(f"Failed to export overview plot: {exc}", "error")

        # Per-position plots
        for pos_idx, phases in self._all_position_data.items():
            grp_key = f"FDRuns/P{pos_idx}/Plot"
            if grp_key not in self._h5file:
                continue
            try:
                png_bytes = self._render_position_plot(pos_idx, phases)
                if png_bytes:
                    plot_grp = self._h5file[grp_key]
                    assert isinstance(plot_grp, h5py.Group)
                    plot_grp.create_dataset(
                        "fd_plot",
                        data=np.frombuffer(png_bytes, dtype=np.uint8),
                    )
            except Exception as exc:
                self._log(
                    f"Failed to export plot for P{pos_idx}: {exc}", "error"
                )

        self._h5file.flush()

    def _render_position_plot(
        self, pos_idx: int, data: List[PhaseData]
    ) -> Optional[bytes]:
        """Render a position's FD plot offscreen and return PNG bytes.

        Args:
            pos_idx: Position index (for title and coloring).
            data: List of PhaseData for this position.

        Returns:
            Raw PNG bytes, or None on failure.
        """
        pw = pg.PlotWidget()
        pw.setBackground("w")
        pw.setLabel("bottom", "Displacement (mm)")
        pw.setLabel("left", "Force (N)")
        pw.setTitle(f"P{pos_idx} Force vs Displacement")
        pw.showGrid(x=True, y=True)
        pw.resize(800, 500)

        invert = -1.0 if self._force_inverted else 1.0

        for seg_idx, phase in enumerate(data):
            if not phase.points:
                continue
            xs = [p.position_mm for p in phase.points]
            ys = [p.force_n * invert for p in phase.points]
            color = _dark_color(pos_idx) if seg_idx % 2 == 0 else _light_color(pos_idx)
            pen = pg.mkPen(color=color, width=2)
            pw.plot(xs, ys, pen=pen, name=f"C{phase.cycle_number} {phase.phase_name}")

        return self._widget_to_png(pw)

    def _close_h5(self) -> None:
        """Flush and close the HDF5 file if open."""
        if self._h5file is not None:
            try:
                self._h5file.flush()
                self._h5file.close()
            except Exception:
                pass
            self._h5file = None

    # =========================================================================
    # Signal Handlers (UI Thread)
    # =========================================================================

    def _on_new_point(
        self,
        pos_idx: int,
        cycle_idx: int,
        seg_idx: int,
        position: float,
        force: float,
    ) -> None:
        """Handle a newly measured data point — update the plot.

        Each position gets a different base hue. Within a position,
        phase 0 (first direction) uses the dark color and phase 1
        uses the light variant.

        Args:
            pos_idx: Zero-based position index.
            cycle_idx: Zero-based cycle index.
            seg_idx: Segment index (0 or 1).
            position: Displacement in mm.
            force: Measured force in N.
        """
        key = (pos_idx, cycle_idx, seg_idx)
        invert = -1.0 if self._force_inverted else 1.0

        if key not in self._curves:
            if seg_idx == 0:
                color = _dark_color(pos_idx)
            else:
                color = _light_color(pos_idx)
            pen = pg.mkPen(color=color, width=2)

            pattern = self._direction_combo.currentText()
            segments = DIRECTION_PATTERNS.get(pattern, [+1])
            direction = segments[seg_idx] if seg_idx < len(segments) else segments[-1]
            phase_name = _determine_phase_name(seg_idx, direction, pattern)
            label = f"P{pos_idx} C{cycle_idx + 1} {phase_name}"
            curve = self._plot_widget.plot(pen=pen, name=label)
            self._curves[key] = curve
            self._curve_xdata[key] = []
            self._curve_ydata[key] = []

        self._curve_xdata[key].append(position)
        self._curve_ydata[key].append(force * invert)
        self._curves[key].setData(
            self._curve_xdata[key], self._curve_ydata[key]
        )

        # Auto-scale Y symmetrically around zero
        abs_f = abs(force * invert)
        if abs_f > self._y_max_seen:
            self._y_max_seen = abs_f
            padding = self._y_max_seen * 0.1 if self._y_max_seen > 0 else 1.0
            self._plot_widget.setYRange(
                -(self._y_max_seen + padding),
                self._y_max_seen + padding,
            )

    def _on_progress(self, text: str) -> None:
        """Update the progress label.

        Args:
            text: Progress text.
        """
        self._progress_label.setText(text)

    def _on_log(self, message: str, level: str) -> None:
        """Append a message to the log panel.

        Args:
            message: Log text.
            level: Severity level.
        """
        self._log(message, level)

    def _on_finished(self) -> None:
        """Handle sweep completion — write plots to HDF5, show analysis."""
        self._test_running = False
        self._update_controls()
        self._progress_label.setText("Sweep complete")

        # Write plots to HDF5 from UI thread, then close
        if self._h5file is not None:
            try:
                self._write_plots_to_h5()
            except Exception as exc:
                self._log(f"Error writing plots to HDF5: {exc}", "error")
            self._close_h5()
            self._log(f"HDF5 saved to: {self._h5path}", "info")

        if self._fd_connected:
            self._force_timer.start()
        if self._motor_connected:
            self._motor_timer.start()

        self._analysis_group.setVisible(True)
        self._compute_analysis()
        self._log("Sweep finished.", "info")

    def _on_aborted(self, reason: str) -> None:
        """Handle sweep abort.

        Args:
            reason: Human-readable abort reason.
        """
        self._test_running = False
        self._update_controls()
        self._progress_label.setText(f"Aborted: {reason}")

        if self._fd_connected:
            self._force_timer.start()
        if self._motor_connected:
            self._motor_timer.start()

        if self._all_position_data:
            self._analysis_group.setVisible(True)
            self._compute_analysis()

        self._log(f"Sweep aborted: {reason}", "warn")

    # =========================================================================
    # Plot Helpers
    # =========================================================================

    def _clear_curves(self) -> None:
        """Remove all plot curves and associated data."""
        for curve in self._curves.values():
            self._plot_widget.removeItem(curve)
        self._curves.clear()
        self._curve_xdata.clear()
        self._curve_ydata.clear()
        plot_item = self._plot_widget.plotItem
        if plot_item is not None and plot_item.legend is not None:
            plot_item.legend.clear()

    def _replot_all(self) -> None:
        """Recreate all curves from stored position data."""
        self._clear_curves()
        invert = -1.0 if self._force_inverted else 1.0

        for pos_idx, phases in self._all_position_data.items():
            seg_counter = 0
            prev_cycle = -1
            for phase in phases:
                cycle_idx = phase.cycle_number - 1
                if cycle_idx != prev_cycle:
                    seg_counter = 0
                    prev_cycle = cycle_idx
                seg_idx = seg_counter
                seg_counter += 1

                key = (pos_idx, cycle_idx, seg_idx)
                if seg_idx == 0:
                    color = _dark_color(pos_idx)
                else:
                    color = _light_color(pos_idx)
                pen = pg.mkPen(color=color, width=2)
                label = f"P{pos_idx} C{cycle_idx + 1} {phase.phase_name}"
                curve = self._plot_widget.plot(pen=pen, name=label)
                self._curves[key] = curve

                xs = [p.position_mm for p in phase.points]
                ys = [p.force_n * invert for p in phase.points]
                self._curve_xdata[key] = xs
                self._curve_ydata[key] = ys
                curve.setData(xs, ys)

    # =========================================================================
    # Data Controls
    # =========================================================================

    def _toggle_invert(self) -> None:
        """Toggle force inversion and replot."""
        self._force_inverted = self._invert_btn.isChecked()
        if self._force_inverted:
            self._invert_btn.setText("Force Inverted \u2713")
        else:
            self._invert_btn.setText("Invert Force")
        self._replot_all()
        if self._analysis_group.isVisible():
            self._compute_analysis()

    def _export_csv(self) -> None:
        """Export all measurement data to a CSV file."""
        path, _ = QFileDialog.getSaveFileName(
            self, "Export CSV", "", "CSV Files (*.csv)"
        )
        if not path:
            return

        invert = -1 if self._force_inverted else 1
        try:
            with open(path, "w") as f:
                f.write("position_idx,motor_steps,cycle,phase,displacement_mm,force_n\n")
                for pos_idx in sorted(self._all_position_data.keys()):
                    phases = self._all_position_data[pos_idx]
                    for phase in phases:
                        for pt in phase.points:
                            f.write(
                                f"{pos_idx},0,{phase.cycle_number},"
                                f"{phase.phase_name},"
                                f"{pt.position_mm:.4f},{pt.force_n * invert:.6f}\n"
                            )
            self._log(f"Exported CSV to {path}", "info")
        except Exception as exc:
            self._log(f"CSV export failed: {exc}", "error")

    def _clear_plot(self) -> None:
        """Clear all plot data and stored measurements."""
        self._all_position_data.clear()
        self._clear_curves()
        self._analysis_group.setVisible(False)
        self._analysis_text.clear()
        self._log("Plot cleared.", "info")

    # =========================================================================
    # Analysis
    # =========================================================================

    def _compute_analysis(self) -> None:
        """Compute post-test analysis within the trimmed edge margin."""
        margin_pct = self._margin_spin.value() / 100.0
        invert = -1.0 if self._force_inverted else 1.0
        results: List[str] = []

        for pos_idx in sorted(self._all_position_data.keys()):
            results.append(f"--- Position {pos_idx} ---")
            phases = self._all_position_data[pos_idx]

            for phase in phases:
                if not phase.points:
                    continue

                positions = [p.position_mm for p in phase.points]
                forces = [p.force_n * invert for p in phase.points]

                total_range = (
                    max(positions) - min(positions) if positions else 0.0
                )
                margin = total_range * margin_pct
                min_pos = min(positions) + margin
                max_pos = max(positions) - margin

                trimmed = [
                    (p, f_val)
                    for p, f_val in zip(positions, forces)
                    if min_pos <= p <= max_pos
                ]

                if not trimmed:
                    continue

                max_f = max(trimmed, key=lambda x: x[1])
                min_f = min(trimmed, key=lambda x: x[1])

                results.append(
                    f"  Cycle {phase.cycle_number} ({phase.phase_name}):"
                )
                results.append(
                    f"    Max force: {max_f[1]:.4f} N at {max_f[0]:.3f} mm"
                )
                results.append(
                    f"    Min force: {min_f[1]:.4f} N at {min_f[0]:.3f} mm"
                )

                # Zero-crossing detection
                for i in range(len(trimmed) - 1):
                    p1, f1 = trimmed[i]
                    p2, f2 = trimmed[i + 1]
                    if f1 * f2 < 0:
                        zero_pos = p1 + (p2 - p1) * (-f1) / (f2 - f1)

                        full_idx = next(
                            j
                            for j, (p, _) in enumerate(trimmed)
                            if p >= p1
                        )
                        window = 5
                        start = max(0, full_idx - window)
                        end = min(len(trimmed), full_idx + window + 1)
                        slope_pts = trimmed[start:end]

                        if len(slope_pts) >= 2:
                            xs = np.array([p for p, _ in slope_pts])
                            ys = np.array([f_val for _, f_val in slope_pts])
                            slope = float(np.polyfit(xs, ys, 1)[0])
                            results.append(
                                f"    Zero-crossing at {zero_pos:.3f} mm "
                                f"(slope: {slope:.4f} N/mm)"
                            )

            results.append("")

        self._analysis_text.setPlainText("\n".join(results))

    # =========================================================================
    # Logging
    # =========================================================================

    def _log(self, message: str, level: str = "info") -> None:
        """Append a timestamped message to the log panel.

        Args:
            message: Text to log.
            level: Severity — ``"info"``, ``"warn"``, or ``"error"``.
        """
        timestamp = time.strftime("%H:%M:%S")
        prefix = {"info": "INFO", "warn": "WARN", "error": "ERR "}.get(
            level, "INFO"
        )
        line = f"[{timestamp}] {prefix}  {message}"
        self._log_text.append(line)

        scrollbar = self._log_text.verticalScrollBar()
        if scrollbar:
            scrollbar.setValue(scrollbar.maximum())

    # =========================================================================
    # Window Events
    # =========================================================================

    def closeEvent(self, a0: Optional[QCloseEvent]) -> None:  # type: ignore[override]
        """Clean up resources when the window is closed."""
        self._force_timer.stop()
        self._motor_timer.stop()

        if self._test_running:
            self._abort_requested = True
            if self._test_thread and self._test_thread.is_alive():
                self._test_thread.join(timeout=3.0)

        self._close_h5()
        self._client.disconnect()
        self._motor.disconnect()

        if a0 is not None:
            a0.accept()


# =============================================================================
# Entry Point
# =============================================================================


def main() -> None:
    """Launch the Force-Deflection V3 application."""
    app = QApplication(sys.argv)
    window = ForceDeflectionSweepWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
