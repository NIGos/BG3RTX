#pragma once
// Four-state opacity micromap bake from an immutable offline mask at mip 0 (rt_omm_dyn, docs/P4_OMM.md 11.2c).
// Compiled by BOTH the CPU tests (tests/omm_mask_bake_test.cpp, tools/omm_mask_bake_sdk_probe.cpp) and DXC
// (shaders/rt/omm_bake_mask.hlsl): the shader runs exactly this classification.
//
// The RT alpha test of a masked record with rt_alpha_fine_lod=1 (layer/alphatest.cpp vt_mask_substitute) is
//   uv = u0 + (u1 - u0) b1 + (u2 - u0) b2, a = bilinear(mask mip 0, REPEAT, uv * scale), solid = a >= cut
// for every ray (the view has one level, so k = 0: no stochastic threshold). A micro-triangle is
//   known opaque      if every texel a bilinear read anywhere inside it can touch is >= max(cuts) + 1/255,
//   known transparent if every such texel is 0, or < min(cuts) - 1/255 (min(cuts) > 0),
//   unknown opaque    otherwise (the any-hit decides, so unknown is always exact).
// Both cuts: the record's effective cut and its first cut (rt_alpha_cut switches between them live).
// Margins, all derived: 1/255 in alpha is the BC4 endpoint precision and bounds the filter's weight rounding
// (8 sub-texel bits: |error| <= (max - min) / 512); 1/256 texel is the sub-texel quantisation of the sample
// position (a texel with weight 0 may be fetched); the uv slack bounds float32 interpolation (32 ulp of the
// magnitudes) and a barycentric disagreement of 1e-5 between the hardware's micro-triangle lookup and the
// reported hit barycentrics. The CPU tests check the bound against dense bilinear sampling and the micro-triangle
// order against the NVIDIA OMM SDK's baker (EXT layout).
#ifdef __cplusplus
#include <cmath>
#include <cstdint>
#include <cstring>
namespace bg3::omm_mask {
#define OMB_INLINE inline
#define OMB_UINT uint32_t
#define OMB_OUT(T) T&
inline float omb_asfloat(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
inline float omb_floor(float x) { return std::floor(x); }
inline float omb_abs(float x) { return std::fabs(x); }
inline float omb_min(float a, float b) { return a < b ? a : b; }
inline float omb_max(float a, float b) { return a > b ? a : b; }
inline bool omb_finite(float x) { return std::isfinite(x); }
// The CPU fetch: wrapped texel (x, y) of the mask (row-major, values in [0, 1]).
struct OmbMask { uint32_t w = 0, h = 0; const float* a = nullptr; };
inline thread_local const OmbMask* g_omb_mask = nullptr;
inline float omb_fetch(int x, int y) { return g_omb_mask->a[size_t(y) * g_omb_mask->w + size_t(x)]; }
#else
#define OMB_INLINE
#define OMB_UINT uint
#define OMB_OUT(T) out T
#define omb_asfloat asfloat
#define omb_floor floor
#define omb_abs abs
#define omb_min min
#define omb_max max
#define omb_finite isfinite
// The shader defines float omb_fetch(int x, int y) (Load of the mask view, wrapped coordinates) before this.
#endif

static const OMB_UINT kOmbLevel = 4u;            // micro-triangles per triangle: 4^4 = 256, 64 bytes of 4-state data
static const OMB_UINT kOmbMaxTexels = 1024u;     // texels a micro-triangle may touch; more: unknown (bounds the bake's loop)
static const OMB_UINT kOmbUnknownIndex = 0xFFFFFFFCu;   // VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_UNKNOWN_OPAQUE_EXT
static const float kOmbAlphaMargin = 1.0f / 255.0f;
static const float kOmbTexelMargin = 1.0f / 256.0f;
static const float kOmbBaryMargin = 1e-5f;
static const float kOmbUlp = 1.1920928955078125e-7f;   // 2^-23

// Micro-triangle index -> discrete barycentrics (the Vulkan spec's EXT micromap order, the "bird" curve).
OMB_INLINE OMB_UINT omb_even_bits(OMB_UINT x) {
  x &= 0x55555555u;
  x = (x | (x >> 1)) & 0x33333333u;
  x = (x | (x >> 2)) & 0x0f0f0f0fu;
  x = (x | (x >> 4)) & 0x00ff00ffu;
  x = (x | (x >> 8)) & 0x0000ffffu;
  return x;
}
OMB_INLINE OMB_UINT omb_prefix_eor(OMB_UINT x) {
  x ^= x >> 1;
  x ^= x >> 2;
  x ^= x >> 4;
  x ^= x >> 8;
  return x;
}
// Micro-triangle `index` of subdivision `level`: corner (iu, iv) in units of 2^-level of (b1, b2) and its
// orientation. Upright: corners (iu, iv), (iu + 1, iv), (iu, iv + 1); inverted: (iu, iv), (iu - 1, iv), (iu, iv - 1).
OMB_INLINE void omb_micro_cell(OMB_UINT index, OMB_UINT level, OMB_OUT(OMB_UINT) iu, OMB_OUT(OMB_UINT) iv,
                               OMB_OUT(bool) upright) {
  iu = 0u; iv = 0u; upright = true;
  if (level == 0u) return;
  const OMB_UINT b0 = omb_even_bits(index), b1 = omb_even_bits(index >> 1);
  const OMB_UINT fx = omb_prefix_eor(b0), fy = omb_prefix_eor(b0 & ~b1);
  const OMB_UINT t = fy ^ b1;
  OMB_UINT u = (fx & ~t) | (b0 & ~t) | (~b0 & ~fx & t);
  OMB_UINT v = fy ^ b0;
  OMB_UINT w = (~fx & ~t) | (b0 & ~t) | (~b0 & fx & t);
  const OMB_UINT mask = (1u << level) - 1u;
  u &= mask; v &= mask; w &= mask;
  upright = ((u & 1u) ^ (v & 1u) ^ (w & 1u)) != 0u;
  if (!upright) { u += 1u; v += 1u; }
  iu = u; iv = v;
}

// Inclusive texel range of a bilinear read anywhere in [lo, hi] (normalised, extent texels), margin in texels.
OMB_INLINE bool omb_axis(float lo, float hi, OMB_UINT extent, float margin, OMB_OUT(int) first, OMB_OUT(int) last) {
  first = 0; last = -1;
  const float e = float(extent);
  const float a = omb_floor(lo * e - 0.5f - margin), b = omb_floor(hi * e - 0.5f + margin) + 1.0f;
  if (!omb_finite(a) || !omb_finite(b) || omb_abs(a) >= 8388608.0f || omb_abs(b) >= 8388608.0f || a > b) return false;
  first = int(a); last = int(b);
  return true;
}
OMB_INLINE int omb_wrap(int x, OMB_UINT extent) {
  const int r = x % int(extent);
  return r < 0 ? r + int(extent) : r;
}
// 0 known transparent, 1 known opaque, 3 unknown opaque (the any-hit runs).
OMB_INLINE OMB_UINT omb_state(float lo, float hi, float cut_lo, float cut_hi) {
  if (lo >= cut_hi + kOmbAlphaMargin) return 1u;
  if (cut_lo > 0.0f && (hi == 0.0f || hi < cut_lo - kOmbAlphaMargin)) return 0u;
  return 3u;
}
// uv slack of one axis for a triangle with corner c0 and edges e1, e2 (already scaled): float32 interpolation
// error and the barycentric margin.
OMB_INLINE float omb_uv_slack(float c0, float e1, float e2) {
  return kOmbBaryMargin * (omb_abs(e1) + omb_abs(e2)) + 32.0f * kOmbUlp * (omb_abs(c0) + omb_abs(e1) + omb_abs(e2));
}

// The state of micro-triangle `index` (level kOmbLevel or any) of a triangle with scaled uv corners
// (x0, y0), (x1, y1), (x2, y2) in vertex order 0, 1, 2 (b1 weights vertex 1, b2 vertex 2), mask w x h.
OMB_INLINE OMB_UINT omb_micro_state(OMB_UINT index, OMB_UINT level, float x0, float y0, float x1, float y1, float x2,
                                    float y2, OMB_UINT w, OMB_UINT h, float cut_lo, float cut_hi) {
  if (!omb_finite(x0) || !omb_finite(y0) || !omb_finite(x1) || !omb_finite(y1) || !omb_finite(x2) || !omb_finite(y2) ||
      w == 0u || h == 0u || !omb_finite(cut_lo) || !omb_finite(cut_hi))
    return 3u;
  OMB_UINT iu, iv;
  bool up;
  omb_micro_cell(index, level, iu, iv, up);
  const float s = omb_asfloat((127u - level) << 23);   // 2^-level, exact
  const float d = up ? s : -s;
  const float ex1 = x1 - x0, ey1 = y1 - y0, ex2 = x2 - x0, ey2 = y2 - y0;
  // The corners, in the alpha test's own operation order (uv0 + e1 b1 + e2 b2).
  const float bu0 = float(iu) * s, bv0 = float(iv) * s;
  const float cx0 = x0 + ex1 * bu0 + ex2 * bv0, cy0 = y0 + ey1 * bu0 + ey2 * bv0;
  const float cx1 = x0 + ex1 * (bu0 + d) + ex2 * bv0, cy1 = y0 + ey1 * (bu0 + d) + ey2 * bv0;
  const float cx2 = x0 + ex1 * bu0 + ex2 * (bv0 + d), cy2 = y0 + ey1 * bu0 + ey2 * (bv0 + d);
  const float sx = omb_uv_slack(x0, ex1, ex2), sy = omb_uv_slack(y0, ey1, ey2);
  const float lox = omb_min(cx0, omb_min(cx1, cx2)) - sx, hix = omb_max(cx0, omb_max(cx1, cx2)) + sx;
  const float loy = omb_min(cy0, omb_min(cy1, cy2)) - sy, hiy = omb_max(cy0, omb_max(cy1, cy2)) + sy;
  int ax, bx, ay, by;
  if (!omb_axis(lox, hix, w, kOmbTexelMargin, ax, bx) || !omb_axis(loy, hiy, h, kOmbTexelMargin, ay, by)) return 3u;
  const OMB_UINT nx = OMB_UINT(bx - ax + 1), ny = OMB_UINT(by - ay + 1);
  if (nx > kOmbMaxTexels || ny > kOmbMaxTexels || nx * ny > kOmbMaxTexels) return 3u;
  float lo = 1.0f, hi = 0.0f;
  for (OMB_UINT j = 0u; j < ny; ++j) {
    const int ty = omb_wrap(ay + int(j), h);
    for (OMB_UINT i = 0u; i < nx; ++i) {
      const float a = omb_fetch(omb_wrap(ax + int(i), w), ty);
      if (!omb_finite(a) || a < 0.0f || a > 1.0f) return 3u;
      lo = omb_min(lo, a);
      hi = omb_max(hi, a);
    }
  }
  return omb_state(lo, hi, cut_lo, cut_hi);
}

#ifdef __cplusplus
// CPU reference of one triangle's 4-state data (EXT OC1_4_STATE: 2 bits per micro-triangle, LSB first).
inline void omb_bake_triangle(const OmbMask& m, const float uv[6], uint32_t level, float cut_lo, float cut_hi,
                              uint32_t* words) {
  const uint32_t n = 1u << (2u * level);
  for (uint32_t k = 0; k < (n + 15u) / 16u; ++k) words[k] = 0u;
  g_omb_mask = &m;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t s = omb_micro_state(i, level, uv[0], uv[1], uv[2], uv[3], uv[4], uv[5], m.w, m.h, cut_lo, cut_hi);
    words[i / 16u] |= s << (2u * (i % 16u));
  }
  g_omb_mask = nullptr;
}
}  // namespace bg3::omm_mask
#endif
