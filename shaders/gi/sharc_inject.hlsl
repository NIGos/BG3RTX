// dxc: -enable-16bit-types
// SHaRC v1 injection (sharc=1; docs/DESIGN_SOTA_NEXT.md 2.3 step 3, core/gi.cpp run_post).
//
// After the GI composite: one thread per sharc_inject_stride^2 block of render pixels, at a
// position inside the block that changes every frame (white noise), writes the radiance
// the camera sees there into the cache as a one-vertex SHaRC update:
//   the lit diffuse b10 (view-independent; the specular is left out, QUALITY Q8), times the
//   albedo on white-shaded skin (as the composite keeps it for prevRadiance), plus the D-EM
//   effects target's emission at that pixel (sharc_fx; the game's own emissive draws);
//   at the surface's absolute position, with its geometric normal facing the camera
//   (rt_origin.hlsl's, else the shading normal).
// b10 holds this frame's GI from the previous cache: the multi-bounce feedback prevRadiance
// already has; it converges because each bounce is multiplied by an albedo below 1.
// Sky (depth 0) and unlit pixels (the lighting does not write them) are skipped.
#define SHARC_UPDATE 1
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "sharc_common.hlsli"

[[vk::binding(49, 0)]] Texture2D<float4> gFx;   // D-EM effects target (sharc_fx = 1), else any image

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const float3 cam_abs = sharc_cam();
  // The camera of this injection, for the next resolve (SHaRC's previous camera).
  if (all(tid.xy == 0)) shState.Store4(0, asuint(float4(cam_abs, 1.0)));
  const uint s = max(shc.stride, 1u);
  const uint2 blocks = (pc.full + s - 1u) / s;
  if (any(tid.xy >= blocks)) return;
  const uint3 r = pcg3d(uint3(tid.xy, pc.frame * 4u + 3u));
  const uint2 fp = min(tid.xy * s + uint2(r.x % s, r.y % s), pc.full - 1u);
  const float d = gDepth.Load(int3(fp, 0));
  const uint cls = mat_class_at(fp);
  const bool skip = d <= 0.0 || cls == kMatUnlit;
  sharc_stat(kShInjectSkipped, skip);
  if (skip) return;

  float3 L = gDiffuse[fp];
  if (cls == kMatSkin && skin_white()) L *= gAlbedo.Load(int3(fp, 0)).rgb;
  if ((pc.sharc_fx & 1u) != 0u) {
    const float3 e = max(gFx.Load(int3(fp, 0)).rgb, 0.0) * shc.em_scale;
    sharc_stat(kShEmissive, any(e > 0.0));
    L += e;
  }
  if (any(!isfinite(L))) return;
  L = clamp(L, 0.0, shc.max_radiance);

  const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  const float3 P = ray_at(uv) * d;
  float3 N = 0;
  if (pc.rt_origin != 0u) N = rtOrigin[fp].xyz;
  if (!(dot(N, N) > 0.25)) N = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));
  N = sharc_face(N, P);

  SharcParameters sp = sharc_params(cam_abs);
  SharcState st;
  SharcInit(st);
  SharcHitData h;
  h.positionWorld = P + cam_abs;
  h.normalWorld = N;
  // SharcAddVoxelData truncates radiance x radiance_scale to integers: a uniform offset of
  // up to one step makes that unbiased (stochastic rounding) for dim interiors.
  const uint3 u = pcg3d(r ^ 0x9e3779b9u);
  const float3 dither = (float3(u >> 8) * (1.0 / 16777216.0)) / shc.radiance_scale;
  const bool ok = SharcUpdateHit(sp, st, h, L + dither, 0.0);
  sharc_stat(kShInjected, ok);
  sharc_stat(kShInsertFailed, !ok);
}
