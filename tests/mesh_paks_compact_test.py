"""Compact catalogue conservation, witnesses, immutable inputs and fail-closed publication."""
import contextlib
import hashlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import numpy as np
from tools.assets.mesh_paks import signature, binary_table
from tools.assets.mesh_paks_compact import compact
from tools.mesh_paks_compact_audit import candidates


class CompactTests(unittest.TestCase):
    def fixture(self, root):
        vertices = np.array([[0, 0, 0, .1, .2], [1, 0, 0, .3, .4], [0, 1, 0, .5, .6]], '<f4')
        sig, raw = signature(vertices, [0, 1, 2])
        geometry = root/'cache.corners'
        geometry.write_bytes(raw)
        color = root/'cache.colors'
        color.write_bytes(np.array([[0, 1, 0, 1]]*3, '<f4').tobytes())
        uuid = '00000000-0000-0000-0000-000000000001'
        row = dict(sig, material_id=uuid, template='Engine_Default_HLOD_GM_VT.lsf',
                   two_sided=True, uses_vertex_color=True, full_geometry_cache=str(geometry),
                   color_layout=1, full_color_cache=str(color), color_sha256=hashlib.sha256(color.read_bytes()).hexdigest(),
                   textures={'BaseColor': {'id': uuid}})
        batch = root/'mesh_paks_cov_paks2_fixture.json'
        shard = batch.with_suffix('.bin')
        shard.write_bytes(binary_table([row]))
        batch.write_text(json.dumps({'rows': [row]}))
        batch.with_suffix('.manifest').write_text(shard.name+'\n')
        summary = root/'summary.json'
        summary.write_text(json.dumps({'batches': [batch.name], 'counts': {'decoded': 1}}))
        return summary, shard, row

    def test_self_contained_roundtrip_and_immutable_sources(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temp)
            summary, shard, row = self.fixture(root)
            original = shard.read_bytes()
            out = root/'mesh_paks_cov_paks2_compact.bin'
            report = compact(summary, out)
            self.assertEqual(report['rows'], 1)
            got = candidates(out)[3][0]
            self.assertEqual(got['geometry'], row['geometry_sha256'])
            self.assertEqual(got['color'], row['color_sha256'])
            self.assertEqual(got['layout'], 1)
            self.assertEqual(got['flags'], 7)
            self.assertEqual(got['refs'], [b'BaseColor'+row['material_id'].encode()])
            self.assertEqual(shard.read_bytes(), original)
            # Compact parser reads no referenced native geometry/cache files.
            (root/'cache.corners').unlink()
            self.assertEqual(candidates(out)[3][0]['cid'], int(row['content_id'], 16))
            with self.assertRaises(FileExistsError):
                compact(summary, out)

    def test_corruption_never_publishes_manifest(self):
        for offset in (16+840+4, 16+840+4+60+8):
            with self.subTest(offset=offset), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                summary, shard, row = self.fixture(root)
                raw = bytearray(shard.read_bytes())
                raw[offset] ^= 1
                shard.write_bytes(raw)
                out = root/'bad.bin'
                with self.assertRaises(ValueError):
                    compact(summary, out)
                self.assertFalse(out.with_suffix('.manifest').exists())

    def test_count_conservation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            summary, shard, row = self.fixture(root)
            doc = json.loads(summary.read_text())
            doc['counts']['decoded'] = 2
            summary.write_text(json.dumps(doc))
            out = root/'bad.bin'
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(ValueError):
                compact(summary, out)
            self.assertFalse(out.with_suffix('.manifest').exists())


if __name__ == '__main__':
    unittest.main()
