#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>

// Offline support bound only. Nothing here enables micromaps or certifies a
// Vulkan texture sample. A future provider must independently bound UNORM
// conversion, sampler/filter arithmetic and LOD precision before passing a
// floating-point alpha interval to the OMM classifier.
namespace bg3::omm {
enum class AlphaEncoding { LinearUnorm8, Unsupported, VirtualTexture };
enum class AlphaFilter { Point, Linear, Unsupported };
enum class AlphaAddress { Repeat, Unsupported };
enum class AlphaBoundsStatus {
  CompleteCodeCoverage, Unsupported, InvalidDomain, MissingMip, InvalidMip, BudgetExceeded
};

// One decoded, selected LINEAR UNORM8 channel per texel. Compressed storage or
// sRGB RGB channels are not this format. Rows may have padding. The caller owns
// an immutable CPU snapshot throughout the call (not a live streaming texture).
struct AlphaMipPlane {
  uint32_t width = 0, height = 0;
  size_t row_stride = 0;
  std::span<const uint8_t> codes;
  bool resident = false;
};
struct AlphaCodeQuery {
  AlphaEncoding encoding = AlphaEncoding::Unsupported;
  AlphaAddress address = AlphaAddress::Repeat;
  AlphaFilter texel_filter = AlphaFilter::Linear;
  AlphaFilter mip_filter = AlphaFilter::Linear;
  uint32_t texture_levels = 0; // exact number of levels exposed by the source view
  float u_min = 0, v_min = 0, u_max = 1, v_max = 1;
  float u_scale = 1, v_scale = 1;
  float lod_min = 0, lod_max = 0;
  float sampler_min_lod = 0, sampler_max_lod = 16; // current gAlphaWrap contract
  uint64_t max_texels = 16u * 1024u * 1024u;
};
struct AlphaCodeBounds {
  uint8_t lower = 0, upper = 255;
  AlphaBoundsStatus status = AlphaBoundsStatus::InvalidDomain;
  uint32_t first_mip = 0, last_mip = 0;
  uint64_t texels_visited = 0;
  bool complete_code_coverage() const { return status == AlphaBoundsStatus::CompleteCodeCoverage; }
};

// Conservative global hull for each selected mip. Scanning the WHOLE mip
// intentionally ignores the supplied UV rectangle after validating it: every
// texel possibly touched by wrap, negative scale, point or bilinear support is
// included. Fractional LOD includes both neighboring mips; nearest mip filtering
// uses the same (possibly wider) set. Cost is O(levels + selected texels), bounded
// by max_texels before the first data access. No texture-sized allocation.
//
// CompleteCodeCoverage is an INTEGER support statement for the declared ideal
// LOD domain. It is NOT a complete floating-point/sampler certificate, does not
// prove the caller's LOD/UV domain contains every runtime ray, and must not be
// converted directly into hardware known-transparent/known-opaque states.
inline AlphaCodeBounds alpha_code_bounds(std::span<const AlphaMipPlane> mips, const AlphaCodeQuery& q) {
  AlphaCodeBounds out;
  auto fail = [&](AlphaBoundsStatus s) { out.status = s; return out; };
  const auto known_filter = [](AlphaFilter f) { return f == AlphaFilter::Point || f == AlphaFilter::Linear; };
  if (q.encoding != AlphaEncoding::LinearUnorm8 || q.address != AlphaAddress::Repeat ||
      !known_filter(q.texel_filter) || !known_filter(q.mip_filter))
    return fail(AlphaBoundsStatus::Unsupported);
  for (float x : {q.u_min, q.v_min, q.u_max, q.v_max, q.u_scale, q.v_scale,
                  q.lod_min, q.lod_max, q.sampler_min_lod, q.sampler_max_lod})
    if (!std::isfinite(x)) return fail(AlphaBoundsStatus::InvalidDomain);
  if (q.u_min > q.u_max || q.v_min > q.v_max || q.lod_min > q.lod_max ||
      q.sampler_min_lod < 0 || q.sampler_min_lod > q.sampler_max_lod)
    return fail(AlphaBoundsStatus::InvalidDomain);
  for (float x : {q.u_min * q.u_scale, q.u_max * q.u_scale,
                  q.v_min * q.v_scale, q.v_max * q.v_scale})
    if (!std::isfinite(x)) return fail(AlphaBoundsStatus::InvalidDomain);
  if (!q.texture_levels || mips.size() != q.texture_levels)
    return fail(AlphaBoundsStatus::MissingMip);
  uint32_t maximum_levels = 0;
  for (uint32_t n = std::max(mips[0].width, mips[0].height); n; n >>= 1) ++maximum_levels;
  if (!maximum_levels || mips.size() > maximum_levels) return fail(AlphaBoundsStatus::InvalidMip);
  // Strict snapshot rule: ALL declared levels must be present and correctly
  // shaped, even when a narrower query currently does not reach one of them.
  for (size_t i = 0; i < mips.size(); ++i) {
    const auto& m = mips[i];
    if (!m.resident || m.codes.empty()) return fail(AlphaBoundsStatus::MissingMip);
    if (!m.width || !m.height || m.row_stride < m.width) return fail(AlphaBoundsStatus::InvalidMip);
    if (i && (m.width != std::max(1u, mips[i-1].width >> 1) ||
              m.height != std::max(1u, mips[i-1].height >> 1)))
      return fail(AlphaBoundsStatus::InvalidMip);
    const size_t limit = std::numeric_limits<size_t>::max();
    if (size_t(m.height - 1) > (limit - size_t(m.width)) / m.row_stride)
      return fail(AlphaBoundsStatus::InvalidMip);
    if (m.codes.size() < size_t(m.height - 1) * m.row_stride + m.width)
      return fail(AlphaBoundsStatus::MissingMip);
  }
  const float last = float(mips.size() - 1);
  auto clamp_lod = [&](float x) {
    return std::clamp(std::clamp(x, q.sampler_min_lod, q.sampler_max_lod), 0.0f, last);
  };
  out.first_mip = uint32_t(std::floor(clamp_lod(q.lod_min)));
  out.last_mip = uint32_t(std::ceil(clamp_lod(q.lod_max)));
  uint64_t total = 0;
  for (uint32_t i = out.first_mip; i <= out.last_mip; ++i) {
    const uint64_t n = uint64_t(mips[i].width) * mips[i].height;
    if (n > q.max_texels || total > q.max_texels - n)
      return fail(AlphaBoundsStatus::BudgetExceeded);
    total += n;
  }
  uint8_t lo = 255, hi = 0;
  for (uint32_t i = out.first_mip; i <= out.last_mip; ++i) {
    const auto& m = mips[i];
    for (uint32_t y = 0; y < m.height; ++y)
      for (uint32_t x = 0; x < m.width; ++x) {
        const uint8_t c = m.codes[size_t(y) * m.row_stride + x];
        lo = std::min(lo, c); hi = std::max(hi, c);
      }
  }
  out.lower = lo; out.upper = hi; out.texels_visited = total;
  out.status = AlphaBoundsStatus::CompleteCodeCoverage;
  return out;
}
} // namespace bg3::omm
