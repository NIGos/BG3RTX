#pragma once
// rt_vram_guard: keep this process's device-local VRAM below the driver's budget (VK_EXT_memory_budget).
// Pure CPU policy; the layer samples the budget (layer.cpp vram_guard_tick) and the consumers ask before
// they grow (bvhsplit AS chunks and mesh pool, geocapture cascade capture) or yield when asked (empty AS
// chunks, the hit-material recycled pool, LRU meshes). docs/progress/vram-guard.md has the evidence.
//
// Evidence (user's bg3rtx.log 2026-11-02 20:04, S8 port, low camera long view): used 21992 MB of a
// 31419 MB budget at 20:04:07, 31668 MB of 30630 MB at 20:04:30 and over budget from then on, with
// Streamline "Frame rate over 100 ms" and fence timeouts. In those 23 s our side grew: BLAS chunks
// 81 -> 124 (64 MB each, +2.7 GB reserved for 253 -> 1059 MB live) and the cascade capture 427 -> 544 MB
// (x3 frame buffers). The game's own share grew too (streaming for the long view): we cannot hold the
// game below budget, only stop adding to it and give back what no frame uses.
#include <algorithm>
#include <cstdint>

namespace bg3::vram_guard {
inline constexpr uint64_t mib = uint64_t(1) << 20;

// ---- headroom ----
// Driver slack: the budget the driver reports is not fixed (other processes, its own reserve). In the
// evidence it fell 789 MB (31419 -> 30630) between two [mem] samples as the process crossed it: 2.5% of
// the budget. The slack is that fraction (the floor), or the largest RECENT drop (Guard::recent_drop: this
// and the last stats window, void once the budget is back), whichever is larger, never above slack_cap.
//
// Why recent and capped (user's bg3rtx.log 2026-11-13 13:41): the budget fell 31419 -> 15477 MB in one
// sample (Windows cuts the budget of a process whose window is not in the foreground) and to 13522 MB
// after. With the session's largest drop kept for ever the margin was 15942 + step = 16454 MB: leaving Low
// needed used + 2 x 16454 <= budget, impossible even at 31419 MB, and allow() refused everything above
// 31419 - 15942 = 15477 MB. One budget dip cost the rest of the session. A drop that large is a change of
// budget, already in the new budget; it is not slack the driver may take again.
inline constexpr uint64_t kSlackPermille = 25;   // 789 / 31419 = 25.1 per mille
// The smallest grow step any consumer makes: one BLAS chunk (rt_mesh_chunk_mb 64).
inline constexpr uint64_t kMinStep = 64 * mib;
uint64_t slack_cap(uint64_t budget);
inline uint64_t slack(uint64_t budget, uint64_t observed_drop) {
  return (std::max)(budget / 1000 * kSlackPermille, (std::min)(observed_drop, slack_cap(budget)));
}
// Margin = the largest single grow step (allocated while the buffer it replaces is still alive) + slack:
// with `used + margin <= budget` the next such step still fits with the driver's slack to spare.
inline uint64_t margin(uint64_t budget, uint64_t largest_step, uint64_t observed_drop) {
  return (std::max)(largest_step, kMinStep) + slack(budget, observed_drop);
}

// ---- per-consumer caps from the device budget (smaller cards) ----
// Each growable consumer's cap is its size qualified on the reference card (RTX 5090, budget 31419 MB,
// the evidence log) scaled by budget / reference, never below the size it starts at (the default, which
// every card ran before the guard) and never above the reference cap (unchanged behaviour on 32 GB).
// RT capture keeps its layout cap: required geometry uses real aggregate headroom instead of a share.
inline constexpr uint64_t kRefBudget = 31419 * mib;
enum Consumer : int { kAsChunks, kMeshPool, kCascade, kHitRecycle, kRtCapture, kConsumerCount };
inline constexpr const char* kConsumerName[kConsumerCount] = {"BLAS chunks", "mesh pool", "cascade capture",
                                                              "hit-material recycled buffers", "RT capture"};
struct CapRef { uint64_t ref, floor; };
inline constexpr CapRef kCapRef[kConsumerCount] = {
    // BLAS chunks: 66 chunks (4224 MB) held the long-session test within budget; floor 22 chunks (1408 MB)
    // = the largest live AS of the long-session test (1.4 GB), so the cap never forces live BLAS out.
    {66 * 64 * mib, 22 * 64 * mib},
    // Mesh pool: capture_budget::kMeshPoolHardCap (2047 MB) on the reference; floor rt_mesh_pool_mb 256.
    {2047 * mib, 256 * mib},
    // Cascade capture (one of the 3 frame buffers): the same 2047 MB hard cap; floor 427 MB (rt_cascade_mb
    // 160 at the 64-byte layout, capture_budget::effective_mb).
    {2047 * mib, 427 * mib},
    // Hit-material recycled pool: native_hit::recycle::kCapBytes (512 MB); floor 0 (pure reuse cache).
    {512 * mib, 0},
    {2047 * mib, 1024 * mib}, // RT capture: separate accounting from the cascade slots.
};

// Refusals are retries, not concurrent allocations. Report their peak allocation
// footprint, not their sum (vg2: 25 x 960 MiB was misleadingly called 24000 MiB).
struct RefusedRequests {
  uint64_t count = 0, peak = 0;
  void note(uint64_t bytes) { ++count; peak = (std::max)(peak, bytes); }
};
inline uint64_t cap(Consumer c, uint64_t budget) {
  const CapRef r = kCapRef[c];
  // Required RT geometry is limited by real aggregate headroom at allocation,
  // not by a scaled share that can discard opaque draws with gigabytes free.
  if (c == kRtCapture) return r.ref;
  if (!budget) return r.ref;
  const uint64_t scaled = uint64_t(double(r.ref) * double(budget) / double(kRefBudget)) / mib * mib;
  return (std::clamp)(scaled, r.floor, r.ref);
}

// Slack cap: what the yielders can give back at most at this budget (the caps of BLAS chunks, mesh pool and
// the recycled pool: 4224 + 2047 + 512 = 6783 MB = 21.6% of the reference budget). Slack is held so that a
// budget drop can be met by yielding; a reserve beyond everything the guard could free protects nothing
// and only refuses growth.
inline uint64_t slack_cap(uint64_t budget) {
  return cap(kAsChunks, budget) + cap(kMeshPool, budget) + cap(kHitRecycle, budget);
}

// ---- state: hysteresis ----
// Low when the next largest step would not fit (used + margin > budget); back to normal only when two
// margins fit (far below), so one freed chunk does not flip it every sample. In Low every growth is
// refused (except required RT capture) and the yielders are asked to free down to the exit threshold. Growth in normal state is still
// checked per request against the headroom left (used + the bytes granted since the sample + slack).
struct Guard {
  bool low = false;
  // The OS budget is below what the process uses (used > budget); cleared when Low is left. For the one
  // log line per transition (layer.cpp): cache trimming cannot fix this, the user has to know.
  bool starved = false;
  uint64_t used = 0, budget = 0, prev_budget = 0, budget_peak = 0;
  // Largest single-sample budget drop of this stats window and the last one (the memory of largest_step),
  // and the budget before the oldest of them: a budget back at that level has undone the drop.
  uint64_t drop_cur = 0, drop_prev = 0, drop_from = 0;
  // The trim need the yielders were last asked for in this Low episode (trim_ask).
  uint64_t trim_asked = 0;
  uint64_t granted = 0;                   // bytes allowed since the last sample (not yet in `used`)
  uint64_t step_cur = 0, step_prev = 0;   // largest request this window and the last one
  uint64_t largest_step() const { return (std::max)((std::max)(step_cur, step_prev), kMinStep); }
  uint64_t recent_drop() const { return (std::max)(drop_cur, drop_prev); }
  uint64_t margin_now() const { return margin(budget, largest_step(), recent_drop()); }
  // Returns true when the state changed.
  bool sample(uint64_t used_bytes, uint64_t budget_bytes) {
    if (prev_budget && budget_bytes < prev_budget) {
      drop_cur = (std::max)(drop_cur, prev_budget - budget_bytes);
      drop_from = (std::max)(drop_from, prev_budget);
    }
    if (budget_bytes >= drop_from) drop_cur = drop_prev = drop_from = 0;   // the driver gave it back
    prev_budget = budget_bytes;
    budget_peak = (std::max)(budget_peak, budget_bytes);
    used = used_bytes;
    budget = budget_bytes;
    granted = 0;
    const bool was = low;
    if (!budget) { low = starved = false; trim_asked = 0; return was != low; }
    const uint64_t m = margin_now();
    if (!low && used + m > budget) low = true;
    else if (low && used + 2 * m <= budget) low = false;
    if (!low) { starved = false; trim_asked = 0; }
    else if (used > budget) starved = true;
    return was != low;
  }
  // A grow request of `bytes` (everything allocated at once, the replaced buffer counted as still alive).
  bool allow(uint64_t bytes) {
    step_cur = (std::max)(step_cur, bytes);
    if (!budget) return true;   // no budget known (extension missing, no sample yet): unchanged behaviour
    if (low) return false;
    const uint64_t reserve = slack(budget, recent_drop());
    if (reserve > budget || used > budget - reserve || granted > budget - reserve - used ||
        bytes > budget - reserve - used - granted) return false;
    granted += bytes;
    return true;
  }
  // Required RT capture growth must not inherit cache-trimming hysteresis,
  // speculative slack or a simulated budget. Count the FULL replacement while
  // its old buffer survives, and share reservations with ordinary consumers.
  bool allow_capture(uint64_t bytes, uint64_t real_budget) {
    step_cur = (std::max)(step_cur, bytes);
    if (!real_budget) return true;  // extension/sample unavailable: legacy behaviour
    if (used > real_budget || granted > real_budget - used ||
        bytes > real_budget - used - granted) return false;
    granted += bytes;
    return true;
  }
  void new_window() {
    step_prev = step_cur; step_cur = 0;
    drop_prev = drop_cur; drop_cur = 0;
    if (!drop_prev) drop_from = 0;
  }
  // Bytes to give back now: down to the exit threshold (used + 2 margins = budget), 0 when not low.
  uint64_t trim_need() const {
    if (!low || !budget) return 0;
    const uint64_t target = used + 2 * margin_now();
    return target > budget ? target - budget : 0;
  }
  // Bytes to ask the yielders for at this sample: the whole need once per Low episode, and again only when
  // the need is worse than the one already acted on by a chunk (kMinStep, the smallest amount a yielder can
  // return to the device). A need the yielders cannot meet (the game's own share is over a low budget) is
  // not asked for again at every sample: that cleared the recycled pool and evicted LRU meshes every 16
  // frames for the rest of the session. The cache keeps its demand-driven share eviction (bvhsplit).
  uint64_t trim_ask() {
    const uint64_t need = trim_need();
    if (!need || (trim_asked && need < trim_asked + kMinStep)) return 0;
    trim_asked = need;
    return need;
  }
};

// ---- who yields first ----
// Order: what costs nothing first, detail last. 1 empty BLAS chunks (no live range: free), 2 the
// hit-material recycled buffers (a reuse cache: costs allocations next frames, no detail), 3 cached
// meshes not used for 8 frames, least recently used first (their BLAS ranges; rebuilt when seen again,
// and a chunk they empty is freed at the next ask). Live need (anything used in the last 8 frames, any
// range a frame in flight reads) is never in `avail`. Refused growth (detail left out, logged) is the
// last resort and is implicit: every grow is refused while low.
enum Yield : int { kYieldEmptyChunks, kYieldRecycled, kYieldMeshLru, kYieldCount };
inline constexpr const char* kYieldName[kYieldCount] = {"empty BLAS chunks", "recycled hit-material buffers",
                                                        "LRU meshes"};
inline void plan_trim(uint64_t need, const uint64_t avail[kYieldCount], uint64_t ask[kYieldCount]) {
  for (int i = 0; i < kYieldCount; ++i) {
    ask[i] = (std::min)(need, avail[i]);
    need -= ask[i];
  }
}
}  // namespace bg3::vram_guard
