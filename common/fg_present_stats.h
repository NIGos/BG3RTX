#pragma once
#include <cstdint>

// A valid resource snapshot alone is insufficient: failed API publication must
// never leave DLSS-G enabled with stale tags or constants.
inline bool fg_present_inputs_usable(bool ready, bool token, bool tagged,
                                     bool tags_ok, bool constants_ok) {
  return ready && token && tagged && tags_ok && constants_ok;
}

// SDK counts are since the previous query, not necessarily this app frame:
// async presents can yield zero now and several on the next successful query.
struct FgPresentStats {
  uint64_t presented = 0, rendered = 0, failures = 0;
  void add(uint32_t count, bool success = true) {
    ++rendered;
    if (success) presented += count; else ++failures;
  }
  double ratio() const { return rendered ? double(presented)/double(rendered) : 0.0; }
};

// Present-thread CPU observations, independent of SDK success or failure.
struct FgCpuTiming {
  uint64_t calls=0, total_us=0, max_us=0;
  void add(uint64_t us) {
    ++calls; total_us+=us; if (us>max_us) max_us=us;
  }
  double mean_ms() const { return calls ? double(total_us)/double(calls)/1000.0 : 0.0; }
  double maximum_ms() const { return double(max_us)/1000.0; }
};

// Never send a multiplier unsupported by the queried adapter. Unknown support
// (zero) leaves generation off until a successful state query supplies it.
inline uint32_t fg_supported_generated(int multiplier,uint32_t maximum) {
  const uint32_t wanted=multiplier<2 ? 0u : multiplier>4 ? 3u : uint32_t(multiplier-1);
  return wanted<maximum ? wanted : maximum;
}
