#pragma once
// Shared encoding arithmetic only. Primary matching still uses virtual depth.
// Float32 radial distance avoids the FP16 ratio and rounded (1+ratio) errors.
#ifdef __cplusplus
#include <cmath>
#include <atomic>
#include <cstring>
#include <cstdint>
namespace bg3::hair_origin {
inline constexpr const char* capability_key="bg3rtx.hair_physical_origin.capability.v1";
inline bool capability(uint32_t* state,bool requested) {
 if(!state)return false;
 std::atomic_ref<uint32_t> value(*state);uint32_t expected=0;
 value.compare_exchange_strong(expected,requested?2u:1u,std::memory_order_acq_rel);
 return value.load(std::memory_order_acquire)==2u;
}
inline bool binding_enabled(bool capability,unsigned mask,unsigned binding){return !(capability&&mask==8u&&binding==12u);}
inline bool alternate_entry(const char* file) {
 const char* names[]={"gi/rt_origin.hlsl","gi/rt_shadow.hlsl","gi/gi_trace_rt.hlsl","gi/gi_trace_rt_sharc.hlsl","gi/gi_trace_rt_hit_direct.hlsl","gi/gi_trace_rt_sharc_hit_direct.hlsl",
 "gi/rt_reflections.hlsl","gi/rt_reflections_sharc.hlsl","gi/rt_local_shadows.hlsl","gi/rt_local_shadows_full.hlsl",
 "gi/rt_local_shadows_box.hlsl","gi/rt_local_shadows_full_box.hlsl","gi/rt_local_shadows_native_full.hlsl",
 "gi/rt_local_shadows_native_full_box.hlsl","gi/rt_local_shadows_ref.hlsl","gi/rt_local_shadows_check.hlsl","gi/rt_local_shadows_full_check.hlsl"};
 for(const char* name:names)if(std::strcmp(name,file)==0)return true;return false;
}
struct OriginVec3 {float x,y,z;};
inline float hpo_sqrt(float x){return std::sqrt(x);}
inline float hpo_abs(float x){return std::abs(x);}
inline bool hpo_finite(float x){return std::isfinite(x);}
inline OriginVec3 hpo_vec(float x,float y,float z){return {x,y,z};}
#define HPO3 OriginVec3
#else
float hpo_sqrt(float x){return sqrt(x);}
float hpo_abs(float x){return abs(x);}
bool hpo_finite(float x){return isfinite(x);}
float3 hpo_vec(float x,float y,float z){return float3(x,y,z);}
#define HPO3 float3
#endif
inline HPO3 hpo_reconstruct(HPO3 raster,float physical_distance){
 float m=hpo_abs(raster.x);m=m>hpo_abs(raster.y)?m:hpo_abs(raster.y);m=m>hpo_abs(raster.z)?m:hpo_abs(raster.z);
 HPO3 v=hpo_vec(raster.x/m,raster.y/m,raster.z/m);
 float scale=physical_distance/hpo_sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
 return hpo_vec(v.x*scale,v.y*scale,v.z*scale);
}
inline bool hpo_unit_normal(HPO3 n){
 const float u=0.000000059604644775390625f,gamma16=16*u/(1-16*u);
 return hpo_abs((n.x*n.x+n.y*n.y+n.z*n.z)-1.0f)<=gamma16;
}
struct HairLightSegment {HPO3 direction;float tmax;bool valid;};
inline HairLightSegment hpo_light_segment(HPO3 receiver,HPO3 origin,HPO3 center,HPO3 sampled_direction,float radius,float clearance){
 HairLightSegment result;result.direction=hpo_vec(0,0,0);result.tmax=0;result.valid=false;
 if(!hpo_finite(radius)||!hpo_finite(clearance)||radius<0||clearance<0)return result;
 HPO3 target=center;
 if(radius>0){
  HPO3 delta=hpo_vec(center.x-receiver.x,center.y-receiver.y,center.z-receiver.z);
  float rr=radius*radius,dd=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
  if(dd<=rr){result.valid=true;return result;} // Receiver already inside emitter.
  float a=sampled_direction.x*sampled_direction.x+sampled_direction.y*sampled_direction.y+sampled_direction.z*sampled_direction.z;
  float b=delta.x*sampled_direction.x+delta.y*sampled_direction.y+delta.z*sampled_direction.z;
  float discriminant=b*b-a*(dd-rr);
  const float u=0.000000059604644775390625f,gamma32=32*u/(1-32*u);
  float error=gamma32*(b*b+a*(dd+rr));
  if(!(a>0)||!hpo_finite(discriminant)||discriminant < -error)return result;
  float t=(b-hpo_sqrt(discriminant>0?discriminant:0))/a;
  if(t<0)return result;
  target=hpo_vec(receiver.x+sampled_direction.x*t,receiver.y+sampled_direction.y*t,receiver.z+sampled_direction.z*t);
 }
 HPO3 segment=hpo_vec(target.x-origin.x,target.y-origin.y,target.z-origin.z);
 HPO3 original=hpo_vec(target.x-receiver.x,target.y-receiver.y,target.z-receiver.z);
 if(segment.x*original.x+segment.y*original.y+segment.z*original.z<=0){result.valid=true;return result;}
 float distance=hpo_sqrt(segment.x*segment.x+segment.y*segment.y+segment.z*segment.z);
 if(distance==0){result.valid=true;return result;}
 if(!(distance>0)||!hpo_finite(distance))return result;
 result.direction=hpo_vec(segment.x/distance,segment.y/distance,segment.z/distance);
 result.tmax=distance-(clearance>radius?clearance-radius:0);
 result.valid=hpo_finite(result.tmax);return result;
}
#undef HPO3
#ifdef __cplusplus
} // namespace bg3::hair_origin
#endif
