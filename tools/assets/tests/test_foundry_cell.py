import unittest
import numpy as np
from tools.assets.probe_foundry_cell import candidate_pose,finite_triangle_slice,native_window_matches,select_archive_terrain
from tools.assets.affine import UnsupportedAffine


class FoundryCellTests(unittest.TestCase):
    def test_explicit_rotation_encodings_and_local_x_stretch(self):
        flags={'ConstructionBend':False,'ConstructionPlaceTwoTiles':False}
        common={'Translate':[3,4,5],'Scale':.5,'ScaleZ':2.,'Flip':True}
        r=dict(common,Rotate=[-1,0,0,0,-1,0,0,0,1])
        q=dict(common,QRotate=[0,0,1,0])
        self.assertEqual(candidate_pose(r,flags),candidate_pose(q,flags))
        self.assertEqual(candidate_pose(q,flags).point((2,1,-3)),(1,3.5,3.5))
        for bad in [dict(r,QRotate=[0,0,1,0]),dict(common),dict(r,Point1='linked'),dict(r,ScaleZ=float('nan'))]:
            with self.assertRaises(UnsupportedAffine):candidate_pose(bad,flags)
        with self.assertRaises(UnsupportedAffine):candidate_pose(r,dict(flags,ConstructionBend=True))

    def test_ordered_native_window_inside_merged_draw(self):
        from scripts.geo_view import DRAW
        world=np.array([[-9,0,0],[-8,0,0],[-9,1,0],[1,2,3],[2,2,3],[1,3,3]],dtype=float)
        draw=np.zeros(1,dtype=DRAW);draw['count']=6;draw['n']=6;draw['inst']=1
        ubo=np.zeros(64);m=np.zeros((4,4));m[0,0]=m[1,1]=m[2,3]=1;ubo[32:48]=m.ravel()
        native={'world':world,'clip':np.ones((6,4)),'draws':draw,'ubo':ubo}
        got=native_window_matches(world[3:].copy(),np.zeros((3,3)),native)
        self.assertEqual([(r['draw'],r['first_vertex'],r['corners']) for r in got],[(0,3,3)])
        bad=world[3:].copy();bad[2,1]+=.1
        self.assertEqual(native_window_matches(bad,np.zeros((3,3)),native),[])
        native['clip'][4,2]=float('nan')
        self.assertEqual(native_window_matches(world[3:],np.zeros((3,3)),native),[])

    def test_archive_crop_keeps_source_topology_and_native_identity(self):
        mesh=dict(attributes=dict(POSITION=[[9,0,9],[10,0,9],[9,0,10],[0,2,0],[1,2,0],[0,2,1]],TERRAIN_GRID_HALF=[[i,i] for i in range(6)]),
                  indices=[0,1,2,5,3,4],source_cell_indices=[9,21],source_fan_indices=[1,3])
        evidence=dict(native_primitive_mapping=[dict(source_cell=9,source_fan=1,native_primitive=101),dict(source_cell=21,source_fan=3,native_primitive=47)])
        cropped=select_archive_terrain(mesh,evidence,np.array([[0,0],[1,1]]))
        self.assertEqual(cropped['indices'],[2,0,1])
        self.assertEqual(cropped['source_vertex_ids'],[3,4,5])
        self.assertEqual(cropped['source_triangle_ids'],[1])
        self.assertEqual(cropped['native_triangle_ids'],[47])
        self.assertEqual(cropped['attributes']['TERRAIN_GRID_HALF'],[[3,3],[4,4],[5,5]])
        self.assertEqual(cropped['bounds'],[[0.,2.,0.],[1.,2.,1.]])
        with self.assertRaises(ValueError):select_archive_terrain(mesh,evidence,np.array([[30,30],[31,31]]))

    def test_native_terrain_slice_preserves_full_finite_triangles(self):
        tri=np.array([[[0,0,0],[1,0,0],[0,0,1]],
                      [[9,0,9],[10,0,9],[9,0,10]],
                      [[-1,0,0],[1,0,0],[0,0,2]],
                      [[0,0,0],[1,float('nan'),0],[0,0,1]]],dtype=float)
        before=tri.copy();indices=finite_triangle_slice(tri,np.array([[0,0],[1,1]]))
        self.assertEqual(indices.tolist(),[0,2])
        self.assertTrue(np.array_equal(tri,before,equal_nan=True))
        self.assertEqual(tri[indices].shape,(2,3,3))
        # Existing unprojection ignores clip Z; a Z-only NaN must still cull.
        clip=np.ones((4,3,4));clip[2,1,2]=float('nan')
        self.assertEqual(finite_triangle_slice(tri,np.array([[0,0],[1,1]]),clip).tolist(),[0])



if __name__=='__main__':unittest.main()
