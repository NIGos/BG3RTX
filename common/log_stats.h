// Periodic developer logging. Feature switches alone must not enable statistics.
#pragma once
namespace bg3::logging {
template<class Config> inline bool stats_enabled(Config cfg) {
  return cfg("detailed_logging", 0) != 0 || cfg("dev", 1) != 0 || cfg("rt_stats", 0) != 0 ||
         cfg("ls_stats", 0) != 0 || cfg("rt_stats_period_frames", 0) > 0;
}
template<class Config> inline bool sdk_info_enabled(Config cfg) {
  return cfg("detailed_logging", 0) != 0 || cfg("dev", 1) != 0 || cfg("sl_verbose", 0) != 0;
}
}  // namespace bg3::logging
