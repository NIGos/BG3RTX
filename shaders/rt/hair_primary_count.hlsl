// rt_hair_groom_primary fail-safe (layer/hair_primary.cpp): strand pixels per groom instance in the pixel record of the
// last frame (entries with that frame's tag). A matched draw whose strands drew no pixel gets its cards back.
// Also the entries whose segment fails the strand readers' pool bound (hp_segment), per groom, at byte 256: the
// readers discard those pixels, the layer logs the count (the header still holds the counted frame's rows).
#include "hair_primary.hlsli"
[[vk::binding(0, 0)]] ByteAddressBuffer Pixels;     // hair primary pixel record (16 B entries)
[[vk::binding(1, 0)]] RWByteAddressBuffer Counts;   // 64 uints: strand pixels per groom instance k; 64: refusals
struct Push { uint tag, n, header_lo, header_hi; };   // header: the record's address (0: no refusal count)
[[vk::push_constant]] Push pc;

groupshared uint gsCount[64];
groupshared uint gsRefused[64];

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi < 64u) gsCount[gi] = 0u, gsRefused[gi] = 0u;
  GroupMemoryBarrierWithGroupSync();
  const uint64_t rec = (uint64_t(pc.header_hi) << 32) | uint64_t(pc.header_lo);
  if (id.x < pc.n) {
    const uint4 e = Pixels.Load4(id.x * 16u);
    if (e.w == hp_seal(e.xyz, pc.tag) && e.x != 0xFFFFFFFFu && (e.x >> 24) < 64u) {
      const uint k = e.x >> 24;
      InterlockedAdd(gsCount[k], 1u);
      if (rec != 0u) {
        const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
        const uint64_t pool = (uint64_t(vk::RawBufferLoad<uint>(rec + 28u, 4)) << 32) | uint64_t(vk::RawBufferLoad<uint>(rec + 24u, 4));
        uint i0;
        if (!hp_segment(pool, vk::RawBufferLoad<uint>(rec + 56u, 4), row, e.x & hair_primary_max_segment, i0))
          InterlockedAdd(gsRefused[k], 1u);
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();
  if (gi < 64u && gsCount[gi] != 0u) Counts.InterlockedAdd(gi * 4u, gsCount[gi]);
  if (gi < 64u && gsRefused[gi] != 0u) Counts.InterlockedAdd(256u + gi * 4u, gsRefused[gi]);
}
