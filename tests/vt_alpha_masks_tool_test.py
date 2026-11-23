"""tools/assets/vt_alpha_masks.py: levels are written at their exact mip size; tile-padded files are cropped in place.

LSLib pads a level below one tile (128 px) to the tile with opaque blocks; the layer's parse_mask
(common/vt_alpha_masks.h) needs the exact chain and rejected 17 of the 71 masks of 2026-11-02 as "invalid".
"""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.assets import vt_alpha_masks as v  # noqa: E402


def level(w, h, seed):
    """A w x h level of distinct BC4 blocks (block i = seed, i)."""
    bx, by = (w + 3) // 4, (h + 3) // 4
    return b"".join(struct.pack("<II", seed, i) for i in range(bx * by))


def padded(w, h, seed, pw, ph):
    """The w x h level inside a pw x ph one, as LSLib extracts it: content top-left, opaque blocks elsewhere."""
    bx, cx, cy = (pw + 3) // 4, (w + 3) // 4, (h + 3) // 4
    rows = []
    content = level(w, h, seed)
    for y in range((ph + 3) // 4):
        row = content[y * cx * 8:(y + 1) * cx * 8] if y < cy else b""
        rows.append(row + b"\xff\xff\0\0\0\0\0\0" * (bx - len(row) // 8))
    return b"".join(rows)


def mask(w0, h0, n, pad):
    body = [v.MAGIC + struct.pack("<3I", w0, h0, n)]
    for l in range(n):
        ew, eh = v.exact_level(w0, h0, l)
        body.append(padded(ew, eh, l, *v.padded_level(w0, h0, l)) if pad else level(ew, eh, l))
    return b"".join(body)


class Crop(unittest.TestCase):
    def test_crop_keeps_the_top_left_blocks(self):
        self.assertEqual(v.crop_bc4(padded(64, 32, 7, 128, 128), 128, 128, 64, 32), level(64, 32, 7))
        same = level(256, 128, 3)
        self.assertEqual(v.crop_bc4(same, 256, 128, 256, 128), same)
        with self.assertRaises(ValueError):
            v.crop_bc4(same, 256, 128, 512, 128)

    def test_padded_sizes_are_the_observed_ones(self):
        # data/assetdb/vt_alpha/1_1ee1...: 1024x1024, 5 levels, 704532 bytes (exact chain: 698388).
        self.assertEqual(len(mask(1024, 1024, 5, True)), 704532)
        self.assertEqual(len(mask(1024, 1024, 5, False)), 698388)
        # 8_87cb...: 256x1024, 5 levels, 196628 bytes (level 2 is 64x256, stored 128x256).
        self.assertEqual(v.padded_level(256, 1024, 2), (128, 256))
        self.assertEqual(len(mask(256, 1024, 5, True)), 196628)


class Repair(unittest.TestCase):
    def test_exact_file_unchanged(self):
        body = mask(512, 256, 4, False)
        self.assertEqual(v.repair_mask(body), (body, 0))

    def test_padded_file_cropped_to_the_exact_chain(self):
        for w0, h0, n in ((1024, 1024, 5), (1024, 512, 6), (256, 1024, 5), (4096, 2048, 8), (256, 512, 3)):
            new, cropped = v.repair_mask(mask(w0, h0, n, True))
            self.assertEqual(new, mask(w0, h0, n, False), (w0, h0, n))
            self.assertGreater(cropped, 0)

    def test_unrecognised(self):
        self.assertIsNone(v.repair_mask(b"XXXXXXXX" + bytes(12)))
        self.assertIsNone(v.repair_mask(mask(512, 512, 3, False) + b"\0" * 8))

    def test_repair_folder_writes_only_padded_files(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            (d / "a.vtalpha").write_bytes(mask(1024, 1024, 5, True))
            (d / "b.vtalpha").write_bytes(mask(512, 512, 3, False))
            (d / "c.vtalpha").write_bytes(b"junk")
            (d / "index.txt").write_text("x")
            before_b = os.stat(d / "b.vtalpha").st_mtime_ns
            self.assertEqual(v.repair(d, dry_run=True), (1, 1, 1))
            self.assertEqual((d / "a.vtalpha").read_bytes(), mask(1024, 1024, 5, True))
            self.assertEqual(v.repair(d), (1, 1, 1))
            self.assertEqual((d / "a.vtalpha").read_bytes(), mask(1024, 1024, 5, False))
            self.assertEqual(os.stat(d / "b.vtalpha").st_mtime_ns, before_b)
            self.assertEqual((d / "c.vtalpha").read_bytes(), b"junk")
            self.assertEqual(sorted(p.name for p in d.iterdir()), ["a.vtalpha", "b.vtalpha", "c.vtalpha", "index.txt"])
            self.assertEqual(v.repair(d), (2, 0, 1))   # idempotent


if __name__ == "__main__":
    unittest.main()
