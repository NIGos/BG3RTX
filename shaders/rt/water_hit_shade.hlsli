// Layout capability only: the setting itself is a Vulkan specialization constant.
// The ordinary requests do not include any of this code (key-off byte identity).
#pragma once
#ifdef WATER_HIT_SHADE_LAYOUT
#ifdef WATER_HIT_SHADE_REFERENCE
static const uint kWaterHitShade = WATER_HIT_SHADE_REFERENCE;
#else
[[vk::constant_id(1016)]] const uint kWaterHitShade = 0u;
#endif
#include "bg3_lights.hlsli"
#include "bg3_ambient.hlsli"
#define BG3_SUN_SET 3
#include "bg3_sun.hlsli"
#define RT_HIT_INSTANCED 1
#define RT_HIT_WIND_RGB 1
#include "native_hit_material.hlsli"
#define BG3_BOX_SET 2
#define NATIVE_HIT_BOXES 1
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 3
#define NATIVE_HIT_SLOTS_BINDING 7
#include "native_hit_direct.hlsli"
#define NATIVE_HIT_SUN_ROOF 1
#define NATIVE_HIT_SHADE_FULL 1
#include "native_hit_shade.hlsli"
#include "../../common/water_world_lights.h"
[[vk::binding(31,0)]] ByteAddressBuffer waterWorldLights;
static bool g_water_truncated=false;
uint water_world_shade(RtHit h,float3 H,float3 R,float footprint,out float3 L){
 L=0;
 if(waterWorldLights.Load(4096u*4u)!=0u)return kHitShadeDirect;
 NativeHitMaterial m=rt_hit_material(h,footprint);if(m.status!=0u)return kHitShadeMaterial;
 float3 P=native_hit_position(h,H),ng=rt_hit_normal(h);
 WlSelection selected=wl_empty();uint bytes;lLights.GetDimensions(bytes);
 [loop]for(uint id=0u;id<min(bytes/80u,4096u);++id){
  uint type=waterWorldLights.Load(id*4u);if(type==0u)continue;
  if(type==3u)return kHitShadeDirect; // inconsistent type: fail closed
  float4 sphere=asfloat(lLights.Load4(id*80u));uint channels=lLights.Load(id*80u+60u);
  if(!wl_reaches(P,sphere.xyz,sphere.w,channels,m.channel))continue;
  float3 d=P-sphere.xyz;wl_insert(selected,id,dot(d,d));
 }
 g_water_truncated=selected.candidates>WlK;
 // Truncation loses unknown energy. Retain the receiver probe rather than
 // silently treating a partial nearest set as complete lighting.
 if(g_water_truncated)return kHitShadeDirect;
 float scale=asfloat(lScale.Load(0));float3 direct=0,native=0;
 [loop]for(uint j=0u;j<selected.count;++j){
  uint id=selected.ids[j];Bg3Light b=load_light(id);
  HdLight l;l.position=b.pos;l.color=b.col;l.direction=b.dir;l.inverse_radius=b.inv_r;
  l.cos_outer=b.cos_outer;l.cos_inner=b.cos_inner;l.global_selector=b.m3;
  l.falloff_exponent=b.m4;l.falloff_bias=b.m5;l.channels=b.chan;
  HdIncident i=hd_native_incident(l,waterWorldLights.Load(id*4u)==2u,P,m.channel,scale);
  HdDirect t=HD_SCATTER(m,m.normal,ng,-R,i,true,1.0);
  if(i.status!=HdOk || t.status!=HdOk)return kHitShadeDirect;
  if(!any(t.radiance>0))continue;
  if(dot(normalize(ng),i.direction)<=0)return kHitShadeDirect;
  float3 o=rt_offset_ray(P,normalize(ng)),delta=b.pos-o;float distance=length(delta);
  float radius=pc.ls_source_radius;bool slot;bool game=hd_policy_on() && hd_light_policy(id,b,radius,slot);
  float reach=distance-max(hd_policy_on()?radius:pc.ls_clear,pc.ls_clear);
  RtCone cone={footprint,0};uint vis=game || reach<=0?kRtLit:rt_visibility(o,delta/distance,reach,cone);
  if(vis==kRtUnknown)return kHitShadeDirect;
  direct+=t.radiance*(vis==kRtLit?1.0:0.0);if(game)native+=t.radiance;
 }
 HdDirect sun=native_hit_sun(m,P,-R,ng,footprint);if(sun.status!=HdOk)return kHitShadeSun;
 // The sky reflection direction at THIS surface: outgoing is -R. Pass the
 // resulting exact world direction to the cube; never re-reflect in the helper.
 float3 skyR=reflect(R,m.normal);
 if(!native_hit_sun_roof_ok(P,skyR))return kHitShadeSun;
 float a=m.parameters.value.base_roughness;
 float nov=max(dot(-R,m.normal),0.001953125);
 float3 sky=lgSpecCube0.SampleLevel(lgSampCube,skyR,a*8.0+lConsts.c[32].y-8.0).rgb*lConsts.c[31].z;
 if(lConsts.c[32].x>0.0)sky=lerp(sky,lgSpecCube1.SampleLevel(lgSampCube,skyR,a*8.0+lConsts.c[32].z-8.0).rgb*lConsts.c[31].w,lConsts.c[32].x);
 float3 diffuse=lgIrrCube0.SampleLevel(lgSampCube,m.normal,0.0).rgb*lConsts.c[31].z;
 if(lConsts.c[32].x>0.0)diffuse=lerp(diffuse,lgIrrCube1.SampleLevel(lgSampCube,m.normal,0.0).rgb*lConsts.c[31].w,lConsts.c[32].x);
 float4 dfg=bg3_dfg(nov,a);
 float A=native_hit_ao(P,ng,footprint,uint3(asuint(P.xy),pc.frame));
 float f=native_hit_ao_factor(A);
 float3 amb=(diffuse*m.parameters.value.diffuse_color*dfg.z*A+
             sky*(m.parameters.value.F0*dfg.x+dfg.y)*native_hit_spec_occlusion(A,dot(-R,m.normal),a))*(scale*100.0);
 if((pc.qa_zero&4u)!=0u)amb=0;
 if((pc.qa_zero&2u)!=0u)direct=native=0;
 L=((pc.ao_direct&3u)==1u?direct-native+native*f:direct*f)+
   sun.radiance*((pc.ao_direct&3u)==1u && (pc.ao_direct&12u)==12u?1.0:f)+amb;
 return all(isfinite(L)) && all(L>=0)?kHitShadeOk:kHitShadeInvalid;
}
// Reasons: 0 accepted, 1 unpublished/material, 2 light source, 3 sun/roof, 4 invalid.
// A qualified material row alone is insufficient: all lighting must also be supported.
uint water_hit_shade(RtHit h,float3 H,float3 R,RtCone cone,uint2 pixel,out float3 L) {
  L=0;
  if(!hit_material_publication_ready()) return 1u;
  if(kWaterHitShade==2u){uint4 cl;if(!native_hit_cluster(native_hit_position(h,H),cl))
    return water_world_shade(h,H,R,cone.f0+cone.spread*h.t,L);}
  NativeHitShade hs=native_hit_shade(h,H,-R,cone.f0+cone.spread*h.t,uint3(pixel,pc.frame));
  if(hs.status==kHitShadeOk) L=hs.radiance;
  return hs.status;
}
#endif
