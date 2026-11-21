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
// rr_post_jitter_align (layer/rrpost.cpp rrpost_final, without the temporal pass): the sign of the jitter shift
// applied to the render-resolution A/C/B lookup at output resolution. 1 = temporal_final's convention
// (uv + jitter / render size), 2 = the opposite sign (a sign check), anything else 0 (the plain `final`).
inline double jitter_align_sign(int key) { return key == 1 ? 1.0 : key == 2 ? -1.0 : 0.0; }
// The A/C/B lookup position in render pixels for output position x (render-pixel units), as the shader does it.
// rr_post_jitter_align=3 (layer/rrpost.cpp, the temporal path only): the jitter terms of rr_post_temporal (history
// reprojection by previous - current jitter, output lookup at uv + jitter) are both 0; without the temporal pass 3 is
// the plain final like 0 (jitter_align_sign(3) == 0).
inline bool temporal_shift_zero(int key) { return key == 3; }
// The temporal pass's history offset and the output lookup offset (render pixels) for a key.
template <class T> T history_offset(T previous_jitter, T jitter, int key) {
  return temporal_shift_zero(key) ? T(0) : previous_jitter - jitter;
}
template <class T> T output_offset(T jitter, int key) { return temporal_shift_zero(key) ? T(0) : jitter; }
inline double align_lookup(double x, double jitter, int key) { return x + jitter_align_sign(key) * jitter; }
}
