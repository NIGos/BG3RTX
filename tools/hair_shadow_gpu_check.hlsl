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
    cand_d += hp_crossing(seen, q.CandidateInstanceIndex(), q.CandidatePrimitiveIndex()) ? 1u : 0u;
  }
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
  [unroll] for (uint i = 4; i < 8u; ++i) gOut.Store(at + i * 4u, 0u);
}
