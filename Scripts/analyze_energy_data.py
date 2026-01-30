"""
Joulescope and Force Data Analysis Script
==========================================

This script analyzes Joulescope power measurement data (.jls files) and correlates
it with force measurements from CSV files for energy characterization of stepper motors.

Requirements:
    pip install joulescope pandas numpy matplotlib

Usage:
    1. Update the file paths in the configuration section
    2. Run the script: python analyze_energy_data.py
    3. View generated plots and summary statistics

Author: Camilo Valencia
Date: 2026-01-28
Project: Energy Harvesting T4 - Motor Characterization
"""

import os
import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime

# Compatibility for numpy trapz deprecation (renamed to trapezoid in numpy 2.0)
def integrate_trapezoid(y, x):
    """Numerical integration using trapezoidal rule, compatible with old/new numpy."""
    if hasattr(np, 'trapezoid'):
        return np.trapezoid(y, x)
    else:
        return np.trapz(y, x)  # type: ignore[attr-defined]

# Try to import joulescope - it may need to be installed
try:
    from joulescope.data_recorder import DataReader
    JOULESCOPE_AVAILABLE = True
except ImportError:
    print("Warning: joulescope package not installed. Install with: pip install joulescope")
    print("Attempting to use alternative .jls reading method...")
    JOULESCOPE_AVAILABLE = False

# =============================================================================
# CONFIGURATION - Update these paths to match your data locations
# =============================================================================

# Directory containing Joulescope .jls files
JOULESCOPE_DATA_DIR = r"C:\Users\camiv\OneDrive - MEMSYS\Documents\joulescope\M2"

# Directory containing Force measurement CSV files  
FORCE_DATA_DIR = r"C:\Users\camiv\OneDrive - MEMSYS\Documents\joulescope\M2"

# Output directory for plots and analysis
OUTPUT_DIR = r"C:\Users\camiv\OneDrive - MEMSYS\Documents\joulescope\M2\output"

# Test parameters (from your test plan)
VOLTAGE_LEVELS = [4, 5, 6]  # Volts
SPEED_LEVELS = {
    'VL': 100,
    'L': 500,
    'M': 2000,
    'H': 5000,
    'VH': 10000
}

# =============================================================================
# JOULESCOPE DATA READING FUNCTIONS
# =============================================================================

def read_joulescope_file(filepath):
    """
    Read a Joulescope .jls file and extract current, voltage, and power data.
    
    Supports both JLS v1 (older) and JLS v2 (newer) file formats.
    
    Args:
        filepath: Path to the .jls file
        
    Returns:
        DataFrame with columns: time, current, voltage, power
    """
    if not JOULESCOPE_AVAILABLE:
        print(f"Cannot read {filepath} - joulescope package not available")
        return None
    
    try:
        # Try JLS v2 format first (newer Joulescope versions)
        try:
            from pyjls import Reader as JlsReader
            with JlsReader(filepath) as reader:
                # Get available signals
                signals = reader.signals
                
                # Find current and voltage signals
                current_signal = None
                voltage_signal = None
                
                for sig in signals.values():
                    if 'current' in sig.name.lower():
                        current_signal = sig
                    elif 'voltage' in sig.name.lower():
                        voltage_signal = sig
                
                if current_signal is None or voltage_signal is None:
                    raise ValueError("Could not find current/voltage signals")
                
                # Read data
                current_data = reader.fsr(current_signal.signal_id, 0, current_signal.length)
                voltage_data = reader.fsr(voltage_signal.signal_id, 0, voltage_signal.length)
                
                # Create time array
                sample_rate = current_signal.sample_rate
                n_samples = len(current_data)
                time = np.arange(n_samples) / sample_rate
                
                df = pd.DataFrame({
                    'time': time,
                    'current': current_data,
                    'voltage': voltage_data[:len(current_data)],
                    'power': current_data * voltage_data[:len(current_data)]
                })
                
                return df
                
        except ImportError:
            pass  # pyjls not available, try legacy format
        except Exception as e:
            pass  # v2 format didn't work, try v1
        
        # Try JLS v1 format (legacy - older joulescope package)
        try:
            from joulescope.data_recorder import DataReader
            reader = DataReader().open(filepath)
            try:
                # Get the data
                start, end = reader.sample_id_range
                data = reader.samples_get(start, end)
                
                # Create time array
                sample_rate = reader.sampling_frequency
                n_samples = len(data['current'])
                time = np.arange(n_samples) / sample_rate
                
                df = pd.DataFrame({
                    'time': time,
                    'current': data['current'],
                    'voltage': data['voltage'],
                    'power': data['power']
                })
                
                return df
            finally:
                reader.close()
        except ImportError:
            raise ImportError("Neither pyjls nor joulescope package available. Install with: pip install pyjls")
            
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return None


def read_joulescope_csv_export(filepath):
    """
    Read a Joulescope CSV export file (if you exported data as CSV instead).
    
    Args:
        filepath: Path to the CSV file
        
    Returns:
        DataFrame with time, current, voltage, power columns, or None if not a power file
    """
    try:
        df = pd.read_csv(filepath)
        
        # Check if this looks like a force file (has Force_N or similar)
        force_cols = [c for c in df.columns if 'force' in c.lower()]
        if force_cols and 'current' not in str(df.columns).lower():
            # This is a force file, not a power file
            return None
        
        # Normalize column names (Joulescope exports may vary)
        col_mapping = {
            'Time': 'time',
            'time (s)': 'time',
            'Current': 'current',
            'current (A)': 'current',
            'Voltage': 'voltage',
            'voltage (V)': 'voltage',
            'Power': 'power',
            'power (W)': 'power'
        }
        df = df.rename(columns=col_mapping)
        
        # Check if this is actually a power file
        if 'current' not in df.columns and 'power' not in df.columns:
            return None  # Not a power file
        
        # Calculate power if not present
        if 'power' not in df.columns and 'current' in df.columns and 'voltage' in df.columns:
            df['power'] = df['current'] * df['voltage']
            
        return df
        
    except Exception as e:
        print(f"Error reading CSV {filepath}: {e}")
        return None


def read_force_csv(filepath):
    """
    Read force measurement CSV file from the FD setup.
    
    Args:
        filepath: Path to the CSV file
        
    Returns:
        DataFrame with time and force columns
    """
    try:
        df = pd.read_csv(filepath)
        
        # Normalize column names - adjust based on your actual CSV format
        # Common formats:
        col_mapping = {
            'Time': 'time',
            'time': 'time',
            'Force': 'force',
            'force': 'force',
            'Force (N)': 'force',
            'force_n': 'force',
            'Load': 'force'
        }
        df = df.rename(columns=col_mapping)
        
        return df
        
    except Exception as e:
        print(f"Error reading force CSV {filepath}: {e}")
        return None


# =============================================================================
# ANALYSIS FUNCTIONS
# =============================================================================

def calculate_energy_stats(power_df, start_time=None, end_time=None):
    """
    Calculate energy and power statistics from power measurement data.
    
    Args:
        power_df: DataFrame with 'time' and 'power' columns
        start_time: Optional start time for analysis window
        end_time: Optional end time for analysis window
        
    Returns:
        Dictionary with statistics
    """
    df = power_df.copy()
    
    # Apply time window if specified
    if start_time is not None:
        df = df[df['time'] >= start_time]
    if end_time is not None:
        df = df[df['time'] <= end_time]
    
    if len(df) < 2:
        return None
    
    # Calculate time step (assuming uniform sampling)
    dt = np.diff(df['time'].values).mean()
    duration = df['time'].max() - df['time'].min()
    
    # Energy = integral of power over time
    energy_j = integrate_trapezoid(df['power'].values, df['time'].values)
    energy_mj = energy_j * 1000  # Convert to millijoules
    
    stats = {
        'duration_s': duration,
        'duration_ms': duration * 1000,
        'energy_j': energy_j,
        'energy_mj': energy_mj,
        'power_mean_w': df['power'].mean(),
        'power_max_w': df['power'].max(),
        'power_min_w': df['power'].min(),
        'power_std_w': df['power'].std(),
        'current_mean_a': df['current'].mean() if 'current' in df.columns else None,
        'current_max_a': df['current'].max() if 'current' in df.columns else None,
        'voltage_mean_v': df['voltage'].mean() if 'voltage' in df.columns else None,
    }
    
    return stats


def find_movement_window(power_df, threshold_factor=2.0):
    """
    Automatically detect the start and end of motor movement based on power spike.
    
    Args:
        power_df: DataFrame with 'time' and 'power' columns
        threshold_factor: Multiple of baseline power to detect movement start
        
    Returns:
        Tuple of (start_time, end_time)
    """
    # Calculate baseline power (first 10% of data, assuming motor starts idle)
    n_baseline = max(10, int(len(power_df) * 0.1))
    baseline_power = power_df['power'].iloc[:n_baseline].mean()
    baseline_std = power_df['power'].iloc[:n_baseline].std()
    
    # Threshold for detecting movement
    threshold = baseline_power + threshold_factor * max(baseline_std, baseline_power * 0.5)
    
    # Find where power exceeds threshold
    above_threshold = power_df['power'] > threshold
    
    if not above_threshold.any():
        return None, None
    
    # Find first and last points above threshold
    start_idx = above_threshold.idxmax()
    end_idx = above_threshold[::-1].idxmax()
    
    start_time = power_df.loc[start_idx, 'time']
    end_time = power_df.loc[end_idx, 'time']
    
    return start_time, end_time


def calculate_force_stats(force_df, start_time=None, end_time=None):
    """
    Calculate force statistics from force measurement data.
    
    Args:
        force_df: DataFrame with 'time' and 'force' columns
        start_time: Optional start time for analysis window
        end_time: Optional end time for analysis window
        
    Returns:
        Dictionary with statistics
    """
    df = force_df.copy()
    
    if start_time is not None:
        df = df[df['time'] >= start_time]
    if end_time is not None:
        df = df[df['time'] <= end_time]
    
    if len(df) < 1:
        return None
    
    stats = {
        'force_mean_n': df['force'].mean(),
        'force_max_n': df['force'].max(),
        'force_min_n': df['force'].min(),
        'force_std_n': df['force'].std(),
    }
    
    return stats


# =============================================================================
# PLOTTING FUNCTIONS
# =============================================================================

def plot_power_profile(power_df, title="Power Profile", save_path=None):
    """
    Plot power, current, and voltage over time.
    """
    fig, axes = plt.subplots(3, 1, figsize=(12, 8), sharex=True)
    
    # Power
    axes[0].plot(power_df['time'], power_df['power'], 'b-', linewidth=0.5)
    axes[0].set_ylabel('Power (W)')
    axes[0].set_title(title)
    axes[0].grid(True, alpha=0.3)
    
    # Current
    if 'current' in power_df.columns:
        axes[1].plot(power_df['time'], power_df['current'] * 1000, 'r-', linewidth=0.5)
        axes[1].set_ylabel('Current (mA)')
        axes[1].grid(True, alpha=0.3)
    
    # Voltage
    if 'voltage' in power_df.columns:
        axes[2].plot(power_df['time'], power_df['voltage'], 'g-', linewidth=0.5)
        axes[2].set_ylabel('Voltage (V)')
        axes[2].grid(True, alpha=0.3)
    
    axes[2].set_xlabel('Time (s)')
    
    plt.tight_layout()
    
    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"Saved plot to {save_path}")
    
    return fig


def plot_power_and_force(power_df, force_df, title="Power and Force", save_path=None):
    """
    Plot power and force on synchronized time axes.
    """
    fig, axes = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    
    # Power
    axes[0].plot(power_df['time'], power_df['power'], 'b-', linewidth=0.5, label='Power')
    axes[0].set_ylabel('Power (W)')
    axes[0].set_title(title)
    axes[0].grid(True, alpha=0.3)
    axes[0].legend()
    
    # Force
    axes[1].plot(force_df['time'], force_df['force'], 'r-', linewidth=1, label='Force')
    axes[1].set_ylabel('Force (N)')
    axes[1].set_xlabel('Time (s)')
    axes[1].grid(True, alpha=0.3)
    axes[1].legend()
    
    plt.tight_layout()
    
    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"Saved plot to {save_path}")
    
    return fig


def plot_energy_comparison(results_df, group_by='voltage', save_path=None):
    """
    Create bar chart comparing energy consumption across test conditions.
    """
    fig, ax = plt.subplots(figsize=(10, 6))
    
    if group_by == 'voltage':
        pivot = results_df.pivot(index='speed', columns='voltage', values='energy_mj')
    else:
        pivot = results_df.pivot(index='voltage', columns='speed', values='energy_mj')
    
    pivot.plot(kind='bar', ax=ax)
    ax.set_ylabel('Energy (mJ)')
    ax.set_title('Energy Consumption Comparison')
    ax.legend(title=group_by.capitalize())
    ax.grid(True, alpha=0.3, axis='y')
    
    plt.xticks(rotation=45)
    plt.tight_layout()
    
    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"Saved plot to {save_path}")
    
    return fig


def plot_thrust_vs_energy(results_df, save_path=None):
    """
    Scatter plot of thrust vs energy consumption, colored by voltage.
    """
    fig, ax = plt.subplots(figsize=(10, 6))
    
    for voltage in results_df['voltage'].unique():
        subset = results_df[results_df['voltage'] == voltage]
        ax.scatter(subset['energy_mj'], subset['force_max_n'], 
                   label=f'{voltage}V', s=100, alpha=0.7)
    
    ax.set_xlabel('Energy (mJ)')
    ax.set_ylabel('Maximum Thrust (N)')
    ax.set_title('Thrust vs Energy Consumption')
    ax.legend(title='Voltage')
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"Saved plot to {save_path}")
    
    return fig


# =============================================================================
# BATCH PROCESSING
# =============================================================================

def process_all_files(joulescope_dir, force_dir, output_dir):
    """
    Process all Joulescope and force files in the specified directories.
    
    Returns:
        DataFrame with all results
    """
    os.makedirs(output_dir, exist_ok=True)
    
    results = []
    
    # Find all Joulescope files
    jls_files = glob.glob(os.path.join(joulescope_dir, "*.jls"))
    csv_power_files = glob.glob(os.path.join(joulescope_dir, "*.csv"))
    
    print(f"Found {len(jls_files)} .jls files and {len(csv_power_files)} .csv files")
    
    # Process each file
    for filepath in jls_files + csv_power_files:
        filename = os.path.basename(filepath)
        print(f"\nProcessing: {filename}")
        
        # Read power data
        if filepath.endswith('.jls'):
            power_df = read_joulescope_file(filepath)
        else:
            power_df = read_joulescope_csv_export(filepath)
        
        if power_df is None:
            print(f"  Skipping - not a power measurement file or could not read")
            continue
        
        # Check if we have the required columns
        if 'power' not in power_df.columns:
            print(f"  Skipping - no power data found")
            continue
        
        # Try to find matching force file
        # Adjust naming convention as needed
        force_filename = filename.replace('.jls', '_force.csv').replace('.csv', '_force.csv')
        force_path = os.path.join(force_dir, force_filename)
        
        force_df = None
        if os.path.exists(force_path):
            force_df = read_force_csv(force_path)
        
        # Detect movement window
        start_time, end_time = find_movement_window(power_df)
        
        if start_time is None:
            print(f"  Warning: Could not detect movement window")
            start_time = power_df['time'].min()
            end_time = power_df['time'].max()
        
        # Calculate statistics
        energy_stats = calculate_energy_stats(power_df, start_time, end_time)
        
        if energy_stats:
            result = {
                'filename': filename,
                **energy_stats
            }
            
            if force_df is not None:
                force_stats = calculate_force_stats(force_df, start_time, end_time)
                if force_stats:
                    result.update(force_stats)
            
            results.append(result)
            
            # Generate plots
            plot_path = os.path.join(output_dir, f"{filename.replace('.', '_')}_power.png")
            plot_power_profile(power_df, title=filename, save_path=plot_path)
            
            if force_df is not None:
                plot_path = os.path.join(output_dir, f"{filename.replace('.', '_')}_combined.png")
                plot_power_and_force(power_df, force_df, title=filename, save_path=plot_path)
        
        plt.close('all')  # Free memory
    
    # Create results DataFrame
    results_df = pd.DataFrame(results)
    
    # Save results to CSV
    results_path = os.path.join(output_dir, 'analysis_results.csv')
    results_df.to_csv(results_path, index=False)
    print(f"\nResults saved to {results_path}")
    
    return results_df


# =============================================================================
# MAIN EXECUTION
# =============================================================================

def main():
    """
    Main entry point for the analysis script.
    """
    print("=" * 60)
    print("Joulescope & Force Data Analysis Script")
    print("=" * 60)
    print(f"Started at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print()
    
    # Check if paths are configured
    if "path/to" in JOULESCOPE_DATA_DIR:
        print("ERROR: Please update the configuration section with your actual file paths!")
        print()
        print("Configuration needed:")
        print(f"  JOULESCOPE_DATA_DIR = {JOULESCOPE_DATA_DIR}")
        print(f"  FORCE_DATA_DIR = {FORCE_DATA_DIR}")
        print(f"  OUTPUT_DIR = {OUTPUT_DIR}")
        print()
        print("Edit this script and update the paths at the top of the file.")
        return
    
    # Process all files
    results_df = process_all_files(JOULESCOPE_DATA_DIR, FORCE_DATA_DIR, OUTPUT_DIR)
    
    # Print summary
    print()
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Processed {len(results_df)} test files")
    
    if len(results_df) > 0:
        print(f"\nEnergy consumption range:")
        print(f"  Min: {results_df['energy_mj'].min():.1f} mJ")
        print(f"  Max: {results_df['energy_mj'].max():.1f} mJ")
        print(f"  Mean: {results_df['energy_mj'].mean():.1f} mJ")
        
        if 'force_max_n' in results_df.columns:
            print(f"\nMaximum thrust range:")
            print(f"  Min: {results_df['force_max_n'].min():.2f} N")
            print(f"  Max: {results_df['force_max_n'].max():.2f} N")
    
    print()
    print(f"Finished at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")


if __name__ == "__main__":
    main()
