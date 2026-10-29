"""Compatibility entry point: run the complete production core tests."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("run_core_tests.py")),run_name="__main__")
