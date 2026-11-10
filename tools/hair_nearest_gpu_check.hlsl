// GPU half of the nearest-strand check (docs/RTX_HAIR_PRIMARY.md 5u; tests/hair_primary_gpu_test.py --nearest-spv; the
// same bindings and push block as tools/hair_primary_gpu_check.hlsl). The harness builds the groom twice (twin mode):
// instance 0, mask 0x02, is the swapped draw's groom (accepted); instance 1, mask 0x04, the same groom moved towards the
// camera, a card-kept groom in front. Per pixel ray, 8 words: (0, 1) the strand G-buffer variant's restart loop
// (shaders/rt/hair_primary_gbuffer.hlsl hp_trace, kHpSkips = 3): key and distance of the accepted hit, or no strand;
// (2, 3) hp_nearest (shaders/rt/hair_primary_nearest.hlsli): key and distance; (4, 5) the reference, the closest hit of
// instance 0 alone (mask 0x02): key and distance; (6) restarts the loop made; (7) 1 when hp_nearest passed over a
// candidate of instance 1.
#include "../shaders/rt/hair_primary.hlsli"
#include "../shaders/rt/hair_primary_nearest.hlsli"

[[vk::binding(0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1)]] ByteAddressBuffer gPoints;
[[vk::binding(2)]] ByteAddressBuffer gStarts;
[[vk::binding(3)]] RWByteAddressBuffer gOut;
struct Push {
  float4 pos_k;
  float4 right_w;
  float4 up_h;
  float4 forward;
  row_major float4x4 vp;
};
[[vk::push_constant]] Push pc;

static const uint kHpSkips = 3u;   // hair_primary_gbuffer.hlsl
struct FirstInstance { bool ok(uint k) { return k == 0u; } };

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
  uint o[8] = {hair_primary_no_strand, 0u, hair_primary_no_strand, 0u, hair_primary_no_strand, 0u, 0u, 0u};
  // (0, 1) hp_trace's loop: the closest strand; another row's: restart past it, at most kHpSkips times.
  float tmin = 0.0;
  uint restarts = 0u;
  for (uint i = 0u; i <= kHpSkips; ++i) {
    RayDesc s = r;
    s.TMin = tmin;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(gScene, 0u, 0x06u, s);
    while (q.Proceed()) {}
    if (q.CommittedStatus() == COMMITTED_NOTHING) break;
    if (q.CommittedInstanceIndex() != 0u) {
      ++restarts;
      tmin = q.CommittedRayT() + 2.0 * hair_primary_max_radius;
      continue;
    }
    o[0] = q.CommittedPrimitiveIndex();
    o[1] = asuint(q.CommittedRayT());
    break;
  }
  o[6] = restarts;
  // (2, 3) the candidate-filtered query.
  uint nk, np;
  float nt;
  bool skipped;
  FirstInstance acc;
  if (hp_nearest(gScene, r, 0x06u, acc, nk, np, nt, skipped)) { o[2] = (nk << 24) | np; o[3] = asuint(nt); }
  o[7] = skipped ? 1u : 0u;
  // (4, 5) the reference: instance 0 alone.
  RayQuery<RAY_FLAG_FORCE_OPAQUE> ref;
  ref.TraceRayInline(gScene, 0u, 0x02u, r);
  while (ref.Proceed()) {}
  if (ref.CommittedStatus() != COMMITTED_NOTHING) {
    o[4] = ref.CommittedPrimitiveIndex();
    o[5] = asuint(ref.CommittedRayT());
  }
  const uint at = (id.y * w + id.x) * 32u;
  [unroll] for (uint j = 0; j < 8u; ++j) gOut.Store(at + j * 4u, o[j]);
}
