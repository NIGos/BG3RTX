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
