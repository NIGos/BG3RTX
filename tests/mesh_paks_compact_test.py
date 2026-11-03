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
from tools.mesh_paks_compact_audit import candidates, audit_log


class CompactTests(unittest.TestCase):
    def test_witness_reaches_existing_material_publication_path(self):
        # Complements the executable production-ring replay: it must remain
        # wired into the actual draw observation and hit lookup, not a test-only
        # witness consumer or a parallel publication mechanism.
        source = (Path(__file__).resolve().parents[1]/'layer/hit_material.cpp').read_text()
        match = source.split('const mesh_paks::Row* paks_match(', 1)[1].split('// The VS inputs', 1)[0]
        self.assertIn('pak_witness_get(state,args,ip,candidate.row', match)
        self.assertLess(match.index('mesh_paks::hlod_compatible(*candidate.row'),
                        match.index('pak_witness_get(state,args,ip,candidate.row'))
        observe = source.split('bool pool_observe(', 1)[1].split('PoolUse pool_lookup(', 1)[0]
        for call in ('paks_match(*state,args,inst,&proof)', 'payload.pak=true', 'payload.pak_valid=std::move(proof)',
                     'pool_table.observe(geometry,frame,o)', 'assign_material(', 'pool_job('):
            self.assertIn(call, observe)
        lookup = source.split('PoolUse pool_lookup(', 1)[1].split('void pool_evicted(', 1)[0]
        for gate in ('pool_table.lookup(', 'pool_current(', 'pool::Basis::Ready', 'paks_rows()'):
            self.assertIn(gate, lookup)

    def test_log_keeps_on_windows_and_does_not_invent_screen_coverage(self):
        text = ('[hit material paks UV] 3 corners: missing candidate\n'
                '[hit material paks UV] 6 corners: missing candidate\n'
                '[hit material paks join] attempts 10 unique 1 missing 9 draws 20\n'
                '[hit material paks] frame 120 enabled 1: exact 0/20 attempts; records 0 basis ready 0\n'
                '[hit material paks] frame 240 enabled 0: exact 0/20 attempts; records 0 basis ready 0\n')
        result = audit_log(text, {3: [object()]})
        self.assertEqual(result['missing_readbacks'], dict(count_present=1, count_absent=1))
        self.assertEqual([w['enabled'] for w in result['windows']], [1, 0])
        self.assertEqual(result['missing_attempt_percent'], 90)
        self.assertEqual(result['missing_draw_percent'], 45)
        self.assertIsNone(result['screen_share'])
        self.assertIsNone(result['missing_family_shares'])

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
