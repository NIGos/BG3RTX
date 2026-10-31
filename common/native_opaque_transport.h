#ifndef BG3_NATIVE_OPAQUE_TRANSPORT_H
#define BG3_NATIVE_OPAQUE_TRANSPORT_H
// Native class0 PARAMETER mapping, not a native BSDF or a TbMaterial adapter.
// No lobe weights are invented. GGX slope width is roughness squared.
// Exact native provenance and the remaining consumer gap: tests/native_opaque_transport_sources.md.
#include "transport_bsdf.h"
#ifdef __cplusplus
#include "native_vt_surface.h"
namespace bg3::transport {
using NtVec3=TbVec3;using NtUint=uint32_t;
inline float nt_fma(float x,float y,float z){return std::fma(x,y,z);}
#else
#define NtVec3 float3
#define NtUint uint
float nt_fma(float x,float y,float z){return mad(x,y,z);}
#endif
static const NtUint NtOk=0,NtUnsupportedClass=1,NtUnknownSamples=2,NtUnknownLinearColor=3,
    NtUnknownOpaqueCoverage=4,NtUnsupportedLobes=5,NtInvalid=6,NtUnknownFootprint=7,NtUnsupportedDecode=8;
static const NtUint NtSamplesKnown=1,NtLinearColorKnown=2,NtOpaqueCoverageKnown=4,
    NtTransmission=8,NtAnisotropy=16,NtExtraLobes=32,NtLobesKnown=64;
struct NtInput {NtVec3 linear_albedo;float roughness,reflectance,metalness;NtUint material_class,evidence;};
struct NtParameters {
    NtVec3 linear_albedo,diffuse_color,F0;
    float reflectance,metalness,perceptual_roughness,base_roughness,base_alpha;
};
struct NtResult {NtParameters value;NtUint status;};
struct NtRoughness {float native_roughness,alpha;NtUint status;};
inline NtResult nt_empty(NtUint status){
    NtResult r;r.value.linear_albedo=tb_vec(0,0,0);r.value.diffuse_color=tb_vec(0,0,0);r.value.F0=tb_vec(0,0,0);
    r.value.reflectance=0;r.value.metalness=0;
    r.value.perceptual_roughness=0;r.value.base_roughness=0;r.value.base_alpha=0;r.status=status;return r;
}
inline NtResult nt_class0_parameters(NtInput in_data){
    if(in_data.material_class!=0)return nt_empty(NtUnsupportedClass);
    if((in_data.evidence&(NtTransmission|NtAnisotropy|NtExtraLobes))!=0 ||
       (in_data.evidence&NtLobesKnown)==0 || (in_data.evidence&~127u)!=0)return nt_empty(NtUnsupportedLobes);
    if((in_data.evidence&NtSamplesKnown)==0)return nt_empty(NtUnknownSamples);
    if((in_data.evidence&NtLinearColorKnown)==0)return nt_empty(NtUnknownLinearColor);
    if((in_data.evidence&NtOpaqueCoverageKnown)==0)return nt_empty(NtUnknownOpaqueCoverage);
    if(!tb_color(in_data.linear_albedo)||!tb_finite(in_data.roughness)||in_data.roughness<0||in_data.roughness>1||
       !tb_finite(in_data.reflectance)||in_data.reflectance<0||in_data.reflectance>1||
       !tb_finite(in_data.metalness)||in_data.metalness<0||in_data.metalness>1)return nt_empty(NtInvalid);
    NtResult r=nt_empty(NtOk);float m=in_data.metalness,nonmetal=1-m;
    r.value.linear_albedo=in_data.linear_albedo;r.value.reflectance=in_data.reflectance;r.value.metalness=m;
    float f0=0.16f*(in_data.reflectance*in_data.reflectance);
    r.value.diffuse_color=tb_vec(in_data.linear_albedo.x*nonmetal,in_data.linear_albedo.y*nonmetal,in_data.linear_albedo.z*nonmetal);
    // Native GLSL mix: these are parameter coefficients, NOT convex lobe weights.
    r.value.F0=tb_vec(f0*nonmetal+in_data.linear_albedo.x*m,f0*nonmetal+in_data.linear_albedo.y*m,f0*nonmetal+in_data.linear_albedo.z*m);
    r.value.perceptual_roughness=in_data.roughness;
    r.value.base_roughness=tb_max(0.09f,in_data.roughness);
    r.value.base_alpha=r.value.base_roughness*r.value.base_roughness;
    return r;
}
// Native screen-quad AA is optional, separately qualified. Offscreen ray cones
// do not supply these derivatives. Unknown footprint is never silently zero AA.
inline NtRoughness nt_native_quad_roughness(NtResult parameters,NtVec3 dh,NtVec3 dv,bool footprint_known){
    NtRoughness out_data;out_data.native_roughness=0;out_data.alpha=0;out_data.status=parameters.status;
    if(parameters.status!=NtOk)return out_data;
    if(!footprint_known){out_data.status=NtUnknownFootprint;return out_data;}
    if(!tb_finite3(dh)||!tb_finite3(dv)){out_data.status=NtInvalid;return out_data;}
    float variance=tb_dot(dh,dh)+tb_dot(dv,dv);
    if(!tb_finite(variance)){out_data.status=NtInvalid;return out_data;}
    float r=parameters.value.base_roughness;
    float a2=nt_fma(r,r,tb_min(variance,0.18f));
    out_data.native_roughness=tb_sqrt(tb_min(1,tb_max(0,a2)));
    // Preserve the native sqrt then square sequence; do not rename perceptual r alpha.
    out_data.alpha=out_data.native_roughness*out_data.native_roughness;
    return out_data;
}
#ifdef __cplusplus
// Decode success does not establish coverage or actual sampled-view provenance.
// Both remain explicit caller evidence, including for an AlphaTest_VT asset.
inline NtResult nt_from_native_surface(native_vt::Status decoded,const native_vt::Surface& s,
                                      NtUint material_class,NtUint evidence){
    if(decoded!=native_vt::Status::SupportedFields)return nt_empty(NtUnsupportedDecode);
    NtInput input{{s.albedo[0],s.albedo[1],s.albedo[2]},s.perceptual_roughness,s.reflectance,s.metalness,material_class,evidence};
    return nt_class0_parameters(input);
}
} // namespace bg3::transport
#else
#undef NtVec3
#undef NtUint
#endif
#endif
