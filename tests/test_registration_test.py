"""Keep portable QA and asset suites reachable through build.ps1 -Test."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TestRegistration(unittest.TestCase):
    def test_qa_and_asset_suites_registered(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        names = re.search(r"foreach\(py_test (.*?)\)", cmake, re.S).group(1).split()
        required = {"assets_discovery", "qa_evidence", "qa_detectors", "fg_log_audit", "fg_motion_gate",
                    "fg_static_effects", "m1_coverage_log", "pan_artifacts", "pan_fg_evidence", "rebase_gate",
                    "rotation_preflight", "build_config", "test_registration"}
        self.assertFalse(required - set(names), f"unregistered suites: {required - set(names)}")
        wrapper = (ROOT / "tests/assets_discovery_test.py").read_text()
        self.assertIn("defaultTestLoader.discover", wrapper)
        self.assertIn("tools/assets/tests", wrapper)
        self.assertIn("sys.exit(not result.wasSuccessful())", wrapper)


if __name__ == "__main__":
    unittest.main()
