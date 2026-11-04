"""C10 CPU twins: float-domain rejection and callers' missing-history policy."""
import math
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def old_pixel(pixel, motion, size):
    # The old conversion is undefined for non-finite or out-of-int-range values.
    position = [p + 0.5 + m for p, m in zip(pixel, motion)]
    if any(not math.isfinite(x) or abs(x) >= 2**31 for x in position):
        raise ValueError("undefined float-to-int conversion")
    previous = tuple(math.floor(x) for x in position)
    return pixel if any(x < 0 or x >= n for x, n in zip(previous, size)) else previous


def new_pixel(pixel, motion, size, has_mv=True):
    if not has_mv:
        return pixel
    previous = tuple(p + 0.5 + m for p, m in zip(pixel, motion))
    if any(not math.isfinite(x) or x < 0 or x >= n for x, n in zip(previous, size)):
        return None
    return tuple(math.floor(x) for x in previous)


def temporal(current, history, valid, weight=0.1):
    # Conditional selection avoids inf * 0 / NaN * 0 on rejected history.
    return tuple(h * (1 - weight) + c * weight for c, h in zip(current, history)) if valid else current


class Reprojection(unittest.TestCase):
    def test_unchanged_valid_pixels(self):
        for pixel in [(0, 0), (15, 9), (31, 17)]:
            for motion in [(0, 0), (0.49, -0.49), (-0.5, -0.5), (1.25, 3.0)]:
                new = new_pixel(pixel, motion, (32, 18))
                if new is not None:
                    self.assertEqual(new, old_pixel(pixel, motion, (32, 18)))
        self.assertEqual(new_pixel((3, 4), (math.nan, math.inf), (32, 18), False), (3, 4))

    def test_reject_boundaries_before_conversion(self):
        for motion in [(-1, 0), (31.5, 0), (0, 17.5), (math.nan, 0),
                       (math.inf, 0), (-math.inf, 0), (1e30, 0), (-1e30, 0)]:
            self.assertIsNone(new_pixel((0, 0), motion, (32, 18)))
        self.assertEqual(old_pixel((0, 0), (31.5, 0), (32, 18)), (0, 0))
        with self.assertRaises(ValueError):
            old_pixel((0, 0), (math.nan, 0), (32, 18))
        self.assertEqual(new_pixel((0, 0), (-0.5, -0.5), (32, 18)), (0, 0))
        self.assertEqual(new_pixel((0, 0), (31.49, 17.49), (32, 18)), (31, 17))

    def test_rejected_temporal_history_is_fresh(self):
        fresh = (12.0, 4.0, 2.0, 0.5)
        for bad in [math.nan, math.inf, -math.inf, 10000.0]:
            self.assertEqual(temporal(fresh, (bad,) * 4, False), fresh)
        self.assertEqual(temporal((4.0,), (2.0,), True), (2.2,))

    def test_source_wiring(self):
        common = (ROOT / "shaders/gi/gi_common.hlsli").read_text()
        guard = common.index("if (!all(isfinite(prev))")
        convert = common.index("pq = int2(floor(prev));")
        self.assertLess(guard, convert)
        self.assertIn("any(prev >= float2(pc.full))", common[guard:convert])
        self.assertEqual(common.count("if (!prev_radiance_px(qp, pq)) return false;"), 2)
        screen = (ROOT / "shaders/gi/gi_trace.hlsl").read_text()
        self.assertIn("if (prev_bounce_at(qp, L)) {\n          bounce += L;\n          hits += 1.0;", screen)
        rt = (ROOT / "shaders/gi/gi_trace_rt.hlsl").read_text()
        self.assertIn("if (prev_bounce_at(qp, L))", rt)
        self.assertIn("if(!visible && !aborted", rt)  # existing fresh hit-light fallback
        reflection = (ROOT / "shaders/gi/rt_reflections.hlsl").read_text()
        self.assertIn("if (prev_radiance_at(qp, L))", reflection)
        self.assertIn("tier == 2 || tier == 3 || hs_check", reflection)
        native = (ROOT / "shaders/rt/native_hit_shade.hlsli").read_text()
        self.assertIn("return prev_bounce_at(qp,L);", native)
        reference = (ROOT / "tools/pt_reference.hlsl").read_text()
        # Missing history stays classified hidden; the fresh PT path continues.
        self.assertIn("r.cls = 2;", reference)
        self.assertIn("if (prev_bounce_at(qp, L)) { r.cls = 1; r.screen = L; }", reference)
        self.assertIn("const float3 p = native_hit_position(hh, o + dir * t);", reference)
        accum = (ROOT / "shaders/gi/gi_temporal.hlsl").read_text()
        self.assertIn("all(prev < float2(pc.half))", accum)
        self.assertIn("valid ? lerp(hist, cur, pc.history) : cur;", accum)


if __name__ == "__main__":
    unittest.main()
