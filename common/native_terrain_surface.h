#ifndef BG3_NATIVE_TERRAIN_SURFACE_H
#define BG3_NATIVE_TERRAIN_SURFACE_H
// Native FS6650c75f3d3e4968 post-sample arithmetic. No texture lookup, sampler,
// derivative, upload/channel binding or native-material/BSDF conversion proof.
// Source/evidence and exact pending packets: tests/native_terrain_surface_sources.md.
#ifdef __cplusplus
#include <cmath>
#include <cstdint>
#include <cstddef>
namespace bg3::native_terrain {
using NttUint=uint32_t;
inline bool ntt_finite(float x) {return std::isfinite(x);}
inline float ntt_sqrt(float x) {return std::sqrt(x);}
inline float ntt_pow(float x,float y) {return std::pow(x,y);}
inline float ntt_fma(float a,float b,float c) {return std::fma(a,b,c);}
#else
#define NttUint uint
bool ntt_finite(float x) {return isfinite(x);}
float ntt_sqrt(float x) {return sqrt(x);}
float ntt_pow(float x,float y) {return pow(x,y);}
float ntt_fma(float a,float b,float c) {return mad(a,b,c);}
#endif
struct Ntt2 {float x,y;};
struct Ntt3 {float x,y,z;};
struct Ntt4 {float x,y,z,w;};
inline Ntt2 ntt_v2(float x,float y) {Ntt2 v;v.x=x;v.y=y;return v;}
inline Ntt3 ntt_v3(float x,float y,float z) {Ntt3 v;v.x=x;v.y=y;v.z=z;return v;}
inline Ntt4 ntt_v4(float x,float y,float z,float w) {Ntt4 v;v.x=x;v.y=y;v.z=z;v.w=w;return v;}
static const NttUint NttOk=0,NttInvalid=1,NttUndefined=2,NttUnknownInputs=3;
// Explicit native std140/std430 field order. These are data contracts, NOT a
// decoder/certificate that authored LSF values occupy these native fields.
struct NttTerrainPacket {Ntt4 m0,m1,m2;Ntt2 m3,m4;NttUint base0,base1;float blend_width,m7;};
struct NttBrushPacket {Ntt4 texture_indices,m1,m2;float height_scale,height_offset,height_contrast,alpha_threshold;
    float tiling,detail_tiling,detail_scale,gradient_bottom,gradient_midpoint,gradient_top,gradient_reach,gradient_power;
    NttUint flags,m16,m17,m18;};
#ifdef __cplusplus
static_assert(sizeof(NttTerrainPacket)==80 && offsetof(NttTerrainPacket,base0)==64 && offsetof(NttTerrainPacket,blend_width)==72);
static_assert(sizeof(NttBrushPacket)==112 && offsetof(NttBrushPacket,height_scale)==48 && offsetof(NttBrushPacket,flags)==96);
#endif
struct NttInputEvidence {NttUint packet_bytes,mask_mapping,samplers,derivatives;};
inline NttUint ntt_input_status(NttInputEvidence e) {
    return e.packet_bytes==1 && e.mask_mapping==1 && e.samplers==1 && e.derivatives==1 ? NttOk:NttUnknownInputs;
} // Caller facts only; no fixture is permitted to certify missing native inputs.
inline float ntt_max(float a,float b) {return a>b?a:b;}
inline float ntt_min(float a,float b) {return a<b?a:b;}
inline float ntt_abs(float x) {return x<0?-x:x;}
inline float ntt_sat(float x) {return ntt_min(1,ntt_max(0,x));}
inline bool ntt_unit(float x) {return ntt_finite(x)&&x>=0&&x<=1;}
inline bool ntt_finite3(Ntt3 a) {return ntt_finite(a.x)&&ntt_finite(a.y)&&ntt_finite(a.z);}
inline bool ntt_finite4(Ntt4 a) {return ntt_finite(a.x)&&ntt_finite(a.y)&&ntt_finite(a.z)&&ntt_finite(a.w);}
inline bool ntt_unit4(Ntt4 a) {return ntt_unit(a.x)&&ntt_unit(a.y)&&ntt_unit(a.z)&&ntt_unit(a.w);}
struct NttProjection {Ntt2 uv;Ntt3 normal,weights;float orientation;NttUint rotate_xy,status;};
inline NttProjection ntt_project(float x,float z,float local_height,Ntt3 rgb) {
    NttProjection r;r.uv=ntt_v2(0,0);r.normal=ntt_v3(0,0,0);r.weights=r.normal;r.orientation=0;r.rotate_xy=0;r.status=NttInvalid;
    if(!ntt_finite(x)||!ntt_finite(z)||!ntt_finite(local_height)||!ntt_unit(rgb.x)||!ntt_unit(rgb.y)||!ntt_unit(rgb.z)) return r;
    Ntt3 n=ntt_v3(2*rgb.x-1,2*rgb.y-1,2*rgb.z-1);
    float length=ntt_sqrt(n.x*n.x+n.y*n.y+n.z*n.z);r.status=NttUndefined;if(!(length>0)) return r;
    n=ntt_v3(n.x/length,n.y/length,n.z/length);
    Ntt3 w=ntt_v3(ntt_max(ntt_abs(n.x)-0.5759999752044678f,0),ntt_max(ntt_abs(n.y)-0.5759999752044678f,0),ntt_max(ntt_abs(n.z)-0.5759999752044678f,0));
    float sum=w.x+w.y+w.z;if(!(sum>0)) return r;w=ntt_v3(w.x/sum,w.y/sum,w.z/sum);
    if(ntt_max(w.x,ntt_max(w.y,w.z))!=1) {
        w.y=0.009999999776482582f;sum=w.x+w.y+w.z;w=ntt_v3(w.x/sum,w.y/sum,w.z/sum);
        float largest=ntt_max(w.x,ntt_max(w.y,w.z));
        w=ntt_v3(ntt_sat(w.x+(w.x==largest?0:-largest)),ntt_sat(w.y+(w.y==largest?0:-largest)),ntt_sat(w.z+(w.z==largest?0:-largest)));
        sum=w.x+w.y+w.z;if(!(sum>0)) return r;w=ntt_v3(w.x/sum,w.y/sum,w.z/sum);
    }
    Ntt3 s=ntt_v3(n.x>=0?w.x:-w.x,n.y>=0?w.y:-w.y,n.z>=0?w.z:-w.z);
    r.uv=ntt_v2(ntt_fma(x,-s.z,ntt_fma(z,s.x,x*s.y)),ntt_fma(-local_height,w.z,ntt_fma(-local_height,w.x,-z*w.y)));
    if(!ntt_finite(r.uv.x)||!ntt_finite(r.uv.y)) return r;
    r.normal=n;r.weights=w;r.orientation=(s.x+s.y)-s.z;r.rotate_xy=w.x>0?1:0;r.status=NttOk;return r;
}
struct NttScore {float value;NttUint status;};
inline NttScore ntt_height_score(float mask,float alpha,float contrast,float scale,float offset,float threshold) {
    NttScore r;r.value=0;r.status=NttInvalid;
    if(!ntt_unit(mask)||!ntt_unit(alpha)||!ntt_finite(contrast)||contrast<0||!ntt_finite(scale)||!ntt_finite(offset)||!ntt_finite(threshold)) return r;
    r.value=-1;r.status=NttOk;if(mask<=0.0010000000474974513f) return r;
    // GLSL pow(0,0) has an undefined domain. Do not silently choose a CPU value.
    if(alpha==0&&contrast==0&&((alpha-1)+mask)>threshold) {r.status=NttUndefined;return r;}
    float h=((alpha-1)+mask)<=threshold?0:ntt_pow(ntt_abs(alpha),contrast)*mask;
    float scaled=h*scale;float score=scaled>0.009999999776482582f?ntt_fma(h,scale,offset):scaled;
    if(!ntt_finite(score)) {r.status=NttUndefined;return r;}
    r.value=score>0.009999999776482582f?score:-1;return r;
}
struct NttBlend {Ntt4 painted;float base;NttUint status;};
inline NttBlend ntt_blend(Ntt4 scores,Ntt4 mask,float blend_width) {
    NttBlend r;r.painted=ntt_v4(0,0,0,0);r.base=0;r.status=NttInvalid;
    if(!ntt_finite4(scores)||!ntt_unit4(mask)||!ntt_finite(blend_width)) return r;
    Ntt4 h=ntt_v4(ntt_max(scores.x,0),ntt_max(scores.y,0),ntt_max(scores.z,0),ntt_max(scores.w,0));
    float base=ntt_sat(1-(((h.x+h.y)+h.z)+h.w)),width=ntt_max(blend_width,9.999999747378752e-6f);
    float highest=ntt_max(ntt_max(ntt_max(ntt_max(h.x,h.y),h.z),h.w),base);
    Ntt4 a=ntt_v4((ntt_max(0,(h.x-highest)+width)+9.999999974752427e-7f)*mask.x,
                 (ntt_max(0,(h.y-highest)+width)+9.999999974752427e-7f)*mask.y,
                 (ntt_max(0,(h.z-highest)+width)+9.999999974752427e-7f)*mask.z,
                 (ntt_max(0,(h.w-highest)+width)+9.999999974752427e-7f)*mask.w);
    float denominator=ntt_fma(base,width,((a.x+a.y)+a.z)+a.w);r.status=NttUndefined;
    if(!(denominator>0)||!ntt_finite(denominator)) return r;
    r.painted=ntt_v4(a.x/denominator,a.y/denominator,a.z/denominator,a.w/denominator);
    r.base=ntt_sat((((1-r.painted.x)-r.painted.y)-r.painted.z)-r.painted.w);r.status=NttOk;return r;
}
struct NttBaseBlend {Ntt2 weights;NttUint status;};
inline NttBaseBlend ntt_base_curves(float base_weight,Ntt2 slope,Ntt2 height) {
    NttBaseBlend r;r.weights=ntt_v2(0,0);r.status=NttInvalid;
    if(!ntt_unit(base_weight)||!ntt_unit(slope.x)||!ntt_unit(slope.y)||!ntt_unit(height.x)||!ntt_unit(height.y)) return r;
    float first=slope.x*height.x,pair=slope.y*height.y,second=2*pair;
    if(ntt_fma(slope.x,height.x,second)==0) first=1;
    float denominator=ntt_fma(pair,2,first);
    if(!(denominator>0)||!ntt_finite(denominator)) {r.status=NttUndefined;return r;}
    // Native forms the common scale first, then multiplies both numerators.
    float scale=base_weight/denominator;r.weights=ntt_v2(first*scale,second*scale);r.status=NttOk;return r;
}
#ifdef __cplusplus
} // namespace bg3::native_terrain
#else
#undef NttUint
#endif
#endif
