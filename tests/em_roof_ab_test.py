"""tools/em_roof_ab.py: gi_far_roof_emission (1054) against the PT emission term, on synthetic arrays and files.

    py -3 tests/em_roof_ab_test.py
"""
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import em_roof_ab as ab  # noqa: E402


def frames(h=9, w=16):
    none = np.zeros((h, w, 4)); none[..., :3] = 0.2; none[..., 3] = 0.8
    off = none.copy(); off[..., :3] += 0.5            # emission 0.5 everywhere with 1054=0
    on = off.copy(); on[:3, :, :3] = none[:3, :, :3]   # 1054=1 drops it in the top three rows
    pt = np.full((h, w, 3), 0.5); pt[:3] = 0.05        # the PT: almost no emission arrives there (a roof)
    depth = np.ones((h, w))
    return off, on, none, pt, depth


class EmRoofAb(unittest.TestCase):
    def test_closer_where_it_acts(self):
        r = ab.compare(*frames())
        self.assertEqual((r["changed"], r["raised_pixels"], r["alpha_changed"]), (48, 0, 0))
        self.assertAlmostEqual(r["e_off"], 0.5); self.assertAlmostEqual(r["e_on"], 0.0); self.assertAlmostEqual(r["pt"], 0.05)
        self.assertAlmostEqual(r["mae_off"], 0.45); self.assertAlmostEqual(r["mae_on"], 0.05)
        self.assertEqual(r["tiles_farther"], 0); self.assertGreater(r["tiles_closer"], 0)
        self.assertIn("CLOSER", ab.report(r)[-1])

    def test_farther_when_the_emitter_is_real(self):
        off, on, none, pt, depth = frames()
        pt[:3] = 0.5   # no roof: the PT sees the emitter, dropping it is wrong
        self.assertIn("FARTHER", ab.report(ab.compare(off, on, none, pt, depth))[-1])

    def test_invalid_when_raised_or_occlusion_changed(self):
        off, on, none, pt, depth = frames()
        on[5, 5, 0] += 0.1
        r = ab.compare(off, on, none, pt, depth)
        self.assertEqual(r["raised_pixels"], 1)
        self.assertIn("INVALID", ab.report(r)[-1])
        off, on, none, pt, depth = frames()
        on[0, 0, 3] = 0.7
        self.assertIn("INVALID", ab.report(ab.compare(off, on, none, pt, depth))[-1])

    def test_no_change_and_unshaded(self):
        off, _, none, pt, depth = frames()
        depth[:] = 0; depth[5:] = 1
        r = ab.compare(off, off.copy(), none, pt, depth)
        self.assertEqual((r["shaded"], r["changed"]), (64, 0))
        self.assertIn("NO CHANGED PIXEL", ab.report(r)[-1])

    def test_files_round_trip(self):
        off, on, none, pt, depth = frames()
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            for name, a in (("off", off), ("on", on), ("none", none)):
                (t / name).mkdir()
                a.astype("<f2").tofile(t / name / "giRaw.raw")
                depth.astype("<f4").tofile(t / name / "giRawDepth.raw")
            (t / "pt").mkdir()
            h, w = depth.shape
            with open(t / "pt" / "pt_emission.pfm", "wb") as f:   # as frame_replay writes it: bottom-up rows
                f.write(f"PF\n{w} {h}\n-1.0\n".encode())
                f.write(pt[::-1].astype("<f4").tobytes())
            out = t / "report.txt"
            self.assertEqual(ab.main(["--off", str(t / "off"), "--on", str(t / "on"), "--none", str(t / "none"), "--pt", str(t / "pt"),
                                      "--out", str(out)]), 0)
            self.assertIn("CLOSER", out.read_text().splitlines()[-1])


if __name__ == "__main__":
    unittest.main()
