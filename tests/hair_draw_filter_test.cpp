// common/hair_primary.h DrawFilter: the lock-free leave of layer/hair_primary.cpp hair_primary_swap. The filter may
// only skip draws the locked path leaves untouched: a random session of pipeline creations, publishes, prepass drops
// and draws is decided by the locked rule and by the filter in front of it; every decision must match. Then the cost
// of both for one frame of game draws that are not hair (synthetic: no Vulkan, no game; printed, not gated).
#include "../common/hair_primary.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <random>
#include <thread>

namespace {
int g_fail = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++g_fail; } } while (0)

struct Model {   // the containers the locked path reads before it leaves a draw alone
  std::mutex mu;
  std::unordered_map<uint64_t, uint32_t> hair_pipes;
  std::unordered_map<uint64_t, uint64_t> variant, cascade_clone;
  hair_primary::SwapGate gate;
  hair_primary::DepthLatch depth;
  hair_primary::DrawFilter pipe_filter, key_filter;
  // true: the locked path does something for this draw (counts it, swaps it, restores its depth...).
  bool locked_matters(uint64_t pipe, uint64_t key, bool sun, bool main_view) {
    std::lock_guard<std::mutex> lock(mu);
    if (sun) return cascade_clone.count(pipe) != 0;
    if (hair_pipes.count(pipe)) return true;   // participation is counted
    if (!main_view) return false;
    return variant.count(pipe) || gate.published.count(key) || gate.published_prev.count(key) || depth.keys.count(key);
  }
  bool filter_leaves(uint64_t pipe, uint64_t key) const { return !pipe_filter.maybe(pipe) && !key_filter.maybe(key); }
};
}  // namespace

int main() {
  Model m;
  std::mt19937_64 rng(20261114);
  std::vector<uint64_t> pipes, keys;
  for (int i = 0; i < 4000; ++i) pipes.push_back((rng() & ~7ull) | 8);   // handles: pointer-like
  for (int i = 0; i < 4000; ++i) keys.push_back(rng() | 1);
  uint64_t frame = 1, left = 0, mattered = 0, sent = 0;
  for (int step = 0; step < 400000; ++step) {
    const uint64_t pipe = pipes[rng() % pipes.size()], key = keys[rng() % keys.size()];
    const uint32_t op = uint32_t(rng() % 1000);
    if (op < 2) {   // a hair pipeline is created: filter first, then the map (as the layer does under g_mu)
      const uint64_t hp = pipes[rng() % 300];
      m.pipe_filter.add(hp);
      const uint32_t kind = uint32_t(rng() % 3);
      if (kind == 0) m.hair_pipes[hp] = 1; else if (kind == 1) m.variant[hp] = 1; else m.cascade_clone[hp] = 1;
    } else if (op < 3) {   // a hair pipeline is destroyed: the filter keeps its bit
      const uint64_t hp = pipes[rng() % 300];
      m.hair_pipes.erase(hp); m.variant.erase(hp); m.cascade_clone.erase(hp);
    } else if (op < 8) {   // a publish (twice in a row for the same set now and then: published and published_prev)
      uint64_t pub[hair_primary::kMaxGrooms] = {};
      const uint32_t n = uint32_t(rng() % 12);
      for (uint32_t k = 0; k < n; ++k) pub[k] = keys[rng() % 200];
      for (uint32_t k = 0; k < n; ++k) m.key_filter.add(pub[k]);
      m.gate.publish(pub, n, ++frame);
      m.depth.prune(frame);
    } else {
      const bool sun = rng() % 4 == 0, main_view = !sun && rng() % 3 != 0;
      const bool matters = m.locked_matters(pipe, key, sun, main_view);
      const bool leaves = m.filter_leaves(pipe, key);
      CHECK(!(leaves && matters));   // the filter never skips a draw the locked path acts on
      left += leaves; mattered += matters; sent += !leaves;
      if (!leaves && matters && main_view && rng() % 8 == 0) {   // a prepass drop of this key (past the leave check)
        m.key_filter.add(key);
        m.depth.prepass(key, frame);
      }
    }
  }
  CHECK(mattered > 1000 && left > 100000);   // both outcomes exercised
  std::printf("equivalence: %llu draws left lock-free, %llu sent to the locked path (%llu mattered)\n",
              (unsigned long long)left, (unsigned long long)sent, (unsigned long long)mattered);

  // Cost: 7000 non-hair draws per frame, the inn's count, on 1 and 4 recording threads.
  for (int threads : {1, 4}) {
    for (int mode = 0; mode < 2; ++mode) {
      const int frames = 300, draws = 7000;
      std::atomic<uint64_t> hits{0};
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<std::thread> pool;
      for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
          std::mt19937_64 r(t + 1);
          uint64_t h = 0;
          for (int i = 0; i < frames * draws / threads; ++i) {
            const uint64_t pipe = r() | 1ull << 63, key = r() | 1ull << 63;   // game draws: never added
            if (mode == 0) h += m.locked_matters(pipe, key, (i & 3) == 0, (i & 3) != 0);
            else h += !m.filter_leaves(pipe, key) && m.locked_matters(pipe, key, (i & 3) == 0, (i & 3) != 0);
          }
          hits += h;
        });
      for (auto& th : pool) th.join();
      const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
      std::printf("%s, %d thread%s: %.1f ns/draw, %.3f ms per 7000-draw frame (thread sum), %llu acted on\n",
                  mode ? "filter first" : "locked only ", threads, threads > 1 ? "s" : "",
                  ns * threads / (double(frames) * draws), ns * threads / frames * 1e-6, (unsigned long long)hits.load());
    }
  }
  if (g_fail) { std::printf("%d failures\n", g_fail); return 1; }
  std::printf("hair_draw_filter_test OK\n");
  return 0;
}
