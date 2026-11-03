"""The preservation gate rejects byte changes/removal in previously active rows."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.mesh_material_replay_audit import POOL_BASE, active_rows, preservation


class PreservationTest(unittest.TestCase):
    def rows(self):
        raw = bytearray(16 + 3 * 256)
        struct.pack_into("<4I", raw, 0, 77, 5, 0, 1)
        struct.pack_into("<I", raw, 16, 1)
        struct.pack_into("<I", raw, 16 + 28, 77)
        struct.pack_into("<I", raw, 16 + 256, 2)
        struct.pack_into("<I", raw, 16 + 256 + 28, 76)  # stale, never a qualified row
        return raw

    def test_active_qualified_superset(self):
        self.assertEqual(list(active_rows(self.rows())), ["captured:0"])

    def test_any_material_byte_change_fails(self):
        before = self.rows()
        after = bytearray(before)
        after[16 + 220] ^= 1
        result = preservation(before, after)
        self.assertFalse(result["passed"])
        self.assertEqual(result["changed_rows"], ["captured:0"])

    def test_removal_and_epoch_change_fail(self):
        before = self.rows()
        self.assertFalse(preservation(before, before[:16])["passed"])
        after = bytearray(before)
        after[0] ^= 1
        self.assertFalse(preservation(before, after)["passed"])

    def test_only_inactive_row_change_preserves_existing_rows(self):
        before = self.rows()
        after = bytearray(before)
        after[16 + 256 + 80] ^= 1
        result = preservation(before, after)
        self.assertTrue(result["passed"])
        self.assertFalse(result["evaluated_energy_proved"])

    def test_pool_offset_and_changed_basis_pointer_fail(self):
        raw = self.rows()
        raw.extend(b"\0" * (POOL_BASE + 256 - len(raw)))
        struct.pack_into("<I", raw, POOL_BASE, 1)
        struct.pack_into("<I", raw, POOL_BASE + 28, 77)
        self.assertIn("pool:0", active_rows(raw))
        after = bytearray(raw)
        after[POOL_BASE + 12] ^= 1  # native pool basis pointer
        self.assertEqual(preservation(raw, after)["changed_rows"], ["pool:0"])

    def test_malformed_baseline_header_rejected(self):
        with self.assertRaises(ValueError):
            active_rows(b"short")

    def test_empty_active_set_is_not_energy_proof(self):
        result = preservation(bytes(16), bytes(16))
        self.assertTrue(result["passed"])
        self.assertEqual(result["active_rows_compared"], 0)
        self.assertFalse(result["evaluated_energy_proved"])


if __name__ == "__main__":
    unittest.main()
