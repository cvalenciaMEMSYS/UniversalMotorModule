"""
Motor Jog Tool — Simple PyQt6 GUI for manual motor control via serial.

Connects to a Universal Motor Module (UMM) device and provides a convenient
interface for jogging, configuring motion parameters, and monitoring status.

Dependencies: PyQt6, pyserial
Usage: python motor_jog_tool.py
"""

import sys
import time
import threading
import re
from typing import Optional, Callable

import serial
import serial.tools.list_ports
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QTextCursor, QCloseEvent
from PyQt6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QFormLayout,
    QGroupBox,
    QComboBox,
    QPushButton,
    QLabel,
    QSpinBox,
    QCheckBox,
    QTextEdit,
)


# ---------------------------------------------------------------------------
# Serial connection manager
# ---------------------------------------------------------------------------

class MotorConnection(QObject):
    """Serial connection manager with background reader thread.

    Handles the serial lifecycle (connect/disconnect), provides a
    thread-safe ``send_command`` method, and emits received lines to the
    UI via Qt signals.
    """

    line_received = pyqtSignal(str)
    """Emitted (from the reader thread) whenever a complete line arrives."""

    connection_lost = pyqtSignal(str)
    """Emitted when an unexpected serial error is detected."""

    def __init__(self, parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self._serial: Optional[serial.Serial] = None
        self._lock = threading.Lock()
        self._reader_thread: Optional[threading.Thread] = None
        self._running = False
        self._response_lines: list[str] = []
        self._response_event = threading.Event()

    # -- public API ---------------------------------------------------------

    @property
    def is_connected(self) -> bool:
        """Return *True* when the serial port is open."""
        return self._serial is not None and self._serial.is_open

    def open_port(self, port: str) -> None:
        """Open *port* at 115200-8N1 with DTR disabled.

        A 2-second post-connect delay is observed to let the ESP32
        boot without being reset.

        Raises:
            serial.SerialException: If the port cannot be opened.
        """
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = 115200
        ser.bytesize = serial.EIGHTBITS
        ser.parity = serial.PARITY_NONE
        ser.stopbits = serial.STOPBITS_ONE
        ser.timeout = 0.1
        ser.dtr = False

        ser.open()
        # Keep DTR deasserted after open (some drivers re-assert it).
        ser.dtr = False

        self._serial = ser
        self._running = True
        self._reader_thread = threading.Thread(
            target=self._reader_loop, daemon=True, name="serial-reader"
        )
        self._reader_thread.start()

        # Post-connect delay so the ESP32 firmware finishes booting.
        time.sleep(2.0)

    def close_port(self) -> None:
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
        """Send *cmd* (newline-terminated) and collect response lines.

        Args:
            cmd: The ASCII command string (without trailing newline).
            timeout: Maximum seconds to wait for response data.

        Returns:
            A list of response lines received within *timeout*.
        """
        with self._lock:
            if not self.is_connected:
                return []

            self._response_lines = []
            self._response_event.clear()

            ser = self._serial
            if ser is None:
                return []
            try:
                ser.write((cmd + "\n").encode("ascii"))
            except serial.SerialException as exc:
                self.connection_lost.emit(str(exc))
                return []

            # Give the device some time to reply.
            self._response_event.wait(timeout=timeout)
            # Grab whatever accumulated during the wait.
            time.sleep(0.05)
            lines = list(self._response_lines)
            self._response_lines = []
            return lines

    # -- background reader --------------------------------------------------

    def _reader_loop(self) -> None:
        """Continuously read lines from the serial port (runs in a daemon thread)."""
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
                        self.line_received.emit(line)
            except serial.SerialException as exc:
                if self._running:
                    self.connection_lost.emit(str(exc))
                break
            except Exception:
                # Guard against transient decoding / OS errors.
                continue


# ---------------------------------------------------------------------------
# Main application window
# ---------------------------------------------------------------------------

class MotorJogWindow(QMainWindow):
    """Main application window for the Motor Jog Tool.

    Provides connection management, motion-parameter configuration,
    jog / stop / enable / disable controls, live status polling, and a
    scrolling command log.
    """

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Motor Jog Tool")
        self.setMinimumWidth(480)

        self._motor = MotorConnection(self)
        self._motor.line_received.connect(self._on_line_received)
        self._motor.connection_lost.connect(self._on_connection_lost)

        self._build_ui()
        self._update_button_states()

        # Status polling timer (500 ms).
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(500)
        self._poll_timer.timeout.connect(self._poll_status)

    # -- UI construction ----------------------------------------------------

    def _build_ui(self) -> None:
        """Construct all UI widgets and lay them out."""
        central = QWidget()
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)

        root_layout.addWidget(self._build_connection_group())
        root_layout.addWidget(self._build_parameters_group())
        root_layout.addWidget(self._build_motion_group())
        root_layout.addWidget(self._build_status_group())
        root_layout.addWidget(self._build_log_group())

    # -- connection section -------------------------------------------------

    def _build_connection_group(self) -> QGroupBox:
        """Build the *Connection* group box."""
        group = QGroupBox("Connection")
        layout = QHBoxLayout(group)

        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(160)
        layout.addWidget(self._port_combo)

        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.clicked.connect(self._refresh_ports)
        layout.addWidget(self._refresh_btn)

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.clicked.connect(self._toggle_connection)
        layout.addWidget(self._connect_btn)

        self._conn_status_label = QLabel("Disconnected")
        layout.addWidget(self._conn_status_label)

        layout.addStretch()
        self._refresh_ports()
        return group

    # -- motor parameters section -------------------------------------------

    def _build_parameters_group(self) -> QGroupBox:
        """Build the *Motor Parameters* group box."""
        group = QGroupBox("Motor Parameters")
        form = QFormLayout(group)

        self._speed_spin = QSpinBox()
        self._speed_spin.setRange(1, 100_000)
        self._speed_spin.setValue(1000)
        self._speed_spin.setSuffix(" steps/s")
        form.addRow("Speed:", self._speed_spin)

        self._accel_spin = QSpinBox()
        self._accel_spin.setRange(0, 100_000)
        self._accel_spin.setValue(0)
        self._accel_spin.setSuffix(" steps/s²")
        form.addRow("Acceleration:", self._accel_spin)

        self._cube_spin = QSpinBox()
        self._cube_spin.setRange(0, 10_000)
        self._cube_spin.setValue(0)
        self._cube_spin.setSuffix(" steps")
        form.addRow("Cubesteps:", self._cube_spin)

        self._travel_spin = QSpinBox()
        self._travel_spin.setRange(1, 1_000_000)
        self._travel_spin.setValue(1000)
        self._travel_spin.setSuffix(" steps")
        form.addRow("Travel:", self._travel_spin)

        self._reverse_check = QCheckBox("Reverse")
        form.addRow("", self._reverse_check)

        self._send_config_btn = QPushButton("Send Config")
        self._send_config_btn.clicked.connect(self._send_config)
        form.addRow(self._send_config_btn)

        return group

    # -- motion controls section --------------------------------------------

    def _build_motion_group(self) -> QGroupBox:
        """Build the *Motion Controls* group box.

        Up (↑) moves positive steps, Down (↓) moves negative.
        When *Reverse* is checked, the directions are swapped so the
        arrows match the physical motion of the setup.
        """
        group = QGroupBox("Motion Controls")
        layout = QHBoxLayout(group)

        # Vertical sub-layout for the arrow buttons (Up / Down)
        arrow_layout = QVBoxLayout()
        arrow_layout.setSpacing(4)

        arrow_style = (
            "QPushButton { font-size: 18px; font-weight: bold; "
            "padding: 8px 24px; }"
        )

        self._fwd_btn = QPushButton("\u25B2  Up")
        self._fwd_btn.setStyleSheet(arrow_style)
        self._fwd_btn.clicked.connect(self._jog_upward)
        arrow_layout.addWidget(self._fwd_btn)

        self._bwd_btn = QPushButton("\u25BC  Down")
        self._bwd_btn.setStyleSheet(arrow_style)
        self._bwd_btn.clicked.connect(self._jog_downward)
        arrow_layout.addWidget(self._bwd_btn)

        layout.addLayout(arrow_layout)

        self._stop_btn = QPushButton("STOP")
        self._stop_btn.setStyleSheet(
            "QPushButton { background-color: #d32f2f; color: white; "
            "font-weight: bold; padding: 6px 18px; }"
            "QPushButton:hover { background-color: #b71c1c; }"
        )
        self._stop_btn.clicked.connect(self._stop)
        layout.addWidget(self._stop_btn)

        self._enable_btn = QPushButton("Enable")
        self._enable_btn.clicked.connect(self._enable)
        layout.addWidget(self._enable_btn)

        self._disable_btn = QPushButton("Disable")
        self._disable_btn.clicked.connect(self._disable)
        layout.addWidget(self._disable_btn)

        return group

    # -- status display section ---------------------------------------------

    def _build_status_group(self) -> QGroupBox:
        """Build the *Status* group box with position and ramp-state labels."""
        group = QGroupBox("Status")
        layout = QFormLayout(group)

        self._position_label = QLabel("—")
        layout.addRow("Position:", self._position_label)

        self._ramp_label = QLabel("—")
        layout.addRow("Ramp State:", self._ramp_label)

        return group

    # -- log panel ----------------------------------------------------------

    def _build_log_group(self) -> QGroupBox:
        """Build the scrolling *Log* panel."""
        group = QGroupBox("Log")
        layout = QVBoxLayout(group)

        self._log_edit = QTextEdit()
        self._log_edit.setReadOnly(True)
        self._log_edit.setMinimumHeight(120)
        layout.addWidget(self._log_edit)

        return group

    # -- port helpers -------------------------------------------------------

    def _refresh_ports(self) -> None:
        """Re-scan available COM ports and populate the combo box."""
        self._port_combo.clear()
        ports = serial.tools.list_ports.comports()
        for port_info in sorted(ports, key=lambda p: p.device, reverse=True):
            self._port_combo.addItem(
                f"{port_info.device} — {port_info.description}", port_info.device
            )
        if self._port_combo.count() == 0:
            self._port_combo.addItem("(no ports found)", "")

    # -- connection toggling ------------------------------------------------

    def _toggle_connection(self) -> None:
        """Connect or disconnect based on current state."""
        if self._motor.is_connected:
            self._do_disconnect()
        else:
            self._do_connect()

    def _do_connect(self) -> None:
        """Attempt to open the selected serial port."""
        port = self._port_combo.currentData()
        if not port:
            self._log("No port selected.")
            return

        self._log(f"Connecting to {port} …")
        try:
            self._motor.open_port(port)
        except serial.SerialException as exc:
            self._log(f"Connection failed: {exc}")
            return

        self._conn_status_label.setText(f"Connected ({port})")
        self._connect_btn.setText("Disconnect")
        self._poll_timer.start()
        self._update_button_states()
        self._log("Connected.")

    def _do_disconnect(self) -> None:
        """Close the serial port and reset UI state."""
        self._poll_timer.stop()
        self._motor.close_port()
        self._conn_status_label.setText("Disconnected")
        self._connect_btn.setText("Connect")
        self._position_label.setText("—")
        self._ramp_label.setText("—")
        self._update_button_states()
        self._log("Disconnected.")

    def _on_connection_lost(self, error: str) -> None:
        """Handle an unexpected serial disconnection."""
        self._poll_timer.stop()
        self._conn_status_label.setText("Disconnected (lost)")
        self._connect_btn.setText("Connect")
        self._position_label.setText("—")
        self._ramp_label.setText("—")
        self._update_button_states()
        self._log(f"Connection lost: {error}")

    # -- button state management --------------------------------------------

    def _update_button_states(self) -> None:
        """Enable / disable buttons depending on connection state."""
        connected = self._motor.is_connected
        self._send_config_btn.setEnabled(connected)
        self._fwd_btn.setEnabled(connected)
        self._bwd_btn.setEnabled(connected)
        self._stop_btn.setEnabled(connected)
        self._enable_btn.setEnabled(connected)
        self._disable_btn.setEnabled(connected)
        self._port_combo.setEnabled(not connected)
        self._refresh_btn.setEnabled(not connected)

    # -- command helpers ----------------------------------------------------

    def _send(self, cmd: str) -> list[str]:
        """Send *cmd* to the motor and log it.

        Returns:
            The list of response lines.
        """
        self._log(f">>> {cmd}")
        lines = self._motor.send_command(cmd)
        for line in lines:
            self._log(line)
        return lines

    # -- slot: Send Config --------------------------------------------------

    def _send_config(self) -> None:
        """Send speed, acceleration, and cubesteps to the motor."""
        self._send(f"set speed {self._speed_spin.value()}")
        self._send(f"set accel {self._accel_spin.value()}")
        self._send(f"set cubesteps {self._cube_spin.value()}")

    # -- slot: Jog Upward / Downward ----------------------------------------

    def _jog_upward(self) -> None:
        """Move in the upward (positive) direction.

        When *Reverse* is checked the sign is flipped so the ↑ button
        always corresponds to the physical "up" direction.
        """
        steps = self._travel_spin.value()
        if self._reverse_check.isChecked():
            steps = -steps
        self._send(f"move {steps}")

    def _jog_downward(self) -> None:
        """Move in the downward (negative) direction.

        When *Reverse* is checked the sign is flipped so the ↓ button
        always corresponds to the physical "down" direction.
        """
        steps = -self._travel_spin.value()
        if self._reverse_check.isChecked():
            steps = -steps
        self._send(f"move {steps}")

    # -- slot: Stop ---------------------------------------------------------

    def _stop(self) -> None:
        """Send an emergency-stop command."""
        self._send("stop")

    # -- slot: Enable / Disable ---------------------------------------------

    def _enable(self) -> None:
        """Enable the motor driver."""
        self._send("enable")

    def _disable(self) -> None:
        """Disable the motor driver."""
        self._send("disable")

    # -- status polling -----------------------------------------------------

    def _poll_status(self) -> None:
        """Query position and ramp state (called by QTimer every 500 ms)."""
        if not self._motor.is_connected:
            return

        # Position
        pos_lines = self._motor.send_command("get pos", timeout=0.5)
        for line in pos_lines:
            match = re.search(r"Position:\s*(-?\d+)", line)
            if match:
                self._position_label.setText(f"{match.group(1)} steps")
                break

        # Ramp state
        ramp_lines = self._motor.send_command("get rampstate", timeout=0.5)
        for line in ramp_lines:
            match = re.search(r"Ramp state:\s*(\S+)", line)
            if match:
                self._ramp_label.setText(match.group(1))
                break

    # -- incoming serial line -----------------------------------------------

    def _on_line_received(self, line: str) -> None:
        """Handle a line received asynchronously from the reader thread."""
        # Lines received outside of send_command are logged here.
        # (Lines within send_command are logged by _send.)
        pass

    # -- log ----------------------------------------------------------------

    def _log(self, text: str) -> None:
        """Append *text* to the log panel and scroll to the bottom."""
        self._log_edit.append(text)
        cursor = self._log_edit.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        self._log_edit.setTextCursor(cursor)

    # -- window lifecycle ---------------------------------------------------

    def closeEvent(self, a0: Optional[QCloseEvent]) -> None:
        """Ensure the serial port is closed when the window is closed."""
        self._poll_timer.stop()
        if self._motor.is_connected:
            self._motor.close_port()
        if a0 is not None:
            a0.accept()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    """Launch the Motor Jog Tool application."""
    app = QApplication(sys.argv)
    window = MotorJogWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
