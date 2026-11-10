// dxc: -T ps_6_6
// Primary strand hair, motion-vector variant (docs/RTX_HAIR_PRIMARY.md increment 2, key rt_hair_groom_primary):
// replaces the fragment shader of a matched static CHAR_Hair velocity draw (native FS b70cadc5dd3adcbd,
// captures/alpha_cache/b70cadc5dd3adcbd.glsl) in a pipeline clone (layer/hair_primary.cpp; depth test
// GREATER_OR_EQUAL, no writes). It reads the pixel entry the G-buffer variant stored this frame (no trace), takes
// the strand point at the same parameter from the previous frame's points and writes the native motion vector:
// current pixel minus previous pixel, both without their jitter, in pixels (TRACK_B_FG "Motion vectors").
// No previous points yet (first frame of a slot): the current point moved with the camera only.
#include "hair_primary_rebase.hlsli"

[[vk::constant_id(1100)]] const uint kRecordLo = 0u;
[[vk::constant_id(1101)]] const uint kRecordHi = 0u;
static const uint hair_primary_slot_mask_offset = 3504u;   // common/hair_primary.h kSlotMaskOffset
static const uint hair_primary_marker_card = 0x40u;        // common/hair_primary.h kMarkerCard
// rt_hair_mv_card (common/shader_variant.h kSpecIdHairMvCard, docs/RTX_HAIR_PRIMARY.md 5x): compiled in at 1, header
// flags switch it live. kFlagMvCardFallback: a strand pixel without valid previous points writes the card's own skinned
// motion (b70cadc5 from location 1) instead of camera-only motion. kFlagMvCardAll: every card fragment of a swapped draw
// writes the card's motion before any strand check (depth: this frame's sealed strand depth of the pixel, else the card's),
// so no discard of the strand path can leave the game's camera-only value. 0: as before.
[[vk::constant_id(1049)]] const uint kHairMvCard = 0u;
static const uint hair_primary_flag_mv_card_fallback = 128u;   // common/hair_primary.h kFlagMvCardFallback
static const uint hair_primary_flag_mv_card_all = 256u;        // common/hair_primary.h kFlagMvCardAll

struct Camera {               // set 0 binding 2
  row_major float4x4 view, proj, vp;
  float near_z, far_z;
  float2 m5;
  float3 position;            // m6: camera world position
};
struct Viewport { float m0, m1, m2, m3; };   // set 0 binding 3: render size in pixels (m0, m1)
struct Screen { float2 scale, offset; };      // set 0 binding 4: m0, m1 (UV scale and offset)
struct Previous {             // set 0 binding 5: the previous frame's camera
  row_major float4x4 m0, m1, vp;               // m2: previous camera-relative view-projection
  float3 position;            // m3: previous camera position
  float m4;
  float2 jitter, prev_jitter; // m5 current, m6 previous jitter in pixels
};
[[vk::binding(2, 0)]] ConstantBuffer<Camera> gCam;
[[vk::binding(3, 0)]] ConstantBuffer<Viewport> gSize;
[[vk::binding(4, 0)]] ConstantBuffer<Screen> gScreen;
[[vk::binding(5, 0)]] ConstantBuffer<Previous> gPrev;

// Native velocity material: twelve scalar floats, unlike the G-buffer vec3/float pairs.
struct Material { float4 c[3]; };
[[vk::binding(1, 1)]] ConstantBuffer<Material> gMat;
[[vk::binding(7, 0)]] SamplerState gWrap;
[[vk::binding(3, 1)]] Texture2D<float4> gNoise;
[[vk::binding(4, 1)]] Texture2D<float4> gMask;

uint64_t record() { return (uint64_t(kRecordHi) << 32) | uint64_t(kRecordLo); }
uint64_t addr2(uint2 a) { return (uint64_t(a.y) << 32) | uint64_t(a.x); }
uint ld(uint64_t a) { return vk::RawBufferLoad<uint>(a, 4); }
float3 ldf3(uint64_t a) { return asfloat(vk::RawBufferLoad<uint3>(a, 4)); }

struct Out {
  float2 mv : SV_Target0;
  float depth : SV_Depth;
};

Out main(float4 pos : SV_Position, [[vk::location(0)]] float2 card_uv : TEXCOORD0,
         [[vk::location(1)]] float4 prev_clip : TEXCOORD1,   // native b70cadc5 location 1: the card's previous clip position
         [[vk::location(6)]] float4 aux : TEXCOORD6) {
  const float2 dx = ddx(card_uv), dy = ddy(card_uv);
  const uint64_t rec = record();
  const Out none = (Out)0;
  // Every discard returns at once: DXC lowers discard to OpDemoteToHelperInvocation, and a demoted lane keeps
  // executing, so a load after a failed bound check still ran with its garbage index (hv8 2026-11-04 Aftermath: page
  // fault on the points load after hp_segment failed, 0x2564a00000, far past the mesh pool).
  const uint tag = ld(rec), width = ld(rec + 4u), height = ld(rec + 8u), grooms = ld(rec + 12u);
  // The card fragment's camera-relative point, as in the G-buffer variant (derivatives before any discard).
  const float4 view = asfloat(vk::RawBufferLoad<uint4>(rec + 40u, 8));
  // The clip position of the vp matrix: the game's vertex shaders write gl_Position.y = -clip.y (rt/xfb_to_world.hlsl,
  // the groom points' space; b70cadc5's motion vector maps clip y with -0.5), so clip y is minus the Vulkan NDC y
  // (hv6 2026-11-04: rays mirrored vertically, x and z on the groom, y 0.66 m off: every strand ray missed).
  const float2 ndc = ((pos.xy - view.xy) / view.zw * 2.0 - 1.0) * float2(1.0, -1.0);
  const float4x4 inv = hp_inverse4(gCam.vp);
  const float4 h = mul(float4(ndc, pos.z, 1.0), inv);
  const float3 pc = h.xyz / h.w;
  const float t_card = length(pc);
  const float3 cn = cross(ddx(pc), ddy(pc));
  const float cos_card = abs(dot(pc / max(t_card, 1e-30), cn)) / max(length(cn), 1e-30);
  const float foot = abs(ddx(t_card)) + abs(ddy(t_card));
  if (tag == 0u || width == 0u || height == 0u) { discard; return none; }
  {   // Round 3 M2: the G-buffer drew this pixel's card in card mode (its accepted marker carries the card flag and its
      // slot is in the header's card mask): the card's native motion vector (b70cadc5:157), not the strand's.
    const uint2 cpx = min(uint2(pos.xy), uint2(width, height) - 1u);
    const uint64_t cpix = addr2(vk::RawBufferLoad<uint2>(rec + 32u, 4));
    const uint cacc = ld(cpix + uint64_t(width) * height * 16u + uint64_t(cpx.y * width + cpx.x) * 8u + 4u);
    if ((tag & 0xFFFFFFu) != 0u && (cacc >> 8u) == (tag & 0xFFFFFFu) && (cacc & hair_primary_marker_card) != 0u &&
        (((ld(rec + hair_primary_slot_mask_offset) >> 16u) >> (cacc & 15u)) & 1u) != 0u) {
      const float2 prev_px = ((prev_clip.xy / prev_clip.w) * float2(0.5, -0.5) + 0.5) * gScreen.scale + gScreen.offset;
      Out c;
      c.mv = (pos.xy + float2(gPrev.jitter.x, -gPrev.jitter.y)) -
             (prev_px * float2(gSize.m0, gSize.m1) + float2(gPrev.prev_jitter.x, -gPrev.prev_jitter.y));
      c.depth = pos.z;
      return c;
    }
  }
  if (kHairMvCard != 0u && (ld(rec + 60u) & hair_primary_flag_mv_card_all) != 0u) {
    const uint2 apx = min(uint2(pos.xy), uint2(width, height) - 1u);
    const uint64_t apix = addr2(vk::RawBufferLoad<uint2>(rec + 32u, 4));
    const uint4 ae = vk::RawBufferLoad<uint4>(apix + uint64_t(apx.y * width + apx.x) * hair_primary_pixel_bytes, 16);
    const bool strand = ae.w == hp_seal(ae.xyz, tag) && ae.x != hair_primary_no_strand;
    const float2 card_prev_px = ((prev_clip.xy / prev_clip.w) * float2(0.5, -0.5) + 0.5) * gScreen.scale + gScreen.offset;
    Out a;
    a.mv = (pos.xy + float2(gPrev.jitter.x, -gPrev.jitter.y)) -
           (card_prev_px * float2(gSize.m0, gSize.m1) + float2(gPrev.prev_jitter.x, -gPrev.prev_jitter.y));
    a.depth = strand ? asfloat(ae.z) : pos.z;
    return a;
  }
  if (grooms == 0u) { discard; return none; }
  float3 org;   // this camera in the published groom's camera-relative frame (hp_rebase)
  if (!hp_rebase(rec, gCam.position, org)) { discard; return none; }
  const uint64_t pool = addr2(vk::RawBufferLoad<uint2>(rec + 24u, 4)), pixels = addr2(vk::RawBufferLoad<uint2>(rec + 32u, 4));
  const uint2 px = min(uint2(pos.xy), uint2(width, height) - 1u);
  const uint4 e = vk::RawBufferLoad<uint4>(pixels + uint64_t(px.y * width + px.x) * hair_primary_pixel_bytes, 16);
  if (e.w != hp_seal(e.xyz, tag) || e.x == hair_primary_no_strand) { discard; return none; }
  const uint k = e.x >> 24, seg = e.x & hair_primary_max_segment;
  if (k >= grooms) { discard; return none; }
  // Only a strand accepted by this frame's G-buffer owner may write motion.
  const uint accepted = ld(pixels + uint64_t(width) * height * 16u + uint64_t(px.y * width + px.x) * 8u + 4u);
  if ((tag & 0xFFFFFFu) == 0u || (accepted >> 8u) != (tag & 0xFFFFFFu) ||
      ld(rec + 3248u + k * 4u) != (accepted & 255u)) { discard; return none; }
  const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
  uint i0;
  if (!hp_segment(pool, ld(rec + 56u), row, seg, i0)) { discard; return none; }
  const float v = asfloat(e.y);
  {   // the G-buffer variant's ownership test: only the card fragment that wrote this strand (hp_on_card)
    const float4 hh = mul(float4(ndc, asfloat(e.z), 1.0), inv);
    const float seg_len = length(ldf3(hp_at(pool, row.x, i0 + 1u, 12u)) - ldf3(hp_at(pool, row.x, i0, 12u)));
    if (!hp_on_card(t_card, length(hh.xyz / hh.w), cos_card, foot, seg_len)) { discard; return none; }
  }
  if ((ld(rec + 60u) & hair_primary_flag_tips) != 0u) {
    if (row.y == 0u) { discard; return none; }
    const float2 uv0 = asfloat(vk::RawBufferLoad<uint2>(hp_at(pool, row.y, i0, 8u), 4));
    const float2 uv1 = asfloat(vk::RawBufferLoad<uint2>(hp_at(pool, row.y, i0 + 1u, 8u), 4));
    const float2 uv = lerp(uv0, uv1, v);
    const float m2 = gMat.c[0].z, m3 = gMat.c[0].w;
    const float nz = gNoise.SampleGrad(gWrap, uv * m2, dx * m2, dy * m2).x * 2.0 - 1.0;
    const float4 ma = gMask.SampleGrad(gWrap, uv + nz * m3, dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
    const float4 mb = gMask.SampleGrad(gWrap, uv, dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
    const float4 M = max(sign(ma), sign(mb)) * max(abs(ma), abs(mb)) + float4(0.5, 0.0, 0.0, 0.0);
    if (!hp_tips_keep(M.w, pos.xy, tag, aux.x)) { discard; return none; }
  }
  // The strand drawn is the published pose (the build camera's frame; build camera = this camera - org). Last frame
  // showed the previous publish's pose: the rows' previous points, relative to that publish's build camera. In the
  // previous camera's space: point + its build camera - previous camera. Without previous points or that camera: the
  // published pose moved with the camera only.
  float3 prev_build;
  float3 prev;
  const bool have_prev = row.w != 0u && hp_prev_build_camera(rec, prev_build);
  if (kHairMvCard != 0u) {
    const uint mv_flags = ld(rec + 60u);
    if (!have_prev && (mv_flags & hair_primary_flag_mv_card_fallback) != 0u) {
      const float2 card_prev_px = ((prev_clip.xy / prev_clip.w) * float2(0.5, -0.5) + 0.5) * gScreen.scale + gScreen.offset;
      Out k;
      k.mv = (pos.xy + float2(gPrev.jitter.x, -gPrev.jitter.y)) -
             (card_prev_px * float2(gSize.m0, gSize.m1) + float2(gPrev.prev_jitter.x, -gPrev.prev_jitter.y));
      k.depth = asfloat(e.z);
      return k;
    }
  }
  if (have_prev) {
    prev = lerp(ldf3(hp_at(pool, row.w, i0, 12u)), ldf3(hp_at(pool, row.w, i0 + 1u, 12u)), v) + prev_build - gPrev.position;
  } else {
    const float3 cur = lerp(ldf3(hp_at(pool, row.x, i0, 12u)), ldf3(hp_at(pool, row.x, i0 + 1u, 12u)), v);
    prev = cur + gCam.position - org - gPrev.position;
  }
  const float4 c = mul(float4(prev, 1.0), gPrev.vp);
  // 42a95e17's sibling b70cadc5:49, with the strand's previous clip position in place of the card's.
  const float2 prev_px = ((c.xy / c.w) * float2(0.5, -0.5) + 0.5) * gScreen.scale + gScreen.offset;
  Out o;
  o.mv = (pos.xy + float2(gPrev.jitter.x, -gPrev.jitter.y)) -
         (prev_px * float2(gSize.m0, gSize.m1) + float2(gPrev.prev_jitter.x, -gPrev.prev_jitter.y));
  o.depth = asfloat(e.z);
  return o;
}
