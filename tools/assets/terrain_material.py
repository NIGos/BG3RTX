"""Native FS6650 terrain blend arithmetic and captured mask/LUT CPU reference.

Actual schema-1 packets supply mask/LUT samples; material alpha remains explicit. Python
binary64 models the formulas, not a bit-identical FP32/fma GPU certificate. Invalid
or undefined native domains are unsupported, not silently black/default material.
"""
import math
import json
import struct
from pathlib import Path


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
    if albedo_alpha==0 and contrast==0 and (albedo_alpha-1.)+mask>threshold:
        raise UnsupportedTerrainArithmetic('Undefined native pow(0,0) domain')
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


BRUSH_SCALARS = ('HeightScale','HeightOffset','HeightContrast','AlphaThreshold',
                 'Tiling','DetailNormalTiling','DetailNormalScale','GradientHeightBottom',
                 'GradientHeightMidPoint','GradientHeightTop','GradientRoughnessReach','GradientRoughnessPower')


def decode_brush(raw):
    """Actual native std430 record. Texture indices remain unresolved native IDs."""
    if len(raw)!=112:raise UnsupportedTerrainArithmetic('Expected 112-byte brush record')
    f=struct.unpack('<24f4I',raw)
    _finite(f[:24])
    return dict(texture_indices=f[:4],bottom_color=f[4:8],mid_color=f[8:12],
                **dict(zip(BRUSH_SCALARS,f[12:24])),flags=f[24],tail=f[25:])


def painted_weights(masks,albedo_alphas,brushes,blend_width):
    """Connect real mask samples/brush fields to the existing native arithmetic.

    BMA alphas must be supplied at the native textureGrad footprint; no guessed
    texture-ID mapping or constant-alpha fallback is allowed.
    """
    if len(masks)!=4 or len(albedo_alphas)!=4 or len(brushes)!=4:
        raise UnsupportedTerrainArithmetic('Four masks, explicit BMA alphas and brush records required')
    scores=[height_score(m,a,b['HeightContrast'],b['HeightScale'],b['HeightOffset'],b['AlphaThreshold'])
            for m,a,b in zip(masks,albedo_alphas,brushes)]
    weights,base=layer_weights(scores,masks,blend_width)
    return dict(scores=scores,painted_weights=weights,base_weight=base)


def linear_footprint(uv,width,height):
    """Normalized clamp-edge footprint at sole mip; ideal real-valued filtering.

    GPU subtexel quantization/FP32 rounding are not modeled by this CPU reference.
    Gather order is GLSL/Vulkan (i0j1,i1j1,i1j0,i0j0).
    """
    if len(uv)!=2 or width<=0 or height<=0:raise UnsupportedTerrainArithmetic('Invalid texture footprint')
    _finite(uv)
    # Clamp before integer conversion also bounds work for huge finite inputs.
    x=_clamp(uv[0])*width-.5;y=_clamp(uv[1])*height-.5
    i,j=math.floor(x),math.floor(y)
    clampx=lambda v:max(0,min(width-1,v));clampy=lambda v:max(0,min(height-1,v))
    return [(clampx(i),clampy(j+1)),(clampx(i+1),clampy(j+1)),
            (clampx(i+1),clampy(j)),(clampx(i),clampy(j))],(x-i,y-j)


class CapturedTerrainMaterial:
    """FS6650 actual packet consumer, not a complete terrain/BSDF renderer."""
    def __init__(self,directory,packet_id=None):
        directory=Path(directory)
        manifest=json.loads((directory/'manifest.json').read_text())
        packets=[p for p in manifest['packets'] if packet_id is None or p['id']==packet_id]
        if manifest.get('schema')!=1 or len(packets)!=1:
            raise UnsupportedTerrainArithmetic('Select exactly one schema-1 terrain packet')
        p=packets[0]
        if p['status']!='copied' or p['fs']!='6650c75f3d3e4968' or p['vs']!='64e2568916ede3d6':
            raise UnsupportedTerrainArithmetic('Missing copied native FS/VS packet')
        samplers=[s for s in p['samplers'] if s['set']==4 and s['binding']==4]
        if len(samplers)!=1:raise UnsupportedTerrainArithmetic('Missing native mask/LUT sampler')
        s=samplers[0]
        if not (s['known'] and s['status']==1 and s['mag']==s['min']==1 and
                s['address']==[2,2,2] and s['anisotropy']==s['compare']==s['unnormalized']==s['reduction']==0 and
                s['min_lod']==0 and s['max_lod']>=0):
            raise UnsupportedTerrainArithmetic('Unsupported native mask/LUT sampler')
        self.resources={r['slot']:r for r in p['resources']};self.raw={}
        for slot in ('s4b0','s1b4','s1b2','s1b3'):
            r=self.resources[slot];name=r['file']
            if Path(name).name!=name or '/' in name or '\\' in name:
                raise UnsupportedTerrainArithmetic('Packet file must be local basename')
            limit={'s4b0':80,'s1b4':7168,'s1b2':16464,'s1b3':2048}[slot]
            if r['bytes']!=limit or (directory/name).stat().st_size!=limit:
                raise UnsupportedTerrainArithmetic('Unsupported packet extent/byte count')
            self.raw[slot]=(directory/name).read_bytes()
        for slot,fmt,w,h in (('s1b2',37,49,84),('s1b3',70,256,4)):
            r=self.resources[slot];v=r['view'];vr=v['range']
            if not (r['width']==w and r['height']==h and r['row_pitch']==w*(4 if fmt==37 else 2) and
                    v['status']=='known' and v['format']==v['image_format']==fmt and v['type']==1 and
                    v['components']==[0,0,0,0] and v['flags']==v['image_flags']==0 and
                    v['extension_first'] is None and v['image_id']==v['current_image_id'] and
                    vr['base_mip']==vr['base_layer']==0 and vr['resolved_mips']==vr['resolved_layers']==1 and
                    v['image_mips']==v['image_layers']==1):
                raise UnsupportedTerrainArithmetic('Unsupported native image view/mip profile')
        self.uniform=struct.unpack('<16f2I2f',self.raw['s4b0'])
        _finite((*self.uniform[:16],*self.uniform[18:]))
        self.brushes=[decode_brush(self.raw['s1b4'][i:i+112]) for i in range(0,448,112)]
        self.base_indices=self.uniform[16:18]
        self.lut=struct.unpack('<1024H',self.raw['s1b3'])
        self.packet=dict(frame=manifest['frame'],id=p['id'],recording=p['recording'],draw=p['draw'])

    def sample_mask(self,grid_x,grid_z):
        uv=(grid_x*self.uniform[2],grid_z*self.uniform[3]) # FS _304, NOT m0.xy
        coords,(a,b)=linear_footprint(uv,49,84)
        def texel(x,y,c):return self.raw['s1b2'][(y*49+x)*4+c]/255.
        result=[]
        for c in range(4):
            x,y,z,w=[texel(i,j,c) for i,j in coords]
            result.append((w*(1-a)+z*a)*(1-b)+(x*(1-a)+y*a)*b)
        return tuple(result)

    def base_weights(self,base_weight,normal_y=None,native_height=None):
        if self.base_indices[0]>=64:
            raise UnsupportedTerrainArithmetic('Native missing-base checker material is not evaluated')
        if self.base_indices[1]>=64:return (base_weight,0.)
        if normal_y is None or native_height is None:
            raise UnsupportedTerrainArithmetic('Active LUT needs native normalized normal.y and _3.y')
        _finite((normal_y,native_height))
        if not -1<=normal_y<=1:raise UnsupportedTerrainArithmetic('Expected normalized normal.y')
        pairs=[]
        for u,v in ((_clamp(normal_y),.25),(native_height,.75)):
            coords,_=linear_footprint((u,v),256,4)
            x,y,z,w=[self.lut[j*256+i]/65535. for i,j in coords]
            # Native manually interpolates horizontal gather pairs, NOT bilinear
            # filtering at v=.25/.75. Fraction is from original unclamped u.
            t=(u*256-.5)%1
            pairs.append((w*(1-t)+z*t,x*(1-t)+y*t))
        return base_curve_weights(base_weight,*pairs)

    def evaluate(self,grid_x,grid_z,albedo_alphas,normal_y=None,native_height=None):
        masks=self.sample_mask(grid_x,grid_z)
        r=painted_weights(masks,albedo_alphas,self.brushes,self.uniform[18])
        r.update(mask=masks,base_weights=self.base_weights(r['base_weight'],normal_y,native_height),
                 packet=self.packet,status='blend_only_explicit_bma_alpha',
                 remaining=['BMA texture identity and native textureGrad derivatives',
                            'sampled normal RGB/projection, material texture samples and final G-buffer/BSDF'])
        return r

    def resolve_authored(self,evidence_path):
        """Require exact captured scalar/color and complete mask-plane matches.

        This resolves brush/channel order, NOT global texture-array IDs to DDS.
        """
        path=Path(evidence_path);e=json.loads(path.read_text());resolved=[]
        if len(e['brushes'])!=4 or len(e['patches'])!=2:
            raise UnsupportedTerrainArithmetic('Expected four brushes/two authored patches')
        for i,(native,authored) in enumerate(zip(self.brushes,e['brushes'])):
            b=authored['brush'];override=authored['overrides']
            if override['ParamOverridesMask']!=0:
                raise UnsupportedTerrainArithmetic('Authored override needs explicit native mapping')
            colors=(tuple(b['GradientBottomHeightColor']),tuple(b['GradientMidHeightColor']))
            scalar_match=all(native[k]==b[k] for k in BRUSH_SCALARS)
            color_match=(native['bottom_color'][:3],native['mid_color'][:3])==colors
            planes=b''.join((path.parent/f'patch{z}-layer{i}.bin').read_bytes() for z in (0,1))
            mask_match=planes==self.raw['s1b2'][i::4]
            if not (scalar_match and color_match and mask_match):
                raise UnsupportedTerrainArithmetic('Authored brush/channel association not exact')
            resolved.append(dict(channel=i,id=authored['id'],name=b['Name'],mask_bytes=len(planes),
                                 scalar_color_exact=True,mask_bytes_exact=True,
                                 native_texture_indices=native['texture_indices'],texture_identity='unresolved'))
        return resolved


def main():
    import argparse
    ap=argparse.ArgumentParser(description='Evaluate captured FS6650 blend inputs; explicit BMA alphas required')
    ap.add_argument('--packet',type=Path,required=True);ap.add_argument('--packet-id',type=int)
    ap.add_argument('--grid',type=float,nargs=2,required=True,metavar=('X','Z'))
    ap.add_argument('--bma-alpha',type=float,nargs=4,required=True)
    ap.add_argument('--normal-y',type=float);ap.add_argument('--native-height',type=float)
    ap.add_argument('--authored-evidence',type=Path)
    args=ap.parse_args();capture=CapturedTerrainMaterial(args.packet,args.packet_id)
    result=capture.evaluate(*args.grid,args.bma_alpha,args.normal_y,args.native_height)
    if args.authored_evidence:result['authored_channels']=capture.resolve_authored(args.authored_evidence)
    print(json.dumps(result,indent=2,allow_nan=False))


if __name__=='__main__':main()
