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

float3 pos_at(int2 p) {
  p = clamp(p, int2(0, 0), int2(pc.full) - 1);
  float d = gDepth.Load(int3(p, 0));
  return ray_at((float2(p) + 0.5) / float2(pc.full)) * d;
}

// Geometric normal from the depth buffer: per axis the neighbour whose depth
// continues the surface best (smaller difference), so silhouettes do not bend it.
float3 depth_normal(int2 p, float3 P) {
  float3 xl = pos_at(p - int2(1, 0)), xr = pos_at(p + int2(1, 0));
  float3 yu = pos_at(p - int2(0, 1)), yd = pos_at(p + int2(0, 1));
  float3 dx = abs(length(xr) - length(P)) < abs(length(P) - length(xl)) ? xr - P : P - xl;
  float3 dy = abs(length(yd) - length(P)) < abs(length(P) - length(yu)) ? yd - P : P - yu;
  float3 n = cross(dy, dx);
  float l = length(n);
  return l > 1e-20 ? n / l : 0.0.xxx;
}

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

  float4 o = float4(depth_normal(int2(fp), P), kOriginNoHit);
  // Only a short window around the depth-buffer point is searched (cheap: no
  // traversal of what lies in front, e.g. foliage), +-1% of the distance or 16
  // pixel footprints, whichever is larger.
  float w = max(0.01 * dist, 16.0 * pa * dist);
  float t0 = max(dist - w, 0.0);
  RtCone cone = {t0 * pa, pa};
  RtHit h = rt_closest_hit(-V * t0, -V, dist + w - t0, cone);
  h.t = h.t >= 0.0 ? h.t + t0 : -1.0;
  if (h.t > 0.0) {
    float3 Ng = rt_tri_normal(h.geom, h.prim);
    float l = length(Ng);
    // The same surface as the depth buffer: within a few pixel footprints along the
    // view ray (more at grazing angles), and not a back-facing sliver.
    float tol = max(4.0 * pa * dist / max(abs(dot(Ng / max(l, 1e-30), V)), 0.05), 2e-4 * dist);
    if (l > 0.0 && abs(h.t - dist) < tol) {
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
