// Track D debug views (rt_debug), written over the lit diffuse at the end of
// the tiled lighting so they show up in every later pass and dump:
//   1 traced sun visibility (white lit, black shadowed)
//   2 game's sun mask in red, traced visibility in green (yellow: both lit), and in
//     blue the visibility written into the mask (rt_sun_replace)
//   3 primary-ray check of the TLAS against the depth buffer: green match
//     (within 0.5%), red geometry in front, blue behind, magenta miss
//   4 accumulated ambient visibility as folded into the game's AO (ray-traced
//     with rt_ao=1, screen-space otherwise)
//   6 rt_sun_replace trust: red roof volume, magenta far from the game's lit mask, blue
//     off-screen fallback, green trusted, grey not traced
// Views 1-3, 5, 6 need rt_shadows=1 (they read its visibility image).
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"

[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float4 v = rtVis[fp];
  float3 c;
  if (pc.rt_debug == 1) c = v.rrr;
  else if (pc.rt_debug == 2) c = float3(v.g, v.r, v.b);   // blue: what was written into the mask
  else if (pc.rt_debug == 6) {
    // rt_sun_replace trust: red under a roof volume, magenta far from any lit game mask,
    // blue off-screen fallback (the game's cascades), green trusted, grey not traced;
    // dim where the traced ray found an occluder.
    c = v.a < 0.06 ? float3(0.3, 0.3, 0.3) : v.a < 0.19 ? float3(1, 0, 1) : v.a < 0.375 ? float3(1, 0, 0)
                                          : lerp(float3(0, 0.2, 1), float3(0, 1, 0), saturate(v.a * 2.0 - 1.0));
    c *= v.r > 0.5 ? 1.0 : 0.35;
    if (gDepth.Load(int3(fp, 0)) <= 0.0) c = 0.0;
  }
  else if (pc.rt_debug == 5) {
    // The primary-ray check's errors by origin: in front on opaque geometry
    // purple, on a virtual-texture cutout red, on a plain-texture cutout orange;
    // behind (a cutout let through) blue for vt, cyan for tex; match dark green.
    uint who = uint(v.a * 3.0 + 0.5);
    float k = v.b;
    bool front = k > 0.5 && k < 0.9, behind = k > 0.2 && k < 0.5;
    c = k > 0.9 ? float3(0, 0.25, 0) : k < 0.2 ? float3(1, 0, 1) : float3(0.5, 0.5, 0.5);
    if (front) c = who == 3 ? float3(0.6, 0, 1) : who == 1 ? float3(1, 0, 0) : who == 2 ? float3(1, 0.5, 0) : c;
    if (behind) c = who == 1 ? float3(0, 0.3, 1) : who == 2 ? float3(0, 1, 1) : c;
    if (gDepth.Load(int3(fp, 0)) <= 0.0) c = 0.0;
  }
  else if (pc.rt_debug == 4) {
    float d = gDepth.Load(int3(fp, 0));
    c = d > 0.0 ? gather_history(fp, d).aaa : 0.0;
  }
  else {
    float k = v.b;
    c = k > 0.9 ? float3(0, 1, 0) : k > 0.5 ? float3(1, 0, 0) : k > 0.2 ? float3(0, 0.3, 1) : float3(1, 0, 1);
    if (gDepth.Load(int3(fp, 0)) <= 0.0) c = 0.0;
  }
  gDiffuse[fp] = c * 0.5;
  gSpecular[fp] = float3(0, 0, 0);
}
