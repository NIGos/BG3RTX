import math
import struct
import unittest
from tools.assets.terrain_patch import (read_foundry_render_patch,assemble_foundry_heightfield,
    heightfield_mesh,TerrainPatchError,FAN_PATTERNS,read_foundry_layer_inputs,bc7_dds)


def patch(z,holes=()):
    rows=65 if z==0 else 21
    header=b'PVersion'+struct.pack('<12I',8,72,50,rows,49,rows-1,50,85,0,z,len(holes),4)+b'\xff\xff\xff\xff'*8
    heights=[x*.125+(r+64*z)*.25 for r in range(rows) for x in range(50)]
    return header+struct.pack('<'+str(len(heights))+'f',*heights)+struct.pack('<'+str(len(holes))+'I',*holes)+b'opaque material tail'


class TerrainPatchTests(unittest.TestCase):
    def test_two_render_patches_global_holes_and_exact_seam(self):
        a=read_foundry_render_patch(patch(0,(0,)));b=read_foundry_render_patch(patch(1,(64*50+3,)))
        self.assertEqual(b.hole_indices,(3203,));self.assertEqual(a.material_tail,b'opaque material tail')
        field=assemble_foundry_heightfield([b,a]);self.assertEqual(field['heights'][64*50+7],16.875)
        m=heightfield_mesh(field);self.assertEqual(m['triangles'],4*(49*84-2));self.assertEqual(m['vertices'],4250+49*84-2)
        self.assertNotIn(0,m['source_cell_indices']);self.assertNotIn(3203,m['source_cell_indices'])
        self.assertEqual(tuple(m['source_fan_indices'][:4]),(0,1,2,3))
        # Independent geometric winding: each fan faces positive local Y.
        for n in range(0,12,3):
            p,q,r=[m['attributes']['POSITION'][i] for i in m['indices'][n:n+3]]
            cross_y=(q[2]-p[2])*(r[0]-p[0])-(q[0]-p[0])*(r[2]-p[2])
            self.assertGreater(cross_y,0)
        self.assertEqual(len(FAN_PATTERNS),4)

    def test_missing_truncated_wrong_format_and_nonfinite_rejected(self):
        good=patch(0)
        for bad in [b'SEBD'+good[4:],good[:87],good[:100],good[:8]+struct.pack('<I',9)+good[12:]]:
            with self.assertRaises(TerrainPatchError):read_foundry_render_patch(bad)
        bad=bytearray(good);struct.pack_into('<f',bad,88,float('nan'))
        with self.assertRaises(TerrainPatchError):read_foundry_render_patch(bad)
        with self.assertRaises(TerrainPatchError):assemble_foundry_heightfield([read_foundry_render_patch(good)])

    def test_layer_identity_planes_and_exact_block_extent(self):
        from dataclasses import replace
        ids=['00000000-0000-0000-0000-00000000000'+str(i) for i in range(4)]
        p=read_foundry_render_patch(patch(1));count=49*20
        tail=b''.join(struct.pack('<I',36)+u.encode()+struct.pack('<I',count)+bytes([i])*count for i,u in enumerate(ids))
        blocks=bytes(13*6*16);tail+=struct.pack('<I',len(blocks))+blocks
        p=replace(p,material_tail=tail);decoded=read_foundry_layer_inputs(p,ids)
        self.assertEqual(decoded['layers'][2]['cell_bytes'],bytes([2])*980)
        self.assertEqual((decoded['image_width'],decoded['image_height']),(52,24))
        self.assertEqual(len(bc7_dds(blocks,52,24)),148+1248)
        for bad in [tail[:-1],tail+b'x',b'wrong'+tail[5:]]:
            with self.assertRaises(TerrainPatchError):read_foundry_layer_inputs(replace(p,material_tail=bad),ids)
        with self.assertRaises(TerrainPatchError):read_foundry_layer_inputs(p,list(reversed(ids)))
        with self.assertRaises(TerrainPatchError):bc7_dds(blocks,4097,24)

    def test_invalid_global_holes_and_seam_never_repaired(self):
        for z,holes in [(1,(0,)),(0,(49,)),(0,(1,1))]:
            with self.assertRaises(TerrainPatchError):read_foundry_render_patch(patch(z,holes))
        bad=bytearray(patch(1));struct.pack_into('<f',bad,88,99)
        with self.assertRaises(TerrainPatchError):assemble_foundry_heightfield([read_foundry_render_patch(patch(0)),read_foundry_render_patch(bad)])


if __name__=='__main__':unittest.main()
