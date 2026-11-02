"""Motion quality gate must reject extra drops and incomplete comparisons."""
import sys
import json
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches.vram_guard.gate import compare_turns


class MotionGateTest(unittest.TestCase):
    def test_job_disables_inherited_budget_simulation(self):
        root = Path(__file__).resolve().parents[1]
        job = json.loads((root / "tools/live_batches/vram_guard/jobs/vg3-s8-low-long-view-fg2.json").read_text())
        settings = dict(item.split("=", 1) for item in job["set"].split(","))
        self.assertEqual(settings.get("rt_vram_guard_budget_mb"), "0")

    def data(self):
        return {f"turn-{s}-{d}": [(0, 1), (0, 2), (0, 3)]
                for s in ("off-a", "on", "off-b") for d in ("r", "l")}

    def test_equal_motion(self):
        self.assertEqual(compare_turns(self.data())[0], [])

    def test_extra_drop_even_one_window(self):
        values = self.data()
        values["turn-on-l"][2] = (0, 3.03)
        self.assertTrue(compare_turns(values)[0])

    def test_off_off_tolerance(self):
        values = self.data()
        values["turn-off-b-r"][2] = (0, 3.1)
        values["turn-on-r"][2] = (0, 3.2)
        self.assertEqual(compare_turns(values)[0], [])

    def test_missing_summary_fails(self):
        values = self.data()
        values["turn-on-r"].pop()
        self.assertTrue(compare_turns(values)[0])

    def test_alpha_quality_also_fails(self):
        values = self.data()
        values["turn-on-r"][0] = (.03, 1)
        self.assertTrue(compare_turns(values)[0])

    def test_more_drops_below_unchanged_peak_fail_mean(self):
        values = self.data()
        values["turn-on-r"][0] = (0, 1.2)
        self.assertTrue(compare_turns(values)[0])

    def test_summary_order_does_not_claim_angle_alignment(self):
        values = self.data()
        values["turn-on-r"].reverse()
        self.assertEqual(compare_turns(values)[0], [])


if __name__ == "__main__":
    unittest.main()
