import math
import unittest
from tools.assets.terrain_material import project_uv,height_score,layer_weights,base_curve_weights,UnsupportedTerrainArithmetic


class TerrainMaterialTests(unittest.TestCase):
    def test_axis_projection_and_sign_are_native_not_world_xz(self):
        expected=[((.5,1,.5),(3,-7),1,False),((1,.5,.5),(7,-2),1,True),((.5,.5,1),(-3,-2),-1,False),((.5,.5,0),(3,-2),1,False)]
        for normal,uv,orientation,rotate in expected:
            result=project_uv(3,7,2,normal)
            self.assertEqual(result['uv'],uv);self.assertEqual(result['signed_orientation'],orientation);self.assertEqual(result['rotate_normal_xy'],rotate)
        with self.assertRaises(UnsupportedTerrainArithmetic):project_uv(0,0,0,(.5,.5,.5))
        with self.assertRaises(UnsupportedTerrainArithmetic):project_uv(math.inf,0,0,(.5,1,.5))

    def test_score_real_brush_values_and_strict_branches(self):
        # Authored SharTemple parameters used explicitly, not an upload proof.
        self.assertEqual(height_score(1,.5,1,5,5,0),7.5)
        self.assertEqual(height_score(.25,.5,1,5,5,0),-1)
        self.assertEqual(height_score(.25,.9,1,5,5,0),6.125)
        self.assertEqual(height_score(.0010000000474974513,1,1,5,5,0),-1)
        # Offset must not revive a contribution below the native scale test.
        self.assertEqual(height_score(1,.5,1,0,100,0),-1)
        with self.assertRaises(UnsupportedTerrainArithmetic):height_score(1,.5,-1,5,0,0)
        with self.assertRaises(UnsupportedTerrainArithmetic):height_score(1,1,1,1e308,1e308,0)

    def test_blend_competition_and_base_not_raw_normalization(self):
        weights,base=layer_weights([-1]*4,[0]*4,.1)
        self.assertEqual((weights,base),((0,0,0,0),1))
        weights,base=layer_weights([7.5,6.125,-1,-1],[1,.25,0,0],.1)
        self.assertEqual(base,0)
        self.assertGreater(weights[0],.99999)
        self.assertGreater(weights[1],0) # native epsilon remains until later branch
        self.assertAlmostEqual(sum(weights),1)
        # Equal positive heights have equal contributions even with unequal sum.
        weights,base=layer_weights([1,1,0,0],[1,1,0,0],.2)
        self.assertEqual(weights,(.5,.5,0,0));self.assertEqual(base,0)
        with self.assertRaises(UnsupportedTerrainArithmetic):layer_weights([0]*4,[float('nan')]*4,.1)

    def test_base_curves_keep_native_factor_two_and_zero_rule(self):
        for actual,expected in zip(base_curve_weights(.6,(1,1),(1,1)),(.2,.4)):
            self.assertLessEqual(abs(actual-expected),math.ulp(expected))
        self.assertEqual(base_curve_weights(.6,(0,0),(0,0)),(.6,0))
        self.assertEqual(base_curve_weights(0,(1,1),(1,1)),(0,0))
        with self.assertRaises(UnsupportedTerrainArithmetic):base_curve_weights(1,(-1,0),(0,1))


if __name__=='__main__':unittest.main()
