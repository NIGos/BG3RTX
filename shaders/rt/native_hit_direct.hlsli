#pragma once
// Bounded reusable point/spot DIRECT-ONLY reference. No sun, projector,
// environment/probe, emission, AO multiplier or multiple-bounce energy implied.
#include "bg3_lights.hlsli"
#include "../../common/native_hit_direct.h"
// NATIVE_HIT_GAME_BRDF (rf_hit_gi, rt_reflections.hlsl): the direct terms at a hit (point/spot, box, sun) use the
// game's own lobes (hd_game_scatter) instead of the declared FresnelBlend, so a light gives the hit what the
// screen's lighting gives it. Without it the call is hd_scatter exactly as before.
#ifdef NATIVE_HIT_GAME_BRDF
#define HD_SCATTER(m,n,ng,o,i,known,vis) hd_game_scatter((m).parameters,n,ng,o,i,known,vis)
#else
#define HD_SCATTER(m,n,ng,o,i,known,vis) hd_scatter((m).scattering,n,ng,o,i,known,vis)
#endif
#ifdef NATIVE_HIT_DLS_POLICY
// native: the part of radiance from lights the screen leaves at the game's own visibility (the
// D-LS policy below); the caller applies the game's AO-on-direct factor to it (native_hit_ao_direct).
struct NativeHitDirect {float3 radiance;uint status;uint lights;float3 native;};
#else
struct NativeHitDirect {float3 radiance;uint status;uint lights;};
#endif
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
#ifdef NATIVE_HIT_BOX_TERM
// rf_hit_shade's bias breakdown (rt_reflections.hlsl, counters 383-384): the projector (box) part of
// the last native_hit_direct_eval's radiance, before the AO-on-direct factor. Diagnostics only.
static float3 g_hd_box=0;
#endif
#ifdef NATIVE_HIT_DLS_POLICY
// The screen's light policy at hits (gi_hit_direct, rf_hit_shade; hit_ls_policy=1, Push::ls_flags
// bit 24; docs/TRACK_D_REFLECTIONS.md 14). D-LS (rt_local_shadows.hlsl ls_light_traced) decides
// per light what the screen shows; the hit follows it, so a point lit on screen and the same
// point seen in a reflection or a bounce get the same light:
//   - a light without a game shadow map that D-LS leaves to the game (range below ls_min_radius,
//     or a matched light ls_skip_light skips: ls_owners, the art-intent rule ls_noshadow, by
//     default the darkvision pair) keeps the game's visibility: UNSHADOWED, no ray. Its term is
//     also returned in .native for the game's AO-on-direct factor mix(1, AO, m0.y);
//   - a traced light stops max(R, ls_clear) short of its centre, R its source radius from the
//     per-light data (lsSlots row 0 .x: asset DB / SE bridge), else ls_source_radius (D-LS's
//     hard-shadow rule);
//   - a light with a game shadow map is traced (the hit has no atlas sample; D-LS replaces the
//     atlas by the traced visibility by default, ls_shadowed=3).
// Per-light data: the includer binds lsSlots as hdSlots (GI: set 0 b46, its push set; D-RF: the
// sun set b7, D-RF's push set is full) and sets ls_flags bit 0 when it holds a join. A slot is
// used only while its key (row 1: absolute position, radius) is this light's: the GI runs before
// this frame's join, so a slot may hold last frame's light (a character light moves <= 0.25 m
// per frame at 15 m/s and 60 fps; another light of the same radius that close is harmless).
[[vk::binding(NATIVE_HIT_SLOTS_BINDING, NATIVE_HIT_SLOTS_SET)]] [[vk::image_format("rgba32ui")]] RWTexture2D<uint4> hdSlots;
static const uint kHdRtlShadow=32u,kHdRtlCastShadow=64u,kHdRtlPlayer=256u;
// Policy counters (core/gi.cpp kHitPolicyFirst): g_hd_pbase +0 lights left at the game's
// visibility, +1 traced with a per-light R; +2 hits with an AO estimate, +3 its sum x 8192.
static uint g_hd_pbase=0u;
void hd_pstat(uint k,bool c){if(g_hd_stats && g_hd_pbase!=0u)stat_add(g_hd_pbase+k,c,g_hd_bin);}
bool hd_policy_on(){return (pc.ls_flags&(1u<<24))!=0u;}
#ifdef NATIVE_HIT_BOXES
// Box (projector) lights the screen leaves to the game (its D-LS box pass did not run this frame:
// ls_box=0, Push::ls_flags bit 26 clear): the game's lighting keeps their diffuse x mix(1, AO, m0.y)
// (docs/LIGHT_CONVENTIONS.md 2), so the hit returns them in .native for the same factor.
bool hd_box_game(){return hd_policy_on() && (pc.ls_flags&(1u<<26))==0u;}
#endif
// ls_skip_light (rt_local_shadows.hlsl), on the same Push bits.
bool hd_skip_light(uint4 si){
 if(((pc.ls_flags>>(8u+min(si.z&255u,4u)))&1u)==0u)return true;
 const uint rule=(pc.ls_flags>>16)&3u;
 const bool noshadow=(si.y&(kHdRtlShadow|kHdRtlCastShadow))==0u;
 return noshadow && (rule==2u || (rule==1u && (si.y&kHdRtlPlayer)!=0u));
}
// True: the screen keeps this light's own (unshadowed) visibility. R: the stop-short radius.
bool hd_light_policy(uint index,Bg3Light light,out float R,out bool slot_r){
 R=pc.ls_source_radius;slot_r=false;
 const bool mapped=(light.flags&1u)!=0u;
 bool game=!mapped && !(1.0/max(light.inv_r,1e-12)>=pc.ls_min_radius);
 if((pc.ls_flags&1u)!=0u && index<4096u){
  const uint4 si=hdSlots[uint2(index,0u)];
  const uint4 key=hdSlots[uint2(index,1u)];
  const float r=1.0/max(light.inv_r,1e-12);
  const float3 w=light.pos+cam2.c[13].xyz,d=asfloat(key.xyz)-w;
  const bool same=all(isfinite(d)) && dot(d,d)<=0.0625 && abs(asfloat(key.w)-r)<=1e-3+1e-4*r;
  if((si.z&0x10000u)!=0u && same){
   const float sr=asfloat(si.x);
   if(isfinite(sr) && sr>=0){R=sr;slot_r=true;}
   if(!mapped && hd_skip_light(si))game=true;
  }
 }
 return game;
}
#endif
#ifdef RT_HIT_MATERIAL
// The hit point on its triangle (vertices x barycentrics, as rtOrigin gives D-LS its exact TLAS
// surface point), not o + d t: Wachter & Binder's ULP offset (rt_offset_ray) assumes a point on
// the plane, and o + d t carries the ray's float error and the hardware t error. Kept only where
// it agrees with the ray point (same triangle): 1 mm + 1e-5 of the distance.
float3 native_hit_position(RtHit hit,float3 ray_point){
 InstInfo ii=inst_info(hit.inst);uint stride=ii.meta&0xffu,bytes;
 if((ii.meta&kInfoLegacy)!=0u || hit.prim>=ii.tris || stride<16u || hit.aborted || !(hit.t>=0))return ray_point;
#ifdef RT_SPEC_NATIVE
  if (RT_HAIR_ON) {
 if((ii.meta&kInfoHairProcedural)!=0u)return ray_point;
  }
#else
#ifdef RT_NATIVE_HAIR
 if((ii.meta&kInfoHairProcedural)!=0u)return ray_point;
#endif
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
#ifdef NATIVE_HIT_DLS_POLICY
 result.native=0;
#endif
#ifdef NATIVE_HIT_BOX_TERM
 g_hd_box=0;
#endif
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
  HdDirect term=HD_SCATTER(material,material.normal,geometric_normal,outgoing,incident,true,1);
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
#ifdef NATIVE_HIT_DLS_POLICY
  float clear=max(pc.ls_clear,0.0);
  if(hd_policy_on()){
   float R;bool slot_r;
   const bool game=hd_light_policy(index,light,R,slot_r);
   hd_pstat(0u,game);hd_pstat(1u,!game && slot_r);
   if(game){   // the screen's visibility for this light: unshadowed (no ray)
    result.radiance+=term.radiance;result.native+=term.radiance;++result.lights;continue;
   }
   clear=max(R,clear);
  }
  float reach=distance-clear;
#else
  // Stop ls_clear short of the light, as D-LS trace_light (rt_local_shadows.hlsl): BG3 lights
  // sit inside their own lamp/sconce/candle geometry, which is in the BVH. A ray to the light
  // centre hits that fixture and every such light reads occluded (live S8: 22871 supported
  // GI hits, 0 nonzero; rf tier (a) check direct term 0). Within ls_clear: lit, as D-LS.
  float reach=distance-max(pc.ls_clear,0.0);
#endif
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
  term=HD_SCATTER(material,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
  if(term.status!=HdOk){result.status=term.status;return result;}
  result.radiance+=term.radiance;++result.lights;
 }
#if defined(NATIVE_HIT_BOXES) || defined(NATIVE_HIT_PROJECTOR_SKIP)
 [loop]for(uint p=0;p<projectors;++p){
  HD_XSTAT(2u,true);
#ifdef NATIVE_HIT_BOXES
  uint bi=lIndex.Load((cluster.x+count+p)*4u);
  if(!bg3_box_in(bi))return result;   // no record: source
  HD_XSTAT(3u,true);
  // bg3_box_reaches from the record's channel word and box rows only (cost, 2026-11-01): most listed
  // boxes do not reach the hit (live S8 ~2 box entries per hit), so the rest of the 128-byte record
  // (and the cookie) is loaded only for a box that does. bg3_box_reaches_rec: the same function
  // (common/box_light.h bl_reaches, box_light_test) as bg3_box_reaches: exact.
  float3 uvw;
  if(!bg3_box_reaches_rec(bi,material.channel,surface_position,uvw))continue;   // channel off or outside: known zero
  Bg3Box box=load_box(bi);
  const float g=bg3_box_falloff(box,uvw);
  if(!(g>0))continue;
  if(!all(isfinite(box.dir)) || !(dot(box.dir,box.dir)>1e-20)){result.status=HdInvalid;return result;}
  HdIncident incident;incident.direction=normalize(box.dir);incident.distance=0;incident.status=HdOk;
  // f469db78's box loop: colour (x cookie) x falloff (x b13[0]), no 1/pi or 1/4pi; the cookie at
  // mip 0 (zero gradients: the game's own rule for invalid quad differences).
  incident.radiance=bg3_box_colour(box,uvw.xy,0.0.xx,0.0.xx)*(g*((box.m1&1u)!=0u?global_scale:1.0));
  if(!all(isfinite(incident.radiance)) || any(incident.radiance<0)){result.status=HdInvalid;return result;}
  HdDirect term=HD_SCATTER(material,material.normal,geometric_normal,outgoing,incident,true,1);
  if(term.status!=HdOk){result.status=term.status;return result;}
  if(!any(term.radiance>0))continue;
  float3 normal=normalize(geometric_normal);
  if(!(dot(normal,incident.direction)>0)){result.status=HdNormalDomain;return result;}
  uint visibility=kRtLit;
#ifdef NATIVE_HIT_BOX_LS2
  // rf_hit_gi: the screen's box rule when its D-LS box pass ran with ls_box=2 (ls_flags bits 26 and 27;
  // rt_local_shadows.hlsl ls_box_traced / ls_box_ray): boxes without a game shadow map are traced too, up
  // to the end of their light-side fade-in, and shadow-mapped ones end at their shadow view's near plane.
  // Otherwise (ls_box 0 / 1) as below: shadow-mapped boxes to the box exit, the others unshadowed.
  const bool ls2=(pc.ls_flags&(1u<<26))!=0u && (pc.ls_flags&(1u<<27))!=0u;
  if((box.flags&1u)!=0u || ls2){
   float3 origin=rt_offset_ray(surface_position,normal);
   float tmax=bg3_box_exit(box,origin,incident.direction);   // the box is the light's extent
   if(ls2)tmax=min(tmax,(box.flags&1u)!=0u?bg3_box_shadow_near(box,origin,incident.direction)
                                         :bg3_box_fixture_end(box,origin,incident.direction));
   RtCone cone={footprint,0};
   if(tmax>0)visibility=rt_visibility(origin,incident.direction,tmax,cone);
  }
#else
  if((box.flags&1u)!=0u){
   float3 origin=rt_offset_ray(surface_position,normal);
   const float tmax=bg3_box_exit(box,origin,incident.direction);   // the box is the light's extent
   RtCone cone={footprint,0};
   if(tmax>0)visibility=rt_visibility(origin,incident.direction,tmax,cone);
  }
#endif
  if(visibility==kRtUnknown){result.status=HdUnknownVisibility;return result;}
  term=HD_SCATTER(material,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
  if(term.status!=HdOk){result.status=term.status;return result;}
  result.radiance+=term.radiance;++result.lights;
#ifdef NATIVE_HIT_BOX_TERM
  g_hd_box+=term.radiance;
#endif
#ifdef NATIVE_HIT_DLS_POLICY
  if(hd_box_game())result.native+=term.radiance;   // x the AO-on-direct factor (native_hit_ao_direct)
#endif
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

#ifdef NATIVE_HIT_DLS_POLICY
// AO at the hit (hit_ao_rays > 0, Push::rt_ao_rays): the screen's lighting multiplies the
// game's ambient by the AO target b6 (diffuse x AO, specular x Lagarde's SO) and direct light by
// mix(1, AO, m0.y), and with GI on that AO is the hybrid GI's ray-traced visibility
// (gi_ao_apply.hlsl, gi_ao 1). A hit has no AO target: this is the same estimator at the hit,
// rays cosine-distributed about the geometric normal, gi_rt_radius long, a hit at t weighing
// 1 - (t / R)^2 and an aborted query the GI's mean fade 1 - u^2 / 3 (gi_trace_rt.hlsl):
// A = 1 - mean weight, the expected value of the screen's AO at the same point. 1 when the game
// does not use its AO texture (m0.x <= 0) or with 0 rays (AO 1, as before).
float native_hit_ao(float3 position,float3 geometric_normal,float footprint,uint3 key){
 const uint rays=min(pc.rt_ao_rays,8u);
 if(rays==0u || !(lFlags.m0.x>0.0))return 1.0;
 const float3 n=normalize(geometric_normal);
 const float R=max(pc.gi_rt_radius,0.01);
 const float3 o=rt_offset_ray(position,n);
 RtCone cone={footprint,0};
 uint seed=pcg3d(key^uint3(0x510e527fu,0x9b05688cu,0x1f83d9abu)).x;
 float occ=0;
 [loop]for(uint i=0;i<rays;++i){
  float3 dir=cosine_hemisphere(n,rand01(seed),rand01(seed));
  bool aborted;
  float t=rt_closest(o,dir,R,cone,aborted);
  const float u=aborted?(t>=0.0?saturate(t/R):1.0):0.0;
  const float k=saturate(t/R);
  occ+=aborted?1.0-u*u/3.0:(t>=0.0?1.0-k*k:0.0);
 }
 const float A=saturate(1.0-occ/float(rays));
 hd_pstat(2u,true);
 if(g_hd_stats && g_hd_pbase!=0u){
  uint v=WaveActiveSum(uint(A*8192.0+0.5));
  if(WaveIsFirstLane() && v)rtStats.InterlockedAdd(((g_hd_pbase+3u)*64u+(WaveReadLaneFirst(g_hd_bin)&63u))*4u,v);
 }
 return A;
}
// The game's AO-on-direct factor mix(1, AO, m0.y) at AO A (f469db78; D-LS ls_ao_factors).
float native_hit_ao_factor(float A){return lerp(1.0,lFlags.m0.x>0.0?A:1.0,lFlags.m0.y);}
// The direct term as the screen gives it: the lights at the game's visibility x the factor; the
// traced ones x it unless ao_direct=1 (the default: traced lights get their full value back;
// 2 uses the game's SSAO, here A).
float3 native_hit_ao_direct(NativeHitDirect d,float A){
 const float f=native_hit_ao_factor(A);
 const float ft=(pc.ao_direct&3u)==1u?1.0:f;
 return (d.radiance-d.native)*ft+d.native*f;
}
#endif
