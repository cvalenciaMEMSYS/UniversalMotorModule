"""
Force-Deflection V2 — Quasi-Static Measurement Tool

Performs step-measure-step force-displacement measurements on buckled beams
and similar structures. Supports push, pull, push-pull, and pull-push patterns
with multiple cycles for hysteresis characterization.

Prerequisites:
    1. Install dependencies:
       pip install PyQt6 pyqtgraph numpy pyserial

    2. On the Raspberry Pi connected to the FD Arduino, start the server:
       python fd_server_nojs.py --serial /dev/ttyACM0

Usage:
    python ForceDeflection_V2.py
"""

import sys
import threading
import time
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Tuple

import numpy as np
import pyqtgraph as pg
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QSizePolicy,
    QSpinBox,
    QSplitter,
    QTextEdit,
    QVBoxLayout,
    QWidget,
    QLineEdit,
)

from fd_client_nojs import FDClientNoJS

# =============================================================================
# Constants
# =============================================================================

HARDWARE_FORCE_LIMIT_N = 100.0

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
# Signal Bridge
# =============================================================================


class SignalBridge(QObject):
    """Thread-safe signal bridge for background measurement loop."""

    new_point = pyqtSignal(int, int, float, float)   # cycle_idx, seg_idx, pos, force
    progress = pyqtSignal(int, int, int, int, str)    # step, total, cycle, total_c, phase
    log = pyqtSignal(str, str)                        # message, level
    finished = pyqtSignal()
    aborted = pyqtSignal(str)                         # reason
    force_update = pyqtSignal(float)                  # live force polling


# =============================================================================
# Helpers
# =============================================================================


def _dark_color(cycle_idx: int) -> QColor:
    """Return the base (dark) color for the given cycle index."""
    r, g, b = BASE_HUES[cycle_idx % len(BASE_HUES)]
    return QColor(r, g, b)


def _light_color(cycle_idx: int, blend: float = 0.40) -> QColor:
    """Return a lightened variant of the base color (blended toward white)."""
    r, g, b = BASE_HUES[cycle_idx % len(BASE_HUES)]
    lr = int(r + (255 - r) * blend)
    lg = int(g + (255 - g) * blend)
    lb = int(b + (255 - b) * blend)
    return QColor(lr, lg, lb)


# =============================================================================
# Main Window
# =============================================================================


class ForceDeflectionWindow(QMainWindow):
    """Main window for Force-Deflection V2 quasi-static measurement tool."""

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Force-Deflection V2 — Quasi-Static Measurement")
        self.resize(1200, 800)

        # State
        self._client = FDClientNoJS()
        self._connected = False
        self._test_running = False
        self._abort_requested = False
        self._experiment_data: List[PhaseData] = []
        self._force_inverted: bool = False
        self._curves: Dict[Tuple[int, int], pg.PlotDataItem] = {}
        self._curve_xdata: Dict[Tuple[int, int], List[float]] = {}
        self._curve_ydata: Dict[Tuple[int, int], List[float]] = {}
        self._test_thread: Optional[threading.Thread] = None
        self._polling_in_progress: bool = False

        # Signal bridge
        self._signals = SignalBridge()
        self._signals.new_point.connect(self._on_new_point)
        self._signals.progress.connect(self._on_progress)
        self._signals.log.connect(self._on_log)
        self._signals.finished.connect(self._on_finished)
        self._signals.aborted.connect(self._on_aborted)
        self._signals.force_update.connect(self._on_force_update)

        # Force polling timer
        self._force_timer = QTimer()
        self._force_timer.setInterval(500)
        self._force_timer.timeout.connect(self._poll_force)

        self._build_ui()
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
        left_widget.setMaximumWidth(400)

        left_layout.addWidget(self._build_connection_group())
        left_layout.addWidget(self._build_params_group())
        left_layout.addWidget(self._build_cycle_group())
        left_layout.addWidget(self._build_manual_group())
        left_layout.addWidget(self._build_test_controls_group())
        left_layout.addStretch()

        splitter.addWidget(left_widget)

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

    # -- Connection group --

    def _build_connection_group(self) -> QGroupBox:
        """Build the connection controls group box."""
        group = QGroupBox("Connection")
        layout = QGridLayout(group)

        layout.addWidget(QLabel("FD Server:"), 0, 0)
        self._host_edit = QLineEdit("192.168.1.12")
        layout.addWidget(self._host_edit, 0, 1)

        self._port_spin = QSpinBox()
        self._port_spin.setRange(1, 65535)
        self._port_spin.setValue(5002)
        layout.addWidget(self._port_spin, 0, 2)

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.clicked.connect(self._toggle_connection)
        layout.addWidget(self._connect_btn, 0, 3)

        self._conn_indicator = QLabel("●")
        self._conn_indicator.setStyleSheet("color: red; font-size: 16px;")
        layout.addWidget(self._conn_indicator, 0, 4)

        return group

    # -- Measurement parameters --

    def _build_params_group(self) -> QGroupBox:
        """Build the measurement parameters group box."""
        group = QGroupBox("Measurement Parameters")
        layout = QFormLayout(group)

        self._step_size_spin = QDoubleSpinBox()
        self._step_size_spin.setRange(0.01, 5.0)
        self._step_size_spin.setValue(0.1)
        self._step_size_spin.setSuffix(" mm")
        self._step_size_spin.setDecimals(2)
        layout.addRow("Step Size:", self._step_size_spin)

        self._total_dist_spin = QDoubleSpinBox()
        self._total_dist_spin.setRange(0.1, 50.0)
        self._total_dist_spin.setValue(5.0)
        self._total_dist_spin.setSuffix(" mm")
        self._total_dist_spin.setDecimals(1)
        layout.addRow("Total Distance:", self._total_dist_spin)

        self._speed_spin = QDoubleSpinBox()
        self._speed_spin.setRange(0.1, 25.0)
        self._speed_spin.setValue(1.0)
        self._speed_spin.setSuffix(" mm/s")
        self._speed_spin.setDecimals(1)
        layout.addRow("Move Speed:", self._speed_spin)

        self._averages_spin = QSpinBox()
        self._averages_spin.setRange(1, 20)
        self._averages_spin.setValue(3)
        self._averages_spin.setSuffix(" samples")
        layout.addRow("Force Averages:", self._averages_spin)

        self._force_limit_spin = QDoubleSpinBox()
        self._force_limit_spin.setRange(0.1, 100.0)
        self._force_limit_spin.setValue(50.0)
        self._force_limit_spin.setSuffix(" N")
        self._force_limit_spin.setDecimals(1)
        layout.addRow("Force Limit:", self._force_limit_spin)

        hw_label = QLabel("Hardware limit: 100 N (load cell max)")
        hw_label.setStyleSheet("color: #e09000; font-weight: bold;")
        layout.addRow(hw_label)

        return group

    # -- Cycle configuration --

    def _build_cycle_group(self) -> QGroupBox:
        """Build the cycle configuration group box."""
        group = QGroupBox("Cycle Configuration")
        layout = QFormLayout(group)

        self._direction_combo = QComboBox()
        self._direction_combo.addItems(list(DIRECTION_PATTERNS.keys()))
        self._direction_combo.setCurrentText("Push only")
        layout.addRow("Direction Pattern:", self._direction_combo)

        self._cycles_spin = QSpinBox()
        self._cycles_spin.setRange(1, 100)
        self._cycles_spin.setValue(1)
        layout.addRow("Number of Cycles:", self._cycles_spin)

        return group

    # -- Manual controls --

    def _build_manual_group(self) -> QGroupBox:
        """Build the manual jog and force readout controls."""
        group = QGroupBox("Manual Controls")
        layout = QVBoxLayout(group)

        self._zero_btn = QPushButton("Zero Load Cell")
        self._zero_btn.setEnabled(False)
        self._zero_btn.clicked.connect(self._zero_loadcell)
        layout.addWidget(self._zero_btn)

        self._fwd_btn = QPushButton("▲ Forward")
        self._fwd_btn.setEnabled(False)
        self._fwd_btn.clicked.connect(self._jog_forward)
        layout.addWidget(self._fwd_btn)

        self._bwd_btn = QPushButton("▼ Backward")
        self._bwd_btn.setEnabled(False)
        self._bwd_btn.clicked.connect(self._jog_backward)
        layout.addWidget(self._bwd_btn)

        self._force_label = QLabel("Force: -- N")
        self._force_label.setFont(QFont("Consolas", 14, QFont.Weight.Bold))
        layout.addWidget(self._force_label)

        return group

    # -- Test controls --

    def _build_test_controls_group(self) -> QGroupBox:
        """Build the start/abort test controls."""
        group = QGroupBox("Test Controls")
        layout = QVBoxLayout(group)

        self._start_btn = QPushButton("Start Measurement")
        self._start_btn.setStyleSheet(
            "background-color: #4CAF50; color: white; font-weight: bold; "
            "padding: 8px;"
        )
        self._start_btn.setEnabled(False)
        self._start_btn.clicked.connect(self._start_measurement)
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
    # Connection
    # =========================================================================

    def _toggle_connection(self) -> None:
        """Connect to or disconnect from the FD server."""
        if self._connected:
            self._disconnect()
        else:
            self._do_connect()

    def _do_connect(self) -> None:
        """Attempt to connect to the FD server."""
        host = self._host_edit.text().strip()
        port = self._port_spin.value()
        self._log(f"Connecting to {host}:{port}...", "info")

        if self._client.connect(host, port):
            self._connected = True
            self._conn_indicator.setStyleSheet("color: green; font-size: 16px;")
            self._connect_btn.setText("Disconnect")
            self._set_controls_enabled(True)
            self._force_timer.start()
            self._log("Connected.", "info")
        else:
            self._log("Connection failed.", "error")

    def _disconnect(self) -> None:
        """Disconnect from the FD server."""
        self._force_timer.stop()
        self._client.disconnect()
        self._connected = False
        self._conn_indicator.setStyleSheet("color: red; font-size: 16px;")
        self._connect_btn.setText("Connect")
        self._set_controls_enabled(False)
        self._force_label.setText("Force: -- N")
        self._log("Disconnected.", "info")

    def _set_controls_enabled(self, enabled: bool) -> None:
        """Enable or disable controls that require a connection."""
        self._zero_btn.setEnabled(enabled)
        self._fwd_btn.setEnabled(enabled)
        self._bwd_btn.setEnabled(enabled)
        self._start_btn.setEnabled(enabled)

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
        self._force_timer.start()

    def _jog_forward(self) -> None:
        """Jog the motor forward by the configured step size."""
        self._force_timer.stop()
        dist = self._step_size_spin.value()
        self._log(f"Jogging forward {dist:.2f} mm", "info")
        self._client.set_speed(self._speed_spin.value())
        if not self._client.jog(dist):
            self._log("Jog forward failed.", "error")
        self._force_timer.start()

    def _jog_backward(self) -> None:
        """Jog the motor backward by the configured step size."""
        self._force_timer.stop()
        dist = self._step_size_spin.value()
        self._log(f"Jogging backward {dist:.2f} mm", "info")
        self._client.set_speed(self._speed_spin.value())
        if not self._client.jog(-dist):
            self._log("Jog backward failed.", "error")
        self._force_timer.start()

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
    # Measurement
    # =========================================================================

    def _start_measurement(self) -> None:
        """Validate parameters and launch the background measurement loop."""
        if not self._connected:
            self._log("Not connected.", "error")
            return

        # Gather parameters
        step_size = self._step_size_spin.value()
        total_distance = self._total_dist_spin.value()
        speed = self._speed_spin.value()
        num_averages = self._averages_spin.value()
        force_limit = self._force_limit_spin.value()
        pattern_name = self._direction_combo.currentText()
        segments = DIRECTION_PATTERNS[pattern_name]
        num_cycles = self._cycles_spin.value()

        steps_per_segment = int(total_distance / step_size)
        if steps_per_segment < 1:
            self._log("Step size is larger than total distance.", "error")
            return

        # Prepare state
        self._abort_requested = False
        self._test_running = True
        self._force_timer.stop()
        self._start_btn.setEnabled(False)
        self._abort_btn.setEnabled(True)
        self._set_controls_enabled(False)
        self._progress_label.setText("Starting...")
        self._analysis_group.setVisible(False)

        # Clear previous data
        self._experiment_data.clear()
        self._clear_curves()

        # Set speed on server
        self._client.set_speed(speed)

        # Launch thread
        self._test_thread = threading.Thread(
            target=self._measurement_loop,
            args=(
                step_size,
                steps_per_segment,
                segments,
                num_cycles,
                num_averages,
                force_limit,
            ),
            daemon=True,
        )
        self._test_thread.start()

    def _measurement_loop(
        self,
        step_size: float,
        steps_per_segment: int,
        segments: List[int],
        num_cycles: int,
        num_averages: int,
        force_limit: float,
    ) -> None:
        """Background thread: step-measure-step quasi-static loop.

        Args:
            step_size: Distance per step in mm.
            steps_per_segment: Number of steps in one directional segment.
            segments: List of +1/-1 direction multipliers.
            num_cycles: How many full cycles to run.
            num_averages: Number of force readings to average per step.
            force_limit: User-configured force abort threshold in N.
        """
        cumulative_position: float = 0.0

        try:
            for cycle_idx in range(num_cycles):
                for seg_idx, direction in enumerate(segments):
                    phase_name = "push" if direction > 0 else "pull"
                    phase = PhaseData(
                        cycle_number=cycle_idx + 1,
                        phase_name=phase_name,
                    )
                    self._experiment_data.append(phase)

                    for step_idx in range(steps_per_segment):
                        if self._abort_requested:
                            self._signals.log.emit("Abort requested by user.", "warn")
                            self._return_to_zero(cumulative_position)
                            self._signals.aborted.emit("User abort")
                            return

                        # Move one step
                        if not self._client.jog(step_size * direction):
                            self._signals.log.emit("Jog command failed.", "error")
                            self._return_to_zero(cumulative_position)
                            self._signals.aborted.emit("Jog failed")
                            return

                        # Read and average force
                        forces: List[float] = []
                        for _ in range(num_averages):
                            f = self._client.read_force()
                            if f is not None:
                                forces.append(f)
                        avg_force = (
                            sum(forces) / len(forces) if forces else 0.0
                        )

                        cumulative_position += step_size * direction

                        # Check hardware limit
                        if abs(avg_force) >= HARDWARE_FORCE_LIMIT_N:
                            msg = (
                                f"HARDWARE LIMIT — force {avg_force:.3f} N "
                                f"exceeds {HARDWARE_FORCE_LIMIT_N} N"
                            )
                            self._signals.log.emit(msg, "error")
                            phase.points.append(
                                MeasurementPoint(cumulative_position, avg_force)
                            )
                            self._signals.new_point.emit(
                                cycle_idx, seg_idx,
                                cumulative_position, avg_force,
                            )
                            self._return_to_zero(cumulative_position)
                            self._signals.aborted.emit(msg)
                            return

                        # Check user force limit
                        if abs(avg_force) >= force_limit:
                            msg = (
                                f"Force limit — {avg_force:.3f} N exceeds "
                                f"user limit {force_limit:.1f} N"
                            )
                            self._signals.log.emit(msg, "warn")
                            phase.points.append(
                                MeasurementPoint(cumulative_position, avg_force)
                            )
                            self._signals.new_point.emit(
                                cycle_idx, seg_idx,
                                cumulative_position, avg_force,
                            )
                            self._return_to_zero(cumulative_position)
                            self._signals.aborted.emit(msg)
                            return

                        # Store point and emit signal
                        phase.points.append(
                            MeasurementPoint(cumulative_position, avg_force)
                        )
                        self._signals.new_point.emit(
                            cycle_idx, seg_idx,
                            cumulative_position, avg_force,
                        )
                        self._signals.progress.emit(
                            step_idx + 1,
                            steps_per_segment,
                            cycle_idx + 1,
                            num_cycles,
                            phase_name,
                        )

                # Return to zero after each cycle
                self._signals.log.emit(
                    f"Cycle {cycle_idx + 1} complete — returning to zero.", "info"
                )
                self._return_to_zero(cumulative_position)
                cumulative_position = 0.0

            self._signals.finished.emit()

        except Exception as exc:
            self._signals.log.emit(f"Measurement error: {exc}", "error")
            self._return_to_zero(cumulative_position)
            self._signals.aborted.emit(str(exc))

    def _return_to_zero(self, cumulative_position: float) -> None:
        """Jog back to the starting position.

        Args:
            cumulative_position: Current offset from origin in mm.
        """
        if abs(cumulative_position) > 0.001:
            self._signals.log.emit(
                f"Returning {-cumulative_position:.3f} mm to origin.", "info"
            )
            self._client.jog(-cumulative_position)

    def _request_abort(self) -> None:
        """Set the abort flag so the measurement loop stops."""
        self._abort_requested = True
        self._log("Abort requested...", "warn")

    # =========================================================================
    # Signal Handlers (UI thread)
    # =========================================================================

    def _on_new_point(
        self,
        cycle_idx: int,
        seg_idx: int,
        position: float,
        force: float,
    ) -> None:
        """Handle a newly measured data point — update the plot.

        Args:
            cycle_idx: Zero-based cycle index.
            seg_idx: Segment index within the cycle (0 or 1).
            position: Displacement in mm.
            force: Measured force in N.
        """
        key = (cycle_idx, seg_idx)
        invert = -1.0 if self._force_inverted else 1.0

        if key not in self._curves:
            # Determine color
            if seg_idx == 0:
                color = _dark_color(cycle_idx)
            else:
                color = _light_color(cycle_idx)
            pen = pg.mkPen(color=color, width=2)

            phase_name = "push" if seg_idx == 0 else "pull"
            # Adjust label for single-direction patterns
            pattern = self._direction_combo.currentText()
            if pattern == "Pull only":
                phase_name = "pull"
            elif pattern == "Push only":
                phase_name = "push"
            elif pattern == "Pull-Push":
                phase_name = "pull" if seg_idx == 0 else "push"

            label = f"C{cycle_idx + 1} {phase_name}"
            curve = self._plot_widget.plot(pen=pen, name=label)
            self._curves[key] = curve
            self._curve_xdata[key] = []
            self._curve_ydata[key] = []

        self._curve_xdata[key].append(position)
        self._curve_ydata[key].append(force * invert)
        self._curves[key].setData(
            self._curve_xdata[key], self._curve_ydata[key]
        )

    def _on_progress(
        self,
        step: int,
        total_steps: int,
        cycle: int,
        total_cycles: int,
        phase: str,
    ) -> None:
        """Update the progress label.

        Args:
            step: Current step number (1-based).
            total_steps: Total steps in this segment.
            cycle: Current cycle number (1-based).
            total_cycles: Total configured cycles.
            phase: Phase name ("push" or "pull").
        """
        self._progress_label.setText(
            f"Step {step}/{total_steps} | Cycle {cycle}/{total_cycles} "
            f"| {phase} phase"
        )

    def _on_log(self, message: str, level: str) -> None:
        """Append a message to the log panel.

        Args:
            message: Log text.
            level: Severity ("info", "warn", "error").
        """
        self._log(message, level)

    def _on_finished(self) -> None:
        """Handle measurement completion."""
        self._test_running = False
        self._abort_btn.setEnabled(False)
        self._set_controls_enabled(True)
        self._start_btn.setEnabled(True)
        self._progress_label.setText("Measurement complete")
        self._force_timer.start()
        self._analysis_group.setVisible(True)
        self._compute_analysis()
        self._log("Measurement finished.", "info")

    def _on_aborted(self, reason: str) -> None:
        """Handle measurement abort.

        Args:
            reason: Human-readable abort reason.
        """
        self._test_running = False
        self._abort_btn.setEnabled(False)
        self._set_controls_enabled(True)
        self._start_btn.setEnabled(True)
        self._progress_label.setText(f"Aborted: {reason}")
        self._force_timer.start()
        if self._experiment_data:
            self._analysis_group.setVisible(True)
            self._compute_analysis()
        self._log(f"Measurement aborted: {reason}", "warn")

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
        # Clear legend by rebuilding it
        plot_item = self._plot_widget.plotItem  # type: ignore[union-attr]
        if plot_item is not None and plot_item.legend is not None:
            plot_item.legend.clear()

    def _replot_all(self) -> None:
        """Recreate all curves from stored experiment data."""
        self._clear_curves()
        invert = -1.0 if self._force_inverted else 1.0

        # Rebuild curve mapping from experiment data
        seg_counter: Dict[int, int] = {}  # cycle_number -> next seg_idx
        for phase in self._experiment_data:
            cycle_idx = phase.cycle_number - 1
            seg_idx = seg_counter.get(cycle_idx, 0)
            seg_counter[cycle_idx] = seg_idx + 1

            key = (cycle_idx, seg_idx)
            if seg_idx == 0:
                color = _dark_color(cycle_idx)
            else:
                color = _light_color(cycle_idx)
            pen = pg.mkPen(color=color, width=2)
            label = f"C{cycle_idx + 1} {phase.phase_name}"
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
            self._invert_btn.setText("Force Inverted ✓")
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
                f.write("cycle,phase,position_mm,force_n\n")
                for phase in self._experiment_data:
                    for pt in phase.points:
                        f.write(
                            f"{phase.cycle_number},{phase.phase_name},"
                            f"{pt.position_mm:.4f},{pt.force_n * invert:.6f}\n"
                        )
            self._log(f"Exported CSV to {path}", "info")
        except Exception as exc:
            self._log(f"CSV export failed: {exc}", "error")

    def _clear_plot(self) -> None:
        """Clear all plot data and stored measurements."""
        self._experiment_data.clear()
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

        for phase in self._experiment_data:
            if not phase.points:
                continue

            positions = [p.position_mm for p in phase.points]
            forces = [p.force_n * invert for p in phase.points]

            # Trim edges
            total_range = max(positions) - min(positions) if positions else 0.0
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

            # Max / min in trimmed region
            max_f = max(trimmed, key=lambda x: x[1])
            min_f = min(trimmed, key=lambda x: x[1])

            results.append(
                f"Cycle {phase.cycle_number} ({phase.phase_name}):"
            )
            results.append(
                f"  Max force: {max_f[1]:.4f} N at {max_f[0]:.3f} mm"
            )
            results.append(
                f"  Min force: {min_f[1]:.4f} N at {min_f[0]:.3f} mm"
            )

            # Zero-crossing detection
            for i in range(len(trimmed) - 1):
                p1, f1 = trimmed[i]
                p2, f2 = trimmed[i + 1]
                if f1 * f2 < 0:  # sign change
                    # Linear interpolation for exact crossing
                    zero_pos = p1 + (p2 - p1) * (-f1) / (f2 - f1)

                    # Tangent slope (±5 points around crossing)
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
                            f"  Zero-crossing at {zero_pos:.3f} mm "
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
            level: Severity — "info", "warn", or "error".
        """
        timestamp = time.strftime("%H:%M:%S")
        prefix = {"info": "INFO", "warn": "WARN", "error": "ERR "}.get(
            level, "INFO"
        )
        line = f"[{timestamp}] {prefix}  {message}"
        self._log_text.append(line)

        # Auto-scroll
        scrollbar = self._log_text.verticalScrollBar()
        if scrollbar:
            scrollbar.setValue(scrollbar.maximum())

    # =========================================================================
    # Window Events
    # =========================================================================

    def closeEvent(self, event) -> None:  # type: ignore[override]
        """Clean up resources when the window is closed."""
        self._force_timer.stop()
        if self._test_running:
            self._abort_requested = True
            # Give the thread a moment to notice
            if self._test_thread and self._test_thread.is_alive():
                self._test_thread.join(timeout=3.0)
        self._client.disconnect()
        super().closeEvent(event)


# =============================================================================
# Entry Point
# =============================================================================


def main() -> None:
    """Launch the Force-Deflection V2 application."""
    app = QApplication(sys.argv)
    window = ForceDeflectionWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
