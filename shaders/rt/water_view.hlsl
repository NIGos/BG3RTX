// M5.2 water reflection views (rt_debug 21-24 with rt_water_reflections=1, core/water.cpp).
// Recorded at the end of every forward pass whose first or last fragment shader is an
// audited water FS (common/water_policy.h), over the game's HDR scene colour (the forward
// target: STORAGE in its usage, trace 20261030-152728 i50311 usage 31), so the view shows on
// top of the water the game just drew. Only pixels the lighting-time pass classified are
// written (the rest of the frame is the game's); each later water pass is overwritten again.
//   21 coverage (PLAN_M5 M5.1): green water in front of the bed, blue water over an empty
//      G-buffer pixel, yellow at the bed within the tolerance, red BEHIND the bed (violation)
//   22 the reflection radiance L where resolved (tier (a), unweighted), magenta a TLAS miss
//      (sky: unresolved), dark blue every other unresolved ray (hidden, off screen, aborted)
//   23 tiers: green screen hit, blue hidden on screen, cyan off screen, magenta miss, red
//      aborted, white invalid ray
//   24 E * L where resolved (the Fresnel-weighted term M5.3 would put in place of the FS's
//      probe term, before the water's opacity and fog), black elsewhere on water
// Exact codes (QA): the values above are written as they are (no exposure, no tonemap of ours).
[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> wOut;
[[vk::binding(1, 0)]] [[vk::image_format("rg32f")]] RWTexture2D<float2> wT;
[[vk::binding(2, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> target;

struct Push { uint2 full; uint view; uint pad; };
[[vk::push_constant]] Push pc;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  const uint cls = uint(wT[fp].y + 0.5);
  if (cls == 0u) return;
  const float4 o = wOut[fp];
  const uint tier = o.a > 0.0 ? 1u : uint(-o.a + 0.5);
  float3 c;
  if (pc.view == 21u) {
    c = cls == 1u ? float3(0, 1, 0) : cls == 2u ? float3(0, 0, 1) : cls == 3u ? float3(1, 1, 0) : float3(1, 0, 0);
  } else {
    if (cls != 1u && cls != 2u) return;   // not traced: the game's pixel
    if (pc.view == 22u) c = tier == 1u ? o.rgb : tier == 4u ? float3(1, 0, 1) : float3(0, 0, 0.3);
    else if (pc.view == 23u)
      c = tier == 1u ? float3(0, 1, 0) : tier == 2u ? float3(0, 0, 1) : tier == 3u ? float3(0, 1, 1)
        : tier == 4u ? float3(1, 0, 1) : tier == 5u ? float3(1, 0, 0) : float3(1, 1, 1);
    else c = tier == 1u ? o.rgb * o.a : float3(0, 0, 0);
  }
  target[fp] = max(c, 0.0);
}
