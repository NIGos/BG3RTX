"""tools/hit_material_blockers.py on a synthetic pack: capture classes from InstInfo bits, the alpha record's
FS hash and row, the per-instance HM_BLOCKERS slots, shares and the shape descriptor.

    py -3 -m unittest tests.hit_material_blockers_test
"""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import hit_material_blockers as hb  # noqa: E402

FRAME = 77


def write_pack(d):
    stride = 64
    # soup: instance 0 (terrain-like 100 x 100 m quad, 2 triangles), instance 1 (alpha, 1 triangle 1 m)
    soup = bytearray()
    def vert(p):
        v = bytearray(stride)
        struct.pack_into("<3f", v, 0, *p)
        return bytes(v)
    for p in [(0, 0, 0), (100, 0, 0), (100, 0, 100), (0, 0, 0), (100, 0, 100), (0, 0, 100)]:
        soup += vert(p)
    alpha_base = len(soup)
    for p in [(0, 0, 0), (1, 0, 0), (0, 1, 0)]:
        soup += vert(p)
    # pool: instance 2 (pool, with a row), instance 3 (pool, no row): one 2 m triangle each, identity matrix
    pool = bytearray()
    for p in [(0, 0, 0), (2, 0, 0), (0, 2, 0)]:
        pool += vert(p)
    info = [(0, stride, 0, 2), (alpha_base, stride | hb.ALPHA, 16 + 5 * 208, 1),
            (0, stride | hb.POOL | hb.POOL_ROW, (1 << 31) | 3, 1), (0, stride | hb.POOL, 0, 1)]
    (d / "s1b7.bin").write_bytes(b"".join(struct.pack("<4I", *r) for r in info))
    alpha = bytearray(16 + 208 * 8)
    struct.pack_into("<4I", alpha, 16 + 5 * 208, 0, 1, 1, 3)                 # first_tri, tris, kind 1, tex
    struct.pack_into("<2I", alpha, 16 + 5 * 208 + 40, 0x4d5d6d7d, 0x0d91554d)  # fs lo, hi
    (d / "s1b0.bin").write_bytes(bytes(alpha))
    hit = bytearray(16 + 256 * 8)
    struct.pack_into("<4I", hit, 0, FRAME, 1, 1, 1)                         # record 5 has no row (valid 0)
    (d / "s1b12.bin").write_bytes(bytes(hit))
    inst = bytearray(64 * 4)
    for i in range(4):
        struct.pack_into("<12f", inst, 64 * i, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
    (d / "instances.bin").write_bytes(bytes(inst))
    (d / "s1b8.bin").write_bytes(bytes(pool))
    (d / "s1b6.ranges.bin").write_bytes(bytes(soup))
    (d / "manifest.txt").write_text(f"bg3rtx_replay_pack 1\nrange 1 6 0 s1b6.ranges.bin 0 {len(soup)}\n")
    stats = np.zeros(512 * 64, dtype="<u4")
    counts = {0: (0, 40, 0, 0), 1: (0, 0, 30, 0), 2: (20, 0, 0, 0), 3: (0, 9, 0, 1)}
    for i, c in counts.items():
        stats[hb.SLOT0 + 4 * i:hb.SLOT0 + 4 * i + 4] = c
    stats.tofile(d / "rtstats.bin")


class Blockers(unittest.TestCase):
    def test_rank(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            write_pack(d)
            out = hb.rank(str(d), str(d / "rtstats.bin"), top=4)
        self.assertIn("lookups 100 (qualified 20, base 49, row 30, other 1)", out)
        self.assertRegex(out, r"soup without record +40\.0%  qualified   0\.0  base  40\.0")
        self.assertRegex(out, r"soup alpha, no row +30\.0%  qualified   0\.0  base   0\.0  row  30\.0")
        self.assertRegex(out, r"pool with row +20\.0%  qualified  20\.0")
        self.assertRegex(out, r"pool without row +10\.0%  qualified   0\.0  base   9\.0  row   0\.0  other   1\.0")
        self.assertIn("terrain-like (>= 64 m flat patch)", out)
        self.assertIn("0d91554d4d5d6d7d kind 1", out)
        lines = [l for l in out.splitlines() if l.startswith("  inst ")]
        self.assertTrue(lines[0].startswith("  inst    0 40.00% soup without record"), lines[0])
        self.assertTrue(lines[1].startswith("  inst    1 30.00% soup alpha, no row"), lines[1])
        self.assertIn("area   10000.0 m2", lines[0])

    def test_shape(self):
        self.assertTrue(hb.shape(np.array([192.0, 28.0, 192.0]), 30000.0).startswith("terrain"))
        self.assertTrue(hb.shape(np.array([0.3, 1.8, 0.4]), 1.5).startswith("small"))
        self.assertEqual(hb.shape(np.array([19.5, 6.9, 15.3]), 23.0), "object")


if __name__ == "__main__":
    unittest.main()
