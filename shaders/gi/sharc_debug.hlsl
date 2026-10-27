// dxc: -enable-16bit-types
// SHaRC debug views (sharc_debug, core/gi.cpp run_post after the injection), written over
// the lit diffuse like the rt_debug views (the dump "debug diffuse"):
//   1 the voxel at each pixel's surface, coloured by its hash and level (SDK
//     HashGridDebugColoredHash): the voxel size and the normal-octant split;
//   2 occupancy of the hash table (SDK HashGridDebugOccupancy, one 7x7 cell per entry);
//   3 the cached radiance at each pixel's own surface (what a GI ray hitting it off screen
//     would get); dark magenta where the cache has nothing;
//   4 the GI rays' tiers (gi_trace_rt_sharc.hlsl): red cache, green screen, blue no
//     radiance (gi_rt_fallback), as fractions;
//   5 roofed-room mask for the QA leak gate (sharc_roof.hlsl when the roof volumes are
//     bound); here, without them: blue, "not known".
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "sharc_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  const float d = gDepth.Load(int3(fp, 0));
  const float3 cam_abs = sharc_cam();
  float3 c = 0;
  if (shc.debug == 2u) {
    HashGridData hd;
    hd.capacity = shc.capacity;
    hd.hashEntriesBuffer = shHash;
    c = HashGridDebugOccupancy(fp, pc.full, hd) * 0.5;
  } else if (shc.debug == 5u) {
    c = d > 0.0 ? float3(0, 0, 0.5) : 0.0.xxx;
  } else if (d > 0.0) {
    const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
    const float3 P = ray_at(uv) * d;
    float3 N = 0;
    if (pc.rt_origin != 0u) N = rtOrigin[fp].xyz;
    if (!(dot(N, N) > 0.25)) N = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));
    N = sharc_face(N, P);
    if (shc.debug == 1u) {
      c = HashGridDebugColoredHash(P + cam_abs, N, sharc_grid(cam_abs)) * 0.5;
    } else if (shc.debug == 3u) {
      float3 L;
      c = sharc_lookup(cam_abs, P + cam_abs, N, L) ? L : float3(0.02, 0, 0.02);
    } else if (shc.debug == 4u) {
      c = gather_history(fp, d).rgb * 0.5;
    }
  }
  if (any(!isfinite(c))) c = 0;
  gDiffuse[fp] = c;
  gSpecular[fp] = 0.0.xxx;
}
