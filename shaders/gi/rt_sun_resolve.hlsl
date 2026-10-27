// rt_sun_replace, second pass (docs/TRACK_D_RT.md 12, core/sun_trust.h): writes the traced
// sun visibility into the game's sun mask where it is trusted, after gi/rt_shadow.hlsl has
// traced every pixel and folded min(game, traced) into the mask.
//
// Trust = the geometric trust rt_shadow.hlsl left in rtVis.a only: 0 under a roof volume of
// the asset database, else the off-screen (frustum) trust. QUALITY R0: nothing here depends
// on the game's own mask, so a pixel the game holds dark is replaced like any other where
// our BVH holds every caster (the old proximity-to-the-game's-lit-mask term is gone;
// rt_sun_near now only splits the in-frame A/B debug class below).
//
// rtVis in: r traced visibility, g the game's mask, b min(game, traced), a geometric trust;
// out: b the visibility written (before qa_zero), a 0.40 / 0.45 brightened (A/B classes).
//
// Formats: rtVis is R8G8B8A8_UNORM (core/gi.cpp g_rt_vis) and the game's mask gSunShadow is
// R8_UNORM (core/gi.cpp rejects any other format), so every value compared here is a code
// k/255; there is no fp16 stage between them.
#include "gi_common.hlsli"

[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;
#include "sun_trust.hlsli"

// The R8 code a UNORM store writes for v (Vulkan: round to nearest, 1/255 per step).
int r8_code(float v) { return int(round(saturate(v) * 255.0)); }

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  const float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return;
  const float4 v = rtVis[fp];
  const float vis = v.r, mask = v.g, fold = v.b, code = v.a;
  const bool traced = code > 0.06;
  const bool roof = traced && code < 0.375;
  const float t_off = saturate(code * 2.0 - 1.0);
  const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  const float dist = length(ray_at(uv)) * d;

  const float trust = roof ? 0.0 : t_off;
  const uint mode = st_u(24);
  // Untrusted: min(game, traced); under roofs rt_sun_roof_mode 1 keeps the game's mask, 2 shadows.
  const float keep = roof && mode == 1u ? mask : roof && mode == 2u ? 0.0 : fold;
  const float out_vis = lerp(mask, lerp(keep, vis, trust), saturate(pc.rt_strength));

  // In-frame A/B (like ls_check): this pixel's written value against min(game, traced),
  // both from the same frame, so animation and camera motion cannot fake a difference.
  // Tolerance: one R8 code step. Both sides are R8 UNORM codes (fold is read back from
  // rtVis.b, out_vis is stored as a code), so any difference that reaches the game's mask
  // is a whole number of steps and the smallest is 1/255; no smaller difference survives
  // the store, and no other rounding sits between them.
  const int dc = r8_code(out_vis) - r8_code(fold);
  const bool bright = dc >= 1;
  const bool darker = dc <= -1;   // only rt_sun_roof_mode 2 should produce these

  // Debug class only (rt_debug=7 and the "sun check" log; it never changes trust or the
  // mask): is a brightened pixel at the game's shadow edge? The edge is where the game's
  // filtered mask is partly lit (its own code > 0: inside the game's filter footprint of
  // an edge) or where a tap within rt_sun_near / 2 crosses 0.5 (the geometric edge of a
  // symmetric filter kernel) on a surface at a similar depth. rt_sun_near = 0: no split,
  // every brightened pixel counts as "at the edge" only by its own mask.
  uint lit_ring = mask > 0.0 ? 0u : 5u;
  const float D = st_f(28);
  if (bright && lit_ring != 0u && D > 0.0) {
    const float px = D / max(dist * pixel_angle(uv), 1e-6);   // rt_sun_near in pixels
    // Debug cost cap only (not a quality gate): taps beyond 96 px are clamped inwards.
    const float R = clamp(px, 2.0, 96.0);
    uint s = pixel_seed(fp, 5u);
    const float rot = rand01(s) * 6.2831853;
    const float dtol = D + 0.05 * d;   // same-surface test for the debug split
    [loop] for (uint ring = 1; ring <= 2 && lit_ring == 5u; ++ring) {
      const float r = R * float(ring) * 0.25;
      [unroll] for (uint k = 0; k < 8; ++k) {
        const float a = rot + (float(k) + 0.5 * float(ring & 1u)) * 0.78539816;
        int2 q = int2(float2(fp) + float2(cos(a), sin(a)) * r);
        if (any(q < 0) || any(q >= int2(pc.full))) continue;
        const float dq = gDepth.Load(int3(q, 0));
        if (dq <= 0.0 || abs(dq - d) > dtol) continue;
        if (rtVis[q].g > 0.5) lit_ring = ring;
      }
    }
  }
  const bool at_edge = lit_ring <= 2u;

  // rtVis.a codes for the debug views: 0.40 brightened at the game's shadow edge,
  // 0.45 brightened farther from it; otherwise the trust code.
  const float a_out = bright ? (at_edge ? 0.40 : 0.45) : code;
  rtVis[fp] = float4(vis, mask, out_vis, a_out);
  if (pc.rt_debug == 0) gSunShadow[fp] = out_vis;
  // QA ambient floor (docs/QA.md, qa_zero bit 0): no direct sun, the mask is 0 everywhere.
  // rt_shadow.hlsl does the same for rt_sun_replace=0; this pass runs after it.
  if ((pc.qa_zero & 1u) != 0u) gSunShadow[fp] = 0.0;

  // The farthest pixel whose trust depends on the roof test (traced, not already
  // untrusted off screen): header byte 48, read back by the core to size the roof query
  // (core/sun_trust.cpp: radius = this + rt_tmax). Positive floats order like their bits.
  const bool needs_roofs = traced && (roof || t_off > 0.0);
  const uint dm = WaveActiveMax(needs_roofs ? asuint(dist) : 0u);
  if (WaveIsFirstLane() && dm != 0u) InterlockedMax(sunTrust[uint2(12, 0)], dm);

  if ((st_u(4) & 8u) != 0) {
    const bool dis = traced && mask < 0.5 && vis > 0.5;
    trust_add(2, dis);
    trust_add(3, dis && trust >= 0.999);
    trust_add(4, dis && trust > 0.0 && trust < 0.999);
    trust_add(5, dis && roof);
    trust_add(6, dis && !roof && t_off <= 0.0);
    const uint gain = uint(max(dc, 0));   // in R8 codes (1/255)
    trust_add_n(10, gain);
    trust_add(16, bright);
    trust_add(17, bright && at_edge);
    trust_add(18, bright && !at_edge);
    trust_add(19, darker);
    trust_add_n(20, at_edge ? gain : 0u);
  }
}
