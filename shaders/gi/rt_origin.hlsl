// Robust secondary-ray origins (rt_origin=1; docs/QUALITY.md Q10, docs/AUDIT_RT.md fix 6).
// Full resolution, first of the ray-traced passes. Per pixel it finds the TLAS
// triangle the camera sees there (a primary ray, alpha-tested) and writes into
// rtOrigin (binding 40, rt/rt_common.hlsli):
//   xyz  the triangle's geometric normal, facing the camera;
//   w    t_hit / |P| - 1, the exact surface along the pixel's view ray relative to
//        the depth-buffer point (small, so fp16 keeps it precise),
//        or kOriginNoHit when no triangle matches the depth buffer within tolerance:
//        then xyz is the geometric normal of the depth buffer (cross product of
//        the position differences towards the flatter neighbours).
// rt_ray_origin() (rt_common.hlsli) then offsets the exact hit point along the
// geometric normal, towards the side the ray leaves on, by Wachter & Binder's
// float-ULP offset (Ray Tracing Gems ch. 6), instead of 0.002 x distance along
// the shading normal.
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"

// The depth buffer's geometric normal: depth_geo_normal (rt/depth_tol.hlsli).

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) {
    rtOrigin[fp] = float4(0, 0, 0, kOriginNoHit);
    return;
  }
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float dist = length(P);
  float3 V = -P / dist;
  float pa = pixel_angle(uv);
  float3 Ns = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));

  float4 o = float4(depth_geo_normal(int2(fp), P), kOriginNoHit);
  // The TLAS surface is the depth buffer's when they differ along this pixel's ray by no
  // more than (rt/depth_tol.hlsli) the slope + curvature over the jitter (the raster depth
  // is the jittered sample's, the primary ray goes through the pixel centre: kJitterPx),
  // one depth rounding, and the measured TLAS-raster error kTlasRel (the 0.5% bin of the
  // rt_debug=3 check, TRACK_D_RT 7.1). The slope: the hit triangle's own normal for the
  // match; the depth buffer's normal for the search window (before the hit is known).
  // Only that window around the depth-buffer point is searched (cheap: no traversal of
  // what lies in front, e.g. foliage). Was: window max(1% dist, 16 footprints), match
  // max(4 footprints / max(|N.V|, 0.05), 2e-4 dist).
  float w = depth_tol(dist, pa, dot(o.xyz, V), kJitterPx, 1.0) + kTlasRel * dist;
  float t0 = max(dist - w, 0.0);
  RtCone cone = {t0 * pa, pa};
  RtHit h = rt_closest_hit(-V * t0, -V, dist + w - t0, cone);
  h.t = h.t >= 0.0 ? h.t + t0 : -1.0;
  if (h.t > 0.0) {
    float3 Ng = rt_hit_normal(h);
    float l = length(Ng);
    // The same surface as the depth buffer (see the window above); an aborted query's hit
    // is not known to be the closest one, so it never matches (the depth normal stays).
    float tol = depth_tol(dist, pa, dot(Ng / max(l, 1e-30), V), kJitterPx, 1.0) + kTlasRel * dist;
    if (l > 0.0 && !h.aborted && abs(h.t - dist) < tol) {
      Ng /= l;
      o = float4(dot(Ng, V) < 0.0 ? -Ng : Ng, h.t / dist - 1.0);
    }
  }
  if (dot(o.xyz, o.xyz) < 0.5) o.xyz = Ns;   // degenerate: fall back to the shading normal
  else if (dot(o.xyz, V) < 0.0) o.xyz = -o.xyz;
  if (!all(isfinite(o))) o = float4(Ns, kOriginNoHit);
  rtOrigin[fp] = o;
  if (pc.rt_stats != 0) {
    uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
    stat_add(71, true, bin);
    stat_add(72, o.w != kOriginNoHit, bin);
    stat_add(73, dot(o.xyz, Ns) < 0.9, bin);
  }
}
