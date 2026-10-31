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
// one RT visibility ray instead of the native mask) + the game's ambient there with AO 1:
// diffuse (sky irradiance x diffuse colour x DFG.z) and specular (bg3_spec_ambient at the hit,
// its local probes included) x b13[0]*100. Not included: emission, projector lights (the hit
// is then unsupported), local DIFFUSE probes (counted: local_probes), aerial perspective.
// status 0 complete; 1 material unknown; 2 point/spot unsupported (outside the frustum,
// projector, unknown visibility); 3 sun unsupported; 4 not finite.
static const uint kHitShadeOk=0u,kHitShadeMaterial=1u,kHitShadeDirect=2u,kHitShadeSun=3u,kHitShadeInvalid=4u;
struct NativeHitShade {float3 radiance,direct,sun,ambient;uint status,material_status;bool local_probes;};   // material_status: rt_hit_material's

// Sun diagnostics (g_hd_stats; counters 191-195, core/gi.cpp): evaluated, a zero incident
// (sun colour x clouds x b13[0]: below the horizon, night, full cloud), a known zero after the
// cosine (N.L <= 0), traced lit, traced occluded.
void hs_sun_stat(uint k,bool c){if(g_hd_stats)stat_add(191u+k,c,g_hd_bin);}
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
 uint visibility=rt_visibility(rt_offset_ray(position,normal),L,pc.rt_tmax,cone);
 hs_sun_stat(3u,visibility==kRtLit);hs_sun_stat(4u,visibility==kRtOccluded);
 if(visibility==kRtUnknown){r.status=HdUnknownVisibility;return r;}
 return hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
}

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
 float3 diffuse=native_hit_diffuse_probes(irr*(material.parameters.value.diffuse_color*bg3_dfg(nov,a).z),position,N,nov,
                                          material.parameters.value.diffuse_color,a,cluster);
 float3 E;uint n_local;
 float3 specular=bg3_spec_ambient(position,N,outgoing,material.parameters.value.F0,a,cluster,E,n_local);
 return (diffuse+specular)*(asfloat(lScale.Load(0))*100.0);
}

NativeHitShade native_hit_shade(RtHit hit,float3 position,float3 outgoing,float footprint){
 NativeHitShade r;r.radiance=0;r.direct=0;r.sun=0;r.ambient=0;r.local_probes=false;r.status=kHitShadeMaterial;
 NativeHitLit lit=native_hit_lit(hit,position,outgoing,footprint);r.material_status=lit.material.status;
 position=native_hit_position(hit,position);   // the same exact point native_hit_lit lit
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
