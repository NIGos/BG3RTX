#pragma once
// Bounded reusable point/spot DIRECT-ONLY reference. No sun, projector,
// environment/probe, emission, AO multiplier or multiple-bounce energy implied.
#include "bg3_lights.hlsli"
#include "../../common/native_hit_direct.h"
struct NativeHitDirect {float3 radiance;uint status;uint lights;};
static const uint kHitDirectSource=5u,kHitDirectProjector=6u,kHitDirectDomain=7u;
// Diagnostics, off unless the caller sets g_hd_stats (its stats key): 12 counters from g_hd_base
// (core/gi.cpp hd_log): +0 light rays traced, +1 lit, +2 occluded with the first hit within 1 cm
// of the origin, +3 occluded by the hit's own triangle (g_hd_inst/g_hd_prim), +4 unknown, +5
// supported hits whose froxel lists no point/spot, +6 listed lights evaluated, +7 of them a known
// zero before the cosine (channel, range, cone), +8 positive but zero after it (N.L <= 0),
// +9 unsupported: source (outside the frustum/froxel slices, bad index), +10 projector, +11 other.
// 4 more from g_hd_xbase (the +12.. slots are taken): +0 occluded light rays whose blocker is
// within 1 m of the light (its fixture, or the character carrying it), +1 light rays traced to
// lights of radius >= 10 m (the darkvision pair has r = 18), +2 projector entries listed at a
// hit whose lists are valid, +3 of them evaluated (NATIVE_HIT_BOXES; the rest skipped).
static bool g_hd_stats=false;
static uint g_hd_base=0u,g_hd_xbase=0u,g_hd_bin=0u,g_hd_inst=0xffffffffu,g_hd_prim=0xffffffffu;
void hd_stat(uint k,bool c){if(g_hd_stats)stat_add(g_hd_base+k,c,g_hd_bin);}
#if defined(NATIVE_HIT_BOXES) || defined(NATIVE_HIT_PROJECTOR_SKIP)
void hd_xstat(uint k,bool c){if(g_hd_stats && g_hd_xbase!=0u)stat_add(g_hd_xbase+k,c,g_hd_bin);}
#define HD_XSTAT(k,c) hd_xstat(k,c)
#else
#define HD_XSTAT(k,c)   // the reference passes (rt_hit_direct, two-bounce): unchanged SPIR-V
#endif
// Projector (box) lights in the hit's froxel (cluster.z bits 0-9; their b24 entries follow the
// spot lights and index the box records b29):
//   NATIVE_HIT_BOXES (the pipeline binds the lighting's set 1 as BG3_BOX_SET, rf_hit_shade):
//     evaluated with rt/bg3_boxes.hlsli (channel, inside the box, box falloff, cookie at mip 0,
//     direction _m9, b13[0] when _m1 bit 0) through the same declared BSDF, with D-LS's
//     ls_box visibility rule: a box with a game shadow map (_m8 bit 0) gets one ray along _m9
//     to where it leaves the box; one without keeps the game's look (unshadowed).
//   NATIVE_HIT_PROJECTOR_SKIP (no box binding, gi_hit_direct): the projector term is left out
//     (counted, xbase +2 - +3) and the point/spot term stays supported. Bias: darker by the
//     projector light at such hits (never brighter).
//   neither: the hit is unsupported (kHitDirectProjector), as before.
#ifdef NATIVE_HIT_BOXES
#include "bg3_boxes.hlsli"
#endif
#ifdef RT_HIT_MATERIAL
// The hit point on its triangle (vertices x barycentrics, as rtOrigin gives D-LS its exact TLAS
// surface point), not o + d t: Wachter & Binder's ULP offset (rt_offset_ray) assumes a point on
// the plane, and o + d t carries the ray's float error and the hardware t error. Kept only where
// it agrees with the ray point (same triangle): 1 mm + 1e-5 of the distance.
float3 native_hit_position(RtHit hit,float3 ray_point){
 InstInfo ii=inst_info(hit.inst);uint stride=ii.meta&0xffu,bytes;
 if((ii.meta&kInfoLegacy)!=0u || hit.prim>=ii.tris || stride<16u || hit.aborted || !(hit.t>=0))return ray_point;
#ifdef RT_NATIVE_HAIR
 if((ii.meta&kInfoHairProcedural)!=0u)return ray_point;
#endif
 if((ii.meta&kInfoPool)!=0u)gMeshPool.GetDimensions(bytes);else gOpaqueSoup.GetDimensions(bytes);
 if(!tri_in_range(ii.base,hit.prim,stride,bytes))return ray_point;
 float3 p0=inst_pos(ii,hit.prim,0),p1=inst_pos(ii,hit.prim,1),p2=inst_pos(ii,hit.prim,2);
 if((ii.meta&kInfoPool)!=0u){p0=mul(hit.o2w,float4(p0,1.0));p1=mul(hit.o2w,float4(p1,1.0));p2=mul(hit.o2w,float4(p2,1.0));}
 float3 p=p0+(p1-p0)*hit.bary.x+(p2-p0)*hit.bary.y;
 return all(isfinite(p)) && length(p-ray_point)<=1e-3+1e-5*length(ray_point)?p:ray_point;
}
#endif
// Unlike bg3_cluster_raw's renderer fallback, missing/overflowed sources cannot
// certify a zero direct term. Native frustum/froxel membership is required.
bool native_hit_cluster(float3 surface_position,out uint4 cluster){
 cluster=0;float2 uv;float depth;
 if(!all(isfinite(surface_position)) || !project(frustum_inverse(),surface_position,uv,depth) ||
    !all(isfinite(uv)) || any(uv<0) || any(uv>=1))return false;
 uint tile_size=asuint(lConsts.c[31].x),last_z=asuint(lConsts.c[31].y);
 float columns=lConsts.c[30].z,rows=lConsts.c[30].w;
 float z=log2(depth*cam2.c[12].y)*lConsts.c[30].x+lConsts.c[30].y;
 if(tile_size==0 || !isfinite(z) || !isfinite(columns+rows) || columns<1 || rows<1 ||
    columns!=floor(columns) || rows!=floor(rows) || z<0 || z>float(last_z))return false;
 if(any(lFlags.res==0u))return false;
 uint2 tile=uint2(uv*float2(lFlags.res))/tile_size;
 if(any(float2(tile)>=float2(columns,rows)))return false;
 float index=(floor(z)*rows+float(tile.y))*columns+float(tile.x);
 uint bytes;lClusters.GetDimensions(bytes);
 if(!isfinite(index) || index<0 || index>=float(bytes/16u))return false;
 cluster=lClusters.Load4(uint(index)*16u);
 uint points=cluster.y&4095u,spots=(cluster.y>>12)&4095u,count=points+spots;
 uint indices;lIndex.GetDimensions(indices);
 if(count>kMaxFroxelLights || cluster.x>indices/4u || count>indices/4u-cluster.x)return false;
 return true;
}
NativeHitDirect native_hit_direct_eval(NativeHitMaterial material,float3 surface_position,float3 outgoing,float3 geometric_normal,float footprint){
 NativeHitDirect result;result.radiance=0;result.status=kHitDirectSource;result.lights=0;
 if(!isfinite(footprint) || footprint<0){result.status=kHitDirectDomain;return result;}
 if(material.status!=0){result.status=HdUnknownMaterial;return result;}
 uint4 cluster;if(!native_hit_cluster(surface_position,cluster))return result;
#if !defined(NATIVE_HIT_BOXES) && !defined(NATIVE_HIT_PROJECTOR_SKIP)
 // The declared subset is the complete point+spot term only; do not silently
 // label a projector-bearing receiver as a complete local-light reference.
 if((cluster.z&1023u)!=0u){result.status=kHitDirectProjector;return result;}
 uint points=cluster.y&4095u,count=points+((cluster.y>>12)&4095u);
#else
 uint points=cluster.y&4095u,count=points+((cluster.y>>12)&4095u),projectors=cluster.z&1023u;
 {uint indices;lIndex.GetDimensions(indices);if(projectors>indices/4u-cluster.x-count)return result;}   // list past b24: source
#endif
 hd_stat(5u,count==0u);
 uint scale_bytes;lScale.GetDimensions(scale_bytes);if(scale_bytes<4)return result;
 float global_scale=asfloat(lScale.Load(0));
 [loop]for(uint item=0;item<count;++item){
  uint index=lIndex.Load((cluster.x+item)*4u),bytes;lLights.GetDimensions(bytes);
  if(index>=bytes/80u)return result;
  Bg3Light light=load_light(index);
  HdLight source;source.position=light.pos;source.color=light.col;source.direction=light.dir;
  source.inverse_radius=light.inv_r;source.cos_outer=light.cos_outer;source.cos_inner=light.cos_inner;
  source.global_selector=light.m3;source.falloff_exponent=light.m4;source.falloff_bias=light.m5;source.channels=light.chan;
  HdIncident incident=hd_native_incident(source,item>=points,surface_position,material.channel,global_scale);
  if(incident.status!=HdOk){result.status=incident.status;return result;}
  // Validate material/normal domain before spending a visibility ray.
  HdDirect term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,1);
  if(term.status!=HdOk){result.status=term.status;return result;}
  const bool incident_zero=!any(incident.radiance>0);
  hd_stat(6u,true);hd_stat(7u,incident_zero);hd_stat(8u,!incident_zero && !any(term.radiance>0));
  if(!any(term.radiance>0))continue;
  // flags bit0 means native atlas presence, not physical transparency to the
  // light. The physical point/spot reference tests every positive term,
  // including lights that the native renderer originally left unshadowed.
  float3 normal=normalize(geometric_normal);
  if(!(dot(normal,incident.direction)>0)){result.status=HdNormalDomain;return result;}
  float3 origin=rt_offset_ray(surface_position,normal),delta=light.pos-origin;
  float distance=length(delta);if(!(distance>0) || !isfinite(distance)){result.status=kHitDirectDomain;return result;}
  // Stop ls_clear short of the light, as D-LS trace_light (rt_local_shadows.hlsl): BG3 lights
  // sit inside their own lamp/sconce/candle geometry, which is in the BVH. A ray to the light
  // centre hits that fixture and every such light reads occluded (live S8: 22871 supported
  // GI hits, 0 nonzero; rf tier (a) check direct term 0). Within ls_clear: lit, as D-LS.
  float reach=distance-max(pc.ls_clear,0.0);
  RtCone cone={footprint,0};uint visibility=reach>0?rt_visibility(origin,delta/distance,reach,cone):kRtLit;
  hd_stat(0u,reach>0);hd_stat(1u,reach>0 && visibility==kRtLit);hd_stat(4u,visibility==kRtUnknown);
  HD_XSTAT(1u,reach>0 && light.inv_r*10.0<=1.0);
  if(g_hd_stats){
   // What occludes: a first hit within 1 cm of the origin or on the hit's own triangle is
   // self-intersection, not a caster (stats key only; one extra closest-hit query).
   bool near=false,self=false;
   if(visibility==kRtOccluded && reach>0){
    RtHit blocker=rt_closest_hit(origin,delta/distance,reach,cone);
    near=blocker.t>=0 && blocker.t<0.01;self=blocker.t>=0 && blocker.inst==g_hd_inst && blocker.prim==g_hd_prim;
    HD_XSTAT(0u,blocker.t>=0 && distance-blocker.t<=1.0);
   }
   hd_stat(2u,near);hd_stat(3u,self);
  }
  if(visibility==kRtUnknown){result.status=HdUnknownVisibility;return result;}
  term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
  if(term.status!=HdOk){result.status=term.status;return result;}
  result.radiance+=term.radiance;++result.lights;
 }
#if defined(NATIVE_HIT_BOXES) || defined(NATIVE_HIT_PROJECTOR_SKIP)
 [loop]for(uint p=0;p<projectors;++p){
  HD_XSTAT(2u,true);
#ifdef NATIVE_HIT_BOXES
  uint bi=lIndex.Load((cluster.x+count+p)*4u);
  if(!bg3_box_in(bi))return result;   // no record: source
  Bg3Box box=load_box(bi);
  HD_XSTAT(3u,true);
  float3 uvw;
  if(!bg3_box_reaches(box,material.channel,surface_position,uvw))continue;   // channel / outside: known zero
  const float g=bg3_box_falloff(box,uvw);
  if(!(g>0))continue;
  if(!all(isfinite(box.dir)) || !(dot(box.dir,box.dir)>1e-20)){result.status=HdInvalid;return result;}
  HdIncident incident;incident.direction=normalize(box.dir);incident.distance=0;incident.status=HdOk;
  // f469db78's box loop: colour (x cookie) x falloff (x b13[0]), no 1/pi or 1/4pi; the cookie at
  // mip 0 (zero gradients: the game's own rule for invalid quad differences).
  incident.radiance=bg3_box_colour(box,uvw.xy,0.0.xx,0.0.xx)*(g*((box.m1&1u)!=0u?global_scale:1.0));
  if(!all(isfinite(incident.radiance)) || any(incident.radiance<0)){result.status=HdInvalid;return result;}
  HdDirect term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,1);
  if(term.status!=HdOk){result.status=term.status;return result;}
  if(!any(term.radiance>0))continue;
  float3 normal=normalize(geometric_normal);
  if(!(dot(normal,incident.direction)>0)){result.status=HdNormalDomain;return result;}
  uint visibility=kRtLit;
  if((box.flags&1u)!=0u){
   float3 origin=rt_offset_ray(surface_position,normal);
   const float tmax=bg3_box_exit(box,origin,incident.direction);   // the box is the light's extent
   RtCone cone={footprint,0};
   if(tmax>0)visibility=rt_visibility(origin,incident.direction,tmax,cone);
  }
  if(visibility==kRtUnknown){result.status=HdUnknownVisibility;return result;}
  term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
  if(term.status!=HdOk){result.status=term.status;return result;}
  result.radiance+=term.radiance;++result.lights;
#endif
 }
#endif
 result.status=all(isfinite(result.radiance))?HdOk:HdInvalid;return result;
}
NativeHitDirect native_hit_direct(NativeHitMaterial material,float3 surface_position,float3 outgoing,float3 geometric_normal,float footprint){
 NativeHitDirect r=native_hit_direct_eval(material,surface_position,outgoing,geometric_normal,footprint);
 hd_stat(9u,r.status==kHitDirectSource);hd_stat(10u,r.status==kHitDirectProjector);
 hd_stat(11u,r.status!=HdOk && r.status!=kHitDirectSource && r.status!=kHitDirectProjector);
 return r;
}
