#pragma once
// Log-only nested CPU receipts. Enabled once per present, never a cfg lookup per draw.
#include "host.h"
namespace bg3 {
enum CpuDetailSlot {CpuDetailPipeline,CpuDetailCascade,CpuDetailBvh,CpuDetailAlpha,
                    CpuDetailMaterial,CpuDetailDispatchPre,CpuDetailDispatchPost,CpuDetailCount};
#if BG3RTX_DIAGNOSTICS
extern std::atomic<bool> g_cpu_detail;
extern std::atomic<uint64_t> g_cpu_detail_ticks[CpuDetailCount];
struct CpuDetailTimer {
 int slot;LARGE_INTEGER begin{};
 explicit CpuDetailTimer(int s):slot(g_cpu_detail.load(std::memory_order_relaxed)?s:-1){if(slot>=0)QueryPerformanceCounter(&begin);}
 ~CpuDetailTimer(){if(slot>=0){LARGE_INTEGER end;QueryPerformanceCounter(&end);g_cpu_detail_ticks[slot].fetch_add(uint64_t(end.QuadPart-begin.QuadPart),std::memory_order_relaxed);}}
};
#else
struct CpuDetailTimer {explicit CpuDetailTimer(int){}};
#endif
}
