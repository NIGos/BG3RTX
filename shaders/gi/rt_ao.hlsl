// Track D phase 2: ray-traced ambient visibility (rt_ao=1). Runs right after
// gi_trace on the same half-resolution samples and replaces their
// screen-space visibility (giRaw.a) with cosine rays against the frame's own
// geometry (TLAS, layer/rtscene.cpp); the bounce (rgb) stays screen-space.
// The temporal pass and gi_ao_apply then fold it into the game's AO as before,
// so off-screen and behind-the-surface occluders now count.
// (With gi_rt=1 the hybrid trace, gi_trace_rt.hlsl, does this itself.)
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  // The same full-resolution sample gi_trace used.
  uint2 fp = min(id * 2 + uint2(pc.frame & 1, (pc.frame >> 1) & 1), pc.full - 1);
  float d = gDepth.Load(int3(fp, 0));
  float2 enc = gNormal.Load(int3(fp, 0)).xy;
  float2 e = enc * 3.5554 - 1.7777;
  if (d <= 0.0 || dot(e, e) + 1.0 > 5.4e10) return;
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float3 N = to_lighting_space(decode_normal(enc));
  float dist = length(P);
  float3 o = P + N * (dist * pc.rt_bias);
  float pa = pixel_angle(uv);
  RtCone cone = {dist * pa * 2.0, pa * 2.0};   // half resolution

  uint seed = hash(id.x * 7307u + id.y * 3323u + pc.frame * 15013u);
  float occ = 0.0;
  for (uint r = 0; r < pc.rt_ao_rays; ++r) {
    float3 dir = cosine_hemisphere(N, rand01(seed), rand01(seed));
    float t = rt_closest(o, dir, pc.rt_ao_radius, cone);
    if (t >= 0.0) {
      // Closer occluders count more: a smooth falloff to zero at the radius.
      float k = t / pc.rt_ao_radius;
      occ += 1.0 - k * k;
    }
  }
  float4 g = giRaw[id];
  g.a = 1.0 - occ / float(pc.rt_ao_rays);
  giRaw[id] = g;
}
