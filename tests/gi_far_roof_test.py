"""CPU segment fixtures and source wiring for far-GI screen-hit roof rejection.

No game/GPU required. --source accepts the old shader to demonstrate the regression.
The CPU mirror exercises the existing roof-volume math; source checks bind its
decision to the screen read and the early exit before alternative radiance paths.
"""
import argparse
import math
from pathlib import Path
import re
import unittest

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, default=ROOT / "shaders/gi/gi_trace_rt.hlsl")
args, remaining = parser.parse_known_args()
SOURCE = re.sub(r"//[^\n]*|/\*.*?\*/", "", args.source.read_text(encoding="utf-8"), flags=re.S)


def qrot_inv(q, v):
    xyz, w = np.asarray(q[:3]), q[3]
    return v + 2 * np.cross(-xyz, np.cross(-xyz, v) + w * v)


def roof_clear(o, d, t, volumes, camera=(0, 0, 0), slot=True, known=True):
    """Mirror gi_far_roof_clear's finite, padded OBB segment test (world volumes)."""
    if not slot or not known:
        return False
    for center, q, half in volumes:
        lo = qrot_inv(q, np.asarray(o) - (np.asarray(center) - camera))
        ld = qrot_inv(q, np.asarray(d))
        inv = 1 / np.where(np.abs(ld) > 1e-8, ld, np.where(ld >= 0, 1e-8, -1e-8))
        a, b = (-np.asarray(half) - lo) * inv, (np.asarray(half) - lo) * inv
        entry, leave = max(float(np.max(np.minimum(a, b))), 0), float(np.min(np.maximum(a, b)))
        if leave >= entry and entry <= t:
            return False
    return True


class FarGiRoof(unittest.TestCase):
    def test_source_wiring(self):
        compact = re.sub(r"\s+", " ", SOURCE)
        self.assertIn("#if (RT_GI_FAR || defined(RT_SPEC_GI_FAR)) && RT_HIT_GI_SUN", compact)
        self.assertRegex(compact, r"#if defined\(RT_SPEC_GI_FAR\) if \(kSpecGiFar != 0u\) #endif screen_roof_blocked = t > RB && !gi_far_roof_clear\(o, dir, t\);")
        self.assertRegex(compact, r"if \(!screen_roof_blocked\) \{ #endif float3 L = prev_bounce_at\(qp\);")
        self.assertRegex(compact, r"if \(screen_roof_blocked\) \{ if \(pc.rt_stats != 0u\) g_far_stat\[4\] \+= 1u; continue; \}")
        reject = SOURCE.index("if (screen_roof_blocked)")
        self.assertLess(SOURCE.index("stat_add(12,"), reject)
        self.assertLess(reject, SOURCE.index("if (visible && (HIT_EM_FLAGS"))
        self.assertLess(reject, SOURCE.index("sharc_ray_hit(hh"))
        # The same function is still used by both hidden-hit reference/spec paths.
        self.assertEqual(SOURCE.count("!gi_far_roof_clear(o,dir,t)"), 2)
        self.assertIn("if (t3 >= t0 && t0 <= t) return false;", SOURCE)
        self.assertIn("(pc.ls_flags & (1u << 25)) == 0u || (st_u(4) & 2u) == 0u", SOURCE)

    def test_segment_radiance_fixtures(self):
        identity = (0, 0, 0, 1)
        room = [((0, 0, 0), identity, (1, 1, 1))]
        canopy = [((0, 0, 3), identity, (1, 1, 0.25))]
        rotated = [((0, 0, 3), (0, 0, math.sin(math.pi / 8), math.cos(math.pi / 8)), (2, 0.25, 0.25))]
        # hit=4 m, base=1 m, far=6 m, radiance=12: old leaked bounce is 20/3.
        cases = [
            ("inside roofed room", (0, 0, 0), (0, 0, 1), 4, room, {}, True),
            ("cross canopy", (0, 0, 0), (0, 0, 1), 4, canopy, {}, True),
            ("hit on canopy boundary", (0, 0, 0), (0, 0, 1), 2.75, canopy, {}, True),
            ("roof beyond hit", (0, 0, 0), (0, 0, 1), 2, canopy, {}, False),
            ("roof behind ray", (0, 0, 5), (0, 0, 1), 4, canopy, {}, False),
            ("parallel outside", (2, 0, 0), (0, 0, 1), 4, canopy, {}, False),
            ("rotated canopy", (1, 1, 0), (0, 0, 1), 4, rotated, {}, True),
            ("camera-relative", (-100, 0, 0), (0, 0, 1), 4, canopy, {"camera": np.array((100, 0, 0))}, True),
            ("known empty volumes", (0, 0, 0), (0, 0, 1), 4, [], {}, False),
            ("unknown volumes", (0, 0, 0), (0, 0, 1), 4, [], {"known": False}, True),
            ("missing trust slot", (0, 0, 0), (0, 0, 1), 4, [], {"slot": False}, True),
            ("base radius unchanged", (0, 0, 0), (0, 0, 1), 1, room, {}, False),
        ]
        match = re.search(r"screen_roof_blocked\s*=\s*(t\s*>\s*RB\s*&&\s*!gi_far_roof_clear\(o,\s*dir,\s*t\));", SOURCE)
        self.assertIsNotNone(match, "the old screen-hit path has no roof guard")
        expression = match.group(1).replace("&&", "and").replace("!gi_far_roof_clear(o, dir, t)", "not clear")
        for name, o, d, t, volumes, options, expected in cases:
            with self.subTest(name=name):
                clear = roof_clear(o, d, t, volumes, **options)
                blocked = eval(expression, {"__builtins__": {}}, {"t": t, "RB": 1, "clear": clear})
                self.assertEqual(blocked, expected)
                old_bounce = 12 * (1 - (t / 6) ** 2)
                bounce, occ = (0, 0) if blocked else (old_bounce, 1 - (t / 6) ** 2)
                if expected:
                    self.assertGreater(old_bounce, 0)
                    self.assertEqual((bounce, occ), (0, 0))
                else:
                    self.assertEqual(bounce, old_bounce)


if __name__ == "__main__":
    unittest.main(argv=[__file__, *remaining])
