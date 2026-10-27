// D-EM debug views, rt_debug 6-8 (core/emissive.cpp). One primary ray per pixel against
// the emissive TLAS (rt/em_common.hlsli): a hit within rt_em_tol of the pixel's depth is
// a visible emissive surface, and its radiance is evaluated from the draw's record. The
// game's effects target at the same pixel is the reference: both are in the effects
// target's (pre-exposed) units. The scene is the previous submitted frame's, moved into
// this frame's space by em_shift (exact for static emitters).
//
// Statistics (emStats, words): 0 pixels with depth, 1 visible emissive hits, 2 effects-
// target pixels brighter than thr with no visible hit, 3 hits where the game shows less
// than 2% of ours, 4 of the bright pixels without a hit at the depth, those with an emissive
// surface in front of it (alpha-blended emitters: not in the depth buffer), 5 such front hits
// in all, 6 ran, 7 effects-target pixels brighter than thr; from 16, 8 words per record:
// ours and game luminance x 4096 as 64-bit sums (lo, hi; dim emitters, e.g. a lava pool at
// 0.01, are kept), pixels.
#include "em_common.hlsli"

struct CameraCB {
  float4 v0, v1, v2;
  float4 m6_r0, m6_r1, m6_r2, m6_r3;
  float4 ray0, ray1, ray2;   // frustum rays at the viewport's (0,0), (1,0), (0,1) in ab space
};
struct ViewportCB { float2 scale; float2 bias; };
struct Camera2 { float4 v[14]; };   // the lighting's set 0 b2: v[13].xyz = absolute camera position (_m6, byte 208)
[[vk::binding(0, 0)]] ConstantBuffer<CameraCB> cam;
[[vk::binding(1, 0)]] ConstantBuffer<ViewportCB> vp;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;   // R32F linear depth
[[vk::binding(3, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gDiffuse;
[[vk::binding(4, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gSpecular;
[[vk::binding(5, 0)]] Texture2D<float4> gFx;     // the game's effects target
[[vk::binding(6, 0)]] ConstantBuffer<Camera2> cam2;

struct Push { uint2 full; uint mode; float tol; float thr; uint has_cam2; float pad1, pad2; };
[[vk::push_constant]] Push pc;

float3 ray_at(float2 uv) {
  float2 ab = ((uv - vp.bias) / vp.scale) * 0.5;
  return cam.ray0.xyz * (1.0 - ab.x - ab.y) + cam.ray1.xyz * ab.x + cam.ray2.xyz * ab.y;
}

void stat(uint word) { emStats.InterlockedAdd(word * 4, 1u); }
// 64-bit sum of luminance x 4096 at byte `a` (lo, hi): the carry from the returned old value.
void add64(uint a, float lum) {
  uint v = uint(clamp(lum, 0.0, 1e5) * 4096.0 + 0.5);
  uint old;
  emStats.InterlockedAdd(a, v, old);
  if (old + v < old) emStats.InterlockedAdd(a + 4, 1u);
}
void rec_stat(uint rec, float ours, float game) {
  if (rec >= 1024) return;
  uint base = (16 + rec * 8) * 4;
  add64(base, ours);
  add64(base + 8, game);
  emStats.InterlockedAdd(base + 16, 1u);
}

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
    const float3 shift = pc.has_cam2 != 0 ? em_shift(cam2.v[13].xyz) : float3(0, 0, 0);
    float3 L;
    uint rec;
    // The emissive surface at the pixel's own depth (the depth buffer says where the
    // primary ray stops; the main TLAS is not needed).
    const float eps = dist * pc.tol + 0.01;
    const bool hit = em_lookup(float3(0, 0, 0), dir, dist, eps, px, shift, L, rec);
    const float ours = em_lum(L), game = em_lum(fx);
    bool bright = game > pc.thr;
    if (bright) stat(7);
    if (!hit) {
      // An emitter in front of the depth: an alpha-blended glow card or halo is drawn into
      // the effects target but not into the depth buffer. Counted apart (not a miss).
      float3 Lf;
      uint recf;
      if (em_lookup(float3(0, 0, 0), dir, 0.5 * max(dist - eps, 0.0), 0.5 * max(dist - eps, 0.0), px, shift, Lf, recf)) {
        stat(5);
        if (bright) {
          stat(4);
          rec_stat(recf, em_lum(Lf), game);
        }
        if (pc.mode == 6) c = Lf;
        bright = false;   // covered, by the emitter in front
      }
    }
    if (hit) {
      stat(1);
      if (game < 0.02 * ours) stat(3);
      rec_stat(rec, ours, game);
    } else if (bright) {
      stat(2);
    }
    if (pc.mode == 6) {
      c = hit ? L : c;
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
  if (pc.mode == 0) return;   // statistics only (the calibration's check)
  gDiffuse[fp] = c;
  gSpecular[fp] = float3(0, 0, 0);
}
