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
        # Beyond the noise, within noise plus floor: INFO in both directions, never FAIL.
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
        # the deliverable rows: a wait that falls is shown (INFO), a frame time that rises beyond noise and floor fails
        found = hq.state_costs({"scene wait mean ms": {"energy-off-a": 3.6, "energy-fix": 1.0, "energy-off-b": 3.5},
                                "frame time median ms": {"energy-off-a": 21.4, "energy-fix": 22.4, "energy-off-b": 21.5}})
        self.assertEqual((len(found[sane.FAIL]), len(found[sane.EXPLAIN]), len(found[sane.INFO])), (1, 0, 1))
        # hq-s8-modes 205520: mode 1 +0.230 ms against a pair difference of 0.200 and a floor of 0.214 is within
        # noise plus floor (INFO, with or without the single OFF periods); a rise of 1 ms at 21 ms still fails
        m = {"frame time median ms": {"energy-off-a": 21.285, "energy-m1": 21.615, "energy-off-b": 21.485}}
        per = {"frame time median ms": {"energy-off-a": [21.07, 21.3, 21.27, 21.4], "energy-off-b": [21.45, 21.52, 21.7, 21.3]}}
        self.assertEqual(len(hq.state_costs(m, None, per)[sane.FAIL]), 0)
        self.assertEqual(len(hq.state_costs(m)[sane.FAIL]), 0)
        m["frame time median ms"]["energy-m1"] = 22.4
        self.assertEqual(len(hq.state_costs(m, None, per)[sane.FAIL]), 1)
        self.assertEqual(len(hq.state_costs(m)[sane.FAIL]), 1)

    def test_hit_checks(self):
        plan = [{"label": f"{kind}-{state}", "set": f"rt_scene_handout={2 * (state in ('m2', 'fix'))},rt_scene_handout_hit_wait={int(state == 'fix')}"}
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
        # the hit work: light rays at hits equal to the OFF pair; a state that loses them fails unless the job predicts it
        r = "GI hit direct: light rays traced"
        rays = {r: {"hit-off-a": 3.84e6, "hit-m2": 1.36e5, "hit-fix": 3.83e6, "hit-off-b": 3.83e6}}
        self.assertEqual(len(hq.hit_checks(rays, plan, {"m2": "lower"})[0][sane.FAIL]), 0)
        self.assertEqual(len(hq.hit_checks(rays, plan)[0][sane.FAIL]), 1)   # m2
        del rays[r]["hit-fix"]
        self.assertEqual(len(hq.hit_checks(rays, plan, {"m2": "lower"})[0][sane.FAIL]), 1)   # no line: no rays at all
        # hq-s8-modes 205520, real numbers: mode 2 traces 135707 light rays against 3.84e6 (the dropped hit work)
        rays = {r: {"hit-off-a": 3.83684e6, "hit-m1": 3.82743e6, "hit-m2": 135707, "hit-fix": 3.82597e6, "hit-off-b": 3.83808e6}}
        found = hq.hit_checks(rays, plan + [{"label": "hit-m1", "set": "rt_scene_handout=1"}])[0]
        self.assertEqual(len(found[sane.FAIL]), 1)
        self.assertIn("hit-m2", found[sane.FAIL][0])
        # an OFF state with the (no-op) hit-wait key is not a fix state
        off = [{"label": "hit-off-a", "set": "rt_scene_handout=0,rt_scene_handout_hit_wait=1"}]
        self.assertEqual(hq.hit_checks({}, off), ({sane.FAIL: [], sane.EXPLAIN: [], sane.INFO: []},
                                                  ["hit-off-a: window not judged (no 'GI far: ambient kept at qualified hits' line)"]))

    def test_inn_fix_211816_not_faults(self):
        """The six FAIL lines of hq-inn-fix.1.211816 (2026-11-14) that were no fault: all INFO, none FAIL."""
        # pan RT +0.130 ms on 11.3 ms: 0.017 ms over the floor, noise 0.095
        off = [11.344, 11.445, 11.255, 11.303, 11.323, 11.269, 11.336]
        self.assertEqual(self.status(11.323, 11.323, 11.453, sane.cost_floor(11.323), "not higher", off_values=off), sane.INFO)
        # hair-pan GI +0.164, noise 0.160, floor 0.158
        self.assertEqual(self.status(15.841, 15.703, 15.935, sane.cost_floor(15.772), "not higher"), sane.INFO)
        # static frame time p99 +1.075 from 3 + 1 OFF periods and 2 ON periods (a tail: the full OFF range is its noise)
        p99 = [32.59, 31.97, 33.83, 32.93]
        self.assertEqual(self.status(32.59, 32.93, 33.835, sane.cost_floor(32.76), "not higher", True, p99, tail=True), sane.OK)
        self.assertEqual(self.status(32.59, 32.93, 33.835, sane.cost_floor(32.76), "not higher", True, p99), sane.INFO)
        self.assertEqual(self.status(32.59, 32.93, 37.0, sane.cost_floor(32.76), "not higher", True, p99, tail=True), sane.FAIL)
        # timeouts in the OFF hit windows are the baseline; light rays +3% against an OFF pair 3% apart
        plan = [{"label": f"hit-{s}", "set": ""} for s in ("off-a", "on", "off-b")]
        lines = {"scene wait timeouts (max)": {"hit-off-a": 1, "hit-on": 0, "hit-off-b": 3, "energy-on": 0},
                 "GI far: ambient kept at qualified hits": {"hit-off-a": 0.0022, "hit-on": 0.00224, "hit-off-b": 0.00212},
                 "GI hit direct: light rays traced": {"hit-off-a": 2.9116e7, "hit-on": 2.95581e7, "hit-off-b": 2.82469e7}}
        found, notes = hq.hit_checks(lines, plan)
        self.assertEqual((found[sane.FAIL], found[sane.EXPLAIN], notes), ([], [], []))
        self.assertEqual(len(found[sane.INFO]), 3)   # two OFF windows with timeouts, the light rays
        # the same events ON: at or under the baseline INFO, above it FAIL; without a baseline one event is a fault
        lines["scene wait timeouts (max)"]["hit-on"] = 3
        self.assertEqual(hq.hit_checks(lines, plan)[0][sane.FAIL], [])
        lines["scene wait timeouts (max)"]["hit-on"] = 4
        self.assertEqual(len(hq.hit_checks(lines, plan)[0][sane.FAIL]), 1)
        lines["scene wait timeouts (max)"] = {"energy-on": 1, "energy-off-a": 0}
        self.assertEqual(len(hq.hit_checks(lines, plan)[0][sane.FAIL]), 1)
        self.assertEqual([sane.events(0, 0), sane.events(1, 0), sane.events(2, 3), sane.events(4, 3)],
                         [sane.OK, sane.FAIL, sane.INFO, sane.FAIL])
        # light rays: 10% under the OFF pair is the hit work missing, whatever the 3% spread
        lines["GI hit direct: light rays traced"]["hit-on"] = 2.58e7
        self.assertEqual(len(hq.hit_checks(lines, plan)[0][sane.FAIL]), 2)

    def test_group_timeouts_against_off(self):
        plan = [{"label": f"pan-{s}", "frames": 420} for s in ("off-a", "on", "off-b")]
        m = {p["label"]: {"scene wait mean": [8.5], "frame time median": [30.0], "scene wait timeouts": [0, 2]} for p in plan}
        m["pan-on"].update({"scene wait mean": [2.9], "scene wait timeouts": [1, 0], "handout fallbacks": [0, 0]})
        found = hq.judge(m, plan)[1]
        self.assertEqual((found[sane.FAIL], len(found[sane.INFO])), ([], 1))   # 1 ON against 2 OFF: baseline
        m["pan-on"]["scene wait timeouts"] = [3, 0]
        self.assertEqual(len(hq.judge(m, plan)[1][sane.FAIL]), 1)
        m["pan-on"].update({"scene wait timeouts": [0], "handout fallbacks": [0, 1]})   # the worst period, no baseline
        self.assertEqual(len(hq.judge(m, plan)[1][sane.FAIL]), 1)


if __name__ == "__main__":
    unittest.main()
