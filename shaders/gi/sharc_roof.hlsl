// sharc_debug=5 with the roof volumes bound (rt_sun_replace's trust image, binding 47): the
// mask of the QA gate roofed_brightening (docs/QA.md, scripts/qa/detectors.py). Written over
// the lit diffuse (the "debug diffuse" dump), per pixel with depth:
//   green 0.5  inside an asset-DB roof volume (roofed room, covered floor; padded like the
//              sun trust: rt_sun_roof_pad sideways, rt_sun_roof_up above);
//   red 0.5    outside every roof volume;
//   blue 0.5   not known: the level's volumes are not loaded.
// Sky stays black. No SHaRC headers needed.
#include "gi_common.hlsli"
#include "sun_trust.hlsli"

struct RoofCam2CB { float4 c[14]; };
[[vk::binding(48, 0)]] ConstantBuffer<RoofCam2CB> roofCam2;   // set 0 b2: c[13].xyz camera, absolute

float3 qrot_inv(float4 q, float3 v) {
  float3 u = -q.xyz;
  float3 t = 2.0 * cross(u, v);
  return v + q.w * t + cross(u, t);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  const float d = gDepth.Load(int3(fp, 0));
  float3 c = 0;
  if (d > 0.0) {
    // Whether the exterior is sunlit is decided by the QA from the game's sun mask (the
    // sun_dbg trace): a test of cam.v0 here marked Water Queen's House and the Campsite,
    // both in daylight, as "sun down" in the first in-game run.
    const bool known = (st_u(4) & 2u) != 0u;
    if (!known) {
      c = float3(0, 0, 0.5);
    } else {
      const float3 P = ray_at((float2(fp) + 0.5) / float2(pc.full)) * d;
      const float3 cam_abs = roofCam2.c[13].xyz;
      const uint n = min(st_u(0), 256u);
      bool inside = false;
      for (uint i = 0; i < n && !inside; ++i) {
        const uint a = 192u + i * 48u;
        const float4 ctr = st_f4(a);
        const float4 q = st_f4(a + 16u);
        const float3 h = st_f4(a + 32u).xyz;
        const float3 lo = qrot_inv(q, P - (ctr.xyz - cam_abs));
        inside = all(abs(lo) <= h);
      }
      c = inside ? float3(0, 0.5, 0) : float3(0.5, 0, 0);
    }
  }
  gDiffuse[fp] = c;
  gSpecular[fp] = 0.0.xxx;
}
