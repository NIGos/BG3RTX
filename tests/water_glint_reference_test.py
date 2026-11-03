"""M5.6 native footprint reference: reject underconverged fixed-tap candidates.

No production filter is qualified by these tests. The actual authored normal-map
fixture makes the bounded candidate rejection reproducible without game assets.
"""
import json
import math
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.low_priority import lower_priority
from tools.water_glint_reference import (add, dot, footprint_reference, half_encoding_bound,
                                        mul, native_sun_spec, norm)


def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])


class NativeFootprint(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tests/fixtures/water_glint_normal_patch.json').read_text())

    def normal_at(self,x,y):
        f=self.fixture
        x-=f['texel_origin'][0];y-=f['texel_origin'][1]
        ix,iy=math.floor(x),math.floor(y);fx,fy=x-ix,y-iy
        rows=f['rgba']
        self.assertGreaterEqual(ix,0);self.assertGreaterEqual(iy,0)
        self.assertLess(ix+1,len(rows[0]));self.assertLess(iy+1,len(rows))
        c=tuple((rows[iy][ix][k]*(1-fx)+rows[iy][ix+1][k]*fx)*(1-fy)+
                (rows[iy+1][ix][k]*(1-fx)+rows[iy+1][ix+1][k]*fx)*fy for k in range(4))
        return norm((2*c[3]-1,2*c[2]-1,-(2*c[1]-1)))

    def test_peak_rejects_underconverged_candidate(self):
        f=self.fixture;x,y=f['center'];width=f['footprint_width_texels']
        n=self.normal_at(x,y)
        tangent=norm(cross(n,(0,0,1)))
        v=norm(add(n,mul(tangent,.6)))
        light=norm(add(add(n,mul(tangent,-.6)),(0,0,.001)))
        nx=self.normal_at(x+width,y);ny=self.normal_at(x,y+width)
        variance=sum((a-b)**2 for a,b in zip(nx,n))+sum((a-b)**2 for a,b in zip(ny,n))
        rough=max(.09,math.sqrt(min(f['authored_roughness']**2+variance,.18)))
        self.assertEqual(rough,.09)  # Not a contradictory AA-free sharp fixture.
        field=lambda a,b:self.normal_at(x+width*(a-.5),y+width*(b-.5))
        values={k:footprint_reference(field,v,light,rough,k) for k in (4,8,16,32,64,128)}
        convergence=abs(values[128]-values[64])
        # Use even the coarser native B10G11R11 channel ULP, not a fitted % or
        # only the tighter separated direct/rest binary16 record precision.
        target_ulp=2**(math.floor(math.log2(values[128]))-5)
        bound=convergence+target_ulp+half_encoding_bound(values[128])
        self.assertAlmostEqual(values[128],.4216939853810021,places=10)
        for taps in (4,8,16,32):
            with self.subTest(taps=taps):
                self.assertGreater(abs(values[taps]-values[128]),bound)
        print(f'native footprint rejected 4/8/16/32 grids; 32-grid {values[32]:.10f}, '
              f'128-grid {values[128]:.10f}, error {abs(values[32]-values[128]):.10f}, '
              f'convergence+encoding bound {bound:.10f}',flush=True)

    def test_zero_unresolved_variance(self):
        n=norm((.1,1,.2));v=norm((.6,1,.1));light=norm((-.5,1,-.1))
        expected=native_sun_spec(n,v,light,.09)
        for grid in (1,2,4,8):
            self.assertEqual(footprint_reference(lambda x,y:n,v,light,.09,grid),expected)

    def test_normalized_footprint_preserves_spatial_energy(self):
        # A periodic field has no boundary flux. Every source term occurs once
        # under each translation, so the integral is preserved independently of
        # the native BRDF's highly nonlinear response to the normal slopes.
        side=8
        field=[[native_sun_spec(norm((.02*math.sin(x),1,.03*math.cos(y))),
                                 norm((.6,1,0)),norm((-.6,1,.001)),.09)
                for x in range(side)] for y in range(side)]
        baseline=math.fsum(v for row in field for v in row)
        filtered=math.fsum(math.fsum(field[(y+dy)%side][(x+dx)%side]
                                     for dx,dy in ((0,0),(1,0),(0,1),(1,1)))/4
                           for x in range(side) for y in range(side))
        bound=math.ulp(baseline)*side*side
        self.assertLessEqual(abs(filtered-baseline),bound)
        # Clouds and visibility are external fixed multipliers, not moments.
        self.assertLessEqual(abs(.37*filtered-.37*baseline),bound)


if __name__=='__main__':
    lower_priority()
    unittest.main()
