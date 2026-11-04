// Full resolution, after the tiled lighting: adds the one-bounce radiance,
// modulated by the diffuse colour, into the game's diffuse buffer (before its
// SSS and fog passes), then keeps this frame's lit radiance for next frame's
// ray hits.
#include "gi_common.hlsli"
#if RT_HAIR_FIBRE_GI_REFERENCE
#include "../rt/hair_fibre_gi.hlsli"
#endif

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) {
    prevRadiance[fp] = 0;
    return;
  }
  float4 gi = gather_history(fp, d);
  float4 alb = gAlbedo.Load(int3(fp, 0));
  float metal = gMaterial.Load(int3(fp, 0)).z;
  // The bounce is diffuse ambient: the same diffuse colour the game's own ambient
  // uses on this class (white on skin, whose albedo the SSS pass applies; nothing on
  // unlit pixels, which the lighting skips).
  const uint cls = mat_class_at(fp);
  float3 dcol = mat_diffuse_colour(cls, alb.rgb, metal);
  float3 add = gi.rgb * dcol * pc.strength;
#if RT_HAIR_FIBRE_GI_REFERENCE
  // Replace the Lambert response before adding it (and before saving the bounce source).
  // gi.rgb is the exact gathered GI used above; no subtraction of an estimated game ambient.
  const float4 fibre_mat = gMaterial.Load(int3(fp, 0));
  if (mat_class(fibre_mat) == kMatHair && hv_strand_pixel(fp, d))
    add = gi.rgb * hd_volume_albedo(saturate(alb.rgb), max(0.09, fibre_mat.x)) * pc.strength;
#endif
  float3 diffuse = gDiffuse[fp];
  if (pc.debug == 1 || pc.debug >= 4) diffuse = gi.rgb;
  else if (pc.debug == 2) diffuse = add * 4.0;
  else if (pc.debug == 3) diffuse = gi.aaa * 0.2;
  else diffuse += add;
  // Never hand the game a non-finite value: it would spread through bloom and DLSS.
  if (any(!isfinite(diffuse))) diffuse = gDiffuse[fp];
  gDiffuse[fp] = diffuse;
  // Next frame's bounce source is radiance: on white-shaded skin that is the
  // diffuse times the albedo the SSS pass will apply.
  float3 rad = cls == kMatSkin && skin_white() ? diffuse * alb.rgb : diffuse;
  // a: the diffuse's share of the luminance, so the GI can bounce the view-independent
  // part only (prev_bounce_at) while the reflections read the whole radiance.
  float3 tot = rad + gSpecular[fp];
  const float3 kY = float3(0.2126, 0.7152, 0.0722);
  float share = dot(tot, kY) > 1e-8 ? saturate(dot(max(rad, 0.0), kY) / dot(tot, kY)) : 1.0;
  // The sum of two finite lighting targets can exceed the rgba16f range.
  prevRadiance[fp] = float4(all(isfinite(tot)) ? clamp(tot, 0.0, 65504.0) : 0.0.xxx, share);
}
