// D-EM debug views, rt_debug 6-8 (core/emissive.cpp). One primary ray per pixel against
// the emissive TLAS (rt/em_common.hlsli): a hit within rt_em_tol of the pixel's depth is
// a visible emissive surface, and its radiance is evaluated from the draw's record. The
// game's effects target at the same pixel is the reference: both are in the effects
// target's (pre-exposed) units.
//
// Statistics (emStats, words): 0 pixels with depth, 1 visible emissive hits, 2 effects-
// target pixels brighter than thr with no visible hit, 3 hits where the game shows less
// than 2% of ours, 6 ran, 7 effects-target pixels brighter than thr; from 16, per record:
// ours x 16, game x 16 (luminance, clamped at 4096), pixels, -.
#include "em_common.hlsli"

struct CameraCB {
  float4 v0, v1, v2;
  float4 m6_r0, m6_r1, m6_r2, m6_r3;
  float4 ray0, ray1, ray2;   // frustum rays at the viewport's (0,0), (1,0), (0,1) in ab space
};
struct ViewportCB { float2 scale; float2 bias; };
[[vk::binding(0, 0)]] ConstantBuffer<CameraCB> cam;
[[vk::binding(1, 0)]] ConstantBuffer<ViewportCB> vp;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;   // R32F linear depth
[[vk::binding(3, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gDiffuse;
[[vk::binding(4, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gSpecular;
[[vk::binding(5, 0)]] Texture2D<float4> gFx;     // the game's effects target

struct Push { uint2 full; uint mode; float tol; float thr; float pad0, pad1, pad2; };
[[vk::push_constant]] Push pc;

float3 ray_at(float2 uv) {
  float2 ab = ((uv - vp.bias) / vp.scale) * 0.5;
  return cam.ray0.xyz * (1.0 - ab.x - ab.y) + cam.ray1.xyz * ab.x + cam.ray2.xyz * ab.y;
}

void stat(uint word) { emStats.InterlockedAdd(word * 4, 1u); }
uint q16(float v) { return uint(clamp(v, 0.0, 4096.0) * 16.0 + 0.5); }

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (all(fp == 0)) emStats.Store(6 * 4, 1u);
  if (any(fp >= pc.full)) return;
  const float d = gDepth.Load(int3(fp, 0));
  const float3 fx = max(gFx.Load(int3(fp, 0)).rgb, 0.0);
  float3 c = float3(0, 0, 0);
  if (d > 0.0) {
    stat(0);
    const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
    const float3 r = ray_at(uv);
    const float3 P = r * d;
    const float dist = length(P);
    const float3 dir = P / max(dist, 1e-20);
    const float px = length(ray_at(uv + float2(1.0 / float(pc.full.x), 0.0)) - r) / max(length(r), 1e-20) * dist;
    float3 L;
    // The emissive surface at the pixel's own depth (the main TLAS is not needed for a
    // primary ray: the depth buffer says where it stops).
    const float eps = dist * pc.tol + 0.01;
    const bool hit = em_lookup(float3(0, 0, 0), dir, dist, eps, px, L);
    const float ours = em_lum(L), game = em_lum(fx);
    const bool bright = game > pc.thr;
    if (bright) stat(7);
    if (hit) {
      stat(1);
      if (game < 0.02 * ours) stat(3);
      // The record of the hit, for the per-family comparison.
      RayDesc ray;
      ray.Origin = float3(0, 0, 0);
      ray.Direction = dir;
      ray.TMin = max(dist - eps, 0.0);
      ray.TMax = dist + eps;
      RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
      q.TraceRayInline(emTlas, RAY_FLAG_NONE, 0xFF, ray);
      q.Proceed();
      if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        uint rec = emInfo.Load(q.CommittedInstanceIndex() * 16 + 8);
        if (rec < 1024) {
          uint base = (16 + rec * 4) * 4;
          emStats.InterlockedAdd(base, q16(ours));
          emStats.InterlockedAdd(base + 4, q16(game));
          emStats.InterlockedAdd(base + 8, 1u);
        }
      }
    } else if (bright) {
      stat(2);
    }
    if (pc.mode == 6) {
      c = hit ? L : float3(0, 0, 0);
    } else if (pc.mode == 8) {
      if (hit && bright) {
        float k = ours / max(game, 1e-6);
        c = k > 1.25 ? float3(100, 0, 0) : k < 0.8 ? float3(0, 0, 100) : float3(0, 100, 0);
      } else if (hit) {
        c = float3(100, 100, 0);   // ours only (yellow)
      } else if (bright) {
        c = float3(100, 0, 100);   // the game's only (magenta)
      }
    }
  }
  gDiffuse[fp] = c;
  gSpecular[fp] = float3(0, 0, 0);
}
