// Half-resolution screen-space diffuse GI: cosine-distributed rays marched
// against the linear depth buffer; a hit returns the lit diffuse + specular
// radiance the game just computed at that pixel (one bounce of direct light).
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  // Rotate the full-res sample inside each 2x2 block over frames.
  uint2 fp = min(id * 2 + uint2(pc.frame & 1, (pc.frame >> 1) & 1), pc.full - 1);
  float d = gDepth.Load(int3(fp, 0));
  float2 enc = gNormal.Load(int3(fp, 0)).xy;
  if (d <= 0.0 || dot(enc * 3.5554 - 1.7777, enc * 3.5554 - 1.7777) > 5.4e10) {
    giRaw[id] = float4(0, 0, 0, 0);
    return;
  }
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float3 N = to_lighting_space(decode_normal(enc));
  float3x3 inv = frustum_inverse();
  P += N * (d * 0.003);

  uint seed = hash(id.x * 1973u + id.y * 9277u + pc.frame * 26699u);
  float3 acc = 0;
  for (uint r = 0; r < pc.rays; ++r) {
    float3 dir = cosine_hemisphere(N, rand01(seed), rand01(seed));
    float tmax = d * pc.radius;
    float t = tmax / float(pc.steps * pc.steps) * (0.5 + rand01(seed));
    for (uint s = 1; s <= pc.steps; ++s) {
      float dt = tmax * float(s * s) / float(pc.steps * pc.steps);   // quadratic step growth
      float3 Q = P + dir * max(t, dt);
      float2 quv;
      float qd;
      if (!project(inv, Q, quv, qd) || any(quv < 0.0) || any(quv > 1.0)) break;
      int2 qp = int2(quv * float2(pc.full));
      float sd = gDepth.Load(int3(qp, 0));
      if (sd > 0.0 && qd > sd * 1.0015 && qd < sd * (1.0 + pc.thickness)) {
        acc += gDiffuse[qp] + gSpecular[qp];
        break;
      }
    }
  }
  giRaw[id] = float4(acc / float(max(pc.rays, 1u)), d);
}
