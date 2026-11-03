from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from tools.assets import mesh_paks as p


class MeshPaksTests(unittest.TestCase):
    def setUp(self):
        self.v = np.array([[0, 0, 0, 0, 0], [1, 0, 0, 1, 0], [0, 1, 0, 0, 1]], '<f4')
        self.i = np.array([0, 1, 2], '<u4')
        self.mid = '00000000-0000-0000-0000-000000000001'
        self.doc = dict(materials={self.mid: dict(source='Lava.lsf', native={})},
                        textures={}, templates={'Lava.lsf': {'template': {'flags': (1 << 15) | (1 << 17)}}},
                        rows=[dict(source='x.GR2', object_id='x.Mesh.0', material_id=self.mid,
                                   material_resolved=True, ambiguous=False, lod=0)])

    def dump(self):
        name = b'Mesh'
        return (b'GR2P' + struct.pack('<III', 1, 1, len(name)) + name + struct.pack('<III', 3, 3, 1)
                + self.v.tobytes() + self.i.tobytes() + struct.pack('<III', 0, 0, 1))

    def test_content_contract_and_uv(self):
        sig, raw = p.signature(self.v, self.i)
        self.assertEqual(sig['canonical_words'][:3], [1, 3, 3])
        self.assertEqual(len(sig['canonical_words']), 131)
        self.assertEqual(sig['content_id'], 'd5ac4d013e3b5a83')
        self.assertEqual(sig['uv'][:2], [0, 0])
        self.assertEqual(sig['uv'][-2:], [0, 1])
        self.assertEqual(raw, self.v.tobytes())
        moved = np.vstack([np.zeros((4, 5), '<f4'), self.v])
        other, _ = p.signature(moved, self.i + 4)
        self.assertEqual(sig, other)

    def test_mutations_and_invalid_data(self):
        sig, _ = p.signature(self.v, self.i)
        v = self.v.copy(); v[1, 3] += .01
        changed, _ = p.signature(v, self.i)
        self.assertEqual(sig['content_id'], changed['content_id'])
        self.assertNotEqual(sig['uv'], changed['uv'])
        self.assertNotEqual(sig['geometry_sha256'], changed['geometry_sha256'])
        v[0, 0] = np.nan
        with self.assertRaises(ValueError):
            p.signature(v, self.i)
        with self.assertRaises(ValueError):
            p.signature(self.v, [0, 1, 9])

    def test_strict_native_decoder(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'m.bin'; path.write_bytes(self.dump())
            v, i, groups, colors = p.read_dump(path)['Mesh']
            self.assertIsNone(colors)
            np.testing.assert_array_equal(v, self.v)
            np.testing.assert_array_equal(i, self.i)
            np.testing.assert_array_equal(groups, [[0, 0, 1]])
            for raw in (self.dump()[:-1], self.dump()+b'!', b'GR2P'+struct.pack('<I', 3)):
                path.write_bytes(raw)
                with self.assertRaises(ValueError):
                    p.read_dump(path)

    def test_exact_object_id_no_ordinal_mesh_guess(self):
        self.assertEqual(p.mesh_object('file.Mesh.1'), ('Mesh', 1))
        for s in ('Mesh', 'file.Mesh', 'file.Mesh.foo', 'file.Mesh.extra.0'):
            with self.assertRaises(ValueError):
                p.mesh_object(s)

    def test_metadata_conflicts_fail_closed(self):
        info = p.material_info(self.doc, self.mid)
        self.assertTrue(info['two_sided']); self.assertTrue(info['uses_vertex_color'])
        self.doc['materials'][self.mid]['native'] = dict(children=[dict(_='Texture2DParameters', ID='missing', ParameterName='BaseColor')])
        with self.assertRaises(ValueError):
            p.material_info(self.doc, self.mid)
        self.doc['rows'][0]['ambiguous'] = True
        self.assertEqual(p.select_rows(self.doc), [])

    def test_global_ambiguity_poison_across_export_orders_and_lods(self):
        row = dict(self.doc['rows'][0], object_id='x.Mesh.1', lod=1,
                   material_id='another-material', ambiguous=True)
        self.doc['rows'].append(row)
        self.assertEqual(p.select_rows(self.doc), [])
        row['ambiguous'] = False
        self.assertEqual(p.select_rows(self.doc), [])

    def test_disabled_texture_ignored(self):
        self.doc['materials'][self.mid]['native'] = dict(children=[dict(
            _='Texture2DParameters', ID='missing', ParameterName='BaseColor', Enabled=False)])
        self.assertEqual(p.material_info(self.doc, self.mid)['textures'], {})

    def test_binary_contract(self):
        sig, full = p.signature(self.v, self.i)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'corners'; path.write_bytes(full)
            row = dict(**sig, **p.material_info(self.doc, self.mid), full_geometry_cache=str(path))
            raw = p.binary_table([row])
            self.assertEqual(len(raw), 16 + 840 + 4 + 60 + 8)
            self.assertEqual(struct.unpack_from('<8sII', raw), (b'B3PAKS1\0', 3, 1))
            self.assertEqual(raw[16+804:16+840].decode(), self.mid)
            self.assertEqual(raw[-68:-8], full)
            row['template'] = 'Public/Engine/Assets/Materials/Engine_Default_HLOD_GM_VT.lsf'
            tagged = p.binary_table([row])
            self.assertEqual(struct.unpack_from('<I', tagged, 16+16)[0], 7)
            path.write_bytes(full[:-1])
            with self.assertRaises(ValueError):
                p.binary_table([row])

    def test_native_color_layout_and_order(self):
        colors = np.array([[0, .2, .4, 1], [1, 0, 0, .5], [.1, .3, .7, 1]], '<f4')
        name = b'Mesh'
        raw = (b'GR2P' + struct.pack('<III', 2, 1, len(name)) + name
               + struct.pack('<IIII', 3, 3, 1, 1) + self.v.tobytes() + colors.tobytes()
               + self.i.tobytes() + struct.pack('<III', 0, 0, 1))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'dump'; path.write_bytes(raw)
            _, indices, _, decoded = p.read_dump(path)['Mesh']
            np.testing.assert_array_equal(decoded[indices], colors)
            for broken in (raw[:-1], raw[:32] + struct.pack('<I', 2) + raw[36:]):
                path.write_bytes(broken)
                with self.assertRaises(ValueError): p.read_dump(path)
            sig, full = p.signature(self.v, self.i)
            geom = Path(tmp)/'corners'; geom.write_bytes(full)
            color = Path(tmp)/'color'; color.write_bytes(colors.tobytes())
            import hashlib
            row = dict(**sig, **p.material_info(self.doc, self.mid), full_geometry_cache=str(geom),
                       color_layout=1, color_sha256=hashlib.sha256(color.read_bytes()).hexdigest(),
                       full_color_cache=str(color))
            encoded = p.binary_table([row])
            self.assertEqual(encoded[-48:], colors.tobytes())
            self.assertEqual(struct.unpack_from('<II', encoded, len(encoded)-56), (1, 3))
            color.write_bytes(color.read_bytes()[:-1])
            with self.assertRaises(ValueError): p.binary_table([row])
            row['color_layout'] = 2
            with self.assertRaises(ValueError): p.binary_table([row])

    def test_catalogue_inventory_tracks_rejected_and_unprocessed(self):
        row = dict(self.doc['rows'][0], source='bad.GR2', ambiguous=True)
        self.doc['rows'].append(row)
        plan = p.catalogue_plan(self.doc)
        self.assertEqual(plan['counts'], dict(candidates=2, eligible=1, rejected_metadata=1,
                                             decoded=0, joined=0, unprocessed=1, sources=1))
        self.assertEqual(plan['families']['Lava.lsf'], dict(candidates=2, eligible=1))

    def test_streaming_bounded_immutable_shards(self):
        sig, full = p.signature(self.v, self.i)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'corners'; path.write_bytes(full)
            row = dict(**sig, **p.material_info(self.doc, self.mid), full_geometry_cache=str(path))
            target = Path(tmp)/'mesh_paks_cov_paks2.json'
            names = p.write_sharded_tables([row, row, row], target, 1840)
            self.assertEqual(names, ['mesh_paks_cov_paks2_0000.bin', 'mesh_paks_cov_paks2_0001.bin'])
            for name, count in zip(names, (2, 1)):
                raw = (Path(tmp)/name).read_bytes()
                self.assertLessEqual(len(raw), 1840)
                self.assertEqual(struct.unpack_from('<8sII', raw), (b'B3PAKS1\0', 3, count))
            with self.assertRaises(ValueError): p.write_sharded_tables([row], target, 1840)
            with self.assertRaises(ValueError): p.write_sharded_tables([row], Path(tmp)/'too-small.json', 16)

    def test_incremental_cache_and_group_failure(self):
        class Pak:
            def read(_, name):
                return b'native gr2'
        with tempfile.TemporaryDirectory() as tmp:
            def decode(lslib, source, output, pwsh):
                output.write_bytes(self.dump())
            with patch.object(p, 'dump', side_effect=decode) as fn:
                result = p.build(self.doc, Pak(), 'lslib', tmp)
                self.assertEqual(len(result['rows']), 1)
                self.assertFalse(result['runtime_join_ready'])
                again = p.build(self.doc, Pak(), 'lslib', tmp)
                self.assertEqual(fn.call_count, 1)
                self.assertEqual(again['counts']['hits'], 1)
                self.doc['rows'][0]['object_id'] = 'x.MissingMesh.2'
                reject = p.build(self.doc, Pak(), 'lslib', tmp)
                self.assertEqual(reject['rows'], [])
                self.assertEqual(len(reject['failures']), 1)


if __name__ == '__main__':
    unittest.main()
