// rt_sun_replace, second pass (docs/TRACK_D_RT.md 12, core/sun_trust.h): writes the traced
// sun visibility into the game's sun mask where it is trusted, after gi/rt_shadow.hlsl has
// traced every pixel and folded min(game, traced) into the mask.
//
// Trust = the geometric trust rt_shadow.hlsl left in rtVis.a (0 under a roof volume of the
// asset database, the off-screen trust otherwise) x the proximity to the game's lit mask:
// the errors of the game's shadow map that R0 lets us correct (over-wide filtering, acne,
// cascade transitions) lie within rt_sun_near metres of a pixel the game has lit. A large
// area the game holds entirely dark (an interior, a cave, the shadow of a cut-away roof or
// of geometry never captured) stays min(game, traced) even when no roof volume covers it.
//
// rtVis in: r traced visibility, g the game's mask, b min(game, traced), a geometric trust;
// out: b the visibility written, a 0.125 where the proximity test failed.
#include "gi_common.hlsli"

[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;
#include "sun_trust.hlsli"

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

  // Proximity: the most lit game mask among taps on four rings out to rt_sun_near metres
  // (on surfaces at a similar depth), weighted down towards the outer ring.
  float near = mask;
  // The innermost ring with a lit game mask (> 0.5): 0 the pixel's own mask is partly lit
  // (a penumbra of the game's), 1-4 the ring, 5 none. Rings 0-2 (within half of
  // rt_sun_near) count as "at the game's shadow edge" in the in-frame A/B below.
  uint lit_ring = mask > 0.1 ? 0u : 5u;
  const float D = st_f(28);
  if (traced && !roof && t_off > 0.0 && mask < 0.999 && D > 0.0) {
    const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
    const float px = D / max(length(ray_at(uv)) * d * pixel_angle(uv), 1e-6);   // D in pixels
    const float R = clamp(px, 2.0, 96.0);
    uint s = pixel_seed(fp, 5u);
    const float rot = rand01(s) * 6.2831853;
    const float dtol = D + 0.05 * d;
    [loop] for (uint ring = 1; ring <= 4 && near < 0.999; ++ring) {
      const float r = R * float(ring) * 0.25;
      const float w = ring <= 2 ? 1.0 : ring == 3 ? 0.66 : 0.33;
      [unroll] for (uint k = 0; k < 8; ++k) {
        const float a = rot + (float(k) + 0.5 * float(ring & 1u)) * 0.78539816;
        int2 q = int2(float2(fp) + float2(cos(a), sin(a)) * r);
        if (any(q < 0) || any(q >= int2(pc.full))) continue;
        const float dq = gDepth.Load(int3(q, 0));
        if (dq <= 0.0 || abs(dq - d) > dtol) continue;
        const float gq = rtVis[q].g;
        near = max(near, gq * w);
        if (gq > 0.5) lit_ring = min(lit_ring, ring);
      }
    }
  }
  const float trust = roof ? 0.0 : t_off * saturate(near * 2.0 - 0.5);   // game lit >= 0.75 nearby: full
  const uint mode = st_u(24);
  // Untrusted: min(game, traced); under roofs rt_sun_roof_mode 1 keeps the game's mask, 2 shadows.
  const float keep = roof && mode == 1u ? mask : roof && mode == 2u ? 0.0 : fold;
  const float out_vis = lerp(mask, lerp(keep, vis, trust), saturate(pc.rt_strength));
  const bool far = traced && !roof && t_off > 0.0 && trust <= 0.0;
  // In-frame A/B (like ls_check): this pixel's replaced value against min(game, traced),
  // both from the same frame, so animation and camera motion cannot fake a difference.
  const float diff = out_vis - fold;
  const bool bright = diff > 0.1;
  const bool at_edge = lit_ring <= 2u;
  const bool darker = diff < -0.1;   // only rt_sun_roof_mode 2 should produce these
  // rtVis.a codes for the debug views: 0.40 brightened at the game's shadow edge,
  // 0.45 brightened farther from it; otherwise the trust code.
  const float a_out = bright ? (at_edge ? 0.40 : 0.45) : far ? 0.125 : code;
  rtVis[fp] = float4(vis, mask, out_vis, a_out);
  if (pc.rt_debug == 0) gSunShadow[fp] = out_vis;

  if ((st_u(4) & 8u) != 0) {
    const bool dis = traced && mask < 0.5 && vis > 0.5;
    trust_add(2, dis);
    trust_add(3, dis && trust >= 0.999);
    trust_add(4, dis && trust > 0.0 && trust < 0.999);
    trust_add(5, dis && roof);
    trust_add(6, dis && !roof && t_off <= 0.0);
    trust_add(11, dis && far);
    trust_add_n(10, uint(round(saturate(diff) * 255.0)));
    trust_add(16, bright);
    trust_add(17, bright && at_edge);
    trust_add(18, bright && !at_edge);
    trust_add(19, darker);
    trust_add_n(20, at_edge ? uint(round(saturate(diff) * 255.0)) : 0u);
  }
}
