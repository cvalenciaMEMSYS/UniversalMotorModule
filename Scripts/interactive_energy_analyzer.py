"""
Interactive Energy & Force Analysis Tool
=========================================

An interactive GUI tool for analyzing Joulescope power data and force measurements
with manual time alignment and region-of-interest selection.

Features:
- Batch load all test files from a motor folder
- Load Joulescope .jls or exported CSV files
- Load force measurement CSV files  
- Manual time alignment with slider/offset controls
- Interactive region selection with draggable markers
- Calculate statistics (min, max, avg, energy) for selected region
- Save results per test and export to CSV/Markdown
- Auto-identify best configurations (min energy, max thrust, best efficiency)
- Generate comparison plots for best configurations

Requirements:
    pip install joulescope pandas numpy matplotlib pyjls

Usage:
    python interactive_energy_analyzer.py

Author: Camilo Valencia
Date: 2026-01-28
Project: Energy Harvesting T4 - Motor Characterization
"""

import os
import sys
import glob
import re
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider, Button, TextBox
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.backends._backend_tk import NavigationToolbar2Tk
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from datetime import datetime
from typing import Dict, List, Optional, Tuple

# Compatibility for numpy trapz deprecation (renamed to trapezoid in numpy 2.0)
def integrate_trapezoid(y, x):
    """Numerical integration using trapezoidal rule, compatible with old/new numpy."""
    if hasattr(np, 'trapezoid'):
        return np.trapezoid(y, x)
    else:
        return np.trapz(y, x)  # type: ignore[attr-defined]

# Try to import joulescope - multiple packages may be needed
JOULESCOPE_AVAILABLE = False
try:
    from pyjls import Reader as JlsReader
    JOULESCOPE_AVAILABLE = True
    print("Using pyjls for JLS v2 file support")
except ImportError:
    try:
        from joulescope.data_recorder import DataReader
        JOULESCOPE_AVAILABLE = True
        print("Using joulescope for JLS v1 file support")
    except ImportError:
        print("Note: Neither pyjls nor joulescope package installed.")
        print("To read .jls files: pip install pyjls")
        print("Only CSV files will be supported.")


# =============================================================================
# TEST FILE DISCOVERY AND PARSING HELPERS
# =============================================================================

def parse_test_id(filename: str) -> Optional[Dict]:
    """
    Parse a test filename to extract motor, speed, voltage, and direction info.
    
    Patterns:
        M1-CVH-6-20260119_150831.jls -> Motor 1, Constant Very High, 6V, Normal
        M2-CL-4-R-20260119_162132.jls -> Motor 2, Constant Low, 4V, Reverse
        M1-TVH-4-20260119_120553.jls -> Motor 1, Trapezoidal Very High, 4V, Normal
        M2-BD-OFF-R_0.0V_20260119_163609.csv -> Motor 2, Backdrive Off, Reverse
    
    Returns dict with: motor, profile, speed_code, speed_value, voltage, direction, test_id
    """
    # Speed code to value mapping
    speed_map = {
        'VL': 100, 'L': 500, 'M': 2000, 'H': 5000, 'VH': 10000, 'UH': 20000
    }
    
    basename = os.path.basename(filename)
    
    # Handle backdrive files specially
    if 'BD-OFF' in basename:
        # M1-BD-OFF-R-Rigid_0.0V_... or M2-BD-OFF_0.0V_...
        match = re.match(r'^(M\d+)-BD-OFF(-R)?', basename)
        if match:
            motor = match.group(1)
            direction = 'R' if match.group(2) else 'N'
            # Check for Spring/Rigid suffix
            if 'Spring' in basename:
                direction = 'Spring'
            elif 'Rigid' in basename:
                direction = 'Rigid'
            return {
                'motor': motor,
                'profile': 'BD',
                'speed_code': 'OFF',
                'speed_value': 0,
                'voltage': 0,
                'direction': direction,
                'test_id': f"{motor}-BD-OFF-{direction}"
            }
        return None
    
    # Main pattern: M[n]-[Profile][Speed]-[Voltage](-R) OR M[n]-[Profile][Speed]-R-[Voltage]
    # Examples: M1-CVH-6, M2-CL-4-R, M1-TVH-4, M1-CVH-R-4 (reverse before voltage)
    
    # Try pattern 1: M1-CVH-6 or M1-CVH-6-R (direction suffix)
    match = re.match(r'^(M\d+)-([CT]?)([VLHU]+|M)-(\d+)(-R)?', basename)
    
    # Try pattern 2: M1-CVH-R-4 (direction before voltage)
    if not match:
        match2 = re.match(r'^(M\d+)-([CT]?)([VLHU]+|M)-R-(\d+)', basename)
        if match2:
            motor = match2.group(1)
            profile_prefix = match2.group(2) or 'C'
            speed_code = match2.group(3)
            voltage = int(match2.group(4))
            direction = 'R'
            
            if profile_prefix == 'T':
                profile = 'Trapezoidal'
            elif profile_prefix == 'S':
                profile = 'S-Curve'
            else:
                profile = 'Constant'
            
            test_id = f"{motor}-{profile_prefix}{speed_code}-{voltage}-R"
            
            return {
                'motor': motor,
                'profile': profile,
                'speed_code': speed_code,
                'speed_value': speed_map.get(speed_code, 0),
                'voltage': voltage,
                'direction': direction,
                'test_id': test_id
            }
    
    if match:
        motor = match.group(1)
        profile_prefix = match.group(2) or 'C'  # Default to Constant if no prefix
        speed_code = match.group(3)
        voltage = int(match.group(4))
        direction = 'R' if match.group(5) else 'N'
        
        # Determine profile type
        if profile_prefix == 'T':
            profile = 'Trapezoidal'
        elif profile_prefix == 'S':
            profile = 'S-Curve'
        else:
            profile = 'Constant'
        
        # Build test_id
        dir_suffix = '-R' if direction == 'R' else ''
        test_id = f"{motor}-{profile_prefix}{speed_code}-{voltage}{dir_suffix}"
        
        return {
            'motor': motor,
            'profile': profile,
            'speed_code': speed_code,
            'speed_value': speed_map.get(speed_code, 0),
            'voltage': voltage,
            'direction': direction,
            'test_id': test_id
        }
    
    return None


def discover_test_files(folder_path: str) -> List[Dict]:
    """
    Discover all test file pairs in a folder.
    
    Returns list of dicts with:
        - jls_file: path to .jls file
        - csv_file: path to matching force .csv file (or None)
        - test_info: parsed test ID info
        - status: 'complete' or 'jls_only'
    """
    tests = []
    
    # Find all JLS files
    jls_files = glob.glob(os.path.join(folder_path, "*.jls"))
    
    for jls_file in jls_files:
        basename = os.path.basename(jls_file)
        test_info = parse_test_id(basename)
        
        if test_info is None:
            print(f"Warning: Could not parse test ID from: {basename}")
            continue
        
        # Try to find matching force CSV
        # JLS: M2-CVH-6-20260119_150831.jls
        # CSV: M2-CVH-6_6.0V_20260119_160911.csv
        test_id = test_info['test_id']
        motor = test_info['motor']
        
        # Look for CSV files that start with the test_id pattern
        # Need to handle both formats:
        #   M2-CVH-6_6.0V_... (forward)
        #   M2-CVH-6-R_6.0V_... (reverse) - should NOT match M2-CVH-6
        csv_pattern = os.path.join(folder_path, f"{test_id}*.csv")
        matching_csvs = glob.glob(csv_pattern)
        
        # Also try underscore pattern
        csv_pattern2 = os.path.join(folder_path, f"{test_id}_*.csv")
        matching_csvs.extend(glob.glob(csv_pattern2))
        
        # Remove duplicates
        matching_csvs = list(set(matching_csvs))
        
        csv_file = None
        if matching_csvs:
            csv_file = matching_csvs[0]
        
        tests.append({
            'jls_file': jls_file,
            'csv_file': csv_file,
            'test_info': test_info,
            'status': 'complete' if csv_file else 'jls_only'
        })
    
    # Also find backdrive-only CSV files (no JLS)
    csv_files = glob.glob(os.path.join(folder_path, "*BD-OFF*.csv"))
    for csv_file in csv_files:
        test_info = parse_test_id(os.path.basename(csv_file))
        if test_info and test_info.get('profile') == 'BD':
            # Check if we already have this from JLS search
            already_found = any(t.get('csv_file') == csv_file for t in tests)
            if not already_found:
                tests.append({
                    'jls_file': None,
                    'csv_file': csv_file,
                    'test_info': test_info,
                    'status': 'backdrive_only'
                })
    
    # Sort by test_id for consistent ordering
    tests.sort(key=lambda x: (
        x['test_info']['motor'],
        x['test_info']['speed_value'],
        x['test_info']['voltage'],
        x['test_info']['direction']
    ))
    
    return tests


class EnergyAnalyzer:
    """Interactive analyzer for energy and force data with batch processing."""
    
    def __init__(self):
        self.power_df = None
        self.force_df = None
        self.time_offset = 0.0  # Offset to apply to force data
        self.selection_start = None
        self.selection_end = None
        
        # Draggable marker state
        self.start_lines = []  # Green lines (one per axis)
        self.end_lines = []    # Red lines (one per axis)
        self.selection_patches = []  # Yellow highlight regions
        self.dragging = None   # 'start', 'end', or None
        self.drag_threshold = 0.01  # Fraction of axis width for click detection
        
        # Batch processing state
        self.test_queue: List[Dict] = []  # List of tests to process
        self.current_test_idx: int = -1  # Currently displayed test index
        self.results: List[Dict] = []  # Accumulated results
        self.motor_id: str = ""  # Current motor being processed
        self.output_dir: str = ""  # Output directory for exports
        
        # Create main window
        self.root = tk.Tk()
        self.root.title("Energy & Force Analyzer - Motor Characterization")
        self.root.geometry("1600x950")
        
        self._create_ui()
        
    def _create_ui(self):
        """Create the user interface."""
        # Main frame
        main_frame = ttk.Frame(self.root, padding="5")
        main_frame.grid(row=0, column=0, sticky="nsew")
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        
        # Left panel - Controls
        control_frame = ttk.LabelFrame(main_frame, text="Controls", padding="5")
        control_frame.grid(row=0, column=0, sticky="ns", padx=5, pady=5)
        
        # === BATCH PROCESSING SECTION (NEW) ===
        batch_frame = ttk.LabelFrame(control_frame, text="Batch Processing", padding="5")
        batch_frame.grid(row=0, column=0, sticky="ew", pady=5)
        
        load_frame = ttk.Frame(batch_frame)
        load_frame.grid(row=0, column=0, pady=2, sticky="ew")
        
        ttk.Button(load_frame, text="📁 Load Folder", 
                   command=self._load_motor_folder).pack(side="left", padx=2)
        ttk.Button(load_frame, text="📂 Load Previous", 
                   command=self._load_previous_results).pack(side="left", padx=2)
        
        # Progress indicator
        self.progress_label = ttk.Label(batch_frame, text="No folder loaded", wraplength=220)
        self.progress_label.grid(row=1, column=0, pady=2)
        
        # Test queue listbox
        self.queue_frame = ttk.Frame(batch_frame)
        self.queue_frame.grid(row=2, column=0, pady=2, sticky="nsew")
        
        self.queue_listbox = tk.Listbox(self.queue_frame, height=8, width=30, font=("Courier", 8))
        queue_scroll = ttk.Scrollbar(self.queue_frame, orient="vertical", command=self.queue_listbox.yview)
        self.queue_listbox.configure(yscrollcommand=queue_scroll.set)
        self.queue_listbox.pack(side="left", fill="both", expand=True)
        queue_scroll.pack(side="right", fill="y")
        self.queue_listbox.bind('<<ListboxSelect>>', self._on_queue_select)
        
        # Navigation buttons
        nav_frame = ttk.Frame(batch_frame)
        nav_frame.grid(row=3, column=0, pady=5, sticky="ew")
        
        ttk.Button(nav_frame, text="◀ Prev", width=8,
                   command=self._prev_test).pack(side="left", padx=2)
        ttk.Button(nav_frame, text="💾 Save & Next ▶", width=14,
                   command=self._save_and_next).pack(side="left", padx=2)
        ttk.Button(nav_frame, text="Skip ▶", width=6,
                   command=self._skip_test).pack(side="left", padx=2)
        
        # Export buttons
        export_frame = ttk.Frame(batch_frame)
        export_frame.grid(row=4, column=0, pady=5, sticky="ew")
        
        ttk.Button(export_frame, text="📊 Export Results", 
                   command=self._export_results).pack(side="left", padx=2)
        ttk.Button(export_frame, text="📈 Generate Plots", 
                   command=self._generate_plots).pack(side="left", padx=2)
        
        ttk.Separator(control_frame, orient='horizontal').grid(row=1, column=0, sticky="ew", pady=10)
        
        # === SINGLE FILE LOADING SECTION ===
        file_frame = ttk.LabelFrame(control_frame, text="Single File Loading", padding="5")
        file_frame.grid(row=2, column=0, sticky="ew", pady=5)
        
        ttk.Button(file_frame, text="Load Joulescope (.jls/.csv)", 
                   command=self._load_power_file).grid(row=0, column=0, pady=2, sticky="ew")
        ttk.Button(file_frame, text="Load Force CSV", 
                   command=self._load_force_file).grid(row=1, column=0, pady=2, sticky="ew")
        ttk.Button(file_frame, text="Load Matched Pair", 
                   command=self._load_matched_pair).grid(row=2, column=0, pady=2, sticky="ew")
        
        self.power_label = ttk.Label(file_frame, text="Power: Not loaded", wraplength=200)
        self.power_label.grid(row=3, column=0, pady=2)
        self.force_label = ttk.Label(file_frame, text="Force: Not loaded", wraplength=200)
        self.force_label.grid(row=4, column=0, pady=2)
        
        # Time alignment section
        align_frame = ttk.LabelFrame(control_frame, text="Time Alignment", padding="5")
        align_frame.grid(row=3, column=0, sticky="ew", pady=5)
        
        ttk.Label(align_frame, text="Force Time Offset (s):").grid(row=0, column=0, sticky="w")
        self.offset_var = tk.StringVar(value="0.0")
        self.offset_entry = ttk.Entry(align_frame, textvariable=self.offset_var, width=10)
        self.offset_entry.grid(row=0, column=1, padx=5)
        self.offset_entry.bind('<Return>', self._apply_offset)
        
        ttk.Button(align_frame, text="Apply Offset", 
                   command=self._apply_offset).grid(row=1, column=0, columnspan=2, pady=5)
        
        offset_btn_frame = ttk.Frame(align_frame)
        offset_btn_frame.grid(row=2, column=0, columnspan=2)
        ttk.Button(offset_btn_frame, text="-1s", width=5,
                   command=lambda: self._adjust_offset(-1)).pack(side="left", padx=2)
        ttk.Button(offset_btn_frame, text="-0.1s", width=5,
                   command=lambda: self._adjust_offset(-0.1)).pack(side="left", padx=2)
        ttk.Button(offset_btn_frame, text="+0.1s", width=5,
                   command=lambda: self._adjust_offset(0.1)).pack(side="left", padx=2)
        ttk.Button(offset_btn_frame, text="+1s", width=5,
                   command=lambda: self._adjust_offset(1)).pack(side="left", padx=2)
        
        # Region selection section
        select_frame = ttk.LabelFrame(control_frame, text="Region Selection", padding="5")
        select_frame.grid(row=4, column=0, sticky="ew", pady=5)
        
        ttk.Label(select_frame, text="Click and drag on plot to select region").grid(row=0, column=0)
        ttk.Label(select_frame, text="or enter manually:").grid(row=1, column=0)
        
        manual_frame = ttk.Frame(select_frame)
        manual_frame.grid(row=2, column=0, pady=5)
        
        ttk.Label(manual_frame, text="Start (s):").grid(row=0, column=0)
        self.start_var = tk.StringVar(value="0.0")
        ttk.Entry(manual_frame, textvariable=self.start_var, width=8).grid(row=0, column=1)
        
        ttk.Label(manual_frame, text="End (s):").grid(row=0, column=2, padx=(10,0))
        self.end_var = tk.StringVar(value="1.0")
        ttk.Entry(manual_frame, textvariable=self.end_var, width=8).grid(row=0, column=3)
        
        ttk.Button(select_frame, text="Apply Selection", 
                   command=self._apply_manual_selection).grid(row=3, column=0, pady=5)
        ttk.Button(select_frame, text="Select All", 
                   command=self._select_all).grid(row=4, column=0, pady=2)
        
        # Statistics display
        stats_frame = ttk.LabelFrame(control_frame, text="Selected Region Statistics", padding="5")
        stats_frame.grid(row=5, column=0, sticky="ew", pady=5)
        
        self.stats_text = tk.Text(stats_frame, height=12, width=35, font=("Courier", 9))
        self.stats_text.grid(row=0, column=0, sticky="nsew")
        self.stats_text.insert("1.0", "Select a region to see statistics...")
        
        # Export button (single file mode)
        ttk.Button(control_frame, text="Export Single Statistics", 
                   command=self._export_stats).grid(row=6, column=0, pady=5)
        
        # Right panel - Plots
        plot_frame = ttk.Frame(main_frame)
        plot_frame.grid(row=0, column=1, sticky="nsew", padx=5, pady=5)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(0, weight=1)
        
        # Create matplotlib figure
        self.fig, self.axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
        self.fig.tight_layout(pad=3.0)
        
        self.axes[0].set_ylabel('Power (W)')
        self.axes[0].set_title('Power Consumption')
        self.axes[0].grid(True, alpha=0.3)
        
        self.axes[1].set_ylabel('Current (mA)')
        self.axes[1].set_title('Current Draw')
        self.axes[1].grid(True, alpha=0.3)
        
        self.axes[2].set_ylabel('Force (N)')
        self.axes[2].set_xlabel('Time (s)')
        self.axes[2].set_title('Force Measurement')
        self.axes[2].grid(True, alpha=0.3)
        
        # Embed in tkinter
        self.canvas = FigureCanvasTkAgg(self.fig, master=plot_frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        # Add navigation toolbar for zoom/pan (time axis zoom, amplitude stays linked)
        toolbar_frame = ttk.Frame(plot_frame)
        toolbar_frame.pack(fill=tk.X, side=tk.BOTTOM)
        self.toolbar = NavigationToolbar2Tk(self.canvas, toolbar_frame)
        self.toolbar.update()
        
        # Connect mouse events for draggable markers
        self.fig.canvas.mpl_connect('button_press_event', self._on_mouse_press)
        self.fig.canvas.mpl_connect('button_release_event', self._on_mouse_release)
        self.fig.canvas.mpl_connect('motion_notify_event', self._on_mouse_move)
        
    def _load_power_file(self, filepath=None):
        """Load a Joulescope power measurement file."""
        if filepath is None:
            filetypes = [
                ("All supported", "*.jls *.csv"),
                ("Joulescope files", "*.jls"),
                ("CSV files", "*.csv")
            ]
            filepath = filedialog.askopenfilename(filetypes=filetypes)
        
        if not filepath:
            return
        
        try:
            if filepath.endswith('.jls'):
                if not JOULESCOPE_AVAILABLE:
                    messagebox.showerror("Error", "joulescope package not installed. Please use CSV export.")
                    return
                self.power_df = self._read_jls_file(filepath)
            else:
                self.power_df = self._read_power_csv(filepath)
            
            if self.power_df is not None:
                self.power_label.config(text=f"Power: {os.path.basename(filepath)}")
                # Initialize selection to full data range
                self.selection_start = self.power_df['time'].min()
                self.selection_end = self.power_df['time'].max()
                self.start_var.set(f"{self.selection_start:.3f}")
                self.end_var.set(f"{self.selection_end:.3f}")
                self._update_plot()
                self._calculate_stats()
                
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load file:\n{e}")
    
    def _load_force_file(self, filepath=None):
        """Load a force measurement CSV file."""
        if filepath is None:
            filetypes = [("CSV files", "*.csv")]
            filepath = filedialog.askopenfilename(filetypes=filetypes)
        
        if not filepath:
            return
        
        try:
            self.force_df = self._read_force_csv(filepath)
            
            if self.force_df is not None:
                self.force_label.config(text=f"Force: {os.path.basename(filepath)}")
                
                # If no power data, initialize selection from force data
                # (for backdrive-only tests or force-only analysis)
                if self.power_df is None and self.selection_start is None:
                    # Use force time range for selection (with current offset applied)
                    force_time = self.force_df['time'] + self.time_offset
                    self.selection_start = force_time.min()
                    self.selection_end = force_time.max()
                    self.start_var.set(f"{self.selection_start:.3f}")
                    self.end_var.set(f"{self.selection_end:.3f}")
                
                self._update_plot()
                self._calculate_stats()  # Also update stats for force-only tests
                
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load file:\n{e}")
    
    def _load_matched_pair(self):
        """Load a matching pair of Joulescope and force files."""
        # First, select the joulescope file
        filetypes = [
            ("Joulescope files", "*.jls"),
            ("CSV files", "*.csv")
        ]
        js_path = filedialog.askopenfilename(title="Select Joulescope File", filetypes=filetypes)
        
        if not js_path:
            return
        
        # Try to find matching force file
        base_name = os.path.basename(js_path)
        dir_path = os.path.dirname(js_path)
        
        # Extract test ID from filename
        # Handles patterns like:
        #   M2-CVH-6-20260119_150831.jls -> test_id = "M2-CVH-6"
        #   M2-CL-4_4.0V_20260119_143723.jls -> test_id = "M2-CL-4"
        import re
        # Match motor ID pattern: M[number]-[letters]-[number]
        match = re.match(r'^(M\d+-[A-Za-z]+-\d+)', base_name)
        test_id = match.group(1) if match else base_name.split('_')[0]
        
        if match:
            # Look for matching force CSV with this test ID
            # Exclude files with -R suffix (reverse direction)
            potential_force_files = glob.glob(os.path.join(dir_path, f"{test_id}*.csv"))
            # Filter: exclude files with -R after test_id, and not the jls itself
            force_files = [
                f for f in potential_force_files 
                if f != js_path and not os.path.basename(f).startswith(f"{test_id}-R")
            ]
            
            if force_files:
                # Use the first match
                self._load_power_file(js_path)
                self._load_force_file(force_files[0])
                print(f"Auto-matched: {os.path.basename(force_files[0])}")
                return
        
        # If no automatic match, prompt for force file
        self._load_power_file(js_path)
        messagebox.showinfo("Info", f"No matching force file found for '{test_id}'. Please select manually.")
        self._load_force_file()
    
    def _read_jls_file(self, filepath):
        """Read a Joulescope .jls file (supports both v1 and v2 formats)."""
        
        # Try JLS v2 format first (newer Joulescope versions use pyjls)
        try:
            from pyjls import Reader as JlsReader
            with JlsReader(filepath) as reader:
                # Get available signals
                signals = reader.signals
                
                # Find current and voltage signals
                current_signal = None
                voltage_signal = None
                
                for sig in signals.values():
                    name_lower = sig.name.lower()
                    if 'current' in name_lower and current_signal is None:
                        current_signal = sig
                    elif 'voltage' in name_lower and voltage_signal is None:
                        voltage_signal = sig
                
                if current_signal is None or voltage_signal is None:
                    raise ValueError("Could not find current/voltage signals in JLS v2 file")
                
                # Read data
                current_data = reader.fsr(current_signal.signal_id, 0, current_signal.length)
                voltage_data = reader.fsr(voltage_signal.signal_id, 0, voltage_signal.length)
                
                # Create time array
                sample_rate = current_signal.sample_rate
                n_samples = len(current_data)
                time = np.arange(n_samples) / sample_rate
                
                # Ensure same length
                min_len = min(len(current_data), len(voltage_data))
                current_data = current_data[:min_len]
                voltage_data = voltage_data[:min_len]
                time = time[:min_len]
                
                df = pd.DataFrame({
                    'time': time,
                    'current': current_data,
                    'voltage': voltage_data,
                    'power': current_data * voltage_data
                })
                
                print(f"Loaded {len(df)} samples at {sample_rate:.0f} Hz")
                
                return df
                
        except ImportError:
            pass  # pyjls not available, try legacy format
        except Exception as e:
            print(f"JLS v2 read failed ({e}), trying v1 format...")
        
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
                
                print(f"Loaded {len(df)} samples at {sample_rate:.0f} Hz")
                
                return df
            finally:
                reader.close()
                
        except ImportError:
            raise ImportError("Neither pyjls nor joulescope package available. Install with: pip install pyjls")
        except Exception as e:
            raise Exception(f"Could not read JLS file with either v1 or v2 format: {e}")
    
    def _read_power_csv(self, filepath):
        """Read a Joulescope CSV export."""
        df = pd.read_csv(filepath)
        
        # Normalize column names
        col_mapping = {
            'Time': 'time', 'time': 'time', 'time (s)': 'time', 'Time (s)': 'time',
            'Current': 'current', 'current': 'current', 'current (A)': 'current', 'Current (A)': 'current',
            'Voltage': 'voltage', 'voltage': 'voltage', 'voltage (V)': 'voltage', 'Voltage (V)': 'voltage',
            'Power': 'power', 'power': 'power', 'power (W)': 'power', 'Power (W)': 'power'
        }
        df = df.rename(columns=col_mapping)
        
        # Calculate power if not present
        if 'power' not in df.columns and 'current' in df.columns and 'voltage' in df.columns:
            df['power'] = df['current'] * df['voltage']
        
        return df
    
    def _read_force_csv(self, filepath):
        """Read a force measurement CSV file.
        
        Expected format: First column is timestamp (in ms), second column is force (in N).
        First row is header and will be skipped.
        """
        # Read CSV, assuming header row exists
        df = pd.read_csv(filepath, header=0)
        
        # Force the column interpretation: first = time, second = force
        # regardless of header names
        if len(df.columns) >= 2:
            # Get the first two columns by position
            time_col = df.columns[0]
            force_col = df.columns[1]
            
            # Create clean dataframe with standard names
            df = pd.DataFrame({
                'time': pd.to_numeric(df[time_col], errors='coerce'),
                'force': pd.to_numeric(df[force_col], errors='coerce')
            })
            
            # Drop any rows that couldn't be parsed as numbers
            df = df.dropna()
            
            # Convert timestamp from ms to seconds
            # (force data is typically at 10Hz with ms timestamps)
            if df['time'].max() > 1000:
                df['time'] = df['time'] / 1000.0
                print(f"Converted time from ms to seconds")
            
            print(f"Loaded {len(df)} force samples")
            return df
        else:
            raise ValueError(f"Expected at least 2 columns, found {len(df.columns)}")
    
    def _update_plot(self):
        """Update the plot with current data."""
        # Clear all axes (this also removes all patches/lines)
        for ax in self.axes:
            ax.clear()
            ax.grid(True, alpha=0.3)
        
        # Clear references to removed artists
        self.selection_patches = []
        self.start_lines = []
        self.end_lines = []
        
        self.axes[0].set_ylabel('Power (W)')
        self.axes[0].set_title('Power Consumption')
        self.axes[1].set_ylabel('Current (mA)')
        self.axes[1].set_title('Current Draw')
        self.axes[2].set_ylabel('Force (N)')
        self.axes[2].set_xlabel('Time (s)')
        self.axes[2].set_title('Force Measurement')
        
        # Plot power data
        if self.power_df is not None:
            self.axes[0].plot(self.power_df['time'], self.power_df['power'], 
                             'b-', linewidth=0.5, label='Power')
            
            if 'current' in self.power_df.columns:
                self.axes[1].plot(self.power_df['time'], self.power_df['current'] * 1000, 
                                 'r-', linewidth=0.5, label='Current')
        
        # Plot force data with offset
        if self.force_df is not None:
            adjusted_time = self.force_df['time'] + self.time_offset
            self.axes[2].plot(adjusted_time, self.force_df['force'], 
                             'g-', linewidth=1, marker='.', markersize=3, label='Force')
        
        # Draw selection region if exists
        self._draw_selection()
        
        self.canvas.draw()
    
    def _draw_selection(self):
        """Draw the selection region on all plots."""
        # Note: patches/lines are already cleared by _update_plot's ax.clear()
        # Just draw fresh ones
        
        if self.selection_start is not None and self.selection_end is not None:
            for ax in self.axes:
                patch = ax.axvspan(self.selection_start, self.selection_end, 
                                   alpha=0.15, color='yellow')
                self.selection_patches.append(patch)
        
        # Draw draggable markers (green=start, red=end)
        if self.selection_start is not None and self.selection_end is not None:
            for ax in self.axes:
                start_line = ax.axvline(self.selection_start, color='green', linestyle='-', 
                                        linewidth=2, alpha=0.9, picker=5)
                end_line = ax.axvline(self.selection_end, color='red', linestyle='-', 
                                      linewidth=2, alpha=0.9, picker=5)
                self.start_lines.append(start_line)
                self.end_lines.append(end_line)
    
    def _on_mouse_press(self, event):
        """Handle mouse press - check if clicking near a marker."""
        if event.inaxes not in self.axes:
            return
        if event.xdata is None:
            return
        
        # Check if toolbar has a mode active (zoom/pan) - if so, don't drag markers
        if self.toolbar.mode:
            return
        
        # Get x-axis range for threshold calculation
        xlim = event.inaxes.get_xlim()
        threshold = (xlim[1] - xlim[0]) * 0.02  # 2% of visible range
        
        if self.selection_start is not None and abs(event.xdata - self.selection_start) < threshold:
            self.dragging = 'start'
            self.root.config(cursor="sb_h_double_arrow")
        elif self.selection_end is not None and abs(event.xdata - self.selection_end) < threshold:
            self.dragging = 'end'
            self.root.config(cursor="sb_h_double_arrow")
    
    def _on_mouse_release(self, event):
        """Handle mouse release - finalize marker position."""
        if self.dragging:
            self.dragging = None
            self.root.config(cursor="")
            # Redraw to update selection patch position
            self._update_plot()
            self._calculate_stats()
    
    def _on_mouse_move(self, event):
        """Handle mouse move - drag marker if active."""
        if not self.dragging or event.xdata is None:
            return
        
        # Update marker position
        if self.dragging == 'start':
            # Don't let start go past end
            new_pos = min(event.xdata, self.selection_end - 0.001) if self.selection_end else event.xdata
            self.selection_start = new_pos
            self.start_var.set(f"{new_pos:.3f}")
            # Update all start lines
            for line in self.start_lines:
                line.set_xdata([new_pos, new_pos])
        elif self.dragging == 'end':
            # Don't let end go past start
            new_pos = max(event.xdata, self.selection_start + 0.001) if self.selection_start else event.xdata
            self.selection_end = new_pos
            self.end_var.set(f"{new_pos:.3f}")
            # Update all end lines
            for line in self.end_lines:
                line.set_xdata([new_pos, new_pos])
        
        # Just redraw the lines, selection patch will update on release
        self.canvas.draw_idle()
    
    def _apply_manual_selection(self):
        """Apply manually entered selection values."""
        try:
            self.selection_start = float(self.start_var.get())
            self.selection_end = float(self.end_var.get())
            self._update_plot()
            self._calculate_stats()
        except ValueError:
            messagebox.showerror("Error", "Invalid start/end values")
    
    def _select_all(self):
        """Select all available data."""
        if self.power_df is not None:
            self.selection_start = self.power_df['time'].min()
            self.selection_end = self.power_df['time'].max()
        elif self.force_df is not None:
            # Force-only test - use force time range
            force_time = self.force_df['time'] + self.time_offset
            self.selection_start = force_time.min()
            self.selection_end = force_time.max()
        else:
            return  # No data loaded
        
        self.start_var.set(f"{self.selection_start:.3f}")
        self.end_var.set(f"{self.selection_end:.3f}")
        self._update_plot()
        self._calculate_stats()
    
    def _apply_offset(self, event=None):
        """Apply the time offset to force data."""
        try:
            self.time_offset = float(self.offset_var.get())
            self._update_plot()
            self._calculate_stats()
        except ValueError:
            messagebox.showerror("Error", "Invalid offset value")
    
    def _adjust_offset(self, delta):
        """Adjust the time offset by delta seconds."""
        try:
            current = float(self.offset_var.get())
            self.time_offset = current + delta
            self.offset_var.set(f"{self.time_offset:.2f}")
            self._update_plot()
            self._calculate_stats()
        except ValueError:
            pass
    
    def _calculate_stats(self):
        """Calculate and display statistics for selected region."""
        if self.selection_start is None or self.selection_end is None:
            return
        
        stats_lines = []
        stats_lines.append("=" * 35)
        stats_lines.append(f"SELECTED REGION: {self.selection_start:.3f}s - {self.selection_end:.3f}s")
        stats_lines.append(f"Duration: {(self.selection_end - self.selection_start)*1000:.1f} ms")
        stats_lines.append("=" * 35)
        
        # Power statistics
        if self.power_df is not None:
            mask = (self.power_df['time'] >= self.selection_start) & \
                   (self.power_df['time'] <= self.selection_end)
            df = self.power_df[mask]
            
            if len(df) > 1:
                # Energy calculation
                energy_j = integrate_trapezoid(df['power'].values, df['time'].values)
                energy_mj = energy_j * 1000
                
                stats_lines.append("\n--- POWER ---")
                stats_lines.append(f"  Mean:   {df['power'].mean()*1000:.2f} mW")
                stats_lines.append(f"  Max:    {df['power'].max()*1000:.2f} mW")
                stats_lines.append(f"  Min:    {df['power'].min()*1000:.2f} mW")
                stats_lines.append(f"  Std:    {df['power'].std()*1000:.2f} mW")
                
                if 'current' in df.columns:
                    stats_lines.append("\n--- CURRENT ---")
                    stats_lines.append(f"  Mean:   {df['current'].mean()*1000:.2f} mA")
                    stats_lines.append(f"  Max:    {df['current'].max()*1000:.2f} mA")
                    stats_lines.append(f"  Min:    {df['current'].min()*1000:.2f} mA")
                
                if 'voltage' in df.columns:
                    stats_lines.append("\n--- VOLTAGE ---")
                    stats_lines.append(f"  Mean:   {df['voltage'].mean():.3f} V")
                    stats_lines.append(f"  Max:    {df['voltage'].max():.3f} V")
                    stats_lines.append(f"  Min:    {df['voltage'].min():.3f} V")
                
                stats_lines.append("\n--- ENERGY ---")
                stats_lines.append(f"  Total:  {energy_mj:.2f} mJ")
                stats_lines.append(f"          {energy_j:.5f} J")
        
        # Force statistics
        if self.force_df is not None:
            adjusted_time = self.force_df['time'] + self.time_offset
            mask = (adjusted_time >= self.selection_start) & \
                   (adjusted_time <= self.selection_end)
            df = self.force_df[mask]
            
            if len(df) > 0:
                stats_lines.append("\n--- FORCE ---")
                stats_lines.append(f"  Mean:   {df['force'].mean():.3f} N")
                stats_lines.append(f"  Max:    {df['force'].max():.3f} N")
                stats_lines.append(f"  Min:    {df['force'].min():.3f} N")
                stats_lines.append(f"  Std:    {df['force'].std():.3f} N")
                stats_lines.append(f"  Samples: {len(df)}")
        
        stats_lines.append("\n" + "=" * 35)
        
        # Update text widget
        self.stats_text.delete("1.0", tk.END)
        self.stats_text.insert("1.0", "\n".join(stats_lines))
        
        # Store current stats for export
        self.current_stats = "\n".join(stats_lines)
    
    def _export_stats(self):
        """Export current statistics to a file."""
        if not hasattr(self, 'current_stats'):
            messagebox.showinfo("Info", "No statistics to export. Select a region first.")
            return
        
        filepath = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")]
        )
        
        if filepath:
            with open(filepath, 'w') as f:
                f.write(f"Energy Analysis Export\n")
                f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
                f.write(f"Power file: {self.power_label.cget('text')}\n")
                f.write(f"Force file: {self.force_label.cget('text')}\n")
                f.write(f"Time offset: {self.time_offset:.3f}s\n\n")
                f.write(self.current_stats)
            
            messagebox.showinfo("Success", f"Statistics exported to:\n{filepath}")
    
    # =========================================================================
    # BATCH PROCESSING METHODS
    # =========================================================================
    
    def _load_motor_folder(self):
        """Load all test files from a motor folder for batch processing."""
        folder = filedialog.askdirectory(title="Select Motor Data Folder")
        if not folder:
            return
        
        # Discover all test files
        self.test_queue = discover_test_files(folder)
        
        if not self.test_queue:
            messagebox.showwarning("Warning", "No test files found in folder.")
            return
        
        # Extract motor ID from first test
        self.motor_id = self.test_queue[0]['test_info']['motor']
        
        # Set output directory
        script_dir = os.path.dirname(os.path.abspath(__file__))
        motor_comparison_dir = os.path.join(os.path.dirname(script_dir), "Motor_comparison", self.motor_id)
        os.makedirs(motor_comparison_dir, exist_ok=True)
        self.output_dir = motor_comparison_dir
        
        # Clear previous results
        self.results = []
        self.current_test_idx = -1
        
        # Update queue listbox
        self._update_queue_display()
        
        # Update progress label
        complete = sum(1 for t in self.test_queue if t['status'] == 'complete')
        jls_only = sum(1 for t in self.test_queue if t['status'] == 'jls_only')
        bd_only = sum(1 for t in self.test_queue if t['status'] == 'backdrive_only')
        
        self.progress_label.config(
            text=f"{self.motor_id}: {len(self.test_queue)} tests\n"
                 f"({complete} complete, {jls_only} JLS-only, {bd_only} backdrive)"
        )
        
        # Load first test
        if self.test_queue:
            self._load_test(0)
    
    def _load_previous_results(self):
        """Load results from a previously exported CSV, optionally with motor data folder."""
        # Ask user which approach they want
        choice = messagebox.askyesnocancel(
            "Load Previous Results",
            "Do you want to load test data files for editing?\n\n"
            "• YES — Select data folder first, then CSV (for editing tests)\n"
            "• NO — Select CSV only (for export/plots, no editing)\n"
            "• Cancel — Abort"
        )
        
        if choice is None:  # Cancel
            return
        
        if choice:  # YES - data folder first
            self._load_with_data_folder()
        else:  # NO - CSV only
            self._load_csv_only()
    
    def _load_csv_to_results(self, csv_path: str) -> bool:
        """Load a CSV file and populate self.results. Returns True on success."""
        try:
            df = pd.read_csv(csv_path)
            
            # Convert DataFrame rows to result dicts
            self.results = []
            for _, row in df.iterrows():
                result = {
                    'test_id': row['test_id'],
                    'motor': row['motor'],
                    'profile': row['profile'],
                    'speed_code': row['speed_code'],
                    'speed_value': int(row['speed_value']),
                    'voltage': int(row['voltage']),
                    'direction': row['direction'],
                    'selection_start': float(row['selection_start']) if pd.notna(row['selection_start']) else None,
                    'selection_end': float(row['selection_end']) if pd.notna(row['selection_end']) else None,
                    'force_time_offset': float(row['force_time_offset']) if 'force_time_offset' in row and pd.notna(row['force_time_offset']) else 0.0,
                    'duration_ms': float(row['duration_ms']),
                    'mean_power_mw': float(row['mean_power_mw']),
                    'peak_power_mw': float(row['peak_power_mw']),
                    'mean_current_ma': float(row['mean_current_ma']),
                    'peak_current_ma': float(row['peak_current_ma']),
                    'energy_mj': float(row['energy_mj']),
                    'max_thrust_n': float(row['max_thrust_n']) if pd.notna(row['max_thrust_n']) else None,
                    'mean_thrust_n': float(row['mean_thrust_n']) if pd.notna(row['mean_thrust_n']) else None,
                }
                self.results.append(result)
            
            # Extract motor ID
            self.motor_id = self.results[0]['motor'] if self.results else "Unknown"
            
            # Set output directory
            script_dir = os.path.dirname(os.path.abspath(__file__))
            self.output_dir = os.path.join(os.path.dirname(script_dir), "Motor_comparison", self.motor_id or "Unknown")
            os.makedirs(self.output_dir, exist_ok=True)
            
            return True
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load CSV:\n{e}")
            return False
    
    def _load_with_data_folder(self):
        """Load previous results with data folder for editing capability."""
        # First, select the motor data folder
        folder = filedialog.askdirectory(title="Select Motor Data Folder (with JLS/CSV files)")
        if not folder:
            return
        
        # Discover test files
        self.test_queue = discover_test_files(folder)
        
        if not self.test_queue:
            messagebox.showwarning("Warning", "No test files found in folder.")
            return
        
        # Now select the previous results CSV
        csv_path = filedialog.askopenfilename(
            title="Select Previous Results CSV",
            filetypes=[("CSV files", "*.csv")],
            initialdir=folder
        )
        
        if not csv_path:
            messagebox.showinfo("Info", "No CSV selected. Loading folder only.")
            # Fall back to normal folder load behavior
            self.motor_id = self.test_queue[0]['test_info']['motor']
            script_dir = os.path.dirname(os.path.abspath(__file__))
            self.output_dir = os.path.join(os.path.dirname(script_dir), "Motor_comparison", self.motor_id)
            os.makedirs(self.output_dir, exist_ok=True)
            self.results = []
            self._update_queue_display()
            if self.test_queue:
                self._load_test(0)
            return
        
        # Load CSV
        if not self._load_csv_to_results(csv_path):
            return
        
        # Update UI
        self._update_queue_display()
        
        complete = sum(1 for t in self.test_queue if t['status'] == 'complete')
        jls_only = sum(1 for t in self.test_queue if t['status'] == 'jls_only')
        bd_only = sum(1 for t in self.test_queue if t['status'] == 'backdrive_only')
        
        self.progress_label.config(
            text=f"{self.motor_id}: {len(self.test_queue)} tests\n"
                 f"({complete} complete, {jls_only} JLS-only, {bd_only} backdrive)\n"
                 f"✅ Loaded {len(self.results)} previous results"
        )
        
        messagebox.showinfo("Success", 
            f"Loaded {len(self.results)} results from:\n{os.path.basename(csv_path)}\n\n"
            f"You can now:\n"
            f"• Click 'Export Results' to regenerate with fixed logic\n"
            f"• Click 'Generate Plots' to create plots\n"
            f"• Click on any test to review/edit its selection")
        
        # Load first test
        if self.test_queue:
            self._load_test(0)
    
    def _load_csv_only(self):
        """Load previous results CSV only (for export/plots without editing)."""
        csv_path = filedialog.askopenfilename(
            title="Select Previous Results CSV",
            filetypes=[("CSV files", "*.csv")]
        )
        
        if not csv_path:
            return
        
        # Load CSV
        if not self._load_csv_to_results(csv_path):
            return
        
        # Create dummy test queue from results (for plot generation)
        self.test_queue = []
        for result in self.results:
            self.test_queue.append({
                'jls_file': None,  # No data files available
                'csv_file': None,
                'test_info': {
                    'test_id': result['test_id'],
                    'motor': result['motor'],
                    'profile': result['profile'],
                    'speed_code': result['speed_code'],
                    'speed_value': result['speed_value'],
                    'voltage': result['voltage'],
                    'direction': result['direction'],
                },
                'status': 'complete'
            })
        
        # Update UI
        self._update_queue_display()
        
        self.progress_label.config(
            text=f"{self.motor_id}: {len(self.results)} tests from CSV\n"
                 f"⚠️ No data files — Export/Plots only"
        )
        
        messagebox.showinfo("Success", 
            f"Loaded {len(self.results)} results from:\n{os.path.basename(csv_path)}\n\n"
            f"⚠️ Note: No data files loaded.\n"
            f"You can:\n"
            f"• Click 'Export Results' to regenerate markdown\n"
            f"• ⚠️ Plots require data folder — use 'Load Data Folder' first")
        
        # Clear display (can't show test without data)
        self.current_test_idx = -1
        for ax in self.axes:
            ax.clear()
    
    def _update_queue_display(self):
        """Update the test queue listbox."""
        self.queue_listbox.delete(0, tk.END)
        
        for i, test in enumerate(self.test_queue):
            info = test['test_info']
            status_icon = "✓" if test['status'] == 'complete' else ("⚡" if test['status'] == 'jls_only' else "📏")
            
            # Check if already processed
            is_processed = any(r['test_id'] == info['test_id'] for r in self.results)
            processed_mark = " ✅" if is_processed else ""
            
            label = f"{status_icon} {info['test_id']}{processed_mark}"
            self.queue_listbox.insert(tk.END, label)
        
        # Highlight current test
        if self.current_test_idx >= 0:
            self.queue_listbox.selection_clear(0, tk.END)
            self.queue_listbox.selection_set(self.current_test_idx)
            self.queue_listbox.see(self.current_test_idx)
    
    def _on_queue_select(self, event):
        """Handle selection of a test from the queue."""
        selection = self.queue_listbox.curselection()
        if selection:
            idx = selection[0]
            self._load_test(idx)
    
    def _load_test(self, idx: int):
        """Load a specific test by index."""
        if idx < 0 or idx >= len(self.test_queue):
            return
        
        self.current_test_idx = idx
        test = self.test_queue[idx]
        info = test['test_info']
        
        # Check if we have saved results for this test
        saved_result = next(
            (r for r in self.results if r['test_id'] == info['test_id']),
            None
        )
        
        # Clear current data
        self.power_df = None
        self.force_df = None
        
        # Restore saved alignment or reset to default
        if saved_result:
            self.time_offset = saved_result.get('force_time_offset', 0.0)
            self.selection_start = saved_result.get('selection_start')
            self.selection_end = saved_result.get('selection_end')
        else:
            self.time_offset = 0.0
            self.selection_start = None
            self.selection_end = None
        
        self.offset_var.set(f"{self.time_offset:.2f}")
        
        # Load JLS file if available
        if test['jls_file']:
            self._load_power_file(test['jls_file'])
        
        # Load CSV file if available
        if test['csv_file']:
            self._load_force_file(test['csv_file'])
        
        # Restore selection bounds in UI (after files loaded, before plot update)
        if saved_result and saved_result.get('selection_start') is not None:
            self.selection_start = saved_result['selection_start']
            self.selection_end = saved_result['selection_end']
            self.start_var.set(f"{self.selection_start:.3f}")
            self.end_var.set(f"{self.selection_end:.3f}")
            self._update_plot()
            self._calculate_stats()
        
        # Update window title with current test
        self.root.title(f"Energy Analyzer — {info['test_id']} [{idx+1}/{len(self.test_queue)}]")
        
        # Update queue display
        self._update_queue_display()
    
    def _prev_test(self):
        """Go to the previous test in queue."""
        if self.current_test_idx > 0:
            self._load_test(self.current_test_idx - 1)
    
    def _skip_test(self):
        """Skip current test and go to next."""
        if self.current_test_idx < len(self.test_queue) - 1:
            self._load_test(self.current_test_idx + 1)
        else:
            messagebox.showinfo("Info", "Reached end of test queue.")
    
    def _save_and_next(self):
        """Save current test results and go to next test."""
        if self.current_test_idx < 0 or self.current_test_idx >= len(self.test_queue):
            messagebox.showwarning("Warning", "No test selected.")
            return
        
        # Extract current metrics
        result = self._extract_current_metrics()
        
        if result:
            # Check if already saved - update if so
            existing_idx = next(
                (i for i, r in enumerate(self.results) if r['test_id'] == result['test_id']),
                None
            )
            if existing_idx is not None:
                self.results[existing_idx] = result
            else:
                self.results.append(result)
            
            print(f"Saved: {result['test_id']} — Energy: {result['energy_mj']:.1f} mJ, "
                  f"Max Thrust: {result['max_thrust_n']:.2f} N" if result['max_thrust_n'] else "")
        
        # Update display and go to next
        self._update_queue_display()
        
        if self.current_test_idx < len(self.test_queue) - 1:
            self._load_test(self.current_test_idx + 1)
        else:
            messagebox.showinfo("Complete", 
                f"All tests processed!\n\n"
                f"Saved {len(self.results)} results.\n"
                f"Click 'Export Results' to generate output files.")
    
    def _extract_current_metrics(self) -> Optional[Dict]:
        """Extract metrics from current selection."""
        if self.current_test_idx < 0:
            return None
        
        test = self.test_queue[self.current_test_idx]
        info = test['test_info']
        
        result = {
            'test_id': info['test_id'],
            'motor': info['motor'],
            'profile': info['profile'],
            'speed_code': info['speed_code'],
            'speed_value': info['speed_value'],
            'voltage': info['voltage'],
            'direction': info['direction'],
            'selection_start': self.selection_start,
            'selection_end': self.selection_end,
            'force_time_offset': self.time_offset,  # NEW: Save force alignment offset
            'duration_ms': 0,
            'mean_power_mw': 0,
            'peak_power_mw': 0,
            'mean_current_ma': 0,
            'peak_current_ma': 0,
            'energy_mj': 0,
            'max_thrust_n': None,
            'mean_thrust_n': None,
        }
        
        # Extract power metrics
        if self.power_df is not None and self.selection_start is not None and self.selection_end is not None:
            mask = (self.power_df['time'] >= self.selection_start) & \
                   (self.power_df['time'] <= self.selection_end)
            df = self.power_df[mask]
            
            if len(df) > 1:
                result['duration_ms'] = (self.selection_end - self.selection_start) * 1000
                result['mean_power_mw'] = df['power'].mean() * 1000
                result['peak_power_mw'] = df['power'].max() * 1000
                result['energy_mj'] = integrate_trapezoid(df['power'].values, df['time'].values) * 1000
                
                if 'current' in df.columns:
                    result['mean_current_ma'] = df['current'].mean() * 1000
                    result['peak_current_ma'] = df['current'].max() * 1000
        
        # Extract force metrics
        if self.force_df is not None and self.selection_start is not None and self.selection_end is not None:
            adjusted_time = self.force_df['time'] + self.time_offset
            mask = (adjusted_time >= self.selection_start) & \
                   (adjusted_time <= self.selection_end)
            df = self.force_df[mask]
            
            if len(df) > 0:
                result['max_thrust_n'] = df['force'].max()
                result['mean_thrust_n'] = df['force'].mean()
        
        return result
    
    def _export_results(self):
        """Export all accumulated results to CSV and Markdown files."""
        if not self.results:
            messagebox.showwarning("Warning", "No results to export. Process some tests first.")
            return
        
        if not self.output_dir:
            self.output_dir = filedialog.askdirectory(title="Select Output Directory")
            if not self.output_dir:
                return
        
        # Export to CSV
        csv_path = os.path.join(self.output_dir, f"{self.motor_id}_detailed_metrics.csv")
        df = pd.DataFrame(self.results)
        df.to_csv(csv_path, index=False)
        print(f"Saved CSV: {csv_path}")
        
        # Export to Markdown
        md_path = os.path.join(self.output_dir, f"{self.motor_id}_summary_table.md")
        self._export_markdown(md_path)
        print(f"Saved Markdown: {md_path}")
        
        messagebox.showinfo("Export Complete", 
            f"Results exported to:\n\n"
            f"📊 {csv_path}\n"
            f"📝 {md_path}\n\n"
            f"Click 'Generate Plots' to create comparison plots.")
    
    def _export_markdown(self, filepath: str):
        """Export results as Markdown tables."""
        # Separate backdrive and active tests
        backdrive_tests = [r for r in self.results if r.get('profile') == 'BD']
        active_tests = [r for r in self.results if r.get('profile') != 'BD']
        
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(f"# {self.motor_id} — Energy Measurement Results\n\n")
            f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
            
            # Active test matrix (excludes backdrive)
            f.write("## Complete Test Matrix\n\n")
            f.write("| Test ID | Speed | Voltage | Dir | Mean Power (mW) | Peak Power (mW) | Peak I (mA) | Energy (mJ) | Duration (ms) | Max Thrust (N) |\n")
            f.write("|---------|-------|---------|-----|-----------------|-----------------|-------------|-------------|---------------|----------------|\n")
            
            for r in active_tests:
                thrust_str = f"{r['max_thrust_n']:.2f}" if r['max_thrust_n'] is not None else "N/A"
                f.write(f"| {r['test_id']} | {r['speed_value']} | {r['voltage']}V | {r['direction']} | "
                       f"{r['mean_power_mw']:.0f} | {r['peak_power_mw']:.0f} | {r['peak_current_ma']:.0f} | "
                       f"{r['energy_mj']:.0f} | {r['duration_ms']:.0f} | {thrust_str} |\n")
            
            # Backdrive results (separate section)
            if backdrive_tests:
                f.write("\n## Backdrive Force Results\n\n")
                f.write("| Test ID | Direction | Max Force (N) | Mean Force (N) |\n")
                f.write("|---------|-----------|---------------|----------------|\n")
                for r in backdrive_tests:
                    max_f = f"{r['max_thrust_n']:.2f}" if r['max_thrust_n'] is not None else "N/A"
                    mean_f = f"{r['mean_thrust_n']:.2f}" if r['mean_thrust_n'] is not None else "N/A"
                    f.write(f"| {r['test_id']} | {r['direction']} | {max_f} | {mean_f} |\n")
            
            # Best configuration summary
            f.write("\n## Best Configuration Summary\n\n")
            f.write("| Goal | Test ID | Speed | Voltage | Mean Power (mW) | Energy (mJ) | Max Thrust (N) | Efficiency (mJ/N) |\n")
            f.write("|------|---------|-------|---------|-----------------|-------------|----------------|-------------------|\n")
            
            best_configs = self._find_best_configurations()
            for goal, config in best_configs.items():
                if config:
                    eff = config['energy_mj'] / config['max_thrust_n'] if config['max_thrust_n'] else 0
                    thrust_str = f"{config['max_thrust_n']:.2f}" if config['max_thrust_n'] else "N/A"
                    eff_str = f"{eff:.1f}" if config['max_thrust_n'] else "N/A"
                    f.write(f"| **{goal}** | {config['test_id']} | {config['speed_value']} | "
                           f"{config['voltage']}V | {config['mean_power_mw']:.0f} | "
                           f"{config['energy_mj']:.0f} | {thrust_str} | {eff_str} |\n")
    
    def _find_best_configurations(self):
        """Find best configurations for different optimization goals.
        
        Returns dict mapping goal name to result dict (or None).
        Excludes backdrive tests from comparison.
        """
        # Exclude backdrive tests (profile == 'BD') from all comparisons
        active_tests = [r for r in self.results if r.get('profile') != 'BD']
        
        # Filter to only tests with thrust data for efficiency calculations
        with_thrust = [r for r in active_tests if r['max_thrust_n'] is not None and r['max_thrust_n'] > 0]
        
        best: Dict[str, Optional[Dict]] = {
            'Min Energy': None,
            'Max Thrust': None,
            'Best Efficiency': None,
            'Balanced (5V)': None,
        }
        
        if not active_tests:
            return best
        
        # Min energy (from active tests only)
        best['Min Energy'] = min(active_tests, key=lambda x: x['energy_mj'])
        
        if with_thrust:
            # Max thrust
            best['Max Thrust'] = max(with_thrust, key=lambda x: x['max_thrust_n'])
            
            # Best efficiency (lowest mJ/N)
            best['Best Efficiency'] = min(with_thrust, 
                key=lambda x: x['energy_mj'] / x['max_thrust_n'] if x['max_thrust_n'] else float('inf'))
            
            # Balanced at 5V
            at_5v = [r for r in with_thrust if r['voltage'] == 5]
            if at_5v:
                best['Balanced (5V)'] = min(at_5v,
                    key=lambda x: x['energy_mj'] / x['max_thrust_n'] if x['max_thrust_n'] else float('inf'))
        
        return best
    
    def _generate_plots(self):
        """Generate comparison plots for best configurations."""
        if not self.results:
            messagebox.showwarning("Warning", "No results to plot. Process some tests first.")
            return
        
        best_configs = self._find_best_configurations()
        best_efficiency = best_configs.get('Best Efficiency')
        
        if not best_efficiency:
            messagebox.showwarning("Warning", "No best efficiency configuration found.")
            return
        
        # Find the test file for the best efficiency config
        best_test = None
        for test in self.test_queue:
            if test['test_info']['test_id'] == best_efficiency['test_id']:
                best_test = test
                break
        
        if not best_test or not best_test['jls_file']:
            # No data file loaded - ask user for data folder
            response = messagebox.askyesno(
                "Data Files Needed",
                "No data files loaded for the best configuration.\n\n"
                "Would you like to select the motor data folder\n"
                f"to generate the plot for {best_efficiency['test_id']}?"
            )
            if not response:
                return
            
            folder = filedialog.askdirectory(title="Select Motor Data Folder (with JLS/CSV files)")
            if not folder:
                return
            
            # Discover test files and match
            test_files = discover_test_files(folder)
            for test in test_files:
                if test['test_info']['test_id'] == best_efficiency['test_id']:
                    best_test = test
                    # Also update the queue entry
                    for i, q_test in enumerate(self.test_queue):
                        if q_test['test_info']['test_id'] == best_efficiency['test_id']:
                            self.test_queue[i] = test
                            break
                    break
            
            if not best_test or not best_test['jls_file']:
                messagebox.showwarning("Warning", f"Could not find data file for {best_efficiency['test_id']} in selected folder.")
                return
        
        # Load the data
        power_df = self._read_jls_file(best_test['jls_file'])
        force_df = None
        if best_test['csv_file']:
            force_df = self._read_force_csv(best_test['csv_file'])
        
        # Get saved offsets and selection region
        start = best_efficiency.get('selection_start')
        end = best_efficiency.get('selection_end')
        saved_offset = best_efficiency.get('force_time_offset', 0.0)
        
        # Apply saved time offset to force data
        if force_df is not None and saved_offset != 0.0:
            force_df['time'] = force_df['time'] + saved_offset
        
        # Create the plot
        fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
        
        # Power plot
        if power_df is not None:
            axes[0].plot(power_df['time'], power_df['power'] * 1000, 'b-', linewidth=0.5, label='Power')
            axes[0].set_ylabel('Power (mW)')
            axes[0].set_title(f"{self.motor_id} — Best Efficiency Configuration: {best_efficiency['test_id']}")
            axes[0].grid(True, alpha=0.3)
            axes[0].legend()
            
            # Add selection region
            if start is not None and end is not None:
                axes[0].axvspan(start, end, alpha=0.2, color='yellow', label='Analysis Region')
                axes[0].axvline(start, color='green', linestyle='--', alpha=0.7)
                axes[0].axvline(end, color='red', linestyle='--', alpha=0.7)
        
        # Force plot
        if force_df is not None:
            axes[1].plot(force_df['time'], force_df['force'], 'g-', linewidth=1, 
                        marker='.', markersize=3, label='Force')
            axes[1].set_ylabel('Force (N)')
            axes[1].set_xlabel('Time (s)')
            axes[1].set_title('Force Measurement')
            axes[1].grid(True, alpha=0.3)
            axes[1].legend()
            
            # Add selection region
            if start is not None and end is not None:
                axes[1].axvspan(start, end, alpha=0.2, color='yellow')
                axes[1].axvline(start, color='green', linestyle='--', alpha=0.7)
                axes[1].axvline(end, color='red', linestyle='--', alpha=0.7)
        else:
            axes[1].text(0.5, 0.5, 'No force data available', 
                        ha='center', va='center', transform=axes[1].transAxes)
        
        # Add metrics annotation
        metrics_text = (
            f"Energy: {best_efficiency['energy_mj']:.1f} mJ\n"
            f"Mean Power: {best_efficiency['mean_power_mw']:.1f} mW\n"
            f"Peak Power: {best_efficiency['peak_power_mw']:.1f} mW\n"
            f"Max Thrust: {best_efficiency['max_thrust_n']:.2f} N\n"
            f"Efficiency: {best_efficiency['energy_mj']/best_efficiency['max_thrust_n']:.1f} mJ/N"
        ) if best_efficiency['max_thrust_n'] else (
            f"Energy: {best_efficiency['energy_mj']:.1f} mJ\n"
            f"Mean Power: {best_efficiency['mean_power_mw']:.1f} mW\n"
            f"Peak Power: {best_efficiency['peak_power_mw']:.1f} mW"
        )
        
        axes[0].text(0.02, 0.98, metrics_text, transform=axes[0].transAxes,
                    fontsize=9, verticalalignment='top', fontfamily='monospace',
                    bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8))
        
        plt.tight_layout()
        
        # Save plot
        plot_path = os.path.join(self.output_dir, f"{self.motor_id}_best_config.png")
        plt.savefig(plot_path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        
        print(f"Saved plot: {plot_path}")
        messagebox.showinfo("Plot Generated", f"Best configuration plot saved to:\n\n{plot_path}")
    
    def run(self):
        """Start the application."""
        self.root.mainloop()


def main():
    """Main entry point."""
    print("Starting Interactive Energy & Force Analyzer...")
    print("=" * 50)
    
    app = EnergyAnalyzer()
    app.run()


if __name__ == "__main__":
    main()
