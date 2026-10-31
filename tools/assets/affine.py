"""Small full-affine primitive for decoded static assets, not a scene provider.

Column vectors: p' = linear * p + translation. Composition preserves shear.
Native TRS inputs are float32 position, quaternion (xyzw), scalar/vec3 scale.
"""
from dataclasses import dataclass
import math
import struct


class UnsupportedAffine(ValueError):pass


def dot(a,b):return math.fsum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def unit(v):
    n=math.sqrt(dot(v,v))
    if not n or not math.isfinite(n):raise UnsupportedAffine('Degenerate/nonfinite vector')
    return tuple(x/n for x in v)


def f32_ulp(x):
    """Spacing of the native float32 encoding, including subnormals."""
    if not math.isfinite(x):raise UnsupportedAffine('Nonfinite native field')
    bits=struct.unpack('<I',struct.pack('<f',abs(x)))[0]
    exponent=(bits>>23)&255
    if exponent==255:raise UnsupportedAffine('Native float32 overflow')
    return math.ldexp(1.,-149 if exponent==0 else exponent-150)


def vec(value,count):
    if len(value)!=count or not all(math.isfinite(x) for x in value):
        raise UnsupportedAffine('Invalid native vector')
    return tuple(map(float,value))


def quaternion_precision(q):
    radii=[0.5*f32_ulp(x) for x in q]
    serialization=math.fsum(2*abs(x)*e+e*e for x,e in zip(q,radii))
    # Declared 12-op FP32 normalization convention: 4 squares, 3 adds, sqrt,
    # 4 divisions. This bounds permitted input drift, not an engine algorithm proof.
    u=2.**-24;g=12*u/(1-12*u)
    normalized=2*g+g*g
    return dict(norm_squared=dot(q,q),serialization_bound=serialization,
                normalization_bound=normalized,total_bound=serialization+normalized+8*math.ulp(1.))


@dataclass(frozen=True)
class Affine:
    linear:tuple=((1.,0.,0.),(0.,1.,0.),(0.,0.,1.))
    translation:tuple=(0.,0.,0.)

    @classmethod
    def from_node(cls,node,normalize_quaternion=True):
        if node is None:return cls()
        p=vec(node.get('Position',(0,0,0)),3)
        q=vec(node.get('RotationQuat',(0,0,0,1)),4)
        s=node.get('Scale',1.)
        s=vec(s,3) if isinstance(s,(list,tuple)) else (float(s),)*3
        if not all(math.isfinite(v) and v!=0 for v in s):raise UnsupportedAffine('Zero/nonfinite scale')
        report=quaternion_precision(q)
        if abs(report['norm_squared']-1)>report['total_bound']:raise UnsupportedAffine('Quaternion outside declared encoding/normalization envelope')
        x,y,z,w=unit(q) if normalize_quaternion else q
        r=((1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)),
           (2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)),
           (2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)))
        return cls(tuple(tuple(row[j]*s[j] for j in range(3)) for row in r),p)

    def point(self,p):return tuple(dot(r,p)+t for r,t in zip(self.linear,self.translation))
    def vector(self,p):return tuple(dot(r,p) for r in self.linear)

    def compose(self,child):
        columns=list(zip(*child.linear))
        return Affine(tuple(tuple(dot(r,c) for c in columns) for r in self.linear),self.point(child.translation))

    def determinant(self):return dot(self.linear[0],cross(self.linear[1],self.linear[2]))

    def normal(self,n):
        r=self.linear;c=(cross(r[1],r[2]),cross(r[2],r[0]),cross(r[0],r[1]))
        d=dot(r[0],c[0])
        # Six determinant products; reject unresolved cancellation, not a tuned scale.
        terms=[r[0][i]*r[1][j]*r[2][k] for i,j,k in ((0,1,2),(1,2,0),(2,0,1),(0,2,1),(1,0,2),(2,1,0))]
        u=2.**-53;error=(8*u/(1-8*u))*sum(map(abs,terms))
        if not math.isfinite(d) or abs(d)<=error:raise UnsupportedAffine('Singular/numerically unresolved normal transform')
        return unit(tuple(dot(row,n)/d for row in c))

    def tangent_frame(self,n,tangent):
        n=self.normal(n);t=self.vector(tangent[:3]);projection=dot(t,n)
        t=unit(tuple(x-projection*y for x,y in zip(t,n)))
        if tangent[3] not in (-1,1):raise UnsupportedAffine('Unknown tangent handedness')
        return n,(*t,tangent[3]*(-1 if self.determinant()<0 else 1))

    def fp32_application_bound(self,p):
        """Coefficient F32 rounding + separate 4-term FP32 dot (gamma7).

        Bound for the declared stored-affine evaluation, not proof of the game's
        transform implementation or runtime placement/state correspondence.
        """
        u=2.**-24;g=7*u/(1-7*u);out=[]
        for row,t in zip(self.linear,self.translation):
            rounding=sum(abs(x)*0.5*f32_ulp(a) for a,x in zip(row,p))+0.5*f32_ulp(t)
            magnitude=sum(abs(a*x) for a,x in zip(row,p))+abs(t)+rounding
            out.append(rounding+g*magnitude)
        return tuple(out)
