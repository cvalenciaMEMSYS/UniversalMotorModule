"""
Plot Generator - Matplotlib plot generation for energy measurements

This module creates standardized plots for force and power measurements,
both for real-time display and for embedding in HDF5 files.

Plot types:
  - force_vs_time: Force trace over time
  - current_vs_time: Current trace over time  
  - voltage_vs_time: Voltage trace over time
  - power_vs_time: Power trace over time
  - combined_plot: 2x2 grid with all traces

Author: Energy Measurement Test System
Date: January 2026
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.figure import Figure
from matplotlib.backends.backend_agg import FigureCanvasAgg
import io
from typing import Optional, Tuple, Dict, Any
from dataclasses import dataclass


# Plot style configuration
PLOT_STYLE = {
    'figure.facecolor': 'white',
    'axes.facecolor': 'white',
    'axes.grid': True,
    'grid.alpha': 0.3,
    'axes.labelsize': 10,
    'axes.titlesize': 11,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'lines.linewidth': 1.5,
    'font.family': 'sans-serif',
}

# Color scheme
COLORS = {
    'force': '#2E86AB',      # Blue
    'current': '#E94F37',    # Red
    'voltage': '#F39C12',    # Orange/Yellow
    'power': '#27AE60',      # Green
    'alignment': '#9B59B6',  # Purple (for markers)
}


@dataclass
class PlotData:
    """Data container for plotting"""
    time_ms: np.ndarray
    values: np.ndarray
    label: str = ""
    unit: str = ""
    color: str = "#000000"
    
    # Optional alignment markers
    start_ms: Optional[float] = None
    stop_ms: Optional[float] = None


class PlotGenerator:
    """
    Generator for standardized measurement plots
    """
    
    def __init__(self, dpi: int = 100):
        """
        Initialize plot generator
        
        Args:
            dpi: Resolution for PNG output
        """
        self.dpi = dpi
        plt.rcParams.update(PLOT_STYLE)
    
    def _create_single_plot(self, 
                            data: PlotData,
                            figsize: Tuple[float, float] = (6, 4),
                            title: str = "",
                            show_alignment: bool = True) -> Figure:
        """
        Create single trace plot
        
        Args:
            data: PlotData with time and values
            figsize: Figure size in inches
            title: Plot title
            show_alignment: Show alignment markers if available
        """
        fig, ax = plt.subplots(figsize=figsize)
        
        # Plot main trace
        ax.plot(data.time_ms, data.values, color=data.color, label=data.label)
        
        # Add alignment markers if present
        if show_alignment and data.start_ms is not None:
            ax.axvline(data.start_ms, color=COLORS['alignment'], 
                      linestyle='--', alpha=0.7, label='Start')
        if show_alignment and data.stop_ms is not None:
            ax.axvline(data.stop_ms, color=COLORS['alignment'], 
                      linestyle='--', alpha=0.7, label='Stop')
        
        # Labels
        ax.set_xlabel('Time (ms)')
        ax.set_ylabel(f'{data.label} ({data.unit})')
        
        if title:
            ax.set_title(title)
        
        ax.legend(loc='upper right', framealpha=0.9)
        
        fig.tight_layout()
        return fig
    
    def create_force_plot(self,
                         time_ms: np.ndarray,
                         force_n: np.ndarray,
                         start_ms: Optional[float] = None,
                         stop_ms: Optional[float] = None,
                         title: str = "Force vs Time",
                         figsize: Tuple[float, float] = (6, 4)) -> Figure:
        """Create force vs time plot"""
        data = PlotData(
            time_ms=time_ms,
            values=force_n,
            label='Force',
            unit='N',
            color=COLORS['force'],
            start_ms=start_ms,
            stop_ms=stop_ms
        )
        return self._create_single_plot(data, figsize, title)
    
    def create_current_plot(self,
                           time_ms: np.ndarray,
                           current_a: np.ndarray,
                           start_ms: Optional[float] = None,
                           stop_ms: Optional[float] = None,
                           title: str = "Current vs Time",
                           figsize: Tuple[float, float] = (6, 4),
                           show_in_ma: bool = True) -> Figure:
        """Create current vs time plot"""
        if show_in_ma:
            values = current_a * 1000
            unit = 'mA'
        else:
            values = current_a
            unit = 'A'
        
        data = PlotData(
            time_ms=time_ms,
            values=values,
            label='Current',
            unit=unit,
            color=COLORS['current'],
            start_ms=start_ms,
            stop_ms=stop_ms
        )
        return self._create_single_plot(data, figsize, title)
    
    def create_voltage_plot(self,
                           time_ms: np.ndarray,
                           voltage_v: np.ndarray,
                           start_ms: Optional[float] = None,
                           stop_ms: Optional[float] = None,
                           title: str = "Voltage vs Time",
                           figsize: Tuple[float, float] = (6, 4)) -> Figure:
        """Create voltage vs time plot"""
        data = PlotData(
            time_ms=time_ms,
            values=voltage_v,
            label='Voltage',
            unit='V',
            color=COLORS['voltage'],
            start_ms=start_ms,
            stop_ms=stop_ms
        )
        return self._create_single_plot(data, figsize, title)
    
    def create_power_plot(self,
                         time_ms: np.ndarray,
                         power_w: np.ndarray,
                         start_ms: Optional[float] = None,
                         stop_ms: Optional[float] = None,
                         title: str = "Power vs Time",
                         figsize: Tuple[float, float] = (6, 4),
                         show_in_mw: bool = True) -> Figure:
        """Create power vs time plot"""
        if show_in_mw:
            values = power_w * 1000
            unit = 'mW'
        else:
            values = power_w
            unit = 'W'
        
        data = PlotData(
            time_ms=time_ms,
            values=values,
            label='Power',
            unit=unit,
            color=COLORS['power'],
            start_ms=start_ms,
            stop_ms=stop_ms
        )
        return self._create_single_plot(data, figsize, title)
    
    def create_combined_plot(self,
                            force_time_ms: Optional[np.ndarray] = None,
                            force_n: Optional[np.ndarray] = None,
                            power_time_ms: Optional[np.ndarray] = None,
                            voltage_v: Optional[np.ndarray] = None,
                            current_a: Optional[np.ndarray] = None,
                            power_w: Optional[np.ndarray] = None,
                            force_start_ms: Optional[float] = None,
                            force_stop_ms: Optional[float] = None,
                            power_start_ms: Optional[float] = None,
                            power_stop_ms: Optional[float] = None,
                            title: str = "",
                            figsize: Tuple[float, float] = (10, 8)) -> Figure:
        """
        Create 2x2 combined plot with all traces
        
        Layout:
            [Force]    [Current]
            [Voltage]  [Power]
        """
        fig = plt.figure(figsize=figsize)
        gs = gridspec.GridSpec(2, 2, figure=fig, hspace=0.3, wspace=0.3)
        
        if title:
            fig.suptitle(title, fontsize=12, fontweight='bold')
        
        # Force plot (top-left)
        ax1 = fig.add_subplot(gs[0, 0])
        if force_time_ms is not None and force_n is not None and len(force_time_ms) > 0:
            ax1.plot(force_time_ms, force_n, color=COLORS['force'])
            if force_start_ms is not None:
                ax1.axvline(force_start_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
            if force_stop_ms is not None:
                ax1.axvline(force_stop_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
        ax1.set_xlabel('Time (ms)')
        ax1.set_ylabel('Force (N)')
        ax1.set_title('Force')
        ax1.grid(True, alpha=0.3)
        
        # Current plot (top-right)
        ax2 = fig.add_subplot(gs[0, 1])
        if power_time_ms is not None and current_a is not None and len(power_time_ms) > 0:
            ax2.plot(power_time_ms, current_a * 1000, color=COLORS['current'])
            if power_start_ms is not None:
                ax2.axvline(power_start_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
            if power_stop_ms is not None:
                ax2.axvline(power_stop_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
        ax2.set_xlabel('Time (ms)')
        ax2.set_ylabel('Current (mA)')
        ax2.set_title('Current')
        ax2.grid(True, alpha=0.3)
        
        # Voltage plot (bottom-left)
        ax3 = fig.add_subplot(gs[1, 0])
        if power_time_ms is not None and voltage_v is not None and len(power_time_ms) > 0:
            ax3.plot(power_time_ms, voltage_v, color=COLORS['voltage'])
            if power_start_ms is not None:
                ax3.axvline(power_start_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
            if power_stop_ms is not None:
                ax3.axvline(power_stop_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
        ax3.set_xlabel('Time (ms)')
        ax3.set_ylabel('Voltage (V)')
        ax3.set_title('Voltage')
        ax3.grid(True, alpha=0.3)
        
        # Power plot (bottom-right)
        ax4 = fig.add_subplot(gs[1, 1])
        if power_time_ms is not None and power_w is not None and len(power_time_ms) > 0:
            ax4.plot(power_time_ms, power_w * 1000, color=COLORS['power'])
            if power_start_ms is not None:
                ax4.axvline(power_start_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
            if power_stop_ms is not None:
                ax4.axvline(power_stop_ms, color=COLORS['alignment'], 
                           linestyle='--', alpha=0.7)
        ax4.set_xlabel('Time (ms)')
        ax4.set_ylabel('Power (mW)')
        ax4.set_title('Power')
        ax4.grid(True, alpha=0.3)
        
        return fig
    
    def figure_to_png(self, fig: Figure) -> bytes:
        """
        Convert matplotlib figure to PNG bytes
        
        Args:
            fig: Matplotlib Figure object
            
        Returns:
            PNG image as bytes
        """
        buf = io.BytesIO()
        fig.savefig(buf, format='png', dpi=self.dpi, bbox_inches='tight',
                   facecolor='white', edgecolor='none')
        buf.seek(0)
        png_bytes = buf.read()
        buf.close()
        plt.close(fig)
        return png_bytes
    
    def create_and_save_all_plots(self,
                                 force_time_ms: Optional[np.ndarray] = None,
                                 force_n: Optional[np.ndarray] = None,
                                 power_time_ms: Optional[np.ndarray] = None,
                                 voltage_v: Optional[np.ndarray] = None,
                                 current_a: Optional[np.ndarray] = None,
                                 power_w: Optional[np.ndarray] = None,
                                 force_start_ms: Optional[float] = None,
                                 force_stop_ms: Optional[float] = None,
                                 power_start_ms: Optional[float] = None,
                                 power_stop_ms: Optional[float] = None,
                                 test_id: str = "") -> Dict[str, bytes]:
        """
        Create all plots and return as PNG bytes
        
        Returns:
            Dictionary mapping plot names to PNG bytes
        """
        plots = {}
        
        # Force plot
        if force_time_ms is not None and force_n is not None and len(force_time_ms) > 0:
            fig = self.create_force_plot(
                force_time_ms, force_n,
                force_start_ms, force_stop_ms,
                title=f"Force - {test_id}" if test_id else "Force vs Time"
            )
            plots['force_vs_time'] = self.figure_to_png(fig)
        
        # Current plot
        if power_time_ms is not None and current_a is not None and len(power_time_ms) > 0:
            fig = self.create_current_plot(
                power_time_ms, current_a,
                power_start_ms, power_stop_ms,
                title=f"Current - {test_id}" if test_id else "Current vs Time"
            )
            plots['current_vs_time'] = self.figure_to_png(fig)
        
        # Voltage plot
        if power_time_ms is not None and voltage_v is not None and len(power_time_ms) > 0:
            fig = self.create_voltage_plot(
                power_time_ms, voltage_v,
                power_start_ms, power_stop_ms,
                title=f"Voltage - {test_id}" if test_id else "Voltage vs Time"
            )
            plots['voltage_vs_time'] = self.figure_to_png(fig)
        
        # Power plot
        if power_time_ms is not None and power_w is not None and len(power_time_ms) > 0:
            fig = self.create_power_plot(
                power_time_ms, power_w,
                power_start_ms, power_stop_ms,
                title=f"Power - {test_id}" if test_id else "Power vs Time"
            )
            plots['power_vs_time'] = self.figure_to_png(fig)
        
        # Combined plot
        fig = self.create_combined_plot(
            force_time_ms=force_time_ms,
            force_n=force_n,
            power_time_ms=power_time_ms,
            voltage_v=voltage_v,
            current_a=current_a,
            power_w=power_w,
            force_start_ms=force_start_ms,
            force_stop_ms=force_stop_ms,
            power_start_ms=power_start_ms,
            power_stop_ms=power_stop_ms,
            title=f"Test Results - {test_id}" if test_id else "Test Results"
        )
        plots['combined_plot'] = self.figure_to_png(fig)
        
        return plots


def compute_metrics_from_aligned_data(
    force_time_ms: np.ndarray,
    force_n: np.ndarray,
    power_time_ms: np.ndarray,
    voltage_v: np.ndarray,
    current_a: np.ndarray,
    power_w: np.ndarray,
    force_start_ms: float,
    force_stop_ms: float,
    power_start_ms: float,
    power_stop_ms: float
) -> Dict[str, float]:
    """
    Compute metrics from aligned data windows
    
    Args:
        All raw data arrays and alignment boundaries
        
    Returns:
        Dictionary of computed metrics
    """
    metrics = {}
    
    # Force metrics (within force window)
    if len(force_time_ms) > 0:
        force_mask = (force_time_ms >= force_start_ms) & (force_time_ms <= force_stop_ms)
        aligned_force = force_n[force_mask]
        
        if len(aligned_force) > 0:
            metrics['max_force_n'] = float(np.max(aligned_force))
            metrics['avg_force_n'] = float(np.mean(aligned_force))
            metrics['min_force_n'] = float(np.min(aligned_force))
            metrics['max_thrust_g'] = float(np.max(aligned_force) * 1000 / 9.81)  # N to grams
            metrics['force_duration_ms'] = force_stop_ms - force_start_ms
    
    # Power metrics (within power window)
    if len(power_time_ms) > 0:
        power_mask = (power_time_ms >= power_start_ms) & (power_time_ms <= power_stop_ms)
        aligned_voltage = voltage_v[power_mask]
        aligned_current = current_a[power_mask]
        aligned_power = power_w[power_mask]
        aligned_time = power_time_ms[power_mask]
        
        if len(aligned_power) > 0:
            metrics['avg_voltage_v'] = float(np.mean(aligned_voltage))
            metrics['avg_current_a'] = float(np.mean(aligned_current))
            metrics['peak_current_a'] = float(np.max(np.abs(aligned_current)))
            metrics['avg_power_w'] = float(np.mean(aligned_power))
            metrics['peak_power_w'] = float(np.max(aligned_power))
            metrics['min_power_w'] = float(np.min(aligned_power))
            metrics['power_duration_ms'] = power_stop_ms - power_start_ms
            
            # Energy via trapezoidal integration
            if len(aligned_time) > 1:
                # numpy 2.0+ uses trapezoid, older uses trapz
                try:
                    metrics['total_energy_j'] = float(np.trapezoid(aligned_power, aligned_time / 1000.0))  # type: ignore
                except AttributeError:
                    metrics['total_energy_j'] = float(np.trapz(aligned_power, aligned_time / 1000.0))  # type: ignore
            else:
                metrics['total_energy_j'] = 0.0
    
    # Aligned duration (use power window as reference)
    metrics['aligned_duration_ms'] = metrics.get('power_duration_ms', 0.0)
    
    return metrics


# =============================================================================
# Example usage and testing
# =============================================================================

if __name__ == "__main__":
    print("Plot Generator - Test Mode")
    print("=" * 50)
    
    # Generate test data
    np.random.seed(42)
    
    # Force data (100 Hz, 5 seconds)
    force_time = np.arange(0, 5000, 10)  # ms
    force_data = np.sin(force_time / 1000) * 2 + 1 + np.random.normal(0, 0.1, len(force_time))
    
    # Power data (1000 Hz, 5 seconds)
    power_time = np.arange(0, 5000, 1)  # ms
    voltage_data = np.ones(len(power_time)) * 5.0 + np.random.normal(0, 0.02, len(power_time))
    current_data = (np.sin(power_time / 1000) * 0.05 + 0.1 + 
                   np.random.normal(0, 0.005, len(power_time)))
    power_data = voltage_data * current_data
    
    # Alignment markers
    force_start, force_stop = 500, 4500
    power_start, power_stop = 450, 4550
    
    # Create generator
    generator = PlotGenerator(dpi=100)
    
    # Generate individual plots
    print("\nGenerating individual plots...")
    
    fig = generator.create_force_plot(force_time, force_data, force_start, force_stop)
    plt.show()
    
    fig = generator.create_current_plot(power_time, current_data, power_start, power_stop)
    plt.show()
    
    # Generate combined plot
    print("\nGenerating combined plot...")
    fig = generator.create_combined_plot(
        force_time_ms=force_time,
        force_n=force_data,
        power_time_ms=power_time,
        voltage_v=voltage_data,
        current_a=current_data,
        power_w=power_data,
        force_start_ms=force_start,
        force_stop_ms=force_stop,
        power_start_ms=power_start,
        power_stop_ms=power_stop,
        title="Test M1-CV-3"
    )
    plt.show()
    
    # Generate all plots as PNG
    print("\nGenerating all plots as PNG bytes...")
    plots = generator.create_and_save_all_plots(
        force_time_ms=force_time,
        force_n=force_data,
        power_time_ms=power_time,
        voltage_v=voltage_data,
        current_a=current_data,
        power_w=power_data,
        force_start_ms=force_start,
        force_stop_ms=force_stop,
        power_start_ms=power_start,
        power_stop_ms=power_stop,
        test_id="M1-CV-3"
    )
    
    print(f"Generated {len(plots)} plots:")
    for name, data in plots.items():
        print(f"  {name}: {len(data)} bytes")
    
    # Compute metrics
    print("\nComputing metrics from aligned data...")
    metrics = compute_metrics_from_aligned_data(
        force_time, force_data,
        power_time, voltage_data, current_data, power_data,
        force_start, force_stop,
        power_start, power_stop
    )
    
    print("Metrics:")
    for key, value in metrics.items():
        print(f"  {key}: {value:.4f}")
    
    print("\nDone!")
