"""CPU fixtures for F03 half stores, F06 mip coordinates and F07 UV bounds.

Float32 arithmetic and float16 narrowing model the shader math. Source assertions
tie each twin to its shader; no GPU/game or generated fixtures are required.
"""
from pathlib import Path
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def page_coordinate(uv, dims, requested, levels, clamp_first):
    mip = min(requested, max(levels, 1) - 1) if clamp_first else requested
    pixel = np.floor(np.float32(uv) * np.float32(dims) * np.float32(2.0 ** -mip))
    effective = min(requested, max(levels, 1) - 1)
    return effective, int(np.clip(pixel, 0, max(dims >> effective, 1) - 1))


def screen_texel(uv, size, fixed):
    uv = np.asarray(uv, dtype=np.float32)
    if np.any(uv < 0) or np.any(uv >= 1 if fixed else uv > 1):
        return None
    return tuple((uv * np.asarray(size, dtype=np.float32)).astype(np.int32))


def radiance_store(diffuse, specular, fixed, water):
    total = np.asarray(diffuse, np.float32) + np.asarray(specular, np.float32)
    if fixed or water:
        total = (np.clip(total, 0, 65504) if fixed else np.maximum(total, 0)) if np.all(np.isfinite(total)) else np.zeros(3)
    with np.errstate(over="ignore", invalid="ignore"):
        return total.astype(np.float16)


class ShaderBoundsTest(unittest.TestCase):
    def test_f06_short_view_uses_effective_mip_coordinate(self):
        # Page IDs equal their x coordinates: requesting mip 4 from a view with
        # levels 0..2 must fetch page 12 at mip 2, rather than unrelated page 3.
        self.assertEqual(page_coordinate(.75, 64, 4, 3, False), (2, 3))
        self.assertEqual(page_coordinate(.75, 64, 4, 3, True), (2, 12))
        self.assertEqual(page_coordinate(.75, 64, 4, 1, True), (0, 48))

    def test_f06_equality_when_mip_is_available(self):
        for mip in range(7):
            for uv in [0, .125, .75, 1]:
                self.assertEqual(page_coordinate(uv, 64, mip, 7, False),
                                 page_coordinate(uv, 64, mip, 7, True))
        src = (ROOT / "shaders/rt/rt_common.hlsli").read_text()
        self.assertLess(src.index("mip = min(mip, float(max(ilevels"),
                        src.index("float2 ip = floor(u * dims"))

    def test_f07_both_boundary_axes_and_interior_equality(self):
        size = (1920, 1080)
        for uv in [(1, 0), (0, 1), (1, 1)]:
            old = screen_texel(uv, size, False)
            self.assertTrue(any(p >= n for p, n in zip(old, size)))
            self.assertIsNone(screen_texel(uv, size, True))
        for uv in [(0, 0), (.5, .5), (np.nextafter(np.float32(1), np.float32(0)), .5), (-.001, .5), (1.001, .5)]:
            self.assertEqual(screen_texel(uv, size, False), screen_texel(uv, size, True))
        for shader in ["gi_trace.hlsl", "gi_contact.hlsl"]:
            src = (ROOT / "shaders/gi" / shader).read_text()
            self.assertIn("any(quv >= 1.0)", src)
            self.assertNotIn("any(quv > 1.0)", src)

    def test_f03_finite_sum_overflows_old_half_store(self):
        for water in [False, True]:
            old = radiance_store([60000] * 3, [60000] * 3, False, water)
            fixed = radiance_store([60000] * 3, [60000] * 3, True, water)
            self.assertTrue(np.all(np.isinf(old)))
            self.assertTrue(np.all(np.isfinite(fixed)))
            np.testing.assert_array_equal(fixed, [65504] * 3)

    def test_f03_equality_for_representable_radiance(self):
        for water in [False, True]:
            for diffuse, specular in [([0, .1, 1], [0, .2, 2]),
                                      ([60000, 32752, 65504], [0, 32752, 0])]:
                np.testing.assert_array_equal(radiance_store(diffuse, specular, False, water),
                                              radiance_store(diffuse, specular, True, water))
        for path, variable in [("shaders/gi/gi_composite.hlsl", "tot"),
                               ("shaders/rt/water_compose.hlsl", "L")]:
            src = (ROOT / path).read_text()
            self.assertIn(f"all(isfinite({variable})) ? clamp({variable}, 0.0, 65504.0) : 0.0.xxx", src)

    def test_f03_negative_and_nonfinite_are_not_radiance(self):
        for water in [False, True]:
            np.testing.assert_array_equal(radiance_store([-1, 0, 1], [0] * 3, True, water), [0, 0, 1])
            for invalid in [np.inf, -np.inf, np.nan]:
                result = radiance_store([invalid, 1, 2], [0] * 3, True, water)
                self.assertTrue(np.all(np.isfinite(result)))


if __name__ == "__main__":
    unittest.main()
