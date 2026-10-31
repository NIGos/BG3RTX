#pragma once
// Exact STI camera-depth FS781cd8d1d600117d / VS689836dece58e778.
// Native samples/vertex interpolation are supplied by the caller. No sampler,
// raster derivative or unknown SeeThrough policy is invented by this helper.
#ifdef __cplusplus
#include <cmath>
#include <cstdint>
namespace bg3::native_tileset_coverage {
using NtcUint=uint32_t;
inline bool ntc_finite(float x){return std::isfinite(x);}
#else
#define NtcUint uint
bool ntc_finite(float x){return isfinite(x);}
#endif
// AlphaRec TABLE header (not an individual record, not the sceneStamp):
// byte12 reserved for this bit. Reset at alpha_begin_capture, preserved through
// every scene/privateSun/query tuple copy; ordinary startup leaves it zero.
static const NtcUint NtcHeaderControlOffset=12u,NtcCaptureIncompleteBit=1u;
static const NtcUint NtcKnown=0u,NtcMissing=1u,NtcNonfinite=2u,NtcSeeThrough=3u;
#ifdef __cplusplus
inline constexpr uint64_t depth_vs=0x689836dece58e778ull,depth_fs=0x781cd8d1d600117dull;
struct Material {float tiling{},see_through{};bool known{};};
// The actual SAME-draw dynamic-offset UBO: set1/binding0,16 bytes. The
// Deferred UBO byte12 is Reflectance and must never be substituted here.
template<class Read> inline Material capture(Read&& read){
 float packet[4]{};Material m;
 if(read(1u,0u,0u,packet,sizeof(packet))){m.tiling=packet[0];m.see_through=packet[3];m.known=true;}
 return m;
}
#endif
struct NtcResult {NtcUint status;bool solid;};
inline NtcResult ntc_result(NtcUint status,bool solid){NtcResult r;r.status=status;r.solid=solid;return r;}
// Native SeeThrough branch is EXACTLY ==1, not nonzero. Other finite modes
// follow the native disabled path. Never interpret an unavailable scalar as0.
inline NtcUint ntc_material(bool known,float tiling,float see_through){
 if(!known)return NtcMissing;
 if(!ntc_finite(tiling)||!ntc_finite(see_through))return NtcNonfinite;
 return see_through==1.0f?NtcSeeThrough:NtcKnown;
}
inline NtcResult ntc_evaluate(NtcUint material_status,bool color_known,float green,
                             bool sample_known,float sampled_alpha){
 if(material_status!=NtcKnown)return ntc_result(material_status,false);
 if(!color_known)return ntc_result(NtcMissing,false);
 if(!ntc_finite(green))return ntc_result(NtcNonfinite,false);
 // Red and Base have literal alpha1 in native depth, not their albedo alpha.
 if(!(green>0.0f))return ntc_result(NtcKnown,true);
 if(!sample_known)return ntc_result(NtcMissing,false);
 if(!ntc_finite(sampled_alpha))return ntc_result(NtcNonfinite,false);
 return ntc_result(NtcKnown,sampled_alpha>=0.5f);
}
#ifdef __cplusplus
} // namespace bg3::native_tileset_coverage
#else
#undef NtcUint
#endif
