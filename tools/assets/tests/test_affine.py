import math
import struct
import unittest
from tools.assets.affine import Affine,UnsupportedAffine,cross,dot,f32_ulp,quaternion_precision
from tools.assets.place_foundry_mesh import decimal_point,transform_mesh


def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]


class AffineTests(unittest.TestCase):
    def test_nonuniform_scale_is_not_averaged(self):
        a=Affine.from_node({'Position':[10,20,30],'Scale':[2,3,4]})
        self.assertEqual(a.point((1,2,3)),(12,26,42))

    def test_nested_rotation_scale_retains_shear(self):
        parent={'Scale':[2,1,1],'Position':[3,4,5]}
        child={'RotationQuat':[0,0,f32(math.sin(math.pi/8)),f32(math.cos(math.pi/8))],'Scale':[1,3,1]}
        a=Affine.from_node(parent).compose(Affine.from_node(child))
        cols=list(zip(*a.linear))
        self.assertGreater(abs(dot(cols[0],cols[1])),1) # Cannot collapse back to TRS.
        for p in ((0,0,0),(1,2,3),(-7,.5,4)):
            actual=a.point(p);reference=decimal_point(p,[parent,child])
            for x,y in zip(actual,reference):self.assertLessEqual(abs(x-y),64*math.ulp(max(1.,abs(y))))

    def test_normal_inverse_transpose(self):
        a=Affine.from_node({'Scale':[2,3,4]})
        n=a.normal((1,1,0));edge=a.vector((1,-1,0))
        self.assertAlmostEqual(dot(n,edge),0,places=14)
        self.assertAlmostEqual(n[0]/n[1],1.5,places=14)

    def test_nonsymmetric_shear_normal(self):
        a=Affine(((2,1,0),(0,3,0),(0,0,4)))
        n=a.normal((1,0,0))
        self.assertAlmostEqual(dot(n,a.vector((0,1,0))),0,places=14)
        self.assertAlmostEqual(n[1]/n[0],-1/3,places=14)

    def test_mirrored_triangle_winding_and_tangent_hand(self):
        m={'attributes':{'POSITION':[(0,0,0),(1,0,0),(0,1,0)],'NORMAL':[(0,0,1)]*3,
            'TANGENT':[(1,0,0,1)]*3,'TEXCOORD_0':[(0,0),(1,0),(0,1)]},'indices':[0,1,2]}
        w=transform_mesh(m,Affine.from_node({'Scale':[-2,3,4]}))
        self.assertEqual(w['indices'],[0,2,1]);self.assertEqual(m['indices'],[0,1,2])
        p=[w['attributes']['POSITION'][i] for i in w['indices']]
        geometric=cross(tuple(x-y for x,y in zip(p[1],p[0])),tuple(x-y for x,y in zip(p[2],p[0])))
        self.assertGreater(dot(geometric,w['attributes']['NORMAL'][0]),0)
        self.assertEqual(w['attributes']['TANGENT'][0],(-1,0,0,-1))
        self.assertEqual(w['attributes']['TEXCOORD_0'],m['attributes']['TEXCOORD_0'])

    def test_reject_invalid_and_singular(self):
        for n in ({'Scale':[1,0,1]},{'Scale':float('inf')},{'RotationQuat':[0,0,0,2]},
                  {'Position':[0,float('nan'),0]}):
            with self.assertRaises(UnsupportedAffine):Affine.from_node(n)
        with self.assertRaises(UnsupportedAffine):Affine(((0,0,0),(0,1,0),(0,0,1))).normal((0,0,1))

    def test_native_f32_spacing_and_application_bound(self):
        self.assertEqual(f32_ulp(1),2.**-23);self.assertEqual(f32_ulp(0),2.**-149)
        a=Affine.from_node({'Position':[-242.89285278320312,6.5762481689453125,-184.18487548828125],
            'RotationQuat':[0,-.70710688829422,0,.7071067094802856]})
        for p in ((41.57826232910156,.29837656021118164,88.80303955078125),(1,2,3)):
            exact=a.point(p);bound=a.fp32_application_bound(p)
            # Explicit independent FP32 dot: coefficient casts, then rounded mul/add.
            native=[]
            for row,t in zip(a.linear,a.translation):
                value=f32(t)
                for x,c in zip(p,row):value=f32(value+f32(f32(c)*f32(x)))
                native.append(value)
            for x,y,e in zip(exact,native,bound):self.assertLessEqual(abs(x-y),e)

    def test_deformed_attributes_are_explicitly_unsupported(self):
        with self.assertRaises(UnsupportedAffine):transform_mesh({'attributes':{'WEIGHTS_0':[]}},Affine())

    def test_actual_board_retains_raw_and_normalized_conventions(self):
        native={'Position':[41.57826232910156,.29837656021118164,88.80303955078125],
                'RotationQuat':[0,.26764363050460815,0,.9635180830955505],'Scale':1}
        report=quaternion_precision(native['RotationQuat'])
        self.assertGreater(abs(report['norm_squared']-1),report['serialization_bound'])
        self.assertLess(abs(report['norm_squared']-1),report['total_bound'])
        normalized=Affine.from_node(native);raw=Affine.from_node(native,normalize_quaternion=False)
        self.assertNotEqual(normalized.point((1,2,3)),raw.point((1,2,3)))
        self.assertEqual(raw.translation,normalized.translation)


if __name__=='__main__':unittest.main()
