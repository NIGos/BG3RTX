// rt_hit_emission (docs/PLAN_M4_REFLECTIONS_GI.md M4.4, docs/TRACK_D_EMISSIVE.md 13.23): the emitted
// radiance of a ray hit on an emissive surface, from that surface's D-EM record.
//
// Needs em_common.hlsli (the host's emissive scene, Bg3HostApi::rt_emissive) included with the
// consumer's EM_SET. The lookup is exactly the projected check's (rt/em_debug.hlsl): em_lookup over
// [t - eps, t + eps] of the main TLAS hit, eps = rt_em_tol x the hit's camera distance + 1 cm, the
// record's em_radiance at the hit's uv with a ray-cone footprint. Units: the effects target's
// (pre-exposed HDR), the same as the lit buffer (compose adds the effects target to it as is).
//
// Record policy (only what the projected check validates; tools/native_hit_gpu_fixture case 7):
//   exact families (no kEmFlagApprox): used;
//   approximate families: used only once the on-screen calibration scaled them (kEmFlagCalibrated);
//   excluded families (kFlagExcluded, emissive_policy.h: rt_em_fx_pulse=0 drops 304be1c7): never
//   (the layer does not capture them, so this is a defensive second gate).
// Emission is two-sided here, as in the projected check (the game's cull state per draw is not tracked).
#pragma once

static const uint kHeUsed = 0u;          // emissive hit, radiance applied
static const uint kHeNone = 1u;          // the hit is not on a captured emitter (or the scene is empty)
static const uint kHeExcluded = 2u;      // excluded family record
static const uint kHeUnvalidated = 3u;   // approximate family not (yet) calibrated by the projected check
static const uint kEmFlagExcluded = 1u << 7;   // common/emissive_policy.h kFlagExcluded

struct HitEmission { float3 radiance; uint status; uint rec; };

// The pass Push (gi_common.hlsli) fields the emission dispatches reuse (unused by every other shader;
// core/gi.cpp hit_em_push): flags bit 0 also screen hits (rt_hit_emission=2), the relative tolerance.
#define HIT_EM_FLAGS pc.ls_kmode
#define HIT_EM_TOL pc.rt_ao_radius

uint hit_em_policy(uint flags) {
  if ((flags & kEmFlagExcluded) != 0u) return kHeExcluded;
  if ((flags & kEmFlagApprox) != 0u && (flags & kEmFlagCalibrated) == 0u) return kHeUnvalidated;
  return kHeUsed;
}

// The emission at the main-TLAS hit at distance t along (o, d) (lighting space, d unit). `cam_rel`
// is the hit's camera-relative position (its length is the camera distance the projected check's
// tolerance scales with), `footprint` the ray cone's width at the hit (world units), `cam_abs` the
// absolute camera position (set 0 b2 _m6: em_shift), `tol` rt_em_tol.
HitEmission native_hit_emission(float3 o, float3 d, float t, float3 cam_rel, float footprint, float3 cam_abs, float tol) {
  HitEmission h;
  h.radiance = 0.0.xxx;
  h.status = kHeNone;
  h.rec = 0xFFFFFFFFu;
  if (!(t >= 0.0) || emHeader.Load(3 * 4) == 0u) return h;
  const float eps = length(cam_rel) * max(tol, 0.0) + 0.01;
  float3 L;
  uint rec;
  if (!em_lookup(o, d, t, eps, max(footprint, 1e-6), em_shift(cam_abs), L, rec)) return h;
  h.rec = rec;
  h.status = hit_em_policy(emRecs[rec].flags);
  if (h.status == kHeUsed) h.radiance = L;
  return h;
}

// Counters (core/gi.cpp kHitEmFirst 331, logged every 300 frames as "hit emission"): per domain
// (0 GI hidden hits, 1 GI screen hits, 2 D-RF tier b, 3 D-RF tier a) lookups, emissive hits used,
// emissive hits skipped by the record policy, the added luminance x 1024 (clamped at 64); then
// excluded-family hits, unvalidated-family hits and clamped values over all domains.
static const uint kHitEmStatFirst = 331u;
void hit_em_count(uint domain, HitEmission h, float added, uint bin) {
  const uint base = kHitEmStatFirst + domain * 4u;
  stat_add(base, true, bin);
  stat_add(base + 1u, h.status == kHeUsed, bin);
  stat_add(base + 2u, h.status == kHeExcluded || h.status == kHeUnvalidated, bin);
  const uint v = uint(clamp(added, 0.0, 64.0) * 1024.0 + 0.5);
  const uint s = WaveActiveSum(v);
  if (WaveIsFirstLane() && s) rtStats.InterlockedAdd(((base + 3u) * 64u + (WaveReadLaneFirst(bin) & 63u)) * 4u, s);
  stat_add(kHitEmStatFirst + 16u, h.status == kHeExcluded, bin);
  stat_add(kHitEmStatFirst + 17u, h.status == kHeUnvalidated, bin);
  stat_add(kHitEmStatFirst + 18u, added > 64.0, bin);
}
