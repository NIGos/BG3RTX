// rt_hair_groom_primary fail-safe (layer/hair_primary.cpp): strand pixels per groom instance in the pixel record of the
// last frame (entries with that frame's tag). A matched draw whose strands drew no pixel gets its cards back.
[[vk::binding(0, 0)]] ByteAddressBuffer Pixels;     // hair primary pixel record (16 B entries)
[[vk::binding(1, 0)]] RWByteAddressBuffer Counts;   // 64 uints: strand pixels per groom instance k
struct Push { uint tag, n, pad0, pad1; };
[[vk::push_constant]] Push pc;

groupshared uint gsCount[64];

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi < 64u) gsCount[gi] = 0u;
  GroupMemoryBarrierWithGroupSync();
  if (id.x < pc.n) {
    const uint4 e = Pixels.Load4(id.x * 16u);
    if (e.w == pc.tag && e.x != 0xFFFFFFFFu && (e.x >> 24) < 64u) InterlockedAdd(gsCount[e.x >> 24], 1u);
  }
  GroupMemoryBarrierWithGroupSync();
  if (gi < 64u && gsCount[gi] != 0u) Counts.InterlockedAdd(gi * 4u, gsCount[gi]);
}
