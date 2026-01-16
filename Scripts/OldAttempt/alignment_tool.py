"""
Alignment Tool - PyQt6 window for manual data alignment

This tool allows users to manually select start and stop points
on current and force traces to define the analysis windows.

Features:
  - Interactive plots with click-to-place markers
  - Manual timestamp entry
  - Auto-detect button for threshold-based detection
  - Live metric updates as markers are adjusted
  - Preview of trimmed data before saving

Author: Energy Measurement Test System
Date: January 2026
"""

import sys
import numpy as np
from typing import Optional, Callable, Tuple
from dataclasses import dataclass

from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QLineEdit, QSplitter, QWidget, QFrame,
    QGridLayout, QMessageBox
)
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QDoubleValidator

import matplotlib
matplotlib.use('QtAgg')
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.backends.backend_qt import NavigationToolbar2QT as NavigationToolbar
from matplotlib.figure import Figure
import matplotlib.pyplot as plt


@dataclass
class AlignmentResult:
    """Result from alignment tool"""
    power_start_ms: float
    power_stop_ms: float
    force_start_ms: float
    force_stop_ms: float
    accepted: bool = False


class InteractivePlot(QWidget):
    """
    Interactive matplotlib plot with click-to-place markers
    """
    
    marker_changed = pyqtSignal(str, float)  # marker_type ('start'/'stop'), value
    
    def __init__(self, title: str, ylabel: str, color: str = '#2E86AB', parent=None):
        super().__init__(parent)
        
        self.title = title
        self.ylabel = ylabel
        self.color = color
        
        self.time_ms: Optional[np.ndarray] = None
        self.values: Optional[np.ndarray] = None
        
        self.start_ms: Optional[float] = None
        self.stop_ms: Optional[float] = None
        
        self._start_line = None
        self._stop_line = None
        self._click_mode = None  # 'start', 'stop', or None
        
        self._setup_ui()
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        
        # Create figure
        self.figure = Figure(figsize=(8, 3), dpi=100)
        self.figure.patch.set_facecolor('white')
        self.canvas = FigureCanvas(self.figure)
        self.ax = self.figure.add_subplot(111)
        
        # Connect click event
        self.canvas.mpl_connect('button_press_event', self._on_click)
        
        # Toolbar
        self.toolbar = NavigationToolbar(self.canvas, self)
        
        layout.addWidget(self.toolbar)
        layout.addWidget(self.canvas)
        
        # Controls
        controls = QHBoxLayout()
        
        controls.addWidget(QLabel("Start:"))
        self.start_edit = QLineEdit()
        self.start_edit.setValidator(QDoubleValidator())
        self.start_edit.setFixedWidth(80)
        self.start_edit.editingFinished.connect(self._on_start_edited)
        controls.addWidget(self.start_edit)
        controls.addWidget(QLabel("ms"))
        
        controls.addSpacing(20)
        
        controls.addWidget(QLabel("Stop:"))
        self.stop_edit = QLineEdit()
        self.stop_edit.setValidator(QDoubleValidator())
        self.stop_edit.setFixedWidth(80)
        self.stop_edit.editingFinished.connect(self._on_stop_edited)
        controls.addWidget(self.stop_edit)
        controls.addWidget(QLabel("ms"))
        
        controls.addSpacing(20)
        
        self.auto_btn = QPushButton("Auto-Detect")
        self.auto_btn.clicked.connect(self._auto_detect)
        controls.addWidget(self.auto_btn)
        
        controls.addStretch()
        
        # Click mode buttons
        self.set_start_btn = QPushButton("📍 Set Start")
        self.set_start_btn.setCheckable(True)
        self.set_start_btn.clicked.connect(lambda: self._set_click_mode('start'))
        controls.addWidget(self.set_start_btn)
        
        self.set_stop_btn = QPushButton("📍 Set Stop")
        self.set_stop_btn.setCheckable(True)
        self.set_stop_btn.clicked.connect(lambda: self._set_click_mode('stop'))
        controls.addWidget(self.set_stop_btn)
        
        layout.addLayout(controls)
    
    def set_data(self, time_ms: np.ndarray, values: np.ndarray):
        """Set plot data"""
        self.time_ms = time_ms
        self.values = values
        self._update_plot()
    
    def set_markers(self, start_ms: float, stop_ms: float):
        """Set marker positions"""
        self.start_ms = start_ms
        self.stop_ms = stop_ms
        self.start_edit.setText(f"{start_ms:.1f}")
        self.stop_edit.setText(f"{stop_ms:.1f}")
        self._update_markers()
    
    def get_markers(self) -> Tuple[float, float]:
        """Get current marker positions"""
        return (self.start_ms or 0.0, self.stop_ms or 0.0)
    
    def _update_plot(self):
        """Redraw the plot"""
        self.ax.clear()
        
        if self.time_ms is not None and self.values is not None:
            self.ax.plot(self.time_ms, self.values, color=self.color, linewidth=1)
        
        self.ax.set_xlabel('Time (ms)')
        self.ax.set_ylabel(self.ylabel)
        self.ax.set_title(self.title)
        self.ax.grid(True, alpha=0.3)
        
        self._start_line = None
        self._stop_line = None
        self._update_markers()
        
        self.figure.tight_layout()
        self.canvas.draw()
    
    def _update_markers(self):
        """Update marker lines on plot"""
        marker_color = '#9B59B6'  # Purple
        
        # Remove old lines
        if self._start_line:
            self._start_line.remove()
            self._start_line = None
        if self._stop_line:
            self._stop_line.remove()
            self._stop_line = None
        
        # Add new lines
        if self.start_ms is not None:
            self._start_line = self.ax.axvline(
                self.start_ms, color=marker_color, linestyle='--', 
                linewidth=2, alpha=0.8, label='START'
            )
        
        if self.stop_ms is not None:
            self._stop_line = self.ax.axvline(
                self.stop_ms, color=marker_color, linestyle='--',
                linewidth=2, alpha=0.8, label='STOP'
            )
        
        # Add shaded region
        if self.start_ms is not None and self.stop_ms is not None:
            self.ax.axvspan(self.start_ms, self.stop_ms, 
                          alpha=0.1, color=marker_color)
        
        self.canvas.draw()
    
    def _set_click_mode(self, mode: Optional[str]):
        """Set click mode for placing markers"""
        if mode == 'start':
            self.set_start_btn.setChecked(True)
            self.set_stop_btn.setChecked(False)
            self._click_mode = 'start'
        elif mode == 'stop':
            self.set_start_btn.setChecked(False)
            self.set_stop_btn.setChecked(True)
            self._click_mode = 'stop'
        else:
            self.set_start_btn.setChecked(False)
            self.set_stop_btn.setChecked(False)
            self._click_mode = None
    
    def _on_click(self, event):
        """Handle click on plot"""
        if event.inaxes != self.ax:
            return
        if self._click_mode is None:
            return
        
        x = event.xdata
        if x is None:
            return
        
        if self._click_mode == 'start':
            self.start_ms = x
            self.start_edit.setText(f"{x:.1f}")
            self.marker_changed.emit('start', x)
        elif self._click_mode == 'stop':
            self.stop_ms = x
            self.stop_edit.setText(f"{x:.1f}")
            self.marker_changed.emit('stop', x)
        
        self._update_markers()
        self._set_click_mode(None)
    
    def _on_start_edited(self):
        """Handle manual start edit"""
        try:
            value = float(self.start_edit.text())
            self.start_ms = value
            self._update_markers()
            self.marker_changed.emit('start', value)
        except ValueError:
            pass
    
    def _on_stop_edited(self):
        """Handle manual stop edit"""
        try:
            value = float(self.stop_edit.text())
            self.stop_ms = value
            self._update_markers()
            self.marker_changed.emit('stop', value)
        except ValueError:
            pass
    
    def _auto_detect(self):
        """Auto-detect start/stop using threshold"""
        if self.time_ms is None or self.values is None:
            return
        
        if len(self.values) < 10:
            return
        
        # Simple threshold detection
        # Find first point above threshold (10% of range above baseline)
        baseline = np.percentile(self.values, 10)
        peak = np.percentile(self.values, 90)
        threshold = baseline + 0.1 * (peak - baseline)
        
        above_threshold = self.values > threshold
        
        # Find first True
        start_idx = np.argmax(above_threshold)
        if start_idx == 0 and not above_threshold[0]:
            start_idx = 0
        
        # Find last True
        stop_idx = len(above_threshold) - 1 - np.argmax(above_threshold[::-1])
        
        # Add small margin
        margin_samples = max(1, len(self.time_ms) // 50)
        start_idx = max(0, start_idx - margin_samples)
        stop_idx = min(len(self.time_ms) - 1, stop_idx + margin_samples)
        
        self.start_ms = self.time_ms[start_idx]
        self.stop_ms = self.time_ms[stop_idx]
        
        self.start_edit.setText(f"{self.start_ms:.1f}")
        self.stop_edit.setText(f"{self.stop_ms:.1f}")
        
        self._update_markers()
        self.marker_changed.emit('start', self.start_ms)
        self.marker_changed.emit('stop', self.stop_ms)


class AlignmentTool(QDialog):
    """
    Dialog for manual data alignment
    
    Allows users to set start/stop markers on current and force traces
    to define the analysis windows for metric computation.
    """
    
    def __init__(self, 
                 test_id: str,
                 power_time_ms: np.ndarray,
                 current_a: np.ndarray,
                 force_time_ms: np.ndarray,
                 force_n: np.ndarray,
                 initial_power_start: Optional[float] = None,
                 initial_power_stop: Optional[float] = None,
                 initial_force_start: Optional[float] = None,
                 initial_force_stop: Optional[float] = None,
                 parent=None):
        super().__init__(parent)
        
        self.test_id = test_id
        self.power_time_ms = power_time_ms
        self.current_a = current_a
        self.force_time_ms = force_time_ms
        self.force_n = force_n
        
        self._result = AlignmentResult(
            power_start_ms=initial_power_start or 0.0,
            power_stop_ms=initial_power_stop or (power_time_ms[-1] if len(power_time_ms) > 0 else 0.0),
            force_start_ms=initial_force_start or 0.0,
            force_stop_ms=initial_force_stop or (force_time_ms[-1] if len(force_time_ms) > 0 else 0.0),
            accepted=False
        )
        
        self._setup_ui()
        self._load_data()
    
    def _setup_ui(self):
        self.setWindowTitle(f"Data Alignment Tool - {self.test_id}")
        self.setMinimumSize(1000, 800)
        self.resize(1200, 900)
        
        layout = QVBoxLayout(self)
        
        # Main content splitter
        splitter = QSplitter(Qt.Orientation.Vertical)
        
        # Current trace plot
        self.current_plot = InteractivePlot(
            title="Current Trace",
            ylabel="Current (mA)",
            color='#E94F37'
        )
        self.current_plot.marker_changed.connect(self._on_current_marker_changed)
        splitter.addWidget(self.current_plot)
        
        # Force trace plot
        self.force_plot = InteractivePlot(
            title="Force Trace",
            ylabel="Force (N)",
            color='#2E86AB'
        )
        self.force_plot.marker_changed.connect(self._on_force_marker_changed)
        splitter.addWidget(self.force_plot)
        
        layout.addWidget(splitter, stretch=1)
        
        # Summary section
        summary_group = QGroupBox("Alignment Summary && Computed Metrics")
        summary_layout = QGridLayout(summary_group)
        
        # Windows info
        summary_layout.addWidget(QLabel("Current window:"), 0, 0)
        self.current_window_label = QLabel("0 ms → 0 ms (0 ms duration)")
        summary_layout.addWidget(self.current_window_label, 0, 1)
        
        summary_layout.addWidget(QLabel("Force window:"), 1, 0)
        self.force_window_label = QLabel("0 ms → 0 ms (0 ms duration)")
        summary_layout.addWidget(self.force_window_label, 1, 1)
        
        # Separator
        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        summary_layout.addWidget(line, 2, 0, 1, 4)
        
        # Metrics
        summary_layout.addWidget(QLabel("Max Force:"), 3, 0)
        self.max_force_label = QLabel("-- N (-- g)")
        summary_layout.addWidget(self.max_force_label, 3, 1)
        
        summary_layout.addWidget(QLabel("Avg Force:"), 4, 0)
        self.avg_force_label = QLabel("-- N")
        summary_layout.addWidget(self.avg_force_label, 4, 1)
        
        summary_layout.addWidget(QLabel("Avg Power:"), 3, 2)
        self.avg_power_label = QLabel("-- mW")
        summary_layout.addWidget(self.avg_power_label, 3, 3)
        
        summary_layout.addWidget(QLabel("Peak Power:"), 4, 2)
        self.peak_power_label = QLabel("-- mW")
        summary_layout.addWidget(self.peak_power_label, 4, 3)
        
        summary_layout.addWidget(QLabel("Total Energy:"), 5, 2)
        self.energy_label = QLabel("-- mJ")
        summary_layout.addWidget(self.energy_label, 5, 3)
        
        layout.addWidget(summary_group)
        
        # Buttons
        button_layout = QHBoxLayout()
        
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        button_layout.addWidget(cancel_btn)
        
        button_layout.addStretch()
        
        preview_btn = QPushButton("Preview Plots")
        preview_btn.clicked.connect(self._preview_plots)
        button_layout.addWidget(preview_btn)
        
        apply_btn = QPushButton("✓ Apply && Save to HDF5")
        apply_btn.setStyleSheet("background-color: #27AE60; color: white; font-weight: bold;")
        apply_btn.clicked.connect(self._apply)
        button_layout.addWidget(apply_btn)
        
        layout.addLayout(button_layout)
    
    def _load_data(self):
        """Load data into plots"""
        # Current plot (show in mA)
        if len(self.power_time_ms) > 0:
            self.current_plot.set_data(self.power_time_ms, self.current_a * 1000)
            self.current_plot.set_markers(self._result.power_start_ms, self._result.power_stop_ms)
        
        # Force plot
        if len(self.force_time_ms) > 0:
            self.force_plot.set_data(self.force_time_ms, self.force_n)
            self.force_plot.set_markers(self._result.force_start_ms, self._result.force_stop_ms)
        
        self._update_summary()
    
    def _on_current_marker_changed(self, marker_type: str, value: float):
        """Handle current marker change"""
        if marker_type == 'start':
            self._result.power_start_ms = value
        else:
            self._result.power_stop_ms = value
        self._update_summary()
    
    def _on_force_marker_changed(self, marker_type: str, value: float):
        """Handle force marker change"""
        if marker_type == 'start':
            self._result.force_start_ms = value
        else:
            self._result.force_stop_ms = value
        self._update_summary()
    
    def _update_summary(self):
        """Update summary labels with current alignment"""
        # Window info
        power_dur = self._result.power_stop_ms - self._result.power_start_ms
        force_dur = self._result.force_stop_ms - self._result.force_start_ms
        
        self.current_window_label.setText(
            f"{self._result.power_start_ms:.1f} ms → {self._result.power_stop_ms:.1f} ms "
            f"({power_dur:.1f} ms duration)"
        )
        self.force_window_label.setText(
            f"{self._result.force_start_ms:.1f} ms → {self._result.force_stop_ms:.1f} ms "
            f"({force_dur:.1f} ms duration)"
        )
        
        # Compute metrics on aligned data
        # Force metrics
        if len(self.force_time_ms) > 0:
            mask = ((self.force_time_ms >= self._result.force_start_ms) & 
                   (self.force_time_ms <= self._result.force_stop_ms))
            aligned_force = self.force_n[mask]
            
            if len(aligned_force) > 0:
                max_f = np.max(aligned_force)
                avg_f = np.mean(aligned_force)
                self.max_force_label.setText(f"{max_f:.3f} N ({max_f * 1000 / 9.81:.1f} g)")
                self.avg_force_label.setText(f"{avg_f:.3f} N")
            else:
                self.max_force_label.setText("-- N (-- g)")
                self.avg_force_label.setText("-- N")
        
        # Power metrics
        if len(self.power_time_ms) > 0:
            mask = ((self.power_time_ms >= self._result.power_start_ms) & 
                   (self.power_time_ms <= self._result.power_stop_ms))
            aligned_current = self.current_a[mask]
            aligned_time = self.power_time_ms[mask]
            
            if len(aligned_current) > 0:
                # Assume constant voltage (we don't have it here, estimate from current)
                voltage = 5.0  # Default assumption
                aligned_power = aligned_current * voltage
                
                avg_p = np.mean(aligned_power) * 1000  # mW
                peak_p = np.max(aligned_power) * 1000  # mW
                
                self.avg_power_label.setText(f"{avg_p:.2f} mW")
                self.peak_power_label.setText(f"{peak_p:.2f} mW")
                
                # Energy
                if len(aligned_time) > 1:
                    # numpy 2.0+ uses trapezoid, older uses trapz
                    try:
                        energy = np.trapezoid(aligned_power, aligned_time / 1000.0) * 1000  # type: ignore  # mJ
                    except AttributeError:
                        energy = np.trapz(aligned_power, aligned_time / 1000.0) * 1000  # type: ignore  # mJ
                    self.energy_label.setText(f"{energy:.2f} mJ")
                else:
                    self.energy_label.setText("-- mJ")
            else:
                self.avg_power_label.setText("-- mW")
                self.peak_power_label.setText("-- mW")
                self.energy_label.setText("-- mJ")
    
    def _preview_plots(self):
        """Show preview of aligned data plots"""
        # Create a simple preview figure
        fig, axes = plt.subplots(2, 1, figsize=(10, 6))
        
        # Current plot
        if len(self.power_time_ms) > 0:
            mask = ((self.power_time_ms >= self._result.power_start_ms) & 
                   (self.power_time_ms <= self._result.power_stop_ms))
            axes[0].plot(self.power_time_ms[mask], self.current_a[mask] * 1000, 
                        color='#E94F37')
            axes[0].set_xlabel('Time (ms)')
            axes[0].set_ylabel('Current (mA)')
            axes[0].set_title('Aligned Current Data')
            axes[0].grid(True, alpha=0.3)
        
        # Force plot
        if len(self.force_time_ms) > 0:
            mask = ((self.force_time_ms >= self._result.force_start_ms) & 
                   (self.force_time_ms <= self._result.force_stop_ms))
            axes[1].plot(self.force_time_ms[mask], self.force_n[mask],
                        color='#2E86AB')
            axes[1].set_xlabel('Time (ms)')
            axes[1].set_ylabel('Force (N)')
            axes[1].set_title('Aligned Force Data')
            axes[1].grid(True, alpha=0.3)
        
        fig.tight_layout()
        plt.show()
    
    def _apply(self):
        """Apply alignment and close"""
        # Validate
        if self._result.power_stop_ms <= self._result.power_start_ms:
            QMessageBox.warning(self, "Invalid Alignment", 
                              "Current stop must be after start!")
            return
        
        if self._result.force_stop_ms <= self._result.force_start_ms:
            QMessageBox.warning(self, "Invalid Alignment",
                              "Force stop must be after start!")
            return
        
        self._result.accepted = True
        self.accept()
    
    def get_result(self) -> AlignmentResult:
        """Get alignment result"""
        return self._result


# =============================================================================
# Standalone testing
# =============================================================================

def main():
    """Test the alignment tool standalone"""
    from PyQt6.QtWidgets import QApplication
    
    app = QApplication(sys.argv)
    
    # Generate test data
    np.random.seed(42)
    
    # Power data (1000 Hz, 5 seconds)
    power_time = np.arange(0, 5000, 1)
    current = (np.sin(power_time / 1000) * 0.05 + 0.1 + 
               np.random.normal(0, 0.005, len(power_time)))
    # Add some baseline before and after
    current[:500] = 0.02 + np.random.normal(0, 0.002, 500)
    current[-500:] = 0.02 + np.random.normal(0, 0.002, 500)
    
    # Force data (100 Hz, 5 seconds)
    force_time = np.arange(0, 5000, 10)
    force = np.sin(force_time / 1000) * 2 + 1 + np.random.normal(0, 0.1, len(force_time))
    force[:50] = 0.1 + np.random.normal(0, 0.02, 50)
    force[-50:] = 0.1 + np.random.normal(0, 0.02, 50)
    
    # Create and show tool
    tool = AlignmentTool(
        test_id="M1-CV-3",
        power_time_ms=power_time,
        current_a=current,
        force_time_ms=force_time,
        force_n=force,
        initial_power_start=500,
        initial_power_stop=4500,
        initial_force_start=500,
        initial_force_stop=4500
    )
    
    if tool.exec() == QDialog.DialogCode.Accepted:
        result = tool.get_result()
        print("Alignment accepted:")
        print(f"  Power: {result.power_start_ms:.1f} → {result.power_stop_ms:.1f} ms")
        print(f"  Force: {result.force_start_ms:.1f} → {result.force_stop_ms:.1f} ms")
    else:
        print("Alignment cancelled")
    
    sys.exit(0)


if __name__ == "__main__":
    main()
