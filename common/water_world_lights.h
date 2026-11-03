#pragma once
// Shared CPU/HLSL bounded world query. 32 = one subgroup's worth of visibility
// rays per hit, versus the existing 1024-entry froxel bound (32x smaller).
#include "native_hit_direct.h"
#ifdef __cplusplus
namespace bg3::transport {
using WlVec3=HdVec3;using WlUint=HdUint;
#else
typedef float3 WlVec3;typedef uint WlUint;
#endif
static const WlUint WlK=32u;
struct WlSelection { WlUint ids[32]; float distances[32]; WlUint count, candidates; };
inline WlSelection wl_empty(){WlSelection s;s.count=0;s.candidates=0;return s;}
// Range is an authored bounding sphere. Strict boundary matches hd_native_incident.
inline bool wl_reaches(WlVec3 p,WlVec3 center,float inverse_radius,WlUint channels,WlUint channel){
 if(channel>31u || !tb_finite3(p) || !tb_finite3(center) ||
    !tb_finite(inverse_radius) || !(inverse_radius>0) || (channels&(1u<<channel))==0u)return false;
 WlVec3 d=tb_vec(p.x-center.x,p.y-center.y,p.z-center.z);
 float q=tb_dot(d,d), ir2=inverse_radius*inverse_radius;
 return tb_finite(q) && tb_finite(ir2) && ir2>0 && q<1/ir2;
}
// The same incident model, but reject spot cones before pow/falloff.
inline HdIncident wl_incident(HdLight light,bool spot,WlVec3 surface_position,WlUint channel,float global_scale){
 HdIncident r;r.radiance=r.direction=tb_vec(0,0,0);r.distance=0;r.status=HdInvalid;
 if(channel>31 || !tb_finite3(surface_position) || !tb_finite3(light.position) || !tb_finite3(light.color) ||
    light.color.x<0 || light.color.y<0 || light.color.z<0 || !tb_finite(light.inverse_radius) || !(light.inverse_radius>0) ||
    !tb_finite(light.global_selector) || !tb_finite(light.falloff_exponent) || !tb_finite(light.falloff_bias))return r;
 if(light.global_selector<0 && (!tb_finite(global_scale) || global_scale<0))return r;
 if((light.channels&(1u<<channel))==0){r.status=HdOk;return r;}
 WlVec3 delta=tb_vec(light.position.x-surface_position.x,light.position.y-surface_position.y,light.position.z-surface_position.z);
 float d2=tb_dot(delta,delta),ir2=light.inverse_radius*light.inverse_radius;
 if(!tb_finite(d2) || !tb_finite(ir2) || !(ir2>0) || !(d2>=1.0000000e-10f))return r;
 r.distance=tb_sqrt(d2);r.direction=hd_mul(delta,1/r.distance);
 r.status=HdOk;if(!(d2<1/ir2))return r;
 float t=0;
 if(spot){
  TbDirection axis=tb_direction(light.direction);
  if(axis.status!=TbOk || !tb_finite(light.cos_outer) || !tb_finite(light.cos_inner) ||
     light.cos_outer< -1 || light.cos_inner>1 || light.cos_inner<light.cos_outer){r.status=HdInvalid;return r;}
  float c=-tb_dot(r.direction,axis.v);if(!(c>light.cos_outer))return r;
  t=tb_min(1,tb_max(0,(c-light.cos_outer)/tb_max(light.cos_inner-light.cos_outer,9.9999997e-05f)));
 }
 // Exact authored falloff in bg3_lights.hlsli/f469db78, before cosine/BRDF.
 float x=d2*ir2;
 float a=hd_pow(tb_max(tb_abs(tb_min(1,tb_max(0,1-x*x))),9.9999997e-05f),light.falloff_exponent);
 float bias=tb_abs(light.falloff_bias);
 a*=light.falloff_bias<0 ? (4*ir2+bias) : (1/tb_max(d2,9.9999997e-05f)+bias);
 float k=0.07957747f*a;
 if(spot)k=0.31830987f*(a*(t*t));
 r.radiance=hd_mul(light.color,k*(light.global_selector<0?global_scale:1));
 if(!tb_finite3(r.radiance) || r.radiance.x<0 || r.radiance.y<0 || r.radiance.z<0)r.status=HdInvalid;
 return r;
}
// Exact incident culling: preserves invalid status and never discards positive energy.
inline bool wl_known_zero(HdIncident i){
 return i.status==HdOk && i.radiance.x==0 && i.radiance.y==0 && i.radiance.z==0;
}
// Adaptive effective K = min(reaching candidates,32); overflow still retains probe.
inline void wl_append(
#ifdef __cplusplus
 WlSelection& s,
#else
 inout WlSelection s,
#endif
 WlUint id,float d2){
 if(!tb_finite(d2) || d2<0)return;
 ++s.candidates;if(s.count<WlK){s.ids[s.count]=id;s.distances[s.count]=d2;++s.count;}
}
// Stable nearest-centre order, slot id breaks ties. Caller visits each slot once.
inline void wl_insert(
#ifdef __cplusplus
 WlSelection& s,
#else
 inout WlSelection s,
#endif
 WlUint id,float d2){
 if(!tb_finite(d2) || d2<0)return;
 ++s.candidates;
 WlUint at=s.count;
 if(at==WlK){at=WlK-1u;if(d2>s.distances[at] || (d2==s.distances[at] && id>=s.ids[at]))return;}
 else ++s.count;
 while(at>0u){WlUint prev=at-1u;if(d2>s.distances[prev] || (d2==s.distances[prev] && id>=s.ids[prev]))break;
  s.ids[at]=s.ids[prev];s.distances[at]=s.distances[prev];at=prev;}
 s.ids[at]=id;s.distances[at]=d2;
}
#ifdef __cplusplus
}
#endif
