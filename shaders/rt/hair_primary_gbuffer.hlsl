// dxc: -T ps_6_6
// Primary strand hair, G-buffer variant (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): replaces the
// fragment shader of a matched static CHAR_Hair G-buffer draw (native FS 42a95e17ec3adc51,
// captures/alpha_cache/42a95e17ec3adc51.glsl) inside a clone of the game's pipeline (layer/hair_primary.cpp).
// The card fragment is only the candidate region: one camera ray through the pixel into the last published groom TLAS
// (the previous frame's: rebased to this camera, hp_rebase); no strand: discard. A strand hit writes its own depth (the clone tests
// GREATER_OR_EQUAL with depth writes: the prepass clone left no card depth), the fibre normal, the game's material
// evaluated at the strand's own UV (card UV at its embedding point), the strand tangent and class 5.
// The first fragment of a pixel traces and stores the hit (common/hair_primary.h pixel entry); later fragments of
// that pixel read it (every fragment's ray is the same, so concurrent stores write the same values; word 3 seals the
// entry, hp_seal). A fragment writes the hit only when it lies on its own card (hp_on_card): a strand of another
// groom or another card layer is written by that card's fragment.
// Review 2026-11-08: the shared pixel trace takes the closest strand of the draw slots that swap this frame (header slot
// mask bits 0-15) and skips any other groom's strand (a card-kept groom's strands hid a swapped draw's own: holes); a
// draw whose own slot is not in that mask (the masked coverage probe) traces privately for its own slot only. A slot in
// the card mask (bits 16-31: the submit-time header no longer holds its groom) draws its card here instead: the native
// material at the card UV, card normal, card tangent and card depth (the prepass dropped the card depth).
// Same descriptor bindings as the native FS (a subset of the game's pipeline layout); the record address comes
// from specialization constants 1100/1101.
#include "hair_primary_rebase.hlsli"
#include "hair_primary_coverage.hlsli"
#include "hair_primary_nearest.hlsli"

// M3.2 (rt_hair_groom_coverage, common/shader_variant.h kSpecIdHairGroomCoverage): two stratified rays per pixel.
// 0: the single pixel ray below, unchanged; 1: the pair, when header flag 8 is also set (live off-switch).
[[vk::constant_id(1042)]] const uint kHairGroomCoverage = 0u;
// 5g (rt_hair_macro_normal / rt_hair_tangent_world, common/shader_variant.h kSpecIdHairShadingSpace): 0: RT0 the fibre
// normal, RT3 the view-space tangent, unchanged; 1: header flag 16 writes RT0 = the card's own normal (exactly what the
// native FS writes: VS 41a168e2 location 1 = view(location 3)), flag 32 writes RT3 in world space, the space of the
// native card tangent (VS 41a168e2 locations 3-5 are world vectors; 3033c895 reads class 5 T without rotation).
[[vk::constant_id(1045)]] const uint kHairShadingSpace = 0u;
// 5u (rt_hair_groom_nearest, common/shader_variant.h kSpecIdHairGroomNearest): 0: hp_trace's restart loop below,
// unchanged; 1: with header flag 64 the closest strand of an accepted row from one candidate-filtered query
// (hair_primary_nearest.hlsli): no kHpSkips limit and no 2 x radius jump past another row's strand.
[[vk::constant_id(1047)]] const uint kHairGroomNearest = 0u;
static const uint hair_primary_flag_nearest = 64u;   // common/hair_primary.h kFlagNearest
static const uint hair_primary_flag_macro_normal = 16u;   // common/hair_primary.h kFlagMacroNormal
static const uint hair_primary_flag_tangent_world = 32u;  // common/hair_primary.h kFlagTangentWorld
[[vk::constant_id(1100)]] const uint kRecordLo = 0u;
[[vk::constant_id(1101)]] const uint kRecordHi = 0u;
[[vk::constant_id(1102)]] const uint kDrawSlot = 16u;

static const uint hair_primary_key_map_offset = 3248u; // common/hair_primary.h kKeyMapOffset
static const uint hair_primary_slot_mask_offset = 3504u; // common/hair_primary.h kSlotMaskOffset
static const uint hair_primary_marker_skipped = 0x20u;   // common/hair_primary.h kMarkerSkipped
static const uint hair_primary_marker_card = 0x40u;      // common/hair_primary.h kMarkerCard
static const uint kHpSkips = 3u;   // strands of other slots skipped along one pixel ray before it gives up

struct Camera {               // set 0 binding 2 (FRAME_MAP "Frame generation")
  row_major float4x4 view;    // world -> view
  row_major float4x4 proj;    // jittered reversed-Z projection
  row_major float4x4 vp;      // camera-relative jittered view-projection
  float near_z, far_z;
  float2 m5;
  float3 position;            // m6: camera world position (the origin of this frame's camera-relative space)
};
struct Draw {                 // set 1 binding 0
  row_major float4x4 world;
  uint m1, channel, m3, m4;
};
struct Material { float4 c[15]; };   // set 1 binding 1: m0..m17 as (vec3, float) pairs, m18..m41 floats

[[vk::binding(2, 0)]] ConstantBuffer<Camera> gCam;
[[vk::binding(0, 1)]] ConstantBuffer<Draw> gDraw;
[[vk::binding(1, 1)]] ConstantBuffer<Material> gMat;
[[vk::binding(7, 0)]] SamplerState gWrap;
[[vk::binding(8, 0)]] SamplerState gLut;
[[vk::binding(3, 1)]] Texture2D<float4> gNoise;    // native _21
[[vk::binding(4, 1)]] Texture2D<float4> gMask;     // native _22 (centred red)
[[vk::binding(5, 1)]] Texture2D<float4> gDetail;   // native _23
[[vk::binding(6, 1)]] Texture2D<float4> gRamp;     // native _24 (sampler binding 8, LOD 0)

[[vk::ext_instruction(/*OpConvertUToAccelerationStructureKHR*/ 4447)]]
RaytracingAccelerationStructure hp_tlas(uint2 address);

float3 V(uint i) { return gMat.c[i / 2u].xyz; }                    // native m0, m2, ..., m16
float F(uint i) {                                                   // native m1, m3, ..., m17 and m18..m41
  if (i < 18u) return gMat.c[i / 2u].w;
  const uint j = i - 18u;
  return gMat.c[9u + j / 4u][j % 4u];
}

uint64_t record() { return (uint64_t(kRecordHi) << 32) | uint64_t(kRecordLo); }
uint64_t addr2(uint2 a) { return (uint64_t(a.y) << 32) | uint64_t(a.x); }
uint ld(uint64_t a) { return vk::RawBufferLoad<uint>(a, 4); }
uint2 ld2(uint64_t a) { return vk::RawBufferLoad<uint2>(a, 4); }
float3 ldf3(uint64_t a) { return asfloat(vk::RawBufferLoad<uint3>(a, 4)); }

// The native colour/roughness/scatter at UV `uv`, gradients from the card fragment's UV (smooth across the quad,
// unlike the strand UVs of neighbouring pixels).
float4 tex(Texture2D<float4> t, float2 uv, float2 dx, float2 dy) { return t.SampleGrad(gWrap, uv, dx, dy); }
float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// One pixel ray: the closest strand whose groom row maps to a draw slot in `accept` (bit per slot), skipping up to
// kHpSkips strands of other rows (no slot, or a slot not accepted) by restarting past them (TMin beyond the swept
// sphere's diameter bound). The same trace and validation for the single ray and each ray of the M3.2 pair. false: no
// valid strand (a miss, or a hit whose groom/segment fails the bounds: stored as no strand). skipped: a strand of another
// row was in front.
// 5u: a groom row accepted by the pixel trace: its draw slot (key map) is in `accept`.
struct HpSlotAccept {
  uint64_t rec;
  uint accept, grooms;
  bool ok(uint k) {
    if (k >= grooms) return false;
    const uint slot = vk::RawBufferLoad<uint>(rec + hair_primary_key_map_offset + k * 4u, 4);
    return slot < 16u && ((accept >> slot) & 1u) != 0u;
  }
};
bool hp_trace(uint64_t rec, float3 org, float3 dir, uint accept, uint grooms, uint64_t pool, uint pool_bytes,
              uint64_t crumbs, uint64_t pool_end, out uint3 e, out float t, out bool skipped) {
  e = uint3(hair_primary_no_strand, 0u, 0u);
  t = 0.0;
  skipped = false;
  if (kHairGroomNearest != 0u && (ld(rec + 60u) & hair_primary_flag_nearest) != 0u) {
    RayDesc r;
    r.Origin = org;
    r.TMin = 0.0;
    r.Direction = dir;
    r.TMax = 1.0e5;
    HpSlotAccept acc;
    acc.rec = rec;
    acc.accept = accept;
    acc.grooms = grooms;
    uint k, seg;
    float th;
    if (!hp_nearest(hp_tlas(ld2(rec + 16u)), r, 0xFEu, acc, k, seg, th, skipped)) return false;
    if (seg > hair_primary_max_segment) return false;
    const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
    uint i0;
    if (!hp_segment(pool, pool_bytes, row, seg, i0) ||
        !hp_crumb_ok(crumbs, 3u, hp_at(pool, row.x, i0, 12u), 24u, pool, pool_end)) return false;
    const float3 p0 = ldf3(hp_at(pool, row.x, i0, 12u)), p1 = ldf3(hp_at(pool, row.x, i0 + 1u, 12u));
    t = th;
    const float3 hit = dir * t;
    const float4 c = mul(float4(hit, 1.0), gCam.vp);
    e = uint3((k << 24) | seg, asuint(hp_segment_param(hit + org, p0, p1)), asuint(c.z / c.w));
    return true;
  }
  float tmin = 0.0;
  for (uint i = 0u; i <= kHpSkips; ++i) {
    RayDesc r;
    r.Origin = org;
    r.TMin = tmin;
    r.Direction = dir;
    r.TMax = 1.0e5;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(hp_tlas(ld2(rec + 16u)), 0u, 0xFEu, r);   // 0xFE: not the grooms without a primary key (bvhsplit)
    while (q.Proceed()) {}
    if (q.CommittedStatus() == COMMITTED_NOTHING) return false;
    const uint k = q.CommittedInstanceIndex(), seg = q.CommittedPrimitiveIndex();
    const float th = q.CommittedRayT();
    if (k >= grooms || seg > hair_primary_max_segment) return false;
    const uint slot = ld(rec + hair_primary_key_map_offset + k * 4u);
    if (slot >= 16u || ((accept >> slot) & 1u) == 0u) {
      skipped = true;
      tmin = th + 2.0 * hair_primary_max_radius;
      continue;
    }
    const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
    uint i0;
    if (!hp_segment(pool, pool_bytes, row, seg, i0) ||
        !hp_crumb_ok(crumbs, 3u, hp_at(pool, row.x, i0, 12u), 24u, pool, pool_end)) return false;
    const float3 p0 = ldf3(hp_at(pool, row.x, i0, 12u)), p1 = ldf3(hp_at(pool, row.x, i0 + 1u, 12u));
    t = th;
    const float3 hit = dir * t;   // this frame's camera-relative point
    const float4 c = mul(float4(hit, 1.0), gCam.vp);
    e = uint3((k << 24) | seg, asuint(hp_segment_param(hit + org, p0, p1)), asuint(c.z / c.w));
    return true;
  }
  return false;
}

// The pixel's strand for the slots in `accept`: the M3.2 pair (rays A and B at hp_cov_offset of the jittered pixel
// centre, hp_cov_pick keeps one hit or none) when compiled in and enabled, else the single pixel ray.
uint3 hp_pixel(uint64_t rec, float3 org, float3 dir, float2 pos, float4 view, float4x4 inv, uint2 px, uint tag,
               uint accept, uint grooms, uint64_t pool, uint pool_bytes, uint64_t crumbs, uint64_t pool_end,
               out bool skipped, out float t) {
  if (kHairGroomCoverage != 0u && (ld(rec + 60u) & hair_primary_flag_coverage) != 0u) {
    uint3 hit_e[2];
    float hit_t[2];
    bool hit[2], skip[2];
    [unroll] for (uint i = 0; i < 2u; ++i) {
      const float2 nd = ((pos + hp_cov_offset(px, i) - view.xy) / view.zw * 2.0 - 1.0) * float2(1.0, -1.0);
      const float4 rh = mul(float4(nd, 1.0, 1.0), inv);
      hit[i] = hp_trace(rec, org, normalize(rh.xyz / rh.w), accept, grooms, pool, pool_bytes, crumbs, pool_end, hit_e[i],
                        hit_t[i], skip[i]);
    }
    skipped = skip[0] || skip[1];
    const uint pick = hp_cov_pick(hit[0], hit[1], hit_t[0], hit_t[1], px, tag);
    t = pick == 0u ? hit_t[0] : pick == 1u ? hit_t[1] : -1.0;
    return pick == 0u ? hit_e[0] : pick == 1u ? hit_e[1] : uint3(hair_primary_no_strand, 0u, 0u);
  }
  uint3 e;
  const bool hit = hp_trace(rec, org, dir, accept, grooms, pool, pool_bytes, crumbs, pool_end, e, t, skipped);
  if (!hit) t = -1.0;
  return e;
}

// The native material (42a95e17ec3adc51:118-195) at UV `uv`: RT1, RT2, RT3.w and the combined mask alpha M.w.
struct NativeMaterial { float3 rt1; float4 rt2; float rt3w; float alpha; };
NativeMaterial native_material(float2 uv, float2 dx, float2 dy, float4 aux) {
  NativeMaterial m;
  const float m5 = F(5u), m7 = F(7u);
  const float4 noise = tex(gNoise, uv * m5, dx * m5, dy * m5);
  const float nz = noise.x * 2.0 - 1.0;
  const float4 ma = tex(gMask, uv + nz * m7, dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
  const float4 mb = tex(gMask, uv, dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
  const float4 M = max(sign(ma), sign(mb)) * max(abs(ma), abs(mb)) + float4(0.5, 0.0, 0.0, 0.0);
  m.alpha = M.w;
  const float m18 = F(18u);
  const float3 c279 = lerp(V(4u), V(6u), m18);
  const float f281 = frac(uv.y);
  const float f286 = lerp(F(19u), F(20u), m18);
  const float f291 = 1.0 - 4.0 * max(f286 * 0.8 - 0.75, 0.0);
  const float f325 = -frac(aux.x + F(21u));
  const float f332 = ceil(f286 * 0.8);
  const float f334 = saturate(-(-f286 * 0.8 + (1.0 - abs(f325 * 0.4 + (f281 * f291 - (M.x * 2.0 - 0.5)))))
                              * f332);
  const float3 c355 = lerp(lerp(lerp(V(0u), V(2u), m18), c279, f334), lerp(V(8u), V(10u), m18),
                           saturate(saturate(aux.z - F(22u)) / (1.0 - F(22u)) * 1.5) * F(23u));
  const float3 c356 = c355 * 0.5;
  const float f363 = saturate(F(25u));
  const float f369 = smoothstep(F(24u) - f363 * 0.5, F(24u) + f363 * 0.5, M.z);
  const float m31 = F(31u);
  const float3 c432 = V(14u) * (1.0 - 0.8 * m31);
  const float4 base = tex(gNoise, uv, dx, dy);
  const float3 c440 = c432 + c432 * (1.0 - base.w);
  const float2 c465 = float2(F(32u), F(33u));
  const float2 c472 = saturate(pow(max(abs(float2(1.0 - base.x, 1.0) * M.x), 1e-4), 1.5) - (1.0 - c465)) /
                      clamp(c465, 1e-6, 1.0) * saturate(c465);
  const float z550 = saturate(saturate(-(m31 - max(0.2 * base.y, base.w)) * 3.0 + 0.25) * c472.y);
  const float3 grey = lum(c356).xxx;
  const float pick = saturate(lerp(f369, pow(max(abs(min(min(saturate(uv.y * 2.0 * 2.0), aux.w), f369)), 1e-4),
                                                F(26u)), F(27u)));
  const float3 P = lerp(grey + (c356 - grey) * 1.2, c355, pick) * lerp(1.0, 0.2 + M.x * 1.8, F(28u)) *
                   lerp(1.0, 0.5 + pow(max(abs(M.y), 1e-4), 2.2) * 1.5, F(29u));
  const float3 Q = max(0.0, (gRamp.SampleLevel(gLut, float2(M.x, 0.0), 0.0).xyz - 0.5) * (V(12u) * f334) + 1.0);
  const float3 X = (tex(gDetail, uv, dx, dy).xyz * 2.0 - 1.0) * F(17u) + (P * Q + nz * F(30u));
  const float3 Y = lerp(lum(c440).xxx, c440, m31 * 2.0 + 1.0);
  const float3 c477 = lerp(X, Y, z550);
  const float tip = pow(max(abs(base.z * saturate(c472.x + F(32u))), 1e-4), 2.0);
  const float f523 = lerp(1.0, 0.5 + (1.0 - M.y) * 1.5, F(35u));
  const float f34 = F(34u);
  m.rt1 = saturate(lerp(c477, V(16u), tip));
  m.rt2 = float4(saturate(lerp(lerp(f34 * f523 + z550, f34 * f523 + z550 * -0.2, m31), 0.2, tip)), 0.5,
                 saturate(F(39u)), hp_rt2_w(kHairPrimaryClass, gDraw.channel, false));
  m.rt3w = clamp((1.0 - 0.3 * z550) * saturate(f334 * (saturate(dot(c279, 1.0.xxx)) * 2.0 - 1.0) + F(38u)), 0.01, 1.0);
  return m;
}

struct Out {
  float4 rt0 : SV_Target0;
  float4 rt1 : SV_Target1;
  float4 rt2 : SV_Target2;
  float4 rt3 : SV_Target3;
  float depth : SV_Depth;
};


Out main(float4 pos : SV_Position, [[vk::location(0)]] float2 card_uv : TEXCOORD0,
         [[vk::location(2)]] float4 aux : TEXCOORD2,
         [[vk::location(3)]] float3 card_normal : TEXCOORD3,
         [[vk::location(4)]] float3 card_bitangent : TEXCOORD4,
         [[vk::location(5)]] float3 card_tangent : TEXCOORD5,
         bool front : SV_IsFrontFace) {
  Out o = (Out)0;
  // Every discard returns at once: DXC lowers discard to OpDemoteToHelperInvocation, and a demoted lane keeps
  // executing, so a load after a failed bound check still ran with its garbage index (hv8 2026-11-04 Aftermath: page
  // fault on the points load after hp_segment failed, 0x2564a00000, far past the mesh pool).
  const uint64_t rec = record();
  const uint tag = ld(rec);
  const uint width = ld(rec + 4u), height = ld(rec + 8u), grooms = ld(rec + 12u);
  // Camera-relative card point for ownership; a fixed-depth pixel ray for the shared cache.
  // Vulkan viewport transform inverted (a flipped viewport has a negative height). Derivatives before any discard.
  const float4 view = asfloat(vk::RawBufferLoad<uint4>(rec + 40u, 8));
  // The clip position of the vp matrix: the game's vertex shaders write gl_Position.y = -clip.y (rt/xfb_to_world.hlsl,
  // the groom points' space; b70cadc5's motion vector maps clip y with -0.5), so clip y is minus the Vulkan NDC y
  // (hv6 2026-11-04: rays mirrored vertically, x and z on the groom, y 0.66 m off: every strand ray missed).
  const float2 ndc = ((pos.xy - view.xy) / view.zw * 2.0 - 1.0) * float2(1.0, -1.0);
  const float4x4 inv = hp_inverse4(gCam.vp);
  const float4 h = mul(float4(ndc, pos.z, 1.0), inv);
  const float3 pc = h.xyz / h.w;
  const float t_card = length(pc);
  // Card depth must not affect even the rounding of the cached hit's v/depth.
  const float4 ray_h = mul(float4(ndc, 1.0, 1.0), inv);
  const float3 dir = normalize(ray_h.xyz / ray_h.w);
  const float3 cn = cross(ddx(pc), ddy(pc));
  const float cos_card = abs(dot(dir, cn)) / max(length(cn), 1e-30);
  const float foot = abs(ddx(t_card)) + abs(ddy(t_card));
  const float2 dx = ddx(card_uv), dy = ddy(card_uv);   // before the first return (quad neighbours)
  if (tag == 0u || width == 0u || height == 0u) { discard; return o; }
  // A compact draw slot identifies the draw key, including misses and rejected hits.
  // Separate scalar words are idempotent across stacked fragments; rejected lanes never erase acceptance.
  if (kDrawSlot >= 16u || pos.x < 0 || pos.y < 0 || pos.x >= width || pos.y >= height) { discard; return o; }
  const uint masks = ld(rec + hair_primary_slot_mask_offset);
  const uint swap_slots = masks & 0xFFFFu;
  // Native prepass alpha (5a03e5b6): combined masks at the card UV, m8 = 0, m9 = 1.
  const float card_noise = tex(gNoise, card_uv * F(5u), dx * F(5u), dy * F(5u)).x * 2.0 - 1.0;
  const float4 card_ma = tex(gMask, card_uv + card_noise * F(7u), dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
  const float4 card_mb = tex(gMask, card_uv, dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
  const float4 card_M = max(sign(card_ma), sign(card_mb)) * max(abs(card_ma), abs(card_mb)) + float4(0.5, 0.0, 0.0, 0.0);
  const float3 card_n = card_normal * (front ? 1.0 : -1.0);
  const float3x3 card_basis = float3x3(card_tangent, card_n, card_bitangent);
  const float3 card_to_cam = normalize(mul(normalize(mul(card_basis, -dir)), card_basis));
  const float grazing = saturate(abs(dot(card_to_cam, normalize(card_n))) * 5.0);
  if (((masks >> 16u) >> kDrawSlot) & 1u) {
    // Card mode (review 2026-11-08 item 1): the header this G-buffer reads holds no groom for this recorded draw. Its
    // card, as the native pipeline draws it: the prepass alpha test, the native material at the card UV, the card normal
    // (view), the card tangent (world: the native T is the bitangent tilted by the detail mask), card depth. No markers.
    if (!hp_tips_keep(card_M.w * grazing, pos.xy, tag, aux.x)) { discard; return o; }
    // Round 3 M2: the accepted word alone, flagged card (no card word: no coverage evidence), so the swapped velocity
    // variant writes this card's motion vector here.
    const uint64_t card_mark = addr2(ld2(rec + 32u)) + uint64_t(width) * height * 16u +
                               uint64_t(uint(pos.y) * width + uint(pos.x)) * 8u;
    vk::RawBufferStore<uint>(card_mark + 4u, (tag << 8u) | kDrawSlot | hair_primary_marker_card, 4);
    const NativeMaterial cm = native_material(card_uv, dx, dy, aux);
    o.rt0 = float4(hp_stereo_encode(normalize(mul(float4(card_n, 0.0), gCam.view).xyz)), 0.0, 0.0);
    o.rt1 = float4(cm.rt1, 1.0);
    o.rt2 = cm.rt2;
    o.rt3 = float4(hp_oct12_encode(normalize(card_bitangent)), cm.rt3w);
    o.depth = pos.z;
    return o;
  }
  if (grooms == 0u) { discard; return o; }
  const uint64_t marker_pixels = addr2(ld2(rec + 32u));
  const uint marker_index = uint(pos.y) * width + uint(pos.x);
  const uint64_t marker = marker_pixels + uint64_t(width) * height * 16u +
                          uint64_t(marker_index) * 8u;
  const uint packed_marker = (tag << 8u) | kDrawSlot;
  const bool card_marked = hp_tips_keep(card_M.w * grazing, pos.xy, tag, aux.x);
  if (card_marked)
    vk::RawBufferStore<uint>(marker, packed_marker, 4);
  float3 org;   // this camera in the published groom's camera-relative frame (hp_rebase)
  if (!hp_rebase(rec, gCam.position, org)) { discard; return o; }
  const uint64_t pool = addr2(ld2(rec + 24u)), pixels = addr2(ld2(rec + 32u));
  const uint pool_bytes = ld(rec + 56u);
  const uint2 px = min(uint2(pos.xy), uint2(width, height) - 1u);
  const uint64_t at = pixels + uint64_t(px.y * width + px.x) * hair_primary_pixel_bytes;
  const uint64_t crumbs = hp_crumbs(rec);
  const uint64_t pixel_end = pixels + uint64_t(width) * height * hair_primary_pixel_bytes, pool_end = pool + pool_bytes;
  if (crumbs != 0u && (px.x & 31u) == 0u && (px.y & 31u) == 0u) {
    hp_crumb_note(crumbs, 0u, addr2(ld2(rec + 16u)));
    hp_crumb_note(crumbs, 1u, pool);
    hp_crumb_note(crumbs, 2u, pixels);
  }
  if (!hp_crumb_ok(crumbs, 1u, at, 16u, pixels, pixel_end)) { discard; return o; }
  uint4 e = vk::RawBufferLoad<uint4>(at, 16);
  if (e.w != hp_seal(e.xyz, tag)) {
    // The first fragment of the pixel traces for the slots that swap this frame (the single ray, or the M3.2 pair) and
    // seals the entry, so later fragments, velocity and count read it.
    bool skipped;
    float t_hit;
    e.xyz = hp_pixel(rec, org, dir, pos.xy, view, inv, px, tag, swap_slots, grooms, pool, pool_bytes, crumbs, pool_end,
                     skipped, t_hit);
    e.w = hp_seal(e.xyz, tag);
    vk::RawBufferStore<uint4>(at, e, 16);
    // Item 2 diagnostic: this pixel's trace passed a strand of a slot that does not swap (the old trace's hole).
    if (skipped && card_marked) vk::RawBufferStore<uint>(marker, packed_marker | hair_primary_marker_skipped, 4);
    if ((px.x & 63u) == 32u && (px.y & 63u) == 32u) {   // diagnostics: one ray of a sparse grid (last writer wins)
      const uint64_t d = rec + hair_primary_ray_debug_offset;
      const bool hit = e.x != hair_primary_no_strand;
      vk::RawBufferStore<float4>(d, float4(org, t_card), 16);
      vk::RawBufferStore<float4>(d + 16u, float4(dir, hit ? 1.0 : 0.0), 16);
      vk::RawBufferStore<float4>(d + 32u, float4(hit ? t_hit : -1.0, hit ? float(e.x >> 24) : -1.0,
                                                 hit ? float(e.x & hair_primary_max_segment) : -1.0, float(tag)), 16);
      vk::RawBufferStore<float4>(d + 48u, float4(gCam.position, 1.0), 16);
    }
  }
  if (((swap_slots >> kDrawSlot) & 1u) == 0u) {
    // This draw's slot does not swap this frame (the masked coverage probe after a card draw): the shared entry skipped
    // its strands, so it traces privately for its own slot (not stored: the shared entry stays the swapped draws').
    bool skipped;
    float t_hit;
    e.xyz = hp_pixel(rec, org, dir, pos.xy, view, inv, px, tag, 1u << kDrawSlot, grooms, pool, pool_bytes, crumbs,
                     pool_end, skipped, t_hit);
  }
  if (e.x == hair_primary_no_strand) { discard; return o; }
  const uint k = e.x >> 24, seg = e.x & hair_primary_max_segment;
  if (k >= grooms || ld(rec + hair_primary_key_map_offset + k * 4u) != kDrawSlot) { discard; return o; }   // the cached entry is validated like a fresh hit (stores of a pixel are not atomic)
  const float v = asfloat(e.y);
  const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
  uint i0;
  if (!hp_segment(pool, pool_bytes, row, seg, i0)) { discard; return o; }
  if (!hp_crumb_ok(crumbs, 4u, hp_at(pool, row.x, i0, 12u), 24u, pool, pool_end)) { discard; return o; }
  const float3 p0 = ldf3(hp_at(pool, row.x, i0, 12u)), p1 = ldf3(hp_at(pool, row.x, i0 + 1u, 12u));
  const float seg_len2 = dot(p1 - p0, p1 - p0);
  if (!(seg_len2 > 1e-20)) { discard; return o; }   // a zero-length segment has no tangent (item 17; the parser rejects them)
  const float4 hh = mul(float4(ndc, asfloat(e.z), 1.0), inv);
  if (!hp_on_card(t_card, length(hh.xyz / hh.w), cos_card, foot, sqrt(seg_len2))) { discard; return o; }
  if (row.y == 0u) { discard; return o; }   // no point UVs (the layer publishes a row without them only for a card-kept draw)
  if (!hp_crumb_ok(crumbs, 5u, hp_at(pool, row.y, i0, 8u), 16u, pool, pool_end)) { discard; return o; }
  const float2 uv0 = asfloat(vk::RawBufferLoad<uint2>(hp_at(pool, row.y, i0, 8u), 4));
  const float2 uv1 = asfloat(vk::RawBufferLoad<uint2>(hp_at(pool, row.y, i0 + 1u, 8u), 4));
  const float2 uv = lerp(uv0, uv1, v);
  const float3 t_world = (p1 - p0) * rsqrt(seg_len2);
  const float3 to_cam = normalize(org - lerp(p0, p1, v));
  const float3 t_view = normalize(mul(float4(t_world, 0.0), gCam.view).xyz);
  const float3 n_view = normalize(mul(float4(hp_fibre_normal(t_world, to_cam), 0.0), gCam.view).xyz);

  // ---- the native material (42a95e17ec3adc51:118-195) at the strand's UV ----
  const NativeMaterial m = native_material(uv, dx, dy, aux);
  if ((ld(rec + 60u) & hair_primary_flag_tips) != 0u && !hp_tips_keep(m.alpha, pos.xy, tag, aux.x)) { discard; return o; }
  vk::RawBufferStore<uint>(marker + 4u, packed_marker, 4); // after every rejection, cache remains reusable

  float3 rt0_n = n_view, rt3_t = t_view;
  if (kHairShadingSpace != 0u) {   // dead as a whole at constant 0
    const uint flags = ld(rec + 60u);
    if ((flags & hair_primary_flag_macro_normal) != 0u) rt0_n = normalize(mul(float4(card_n, 0.0), gCam.view).xyz);
    if ((flags & hair_primary_flag_tangent_world) != 0u) rt3_t = t_world;
  }
  o.rt0 = float4(hp_stereo_encode(rt0_n), 0.0, 0.0);
  o.rt1 = float4(m.rt1, 1.0);
  o.rt2 = m.rt2;
  o.rt3 = float4(hp_oct12_encode(rt3_t), m.rt3w);
  o.depth = asfloat(e.z);
  // C2: one fragment per 8 x 8 pixel block stores this groom's colour sample (albedo, fibre roughness) and the tag.
  if (all((uint2(pos.xy) & 7u) == 0u)) {
    vk::RawBufferStore<uint4>(rec + hair_primary_colour_offset + k * 32u, asuint(float4(o.rt1.rgb, max(0.09, o.rt2.x))), 16);
    vk::RawBufferStore<uint>(rec + hair_primary_colour_offset + k * 32u + 16u, tag, 4);
  }
  return o;
}
