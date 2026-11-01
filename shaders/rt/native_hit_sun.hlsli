#pragma once
// The sun at a qualified hidden hit, shared by rf_hit_shade (rt_reflections.hlsl RF_HIT_SHADE,
// through native_hit_shade.hlsli) and gi_hit_sun (gi_trace_rt.hlsl RT_HIT_GI_SUN): the replicated
// sun colour x cloud shadows x b13[0] (bg3_sun.hlsli), the same declared BSDF as the point/spot
// term (hd_scatter), one RT visibility ray instead of the game's mask (none exists at a hit).
// Needs bg3_sun.hlsli (BG3_SUN_SET) and native_hit_direct.hlsli first.
//
// NATIVE_HIT_SUN_ROOF (both consumers): the R0 roof rule. The TLAS holds no roof the camera cut
// away unless rt_cascade_casters puts the sun cascade's casters in it; the variant then has
// RT_CASCADE_MASKS and, with RT_CASCADE_SUN_HIT, its sun ray (only) sees them (mask 0x02,
// rt_common.hlsli, as rt_shadow.hlsl's sun ray). Without the casters, a hit whose sun ray
// passes through a roof volume (core/sun_trust.h, the sun pass's own test gi/sun_roof.hlsli,
// read from the sun set's b8) has an UNSUPPORTED sun (kHitSunRoof, counted): never lit through
// a roof the camera removed. So does every hit while the volumes are not known (no trust slot
// this frame: Push::ls_flags bit 25 clear; no asset DB roofs: header flag 2 clear). The consumer
// then keeps its old term (rf: the probe, tier c; GI: the point/spot term alone).
// NATIVE_HIT_SUN_DISC (gi_hit_sun): the ray samples the sun's disc as rt_shadow.hlsl does
// (uniform in the cone of half-angle sun_radius about L; the caller sets g_hs_seed per ray).
// Diagnostics (g_hd_stats) from NATIVE_HIT_SUN_STAT_BASE (rf_hit_shade 191-195, gi_hit_sun
// 320.., core/gi.cpp): +0 evaluated, +1 a zero incident (sun colour x clouds x b13[0]: below the
// horizon, night, full cloud), +2 a known zero after the cosine (N.L <= 0), +3 traced lit,
// +4 traced occluded; with the roof rule from NATIVE_HIT_SUN_XBASE (rf 329, GI 325): +0
// roof-unsupported, +1 unknown visibility.
#ifndef NATIVE_HIT_SUN_STAT_BASE
#define NATIVE_HIT_SUN_STAT_BASE 191u
#endif
void hs_sun_stat(uint k,bool c){if(g_hd_stats)stat_add(NATIVE_HIT_SUN_STAT_BASE+k,c,g_hd_bin);}
#ifdef NATIVE_HIT_SUN_ROOF
#ifndef NATIVE_HIT_SUN_XBASE
#define NATIVE_HIT_SUN_XBASE 329u
#endif
#define SUN_TRUST_BINDING 8
#define SUN_TRUST_SET BG3_SUN_SET
#include "../gi/sun_trust.hlsli"
#include "../gi/sun_roof.hlsli"
static const uint kHitSunRoof=5u;   // HdDirect.status: the sun is unsupported under a roof volume
void hs_sun_xstat(uint k,bool c){if(g_hd_stats)stat_add(NATIVE_HIT_SUN_XBASE+k,c,g_hd_bin);}
bool native_hit_sun_roof_ok(float3 position,float3 L){
#ifdef RT_CASCADE_MASKS
 return true;   // the cascade's casters hold the cut-away roofs; the sun ray sees them
#else
 if((pc.ls_flags&(1u<<25))==0u || (st_u(4)&2u)==0u)return false;   // roof volumes unknown
 return !sun_under_roof(position,L,cam2.c[13].xyz);
#endif
}
#endif
#ifdef NATIVE_HIT_SUN_DISC
static uint g_hs_seed=0u;
// rt_shadow.hlsl's disc sample (its tangent frame, sqrt(u1) x tan(sun_radius), phi = 2 pi u2).
float3 native_hit_sun_dir(float3 L){
 if(!(pc.sun_radius>0.0))return L;
 const float u1=rand01(g_hs_seed),u2=rand01(g_hs_seed);
 const float3 t=abs(L.y)<0.99?normalize(cross(float3(0,1,0),L)):normalize(cross(float3(0,0,1),L));   // orthonormal near the zenith (as RT_SUN_CURRENT)
 const float3 b=cross(L,t);
 const float rr=sqrt(u1)*tan(pc.sun_radius),phi=6.2831853*u2;
 return normalize(L+(t*cos(phi)+b*sin(phi))*rr);
}
#endif
// The sun at a qualified hit, through the same declared BSDF as the point/spot term.
HdDirect native_hit_sun(NativeHitMaterial material,float3 position,float3 outgoing,float3 geometric_normal,float footprint){
 HdDirect r;r.radiance=0;r.status=HdOk;
 if((pc.qa_zero&1u)!=0u)return r;   // QA ambient floor: no direct sun (as rt_shadow.hlsl)
 const float3 L=normalize(cam.v0.xyz);
 HdIncident incident;incident.direction=L;incident.distance=0;incident.status=HdOk;
 incident.radiance=bg3_sun_colour()*(bg3_cloud_shadow(position)*asfloat(lScale.Load(0)));
 if(!all(isfinite(incident.radiance)) || any(incident.radiance<0)){r.status=HdInvalid;return r;}
 HdDirect term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,1);
 hs_sun_stat(0u,true);hs_sun_stat(1u,!any(incident.radiance>0));
 hs_sun_stat(2u,term.status==HdOk && any(incident.radiance>0) && !any(term.radiance>0));
 if(term.status!=HdOk || !any(term.radiance>0))return term;
 float3 normal=normalize(geometric_normal);
 if(!(dot(normal,L)>0)){r.status=HdNormalDomain;return r;}
 RtCone cone={footprint,0};
#ifdef NATIVE_HIT_SUN_ROOF
 if(!native_hit_sun_roof_ok(position,L)){hs_sun_xstat(0u,true);r.status=kHitSunRoof;return r;}
#endif
#if defined(RT_CASCADE_SUN_HIT) && defined(RT_CASCADE_MASKS)
 g_rt_cascade_ray=true;
#endif
#ifdef NATIVE_HIT_SUN_DISC
 uint visibility=rt_visibility(rt_offset_ray(position,normal),native_hit_sun_dir(L),pc.rt_tmax,cone);
#else
 uint visibility=rt_visibility(rt_offset_ray(position,normal),L,pc.rt_tmax,cone);
#endif
#if defined(RT_CASCADE_SUN_HIT) && defined(RT_CASCADE_MASKS)
 g_rt_cascade_ray=false;
#endif
#ifdef NATIVE_HIT_SUN_ROOF
 hs_sun_xstat(1u,visibility==kRtUnknown);
#endif
 hs_sun_stat(3u,visibility==kRtLit);hs_sun_stat(4u,visibility==kRtOccluded);
 if(visibility==kRtUnknown){r.status=HdUnknownVisibility;return r;}
 return hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
}
