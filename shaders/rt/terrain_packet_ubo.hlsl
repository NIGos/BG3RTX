// Exact opt-in terrain packet readback. Native UBO has 80 bytes, no TRANSFER_SRC.
struct TerrainUniform { uint4 words[5]; };
[[vk::binding(0,0)]] ConstantBuffer<TerrainUniform> source;
[[vk::binding(1,0)]] RWByteAddressBuffer destination;
[[vk::push_constant]] struct Push { uint words; } push;
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
 if(id.x<push.words && id.x<20u)
  destination.Store(id.x*4u,source.words[id.x>>2u][id.x&3u]);
}
