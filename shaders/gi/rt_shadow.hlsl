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
// b: primary-ray check (1 match within depth_tol, 0.66 hit in front, 0.33 hit behind, 0 miss
//    or aborted; rt_debug=3/5), the in-front distance log2(e)/8 in tolerances (rt_debug=8),
//    else the visibility written into the mask;
// a: which geometry an in-front/behind error comes from (x3: 1 alpha vt, 2 alpha tex, 3 opaque;
//    rt_debug=3/5), 1 alpha-tested / 0.5 opaque hit in front (rt_debug=8), else the trust from
//    the geometry: 0 not traced (back face), 0.25 under a roof volume, 0.5 + 0.5 x the
//    off-screen trust, kSunUnknownCode: the ray was aborted by the candidate cap (unknown),
//    the game's mask is kept. (rt_sun_resolve.hlsl overwrites a with 0.40 / 0.45 for the
//    pixels it brightened; there is no proximity code any more.)
[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;
// 8/255 in the RGBA8 image: 8 codes above "not traced" (0) and 7 below the 0.06 (15.3
// codes) that rt_sun_resolve.hlsl reads as "traced", so it is untraced there, and
// rt_debug=6 tells it apart from 0 by v.a > 4/255 (half-way between the two codes).
static const float kSunUnknownCode = 8.0 / 255.0;

// ---- rt_sun_replace: where the traced visibility replaces the game's mask (core/sun_trust.h,
// docs/TRACK_D_RT.md 12; the header layout is in sun_trust.hlsli).
#include "sun_trust.hlsli"
struct SunCam2CB { float4 c[14]; };
[[vk::binding(48, 0)]] ConstantBuffer<SunCam2CB> sunCam2;   // set 0 b2: c[13].xyz camera, absolute

float3 qrot_inv(float4 q, float3 v) {   // rotate by the conjugate of q (world -> local)
  float3 u = -q.xyz;
  float3 t = 2.0 * cross(u, v);
  return v + q.w * t + cross(u, t);
}

// Does the ray o + t d, t >= 0, pass through a roof volume (camera-relative o)?
bool sun_under_roof(float3 o, float3 d, float3 cam_abs) {
  const uint n = min(st_u(0), 256u);
  for (uint i = 0; i < n; ++i) {
    const uint a = 192u + i * 48u;   // kSunHeader
    float4 c = st_f4(a);
    float4 q = st_f4(a + 16u);
    float3 h = st_f4(a + 32u).xyz;
    float3 lo = qrot_inv(q, o - (c.xyz - cam_abs));
    float3 ld = qrot_inv(q, d);
    float3 inv = 1.0 / select(abs(ld) > 1e-8, ld, select(ld >= 0.0, 1e-8, -1e-8));
    float3 t1 = (-h - lo) * inv, t2 = (h - lo) * inv;
    float3 tn = min(t1, t2), tf = max(t1, t2);
    float t0 = max(max(tn.x, tn.y), tn.z), t3 = min(min(tf.x, tf.y), tf.z);
    if (t3 >= max(t0, 0.0)) return true;
  }
  return false;
}

// Off-screen trust: 1 when the sun ray stays inside the view frustum (the captured
// region, + margin) until it has risen above the scene top, or `rise` above P;
// ramps to 0 as the inside part shrinks below `ramp` of that path.
float sun_offscreen_trust(float3 P, float3 L, float cam_y) {
  const float rise = st_f(8);
  const float top = st_f(12) - cam_y;   // camera-relative
  const float m = st_f(16);
  const float ramp = saturate(st_f(20));
  if (L.y < 0.01) return 0.0;
  const float t_need = min((max(top, P.y + rise) - P.y) / L.y, pc.rt_tmax);
  // x(t) = (d, d a, d b) of P + t L: linear in t, so each frustum side is a linear test.
  float3x3 inv = frustum_inverse();
  float3 x0 = mul(inv, P), x1 = mul(inv, L);
  float2 abA = uv_to_ab(float2(-m, -m)), abB = uv_to_ab(float2(1.0 + m, 1.0 + m));
  float2 lo = min(abA, abB), hi = max(abA, abB);
  float4 f0 = float4(x0.y - lo.x * x0.x, hi.x * x0.x - x0.y, x0.z - lo.y * x0.x, hi.y * x0.x - x0.z);
  float4 f1 = float4(x1.y - lo.x * x1.x, hi.x * x1.x - x1.y, x1.z - lo.y * x1.x, hi.y * x1.x - x1.z);
  float t_exit = 1e30;
  [unroll] for (int k = 0; k < 4; ++k)
    if (f1[k] < 0.0) t_exit = min(t_exit, max(f0[k], 0.0) / -f1[k]);
  if (x1.x < 0.0) t_exit = min(t_exit, max(x0.x, 0.0) / -x1.x);   // behind the camera
  const float frac_in = t_exit / max(t_need, 1e-3);
  return saturate((frac_in - ramp) / max(1.0 - ramp, 1e-3));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  g_rt_pass = kRtPassSun;
  if (all(fp == 0u)) {   // read back by the core
    const uint3 c = asuint(sunCam2.c[13].xyz);
    st_store(36u, c.x);
    st_store(40u, c.y);
    st_store(44u, c.z);
  }
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
  bool unknown = false;
#ifdef RT_SUN_CURRENT
  bool current_covers=false;
  uint native_samples=0u,native_reason=0u;
#endif
  if (nl > 0.0 || backlit) {
    RtCone cone = {dist * pa, pa};
    float3 dir = L;
#ifdef RT_SUN_CURRENT
    float3 t=abs(L.y)<0.99?normalize(cross(float3(0,1,0),L)):normalize(cross(float3(0,0,1),L));
    float3 b=cross(L,t);
#endif
    if (pc.sun_radius > 0.0) {   // uniform sample of the sun's disc (half-angle sun_radius)
      uint s = pixel_seed(fp, 1u);
      float u1 = rand01(s), u2 = rand01(s);
#ifndef RT_SUN_CURRENT
      float3 t = abs(L.y) < 0.99 ? normalize(cross(float3(0, 1, 0), L)) : float3(1, 0, 0);
      float3 b = cross(L, t);
#endif
      float r = sqrt(u1) * tan(pc.sun_radius), phi = 6.2831853 * u2;
      dir = normalize(L + (t * cos(phi) + b * sin(phi)) * r);
    }
    // From the side the light comes from (the back side for transmission).
    float3 o = rt_ray_origin(fp, P, N, dist * pa, dir);
#ifdef RT_SUN_CURRENT
    NativeSunResult native=sun_native_visibility(P,d,o,dir,L,t,b,pc.sun_radius,cone,N,rt_origin_at(fp).xyz);
    unknown=native.unknown;current_covers=!native.unknown;native_samples=native.samples;native_reason=native.reason;
    vis=unknown?mask:native.visibility;
#else
    const uint vs = rt_visibility(o, dir, pc.rt_tmax, cone);
    // Unknown (the candidate cap aborted the query, rt_common.hlsli): the game's own sun
    // visibility for this pixel, its mask value. One ray per pixel, so the decision is per
    // ray and per pixel at once; the pixel is marked (rtVis.a kSunUnknownCode below) so that
    // the fold, the trust and rt_sun_resolve all keep the game's mask there.
    unknown = vs == kRtUnknown;
    vis = unknown ? mask : vs == kRtOccluded ? 0.0 : 1.0;
#endif
  }
  float check = 0.0, fam = 0.0;
  const bool primary_check = pc.rt_debug == 3 || pc.rt_debug == 5 || pc.rt_debug == 8;
  if (primary_check) {
    RtCone cone = {0.0, pa};
    RtDebugHit h = rt_closest_debug(float3(0, 0, 0), P / dist, dist * 1.5 + 1.0, cone);
    // Same surface or not: the derived tolerance (shaders/rt/depth_tol.hlsli; was a fixed
    // 0.5%). The raster depth was sampled at the jittered position (<= kJitterPx from the
    // centre this ray goes through), one depth-buffer rounding, plus the measured
    // TLAS-against-raster error kTlasRel (no per-pixel TLAS reference here: this IS the
    // check). N.V from the geometric normal: rtOrigin's (rt_origin=1), else the depth's.
    const float4 org = rt_origin_at(fp);
    const float3 ng = org.w < kOriginNoHit ? org.xyz : depth_geo_normal(int2(fp), P);
    const float tol = depth_tol(dist, pa, dot(ng, P) / dist, kJitterPx, 1.0) + kTlasRel * dist;
    const float diff = h.t - dist;   // < 0: the TLAS hit lies in front of the raster surface
    check = h.t < 0.0 || h.aborted ? 0.0 : abs(diff) < tol ? 1.0 : diff < 0.0 ? 0.66 : 0.33;
    bool front = check > 0.5 && check < 0.9, behind = check > 0.2 && check < 0.5;
    // Who is to blame: in front, the geometry hit (opaque, or an alpha-tested
    // draw of family 1/2); behind, the nearest alpha-tested candidate let through.
    uint who = front ? (h.alpha ? h.hit_kind : 3u) : behind ? h.near_kind : 0u;
    fam = float(who) / 3.0;
    const float e8 = front ? saturate(log2(max(-diff / tol, 1.0)) / 8.0) : 0.0;
    const float f8 = front ? (h.alpha ? 1.0 : 0.5) : 0.0;
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
    if (pc.rt_debug == 8) {
      // QA view (scripts/qa tlas_front): how far in front, in units of the tolerance,
      // log2-encoded so that 8 bits span e = 1..256 (one code = a factor 1.022):
      // b = log2(-diff / tol) / 8 where in front, else 0. a: 1 alpha-tested hit (raster and
      // trace run the same alpha test there, so a disagreement is an artifact: the gate),
      // 0.5 opaque hit (cut-away walls, dithered LOD fades: the TLAS legitimately holds
      // geometry the raster faded out; review), 0 not in front / no depth.
      check = e8;
      fam = f8;
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
  // The fold: min(game, traced), so off-screen casters come from the game's cascades.
  // rt_sun_replace: gi/rt_sun_resolve.hlsl then writes the traced visibility itself where
  // it is trusted; this pass leaves the trust from the geometry in rtVis.a (see the top).
  // An unknown pixel keeps the game's mask exactly (vis = mask would give mask^2 here).
  const float fold = unknown ? mask : mask * lerp(1.0, vis, saturate(pc.rt_strength));
  const uint tflags = st_u(4);
  // (An unknown pixel counts as not traced: rt_sun_resolve.hlsl then keeps `fold`, the mask.)
  const bool traced = (nl > 0.0 || backlit) && !unknown;
  bool roof = false;
  float t_off = 1.0;
  if (traced && ((tflags & 1u) != 0 || pc.rt_debug == 6)) {
    const float3 cam_abs = sunCam2.c[13].xyz;
    if ((tflags & 2u) != 0) roof = sun_under_roof(P, L, cam_abs);
    if ((tflags & 4u) != 0) t_off = sun_offscreen_trust(P, L, cam_abs.y);
  }
#ifdef RT_SUN_CURRENT
  if(traced) {
    // Explicit finite native caster domain, exact selected/next membership.
    // Unsupported auxiliary/domain/material cases keep the native reference.
    t_off=current_covers?1.0:0.0;
    if(current_covers) roof=false;
  }
  if((tflags&8u)!=0u) {
    trust_add(21u,traced && current_covers);
    trust_add(22u,traced && !current_covers);
    trust_add(23u,(nl>0.0 || backlit) && !sun_current_scene_ready());
    uint packet=gSunParameters.Load(1200u);
    trust_add(24u,(nl>0.0 || backlit) && (packet&2u)==0u);
    trust_add(25u,(nl>0.0 || backlit) && (packet&4u)==0u);
    trust_add(26u,(nl>0.0 || backlit) && (packet&8u)==0u);
    trust_add(27u,native_reason==2u);trust_add(28u,native_reason==3u);trust_add(29u,native_reason==4u);
    trust_add(30u,native_samples>0u);trust_add(30u,native_samples>1u);
    trust_add(31u,current_covers && native_samples==0u);
  }
#endif
  if (!primary_check) {
    check = fold;
    fam = unknown ? kSunUnknownCode : !traced ? 0.0 : roof ? 0.25 : 0.5 + 0.5 * t_off;
  }
  rtVis[fp] = float4(vis, mask, check, fam);
  if ((tflags & 8u) != 0) {
    trust_add(15, true);
    trust_add(0, traced);
    trust_add(1, traced && vis > 0.5);
    trust_add(7, traced && roof);
    trust_add(8, traced && !roof && t_off < 0.999);
    trust_add(9, traced && mask > 0.5 && vis < 0.5);
    // The highest visible point (absolute y) within the ray length, read back by the core:
    // the scene top. (Sky domes and distant backdrops are depth-written far away.)
    uint yu = asuint(P.y + sunCam2.c[13].y);
    yu = dist > pc.rt_tmax ? 0u : (yu & 0x80000000u) != 0 ? ~yu : yu | 0x80000000u;
    uint ym = WaveActiveMax(yu);
    if (WaveIsFirstLane()) InterlockedMax(sunTrust[uint2(8, 0)], ym);
  }
  if (mask > 0.0 && pc.rt_debug == 0) gSunShadow[fp] = fold;

  // QA ambient floor (docs/QA.md, qa_zero bit 0): no direct sun (rt_sun_resolve.hlsl repeats it).
  if ((pc.qa_zero & 1u) != 0u) gSunShadow[fp] = 0.0;
}
