// D-LS per-light data (docs/SE_BRIDGE.md, docs/TRACK_D_RT.md 9.8): joins the runtime
// lights the core uploaded (the Script Extender bridge merged with the asset database,
// core/ls_lights.cpp) to the lighting's light array b25, one thread per array slot.
//
// A slot matches the nearest uploaded light whose absolute position is within that
// light's tolerance (5 cm + its jitter for lights that stay put, more for item and
// character lights, which move between the bridge's 4 Hz exports) and whose radius
// agrees with 1/m0.w within 2%. The result, per slot, in lsSlots (4096 x 1):
//   x source radius (asfloat; 0xFFFFFFFF = no match), y Bg3RtLightFlags,
//   z owner | type << 8 | 1 << 16 (matched), w the uploaded light's index.
// Thread 0 also writes the camera position (set 0 b2 _m6, absolute) into the upload
// header, which the core reads back a few frames later to centre the next upload.
#include "gi_common.hlsli"
#include "../rt/bg3_lights.hlsli"

[[vk::binding(45, 0)]] RWByteAddressBuffer lsKnown;   // header (count, camera) + 48-byte records
[[vk::binding(46, 0)]] [[vk::image_format("rgba32ui")]] RWTexture2D<uint4> lsSlots;

groupshared float4 sPos[64];   // xyz absolute position, w radius
groupshared float4 sAux[64];   // x source radius, y tolerance
groupshared uint4 sU[64];      // flags, owner | type << 8 | channel << 16, db index, index

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  const uint li = tid.x;
  const uint n = min(lsKnown.Load(0), 1024u);
  const float3 cam = cam2.c[13].xyz;
  if (li == 0) lsKnown.Store3(4, asuint(cam));

  uint bytes;
  lLights.GetDimensions(bytes);
  bool valid = li < bytes / 80u;
  Bg3Light l = load_light(min(li, max(bytes / 80u, 1u) - 1u));
  valid = valid && l.inv_r > 0.0 && l.inv_r < 1e6 && all(isfinite(l.pos));
  const float r = valid ? 1.0 / l.inv_r : 0.0;
  const float3 w = l.pos + cam;

  float bq = 1e30;
  uint4 best = uint4(0xFFFFFFFFu, 0u, 0u, 0xFFFFFFFFu);
  for (uint base = 0; base < n; base += 64u) {
    uint k = base + gi;
    if (k < n) {
      uint a = 16u + k * 48u;
      sPos[gi] = asfloat(lsKnown.Load4(a));
      sAux[gi] = asfloat(lsKnown.Load4(a + 16u));
      sU[gi] = lsKnown.Load4(a + 32u);
    }
    GroupMemoryBarrierWithGroupSync();
    uint m = min(64u, n - base);
    if (valid) {
      for (uint j = 0; j < m; ++j) {
        float3 d = w - sPos[j].xyz;
        float q = dot(d, d);
        float tol = sAux[j].y;
        if (q <= tol * tol && q < bq && abs(sPos[j].w - r) <= 0.02 * max(r, 0.1) + 0.01) {
          bq = q;
          best = uint4(asuint(sAux[j].x), sU[j].x, (sU[j].y & 0xFFFFu) | 0x10000u, base + j);
        }
      }
    }
    GroupMemoryBarrierWithGroupSync();
  }
  if (li < 4096u) lsSlots[uint2(li, 0)] = best;
}
