// Half-resolution screen-space GI, run before BG3's tiled lighting: cosine
// rays marched against the linear depth. A hit counts as occlusion of the
// game's ambient and returns last frame's lit radiance at that pixel (the
// bounce); a miss leaves the ambient to the game's own IBL.
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  // Rotate the full-res sample inside each 2x2 block over frames.
  uint2 fp = min(id * 2 + uint2(pc.frame & 1, (pc.frame >> 1) & 1), pc.full - 1);
  float d = gDepth.Load(int3(fp, 0));
  float2 enc = gNormal.Load(int3(fp, 0)).xy;
  float2 e = enc * 3.5554 - 1.7777;
  if (d <= 0.0 || dot(e, e) + 1.0 > 5.4e10) {
    giRaw[id] = float4(0, 0, 0, 1);
    giRawDepth[id] = 0;
    return;
  }
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float3 N = to_lighting_space(decode_normal(enc));
  float3x3 inv = frustum_inverse();
  if (pc.debug == 4) {  // round-trip check: project(P) must give back uv and d
    float2 ruv;
    float rd;
    project(inv, P, ruv, rd);
    giRaw[id] = float4(abs(ruv - uv) * 100.0, abs(rd / d - 1.0) * 100.0, 1);
    giRawDepth[id] = d;
    return;
  }
  if (pc.debug == 5) {  // normal in lighting space, and ray length relative to the screen
    giRaw[id] = float4(N * 0.5 + 0.5, 1);
    giRawDepth[id] = d;
    return;
  }
  // Lengths are relative to the distance from the camera: the frustum rays carry
  // the far-plane scale, so linear depth alone is not a distance.
  float dist = length(P);
  P += N * (dist * 0.003);

  uint seed = hash(id.x * 1973u + id.y * 9277u + pc.frame * 26699u);
  float3 bounce = 0;
  float hits = 0;
  float steps2 = float(pc.steps * pc.steps);
  for (uint r = 0; r < pc.rays; ++r) {
    float3 dir = cosine_hemisphere(N, rand01(seed), rand01(seed));
    float tmax = dist * pc.radius;
    float jitter = rand01(seed);
    for (uint s = 1; s <= pc.steps; ++s) {
      float t = tmax * (float(s * s) - jitter * float(2 * s - 1)) / steps2;   // jittered quadratic steps
      float3 Q = P + dir * t;
      float2 quv;
      float qd;
      if (!project(inv, Q, quv, qd) || any(quv < 0.0) || any(quv > 1.0)) break;
      int2 qp = int2(quv * float2(pc.full));
      float sd = gDepth.Load(int3(qp, 0));
      if (sd > 0.0 && qd > sd * 1.0015 && qd < sd * (1.0 + pc.thickness)) {
        bounce += prevRadiance[qp].rgb;
        hits += 1.0;
        break;
      }
    }
  }
  float n = float(max(pc.rays, 1u));
  giRaw[id] = float4(bounce / n, 1.0 - hits / n);
  giRawDepth[id] = d;
}
