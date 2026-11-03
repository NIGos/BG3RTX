"""The preservation gate rejects byte changes/removal in previously active rows."""
import struct
import sys
import unittest
import tempfile
import hashlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.mesh_material_replay_audit import POOL_BASE, active_rows, preservation, pak_geometry


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

    def test_exact_pool_candidate_is_not_a_join_and_hash_is_not_proof(self):
        with tempfile.TemporaryDirectory() as tmp:
            pack = Path(tmp)
            full = struct.pack('<15f', 0,0,0,0,0, 1,0,0,1,0, 0,1,0,0,1)
            native = b''.join(full[i:i+12] + b'\0'*4 + full[i+12:i+20] for i in range(0,60,20))
            (pack/'pool').write_bytes(native)
            (pack/'manifest.txt').write_text('buffer 1 8 72 pool\n')
            (pack/'s1b7.bin').write_bytes(struct.pack('<4I', 0, 256|24, 0, 1))
            corners = pack/'corners'; corners.write_bytes(full)
            row = dict(material_id='one', full_geometry_cache=str(corners))
            index = {(3, hashlib.sha256(full).hexdigest()): [row]}
            result = pak_geometry(pack, index)
            self.assertEqual(result['unique_candidate'], 1)
            self.assertEqual(result['proven_joined_rows'], 0)
            self.assertIsNone(result['joined_coverage_estimate'])
            index[next(iter(index))].append(dict(row, material_id='two'))
            self.assertEqual(pak_geometry(pack, index)['ambiguous'], 1)
            index[next(iter(index))][1] = dict(row, color_layout=1, color_sha256='different native colors')
            self.assertEqual(pak_geometry(pack, index)['ambiguous'], 1)
            corners.write_bytes(b'changed')
            self.assertEqual(pak_geometry(pack, index)['missing'], 1)
            (pack/'s1b7.bin').write_bytes(struct.pack('<4I', 0, 256|16, 0, 1))
            self.assertEqual(pak_geometry(pack, index)['no_uv'], 1)


if __name__ == "__main__":
    unittest.main()
