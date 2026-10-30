// Conservative OMM classification over an already proven sampling domain.
// This is a CPU-side building block, not permission to attach hardware states.
// The future baker must bound every UV/filter/mip/residency read and invalidate
// the proof when any of those inputs changes (docs/P4_OMM.md).
#pragma once
#include <cmath>

namespace bg3::omm {
enum class State { Unknown, Transparent, Opaque };
struct Interval {
  float lower = 0, upper = 0;
  bool complete = false;
};
inline bool valid(Interval x) {
  return x.complete && std::isfinite(x.lower) && std::isfinite(x.upper) && x.lower <= x.upper;
}

// Runtime alpha_eval uses >=, not >. Alpha bounds must be outward-rounded by
// the provider, including filtering/format/arithmetic error, before this call.
inline State classify(Interval alpha, Interval threshold) {
  if (!valid(alpha) || !valid(threshold)) return State::Unknown;
  if (alpha.lower >= threshold.upper) return State::Opaque;
  if (alpha.upper < threshold.lower) return State::Transparent;
  return State::Unknown;
}

// rt_common.hlsli alpha_eval: hard cut for levels <=1, then a lerp towards
// alpha_hash, whose 24-bit grid INCLUDES zero. With arbitrary supported ray
// footprints, k spans [0,1]. [0,1] is a conservative threshold hull, including
// float interpolation rounding. Never pretend that mips 0/1 bound that domain.
// Unsupported cuts are unknown rather than silently clamped to another policy.
inline Interval legacy_thresholds(float cut, bool stochastic) {
  if (!std::isfinite(cut) || cut < 0 || cut > 1) return {};
  return stochastic ? Interval{0, 1, true} : Interval{cut, cut, true};
}
} // namespace bg3::omm
