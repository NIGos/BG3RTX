// GPU half of the strand shadow check (tests/hair_primary_gpu_test.py --shadow-spv; same bindings and push block as
// tools/hair_primary_gpu_check.hlsl): per pixel-centre camera ray, how many strand segments it crosses, counted two
// ways on the layer's LSS geometry: (0) the candidates of one non-opaque query (none committed), (1) successive
// closest hits (gi/hair_fibre.hlsl hf_strands). The CPU reference counts the capsules the ray enters.
#include "../shaders/rt/hair_primary.hlsli"

[[vk::binding(0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1)]] ByteAddressBuffer gPoints;
[[vk::binding(2)]] ByteAddressBuffer gStarts;
[[vk::binding(3)]] RWByteAddressBuffer gOut;     // 8 words per pixel
struct Push {
  float4 pos_k;
  float4 right_w;
  float4 up_h;
  float4 forward;
  row_major float4x4 vp;
};
[[vk::push_constant]] Push pc;

uint3 pcg3d(uint3 v) {   // gi/gi_common.hlsli
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  return v;
}

// rt_common.hlsli rt_groom_links on this check's segment start table (gStarts: start point of each segment).
void links(uint prim, out bool prev, out bool next) {
  uint bytes;
  gStarts.GetDimensions(bytes);
  prev = next = true;
  if ((prim + 2u) * 4u > bytes) return;
  const uint st = gStarts.Load(prim * 4u);
  prev = prim > 0u && gStarts.Load((prim - 1u) * 4u) + 1u == st;
  next = gStarts.Load((prim + 1u) * 4u) == st + 1u;
}

// (4) rt_common.hlsli rt_groom_blocks' estimator with a_f = 0.5 in every channel: rays that pass of kTrials.
static const uint kTrials = 256u;
uint passes(RayDesc r, float3 o) {
  uint lit = 0u;
  for (uint s = 0u; s < kTrials; ++s) {
    HpCrossings seen = hp_crossings_init();
    uint n = 0u;
    float3 T = 1.0.xxx;   // rt_groom_blocks: transmitted colour over the crossings, one decision on its luminance
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> q;
    q.TraceRayInline(gScene, 0u, 0xFFu, r);
    while (q.Proceed()) {
      bool lp, ln;
      links(q.CandidatePrimitiveIndex(), lp, ln);
      if (!hp_crossing(seen, q.CandidateInstanceIndex(), q.CandidatePrimitiveIndex(), lp, ln)) continue;
      T *= 0.5 * (n == 0u ? 0.7 : 1.0);
      ++n;
    }
    const uint3 h = pcg3d(uint3(asuint(o.x) ^ asuint(r.Direction.y), asuint(o.y) ^ asuint(r.Direction.z),
                                asuint(o.z) ^ (s * 0x85EBCA6Bu)));
    const bool blocked = n > 0u && float(h.x >> 8) * (1.0 / 16777216.0) >= dot(T, float3(0.2126, 0.7152, 0.0722));
    lit += blocked ? 0u : 1u;
  }
  return lit;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint w = asuint(pc.right_w.w), h = asuint(pc.up_h.w);
  if (id.x >= w || id.y >= h) return;
  const float k = pc.pos_k.w;
  const float x = (float(id.x) + 0.5 - 0.5 * float(w)) * k, y = -(float(id.y) + 0.5 - 0.5 * float(h)) * k;
  RayDesc r;
  r.Origin = pc.pos_k.xyz;
  r.TMin = 0.0;
  r.Direction = normalize(pc.forward.xyz + x * pc.right_w.xyz + y * pc.up_h.xyz);
  r.TMax = 1.0e5;
  // (0) candidates of one non-opaque query, (2) the same with adjacent segments of a strand counted once
  // (hp_crossing: the joint of two chained segments is reported by both), (1) successive closest hits, (3) the same
  // deduplicated against the previous hit.
  uint cand = 0u, cand_d = 0u;
  HpCrossings seen = hp_crossings_init();
  RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> q;
  q.TraceRayInline(gScene, 0u, 0xFFu, r);
  while (q.Proceed()) {
    ++cand;
    bool lp, ln;
    links(q.CandidatePrimitiveIndex(), lp, ln);
    cand_d += hp_crossing(seen, q.CandidateInstanceIndex(), q.CandidatePrimitiveIndex(), lp, ln) ? 1u : 0u;
  }
  const uint lit = passes(r, r.Origin);
  uint succ = 0u, succ_d = 0u, last = 0xFFFFFFFFu;
  [loop] for (uint it = 0; it < 64u; ++it) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> c;
    c.TraceRayInline(gScene, 0u, 0xFFu, r);
    while (c.Proceed()) {}
    if (c.CommittedStatus() == COMMITTED_NOTHING) break;
    const uint prim = c.CommittedPrimitiveIndex();
    ++succ;
    succ_d += (last == 0xFFFFFFFFu || (prim != last + 1u && prim + 1u != last)) ? 1u : 0u;
    last = prim;
    r.TMin = c.CommittedRayT() + 1e-5;
  }
  const uint at = (id.y * w + id.x) * 32u;
  gOut.Store(at, cand);
  gOut.Store(at + 4u, succ);
  gOut.Store(at + 8u, cand_d);
  gOut.Store(at + 12u, succ_d);
  gOut.Store(at + 16u, lit);
  [unroll] for (uint i = 5; i < 8u; ++i) gOut.Store(at + i * 4u, 0u);
}
