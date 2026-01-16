# Utils package
"""
Utility modules for Energy Measurement Test System

Modules:
  - hdf5_manager: HDF5 data storage and retrieval
  - plot_generator: Matplotlib plot generation
"""

from .hdf5_manager import (
    HDF5Manager,
    TestMetadata,
    AlignmentData,
    ComputedMetrics,
    ForceRawData,
    PowerRawData,
    TestRecord
)

from .plot_generator import (
    PlotGenerator,
    compute_metrics_from_aligned_data,
    COLORS
)

__all__ = [
    'HDF5Manager',
    'TestMetadata',
    'AlignmentData', 
    'ComputedMetrics',
    'ForceRawData',
    'PowerRawData',
    'TestRecord',
    'PlotGenerator',
    'compute_metrics_from_aligned_data',
    'COLORS'
]
