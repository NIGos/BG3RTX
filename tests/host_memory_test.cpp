// common/host_memory.h: host-visible buffers fall back to system memory when the BAR heap is full.
// Regression of 2026-11-07: without Resizable BAR the BVH split's instance ring failed in the 214 MiB
// BAR heap, the split turned off and hair grooms never joined a draw.
#include "../common/host_memory.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::host_memory;
static unsigned checks;
static void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "host memory check %u failed\n", checks); std::exit(1); } }

int main() {
  // The user's 5090 after the reinstall (vulkaninfo): heap 0 VRAM, heap 1 system, heap 2 the 214 MiB BAR.
  const uint32_t no_rebar[] = {0x0, kDeviceLocal, kSystem, kSystem | 0x8 /*cached*/, kBar};
  const uint32_t no_rebar_heap[] = {1, 0, 1, 1, 2};
  // Resizable BAR on: the BAR type lives in the VRAM heap.
  const uint32_t rebar[] = {0x0, kDeviceLocal, kSystem, kSystem | 0x8, kBar};
  const uint32_t all = 0x1f;

  // BAR heap full: before the fix the buffer failed; now system memory takes it (type 2).
  {
    bool bar_full = true, fell_back = false;
    unsigned tries = 0;
    const uint32_t t = allocate_host_visible(no_rebar, 5, all, [&](uint32_t type) {
      ++tries;
      return !(bar_full && no_rebar_heap[type] == 2);
    }, &fell_back);
    check(t == 2);
    check(fell_back);
    check(tries == 2);
  }
  // BAR heap with room: unchanged choice, one allocation, no fallback.
  {
    bool fell_back = true;
    unsigned tries = 0;
    check(allocate_host_visible(rebar, 5, all, [&](uint32_t) { ++tries; return true; }, &fell_back) == 4);
    check(!fell_back && tries == 1);
  }
  // No BAR type allowed by the resource: system memory directly (the old fallback), not counted as one.
  {
    bool fell_back = true;
    check(allocate_host_visible(no_rebar, 5, all & ~(1u << 4), [](uint32_t) { return true; }, &fell_back) == 2);
    check(!fell_back);
  }
  // Both fail: no type; never a device-local-only or the BAR type twice.
  {
    unsigned tries = 0;
    check(allocate_host_visible(no_rebar, 5, all, [&](uint32_t type) { ++tries; check(type == 4 || type == 2); return false; }) == kNoType);
    check(tries == 2);
  }
  // Only the BAR type is host-visible: no second try of the same full heap.
  {
    const uint32_t bar_only[] = {kDeviceLocal, kBar};
    unsigned tries = 0;
    check(allocate_host_visible(bar_only, 2, 0x3, [&](uint32_t) { ++tries; return false; }) == kNoType);
    check(tries == 1);
  }
  // find_type honours the resource's bits and the exclusion.
  check(find_type(no_rebar, 5, all, kSystem, kDeviceLocal) == 2);
  check(find_type(no_rebar, 5, all & ~0x4u & ~0x8u, kSystem, kDeviceLocal) == kNoType);
  check(find_type(no_rebar, 5, all, kSystem) == 2);
  std::printf("host_memory_test: %u checks passed\n", checks);
  return 0;
}
