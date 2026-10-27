// Depth tolerances derived from the depth format and the pixel footprint
// (docs/QUALITY.md R0 "every gate needs a derivation", docs/QA.md "Thresholds and their
// derivation"). Replaces the fixed relative tolerances (3% GI on-screen hits, 3% reflections,
// 3% GI temporal, 1% bilateral sigma, the rt_origin window and match). docs/TRACK_C_GI.md
// "Derived depth tolerances" has the numbers.
//
// Included by gi_common.hlsli after ray_at / pixel_angle / gDepth.
//
// The question every caller asks: two depth values describe points of the SAME surface,
// `lat_px` render pixels apart on screen; how far apart along the view ray can they be?
//   slope      lat · f · tan(theta)    f = dist · pixel angle, the footprint of one pixel;
//                                      theta the angle between the geometric normal and V.
//                                      A plane's depth changes by tan(theta) per unit of
//                                      lateral offset (exact for a plane).
//   curvature  lat · f                 the sag of any circular profile over a chord s is
//                                      <= s (R <= s: sag <= R <= s; R >= s: sag <= s^2/R <= s),
//                                      so one footprint per pixel bounds what the plane misses.
//   precision  n · depth_prec(dist)    each depth-buffer value carries its own rounding.
// The coordinator's 1/|N.V| form is sqrt(tan^2 + 1) <= tan + 1: the sum above is that
// bound made explicit (slope and curvature can add up at the same point).
//
// Callers add, where it applies (each says so next to the call):
//   - the DLSS jitter: the depth buffer is rasterised at the jittered sample (Halton, +-0.5 px
//     per axis, docs/FRAME_MAP.md "Jitter"), so up to kJitterPx from the pixel centre, and
//     two frames' samples up to 2 kJitterPx apart;
//   - the TLAS-against-raster error kTlasRel, where a TLAS hit is compared with the raster
//     depth and no per-pixel TLAS reference (rtOrigin.w) is available.
#pragma once

// The depth chain: the game rasterises into D24S8, reversed Z, infinite far plane, near
// 0.1 (docs/FRAME_MAP.md steps 2-4 and "Evaluate"; TRACK_B_FG.md: _m1 near 0.1), and a
// later pass writes the R32F linear depth we read (gDepth). Reversed infinite Z stores
// z_ndc = n / z, so one rounding of z_ndc (half a D24 code, 2^-25) moves z by z^2 / n * 2^-25.
// The R32F storage adds half an fp32 ulp, z * 2^-24. If the linear depth came from a D32F
// instead, the D24 term is a strict over-estimate (fp32 rounds 2^-24 relative to z_ndc).
static const float kDepthNear = 0.1;            // m, the projection's near plane
static const float kD24HalfCode = 2.9802322e-8;  // 2^-25
static const float kF32HalfUlp = 5.9604645e-8;   // 2^-24
// DLSS jitter: Halton offsets within +-0.5 px per axis (docs/FRAME_MAP.md "Jitter",
// docs/QA.md "jitter"): at most 0.5 * sqrt(2) px from the pixel centre.
static const float kJitterPx = 0.70710678;
// fp16 storage of a unit normal (rtOrigin, RGBA16F): each component rounds by at most half
// an ulp below 1, 2^-12, so N.V moves by at most sqrt(3) * 2^-12.
static const float kNdvFp16 = 4.2286e-4;
// Measured, not derived: TLAS against raster depth. docs/TRACK_D_RT.md 7.1, primary-ray
// check (rt_debug=3, Steel Watch Foundry, rt_alpha=1): 99.09% of pixels have the TLAS hit
// within 0.5% of the raster depth along the pixel's ray; the 0.91% outside are other
// geometry in front (dithered LOD/cut-away fades 0.73%, cutouts 0.17%), i.e. not the same
// surface. 0.5% is the bin edge of that measurement: the error distribution inside it is
// not measured (rtOrigin.w holds it per pixel; a histogram of it would tighten this).
static const float kTlasRel = 0.005;

// Rounding of one depth-buffer value at camera distance `dist` (m), along the ray. The view
// z is <= dist, so dist bounds it from above.
float depth_prec(float dist) { return dist * dist * (kD24HalfCode / kDepthNear) + dist * kF32HalfUlp; }

// The N.V uncertainty of a geometric normal at this distance: the larger of the fp16
// storage (rtOrigin) and the depth buffer's own (a normal from depth differences across
// one footprint f, each difference uncertain by 2 roundings: angle ~ 2 * prec / f).
float ndv_eps(float dist, float pa) { return max(kNdvFp16, 2.0 * depth_prec(dist) / max(dist * pa, 1e-30)); }

// Largest difference along the view ray (m) between two depth values of the same surface,
// `lat_px` render pixels apart, `n_depth` of them read from the depth buffer.
//   dist  camera distance (m); pa  the angle of one render pixel there (pixel_angle);
//   ndv   N.V of the surface's GEOMETRIC normal (any sign; not the normal-mapped G-buffer
//         normal, which says nothing about the depth slope).
// N.V is first moved by its uncertainty towards grazing (conservative), and floored there:
// at |N.V| <= eps the slope is unresolved and tan(theta) is bounded by 1 / eps.
float depth_tol(float dist, float pa, float ndv, float lat_px, float n_depth) {
  const float eps = ndv_eps(dist, pa);
  const float c = max(abs(ndv) - eps, eps);
  const float tan_t = sqrt(max(1.0 - c * c, 0.0)) / c;
  return lat_px * dist * pa * (tan_t + 1.0) + n_depth * depth_prec(dist);
}

// Camera-relative position of the depth buffer at pixel p (clamped to the screen).
float3 depth_pos(int2 p) {
  p = clamp(p, int2(0, 0), int2(pc.full) - 1);
  float d = gDepth.Load(int3(p, 0));
  return ray_at((float2(p) + 0.5) / float2(pc.full)) * d;
}

// Geometric normal of the depth buffer at p (P its position): per axis the neighbour whose
// depth continues the surface best (smaller difference), so silhouettes do not bend it.
// 0 where it is degenerate (depth_tol then treats the slope as unresolved).
float3 depth_geo_normal(int2 p, float3 P) {
  float3 xl = depth_pos(p - int2(1, 0)), xr = depth_pos(p + int2(1, 0));
  float3 yu = depth_pos(p - int2(0, 1)), yd = depth_pos(p + int2(0, 1));
  float3 dx = abs(length(xr) - length(P)) < abs(length(P) - length(xl)) ? xr - P : P - xl;
  float3 dy = abs(length(yd) - length(P)) < abs(length(P) - length(yu)) ? yd - P : P - yu;
  float3 n = cross(dy, dx);
  float l = length(n);
  return l > 1e-20 ? n / l : 0.0.xxx;
}

// On-screen hit acceptance (GI and reflection rays): is a ray hit at frustum depth qd,
// projecting into pixel qp, the surface the depth buffer holds there (sd > 0)?
//   ng        geometric normal of the surface at qp (any sign, 0 if unknown);
//   has_tlas  tlas_rel is rtOrigin.w at qp: the TLAS surface along qp's ray sits at
//             sd * (1 + tlas_rel). Then hit and reference are both TLAS and the measured
//             TLAS-raster error does not apply; without it kTlasRel is added.
//   k         the ini multiplier (gi_rt_depth_k / rf_depth_k, 1 = as derived).
// Lateral offset: the hit lies anywhere in pixel qp (<= 0.5 sqrt 2 px from its centre) and
// the depth there was rasterised at the jittered sample (<= kJitterPx): 2 kJitterPx.
bool depth_same_surface(int2 qp, float qd, float sd, float3 ng, bool has_tlas, float tlas_rel, float k) {
  const float2 uvq = (float2(qp) + 0.5) / float2(pc.full);
  const float3 Pq = ray_at(uvq) * sd;
  const float dq = length(Pq);
  if (!(dq > 0.0)) return false;
  const float ref = has_tlas ? sd * (1.0 + tlas_rel) : sd;
  float tol = depth_tol(dq, pixel_angle(uvq), dot(ng, Pq) / dq, 2.0 * kJitterPx, 1.0);
  if (!has_tlas) tol += kTlasRel * dq;
  // qd and sd are frustum depths along the same ray: relative tolerances carry over.
  return abs(qd - ref) < ref * (k * tol / dq);
}
