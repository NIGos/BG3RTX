#pragma once
// gi_far (docs/TRACK_C_GI.md "Far-field sun bounce"): the per-ray rule of the long GI rays, shared by
// gi_trace_rt.hlsl (RT_GI_FAR) and the analytic GPU fixture (tools/native_hit_gpu_fixture.hlsl case 10).
// Pure functions: no bindings, no counters.
//
// A ray reaches R = gi_far_m. A hit whose radiance is known replaces the native ambient for that ray:
// screen hits (last frame's radiance) and qualified native-material hits lit by the sun (and the froxel
// point/spot term, or none outside the froxel volume). Their weight is the far fade w = 1 - (t/R)^2.
// A qualified hit keeps the albedo share rho of the native ambient (gi_far_rho): its own ambient-lit
// radiance, approximated as rho x the receiver's ambient in that direction, which the direct terms lack.
// Every other hit (unqualified material, unsupported sun or lights, an aborted query) is what it is
// without gi_far: occlusion with the base radius's fade w0 = 1 - (t/R0)^2, nothing beyond R0, so the
// native ambient stays for it. A miss is a miss.

// The two weights of one ray. aborted: the closest hit is somewhere in [0, T], T = min(t, R) (R with
// nothing committed); w0 is then the base fade's mean over a uniform distance in [0, T]
// (1 - (T/R0)^2 / 3 for T <= R0, else (2/3) R0 / T; the SHaRC rule) and w is unused (never a radiance hit).
struct GiFarRay { float w, w0; };
GiFarRay gi_far_ray(float t, bool aborted, float R, float R0) {
  GiFarRay r;
  if (aborted) {
    const float T = t >= 0.0 ? min(t, R) : R;
    r.w = 0.0;
    r.w0 = T <= R0 ? 1.0 - (T / R0) * (T / R0) / 3.0 : (2.0 / 3.0) * R0 / max(T, 1e-6);
    return r;
  }
  const float k = saturate(t / R), k0 = saturate(t / R0);
  r.w = t >= 0.0 ? 1.0 - k * k : 0.0;
  r.w0 = t >= 0.0 ? 1.0 - k0 * k0 : 0.0;
  return r;
}

// The local (point/spot) term at a qualified hit.
static const uint kGiFarLocalKnown = 0u;    // native_hit_direct HdOk: complete
static const uint kGiFarLocalOutside = 1u;  // off screen and outside the froxel volume: no list exists, declared 0
static const uint kGiFarLocalUnknown = 2u;  // unsupported (domain, visibility, material)

struct GiFarHit { float3 bounce; float occ; bool radiance; };
// t: the hit distance (t >= 0, not aborted); local / sun: the terms (already x the AO-on-direct factor);
// rho: the hit's diffuse albedo luminance. A hit has radiance when its sun term is known and its local
// term is not unknown, or (as gi_hit_direct without gi_far) its local term is known within R0.
GiFarHit gi_far_hit(GiFarRay fr, float t, float R0, uint local_state, float3 local, bool sun_known, float3 sun, float rho) {
  GiFarHit h;
  h.radiance = sun_known ? local_state != kGiFarLocalUnknown : (local_state == kGiFarLocalKnown && t <= R0);
  const float3 l = local_state == kGiFarLocalKnown ? local : 0.0.xxx;
  h.bounce = h.radiance ? (l + (sun_known ? sun : 0.0.xxx)) * fr.w : 0.0.xxx;
  h.occ = h.radiance ? fr.w * (1.0 - saturate(rho)) : fr.w0;
  return h;
}
float gi_far_luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
