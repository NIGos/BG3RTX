"""Native FS6650 terrain arithmetic after texture sampling (CPU reference).

Explicit sample/native-member inputs, never an upload/channel/LOD decoder. Python
binary64 models the formulas, not a bit-identical FP32/fma GPU certificate. Invalid
or undefined native domains are unsupported, not silently black/default material.
"""
import math


class UnsupportedTerrainArithmetic(ValueError):pass


def _finite(values):
    if not all(math.isfinite(v) for v in values):raise UnsupportedTerrainArithmetic('Nonfinite terrain arithmetic input')


def _clamp(x,lo=0.,hi=1.):return max(lo,min(x,hi))


def project_uv(grid_x,grid_z,local_height,normal_rgb):
    """FS _219.._298; local_height is native fma(height,scale,bias)."""
    _finite((grid_x,grid_z,local_height,*normal_rgb))
    if len(normal_rgb)!=3 or any(not 0<=v<=1 for v in normal_rgb):raise UnsupportedTerrainArithmetic('Expected sampled UNORM normal RGB')
    n=[2*v-1 for v in normal_rgb];length=math.sqrt(sum(v*v for v in n))
    if length==0:raise UnsupportedTerrainArithmetic('Undefined native normal normalization')
    n=[v/length for v in n];w=[max(abs(v)-0.5759999752044678,0.) for v in n];total=sum(w)
    if total==0:raise UnsupportedTerrainArithmetic('Undefined native projection weight normalization')
    w=[v/total for v in w]
    if max(w)!=1.:
        w[1]=0.009999999776482582;total=sum(w);w=[v/total for v in w];largest=max(w)
        w=[_clamp(v if v==largest else v-largest) for v in w];total=sum(w)
        if total==0:raise UnsupportedTerrainArithmetic('Undefined native rebalanced projection')
        w=[v/total for v in w]
    signed=[v if nn>=0 else -v for v,nn in zip(w,n)];sx,sy,sz=signed
    return dict(uv=(grid_x*(-sz)+grid_z*sx+grid_x*sy,-local_height*w[2]-local_height*w[0]-grid_z*w[1]),
                signed_orientation=sx+sy-sz,rotate_normal_xy=w[0]>0,projection_weights=tuple(w),normal=tuple(n))


def height_score(mask,albedo_alpha,contrast,scale,offset,threshold):
    """Native _341.._349. Parameters are actual members m5,m3,m4,m6.

    Authored similarly named brush fields are not automatically bound here.
    """
    _finite((mask,albedo_alpha,contrast,scale,offset,threshold))
    if not 0<=mask<=1 or not 0<=albedo_alpha<=1 or contrast<0:
        raise UnsupportedTerrainArithmetic('Unsupported mask/alpha/power domain')
    if mask<=0.0010000000474974513:return -1.
    h=0. if (albedo_alpha-1.)+mask<=threshold else abs(albedo_alpha)**contrast*mask
    h=h*scale+offset if h*scale>0.009999999776482582 else h*scale
    _finite((h,))
    return h if h>0.009999999776482582 else -1.


def layer_weights(scores,masks,blend_width):
    """Native _486.._546: four painted contributions plus remaining base."""
    if len(scores)!=4 or len(masks)!=4:raise UnsupportedTerrainArithmetic('Four painted layers required')
    _finite((*scores,*masks,blend_width))
    if any(not 0<=v<=1 for v in masks):raise UnsupportedTerrainArithmetic('Expected sampled mask RGBA')
    h=[max(v,0.) for v in scores];base=_clamp(1.-sum(h));width=max(blend_width,9.999999747378752e-6)
    highest=max(*h,base)
    numerators=[(max(0.,v-highest+width)+9.999999974752427e-7)*m for v,m in zip(h,masks)]
    denominator=sum(numerators)+base*width
    if not denominator>0 or not math.isfinite(denominator):raise UnsupportedTerrainArithmetic('Undefined native blend denominator')
    weights=tuple(v/denominator for v in numerators)
    return weights,_clamp(1.-sum(weights))


def base_curve_weights(base_weight,slope_pair,height_pair):
    """Native _1131.._1152 AFTER gather/interpolation. No LUT upload guess."""
    _finite((base_weight,*slope_pair,*height_pair))
    if len(slope_pair)!=2 or len(height_pair)!=2 or not 0<=base_weight<=1 or any(not 0<=v<=1 for v in (*slope_pair,*height_pair)):
        raise UnsupportedTerrainArithmetic('Expected normalized interpolated curve pairs')
    first=slope_pair[0]*height_pair[0];second=2*slope_pair[1]*height_pair[1]
    if first+second==0:first=1.
    return base_weight*first/(first+second),base_weight*second/(first+second)
