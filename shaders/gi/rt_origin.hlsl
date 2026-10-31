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
#ifdef RT_HAIR_PHYSICAL_ORIGIN
    rtHairOrigin[fp]=0.0.xxxx;
#endif
    return;
  }
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float dist = length(P);
  float3 V = -P / dist;
  float pa = pixel_angle(uv);
  float3 Ns = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));

  float4 o = float4(depth_geo_normal(int2(fp), P), kOriginNoHit);
#ifdef RT_HAIR_PHYSICAL_ORIGIN
  float4 physical_origin=0.0.xxxx;
  bool physical_candidate=false;
#endif
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
#if defined(RT_NATIVE_HAIR) || defined(RT_NATIVE_SCALP)
  RtHit h = rt_closest_hit(-V*t0,-V,dist+w-t0,cone,true);
#else
  RtHit h = rt_closest_hit(-V * t0, -V, dist + w - t0, cone);
#endif
  h.t = h.t >= 0.0 ? h.t + t0 : -1.0;
  if (h.t > 0.0) {
    float3 Ng = rt_hit_normal(h);
#ifdef RT_NATIVE_HAIR
    // The primary faux-depth surface's geometric normal comes from the actual
    // native depth field. The base-card normal belongs to physical secondaries.
    if((inst_info(h.inst).meta&kInfoHairProcedural)!=0u)Ng=o.xyz;
#endif
    float l = length(Ng);
    // The same surface as the depth buffer (see the window above); an aborted query's hit
    // is not known to be the closest one, so it never matches (the depth normal stays).
    float tol = depth_tol(dist, pa, dot(Ng / max(l, 1e-30), V), kJitterPx, 1.0) + kTlasRel * dist;
    if (l > 0.0 && !h.aborted && abs(h.t - dist) < tol) {
      Ng /= l;
      o = float4(dot(Ng, V) < 0.0 ? -Ng : Ng, h.t / dist - 1.0);
#ifdef RT_HAIR_PHYSICAL_ORIGIN
      if((inst_info(h.inst).meta&kInfoHairProcedural)!=0u) {
        HairPrimitive primitive=hair_primitive(h.inst,h.prim);
        physical_candidate=primitive.camera;
        // Start from camera zero, not the restricted virtual search window:
        // a large virtual displacement must not cause cancellation of tBase.
        float3 physical_o=0.0.xxx;
#ifdef RT_SCENE_REBASE
        physical_o=rt_scene_ray_origin(physical_o);
#endif
        if(primitive.valid && !primitive.inactive && primitive.camera) {
          HairTriangleHit card=hm_ray_triangle(primitive.p0,primitive.p1,primitive.p2,physical_o,-V);
          float physical_t=card.t;
          float3 physical_n=normalize(primitive.n);
          if(card.status==1u && physical_t>0.0 && isfinite(physical_t) && all(isfinite(physical_n)) && hpo_unit_normal(physical_n)) {
            physical_origin=float4(dot(physical_n,V)<0?-physical_n:physical_n,physical_t);
          }
        }
      }
#endif
    }
  }
  if (dot(o.xyz, o.xyz) < 0.5) o.xyz = Ns;   // degenerate: fall back to the shading normal
  else if (dot(o.xyz, V) < 0.0) o.xyz = -o.xyz;
  if (!all(isfinite(o))) o = float4(Ns, kOriginNoHit);
  rtOrigin[fp] = o;
#ifdef RT_HAIR_PHYSICAL_ORIGIN
  rtHairOrigin[fp]=physical_origin;
#endif
  if (pc.rt_stats != 0) {
    uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
    stat_add(71, true, bin);
    stat_add(72, o.w != kOriginNoHit, bin);
    stat_add(73, dot(o.xyz, Ns) < 0.9, bin);
#ifdef RT_HAIR_PHYSICAL_ORIGIN
    stat_add(188,physical_candidate,bin);
    stat_add(189,physical_origin.w>0.0,bin);
    stat_add(190,physical_candidate && physical_origin.w<=0.0,bin);
#endif
  }
}
