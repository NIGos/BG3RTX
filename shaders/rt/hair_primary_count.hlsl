// rt_hair_groom_primary fail-safe (layer/hair_primary.cpp): strand pixels per groom instance in the pixel record of the
// last frame (entries with that frame's tag). A matched draw whose strands drew no pixel gets its cards back.
// Also the entries whose segment fails the strand readers' pool bound (hp_segment), per groom, at byte 256: the
// readers discard those pixels, the layer logs the count (the header still holds the counted frame's rows). At byte
// 512 the strand entries sealed with an adjacent frame's tag: a misaligned counted frame is no evidence for a strike.
#include "hair_primary.hlsli"
#include "hair_dual.hlsli"
[[vk::binding(0, 0)]] ByteAddressBuffer Pixels;     // hair primary pixel record (16 B entries)
[[vk::binding(1, 0)]] RWByteAddressBuffer Counts;   // 64 uints: strand pixels per groom instance k; 64: refusals
struct Push { uint tag, n, header_lo, header_hi; };   // header: the record's address (0: no refusal count)
[[vk::push_constant]] Push pc;

static const uint hair_primary_card_count_offset = 2896u; // common/hair_primary.h kCardCountOffset

groupshared uint gsCount[64];
groupshared uint gsRefused[64];
groupshared uint gsOther[64];
groupshared uint gsTraced;
groupshared uint gsHit;
groupshared uint gsCardCount[256];
groupshared uint gsTagCount[256];   // 4 tags (tag - j) x 64 grooms: common/hair_primary.h kTagCountOffset

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi < 64u) gsCount[gi] = 0u, gsRefused[gi] = 0u, gsOther[gi] = 0u;
  if (gi == 0u) gsTraced = 0u, gsHit = 0u;
  gsTagCount[gi] = 0u;
  gsCardCount[gi] = 0u;
  GroupMemoryBarrierWithGroupSync();
  const uint64_t rec = (uint64_t(pc.header_hi) << 32) | uint64_t(pc.header_lo);
  if (id.x < pc.n) {
    const uint4 e = Pixels.Load4(id.x * 16u);
    const uint2 mark = Pixels.Load2(pc.n * 16u + id.x * 8u);
    const uint slot = mark.x & 255u;
    for (uint j = 0u; j < 4u; ++j) {
      const uint tag = pc.tag - j;
      if (tag != 0u && slot < 16u && (mark.x >> 8u) == (tag & 0xFFFFFFu)) {
        InterlockedAdd(gsCardCount[j * 64u + slot], 1u);
        if (mark.y == mark.x) InterlockedAdd(gsTagCount[j * 64u + slot], 1u);
      }
    }
    if (e.w == hp_seal(e.xyz, pc.tag)) {   // diagnostics: entries the strand variant traced this frame, and with a strand
      InterlockedAdd(gsTraced, 1u);
      if (e.x != 0xFFFFFFFFu) InterlockedAdd(gsHit, 1u);
    }
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
    // An adjacent frame's tag (byte 512): the G-buffer of the counted frame ran against another publish.
    else if ((e.w == hp_seal(e.xyz, pc.tag - 1u) || e.w == hp_seal(e.xyz, pc.tag + 1u)) && e.x != 0xFFFFFFFFu &&
             (e.x >> 24) < 64u)
      InterlockedAdd(gsOther[e.x >> 24], 1u);
  }
  GroupMemoryBarrierWithGroupSync();
  if (gi < 64u && gsCount[gi] != 0u) Counts.InterlockedAdd(gi * 4u, gsCount[gi]);
  if (gi < 64u && gsRefused[gi] != 0u) Counts.InterlockedAdd(256u + gi * 4u, gsRefused[gi]);
  if (gi < 64u && gsOther[gi] != 0u) Counts.InterlockedAdd(512u + gi * 4u, gsOther[gi]);
  if (gsCardCount[gi] != 0u) Counts.InterlockedAdd(hair_primary_card_count_offset + gi * 4u, gsCardCount[gi]);
  if (gsTagCount[gi] != 0u) Counts.InterlockedAdd(1872u + gi * 4u, gsTagCount[gi]);
  if (gi == 0u && gsTraced != 0u) Counts.InterlockedAdd(1792u, gsTraced);   // common/hair_primary.h kTracedOffset
  if (gi == 0u && gsHit != 0u) Counts.InterlockedAdd(1796u, gsHit);
  if (id.x == 0u && rec != 0u)   // the sampled ray (kRaySampleOffset 1808 <- header kRayDebugOffset 3168)
    for (uint w = 0u; w < 4u; ++w) Counts.Store4(1808u + w * 16u, vk::RawBufferLoad<uint4>(rec + 3168u + w * 16u, 16));
  // C2: the strand colour sample of each groom written in the counted frame -> its forward-scattered fraction a_f
  // (the fibre pass's own table, hair_dual.hlsli), at counts byte 768 + 16 k (w = 1: measured).
  if (id.x < 64u && rec != 0u) {
    const uint64_t at = rec + hair_primary_colour_offset + id.x * 32u;
    const float4 s = asfloat(vk::RawBufferLoad<uint4>(at, 16));
    const bool ok = vk::RawBufferLoad<uint>(at + 16u, 4) == pc.tag && all(isfinite(s));
    const float3 a_f = ok ? hd_lookup3(saturate(s.rgb), max(0.09, s.w), 0u) : 0.0.xxx;
    Counts.Store4(768u + id.x * 16u, asuint(float4(a_f, ok ? 1.0 : 0.0)));
  }
}
