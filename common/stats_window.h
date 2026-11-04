// Live-test statistics controls. Zero preserves each producer's native cadence.
#pragma once
#include <cstdint>
namespace bg3::stats_window {
template<class Config> inline unsigned period(Config cfg, unsigned native) {
  const int n = cfg("rt_stats_period_frames", 0);
  return n > 0 ? unsigned(n) : native;
}
template<class Config> inline bool reset(Config cfg, int& seen) {
  const int token = cfg("rt_stats_reset", 0);
  if (cfg("rt_stats_period_frames", 0) <= 0 || token == seen) return false;
  seen = token;
  return token != 0;
}
}  // namespace bg3::stats_window
