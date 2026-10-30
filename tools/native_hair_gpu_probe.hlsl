// Console fixture. Uses the ACTUAL production query and material functions.
struct ProbePC {uint frame,rt_stats,rt_max_cand,rt_origin;float rt_bias;uint pad0,pad1,pad2;};
[[vk::push_constant]] ProbePC pc;
uint3 pcg3d(uint3 v){v=v*1664525u+1013904223u;v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;v^=v>>16;v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;return v;}
#define RT_NATIVE_SCALP 1
#define RT_NATIVE_HAIR 1
#include "../shaders/rt/rt_common.hlsli"
[[vk::binding(21,0)]] RWByteAddressBuffer Result;
[numthreads(1,1,1)]
void main(){
 RtCone cone={0,1.0/1024.0};float3 origin=0.0.xxx,direction=float3(0,0,1);bool aborted;
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
}
