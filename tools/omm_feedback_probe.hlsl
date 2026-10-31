// Existing console probe binding wrapper for the ACTUAL production guard.
[[vk::binding(9,1)]] globallycoherent RWByteAddressBuffer gSceneStamp;
#include "../shaders/rt/omm_scene_guard.hlsli"
[[vk::binding(12,1)]] RWStructuredBuffer<uint> result;
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 result[id.x]=rt_omm_scene_current()?1u:0u;
}
