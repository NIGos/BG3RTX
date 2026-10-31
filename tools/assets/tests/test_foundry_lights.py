import unittest
import tempfile
from pathlib import Path
from tools.assets.probe_foundry_lights import sphere_box_candidate,historical_rows

class FoundryLightTests(unittest.TestCase):
    def test_actual_support_sphere_box_boundary_not_center_radius(self):
        box=[[0,0,0],[1,1,1]]
        self.assertEqual(sphere_box_candidate([2,.5,.5],1,box),(True,1))
        self.assertEqual(sphere_box_candidate([2,.5,.5],.5,box),(False,1))
        self.assertEqual(sphere_box_candidate([.5,.5,.5],0,box),(True,0))
        self.assertEqual(sphere_box_candidate([2,3,3],3,box),(True,3))
        for p,r,b in [([0,0,0],-1,box),([float('nan'),0,0],1,box),([0,0,0],1,[[1,0,0],[0,1,1]])]:
            with self.assertRaises(ValueError):sphere_box_candidate(p,r,b)

if __name__=='__main__':unittest.main()

class HistoricalLightTests(unittest.TestCase):
    def test_decimal_rounding_position_join_does_not_merge_duplicate_names(self):
        lights=[dict(resolved=dict(name='same',map_key=key),full_affine=dict(linear=((1,0,0),(0,1,0),(0,0,1)),translation=p)) for key,p in [('a',(1.,2.,3.)),('b',(1.,2.,4.))]]
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'old.csv';path.write_text('db_name,x,y,z\nsame,1.0000,2.0000,3.0000\nsame,1.0000,2.0000,8.0000\n')
            rows=historical_rows([path],lights)
        self.assertEqual(rows[0]['position_and_name_candidates'],['a'])
        self.assertEqual(rows[1]['position_and_name_candidates'],[])
