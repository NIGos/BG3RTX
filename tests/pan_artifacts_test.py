"""Synthetic temporal oracle: flash, persistent shadow, disocclusion, reprojection."""
import unittest
import numpy as np
from scripts.qa.pan_artifacts import transient, reproject, camera


class PanTests(unittest.TestCase):
    def test_short_flashes_and_persistent_shadow(self):
        for duration in (1,2,3):
            a = np.ones((16,3,4),dtype='f4')
            a[5:5+duration,1,1] = .1
            a[4:10,2,2] = .1
            mask, count = transient(a,np.ones_like(a,dtype=bool),.2)
            self.assertEqual(count,1)
            self.assertEqual(int(mask.sum()),duration)
            self.assertFalse(mask[:,2,2].any())

    def test_disocclusion_and_no_recovery_are_not_flash(self):
        a = np.ones((16,2,2),dtype='f4')
        a[5,0,0] = 0
        a[12:,1,1] = 0
        good = np.ones_like(a,dtype=bool); good[4,0,0] = False
        mask,count = transient(a,good,.1)
        self.assertEqual(count,0)
        self.assertFalse(mask.any())

    def test_camera_header_exact_bits(self):
        values = np.arange(48,dtype='<f4')/7
        raw = np.frombuffer(values.tobytes(),dtype='u1').reshape(48,4)
        p = np.zeros((2,50,4),dtype='f4'); p[0,:48] = raw
        _,scale,bias,origin = camera(p)
        np.testing.assert_array_equal(scale,values[12:14])
        np.testing.assert_array_equal(origin,values[16:19])

    def test_reprojection_translation(self):
        c = (np.eye(3),np.array([.5,.5]),np.zeros(2),np.zeros(3))
        d = np.ones((8,10))
        iy,ix,z,valid,motion = reproject(d,c,c)
        y,x = np.indices(d.shape)
        np.testing.assert_array_equal(ix,x)
        np.testing.assert_array_equal(iy,y)
        np.testing.assert_allclose(motion,0,atol=1e-12)
        moved = (*c[:3],np.array([0,.1,0]))
        _,ix,_,_,_ = reproject(d,c,moved)
        np.testing.assert_array_equal(ix[:,1:],x[:,1:]-1)


if __name__ == '__main__':
    unittest.main()
