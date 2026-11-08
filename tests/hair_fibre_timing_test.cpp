#include "../common/hair_fibre_timing.h"
#include <cstdio>

using namespace bg3::hair_fibre_timing;

int main() {
  Window w;
  Summary s;
  // 1 tick = 1 ns: fibre 2 ms, spec 0.5 ms.
  const uint64_t t[4] = {1000000, 3000000, 5000000, 5500000};
  for (int i = 0; i < 9; ++i) w.add(t, 1u, 1.0);   // the spec pass did not run: no spec sample
  if (w.ms[kSpec].size() != 0 || w.ms[kFibre].size() != 9) return 1;
  if (w.take(kFibre, 10, s)) return 2;              // not yet a full window
  w.add(t, 3u, 1.0);
  if (!w.take(kFibre, 10, s) || s.frames != 10 || s.median != 2.0f || s.p90 != 2.0f) return 3;
  if (!w.ms[kFibre].empty()) return 4;              // the window starts over
  if (w.ms[kSpec].size() != 1 || w.ms[kSpec][0] != 0.5f) return 5;
  // A reordered pair (end before begin) is not a sample, never a zero.
  const uint64_t bad[4] = {3000000, 1000000, 0, 0};
  w.add(bad, 3u, 1.0);
  if (!w.ms[kFibre].empty() || w.ms[kSpec].size() != 2 || w.ms[kSpec][1] != 0.0f) return 6;
  // Medians and percentiles from sorted samples; the tick period applies.
  Window m;
  for (uint64_t i = 1; i <= 10; ++i) {
    const uint64_t u[4] = {0, i * 1000000, 0, 0};
    m.add(u, 1u, 0.5);   // 0.5 ns per tick: i * 0.5 ms
  }
  if (!m.take(kFibre, 10, s) || s.median != 3.0f || s.p10 != 1.0f || s.p90 != 5.0f) return 7;
  if (m.take(kFibre, 0, s)) return 8;
  w.clear();
  if (!w.ms[kSpec].empty()) return 9;
  std::puts("hair_fibre_timing_test: ok");
  return 0;
}
