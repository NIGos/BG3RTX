"""The bounded export check rejects malformed streams without requiring LSLib."""
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from tools.assets.probe_foundry_mesh import accessor, check_collada, decode_mesh
from tools.assets.resource import Node


class MeshProbeTests(unittest.TestCase):
    def setUp(self):
        self.raw=b'\0'*4+struct.pack('<3fI3fI',1,2,3,0,4,5,6,0)
        self.doc={'accessors':[{'bufferView':0,'componentType':5126,'type':'VEC3','count':2}],
                  'bufferViews':[{'buffer':0,'byteOffset':4,'byteLength':32,'byteStride':16}]}

    def test_interleaved_accessor(self):
        self.assertEqual(accessor(self.doc,[self.raw],0),[(1,2,3),(4,5,6)])

    def test_truncated_declared_view(self):
        d=copy.deepcopy(self.doc);d['bufferViews'][0]['byteLength']=27
        with self.assertRaises(ValueError):accessor(d,[self.raw],0)

    def test_truncated_actual_buffer(self):
        with self.assertRaises(ValueError):accessor(self.doc,[self.raw[:-1]],0)

    def test_invalid_stride_alignment_and_offset(self):
        for stride in (0,8,13):
            d=copy.deepcopy(self.doc);d['bufferViews'][0]['byteStride']=stride
            with self.assertRaises(ValueError):accessor(d,[self.raw],0)
        d=copy.deepcopy(self.doc);d['accessors'][0]['byteOffset']=-4
        with self.assertRaises(ValueError):accessor(d,[self.raw],0)

    def test_nonfinite_and_unsupported(self):
        bad=self.raw[:4]+struct.pack('<f',float('nan'))+self.raw[8:]
        with self.assertRaises(ValueError):accessor(self.doc,[bad],0)
        for field in ('normalized','sparse'):
            d=copy.deepcopy(self.doc);d['accessors'][0][field]=True
            with self.assertRaises(ValueError):accessor(d,[self.raw],0)

    def test_exported_buffer_path_escape(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'m.gltf';p.write_text(json.dumps({'buffers':[{'uri':'../outside.bin','byteLength':4}]}))
            with self.assertRaises(ValueError):decode_mesh(p,Node('Resource'))

    def test_independent_dae_indexing_detects_winding(self):
        xml='''<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema"><library_geometries>
        <geometry name="mesh"><mesh><source id="pos"><float_array>0 0 0 1 0 0 0 1 0</float_array>
        <technique_common><accessor stride="3" /></technique_common></source>
        <vertices id="verts"><input semantic="POSITION" source="#pos" /></vertices>
        <triangles><input semantic="VERTEX" source="#pos" offset="0" /><p>0 1 2</p></triangles>
        </mesh></geometry></library_geometries></COLLADA>'''
        mesh={'name':'mesh','attributes':{'POSITION':[(0,0,0),(1,0,0),(0,1,0)]},'indices':[0,1,2]}
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'m.dae';p.write_text(xml)
            self.assertEqual(check_collada(p,[mesh])[0]['max_position_error'],0)
            mesh['indices']=[0,2,1]
            with self.assertRaises(ValueError):check_collada(p,[mesh])


if __name__=='__main__':unittest.main()
