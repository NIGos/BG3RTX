// CPU reference for the temporal post shader (no graphics dependencies).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace bg3::post_temporal {
using Value = std::array<double, 7>; // C.rgb, A, bloom.rgb (exposure deliberately excluded)
inline Value accumulate(const Value& current, const Value& previous,
                        const Value& lo, const Value& hi, bool valid) {
  for (double v : previous) valid = valid && std::isfinite(v);
  if (!valid) return current;
  Value result{};
  for (unsigned i = 0; i < result.size(); ++i)
    result[i] = std::clamp(previous[i], lo[i], hi[i]) * (15.0 / 16.0) + current[i] / 16.0;
  return result;
}
inline bool accepts(double old_depth, double lo_depth, double hi_depth,
                    const std::array<double, 2>& mv, const std::array<double, 2>& old_mv) {
  constexpr double depth_rounding = 2.0 / 16777215.0;
  if (!std::isfinite(old_depth) || old_depth < lo_depth-depth_rounding || old_depth > hi_depth+depth_rounding)
    return false;
  for (unsigned i = 0; i < 2; ++i)
    if (!std::isfinite(mv[i]) || !std::isfinite(old_mv[i]) ||
        std::abs(mv[i]-old_mv[i]) > 1.0+(std::abs(mv[i])+std::abs(old_mv[i]))/1024.0) return false;
  return true;
}
}
