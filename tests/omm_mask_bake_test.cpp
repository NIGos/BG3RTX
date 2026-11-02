// common/omm_mask_bake.h (rt_omm_dyn, docs/P4_OMM.md 11.2c): micro-triangle order, texel support, states, and the
// bake against dense bilinear sampling of the alpha test. The order itself is checked against the NVIDIA SDK's
// baker by tools/omm_mask_bake_sdk_probe.cpp (BG3RTX_OMM_CPU_PROBE); the level-2 table below is what that probe
// verified (docs/evidence/omm-mask-bake-sdk-20261102.txt).
#include "../common/omm_mask_bake.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace bg3::omm_mask;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// The alpha test of a masked record (rt_common.hlsli alpha_eval + tex_alpha at mip 0): bilinear, REPEAT.
static double sample(const OmbMask& m, double u, double v) {
  const double x = u * m.w - 0.5, y = v * m.h - 0.5;
  const double fx = std::floor(x), fy = std::floor(y);
  const double ax = x - fx, ay = y - fy;
  auto at = [&](long long i, long long j) {
    i %= (long long)m.w; j %= (long long)m.h;
    if (i < 0) i += m.w;
    if (j < 0) j += m.h;
    return double(m.a[size_t(j) * m.w + size_t(i)]);
  };
  const long long i = (long long)fx, j = (long long)fy;
  return (at(i, j) * (1 - ax) + at(i + 1, j) * ax) * (1 - ay) + (at(i, j + 1) * (1 - ax) + at(i + 1, j + 1) * ax) * ay;
}

static void order_tests() {
  for (uint32_t level = 0; level <= 6; ++level) {
    const uint32_t n = 1u << (2 * level), steps = 1u << level;
    std::vector<int> seen(size_t(steps) * steps * 2, 0);
    bool ok = true;
    for (uint32_t i = 0; i < n; ++i) {
      uint32_t iu, iv; bool up;
      omb_micro_cell(i, level, iu, iv, up);
      const uint32_t cu = up ? iu : iu - 1, cv = up ? iv : iv - 1;
      const bool inside = cu < steps && cv < steps && (up ? cu + cv + 1 <= steps : cu + cv + 2 <= steps);
      if (!inside) { ok = false; continue; }
      ok = ok && !seen[(size_t(cv) * steps + cu) * 2 + (up ? 0 : 1)]++;
    }
    CHECK(ok);   // a bijection onto the 4^level cells of the triangle
  }
  // Level 2 (16 micro-triangles), as verified against the SDK: iu, iv, upright.
  static const uint32_t kLevel2[16][3] = {
      {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}, {1, 2, 0}, {1, 1, 1}, {2, 2, 0}, {2, 1, 0},
      {2, 0, 1}, {3, 1, 0}, {3, 0, 1}, {2, 1, 1}, {1, 2, 1}, {1, 3, 0}, {0, 2, 1}, {0, 3, 1}};
  bool table_ok = true;
  for (uint32_t i = 0; i < 16; ++i) {
    uint32_t iu, iv; bool up;
    omb_micro_cell(i, 2, iu, iv, up);
    if (iu != kLevel2[i][0] || iv != kLevel2[i][1] || uint32_t(up) != kLevel2[i][2]) {
      table_ok = false;
      std::printf("  level 2 index %u: (%u, %u, %u)\n", i, iu, iv, unsigned(up));
    }
  }
  CHECK(table_ok);
}

static void unit_tests() {
  // States: >= cut + 1/255 opaque; 0 or < cut - 1/255 transparent; otherwise unknown.
  CHECK(omb_state(1.0f, 1.0f, 0.5f, 0.5f) == 1u);
  CHECK(omb_state(0.5f + 1.0f / 255.0f, 1.0f, 0.5f, 0.5f) == 1u);
  CHECK(omb_state(0.5f, 1.0f, 0.5f, 0.5f) == 3u);           // equality: the margin keeps it unknown
  CHECK(omb_state(0.0f, 0.0f, 0.5f, 0.5f) == 0u);
  CHECK(omb_state(0.0f, 0.0f, 0.0f, 0.0f) == 3u);           // cut 0: everything passes >= 0, never transparent
  CHECK(omb_state(0.0f, 0.497f, 0.5f, 0.5f) == 3u);
  CHECK(omb_state(0.0f, 0.4f, 0.5f, 0.5f) == 0u);
  CHECK(omb_state(0.9f, 1.0f, 0.2f, 0.95f) == 3u);          // both cuts: opaque needs the higher one
  CHECK(omb_state(0.0f, 0.3f, 0.2f, 0.95f) == 3u);          // transparent needs the lower one
  // Texel support of a bilinear read.
  int a, b;
  CHECK(omb_axis(0.5f, 0.5f, 8, 0.0f, a, b) && a == 3 && b == 4);
  CHECK(omb_axis(0.0f, 0.0f, 8, kOmbTexelMargin, a, b) && a == -1 && b == 0);
  CHECK(!omb_axis(std::nanf(""), 0.5f, 8, 0.0f, a, b));
  CHECK(!omb_axis(0.0f, 1e9f, 8, 0.0f, a, b));
  CHECK(omb_wrap(-1, 8) == 7 && omb_wrap(8, 8) == 0 && omb_wrap(17, 8) == 1);
  // Non-finite uv or an empty mask: every micro-triangle unknown.
  std::vector<float> one(16, 1.0f);
  OmbMask m{4, 4, one.data()};
  const float bad[6] = {0, 0, std::nanf(""), 0, 0, 1};
  uint32_t words[16];
  omb_bake_triangle(m, bad, kOmbLevel, 0.5f, 0.5f, words);
  bool all_unknown = true;
  for (uint32_t w : words) all_unknown = all_unknown && w == 0xFFFFFFFFu;
  CHECK(all_unknown);
  // A solid mask: every micro-triangle known opaque (packing: 01 per state).
  const float good[6] = {0.1f, 0.1f, 0.4f, 0.1f, 0.1f, 0.4f};
  omb_bake_triangle(m, good, kOmbLevel, 0.5f, 0.5f, words);
  bool all_opaque = true;
  for (uint32_t w : words) all_opaque = all_opaque && w == 0x55555555u;
  CHECK(all_opaque);
}

// Every known state against the alpha test at many points of its micro-triangle (corners, edges, interior).
static void bound_tests() {
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  uint64_t known = 0, total = 0, samples = 0, violations = 0;
  for (int t = 0; t < 6; ++t) {
    const uint32_t w = t % 2 ? 64 : 128, h = t % 3 ? 128 : 64;
    std::vector<float> a(size_t(w) * h, 0.0f);
    for (int k = 0; k < 12; ++k) {   // discs with soft rims, 8-bit codes
      const float cx = U(rng) * w, cy = U(rng) * h, r = 3 + U(rng) * 12;
      for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
          const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
          const float v = d < r ? 1.0f : d < r + 2 ? std::round((r + 2 - d) * 0.5f * 255.0f) / 255.0f : 0.0f;
          a[size_t(y) * w + x] = std::max(a[size_t(y) * w + x], v);
        }
    }
    OmbMask m{w, h, a.data()};
    for (int tri = 0; tri < 60; ++tri) {
      // Triangles inside and across the wrap, up to a third of the mask.
      const float ox = U(rng) * 4 - 2, oy = U(rng) * 4 - 2, s = 0.02f + U(rng) * 0.3f;
      const float uv[6] = {ox, oy, ox + s * (U(rng) - 0.3f), oy + s * U(rng), ox + s * U(rng), oy + s * (U(rng) - 0.3f)};
      const float cut_lo = 0.2f + 0.3f * U(rng), cut_hi = tri % 4 ? cut_lo : std::min(1.0f, cut_lo + 0.2f);
      uint32_t words[16];
      omb_bake_triangle(m, uv, kOmbLevel, cut_lo, cut_hi, words);
      for (uint32_t i = 0; i < 256; ++i) {
        const uint32_t st = (words[i / 16] >> (2 * (i % 16))) & 3u;
        ++total;
        if (st > 1) continue;
        ++known;
        uint32_t iu, iv; bool up;
        omb_micro_cell(i, kOmbLevel, iu, iv, up);
        const double q = 1.0 / 16, d = up ? q : -q, b1 = iu * q, b2 = iv * q;
        for (int k = 0; k < 40; ++k) {
          // Barycentrics in the micro-triangle (k < 3 corners, then edges and interior).
          double r1 = U(rng), r2 = U(rng);
          if (r1 + r2 > 1) { r1 = 1 - r1; r2 = 1 - r2; }
          if (k == 0) r1 = r2 = 0;
          if (k == 1) { r1 = 1; r2 = 0; }
          if (k == 2) { r1 = 0; r2 = 1; }
          if (k >= 3 && k < 10) r2 = 1 - r1;
          const double p1 = b1 + d * r1, p2 = b2 + d * r2;
          const float fu = uv[0] + (uv[2] - uv[0]) * float(p1) + (uv[4] - uv[0]) * float(p2);
          const float fv = uv[1] + (uv[3] - uv[1]) * float(p1) + (uv[5] - uv[1]) * float(p2);
          const double alpha = sample(m, fu, fv);
          ++samples;
          // Both cuts: the record's effective and first cut-off.
          const bool bad = st == 1 ? (alpha < cut_hi || alpha < cut_lo) : (alpha >= cut_lo || alpha >= cut_hi);
          violations += bad;
        }
      }
    }
  }
  std::printf("bound: %llu of %llu micro-triangles known (%.1f%%), %llu samples, %llu violations\n",
              (unsigned long long)known, (unsigned long long)total, 100.0 * double(known) / double(total),
              (unsigned long long)samples, (unsigned long long)violations);
  CHECK(violations == 0);
  CHECK(known * 5 >= total);   // the discs leave most micro-triangles uniform
}

int main() {
  order_tests();
  unit_tests();
  bound_tests();
  std::printf(g_fail ? "omm_mask_bake_test: %d failures\n" : "omm_mask_bake_test: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
