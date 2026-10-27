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
//   7 rt_sun_replace in-frame A/B: brightened against min(game, traced) by > 0.1 at the
//     game's shadow edge yellow, farther red, darker blue, over the lit diffuse in grey
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
    // (0.40 / 0.45: brightened pixels, rt_sun_resolve.hlsl; they were trusted: green)
    c = v.a < 0.06 ? float3(0.3, 0.3, 0.3) : v.a < 0.19 ? float3(1, 0, 1) : v.a < 0.31 ? float3(1, 0, 0)
        : v.a < 0.49 ? float3(0, 1, 0) : lerp(float3(0, 0.2, 1), float3(0, 1, 0), saturate(v.a * 2.0 - 1.0));
    c *= v.r > 0.5 ? 1.0 : 0.35;
    if (gDepth.Load(int3(fp, 0)) <= 0.0) c = 0.0;
  }
  else if (pc.rt_debug == 7) {
    // rt_sun_replace in-frame A/B: only the pixels the replace changed, over the frame's own
    // lit diffuse in dim grey. Yellow: brightened by > 0.1 against min(game, traced) at the
    // game's shadow edge (its mask partly lit, or lit within rt_sun_near / 2); red: brightened
    // farther from it; blue: darker than min. Exact codes, for scripts/qa.
    const float fold = v.g * lerp(1.0, v.r, saturate(pc.rt_strength));
    const float diff = v.b - fold;
    const float3 lit = gDiffuse[fp];
    c = min(dot(lit, float3(0.2126, 0.7152, 0.0722)), 1.0).xxx * 0.4;   // <= 0.2 after the 0.5 below
    if (diff > 0.1) c = abs(v.a - 0.40) < 0.02 ? float3(1, 1, 0) : float3(1, 0, 0);
    else if (diff < -0.1) c = float3(0, 0, 1);
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
