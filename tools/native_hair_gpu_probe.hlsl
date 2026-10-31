// Console fixture. Uses the ACTUAL production query and material functions.
struct ProbePC {uint frame,rt_stats,rt_max_cand,rt_origin;float rt_bias;uint pad0,pad1,pad2;};
[[vk::push_constant]] ProbePC pc;
uint3 pcg3d(uint3 v){v=v*1664525u+1013904223u;v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;v^=v>>16;v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;return v;}
#if defined(NATIVE_TILESET_PROBE) || defined(NATIVE_MATERIAL_PROBE)
#define RT_NATIVE_TILESET 1
#else
#define RT_NATIVE_SCALP 1
#define RT_NATIVE_HAIR 1
#endif
#ifdef NATIVE_MATERIAL_PROBE
#define RT_HIT_MATERIAL 1
#endif
#include "../shaders/rt/rt_common.hlsli"
#ifdef NATIVE_MATERIAL_PROBE
#include "../shaders/rt/native_hit_material.hlsli"
#endif
[[vk::binding(21,0)]] RWByteAddressBuffer Result;
[numthreads(1,1,1)]
void main(){
 RtCone cone={0,1.0/1024.0};float3 origin=0.0.xxx,direction=float3(0,0,1);bool aborted;
#ifdef NATIVE_MATERIAL_PROBE
 RtHit hit=rt_closest_hit(origin,direction,10,cone);
 NativeHitMaterial material=rt_hit_material(hit,cone.spread*max(hit.t,0));
 Result.Store4(0,uint4(material.status,asuint(hit.t),material.channel,material.family));
 Result.Store3(16,asuint(material.parameters.value.linear_albedo));
 Result.Store(28,asuint(material.parameters.value.perceptual_roughness));
 Result.Store2(32,asuint(float2(material.parameters.value.metalness,material.parameters.value.reflectance)));
 Result.Store3(40,asuint(material.normal));Result.Store(52,gHitRecords.Load(16));
 Result.Store4(56,gHitRecords.Load4(16u+48u));
#elif defined(NATIVE_TILESET_PROBE)
 Result.Store(0,rt_visibility(origin,direction,10,cone));
 float distance=rt_closest(origin,direction,10,cone,aborted);Result.Store(4,asuint(distance));Result.Store(8,uint(aborted));
 RtHit hit=rt_closest_hit(origin,direction,10,cone);Result.Store(12,asuint(hit.t));Result.Store(16,uint(hit.aborted));
 RtDebugHit debug=rt_closest_debug(origin,direction,10,cone);Result.Store(20,asuint(debug.t));Result.Store(24,uint(debug.aborted));
 Result.Store(28,uint(rt_scene_ok()));
 // Same texture, deliberately contrasting sampler: real kind5 uses binding19;
 // legacy wrap would sample alpha0 instead of native opaque-white border1.
 Result.Store(32,asuint(gAlphaTex[1].SampleLevel(gAlphaWrap,2.5.xx,0).a));
 Result.Store(36,asuint(gAlphaTex[1].SampleLevel(gNativeTilesetSampler[1],2.5.xx,0).a));
#else
 Result.Store(0,rt_visibility(origin,direction,10,cone));
 float distance=rt_closest(origin,direction,10,cone,aborted);Result.Store(4,asuint(distance));Result.Store(8,uint(aborted));
 RtHit physical=rt_closest_hit(origin,direction,10,cone);Result.Store(12,asuint(physical.t));Result.Store(16,uint(physical.aborted));
 Result.Store(20,physical.prim);
 RtHit primary=rt_closest_hit(origin,direction,10,cone,true);Result.Store(24,asuint(primary.t));Result.Store(28,uint(primary.aborted));
 RtDebugHit debug=rt_closest_debug(origin,direction,10,cone);Result.Store(32,asuint(debug.t));Result.Store(36,uint(debug.aborted));
 RtHit window=rt_closest_hit(float3(0,0,3.1),direction,1,cone,true);Result.Store(40,asuint(window.t));Result.Store(44,uint(window.aborted));
 RtHit secondary=rt_closest_hit(float3(0.2,0,0),direction,10,cone);Result.Store(48,asuint(secondary.t));Result.Store(52,uint(secondary.aborted));
 RtHit nonprimary=rt_closest_hit(float3(0.2,0,0),direction,10,cone,true);Result.Store(56,asuint(nonprimary.t));Result.Store(60,uint(nonprimary.aborted));
 Result.Store(64,rt_visibility(float3(-100,0,2),normalize(float3(100,0,1)),200,cone));
 Result.Store(68,uint(rt_scene_ok()));Result.Store(72,gSceneStamp.Load(12));
 Result.Store(76,uint(primary.t>=0 && !primary.aborted && alpha_instance(primary.inst,primary.geom)));
 float3 normal=physical.t>=0 && !physical.aborted?rt_hit_normal(physical):0.0.xxx;
 Result.Store3(80,asuint(normal));
 if(gAlphaRec.Load(16u+8u)==4u && gAlphaRec.Load(16u+196u)==1u) {
  // Test the actual new Pubes FMA branch with angular<1. Native equations are
  // evaluated independently below; the existing query cases still test geometry.
  g_rt_class=0;gNativeHair.valid=true;gNativeHair.physical=false;
  gNativeHair.p=float3(1,0,10);gNativeHair.n=float3(1,0,0);
  gNativeHair.t=float3(0,1,0);gNativeHair.b=float3(0,0,1);gNativeHair.fade=0;
  AlphaRead actual=native_hair_alpha(16u,0.5.xx,0,1,2);
  float4 mask=gAlphaTex[1].SampleLevel(gAlphaWrap,0.5.xx,0);
  float3 view=-(gNativeHair.p-normalize(gNativeHair.p)*(20.0*0.01));
  float3 v=normalize(view),nn=normalize(gNativeHair.n);
  float3 local=normalize(float3(dot(v,gNativeHair.b),dot(v,gNativeHair.n),dot(v,gNativeHair.t)));
  float3 world=normalize(gNativeHair.b*local.x+gNativeHair.n*local.y+gNativeHair.t*local.z);
  float angular=saturate(abs(dot(world,nn))*5.0);
  float4x4 vp=float4x4(asfloat(gAlphaRec.Load4(112u)),asfloat(gAlphaRec.Load4(128u)),
                       asfloat(gAlphaRec.Load4(144u)),asfloat(gAlphaRec.Load4(160u)));
  float4 clip=mul(float4(gNativeHair.p,1),vp),viewport=asfloat(gAlphaRec.Load4(176u));
  float2 raster=floor(viewport.xy+(float2(clip.x,-clip.y)/clip.w+1.0)*viewport.zw*0.5)+0.5;
  float2 frame=asfloat(gAlphaRec.Load2(48u));
  float phase=frame.x-4.0*trunc(frame.x/4.0);
  float noise=frac(dot(float2(0.833,0.1666),float2(frac(((mad(raster.y,2.0,raster.x)-1.5)+phase)*0.2),
                                                           frac(mad(raster.x,2.4084,raster.y*3.2535)))));
  float base=mask.w*mad(-mask.z,hair_param(16u,8u),1.0);
  float reference=frame.y!=0?mad(base,angular,noise-0.6667):base*angular;
  Result.Store(96,asuint(actual.a));Result.Store(100,asuint(reference));Result.Store(104,uint(actual.ok));
  precise float rounded=base*angular;
  Result.Store(108,asuint(frame.y!=0?mad(rounded,1.0,noise-0.6667):rounded));
 }
#endif
}
