// dxc: -T ps_6_6
// Coverage-only prepass FS: same camera ray, hp_segment bounds and hp_on_card band
// as hair_primary_gbuffer. Never reads or writes the G-buffer pixel cache. Late
// fragment tests are required: discarded rays must not contribute occlusion samples.
#include "hair_primary_rebase.hlsli"

[[vk::constant_id(1100)]] const uint kRecordLo = 0u;
[[vk::constant_id(1101)]] const uint kRecordHi = 0u;

struct Camera {               // set 0 binding 2 (FRAME_MAP "Frame generation")
  row_major float4x4 view;    // world -> view
  row_major float4x4 proj;    // jittered reversed-Z projection
  row_major float4x4 vp;      // camera-relative jittered view-projection
  float near_z, far_z;
  float2 m5;
  float3 position;            // m6: camera world position (the origin of this frame's camera-relative space)
};
[[vk::binding(2, 0)]] ConstantBuffer<Camera> gCam;
[[vk::ext_instruction(4447)]] RaytracingAccelerationStructure hp_tlas(uint2 address);
uint64_t record() { return (uint64_t(kRecordHi) << 32) | uint64_t(kRecordLo); }
uint64_t addr2(uint2 a) { return (uint64_t(a.y) << 32) | uint64_t(a.x); }
uint ld(uint64_t a) { return vk::RawBufferLoad<uint>(a, 4); }
uint2 ld2(uint64_t a) { return vk::RawBufferLoad<uint2>(a, 4); }
float3 ldf3(uint64_t a) { return asfloat(vk::RawBufferLoad<uint3>(a, 4)); }

void main(float4 pos : SV_Position) {
  const uint64_t rec = record();
  const uint tag = ld(rec);
  const uint width = ld(rec + 4u), height = ld(rec + 8u), grooms = ld(rec + 12u);
  // Camera-relative point of the card fragment: the ray direction (camera at the origin of the groom TLAS frame).
  // Vulkan viewport transform inverted (a flipped viewport has a negative height). Derivatives before any discard.
  const float4 view = asfloat(vk::RawBufferLoad<uint4>(rec + 40u, 8));
  const float2 ndc = (pos.xy - view.xy) / view.zw * 2.0 - 1.0;
  const float4x4 inv = hp_inverse4(gCam.vp);
  const float4 h = mul(float4(ndc, pos.z, 1.0), inv);
  const float3 pc = h.xyz / h.w;
  const float t_card = length(pc);
  const float3 dir = pc / max(t_card, 1e-30);
  const float3 cn = cross(ddx(pc), ddy(pc));
  const float cos_card = abs(dot(dir, cn)) / max(length(cn), 1e-30);
  const float foot = abs(ddx(t_card)) + abs(ddy(t_card));
  if (tag == 0u || grooms == 0u || width == 0u || height == 0u) discard;
  float3 org;   // this camera in the published groom's camera-relative frame (hp_rebase)
  if (!hp_rebase(rec, gCam.position, org)) discard;
  const uint64_t pool = addr2(ld2(rec + 24u));
  const uint pool_bytes = ld(rec + 56u);
  RayDesc r;
  r.Origin = org; r.TMin = 0.0; r.Direction = dir; r.TMax = 1.0e5;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(hp_tlas(ld2(rec + 16u)), 0u, 0xFFu, r);
  while (q.Proceed()) {}
  if (q.CommittedStatus() == COMMITTED_NOTHING) discard;
  const uint k = q.CommittedInstanceIndex(), seg = q.CommittedPrimitiveIndex();
  if (k >= grooms || seg > hair_primary_max_segment) discard;
  const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + k * 16u, 16);
  uint i0;
  if (!hp_segment(pool, pool_bytes, row, seg, i0) || row.y == 0u) discard;
  const float3 p0 = ldf3(hp_at(pool, row.x, i0, 12u)), p1 = ldf3(hp_at(pool, row.x, i0 + 1u, 12u));
  if (!hp_on_card(t_card, q.CommittedRayT(), cos_card, foot, length(p1 - p0))) discard;
}
