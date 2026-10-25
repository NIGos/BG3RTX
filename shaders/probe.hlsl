// Template compute shader for the hot-reload loop. Edit and save while the
// game runs: the host recompiles it and the core rebuilds its pipeline.
#include "common.hlsli"

[[vk::binding(0, 0)]] RWTexture2D<float4> Target;

struct Push { float4 tint; };
[[vk::push_constant]] Push pc;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint w, h;
  Target.GetDimensions(w, h);
  if (id.x >= w || id.y >= h) return;
  Target[id.xy] = lerp(Target[id.xy], pc.tint, pc.tint.a);
}
