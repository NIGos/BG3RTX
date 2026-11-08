#pragma once
// GPU time of the two strand-pixel passes (core/gi.cpp hair_fibre_timestamp): gi/hair_fibre.hlsl after D-LS and
// gi/hair_fibre_spec.hlsl after D-RF. Both run inside the ls / rf timers; this splits them out (AGENTS.md: every
// pass reports its cost). One ring slot holds 4 timestamps per frame (fibre begin/end, spec begin/end) and a ran
// mask; a pass contributes a sample only in the frames it was dispatched (a missing pass is never a zero sample).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bg3::hair_fibre_timing {

enum Pass : uint32_t { kFibre = 0, kSpec = 1, kPasses = 2 };

struct Summary {
  float median = 0, p10 = 0, p90 = 0;
  size_t frames = 0;
};

struct Window {
  std::vector<float> ms[kPasses];

  // t: the slot's 4 timestamps; ran: bit p set when pass p was dispatched in that frame (both edges written).
  void add(const uint64_t t[4], uint32_t ran, double ns_per_tick) {
    for (uint32_t p = 0; p < kPasses; ++p) {
      if (!(ran & (1u << p))) continue;
      const uint64_t a = t[2 * p], b = t[2 * p + 1];
      if (b < a) continue;   // wrapped or reordered: not a sample
      const double v = double(b - a) * ns_per_tick * 1e-6;
      if (v >= 0 && v < 1000) ms[p].push_back(float(v));
    }
  }
  // Pass p's summary once it holds `period` samples (then its window starts over), else false.
  bool take(uint32_t p, size_t period, Summary& out) {
    auto& v = ms[p];
    if (period == 0 || v.size() < period) return false;
    std::sort(v.begin(), v.end());
    out.median = v[v.size() / 2];
    out.p10 = v[v.size() / 10];
    out.p90 = v[v.size() * 9 / 10];
    out.frames = v.size();
    v.clear();
    return true;
  }
  void clear() {
    for (auto& v : ms) v.clear();
  }
};

}  // namespace bg3::hair_fibre_timing
