#pragma once
#include "omm_alpha_bounds.h"
#include <array>

namespace bg3::omm {
// Integer support of a declared UV rectangle, not a GPU numerical certificate.
// The caller must bound actual barycentrics/interpolation before supplying UVs.
// Unlike the whole-mip provider, this scans only wrapped neighboring texels.
struct WrappedAxis {
  uint32_t begin{}, count{}, extent{};
  uint32_t at(uint32_t i) const { return uint32_t((uint64_t(begin) + i) % extent); }
};
inline bool spatial_axis(float lo, float hi, float scale, uint32_t extent,
                         AlphaFilter filter, WrappedAxis& out) {
  out = {};
  if (!extent || !std::isfinite(lo) || !std::isfinite(hi) || lo > hi ||
      !std::isfinite(scale) || (filter != AlphaFilter::Point && filter != AlphaFilter::Linear)) return false;
  const float a = lo * scale, b = hi * scale;
  if (!std::isfinite(a) || !std::isfinite(b)) return false;
  // One outward float32 neighbor includes the endpoint multiply rounding.
  const double low = std::nextafter(std::min(a, b), -INFINITY);
  const double high = std::nextafter(std::max(a, b), INFINITY);
  const double shift = filter == AlphaFilter::Linear ? 0.5 : 0;
  // A one-texel halo covers a rounded filtering coordinate crossing an integer
  // boundary. Linear needs its second neighbor as well. This widens support;
  // no claim about the filtered alpha's numerical error follows from it.
  const double first = std::floor(low * extent - shift) - 1;
  const double last = std::floor(high * extent - shift) + (filter == AlphaFilter::Linear ? 2 : 1);
  if (!std::isfinite(first) || !std::isfinite(last)) return false;
  if (last - first + 1 >= extent) { out = {0, extent, extent}; return true; }
  // Exact integral arithmetic below. Avoid undefined casts near int64 limits.
  constexpr double limit = 4503599627370496.0; // 2^52, exact integer range in double.
  if (first < -limit || last > limit || first > last) return false;
  const int64_t start = int64_t(first);
  int64_t wrapped = start % int64_t(extent);
  if (wrapped < 0) wrapped += extent;
  out = {uint32_t(wrapped), uint32_t(last - first + 1), extent};
  return true;
}

inline AlphaCodeBounds spatial_alpha_code_bounds(std::span<const AlphaMipPlane> mips,
                                                 const AlphaCodeQuery& q) {
  // Reuse strict snapshot/shape/LOD validation without scanning source bytes.
  // A budget of zero deliberately stops the global provider before any read.
  AlphaCodeQuery validation = q;
  validation.max_texels = 0;
  AlphaCodeBounds out = alpha_code_bounds(mips, validation);
  if (out.status != AlphaBoundsStatus::BudgetExceeded) return out;
  std::array<WrappedAxis, 32> xs{}, ys{};
  if (mips.size() > xs.size()) { out.status = AlphaBoundsStatus::InvalidMip; return out; }
  uint64_t total = 0;
  for (uint32_t level = out.first_mip; level <= out.last_mip; ++level) {
    const auto& m = mips[level];
    if (!spatial_axis(q.u_min, q.u_max, q.u_scale, m.width, q.texel_filter, xs[level]) ||
        !spatial_axis(q.v_min, q.v_max, q.v_scale, m.height, q.texel_filter, ys[level])) {
      out.status = AlphaBoundsStatus::InvalidDomain; return out;
    }
    const uint64_t count = uint64_t(xs[level].count) * ys[level].count;
    if (count > q.max_texels || total > q.max_texels - count) {
      out.status = AlphaBoundsStatus::BudgetExceeded; return out;
    }
    total += count;
  }
  uint8_t lo = 255, hi = 0;
  for (uint32_t level = out.first_mip; level <= out.last_mip; ++level) {
    const auto& m = mips[level];
    for (uint32_t y = 0; y < ys[level].count; ++y)
      for (uint32_t x = 0; x < xs[level].count; ++x) {
        const uint8_t code = m.codes[size_t(ys[level].at(y)) * m.row_stride + xs[level].at(x)];
        lo = std::min(lo, code); hi = std::max(hi, code);
      }
  }
  out.status = AlphaBoundsStatus::CompleteCodeCoverage;
  out.lower = lo; out.upper = hi; out.texels_visited = total;
  return out;
}
}
