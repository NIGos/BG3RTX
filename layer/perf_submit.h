#pragma once
#include "host.h"
namespace bg3 {
struct SubmitCpuTimer {
  DiagCounter counter;
  LARGE_INTEGER start{};
  explicit SubmitCpuTimer(DiagCounter c) : counter(c) {
#if BG3RTX_DIAGNOSTICS
    if (cfg_int("cpu_timing",0)) QueryPerformanceCounter(&start);
#endif
  }
  ~SubmitCpuTimer() {
    if (!start.QuadPart) return;
    LARGE_INTEGER end, frequency; QueryPerformanceCounter(&end); QueryPerformanceFrequency(&frequency);
    diag(counter, uint64_t((end.QuadPart-start.QuadPart)*1000000/frequency.QuadPart));
  }
};
}
