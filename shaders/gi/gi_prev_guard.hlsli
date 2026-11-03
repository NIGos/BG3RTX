// gi_prev_radiance_guard=1 (GI_PREV_RADIANCE_GUARD=1, docs/TRACK_C_GI.md "Reprojection guards"; from
// codex f32657e). Included right after gi_common.hlsli by the passes that read last frame's radiance
// (gi_trace, gi_trace_rt*, rt_reflections*), only with the define, so the key-off shaders are unchanged.
//
// gi_common.hlsli's prev_radiance_px gives the CURRENT pixel when the surface was off screen last frame
// (its reprojected position is outside the image), so the caller reads another surface's radiance:
// at the leading screen edge of a pan, the GI bounce and the tier-a reflections take the old light of
// whatever was at that pixel. Here such a pixel, or a non-finite motion vector, is rejected: the guarded
// prev_radiance_at / prev_bounce_at return NaN, which every caller already treats as "not visible"
// (all(isfinite(L))) and sends to its next tier: the hidden-hit fallback for the RT GI, the probe for the
// reflections; screen-space GI (gi_trace.hlsl) counts it as a ray that left the screen.
#ifndef GI_PREV_GUARD_HLSLI
#define GI_PREV_GUARD_HLSLI

static const float kPrevRejected = asfloat(0x7fc00000u);

bool prev_radiance_px_ok(int2 qp, out int2 pq) {
  pq = qp;
  if (pc.has_mv == 0) return true;   // no motion vectors: in-place history, as gi_common
  const float2 prev = float2(qp) + 0.5 + gVelocity.Load(int3(qp, 0)) * pc.mv_scale;
  if (!all(isfinite(prev)) || any(prev < 0.0) || any(prev >= float2(pc.full))) return false;
  pq = int2(floor(prev));
  return true;
}
float3 prev_radiance_at_guarded(int2 qp) {
  int2 pq;
  return prev_radiance_px_ok(qp, pq) ? prevRadiance[pq].rgb : kPrevRejected.xxx;
}
float3 prev_bounce_at_guarded(int2 qp) {
  int2 pq;
  if (!prev_radiance_px_ok(qp, pq)) return kPrevRejected.xxx;
  const float4 v = prevRadiance[pq];
  return v.rgb * saturate(v.a);
}
#if GI_SPEC_GUARDS
// Variant migration step G (docs/BUILD_AND_PACKAGE_EFFICIENCY.md): one module for both values of
// gi_prev_radiance_guard, chosen by specialization constant 1002 at pipeline creation (core/gi.cpp refresh).
#if GI_PREV_RADIANCE_GUARD
#error GI_SPEC_GUARDS replaces GI_PREV_RADIANCE_GUARD
#endif
[[vk::constant_id(1002)]] const uint kSpecPrevGuard = 0u;
float3 prev_radiance_at_sel(int2 qp) {
  if (kSpecPrevGuard != 0u) return prev_radiance_at_guarded(qp);
  return prev_radiance_at(qp);
}
float3 prev_bounce_at_sel(int2 qp) {
  if (kSpecPrevGuard != 0u) return prev_bounce_at_guarded(qp);
  return prev_bounce_at(qp);
}
#define prev_radiance_at prev_radiance_at_sel
#define prev_bounce_at prev_bounce_at_sel
#else
// Every later caller (this file's includer and rt/native_hit_shade.hlsli) gets the guarded reads.
#define prev_radiance_at prev_radiance_at_guarded
#define prev_bounce_at prev_bounce_at_guarded
#endif

#endif
