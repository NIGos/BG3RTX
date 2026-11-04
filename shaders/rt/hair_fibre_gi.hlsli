// M4.1: only the GI composite's RT_HAIR_FIBRE_GI_REFERENCE variant includes this file.
#pragma once
#include "hair_dual.hlsli"
#include "hair_primary.hlsli"
[[vk::constant_id(1100)]] const uint kHvRecordLo = 0u;
[[vk::constant_id(1101)]] const uint kHvRecordHi = 0u;

// A strand pixel of this frame's G-buffer: an entry sealed with the applied header's tag (order mode applies the
// previous build's publish: its tag, not this frame's) at this pixel's depth.
bool hv_strand_pixel(uint2 fp, float d) {
  const uint64_t rec = (uint64_t(kHvRecordHi) << 32) | uint64_t(kHvRecordLo);
  if (rec == 0u) return false;
  const uint4 hdr = vk::RawBufferLoad<uint4>(rec, 16);
  if (hdr.x == 0u || hdr.w == 0u || any(fp >= hdr.yz)) return false;
  const uint2 addr = vk::RawBufferLoad<uint2>(rec + 32u, 8);
  const uint64_t pixels = (uint64_t(addr.y) << 32) | uint64_t(addr.x);
  const uint4 e = vk::RawBufferLoad<uint4>(pixels + uint64_t(fp.y * hdr.y + fp.x) * hair_primary_pixel_bytes, 16);
  if (e.w != hp_seal(e.xyz, hdr.x) || e.x == hair_primary_no_strand || (e.x >> 24) >= hdr.w) return false;
  if (abs(asfloat(e.z) - d) > 1e-6 * max(abs(d), 1e-6)) return false;
  const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + (e.x >> 24) * 16u, 16);
  const uint2 pool = vk::RawBufferLoad<uint2>(rec + 24u, 8);
  uint i0;
  return hp_segment((uint64_t(pool.y) << 32) | uint64_t(pool.x), vk::RawBufferLoad<uint>(rec + 56u, 4),
                    row, e.x & hair_primary_max_segment, i0);
}
