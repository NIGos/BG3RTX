// dxc: -T ps_6_6
// Primary strand hair, G-buffer variant (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): replaces the
// fragment shader of a matched static CHAR_Hair G-buffer draw (native FS 42a95e17ec3adc51,
// captures/alpha_cache/42a95e17ec3adc51.glsl) inside a clone of the game's pipeline (layer/hair_primary.cpp).
// The card fragment is only the candidate region: one camera ray through the pixel into this frame's groom TLAS
// (built after the prepass); no strand: discard. A strand hit writes its own depth (the clone tests
// GREATER_OR_EQUAL with depth writes: the prepass clone left no card depth), the fibre normal, the game's material
// evaluated at the strand's own UV (card UV at its embedding point), the strand tangent and class 5.
// The first fragment of a pixel traces and stores the hit (common/hair_primary.h pixel entry); later fragments of
// that pixel read it (every fragment's ray is the same, so concurrent stores write the same values).
// Same descriptor bindings as the native FS (a subset of the game's pipeline layout); the record address comes
// from specialization constants 1100/1101.
#include "hair_primary.hlsli"

[[vk::constant_id(1100)]] const uint kRecordLo = 0u;
[[vk::constant_id(1101)]] const uint kRecordHi = 0u;

struct Camera {               // set 0 binding 2 (FRAME_MAP "Frame generation")
  row_major float4x4 view;    // world -> view
  row_major float4x4 proj;    // jittered reversed-Z projection
  row_major float4x4 vp;      // camera-relative jittered view-projection
  float near_z, far_z;
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

float4x4 inverse4(float4x4 m) {
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

struct Out {
  float4 rt0 : SV_Target0;
  float4 rt1 : SV_Target1;
  float4 rt2 : SV_Target2;
  float4 rt3 : SV_Target3;
  float depth : SV_Depth;
};

// The native colour/roughness/scatter at UV `uv`, gradients from the card fragment's UV (smooth across the quad,
// unlike the strand UVs of neighbouring pixels).
float4 tex(Texture2D<float4> t, float2 uv, float2 dx, float2 dy) { return t.SampleGrad(gWrap, uv, dx, dy); }
float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

Out main(float4 pos : SV_Position, [[vk::location(0)]] float2 card_uv : TEXCOORD0,
         [[vk::location(2)]] float4 aux : TEXCOORD2) {
  Out o = (Out)0;
  const uint64_t rec = record();
  const uint tag = ld(rec);
  const uint width = ld(rec + 4u), height = ld(rec + 8u), grooms = ld(rec + 12u);
  if (tag == 0u || grooms == 0u || width == 0u || height == 0u) discard;
  const uint64_t pool = addr2(ld2(rec + 24u)), pixels = addr2(ld2(rec + 32u));
  const uint2 px = min(uint2(pos.xy), uint2(width, height) - 1u);
  const uint64_t at = pixels + uint64_t(px.y * width + px.x) * hair_primary_pixel_bytes;
  uint4 e = vk::RawBufferLoad<uint4>(at, 16);
  if (e.w != tag) {
    // Camera-relative point of the card fragment: the ray direction (camera at the origin of the groom TLAS frame).
    // Vulkan viewport transform inverted (a flipped viewport has a negative height).
    const float4 view = asfloat(vk::RawBufferLoad<uint4>(rec + 40u, 8));
    const float2 ndc = (pos.xy - view.xy) / view.zw * 2.0 - 1.0;
    const float4 h = mul(float4(ndc, pos.z, 1.0), inverse4(gCam.vp));
    const float3 dir = normalize(h.xyz / h.w);
    RayDesc r;
    r.Origin = 0.0.xxx;
    r.TMin = 0.0;
    r.Direction = dir;
    r.TMax = 1.0e5;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(hp_tlas(ld2(rec + 16u)), 0u, 0xFFu, r);
    while (q.Proceed()) {}
    e = uint4(hair_primary_no_strand, 0u, 0u, tag);
    if (q.CommittedStatus() != COMMITTED_NOTHING) {
      const uint k = q.CommittedInstanceIndex(), seg = q.CommittedPrimitiveIndex();
      if (k < grooms && seg <= hair_primary_max_segment) {
        const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
        const uint i0 = ld(pool + row.z + seg * 4u);
        const float3 p0 = ldf3(pool + row.x + i0 * 12u), p1 = ldf3(pool + row.x + (i0 + 1u) * 12u);
        const float3 hit = dir * q.CommittedRayT();
        const float v = hp_segment_param(hit, p0, p1);
        const float4 c = mul(float4(hit, 1.0), gCam.vp);
        e = uint4((k << 24) | seg, asuint(v), asuint(c.z / c.w), tag);
      }
    }
    vk::RawBufferStore<uint4>(at, e, 16);
  }
  if (e.x == hair_primary_no_strand) discard;
  const uint k = e.x >> 24, seg = e.x & hair_primary_max_segment;
  if (k >= grooms) discard;   // the cached entry is validated like a fresh hit (stores of a pixel are not atomic)
  const float v = asfloat(e.y);
  const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
  const uint i0 = ld(pool + row.z + seg * 4u);
  const float3 p0 = ldf3(pool + row.x + i0 * 12u), p1 = ldf3(pool + row.x + (i0 + 1u) * 12u);
  const float2 uv0 = asfloat(vk::RawBufferLoad<uint2>(pool + row.y + i0 * 8u, 4));
  const float2 uv1 = asfloat(vk::RawBufferLoad<uint2>(pool + row.y + (i0 + 1u) * 8u, 4));
  const float2 uv = lerp(uv0, uv1, v);
  const float3 t_world = normalize(p1 - p0);
  const float3 to_cam = -normalize(lerp(p0, p1, v));
  const float3 t_view = normalize(mul(float4(t_world, 0.0), gCam.view).xyz);
  const float3 n_view = normalize(mul(float4(hp_fibre_normal(t_world, to_cam), 0.0), gCam.view).xyz);

  // ---- the native material (42a95e17ec3adc51:118-195) at the strand's UV ----
  const float2 dx = ddx(card_uv), dy = ddy(card_uv);
  const float m5 = F(5u), m7 = F(7u);
  const float4 noise = tex(gNoise, uv * m5, dx * m5, dy * m5);
  const float nz = noise.x * 2.0 - 1.0;
  const float4 ma = tex(gMask, uv + nz * m7, dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
  const float4 mb = tex(gMask, uv, dx, dy) - float4(0.5, 0.0, 0.0, 0.0);
  const float4 M = max(sign(ma), sign(mb)) * max(abs(ma), abs(mb)) + float4(0.5, 0.0, 0.0, 0.0);
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

  o.rt0 = float4(hp_stereo_encode(n_view), 0.0, 0.0);
  o.rt1 = float4(saturate(lerp(c477, V(16u), tip)), 1.0);
  o.rt2 = float4(saturate(lerp(lerp(f34 * f523 + z550, f34 * f523 + z550 * -0.2, m31), 0.2, tip)), 0.5,
                 saturate(F(39u)), hp_rt2_w(kHairPrimaryClass, gDraw.channel, false));
  o.rt3 = float4(hp_oct12_encode(t_view),
                 clamp((1.0 - 0.3 * z550) * saturate(f334 * (saturate(dot(c279, 1.0.xxx)) * 2.0 - 1.0) + F(38u)),
                       0.01, 1.0));
  o.depth = asfloat(e.z);
  return o;
}
