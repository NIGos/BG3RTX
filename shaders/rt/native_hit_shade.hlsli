#pragma once
// Shared hidden-hit shading: the qualified native material (native_hit_material.hlsli) and
// its RT-shadowed point/spot light (native_hit_direct.hlsli), the same two calls gi_hit_direct
// makes (gi_trace_rt.hlsl RT_HIT_GI_DIRECT, which keeps its inline calls so that its SPIR-V,
// under live qualification as M4.1, stays byte-identical; M4.3 moves it here). rf_hit_shade
// (rt_reflections.hlsl, RF_HIT_SHADE) adds the sun and the game's ambient at the hit
// (NATIVE_HIT_SHADE_FULL; docs/PLAN_M4_REFLECTIONS_GI.md M4.2).
// Needs rt_common.hlsli, native_hit_material.hlsli and native_hit_direct.hlsli first (the full
// shading with NATIVE_HIT_DLS_POLICY: the screen's light policy and AO).
struct NativeHitLit {NativeHitMaterial material;NativeHitDirect direct;};
NativeHitLit native_hit_lit(RtHit hit,float3 position,float3 outgoing,float footprint){
 NativeHitLit r;r.material=rt_hit_material(hit,footprint);
 r.direct.radiance=0;r.direct.status=HdUnknownMaterial;r.direct.lights=0;
 g_hd_inst=hit.inst;g_hd_prim=hit.prim;
 if(r.material.status==0u)r.direct=native_hit_direct(r.material,native_hit_position(hit,position),outgoing,rt_hit_normal(hit),footprint);
 return r;
}

#if NATIVE_HIT_SHADE_FULL
// Also needs bg3_sun.hlsli (the D-LS sun set) and bg3_ambient.hlsli (the lighting's own set 1
// as set 2): its sky irradiance cubes b34/b36 (f469db78 _14/_16, read at N, mip 0, x _m27/_m28,
// blended by _m29) are the diffuse twin of the specular cubes b35/b37.
[[vk::binding(34, 2)]] TextureCube<float4> lgIrrCube0;
[[vk::binding(36, 2)]] TextureCube<float4> lgIrrCube1;

// L_b at the hit = point/spot (native_hit_direct) + sun (replicated colour x clouds x b13[0],
// one RT visibility ray instead of the native mask) + the game's ambient there with AO A (AO 1
// before hit_ao_rays; native_hit_ao, native_hit_direct.hlsli: the screen's AO estimator at the
// hit, applied as the lighting applies b6: ambient diffuse x A, specular x SO(A), direct light
// x mix(1, A, m0.y) where the screen keeps that factor):
// diffuse (sky irradiance x diffuse colour x DFG.z) and specular (bg3_spec_ambient at the hit,
// its local probes included) x b13[0]*100, and the projector (box) lights of the hit's froxel
// (native_hit_direct.hlsli NATIVE_HIT_BOXES, D-LS's ls_box visibility rule). Not included:
// emission, aerial perspective.
// status 0 complete; 1 material unknown; 2 local lights unsupported (outside the frustum,
// bad list or record, unknown visibility); 3 sun unsupported; 4 not finite.
static const uint kHitShadeOk=0u,kHitShadeMaterial=1u,kHitShadeDirect=2u,kHitShadeSun=3u,kHitShadeInvalid=4u;
// material_status: rt_hit_material's. Terms as added (after A); native: the direct part from lights
// at the game's visibility; ambient_d / ambient_s: the ambient's diffuse / specular parts;
// radiance_ao1: the same hit at AO 1 (the L_b before hit_ao_rays, for the tier (a) log); ao: A.
struct NativeHitShade {float3 radiance,direct,sun,ambient,native,ambient_d,ambient_s,radiance_ao1;float ao;uint status,material_status;bool local_probes;
#ifdef NATIVE_HIT_BOX_TERM
 float3 box;   // the projector (box) part of direct, after its AO-on-direct factor (bias breakdown only)
#endif
#ifdef NATIVE_HIT_GI
 float3 gi;     // the indirect diffuse the screen's GI composite adds (bounce x diffuse colour x gi_strength)
 float3 dcol;   // the hit's diffuse colour (for the screen's GI term at the same pixel, rt_reflections.hlsl)
#endif
};

#include "native_hit_sun.hlsli"

// The game's local DIFFUSE probes over the sky irradiance (f469db78:994-1178, _1687): every
// probe listed in the hit's froxel with a diffuse cube ((m5 >> 1) & 1; cube pair from m5 bits
// 3-16 / 17-30, m6 is the specular pair) is parallax-corrected along N (box or sphere, as the
// specular loop along R), faded at its edge, sampled at mip 0, x m4 x diffuse colour x DFG.z at
// the parallax roughness, and lerped over the running term by weight cube x fade. Indoors this
// replaces the outdoor sky irradiance (live S8: every shaded hit lists local probes).
float3 native_hit_diffuse_probes(float3 D,float3 P,float3 N,float nov,float3 dcol,float a,uint4 cl){
 uint first=(cl.z&1023u)+((cl.y>>12)&4095u)+(cl.y&4095u)+cl.x,end=first+(cl.y>>24);
 [loop]for(uint k=first;k<end;++k){
  uint pi=bg3_light_index(k);
  if(!bg3_in(lgProbes,pi,144u))continue;
  uint b=pi*144u;
  float4 m0=asfloat(lgProbes.Load4(b));
  float4 r0=asfloat(lgProbes.Load4(b+16u)),r1=asfloat(lgProbes.Load4(b+32u)),r2=asfloat(lgProbes.Load4(b+48u));
  float m3=asfloat(lgProbes.Load(b+128u)),m4=asfloat(lgProbes.Load(b+132u));
  uint m5=lgProbes.Load(b+136u);
  bool box=(m5&1u)!=0u;
  float3 q=P-float3(r0.w,r1.w,r2.w);
  float3 p=float3(dot(r0.xyz,q),dot(r1.xyz,q),dot(r2.xyz,q));
  float3 ap=abs(p);float pp=dot(p,p);
  if(box?any(ap>=m0.xyz):(pp>=m0.y))continue;
  if((m5&2u)==0u)continue;   // (_1715 & 1): no diffuse cube
  float3 d=float3(dot(r0.xyz,N),dot(r1.xyz,N),dot(r2.xyz,N));
  float2 ts=0;bool hit;
  if(box){
   float3 t0=(m0.xyz-p)/d,t1=(-m0.xyz-p)/d;
   float3 tn=min(t0,t1),tf=max(t0,t1);
   ts=float2(max(tn.x,max(tn.y,tn.z)),min(tf.x,min(tf.y,tf.z)));hit=!(ts.x>ts.y);
  }else{
   float qa=dot(d,d),hb=dot(d,p),qb=2.0*hb,qc=pp-m0.y,disc=qb*qb-4.0*qa*qc;
   hit=disc>=0.0;float x0=0,x1=0;
   if(hit){
    if(disc==0.0){x0=x1=-hb/qa;}
    else{float qq=qb>0.0?-0.5*(qb+sqrt(disc)):-0.5*(qb-sqrt(disc));x0=qc/qq;x1=qq/qa;}
    hit=!(x1<0.0 && x0<0.0);ts=x1>x0?float2(x0,x1):float2(x1,x0);
   }
  }
  float3 X=p+d*ts.y;
  float fade=box?1.0-saturate(length(max(ap-(m0.xyz-m0.w),0.0))/max(m0.w,9.9999997e-05))
                :saturate((m0.x-length(p))/max(m0.w,9.9999997e-05));
  float w=(hit?1.0:0.0)*smoothstep(0.0,1.0,fade);
  if(!(w>9.9999997e-05))continue;   // the game lerps with weight 0
  int i0=(int((m5>>3)&16383u)-1)*2,i1=(int((m5>>17)&16383u)-1)*2;
  if(i0<0)continue;   // no cube: the game would index below the array; keep the running term
  float al=lerp(clamp(ts.y/length(X)*a,0.0,a),a,a);
  float3 dir=lerp(X,d,al);
  float4 c=bg3_probe_cube(uint(i0),dir,0.0);
  [branch]if(i1>=0)c=lerp(c,bg3_probe_cube(uint(i1),dir,0.0),m3);
  c*=m3<0.0?-m3:1.0;
  float3 rgb=c.rgb*m4*(dcol*bg3_dfg(nov,al).z);
  D=lerp(D,rgb,c.w*w);
 }
 return D;
}

// The game's diffuse + specular ambient at the hit (bg3_ambient.hlsli, f469db78:967-1344), at AO 1:
// diffuse d, specular s, both x b13[0]*100.
float3 native_hit_ambient(NativeHitMaterial material,float3 position,float3 outgoing,uint4 cluster,out bool local_probes,
                          out float3 d,out float3 s){
 local_probes=(cluster.y>>24)!=0u;d=0;s=0;
 if((pc.qa_zero&4u)!=0u)return 0;   // QA ambient floor: AO 0 removes the ambient
 const float3 N=material.normal;
 const float a=material.parameters.value.base_roughness;
 const float nov=max(dot(outgoing,N),0.001953125);
 float m27=lConsts.c[31].z,m28=lConsts.c[31].w,m29=lConsts.c[32].x;
 float3 irr=lgIrrCube0.SampleLevel(lgSampCube,N,0.0).rgb*m27;
 [branch] if(m29>0.0)irr=lerp(irr,lgIrrCube1.SampleLevel(lgSampCube,N,0.0).rgb*m28,m29);
 float3 diffuse=native_hit_diffuse_probes(irr*(material.parameters.value.diffuse_color*bg3_dfg(nov,a).z),position,N,nov,
                                          material.parameters.value.diffuse_color,a,cluster);
 float3 E;uint n_local;
 float3 specular=bg3_spec_ambient(position,N,outgoing,material.parameters.value.F0,a,cluster,E,n_local);
 const float k=asfloat(lScale.Load(0))*100.0;
 d=diffuse*k;s=specular*k;
 return d+s;
}
// Lagarde's specular occlusion as the lighting computes it from b6 (bg3_spec_occlusion), at AO A.
float native_hit_spec_occlusion(float A,float nov_raw,float a){
 if(!(lFlags.m0.x>0.0))return 1.0;
 return saturate(pow(max(abs(nov_raw+A),9.9999997e-05),exp2(-16.0*a-1.0))-1.0+A);
}

#ifdef NATIVE_HIT_GI
#include "../../common/hit_gi_compose.h"
// rf_hit_gi (docs/TRACK_D_REFLECTIONS.md 14 "Indirect diffuse at hits"): the hit gets the screen's
// indirect diffuse. The screen's diffuse at a point is the game's ambient x b6 (b6 = the hybrid GI's
// traced visibility, gi_ao_apply.hlsl) + the GI bounce x the diffuse colour (gi_composite.hlsl); the
// hit had only the first. Composition: common/hit_gi_compose.h (CPU test hit_gi_compose_test).
// Sources of A and the bounce:
//  - g_hg_use_screen (the tier (a) check with rf_hit_gi=2, a hit the camera sees at its pixel): the
//    screen's own b6 there and its GI history (gather_history), set by the caller: exactly the screen's
//    terms, so the check isolates the material / lighting replication;
//  - else the screen's estimator at the hit (native_hit_ao_gi): the AO rays (hit_ao_rays, gi_rt_radius,
//    cosine about the geometric normal) also take the GI trace's bounce rule (gi_trace_rt.hlsl with
//    gi_rt_fallback 0): a ray hit the screen shows (depth_same_surface x gi_rt_depth_k) adds last frame's
//    diffuse radiance there (prev_bounce_at) x the ray's weight; with SHaRC (SHARC_QUERY_PASS) a hidden /
//    off-screen ray hit the cache knows adds the cache's radiance; any other hit only occludes.
// Not reproduced: the GI's own hidden-hit shading (gi_hit_direct / gi_hit_sun, gi_far) and the screen's
// SHaRC radius split; both only add, so the hit's bounce is a lower bound of the screen's there.
static bool g_hg_use_screen=false;
static float4 g_hg_screen=0;   // rgb the GI history at the pixel, a its b6 (the folded AO the lighting read)
static uint g_hg_hits=0,g_hg_screen_src=0,g_hg_cache_src=0;   // AO-ray hits, of them with a screen / cache source

// Ray hit point ph: on screen and the same surface there (gi_trace_rt.hlsl's test, gi_rt_depth_k)?
// L: last frame's diffuse radiance at that pixel.
bool native_hit_gi_screen(float3x3 inv,float3 ph,out bool onscreen,out float3 L){
 L=0;float2 quv;float qd;
 onscreen=project(inv,ph,quv,qd) && all(quv>=0.0) && all(quv<=1.0);
 if(!onscreen)return false;
 const int2 qp=min(int2(quv*float2(pc.full)),int2(pc.full)-1);
 const float sd=gDepth.Load(int3(qp,0));
 const float4 oq=rt_origin_at(uint2(qp));
 const bool has_tlas=pc.rt_origin!=0u && oq.w<kOriginNoHit;
 const float3 ngq=pc.rt_origin!=0u?oq.xyz:depth_geo_normal(qp,ray_at((float2(qp)+0.5)/float2(pc.full))*sd);
 if(!(sd>0.0 && depth_same_surface(qp,qd,sd,ngq,has_tlas,oq.w,pc.gi_rt_tol)))return false;
 return prev_bounce_at(qp,L);
}

// native_hit_ao's estimator (same rays, same random stream, same weights: A is the same) plus the
// GI's bounce along those rays: mean L x w over the rays.
float native_hit_ao_gi(float3 position,float3 geometric_normal,float footprint,uint3 key,out float3 bounce){
 bounce=0;
 const uint rays=min(pc.rt_ao_rays,8u);
 if(rays==0u)return 1.0;
 const float3 n=normalize(geometric_normal);
 const float R=max(pc.gi_rt_radius,0.01);
 const float3 o=rt_offset_ray(position,n);
 RtCone cone={footprint,0};
 uint seed=pcg3d(key^uint3(0x510e527fu,0x9b05688cu,0x1f83d9abu)).x;
 const float3x3 inv=frustum_inverse();
 float occ=0;
 [loop]for(uint i=0;i<rays;++i){
  float3 dir=cosine_hemisphere(n,rand01(seed),rand01(seed));
  RtHit h=rt_closest_hit(o,dir,R,cone);
  const float w=hg_ray_weight(h.t,h.aborted,R);
  occ+=w;
  if(h.aborted || h.t<0.0)continue;   // unknown or miss: no radiance (the GI's rule)
  ++g_hg_hits;
  const float3 ph=o+dir*h.t;
  bool onscreen;float3 L;
  if(native_hit_gi_screen(inv,ph,onscreen,L)){bounce+=L*w;++g_hg_screen_src;continue;}
#if SHARC_QUERY_PASS
  if((shc.flags&(onscreen?1u:2u))!=0u){
   float3 Lc;bool near;SharcCell cell;
   if(sharc_ray_hit(h,ph,dir,h.t,sharc_cam(),cone,Lc,near,cell)){bounce+=Lc*w;++g_hg_cache_src;}
  }
#endif
 }
 bounce/=float(rays);
 if(!(lFlags.m0.x>0.0))return 1.0;   // the game does not use b6: AO 1 (the bounce still adds, as on screen)
 const float A=saturate(1.0-occ/float(rays));
 hd_pstat(2u,true);
 if(g_hd_stats && g_hd_pbase!=0u){
  uint v=WaveActiveSum(uint(A*8192.0+0.5));
  if(WaveIsFirstLane() && v)rtStats.InterlockedAdd(((g_hd_pbase+3u)*64u+(WaveReadLaneFirst(g_hd_bin)&63u))*4u,v);
 }
 return A;
}
#endif

// key: the pixel and frame, for the AO rays' random stream.
NativeHitShade native_hit_shade(RtHit hit,float3 position,float3 outgoing,float footprint,uint3 key){
 NativeHitShade r;r.radiance=0;r.direct=0;r.sun=0;r.ambient=0;r.native=0;r.ambient_d=0;r.ambient_s=0;r.radiance_ao1=0;r.ao=1;
 r.local_probes=false;r.status=kHitShadeMaterial;
#ifdef NATIVE_HIT_BOX_TERM
 r.box=0;
#endif
#ifdef NATIVE_HIT_GI
 r.gi=0;r.dcol=0;
#endif
 // Exact early-out (2026-11-01, cost): a hit outside the froxel grid (off screen, beyond the last
 // slice) can never be shaded: native_hit_direct_eval fails on the same native_hit_cluster at the
 // same native_hit_position. Test it before the material decode (3 VT layers), which every off-screen
 // hit on a qualified row paid (S6: ~60% of qualified decodes ended as "source"). Output identical;
 // only with the stats key off, so the reason counters (material vs lighting) stay as they were.
 if(!g_hd_stats){
  uint4 early;
  if(!native_hit_cluster(native_hit_position(hit,position),early)){r.status=kHitShadeDirect;r.material_status=1u;return r;}
 }
 NativeHitLit lit=native_hit_lit(hit,position,outgoing,footprint);r.material_status=lit.material.status;
 position=native_hit_position(hit,position);   // the same exact point native_hit_lit lit
 if(lit.material.status!=0u)return r;
 r.status=kHitShadeDirect;
 if(lit.direct.status!=HdOk)return r;
 uint4 cluster;if(!native_hit_cluster(position,cluster))return r;
 const bool qa_local=(pc.qa_zero&2u)!=0u;   // QA: local-light visibility 0
 HdDirect sun=native_hit_sun(lit.material,position,outgoing,rt_hit_normal(hit),footprint);
 r.status=kHitShadeSun;
 if(sun.status!=HdOk)return r;
 float3 amb_d,amb_s;
 native_hit_ambient(lit.material,position,outgoing,cluster,r.local_probes,amb_d,amb_s);
 r.radiance_ao1=(qa_local?0.0.xxx:lit.direct.radiance)+sun.radiance+amb_d+amb_s;
#ifdef RF_SPEC_HIT_GI
 // Keep the material colour outside the composition branch. Copying this float3
 // through its merged struct introduces a redundant vector shuffle after specialization.
 r.dcol=lit.material.parameters.value.diffuse_color;
#endif
#ifdef NATIVE_HIT_GI
#ifdef RF_SPEC_HIT_GI
 if(RF_GI_ON) {
#endif
 // A and the GI bounce from the screen at the hit's pixel or from the estimator, then the screen's
 // composition (common/hit_gi_compose.h); the box part as below.
 float3 bounce;
 if(g_hg_use_screen){r.ao=lFlags.m0.x>0.0?saturate(g_hg_screen.a):1.0;bounce=max(g_hg_screen.rgb,0.0);}
 else r.ao=native_hit_ao_gi(position,rt_hit_normal(hit),footprint,key,bounce);
 {
  HgIn hi;hi.amb_d=amb_d;hi.amb_s=amb_s;
  hi.traced=qa_local?0.0.xxx:lit.direct.radiance-lit.direct.native;hi.game=qa_local?0.0.xxx:lit.direct.native;
  hi.sun=sun.radiance;hi.dcol=lit.material.parameters.value.diffuse_color;
  hi.nov_raw=dot(outgoing,lit.material.normal);hi.a=lit.material.parameters.value.base_roughness;
  HgFrame hf;hf.m0x=lFlags.m0.x;hf.m0y=lFlags.m0.y;hf.ao_direct=pc.ao_direct;hf.strength=pc.strength;
  const HgOut ho=hg_compose(hi,hf,r.ao,bounce);
  r.direct=ho.direct;r.native=hi.game*ho.f;r.sun=ho.sun;r.ambient_d=ho.amb_d;r.ambient_s=ho.amb_s;
  r.ambient=r.ambient_d+r.ambient_s;r.gi=ho.gi;
#ifndef RF_SPEC_HIT_GI
  r.dcol=hi.dcol;
#endif
  r.radiance=ho.radiance;
#ifdef NATIVE_HIT_BOX_TERM
  r.box=qa_local?0.0.xxx:g_hd_box*(hd_box_game()?ho.f:((pc.ao_direct&3u)==1u?1.0:ho.f));
#endif
 }
#endif
#ifdef RF_SPEC_HIT_GI
 } else {
#endif
#if !defined(NATIVE_HIT_GI) || defined(RF_SPEC_HIT_GI)
 // The screen's AO at the hit, applied term by term as the lighting applies b6.
 r.ao=native_hit_ao(position,rt_hit_normal(hit),footprint,key);
 const float f=native_hit_ao_factor(r.ao);
 r.direct=qa_local?0.0.xxx:native_hit_ao_direct(lit.direct,r.ao);
 r.native=qa_local?0.0.xxx:lit.direct.native*f;
#ifdef NATIVE_HIT_BOX_TERM
 // As native_hit_ao_direct: box terms are in .native (x f) when the screen leaves them to the game.
 r.box=qa_local?0.0.xxx:g_hd_box*(hd_box_game()?f:((pc.ao_direct&3u)==1u?1.0:f));
#endif
 // The sun keeps mix(1, AO, m0.y) unless D-LS's ao_direct=1 gives it back (bit 2 its shadow ray
 // traced, bit 3 its inputs bound: rt_local_shadows.hlsl).
 r.sun=sun.radiance*((pc.ao_direct&3u)==1u && (pc.ao_direct&12u)==12u?1.0:f);
 r.ambient_d=amb_d*r.ao;
 r.ambient_s=amb_s*native_hit_spec_occlusion(r.ao,dot(outgoing,lit.material.normal),lit.material.parameters.value.base_roughness);
 r.ambient=r.ambient_d+r.ambient_s;
 r.radiance=r.direct+r.sun+r.ambient;
#endif
#ifdef RF_SPEC_HIT_GI
 }
#endif
 r.status=all(isfinite(r.radiance)) && all(r.radiance>=0) && all(isfinite(r.radiance_ao1))?kHitShadeOk:kHitShadeInvalid;
 return r;
}
#endif
