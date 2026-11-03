// Primary strand hair (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): what the strand variants of the
// matched hair draws write. Math twin of tools/hair_groom/primary.py (CTest hair_groom_test, test_primary checks
// the encodings through the game's decoders and the depth formula against the explicit projection).
#pragma once

static const uint kHairPrimaryClass = 5u;   // RT2.w bits 0-2: Marschner hair (3033c895)

// RT0 (RG16F): stereographic view-space normal, as the G-buffer FS aa4efa4d writes it.
float2 hp_stereo_encode(float3 n) { return n.xy / max(1.0 - n.z, 1e-7) * 0.28125 + 0.5; }

// RT3.xyz (RGBA8 UNORM): 12:12 octahedral unit vector, the inverse of bg3_oct12 (bg3_materials.hlsli).
float3 hp_oct12_encode(float3 v) {
  const float3 p = v / (abs(v.x) + abs(v.y) + abs(v.z));
  const float2 sg = float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
  const float2 e = p.z >= 0.0 ? p.xy : (1.0 - abs(p.yx)) * sg;
  const uint2 q = uint2(clamp(round((e + 1.0) * 2047.0), 0.0, 4095.0));
  return float3(q.x >> 4, ((q.x & 15u) << 4) | (q.y >> 8), q.y & 255u) / 255.0;
}

// RT2.w (UNORM8): class bits 0-2, RG16F-copy flag bit 3, light-channel mask bits 4-6.
float hp_rt2_w(uint cls, uint light_mask, bool copy_flag) {
  return float((cls & 7u) | (copy_flag ? 8u : 0u) | ((light_mask & 7u) << 4)) / 255.0;
}

// Fibre shading normal: the direction to the camera made perpendicular to the tangent.
float3 hp_fibre_normal(float3 t, float3 to_camera) {
  const float3 n = to_camera - t * dot(to_camera, t);
  return n * rsqrt(max(dot(n, n), 1e-24));
}

// Native perspective depth D = A + B / w (A = near / (near - far)): on one camera ray w is proportional to the
// distance, so the strand hit at t_s on the card fragment's ray (card depth D_c at t_c) has this depth.
float hp_strand_depth(float d_card, float t_card, float t_strand, float a) {
  return a + (d_card - a) * (t_card / t_strand);
}
// Orthographic: D affine in the distance along the parallel ray.
float hp_strand_depth_ortho(float d_card, float t_card, float t_strand, float dd_dt) {
  return d_card + (t_strand - t_card) * dd_dt;
}

// The frame record (common/hair_primary.h): table offset, pixel entry size, the no-strand key, the segment bits.
static const uint hair_primary_table_offset = 64u;
static const uint hair_primary_pixel_bytes = 16u;
static const uint hair_primary_no_strand = 0xFFFFFFFFu;
static const uint hair_primary_max_segment = 0x00FFFFFFu;
static const uint hair_primary_colour_offset = 1088u;   // common/hair_primary.h kColourOffset (32 bytes per groom)
// The first point i0 of segment `seg` of groom table row `row` (mesh-pool byte offsets of the points, point UVs, segment
// starts, previous points; common/hair_primary.h), when every pool byte the strand readers touch for that segment lies
// inside the mesh pool (pool_bytes: header word 14). A pixel entry of another groom, LOD or frame then fails closed
// (2026-11-03 19:53: a strand read at pool + 3.9 GB, past the 2 GB pool, lost the device). 64-bit offsets: i0 * 12 of a
// garbage i0 wrapped in 32 bits.
bool hp_pool_ok(uint64_t off, uint64_t bytes, uint pool_bytes) { return off + bytes <= uint64_t(pool_bytes); }
bool hp_segment(uint64_t pool, uint pool_bytes, uint4 row, uint seg, out uint i0) {
  i0 = 0u;
  const uint64_t s = uint64_t(row.z) + uint64_t(seg) * 4u;
  if (!hp_pool_ok(s, 4u, pool_bytes)) return false;
  i0 = vk::RawBufferLoad<uint>(pool + s, 4);
  return hp_pool_ok(uint64_t(row.x) + uint64_t(i0) * 12u, 24u, pool_bytes) &&
         (row.y == 0u || hp_pool_ok(uint64_t(row.y) + uint64_t(i0) * 8u, 16u, pool_bytes)) &&
         (row.w == 0u || hp_pool_ok(uint64_t(row.w) + uint64_t(i0) * 12u, 24u, pool_bytes));
}
// Byte address of element i (size bytes) of the pool array at offset off (after hp_segment accepted i0).
uint64_t hp_at(uint64_t pool, uint off, uint i, uint size) { return pool + uint64_t(off) + uint64_t(i) * size; }

// Strand crossings of one ray from the candidates of a non-opaque groom query (arbitrary order): a strand's chained
// segments meet in one joint that both report, so a candidate whose neighbour segment (same instance, primitive +- 1)
// was already seen is the same crossing. The last kHpSeen candidates are kept (a shadow ray crosses few strands).
static const uint kHpSeen = 8u;
struct HpCrossings { uint inst[kHpSeen]; uint prim[kHpSeen]; uint n; };
HpCrossings hp_crossings_init() { HpCrossings c = (HpCrossings)0; c.n = 0u; return c; }
bool hp_crossing(inout HpCrossings c, uint inst, uint prim) {
  bool fresh = true;
  [unroll] for (uint i = 0; i < kHpSeen; ++i)
    if (i < min(c.n, kHpSeen) && c.inst[i] == inst && (c.prim[i] + 1u == prim || prim + 1u == c.prim[i])) fresh = false;
  const uint slot = c.n % kHpSeen;
  c.inst[slot] = inst;
  c.prim[slot] = prim;
  ++c.n;
  return fresh;
}

// Largest strand radius a groom file may hold (common/hair_groom_asset.h rejects radii >= 10 mm).
static const float hair_primary_max_radius = 0.01;

// Pixel entry seal (word 3): the frame tag mixed with the other three words, so a reader never pairs this frame's tag
// with the words of an older entry (the 16-byte stores of a pixel are not atomic). Bit 31 set: a zeroed entry never
// matches.
uint hp_mix(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
uint hp_seal(uint3 e, uint tag) { return (tag ^ hp_mix(e.x ^ hp_mix(e.y ^ hp_mix(e.z)))) | 0x80000000u; }

// Inverse of a 4x4 matrix (the card fragment's camera-relative point from the jittered view-projection).
float4x4 hp_inverse4(float4x4 m) {
  const float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3];
  const float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3];
  const float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3];
  const float a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3];
  const float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10;
  const float b03 = a01 * a12 - a02 * a11, b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
  const float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30, b08 = a20 * a33 - a23 * a30;
  const float b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
  const float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
  const float s = 1.0 / det;
  return float4x4(
      (a11 * b11 - a12 * b10 + a13 * b09) * s, (a02 * b10 - a01 * b11 - a03 * b09) * s,
      (a31 * b05 - a32 * b04 + a33 * b03) * s, (a22 * b04 - a21 * b05 - a23 * b03) * s,
      (a12 * b08 - a10 * b11 - a13 * b07) * s, (a00 * b11 - a02 * b08 + a03 * b07) * s,
      (a32 * b02 - a30 * b05 - a33 * b01) * s, (a20 * b05 - a22 * b02 + a23 * b01) * s,
      (a10 * b10 - a11 * b08 + a13 * b06) * s, (a01 * b08 - a00 * b10 - a03 * b06) * s,
      (a30 * b04 - a31 * b02 + a33 * b00) * s, (a21 * b02 - a20 * b04 - a23 * b00) * s,
      (a11 * b07 - a10 * b09 - a12 * b06) * s, (a00 * b09 - a01 * b07 + a02 * b06) * s,
      (a31 * b01 - a30 * b03 - a32 * b00) * s, (a20 * b03 - a21 * b01 + a22 * b00) * s);
}

// Strand ownership of a card fragment (overlapping grooms, layered cards). Every strand point lies on its groom's
// card triangles (hair_groom_update.hlsl), so a point of segment p0 -> p1 is within |p1 - p0| / 2 of the card surface
// and the capsule's surface within the radius more. Along the fragment's ray (card surface at t_card; cos_card: |cos|
// of the ray against the card normal) that is a band of that distance / cos_card, plus the card's own distance spread
// over the pixel (foot). A hit outside the band belongs to another card: that card's own fragment writes it.
bool hp_on_card(float t_card, float t_hit, float cos_card, float foot, float seg_len) {
  return abs(t_hit - t_card) <= (hair_primary_max_radius + 0.5 * seg_len) / max(cos_card, 1e-6) + foot;
}

// Parameter in [0, 1] of the point of segment p0 -> p1 closest to `hit` (the strand's UV and motion are
// interpolated with it).
float hp_segment_param(float3 hit, float3 p0, float3 p1) {
  const float3 d = p1 - p0;
  return saturate(dot(hit - p0, d) / max(dot(d, d), 1e-30));
}
