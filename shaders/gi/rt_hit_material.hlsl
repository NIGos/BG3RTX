// Full qualified VT -> native parameters -> declared FresnelBlend provider view.
// rt_debug9 only; unsupported hits retain the existing renderer output.
#define RT_HIT_MATERIAL 1
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/native_hit_material.hlsli"
void material_sum(uint counter,uint value,uint bin){
 uint sum=WaveActiveSum(value);if(WaveIsFirstLane() && sum)rtStats.InterlockedAdd((counter*64u+(WaveReadLaneFirst(bin)&63u))*4u,sum);
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
 uint2 fp=id.xy;if(any(fp>=pc.full))return;
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
 if(material.status!=0)return;
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
 // No guessed lighting is installed. The same full result is available to later
 // shaded-hit consumers; this view only demonstrates actual runtime acquisition.
 gDiffuse[fp]=material.parameters.value.linear_albedo*.5;
 gSpecular[fp]=0;
}
