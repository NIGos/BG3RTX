// rf_debug=7: point/spot DIRECT-ONLY primary-hit reference, not normal RF.
// No ambient/sun/probe term or complete-radiance claim. Unknown keeps old output.
#define RT_HIT_MATERIAL 1
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/native_hit_material.hlsli"
#include "../rt/native_hit_direct.hlsli"
#ifdef RT_HIT_TWO_BOUNCE
#include "../rt/native_hit_bounce.hlsli"
void bounce_status(uint status,uint2 fp){if(pc.rt_stats!=0)stat_add(269u+min(status,11u),true,fp.x^fp.y);}
#endif
void material_sum(uint counter,uint value,uint bin){
 uint sum=WaveActiveSum(value);if(WaveIsFirstLane() && sum)rtStats.InterlockedAdd((counter*64u+(WaveReadLaneFirst(bin)&63u))*4u,sum);
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
 uint2 fp=id.xy;if(any(fp>=pc.full) || !hit_material_publication_ready())return;
 float depth=gDepth.Load(int3(fp,0));if(!(depth>0))return;
 float2 uv=(float2(fp)+.5)/float2(pc.full);float3 p=ray_at(uv)*depth;float dist=length(p);
 RtCone cone={0,pixel_angle(uv)};
#if defined(RT_NATIVE_HAIR) || defined(RT_NATIVE_SCALP)
 RtHit hit=rt_closest_hit(0.0.xxx,p/dist,dist*1.5+1,cone,true);
#else
 RtHit hit=rt_closest_hit(0.0.xxx,p/dist,dist*1.5+1,cone);
#endif
 NativeHitMaterial material=rt_hit_material(hit,cone.spread*max(hit.t,0));
 if(pc.rt_stats!=0){[unroll]for(uint status=0;status<10;++status)stat_add(253u+status,material.status==status,fp.x^fp.y);}
 if(material.status!=0){
#ifdef RT_HIT_TWO_BOUNCE
  bounce_status(HbPathPrimaryMaterial,fp);
#endif
  return;
 }
#ifdef RT_HIT_TWO_BOUNCE
 // This reference starts on the actual raster primary, not a different layer
 // behind it. The tolerance is the existing native encoding/footprint bound.
 float3 ng=rt_hit_normal(hit);float nl=length(ng);
 float tolerance=depth_tol(dist,cone.spread,dot(ng/max(nl,1e-30),p)/dist,kJitterPx,1.0)+kTlasRel*dist;
 if(mat_class(gMaterial.Load(int3(fp,0)))!=0 || !(nl>0) || !all(isfinite(ng)) || abs(hit.t-dist)>tolerance){bounce_status(HbPathPrimaryMismatch,fp);return;}
#endif
 if(pc.rt_stats!=0){
  float3 ng=normalize(rt_hit_normal(hit));float tol=depth_tol(dist,cone.spread,dot(ng,p)/dist,kJitterPx,1.0)+kTlasRel*dist;
  bool matched=mat_class(gMaterial.Load(int3(fp,0)))==0 && abs(hit.t-dist)<=tol;
  float3 alb=gAlbedo.Load(int3(fp,0)).rgb;float3 physical=gMaterial.Load(int3(fp,0)).xyz;
  float3 normal=to_lighting_space(decode_normal(gNormal.Load(int3(fp,0)).xy));
  matched=matched && all(isfinite(alb)) && all(isfinite(physical)) && all(isfinite(normal));
  stat_add(263u,matched,fp.x^fp.y);
  float3 da=abs(alb-material.parameters.value.linear_albedo);
  float errors[5]={max(da.x,max(da.y,da.z)),length(normal-material.normal),abs(physical.x-material.parameters.value.perceptual_roughness),
    abs(physical.y-material.parameters.value.reflectance),abs(physical.z-material.parameters.value.metalness)};
  [unroll]for(uint i=0;i<5;++i){
   // Raw residual sums quantized1/256; not an equality gate. The declared cone
   // derivatives differ from native raster quad derivatives/mip selection.
   material_sum(264u+i,matched?uint(round(errors[i]*256.0)):0u,fp.x^fp.y);
  }
 }
 NativeHitDirect direct=native_hit_direct(material,(p/dist)*hit.t,-p/dist,rt_hit_normal(hit),cone.spread*hit.t);
 if(direct.status!=HdOk){
#ifdef RT_HIT_TWO_BOUNCE
  bounce_status(HbPathPrimaryDirect,fp);
#endif
  return;
 }
 // This is an explicit direct-only view; the normal RF/probe delta is unchanged.
#ifdef RT_HIT_TWO_BOUNCE
 RtCone bounce_cone={cone.spread*hit.t,cone.spread};
 NativeHitBounce bounce=native_hit_bounce(fp,material,(p/dist)*hit.t,-p/dist,rt_hit_normal(hit),bounce_cone,direct.radiance);
 bounce_status(bounce.status,fp);
 if(pc.rt_stats!=0)stat_add(281u,bounce.status==HbPathComplete && bounce.positive_indirect,fp.x^fp.y);
 gDiffuse[fp]=bounce.radiance; // Incomplete paths retain direct prefix, never a completed estimate.
#else
 gDiffuse[fp]=direct.radiance;
#endif
 gSpecular[fp]=0;
}
