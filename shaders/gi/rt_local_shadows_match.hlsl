// D-LS per-light data (docs/SE_BRIDGE.md, docs/TRACK_D_RT.md 9.8): joins the runtime
// lights the core uploaded (the Script Extender bridge merged with the asset database,
// core/ls_lights.cpp) to the lighting's light array b25, one thread per array slot.
//
// A slot matches the nearest uploaded light whose absolute position is within that
// light's position tolerance and whose radius agrees with 1/m0.w within that light's radius
// tolerance (both per record, derived in core/ls_lights.cpp: float precision and the
// bridge's rounding for lights that stay put, speed x export age for item, character and
// visual lights, which move between the bridge's 4 Hz exports). The result, per slot, in
// lsSlots row 0 (4096 x 3):
//   x source radius (asfloat; 0xFFFFFFFF = no match), y Bg3RtLightFlags,
//   z owner | type << 8 | 1 << 16 (matched), w the uploaded light's index.
// Rows 1-2 cache what that result was computed from: the slot's absolute position and
// radius (bits), and the upload's generation (it changes with the uploaded records). A
// slot whose inputs are unchanged keeps its result, and a group of 64 slots that are
// all unchanged skips the search (docs/TRACK_D_RT.md 9.9): the same result, not redone.
// ls_check=1 (pc.ls_pass bit 0) searches every slot and counts the kept results that differ
// from the search (rtStats 84 slots compared, 85 differing; core/gi.cpp logs them).
// Thread 0 also writes the camera position (set 0 b2 _m6, absolute) into the upload
// header, which the core reads back a few frames later to centre the next upload.
//
// Upload layout (core/ls_lights.cpp): header 32 bytes (uint count, float3 camera written
// here, uint generation, uint force = recompute every slot), then 48-byte records.
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/bg3_lights.hlsli"

[[vk::binding(45, 0)]] RWByteAddressBuffer lsKnown;   // header + 48-byte records
[[vk::binding(46, 0)]] [[vk::image_format("rgba32ui")]] RWTexture2D<uint4> lsSlots;

groupshared float4 sPos[64];   // xyz absolute position, w radius
groupshared float4 sAux[64];   // x source radius, y position tolerance, z intensity, w radius tolerance
groupshared uint4 sU[64];      // flags, owner | type << 8 | channel << 16, db index, index
groupshared uint sAny;         // a slot of this group needs the search

static const uint kKnownHeader = 32u;

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  const uint li = tid.x;
  const uint n = min(lsKnown.Load(0), 1024u);
  const uint gen = lsKnown.Load(16);
  const bool force = lsKnown.Load(20) != 0u;
  const float3 cam = cam2.c[13].xyz;
  if (li == 0) lsKnown.Store3(4, asuint(cam));
  if (gi == 0) sAny = 0u;

  uint bytes;
  lLights.GetDimensions(bytes);
  bool valid = li < bytes / 80u;
  Bg3Light l = load_light(min(li, max(bytes / 80u, 1u) - 1u));
  valid = valid && l.inv_r > 0.0 && l.inv_r < 1e6 && all(isfinite(l.pos));
  const float r = valid ? 1.0 / l.inv_r : 0.0;
  const float3 w = l.pos + cam;
  const uint4 key = uint4(asuint(w), asuint(r));
  const bool own = li < 4096u;
  const bool check = (pc.ls_pass & 1u) != 0u;
  const bool keep = valid && own && !force && all(lsSlots[uint2(li, 1)] == key) && lsSlots[uint2(li, 2)].x == gen;
  const bool need = valid && own && (!keep || check);
  GroupMemoryBarrierWithGroupSync();
  if (need) InterlockedOr(sAny, 1u);
  GroupMemoryBarrierWithGroupSync();
  if (own && !valid) {   // no match, and no cached key (a finite position never has these bits)
    lsSlots[uint2(li, 0)] = uint4(0xFFFFFFFFu, 0u, 0u, 0xFFFFFFFFu);
    lsSlots[uint2(li, 1)] = uint4(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
  }
  if (sAny == 0u) return;   // group-uniform: every slot keeps its result

  float bq = 1e30;
  uint4 best = uint4(0xFFFFFFFFu, 0u, 0u, 0xFFFFFFFFu);
  for (uint base = 0; base < n; base += 64u) {
    uint k = base + gi;
    if (k < n) {
      uint a = kKnownHeader + k * 48u;
      sPos[gi] = asfloat(lsKnown.Load4(a));
      sAux[gi] = asfloat(lsKnown.Load4(a + 16u));
      sU[gi] = lsKnown.Load4(a + 32u);
    }
    GroupMemoryBarrierWithGroupSync();
    uint m = min(64u, n - base);
    if (need) {
      for (uint j = 0; j < m; ++j) {
        float3 d = w - sPos[j].xyz;
        float q = dot(d, d);
        float tol = sAux[j].y;
        // Both tolerances come with the record (core/ls_lights.cpp, where they are derived):
        // y position (float ulps at its magnitude + the bridge's %.4f, or for moving lights
        // their speed x the export's age), w radius (%.3f + float ulps of 1/inv_r).
        if (q <= tol * tol && q < bq && abs(sPos[j].w - r) <= sAux[j].w) {
          bq = q;
          best = uint4(asuint(sAux[j].x), sU[j].x, (sU[j].y & 0xFFFFu) | 0x10000u, base + j);
        }
      }
    }
    GroupMemoryBarrierWithGroupSync();
  }
  if (check) {
    const bool same = all(lsSlots[uint2(li, 0)] == best);
    stat_add(116, keep, 0u);
    stat_add(117, keep && !same, 0u);
  }
  if (need) {
    lsSlots[uint2(li, 0)] = best;
    lsSlots[uint2(li, 1)] = key;
    lsSlots[uint2(li, 2)] = uint4(gen, 0u, 0u, 0u);
  }
}
