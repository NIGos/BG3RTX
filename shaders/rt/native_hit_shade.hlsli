#pragma once
// Shared hidden-hit shading: the qualified native material (native_hit_material.hlsli) and
// its RT-shadowed point/spot light (native_hit_direct.hlsli), the same two calls gi_hit_direct
// makes (gi_trace_rt.hlsl RT_HIT_GI_DIRECT, which keeps its inline calls so that its SPIR-V,
// under live qualification as M4.1, stays byte-identical; M4.3 moves it here). rf_hit_shade
// (rt_reflections.hlsl, RF_HIT_SHADE) adds the sun and the game's ambient at the hit
// (NATIVE_HIT_SHADE_FULL; docs/PLAN_M4_REFLECTIONS_GI.md M4.2).
// Needs rt_common.hlsli, native_hit_material.hlsli and native_hit_direct.hlsli first.
struct NativeHitLit {NativeHitMaterial material;NativeHitDirect direct;};
NativeHitLit native_hit_lit(RtHit hit,float3 position,float3 outgoing,float footprint){
 NativeHitLit r;r.material=rt_hit_material(hit,footprint);
 r.direct.radiance=0;r.direct.status=HdUnknownMaterial;r.direct.lights=0;
 if(r.material.status==0u)r.direct=native_hit_direct(r.material,position,outgoing,rt_hit_normal(hit),footprint);
 return r;
}

#if NATIVE_HIT_SHADE_FULL
// Also needs bg3_sun.hlsli (the D-LS sun set) and bg3_ambient.hlsli (the lighting's own set 1
// as set 2): its sky irradiance cubes b34/b36 (f469db78 _14/_16, read at N, mip 0, x _m27/_m28,
// blended by _m29) are the diffuse twin of the specular cubes b35/b37.
[[vk::binding(34, 2)]] TextureCube<float4> lgIrrCube0;
[[vk::binding(36, 2)]] TextureCube<float4> lgIrrCube1;

// L_b at the hit = point/spot (native_hit_direct) + sun (replicated colour x clouds x b13[0],
// one RT visibility ray instead of the native mask) + the game's ambient there with AO 1:
// diffuse (sky irradiance x diffuse colour x DFG.z) and specular (bg3_spec_ambient at the hit,
// its local probes included) x b13[0]*100. Not included: emission, projector lights (the hit
// is then unsupported), local DIFFUSE probes (counted: local_probes), aerial perspective.
// status 0 complete; 1 material unknown; 2 point/spot unsupported (outside the frustum,
// projector, unknown visibility); 3 sun unsupported; 4 not finite.
static const uint kHitShadeOk=0u,kHitShadeMaterial=1u,kHitShadeDirect=2u,kHitShadeSun=3u,kHitShadeInvalid=4u;
struct NativeHitShade {float3 radiance,direct,sun,ambient;uint status;bool local_probes;};

// The sun at a qualified hit, through the same declared BSDF as the point/spot term.
HdDirect native_hit_sun(NativeHitMaterial material,float3 position,float3 outgoing,float3 geometric_normal,float footprint){
 HdDirect r;r.radiance=0;r.status=HdOk;
 if((pc.qa_zero&1u)!=0u)return r;   // QA ambient floor: no direct sun (as rt_shadow.hlsl)
 const float3 L=normalize(cam.v0.xyz);
 HdIncident incident;incident.direction=L;incident.distance=0;incident.status=HdOk;
 incident.radiance=bg3_sun_colour()*(bg3_cloud_shadow(position)*asfloat(lScale.Load(0)));
 if(!all(isfinite(incident.radiance)) || any(incident.radiance<0)){r.status=HdInvalid;return r;}
 HdDirect term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,1);
 if(term.status!=HdOk || !any(term.radiance>0))return term;
 float3 normal=normalize(geometric_normal);
 if(!(dot(normal,L)>0)){r.status=HdNormalDomain;return r;}
 RtCone cone={footprint,0};
 uint visibility=rt_visibility(rt_offset_ray(position,normal),L,pc.rt_tmax,cone);
 if(visibility==kRtUnknown){r.status=HdUnknownVisibility;return r;}
 return hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
}

// The game's diffuse + specular ambient at the hit, AO 1 (bg3_ambient.hlsli, f469db78:967-1344).
float3 native_hit_ambient(NativeHitMaterial material,float3 position,float3 outgoing,uint4 cluster,out bool local_probes){
 local_probes=(cluster.y>>24)!=0u;
 if((pc.qa_zero&4u)!=0u)return 0;   // QA ambient floor: AO 0 removes the ambient
 const float3 N=material.normal;
 const float a=material.parameters.value.base_roughness;
 const float nov=max(dot(outgoing,N),0.001953125);
 float m27=lConsts.c[31].z,m28=lConsts.c[31].w,m29=lConsts.c[32].x;
 float3 irr=lgIrrCube0.SampleLevel(lgSampCube,N,0.0).rgb*m27;
 [branch] if(m29>0.0)irr=lerp(irr,lgIrrCube1.SampleLevel(lgSampCube,N,0.0).rgb*m28,m29);
 float3 diffuse=irr*(material.parameters.value.diffuse_color*bg3_dfg(nov,a).z);
 float3 E;uint n_local;
 float3 specular=bg3_spec_ambient(position,N,outgoing,material.parameters.value.F0,a,cluster,E,n_local);
 return (diffuse+specular)*(asfloat(lScale.Load(0))*100.0);
}

NativeHitShade native_hit_shade(RtHit hit,float3 position,float3 outgoing,float footprint){
 NativeHitShade r;r.radiance=0;r.direct=0;r.sun=0;r.ambient=0;r.local_probes=false;r.status=kHitShadeMaterial;
 NativeHitLit lit=native_hit_lit(hit,position,outgoing,footprint);
 if(lit.material.status!=0u)return r;
 r.status=kHitShadeDirect;
 if(lit.direct.status!=HdOk)return r;
 uint4 cluster;if(!native_hit_cluster(position,cluster))return r;
 r.direct=(pc.qa_zero&2u)!=0u?0.0.xxx:lit.direct.radiance;   // QA: local-light visibility 0
 HdDirect sun=native_hit_sun(lit.material,position,outgoing,rt_hit_normal(hit),footprint);
 r.status=kHitShadeSun;
 if(sun.status!=HdOk)return r;
 r.sun=sun.radiance;
 r.ambient=native_hit_ambient(lit.material,position,outgoing,cluster,r.local_probes);
 r.radiance=r.direct+r.sun+r.ambient;
 r.status=all(isfinite(r.radiance)) && all(r.radiance>=0)?kHitShadeOk:kHitShadeInvalid;
 return r;
}
#endif
