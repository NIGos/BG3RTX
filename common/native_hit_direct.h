#ifndef BG3_NATIVE_HIT_DIRECT_H
#define BG3_NATIVE_HIT_DIRECT_H
// Declared FresnelBlend direct-light model with BG3's authored point/spot
// incident amplitudes. Not native BRDF equivalence or complete hit radiance.
#include "native_opaque_transport.h"
#include "transport_frame.h"
#ifdef __cplusplus
namespace bg3::transport {
using HdVec3=TbVec3;using HdUint=uint32_t;
inline float hd_pow(float a,float b){return std::pow(a,b);}
#else
#define HdVec3 float3
#define HdUint uint
float hd_pow(float a,float b){return pow(a,b);}
#endif
static const HdUint HdOk=0,HdInvalid=1,HdUnknownMaterial=2,HdNormalDomain=3,HdUnknownVisibility=4;
struct HdLight {
 HdVec3 position,color,direction;
 float inverse_radius,cos_outer,cos_inner,global_selector,falloff_exponent,falloff_bias;
 HdUint channels;
};
struct HdIncident {HdVec3 radiance,direction;float distance;HdUint status;};
struct HdDirect {HdVec3 radiance;HdUint status;};
inline HdVec3 hd_mul(HdVec3 a,float s){return tb_vec(a.x*s,a.y*s,a.z*s);}
inline HdIncident hd_native_incident(HdLight light,bool spot,HdVec3 surface_position,HdUint channel,float global_scale){
 HdIncident r;r.radiance=r.direction=tb_vec(0,0,0);r.distance=0;r.status=HdInvalid;
 if(channel>31 || !tb_finite3(surface_position) || !tb_finite3(light.position) || !tb_finite3(light.color) ||
    light.color.x<0 || light.color.y<0 || light.color.z<0 || !tb_finite(light.inverse_radius) || !(light.inverse_radius>0) ||
    !tb_finite(light.global_selector) || !tb_finite(light.falloff_exponent) || !tb_finite(light.falloff_bias))return r;
 if(light.global_selector<0 && (!tb_finite(global_scale) || global_scale<0))return r;
 if((light.channels&(1u<<channel))==0){r.status=HdOk;return r;}
 HdVec3 delta=tb_vec(light.position.x-surface_position.x,light.position.y-surface_position.y,light.position.z-surface_position.z);
 float d2=tb_dot(delta,delta),ir2=light.inverse_radius*light.inverse_radius;
 if(!tb_finite(d2) || !tb_finite(ir2) || !(ir2>0) || !(d2>=1.0000000e-10f))return r;
 r.distance=tb_sqrt(d2);r.direction=hd_mul(delta,1/r.distance);
 r.status=HdOk;if(!(d2<1/ir2))return r;
 // Exact authored falloff in bg3_lights.hlsli/f469db78, before cosine/BRDF.
 float x=d2*ir2;
 float a=hd_pow(tb_max(tb_abs(tb_min(1,tb_max(0,1-x*x))),9.9999997e-05f),light.falloff_exponent);
 float bias=tb_abs(light.falloff_bias);
 a*=light.falloff_bias<0 ? (4*ir2+bias) : (1/tb_max(d2,9.9999997e-05f)+bias);
 float k=0.07957747f*a;
 if(spot){
  TbDirection axis=tb_direction(light.direction);
  if(axis.status!=TbOk || !tb_finite(light.cos_outer) || !tb_finite(light.cos_inner) ||
     light.cos_outer< -1 || light.cos_inner>1 || light.cos_inner<light.cos_outer){r.status=HdInvalid;return r;}
  float c=-tb_dot(r.direction,axis.v);if(!(c>light.cos_outer))return r;
  float t=tb_min(1,tb_max(0,(c-light.cos_outer)/tb_max(light.cos_inner-light.cos_outer,9.9999997e-05f)));
  k=0.31830987f*(a*(t*t));
 }
 r.radiance=hd_mul(light.color,k*(light.global_selector<0?global_scale:1));
 if(!tb_finite3(r.radiance) || r.radiance.x<0 || r.radiance.y<0 || r.radiance.z<0)r.status=HdInvalid;
 return r;
}
inline HdDirect hd_scatter(NtFresnelBlendResult material,HdVec3 normal,HdVec3 geometric_normal,
                          HdVec3 outgoing,HdIncident incident,bool visibility_known,float visibility){
 HdDirect r;r.radiance=tb_vec(0,0,0);r.status=HdUnknownMaterial;
 if(material.status!=NtOk)return r;
 r.status=incident.status;if(incident.status!=HdOk)return r;
 r.status=HdUnknownVisibility;if(!visibility_known)return r;
 r.status=HdInvalid;if(!tb_finite(visibility) || visibility<0 || visibility>1)return r;
 // A channel/range/cone-excluded source is a known zero, not missing data.
 if(incident.radiance.x==0 && incident.radiance.y==0 && incident.radiance.z==0){r.status=HdOk;return r;}
 TfFrame frame=tf_frame(normal);TbDirection ng=tb_direction(geometric_normal),wo=tb_direction(outgoing),wi=tb_direction(incident.direction);
 if(frame.status!=TbOk || ng.status!=TbOk || wo.status!=TbOk || wi.status!=TbOk)return r;
 HdVec3 local_o=tf_to_local(frame,wo.v),local_i=tf_to_local(frame,wi.v);
 r.status=HdNormalDomain;
 // No unimplemented shading-normal correction or backface reflection is hidden.
 if(!(local_o.z>0) || !(tb_dot(ng.v,wo.v)>0) || tb_dot(ng.v,wi.v)*local_i.z<0)return r;
 if(local_i.z<=0){r.status=HdOk;return r;}
 TbEval value=tb_fresnel_evaluate(material.value,local_o,local_i);
 r.status=HdInvalid;if(value.status!=TbOk)return r;
 float factor=local_i.z*visibility;
 r.radiance=tb_vec(value.f.x*incident.radiance.x*factor,value.f.y*incident.radiance.y*factor,value.f.z*incident.radiance.z*factor);
 if(tb_finite3(r.radiance))r.status=HdOk;return r;
}
#ifdef __cplusplus
}
#else
#undef HdVec3
#undef HdUint
#endif
#endif
