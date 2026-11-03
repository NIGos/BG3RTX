"""CTest entry point for the asset catalogue's unittest module."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

if __name__ == "__main__":
    unittest.main(module="tools.assets.tests.test_mesh_materials")
