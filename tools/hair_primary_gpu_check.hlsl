// GPU half of the primary strand check (tools/hair_primary_gpu_check.cpp, tests/hair_primary_gpu_test.py): pixel-centre
// camera rays into an LSS groom, closest hit, then the same helpers the G-buffer variant uses
// (shaders/rt/hair_primary.hlsli): segment parameter, depth by the view-projection, fibre normal, RT0/RT3/RT2.w words.
#include "../shaders/rt/hair_primary.hlsli"

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
