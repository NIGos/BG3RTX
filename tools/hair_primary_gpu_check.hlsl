// GPU half of the primary strand check (tools/hair_primary_gpu_check.cpp, tests/hair_primary_gpu_test.py): pixel-centre
// camera rays into an LSS groom, closest hit, then the same helpers the G-buffer variant uses
// (shaders/rt/hair_primary.hlsli): segment parameter, depth by the view-projection, fibre normal, RT0/RT3/RT2.w words.
// Built a second time with -DHP_CHECK_COVERAGE=1 (M3.2): the two rays of hp_cov_offset and hp_cov_pick per pixel; the
// frame jitter is folded into pc.forward by the caller and the frame tag is pc.forward.w (uint bits).
#include "../shaders/rt/hair_primary.hlsli"
#ifndef HP_CHECK_COVERAGE
#define HP_CHECK_COVERAGE 0
#endif
#if HP_CHECK_COVERAGE
#include "../shaders/rt/hair_primary_coverage.hlsli"
#endif

[[vk::binding(0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1)]] ByteAddressBuffer gPoints;    // float3 per point
[[vk::binding(2)]] ByteAddressBuffer gStarts;    // first point of each segment
[[vk::binding(3)]] RWByteAddressBuffer gOut;     // 8 words per pixel
struct Push {
  float4 pos_k;     // camera position, metres per pixel at unit distance
  float4 right_w;   // unit right, width (as uint bits)
  float4 up_h;      // unit up, height (as uint bits)
  float4 forward;   // unit forward
  row_major float4x4 vp;   // camera-relative view-projection (row vectors), the native depth = z / w
};
[[vk::push_constant]] Push pc;

#if HP_CHECK_COVERAGE
// Coverage mode, 8 words per pixel: kept segment key (or no strand), pick (0 A, 1 B, 2 none), kept hit distance,
// hit bits (1 A, 2 B), ray A key, ray B key, ray A distance, ray B distance.
bool cov_ray(float2 at, out uint key, out float t) {
  const float k = pc.pos_k.w;
  const uint w = asuint(pc.right_w.w), h = asuint(pc.up_h.w);
  const float x = (at.x - 0.5 * float(w)) * k, y = -(at.y - 0.5 * float(h)) * k;
  RayDesc r;
  r.Origin = pc.pos_k.xyz;
  r.TMin = 0.0;
  r.Direction = normalize(pc.forward.xyz + x * pc.right_w.xyz + y * pc.up_h.xyz);
  r.TMax = 1.0e5;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(gScene, 0u, 0xFFu, r);
  while (q.Proceed()) {}
  key = hair_primary_no_strand;
  t = 0.0;
  if (q.CommittedStatus() == COMMITTED_NOTHING) return false;
  key = (q.CommittedInstanceIndex() << 24) | q.CommittedPrimitiveIndex();
  t = q.CommittedRayT();
  return true;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint w = asuint(pc.right_w.w), h = asuint(pc.up_h.w);
  if (id.x >= w || id.y >= h) return;
  uint key[2];
  float t[2];
  bool hit[2];
  [unroll] for (uint i = 0; i < 2u; ++i) hit[i] = cov_ray(float2(id.xy) + 0.5 + hp_cov_offset(id.xy, i), key[i], t[i]);
  const uint pick = hp_cov_pick(hit[0], hit[1], t[0], t[1], id.xy, asuint(pc.forward.w));
  uint o[8] = {pick < 2u ? key[pick] : hair_primary_no_strand, pick, asuint(pick < 2u ? t[pick] : 0.0),
               (hit[0] ? 1u : 0u) | (hit[1] ? 2u : 0u), key[0], key[1], asuint(t[0]), asuint(t[1])};
  const uint at = (id.y * w + id.x) * 32u;
  [unroll] for (uint j = 0; j < 8u; ++j) gOut.Store(at + j * 4u, o[j]);
}
#else
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint w = asuint(pc.right_w.w), h = asuint(pc.up_h.w);
  if (id.x >= w || id.y >= h) return;
  const float k = pc.pos_k.w;
  const float x = (float(id.x) + 0.5 - 0.5 * float(w)) * k, y = -(float(id.y) + 0.5 - 0.5 * float(h)) * k;
  const float3 dir = normalize(pc.forward.xyz + x * pc.right_w.xyz + y * pc.up_h.xyz);
  RayDesc r;
  r.Origin = pc.pos_k.xyz;
  r.TMin = 0.0;
  r.Direction = dir;
  r.TMax = 1.0e5;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(gScene, 0u, 0xFFu, r);
  while (q.Proceed()) {}
  uint o[8] = {hair_primary_no_strand, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
  if (q.CommittedStatus() != COMMITTED_NOTHING) {
    const uint seg = q.CommittedPrimitiveIndex();
    const uint i0 = gStarts.Load(seg * 4u);
    const float3 p0 = asfloat(gPoints.Load3(i0 * 12u)), p1 = asfloat(gPoints.Load3((i0 + 1u) * 12u));
    const float t = q.CommittedRayT();
    const float3 hit = pc.pos_k.xyz + dir * t;
    const float v = hp_segment_param(hit, p0, p1);
    const float4 c = mul(float4(hit - pc.pos_k.xyz, 1.0), pc.vp);
    const float3 tan = normalize(p1 - p0);
    const float3 n = hp_fibre_normal(tan, -dir);
    const float3 nv = float3(dot(n, pc.right_w.xyz), dot(n, pc.up_h.xyz), dot(n, pc.forward.xyz));
    const float2 rg = hp_stereo_encode(nv);
    const uint3 t3 = uint3(round(hp_oct12_encode(tan) * 255.0));
    o[0] = (q.CommittedInstanceIndex() << 24) | seg;
    o[1] = asuint(v);
    o[2] = asuint(t);
    o[3] = asuint(c.z / c.w);
    o[4] = asuint(rg.x);
    o[5] = asuint(rg.y);
    o[6] = t3.x | (t3.y << 8) | (t3.z << 16);
    o[7] = uint(round(hp_rt2_w(kHairPrimaryClass, 3u, false) * 255.0));
  }
  const uint at = (id.y * w + id.x) * 32u;
  [unroll] for (uint i = 0; i < 8u; ++i) gOut.Store(at + i * 4u, o[i]);
}
#endif
