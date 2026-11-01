"""The RT sun's roof-volume cap (core/sun_trust.h kSunMaxVolumes) holds every level's roof volumes.

On 2026-11-01 the cap was 256 and CTY_Main_A dropped 103 of 359 volumes (the derived query radius reached
1980 m): pixels under them were trusted. The C++ cap and the shaders' SUN_MAX_VOLUMES must agree, the
slot must hold the records, and with the asset DB present (data/assetdb) no level may exceed the cap
(scripts/roof_stats.py --check).

    py -3 -m unittest tests.roof_cap_test
"""
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import roof_stats  # noqa: E402


class RoofCap(unittest.TestCase):
    def test_cpp_and_shader_caps_agree(self):
        cpp, hlsl = roof_stats.read_cap(str(ROOT))
        self.assertEqual(cpp, hlsl)
        for f in ("shaders/gi/sun_roof.hlsli", "shaders/gi/sharc_roof.hlsl", "shaders/gi/gi_trace_rt.hlsl"):
            src = (ROOT / f).read_text(encoding="utf-8")
            self.assertNotRegex(src, r"min\(st_u\(0\), \d+u\)", f"{f}: a literal volume cap (use SUN_MAX_VOLUMES)")

    def test_slot_holds_the_records(self):
        cpp, _ = roof_stats.read_cap(str(ROOT))
        h = (ROOT / "core/sun_trust.h").read_text(encoding="utf-8")
        c = (ROOT / "core/sun_trust.cpp").read_text(encoding="utf-8")
        header = int(re.search(r"kSunHeader = (\d+);", h).group(1))
        slot = int(re.search(r"kSlotBytes = (\d+);", c).group(1))
        self.assertLessEqual(header + cpp * 48, slot)   # Bg3DbVolume is 48 bytes

    def test_every_level_fits(self):
        db = ROOT / "data" / "assetdb" / "bg3rtx_assetdb.bin"
        if not db.exists():
            self.skipTest("no asset DB in this checkout")
        r = subprocess.run([sys.executable, str(ROOT / "scripts" / "roof_stats.py"), str(db), "--check"],
                           capture_output=True, text=True, cwd=str(ROOT))
        self.assertEqual(r.returncode, 0, r.stdout[-2000:] + r.stderr[-2000:])
        self.assertIn("roof cap: at most", r.stdout)


if __name__ == "__main__":
    unittest.main()
