"""Sensible gates (tools/live_batches/sane_gates.py, user rule 2026-11-14) on the numbers of the 2026-11-14 handout runs.

    py -3 tests/sane_gates_test.py
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.live_batches import sane_gates as sane  # noqa: E402
from tools.live_batches_staged.handout_qual import analyze as hq  # noqa: E402


class SaneGates(unittest.TestCase):
    def status(self, *a, **kw):
        return sane.compare(*a, **kw)[0]

    def test_cost(self):
        floor = sane.cost_floor(4.76)
        self.assertEqual(floor, 0.1)
        self.assertEqual(sane.cost_floor(30.0), 0.3)
        # GI trace 4.83 / 4.69 OFF, 3.49 ON: a drop the handout does not predict.
        self.assertEqual(self.status(4.83, 4.69, 3.49, floor, "not higher"), sane.EXPLAIN)
        self.assertEqual(self.status(4.83, 4.69, 3.49, floor, "not higher", predicted=True), sane.OK)
        self.assertEqual(self.status(4.83, 4.69, 6.0, floor, "not higher"), sane.FAIL)
        # Beyond the noise, under the floor: INFO in both directions, never FAIL.
        self.assertEqual(self.status(4.70, 4.71, 4.78, floor, "not higher"), sane.INFO)
        self.assertEqual(self.status(4.70, 4.71, 4.63, floor, "not higher"), sane.INFO)
        # Inside the OFF pair's noise: ok whatever the floor.
        self.assertEqual(self.status(20.0, 21.0, 21.2, 0.2, "not higher"), sane.OK)
        # The deliverable: a drop is the design, a rise beyond noise and floor fails.
        self.assertEqual(self.status(3.6, 3.7, 0.04, 0.1, "lower"), sane.OK)
        self.assertEqual(self.status(20.9, 20.8, 22.0, 0.21, "lower"), sane.FAIL)

    def test_off_range(self):
        # hq-s8-ultra hair-pan p99: OFF periods 25.0-27.1; an ON median inside them is no difference.
        off = [26.25, 27.11, 25.01, 25.28, 26.77]
        self.assertEqual(self.status(26.68, 25.28, 27.0, 0.26, "not higher", off_values=off), sane.OK)
        self.assertEqual(self.status(26.68, 25.28, 30.39, 0.26, "not higher", off_values=off), sane.FAIL)

    def test_match_and_count(self):
        self.assertEqual(self.status(97.4, 97.5, 97.0, sane.MATCH_FLOOR_POINTS, "not lower"), sane.INFO)
        self.assertEqual(self.status(97.4, 97.5, 60.0, sane.MATCH_FLOOR_POINTS, "not lower"), sane.FAIL)
        self.assertEqual(self.status(97.4, 97.5, 99.9, sane.MATCH_FLOOR_POINTS, "not lower"), sane.OK)
        self.assertEqual(self.status(840, 840, 700, sane.count_floor(840), "equal"), sane.EXPLAIN)
        self.assertEqual(self.status(840, 840, 845, sane.count_floor(840), "equal"), sane.INFO)

    def test_energy(self):
        off = [647.47, 649.11, 647.96, 648.97]
        self.assertEqual(sane.energy(off, 648.5)[0], sane.OK)
        status, rel = sane.energy(off, 647.24)   # hq-s8-energy 191419: -0.20%, real, under the visibility floor
        self.assertEqual(status, sane.EXPLAIN)
        self.assertAlmostEqual(rel, -0.0018, places=3)
        self.assertEqual(sane.energy(off, 647.24, predicted=True)[0], sane.INFO)
        self.assertEqual(sane.energy(off, 640.0)[0], sane.FAIL)   # -1.3%
        self.assertEqual(sane.energy(off, 655.0)[0], sane.FAIL)   # +1.0%

    def test_windows(self):
        periods = {"static-on": {"RT": [1.0, 1.1]}, "pan-on-q": {"RT": []}}
        judged, notes = sane.split_windows(["static-on", "pan-on-q", "gone"], periods, lambda label: ("RT",))
        self.assertEqual(judged, ["static-on"])
        self.assertEqual(len(notes), 2)
        self.assertIn("pan-on-q: window not judged", notes[0])

    def test_handout_judge(self):
        plan = [{"label": f"pan-{s}{side}", "frames": 420} for s in ("off-a", "on", "off-b") for side in ("-q", "-e")]
        m = {p["label"]: {"scene wait mean": [3.7], "frame time median": [20.8], "GI": [9.1], "handout fallbacks": [0]}
             for p in plan}
        m["pan-on-q"] = {}   # a pan side without a complete period: the group is judged on the other side
        m["pan-on-e"].update({"scene wait mean": [0.05], "frame time median": [19.9], "GI": [8.0]})
        rows, found, decisive, notes = hq.judge(m, plan)
        self.assertTrue(decisive)
        self.assertEqual(found[sane.FAIL], [])
        self.assertEqual(len(found[sane.EXPLAIN]), 1)   # GI 9.1 -> 8.0
        self.assertIn("GI", found[sane.EXPLAIN][0])
        m["pan-on-e"]["handout fallbacks"] = [1]
        self.assertEqual(len(hq.judge(m, plan)[1][sane.FAIL]), 1)   # one event is a fault
        self.assertEqual(hq.verdict_of(found, False), "EXPLAIN (1 unexplained, no failure)")
        self.assertEqual(hq.verdict_of(found, True), "INVALID")

    def test_state_costs(self):
        medians = {"GI trace GPU median ms": {"energy-off-a": 4.83, "energy-on": 3.49, "energy-off-b": 4.69}}
        self.assertEqual(len(hq.state_costs(medians)[sane.EXPLAIN]), 1)
        found = hq.state_costs({"GI trace GPU median ms": {"energy-off-a": 4.83, "energy-m2": 3.49, "energy-off-b": 4.69}},
                               {"m2": "lower"})
        self.assertEqual((len(found[sane.EXPLAIN]), len(found[sane.INFO])), (0, 1))

    def test_hit_checks(self):
        plan = [{"label": f"{kind}-{state}", "set": f"rt_scene_handout_hit_wait={int(state == 'fix')}"}
                for state in ("off-a", "m2", "fix", "off-b") for kind in ("energy", "hit")]
        q, c = "GI far: ambient kept at qualified hits", "hit material passes on the committed scene"
        good = {q: {"hit-off-a": 0.0077, "hit-m2": 0.0, "hit-fix": 0.0076, "hit-off-b": 0.0078},
                c: {"hit-fix": 240.0, "energy-fix": 480.0, "hit-m2": 0.0},
                "scene wait timeouts (max)": {"hit-m2": 0.0}}
        found, notes = hq.hit_checks(good, plan, {"m2": "lower"})
        self.assertEqual((found[sane.FAIL], notes), ([], []))
        self.assertEqual(len(found[sane.INFO]), 1)   # m2: predicted
        bad = {q: {"hit-off-a": 0.0077, "hit-m2": 0.0, "hit-fix": 0.0}, c: {"hit-fix": 0.0},
               "handout fallbacks (max)": {"hit-fix": 1.0}}
        found, notes = hq.hit_checks(bad, plan)
        self.assertEqual(len(found[sane.FAIL]), 4)   # a fallback, m2 and fix without qualified hits, the fix not running
        self.assertEqual(len(notes), 1)              # hit-off-b has no line: not judged


if __name__ == "__main__":
    unittest.main()
