import unittest
import numpy as np
from tools.assets.check_floor_b_positions import projection_bound


class ProjectionEnvelopeTests(unittest.TestCase):
    def test_independent_separate_fp32_dot_and_inverse_rounding(self):
        m=np.array([[1.31,.27,.01,.4],[-.37,.91,.2,-.18],[.23,.51,1.04,.83],[.3,-.2,.7,0]],dtype=np.float32)
        ubo=np.zeros(64,dtype=np.float32);ubo[32:48]=m.ravel()
        a=m[:3,[0,1,3]].astype(np.float64).T;t=m[3,[0,1,3]].astype(np.float64)
        original=np.array([[0,0,0],[1,-2,3],[-100,37,.0001],[1.e-10,-1.e-9,1.e-8],[79,-91,32]],dtype=np.float32)
        clips=[]
        # Independent shader evaluator: four separately rounded products, three adds.
        for p in original:
            row=[]
            for j in (0,1,3):
                products=[np.float32(np.float32(x)*m[k,j]) for k,x in enumerate((*p,np.float32(1))) ]
                v=products[0]
                for x in products[1:]:v=np.float32(v+x)
                row.append(float(v))
            clips.append(row)
        decoded=((np.asarray(clips)-t)@np.linalg.inv(a).T).astype(np.float32).astype(np.float64)
        errors=np.abs(decoded-original.astype(np.float64));envelope=projection_bound(decoded,ubo)
        self.assertTrue(np.all(errors<=envelope))
        self.assertTrue(np.isfinite(envelope).all())
        self.assertTrue(np.all(envelope>=0))

    def test_singular_camera_is_unsupported(self):
        with self.assertRaises(np.linalg.LinAlgError):projection_bound(np.ones((1,3)),np.zeros(64))


if __name__=='__main__':unittest.main()
