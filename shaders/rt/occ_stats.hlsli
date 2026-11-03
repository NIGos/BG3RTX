// rt_occ_stats counters (common/occ_stats.h): device-local (set 4 binding 1, core/gi.cpp
// ensure_occ), reduced per 8x8 group in groupshared memory, one atomic per non-zero counter and
// group into one of 64 bins (the host sums them). Included after gi_common.hlsli and
// rt_common.hlsli with RT_OCC_STATS defined (g_occ_kind / g_occ_t of the last query).
#pragma once
[[vk::binding(1, 4)]] RWByteAddressBuffer occStats;
groupshared uint gsOcc[kOccCount];

// Before the group's first barrier.
void occ_clear(uint gi) {
  for (uint k = gi; k < kOccCount; k += 64u) gsOcc[k] = 0u;
}
void occ_add(uint k, uint v) {
  if (v != 0u) InterlockedAdd(gsOcc[k], v);
}
uint occ_q(float x) { return isfinite(x) && x > 0.0 ? uint(min(x, 64.0) * kOccQ + 0.5) : 0u; }
// After the group's last barrier: thread gi adds counters gi, gi + 64, ... to the bin of its group.
void occ_flush(uint gi, uint2 gid) {
  const uint bin = (gid.x * 7u + gid.y * 13u) & 63u;
  for (uint k = gi; k < kOccCount; k += 64u) {
    const uint v = gsOcc[k];
    if (v != 0u) occStats.InterlockedAdd((k * kOccBins + bin) * 4u, v);
  }
}
bool occ_character(uint cls) { return cls == kMatSkin || cls == kMatHair; }

// The class of the visible surface the hit h lies on (the depth there is the hit itself, the GI
// trace's test): 1 hair, 2 skin, 0 another class, off screen or hidden.
uint occ_screen_class(float3 h) {
  float2 quv;
  float qd;
  if (!project(frustum_inverse(), h, quv, qd) || any(quv < 0.0) || any(quv > 1.0)) return 0u;
  const int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
  const float sd = gDepth.Load(int3(qp, 0));
  if (!(sd > 0.0)) return 0u;
  const float4 oq = rt_origin_at(uint2(qp));
  const bool has_tlas = pc.rt_origin != 0u && oq.w < kOriginNoHit;
  const float3 ngq = pc.rt_origin != 0u ? oq.xyz : depth_geo_normal(qp, ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd);
  if (!depth_same_surface(qp, qd, sd, ngq, has_tlas, oq.w, max(pc.gi_rt_tol, 1.0))) return 0u;
  const uint c = mat_class(gMaterial.Load(int3(qp, 0)));
  return c == kMatHair ? 1u : c == kMatSkin ? 2u : 0u;
}

// One ray of source s from a character pixel, right after its query: vs its visibility (kRtLit /
// kRtOccluded / kRtUnknown), dark the darkening fraction this ray causes (attributed to its
// occluder kind, g_occ_kind; the hit point g_occ_p).
void occ_ray(uint s, uint vs, float dark) {
  const uint b = s * kOccSrcStride;
  occ_add(b + kOccRays, 1u);
  if (vs == kRtUnknown) {
    occ_add(b + kOccRaysUnknown, 1u);
    return;
  }
  if (vs != kRtOccluded) return;
  occ_add(b + kOccRaysOcc, 1u);
  const uint k = min(g_occ_kind, kOccKinds - 1u);
  occ_add(b + kOccKindRays + k, 1u);
  occ_add(b + kOccKindNear + k, g_occ_hit && g_occ_t <= kOccNear ? 1u : 0u);
  occ_add(b + kOccKindDark + k, occ_q(dark));
  if (g_occ_hit) {
    // common/occ_stats.h kOccHistEdge
    const float t = g_occ_t;
    const uint bin = t < 0.05 ? 0u : t < 0.1 ? 1u : t < 0.2 ? 2u : t < 0.5 ? 3u : t < 1.0 ? 4u : t < 2.0 ? 5u : t < 5.0 ? 6u : 7u;
    occ_add(b + kOccHistW + bin, occ_q(dark));
    occ_add(b + kOccHistN + bin, 1u);
    const uint c = occ_screen_class(g_occ_p);
    occ_add(b + kOccScrHair, c == 1u ? 1u : 0u);
    occ_add(b + kOccScrSkin, c == 2u ? 1u : 0u);
  }
}
// One character pixel of source s: dark its darkening fraction.
void occ_pixel(uint s, float dark) {
  const uint b = s * kOccSrcStride;
  occ_add(b + kOccPx, 1u);
  occ_add(b + kOccDarkPx, dark > 1.0 / 512.0 ? 1u : 0u);
  occ_add(b + kOccDark, occ_q(saturate(dark)));
}
