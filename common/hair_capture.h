#pragma once
// Runtime hair capture (docs/RTX_HAIR_ROADMAP.md 2c): the CPU side the layer and the converter share. Header-only and
// platform-free so tests can run it: the full-mask rule, the card stretch statistic, the soup -> vertex rebuild and the
// .hcap v2 encoder / decoder. The layer (layer/hair_capture.cpp) fills a Capture from its GPU readbacks and hands it to
// write_hcap on a worker thread; tools/hairconv reads the file (BC3 mask decoded there).
//
// .hcap v2 (v1 is tools/hair_groom/hcap.py's dev layout: the same fields with an RGBA8 mask and no stretch):
//   'HCAP', 2, index count N, vertex count V, mask width W, mask height H, flags, grid columns, grid rows,
//   material hash u64, head centre f32[3], head radii f32[3],
//   mask format u32 (0 RGBA8, 1 BC3 raw blocks), mask bytes u32, stretch min / median / max f32, captures u32
//   indices u32[N] (rebased to the lowest index), positions f32[V*3], uv f32[V*2], mask bytes
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bg3::hair_capture {

constexpr uint32_t kVersion = 2, kMaskRgba8 = 0, kMaskBc3 = 1;
constexpr uint32_t kFlagHead = 1, kFlagGrid = 2;
constexpr size_t kHeaderBytes = 4 + 8 * 4 + 8 + 6 * 4 + 4 * 2 + 4 * 3 + 4;
// Smallest mask accepted at all: the game's streaming placeholders are 128 x 128 with one mip (hcap1-dialogue,
// 2026-11-08); the full CHAR_Hair masks seen were 2048 x 2048 with 12 mips.
constexpr uint32_t kMinMaskSize = 256;

struct MaskInfo { uint32_t width = 0, height = 0, mips = 0, format = 0; };

// The full-mask rule: more than one mip, at least kMinMaskSize, and at least the largest size seen before for this
// style (expected_* 0 = not seen yet). A placeholder never feeds the converter.
inline bool full_mask(const MaskInfo& m, uint32_t expected_w, uint32_t expected_h) {
  return m.mips > 1 && m.width >= kMinMaskSize && m.height >= kMinMaskSize && m.width >= expected_w && m.height >= expected_h;
}
// A later capture replaces an earlier one when its mask is larger (a placeholder was upgraded) and full.
inline bool upgrade(const MaskInfo& captured, const MaskInfo& now) {
  return full_mask(now, captured.width, captured.height) && (now.width > captured.width || now.height > captured.height);
}

// Card stretch: per triangle edge, the posed length over the first-seen length (min, median, max). Edges with a
// first-seen length below 1e-6 m are skipped. corner positions are in index order (3 per triangle).
struct Stretch { float min = 1, median = 1, max = 1; };
inline Stretch stretch(const float* first, const float* now, size_t corners) {
  std::vector<float> r;
  r.reserve(corners);
  for (size_t t = 0; t + 2 < corners; t += 3)
    for (int e = 0; e < 3; ++e) {
      const size_t a = t + e, b = t + (e + 1) % 3;
      double l0 = 0, l1 = 0;
      for (int k = 0; k < 3; ++k) {
        const double d0 = double(first[b * 3 + k]) - first[a * 3 + k], d1 = double(now[b * 3 + k]) - now[a * 3 + k];
        l0 += d0 * d0; l1 += d1 * d1;
      }
      if (l0 > 1e-12) r.push_back(float(std::sqrt(l1 / l0)));
    }
  Stretch s;
  if (r.empty()) return s;
  std::sort(r.begin(), r.end());
  s.min = r.front(); s.max = r.back(); s.median = r[r.size() / 2];
  return s;
}

struct Capture {
  std::vector<uint32_t> indices;     // the draw's index range (native numbering, any base)
  std::vector<float> corner_pos;     // per corner in index order: post-skin world position (3 floats)
  std::vector<float> corner_uv;      // per corner: UV0 (2 floats)
  MaskInfo mask;
  std::vector<uint8_t> mask_bytes;   // mip 0: RGBA8 rows or BC3 blocks
  uint32_t mask_format = kMaskBc3;
  uint64_t material = 0;
  Stretch stretch;
  uint32_t captures = 1;             // how many times this style was captured (re-captures on upgrade)
};

namespace detail {
template <class T> void put(std::vector<uint8_t>& b, T v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + sizeof v); }
template <class T> T get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
}  // namespace detail

inline size_t bc3_bytes(uint32_t w, uint32_t h) { return size_t((w + 3) / 4) * ((h + 3) / 4) * 16; }

// .hcap v2 bytes of a capture: vertices rebuilt from the corners (vertex v takes its first corner's values; the vertex
// shader is deterministic, so all its corners agree). Empty on inconsistent input.
inline std::vector<uint8_t> encode_hcap(const Capture& c) {
  using detail::put;
  const size_t n = c.indices.size();
  if (!n || n % 3 || c.corner_pos.size() != n * 3 || c.corner_uv.size() != n * 2) return {};
  const size_t want = c.mask_format == kMaskBc3 ? bc3_bytes(c.mask.width, c.mask.height) : size_t(c.mask.width) * c.mask.height * 4;
  if (c.mask_bytes.size() != want || !want) return {};
  const uint32_t lo = *std::min_element(c.indices.begin(), c.indices.end());
  const uint32_t hi = *std::max_element(c.indices.begin(), c.indices.end());
  const size_t V = size_t(hi - lo) + 1;
  std::vector<float> pos(V * 3, 0.0f), uv(V * 2, 0.0f);
  std::vector<uint8_t> seen(V, 0);
  for (size_t i = 0; i < n; ++i) {
    const size_t v = c.indices[i] - lo;
    if (seen[v]) continue;
    seen[v] = 1;
    std::memcpy(&pos[v * 3], &c.corner_pos[i * 3], 12);
    std::memcpy(&uv[v * 2], &c.corner_uv[i * 2], 8);
  }
  std::vector<uint8_t> b;
  b.reserve(kHeaderBytes + n * 4 + V * 20 + c.mask_bytes.size());
  b.insert(b.end(), {'H', 'C', 'A', 'P'});
  put<uint32_t>(b, kVersion); put<uint32_t>(b, uint32_t(n)); put<uint32_t>(b, uint32_t(V));
  put<uint32_t>(b, c.mask.width); put<uint32_t>(b, c.mask.height); put<uint32_t>(b, 0u); put<uint32_t>(b, 0u); put<uint32_t>(b, 0u);
  put<uint64_t>(b, c.material);
  for (int k = 0; k < 6; ++k) put<float>(b, 0.0f);
  put<uint32_t>(b, c.mask_format); put<uint32_t>(b, uint32_t(c.mask_bytes.size()));
  put<float>(b, c.stretch.min); put<float>(b, c.stretch.median); put<float>(b, c.stretch.max); put<uint32_t>(b, c.captures);
  for (uint32_t i : c.indices) put<uint32_t>(b, i - lo);
  const auto* pp = reinterpret_cast<const uint8_t*>(pos.data());
  b.insert(b.end(), pp, pp + pos.size() * 4);
  const auto* up = reinterpret_cast<const uint8_t*>(uv.data());
  b.insert(b.end(), up, up + uv.size() * 4);
  b.insert(b.end(), c.mask_bytes.begin(), c.mask_bytes.end());
  return b;
}

// BC3 (DXT5) block decode to RGBA8: BC1-style colour endpoints (always 4-colour in BC3) and a BC4 alpha block.
inline void decode_bc3(const uint8_t* blocks, uint32_t w, uint32_t h, std::vector<uint8_t>& rgba) {
  rgba.assign(size_t(w) * h * 4, 0);
  const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
  for (uint32_t by = 0; by < bh; ++by)
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint8_t* blk = blocks + (size_t(by) * bw + bx) * 16;
      uint8_t a[8];
      a[0] = blk[0]; a[1] = blk[1];
      if (a[0] > a[1]) for (int i = 1; i < 7; ++i) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
      else { for (int i = 1; i < 5; ++i) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5); a[6] = 0; a[7] = 255; }
      uint64_t abits = 0;
      for (int i = 0; i < 6; ++i) abits |= uint64_t(blk[2 + i]) << (8 * i);
      const uint16_t c0 = uint16_t(blk[8] | blk[9] << 8), c1 = uint16_t(blk[10] | blk[11] << 8);
      uint8_t col[4][3];
      auto rgb = [](uint16_t c, uint8_t o[3]) {
        o[0] = uint8_t(((c >> 11) & 31) * 255 / 31); o[1] = uint8_t(((c >> 5) & 63) * 255 / 63); o[2] = uint8_t((c & 31) * 255 / 31);
      };
      rgb(c0, col[0]); rgb(c1, col[1]);
      for (int k = 0; k < 3; ++k) {
        col[2][k] = uint8_t((2 * col[0][k] + col[1][k]) / 3);
        col[3][k] = uint8_t((col[0][k] + 2 * col[1][k]) / 3);
      }
      const uint32_t cbits = uint32_t(blk[12]) | uint32_t(blk[13]) << 8 | uint32_t(blk[14]) << 16 | uint32_t(blk[15]) << 24;
      for (int py = 0; py < 4; ++py)
        for (int px = 0; px < 4; ++px) {
          const uint32_t x = bx * 4 + px, y = by * 4 + py;
          if (x >= w || y >= h) continue;
          const int i = py * 4 + px;
          uint8_t* o = &rgba[(size_t(y) * w + x) * 4];
          const int ci = (cbits >> (2 * i)) & 3;
          o[0] = col[ci][0]; o[1] = col[ci][1]; o[2] = col[ci][2];
          o[3] = a[(abits >> (3 * i)) & 7];
        }
    }
}

}  // namespace bg3::hair_capture
