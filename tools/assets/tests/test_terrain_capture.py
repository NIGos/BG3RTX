"""CPU only. Tiny real R5 extract plus independent sampling/order oracles."""
import copy
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest

from tools.assets.terrain_material import (CapturedTerrainMaterial,decode_brush,
    linear_footprint,painted_weights,UnsupportedTerrainArithmetic,BRUSH_SCALARS)

FIXTURE=json.loads((Path(__file__).parent/'fixtures/terrain_r5_extract.json').read_text())


class TerrainCaptureTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name);self.manifest=copy.deepcopy(FIXTURE['manifest'])
        self.raw={
            's4b0':bytes.fromhex(FIXTURE['ubo_hex']),
            's1b4':bytes.fromhex(FIXTURE['brushes_hex'])+bytes(60*112),
            's1b2':bytearray(49*84*4),
            's1b3':struct.pack('<1024H',*[v for v in FIXTURE['lut_constant_rows'] for _ in range(256)])}
        e=FIXTURE['mask_extract'];x,y=e['origin']
        for row in range(2):self.raw['s1b2'][((y+row)*49+x)*4:((y+row)*49+x+2)*4]=bytes(e['rgba'][row*8:row*8+8])
        self.write()

    def write(self):
        (self.path/'manifest.json').write_text(json.dumps(self.manifest))
        for r in self.manifest['packets'][0]['resources']:
            (self.path/r['file']).write_bytes(self.raw[r['slot']])

    def test_actual_record_scalars_and_colors_match_authored_without_texture_guess(self):
        c=CapturedTerrainMaterial(self.path)
        self.assertEqual(c.base_indices,(0,255))
        for i,a in enumerate(FIXTURE['authored']):
            b=c.brushes[i]
            self.assertEqual([b[k] for k in BRUSH_SCALARS],[a['brush'][k] for k in BRUSH_SCALARS])
            self.assertEqual(list(b['bottom_color'][:3]),a['brush']['GradientBottomHeightColor'])
            self.assertEqual(list(b['mid_color'][:3]),a['brush']['GradientMidHeightColor'])
        self.assertEqual(c.brushes[0]['texture_indices'],(208,209,210,-1))
        self.assertEqual(c.brushes[3]['texture_indices'],(383,384,385,24))

    def test_real_extracted_neighborhood_linear_sample_and_connected_weights(self):
        c=CapturedTerrainMaterial(self.path);grid=(8.75,2.75)
        # Independent four-corner weighted sum in native pixel space.
        tx=grid[0]*c.uniform[2]*49-.5-8;ty=grid[1]*c.uniform[3]*84-.5-2
        p=FIXTURE['mask_extract']['rgba']
        expected=[sum(p[(j*2+i)*4+k]/255*((tx if i else 1-tx)*(ty if j else 1-ty))
                      for j in range(2) for i in range(2)) for k in range(4)]
        for x,y in zip(c.sample_mask(*grid),expected):self.assertAlmostEqual(x,y,places=15)
        r=c.evaluate(*grid,(1,1,1,1))
        self.assertGreater(r['painted_weights'][1],0)
        self.assertGreater(r['painted_weights'][2],0)
        self.assertAlmostEqual(sum(r['painted_weights'])+r['base_weight'],1)
        # Actual 255 bypasses LUT, including absent normal/height inputs.
        self.assertEqual(r['base_weights'],(r['base_weight'],0))
        with self.assertRaises(UnsupportedTerrainArithmetic):c.evaluate(*grid,(1,1,math.nan,1))
        with self.assertRaises(UnsupportedTerrainArithmetic):painted_weights([0]*4,[1]*3,c.brushes,.2)

    def test_linear_edges_and_gather_corner_order(self):
        for uv,expected in [((.5,.5),[(0,1),(1,1),(1,0),(0,0)]),
                            ((-99,-99),[(0,0)]*4),((99,99),[(1,1)]*4)]:
            self.assertEqual(linear_footprint(uv,2,2)[0],expected)
        # Sample every center of a nonconstant synthetic mask, independently.
        self.raw['s1b2']=bytes(v for y in range(84) for x in range(49) for v in (x,y,x+y,255))
        self.write();c=CapturedTerrainMaterial(self.path)
        for y in (0,1,40,83):
            for x in (0,1,20,48):
                m=c.sample_mask((x+.5)/(49*c.uniform[2]),(y+.5)/(84*c.uniform[3]))
                for a,b in zip(m,(x/255,y/255,(x+y)/255,1)):self.assertAlmostEqual(a,b,places=13)

    def test_active_lut_native_gather_rows_and_factor_two(self):
        c=CapturedTerrainMaterial(self.path);c.base_indices=(0,1)
        # Actual R5 LUT contains alternating constant 1/0 rows; base1 inactive
        # in R5, but this exercises the same-data active branch separately.
        self.assertEqual(c.base_weights(.6,.3,.7),(.6,0))
        with self.assertRaises(UnsupportedTerrainArithmetic):c.base_weights(.6)
        # Nonconstant independent LUT: distinct horizontal ramp on every row.
        c.lut=tuple((i+1)*(j+1)*31 for j in range(4) for i in range(256))
        normal,height=.4,.6;pairs=[]
        for u,lower in ((normal,0),(height,2)):
            f=u*256-.5;i=math.floor(f);t=f-i
            first=((i+1)*(lower+1)*31*(1-t)+(i+2)*(lower+1)*31*t)/65535
            second=((i+1)*(lower+2)*31*(1-t)+(i+2)*(lower+2)*31*t)/65535
            pairs.append((first,second))
        a=pairs[0][0]*pairs[1][0];b=2*pairs[0][1]*pairs[1][1]
        actual=c.base_weights(.6,normal,height)
        self.assertAlmostEqual(actual[0],.6*a/(a+b),places=15)
        self.assertAlmostEqual(actual[1],.6*b/(a+b),places=15)

    def test_reject_unknown_sampler_view_extent_and_traversal(self):
        original=copy.deepcopy(self.manifest)
        def reject(change):
            self.manifest=copy.deepcopy(original);change(self.manifest['packets'][0]);self.write()
            with self.assertRaises(UnsupportedTerrainArithmetic):CapturedTerrainMaterial(self.path)
        reject(lambda p:p['samplers'][0].update(known=False))
        reject(lambda p:p['samplers'][0].update(address=[0,0,0]))
        reject(lambda p:p['samplers'][0].update(anisotropy=1))
        reject(lambda p:p['resources'][2]['view'].update(components=[3,0,0,0]))
        reject(lambda p:p['resources'][2]['view'].update(image_mips=2))
        reject(lambda p:p.update(status='declined'))
        reject(lambda p:p['resources'][2].update(row_pitch=200))
        reject(lambda p:p['resources'][0].update(file='../outside.bin'))
        with self.assertRaises(UnsupportedTerrainArithmetic):decode_brush(bytes(111))

    def test_authored_exact_channel_join_and_mismatch_rejection(self):
        c=CapturedTerrainMaterial(self.path)
        authored={'brushes':[dict(id=a['id'],brush=dict(a['brush'],Name=a['name']),overrides=a['overrides'])
                             for a in FIXTURE['authored']],'patches':[{},{}]}
        e=self.path/'authored.json';e.write_text(json.dumps(authored))
        for i in range(4):
            plane=bytes(c.raw['s1b2'][i::4])
            for z,data in enumerate((plane[:3136],plane[3136:])):
                (self.path/f'patch{z}-layer{i}.bin').write_bytes(data)
        rows=c.resolve_authored(e)
        self.assertTrue(all(r['texture_identity']=='unresolved' for r in rows))
        plane=self.path/'patch1-layer2.bin';plane.write_bytes(bytes([255])+plane.read_bytes()[1:])
        with self.assertRaises(UnsupportedTerrainArithmetic):c.resolve_authored(e)


if __name__=='__main__':unittest.main()
