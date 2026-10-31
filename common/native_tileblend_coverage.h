#pragma once
#include "native_tileset_coverage.h"
// Native STI camera FS0f64d3a9dd86ff98. Samples are supplied with the native
// sampler; this is post-sample arithmetic, not a raster-derivative certificate.
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace bg3::native_tileset_coverage {
struct BlendMaterial {float packet[8]{};bool known{};};
template<class Read> inline BlendMaterial capture_blend(Read&& read){
 BlendMaterial m;m.known=read(1u,0u,0u,m.packet,sizeof(m.packet));return m;
}
inline NtcUint ntb_material(const BlendMaterial& m){
 if(!m.known)return NtcMissing;
 for(float x:m.packet)if(!ntc_finite(x))return NtcNonfinite;
 if(m.packet[7]==1.f)return NtcSeeThrough;
 return m.packet[5]>m.packet[6]?NtcMissing:NtcKnown;
}
inline float ntb_pow(float x,float y){return std::pow(x,y);}
inline float ntb_max(float x,float y){return std::max(x,y);}
inline float ntb_min(float x,float y){return std::min(x,y);}
inline float ntb_abs(float x){return std::abs(x);}
inline float ntb_mix(float a,float b,float t){return a*(1.f-t)+b*t;}
#else
#define NtcUint uint
float ntb_pow(float x,float y){return pow(x,y);}
float ntb_max(float x,float y){return max(x,y);}
float ntb_min(float x,float y){return min(x,y);}
float ntb_abs(float x){return abs(x);}
float ntb_mix(float a,float b,float t){return lerp(a,b,t);}
#endif
inline NtcResult ntb_evaluate(NtcUint status,bool samples_known,float base_alpha,float mask_r,float mask_g,
                              float bias,float exponent,float lower,float upper){
 if(status!=NtcKnown)return ntc_result(status,false);
 if(!samples_known)return ntc_result(NtcMissing,false);
 if(!ntc_finite(base_alpha)||!ntc_finite(mask_r)||!ntc_finite(mask_g)||!ntc_finite(bias)||
    !ntc_finite(exponent)||!ntc_finite(lower)||!ntc_finite(upper))return ntc_result(NtcNonfinite,false);
 if(lower>upper)return ntc_result(NtcMissing,false); // undefined native clamp domain
 float shifted=mask_g+bias;
 float weight=ntb_pow(ntb_max(ntb_abs(shifted),0.0001f),exponent);
 if(!ntc_finite(shifted)||!ntc_finite(weight))return ntc_result(NtcNonfinite,false);
 weight=ntb_min(ntb_max(weight,lower),upper);
 float alpha=ntb_mix(1.f,base_alpha,weight)*(1.f-mask_r);
 if(!ntc_finite(alpha))return ntc_result(NtcNonfinite,false);
 return ntc_result(NtcKnown,alpha>=0.5f);
}
#ifdef __cplusplus
} // namespace bg3::native_tileset_coverage
#else
#undef NtcUint
#endif
