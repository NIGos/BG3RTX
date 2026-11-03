"""Dialogue pairs must reject camera steps at the actual captured images."""
import importlib.util
import json
from pathlib import Path
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "dialogue_registration", ROOT / "tools/live_batches/dialogue_shadows/pair_registration.py")
REG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REG)


class DialogueRegistrationTest(unittest.TestCase):
    def test_real_camera_step_sizes_rejected(self):
        image = np.random.default_rng(42).random((100, 140, 3))
        self.assertTrue(REG.registration(image, image)["valid"])
        for step in (5, 7, -5):
            self.assertFalse(REG.registration(image, np.roll(image, step, axis=1))["valid"])

    def test_missing_albedo_rejected(self):
        with self.assertRaises(ValueError):
            REG.registration(None, np.zeros((100, 140, 3)))
        with self.assertRaises(ValueError):
            REG.registration(np.zeros((100, 140, 3)), np.zeros((100, 140, 3)))

    def test_plan_pairs_cover_both_neighbours_and_motion_controls(self):
        plan = json.loads((ROOT / "tools/live_batches/dialogue_shadows/plans/ds_dialogue.json").read_text())
        pairs = REG.comparisons(plan)
        for i, phase in enumerate(plan):
            label = phase["label"]
            if label.startswith("shot-") and not label.startswith("shot-off-"):
                before = next(p["label"] for p in reversed(plan[:i]) if p["label"].startswith("shot-off-"))
                after = next(p["label"] for p in plan[i+1:] if p["label"].startswith("shot-off-"))
                for neighbour in (before, after):
                    self.assertIn((neighbour, 0, label, 0), pairs)
        for control in ("motion-base-fix-off", "motion-nosun"):
            for index in (0, 1):
                self.assertIn((control, index, "motion-base", index), pairs)
        # The bundle adds box-old and GI-off attribution shots; both need OFF neighbours.
        self.assertIn(("shot-off-c", 0, "shot-boxold-on", 0), pairs)
        self.assertIn(("shot-off-d", 0, "shot-nogi-on", 0), pairs)
        self.assertEqual(len(pairs), 35)


if __name__ == "__main__":
    unittest.main()
