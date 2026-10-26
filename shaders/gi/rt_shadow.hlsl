// Track D phase 2: ray-traced sun shadows against the frame's own G-buffer
// geometry (TLAS built by the host, layer/rtscene.cpp), folded into the game's
// screen-space sun shadow mask before the tiled lighting, so BG3's own sun
// term shades with it. Runs in place of the screen-space contact shadows.
//
// Space: the TLAS holds camera-relative world positions (W of the G-buffer
// VS), the lighting reconstructs P = ray(uv) * linear depth in the same space
// (checked with rt_debug=3), and cam.v0 points towards the sun.
// Phase 3: alpha-tested geometry (grates, chains, foliage) lets light through
// where the game's own alpha test discards (rt_common.hlsli).
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"

// r: traced sun visibility, g: the game's sun mask before the fold,
// b: primary-ray check (1 match, 0.66 hit in front, 0.33 hit behind, 0 miss; rt_debug=3/5 only)
// a: which geometry an in-front/behind error comes from (x3: 1 alpha vt, 2 alpha tex, 3 opaque)
[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  float mask = gSunShadow[fp];
  if (d <= 0.0) {
    rtVis[fp] = float4(1, mask, 0, 1);
    return;
  }
  float3 L = normalize(cam.v0.xyz);
  float3 N = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float dist = length(P);
  float pa = pixel_angle(uv);

  // Back-lit pixels: the skin and thin-translucency permutations add transmitted sun
  // `T * mask * sun * saturate((0.2588 - N.L) * 0.631)` (118833ff:1905-1920, 19763259:1750),
  // and hair has no N.L gate (3033c895:1739), so their mask must hold the sun's
  // visibility from the far side too. Other back-facing pixels never read it: left as is.
  const float nl = dot(N, L);
  const uint cls = mat_class(gMaterial.Load(int3(fp, 0)));
  const bool backlit = pc.rt_backlit != 0 && nl <= 0.0 && mat_transmits(cls);
  float vis = pc.rt_backlit != 0 ? 1.0 : 0.0;   // (legacy: back faces were zeroed)
  if (nl > 0.0 || backlit) {
    RtCone cone = {dist * pa, pa};
    float3 dir = L;
    if (pc.sun_radius > 0.0) {   // uniform sample of the sun's disc (half-angle sun_radius)
      uint s = pixel_seed(fp, 1u);
      float u1 = rand01(s), u2 = rand01(s);
      float3 t = abs(L.y) < 0.99 ? normalize(cross(float3(0, 1, 0), L)) : float3(1, 0, 0);
      float3 b = cross(L, t);
      float r = sqrt(u1) * tan(pc.sun_radius), phi = 6.2831853 * u2;
      dir = normalize(L + (t * cos(phi) + b * sin(phi)) * r);
    }
    // From the side the light comes from (the back side for transmission).
    float3 o = rt_ray_origin(fp, P, N, dist * pa, dir);
    vis = rt_occluded(o, dir, pc.rt_tmax, cone) ? 0.0 : 1.0;
  }
  float check = 0.0, fam = 0.0;
  if (pc.rt_debug == 3 || pc.rt_debug == 5) {
    RtCone cone = {0.0, pa};
    RtDebugHit h = rt_closest_debug(float3(0, 0, 0), P / dist, dist * 1.5 + 1.0, cone);
    float rel = h.t / dist - 1.0;
    check = h.t < 0.0 ? 0.0 : abs(rel) < 0.005 ? 1.0 : rel < 0.0 ? 0.66 : 0.33;
    bool front = check > 0.5 && check < 0.9, behind = check > 0.2 && check < 0.5;
    // Who is to blame: in front, the geometry hit (opaque, or an alpha-tested
    // draw of family 1/2); behind, the nearest alpha-tested candidate let through.
    uint who = front ? (h.geom == 0 ? 3u : h.hit_kind) : behind ? h.near_kind : 0u;
    fam = float(who) / 3.0;
    if (pc.rt_stats != 0) {
      uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
      stat_add(0, true, bin);
      stat_add(1, check > 0.9, bin);
      stat_add(2, front, bin);
      stat_add(3, behind, bin);
      stat_add(4, check < 0.2, bin);
      stat_add(5, front && who == 3, bin);   // in front: opaque geometry
      stat_add(6, front && who == 1, bin);   // in front: alpha-tested, virtual texture
      stat_add(7, front && who == 2, bin);   // in front: alpha-tested, plain texture
      stat_add(13, behind && who == 1, bin); // behind: passed through a vt cutout
      stat_add(14, behind && who == 2, bin); // behind: passed through a tex cutout
    }
  }
  if (pc.rt_stats != 0) {
    // Back-lit transmitting pixels (N.L < 0.2588, where the transmission lobe is
    // non-zero): how lit the game's mask is there, and how lit the trace is.
    uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
    bool tr = mat_transmits(cls) && nl < 0.25881907;
    stat_add(39, true, bin);
    stat_add(40, tr, bin);
    stat_add(41, tr && mask > 0.5, bin);
    stat_add(42, tr && vis > 0.5, bin);
    stat_add(43, tr && mask > 0.5 && vis < 0.5, bin);
    stat_add(44, cls == kMatTranslucent, bin);
    stat_add(45, cls == kMatSkin, bin);
    stat_add(46, cls == kMatHair, bin);
    stat_add(47, cls != kMatStandard, bin);
  }
  rtVis[fp] = float4(vis, mask, check, fam);
  // Only ever darkens: off-screen casters still come from the game's cascades.
  if (mask > 0.0 && pc.rt_debug == 0) gSunShadow[fp] = mask * lerp(1.0, vis, saturate(pc.rt_strength));
}
