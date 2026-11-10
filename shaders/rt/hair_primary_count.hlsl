// rt_hair_groom_primary fail-safe (layer/hair_primary.cpp): strand pixels per groom instance in the pixel record of the
// last frame (entries with that frame's tag). A matched draw whose strands drew no pixel gets its cards back.
// Also the entries whose segment fails the strand readers' pool bound (hp_segment), per groom, at byte 256: the
// readers discard those pixels, the layer logs the count (the header still holds the counted frame's rows). At byte
// 512 the strand entries sealed with an adjacent frame's tag: a misaligned counted frame is no evidence for a strike.
// Review 2026-11-08 (items 3/4): with the current-frame header this count runs before this frame's G-buffer, so the
// frame it judges is tag - 1. Traced/hit, strand pixels, pool refusals, the C2 colour samples and the mask-hole flags
// are counted for the same four tag windows as the markers (common/hair_primary.h kTagTracedOffset ...).
#include "hair_primary.hlsli"
#include "hair_dual.hlsli"
[[vk::binding(0, 0)]] ByteAddressBuffer Pixels;     // hair primary pixel record (16 B entries)
[[vk::binding(1, 0)]] RWByteAddressBuffer Counts;   // 64 uints: strand pixels per groom instance k; 64: refusals
struct Push { uint tag, n, header_lo, header_hi; };   // header: the record's address (0: no refusal count)
[[vk::push_constant]] Push pc;

static const uint hair_primary_card_count_offset = 2896u; // common/hair_primary.h kCardCountOffset
static const uint hair_primary_tag_traced_offset = 3920u; // kTagTracedOffset (4 x traced, hit)
static const uint hair_primary_tag_pixel_offset = 3952u;  // kTagPixelOffset (4 x 64)
static const uint hair_primary_tag_refused_offset = 4976u; // kTagRefusedOffset (4 x 64)
static const uint hair_primary_mask_hole_offset = 6000u;  // kMaskHoleOffset (4)
static const uint hair_primary_marker_skipped = 0x20u;    // kMarkerSkipped

groupshared uint gsCount[64];
groupshared uint gsRefused[64];
groupshared uint gsOther[64];
groupshared uint gsTraced;
groupshared uint gsHit;
groupshared uint gsCardCount[256];
groupshared uint gsTagCount[256];   // 4 tags (tag - j) x 64 grooms: common/hair_primary.h kTagCountOffset
groupshared uint gsTagPixels[256];
groupshared uint gsTagRefused[256];
groupshared uint gsTagTraced[8];    // per tag window: traced, hit
groupshared uint gsHoles[4];
#ifdef HP_COUNT_UNION
// 5u, rt_hair_coverage_union (request rt/hair_primary_count.hlsl?HP_COUNT_UNION=1): per tag window and draw slot every
// accepted strand pixel, whatever the card marker of the pixel holds (common/hair_primary.h kUnionCountOffset).
static const uint hair_primary_union_count_offset = 6016u;
groupshared uint gsUnion[256];
#endif

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
#ifdef HP_COUNT_UNION
  gsUnion[gi] = 0u;
#endif
  if (gi < 64u) gsCount[gi] = 0u, gsRefused[gi] = 0u, gsOther[gi] = 0u;
  if (gi == 0u) gsTraced = 0u, gsHit = 0u;
  if (gi < 8u) gsTagTraced[gi] = 0u;
  if (gi < 4u) gsHoles[gi] = 0u;
  gsTagCount[gi] = 0u;
  gsCardCount[gi] = 0u;
  gsTagPixels[gi] = 0u;
  gsTagRefused[gi] = 0u;
  GroupMemoryBarrierWithGroupSync();
  const uint64_t rec = (uint64_t(pc.header_hi) << 32) | uint64_t(pc.header_lo);
  // The header in the buffer is the last one applied on the GPU: its rows are the frame with that tag's.
  const uint header_tag = rec != 0u ? vk::RawBufferLoad<uint>(rec, 4) : 0u;
  if (id.x < pc.n) {
    const uint4 e = Pixels.Load4(id.x * 16u);
    const uint2 mark = Pixels.Load2(pc.n * 16u + id.x * 8u);
    const uint slot = mark.x & 15u;
    const uint card_word = mark.x & ~hair_primary_marker_skipped;
    for (uint j = 0u; j < 4u; ++j) {
      const uint tag = pc.tag - j;
      if ((tag & 0xFFFFFFu) != 0u && (mark.x & 0xD0u) == 0u && (mark.x >> 8u) == (tag & 0xFFFFFFu)) {
        InterlockedAdd(gsCardCount[j * 64u + slot], 1u);
        if (mark.y == card_word) InterlockedAdd(gsTagCount[j * 64u + slot], 1u);
        if ((mark.x & hair_primary_marker_skipped) != 0u) InterlockedAdd(gsHoles[j], 1u);
      }
#ifdef HP_COUNT_UNION
      if ((tag & 0xFFFFFFu) != 0u && (mark.y & 0xF0u) == 0u && (mark.y >> 8u) == (tag & 0xFFFFFFu))
        InterlockedAdd(gsUnion[j * 64u + (mark.y & 15u)], 1u);
#endif
      // The entries of every tag window (the judged frame is the newest one with markers, often tag - 1).
      if (e.w == hp_seal(e.xyz, tag)) {
        InterlockedAdd(gsTagTraced[j * 2u], 1u);
        if (e.x != 0xFFFFFFFFu) {
          InterlockedAdd(gsTagTraced[j * 2u + 1u], 1u);
          if ((e.x >> 24) < 64u) {
            const uint k = e.x >> 24;
            InterlockedAdd(gsTagPixels[j * 64u + k], 1u);
            if (rec != 0u && tag == header_tag) {
              const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
              const uint64_t pool = (uint64_t(vk::RawBufferLoad<uint>(rec + 28u, 4)) << 32) | uint64_t(vk::RawBufferLoad<uint>(rec + 24u, 4));
              uint i0;
              if (!hp_segment(pool, vk::RawBufferLoad<uint>(rec + 56u, 4), row, e.x & hair_primary_max_segment, i0))
                InterlockedAdd(gsTagRefused[j * 64u + k], 1u);
            }
          }
        }
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
  if (gsTagPixels[gi] != 0u) Counts.InterlockedAdd(hair_primary_tag_pixel_offset + gi * 4u, gsTagPixels[gi]);
  if (gsTagRefused[gi] != 0u) Counts.InterlockedAdd(hair_primary_tag_refused_offset + gi * 4u, gsTagRefused[gi]);
  if (gi < 8u && gsTagTraced[gi] != 0u) Counts.InterlockedAdd(hair_primary_tag_traced_offset + gi * 4u, gsTagTraced[gi]);
  if (gi < 4u && gsHoles[gi] != 0u) Counts.InterlockedAdd(hair_primary_mask_hole_offset + gi * 4u, gsHoles[gi]);
#ifdef HP_COUNT_UNION
  if (gsUnion[gi] != 0u) Counts.InterlockedAdd(hair_primary_union_count_offset + gi * 4u, gsUnion[gi]);
#endif
  if (gi == 0u && gsTraced != 0u) Counts.InterlockedAdd(1792u, gsTraced);   // common/hair_primary.h kTracedOffset
  if (gi == 0u && gsHit != 0u) Counts.InterlockedAdd(1796u, gsHit);
  if (id.x == 0u && rec != 0u)   // the sampled ray (kRaySampleOffset 1808 <- header kRayDebugOffset 3168)
    for (uint w = 0u; w < 4u; ++w) Counts.Store4(1808u + w * 16u, vk::RawBufferLoad<uint4>(rec + 3168u + w * 16u, 16));
  // C2: the strand colour sample of each groom written in one of the four tag windows -> its forward-scattered fraction
  // a_f (the fibre pass's own table, hair_dual.hlsli), at counts byte 768 + 16 k; w = j + 1 for the window tag - j (the
  // layer keeps only the judged frame's window and maps groom k with that frame's rows), 0: none.
  if (id.x < 64u && rec != 0u) {
    const uint64_t at = rec + hair_primary_colour_offset + id.x * 32u;
    const float4 s = asfloat(vk::RawBufferLoad<uint4>(at, 16));
    const uint stag = vk::RawBufferLoad<uint>(at + 16u, 4);
    const uint j = pc.tag - stag;
    const bool ok = stag != 0u && j < 4u && all(isfinite(s));
    const float3 a_f = ok ? hd_lookup3(saturate(s.rgb), max(0.09, s.w), 0u) : 0.0.xxx;
    Counts.Store4(768u + id.x * 16u, asuint(float4(a_f, ok ? float(j + 1u) : 0.0)));
  }
}
