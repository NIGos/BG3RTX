// common/pipeline_async.h: the background pipeline builds of core/gi.cpp refresh and core/water.cpp
// make_pipe. Start, busy, the result handed over once, failure cooldown, a parked layout destroyed only
// after its builds finish (and their unclaimed results discarded), join discarding the rest.
#include "../common/pipeline_async.h"
#include "../common/pipeline_specs.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace pa = bg3::pipe_async;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  // The queued payload owns its values after the render stack is changed/destroyed.
  pa::PipelineSpecs off;
  off.add(1008, 0); off.add(1010, 0); off.add(1014, 0); off.add(1017, 0);
  auto on = off;
  on.data[1] = 1;
  CHECK(off.key() != on.key());
  auto edge = on;
  edge.data[2] = 1;
  CHECK(edge.key() != on.key());
  on.data[3] = 1;
  CHECK(off.key() != on.key());
  auto gi_cull_off = on; gi_cull_off.data[3] = 0;
  CHECK(gi_cull_off.key() != on.key());
  pa::Builds specialization_builds(1, [](const pa::Result&) {});
  CHECK(specialization_builds.poll(on.key(), 1, [on] {
    VkSpecializationInfo info{on.count, on.entries, on.count * sizeof(uint32_t), on.data};
    const auto* values = static_cast<const uint32_t*>(info.pData);
    return pa::Result{values[1] == 1 && values[2] == 0 && info.pMapEntries[1].constantID == 1010 && values[3] == 1 && info.pMapEntries[3].constantID == 1017 ? 42u : 0u, 1, true};
  }, nullptr) == pa::State::kStarted);
  const auto on_key = on.key();
  on.data[1] = 0; on.data[2] = 1; on.data[3] = 0;
  specialization_builds.wait_idle();
  pa::Result specialized{};
  CHECK(specialization_builds.poll(on_key, 1, {}, &specialized) == pa::State::kReady);
  CHECK(specialized.pipeline == 42);
  std::set<uint64_t> discarded;
  std::atomic<int> inits{0};
  {
    pa::Builds b(2, [&](const pa::Result& r) { discarded.insert(r.pipeline); }, [&] { ++inits; });
    std::atomic<bool> gate{false};
    auto slow = [&](uint64_t h) {
      return [&gate, h] {
        while (!gate) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return pa::Result{h, 7, true};
      };
    };
    pa::Result r{};
    // A new key starts; polled again while it runs it is busy and nothing is handed over.
    CHECK(b.poll("a", 100, slow(1), &r) == pa::State::kStarted);
    CHECK(b.poll("a", 100, slow(1), &r) == pa::State::kBuilding);
    CHECK(r.pipeline == 0);
    CHECK(b.busy(100) && !b.busy(200));
    // A layout parked while its build runs is not destroyed yet.
    bool destroyed = false;
    b.park(100, [&] { destroyed = true; });
    CHECK(!destroyed);
    gate = true;
    b.wait_idle();
    CHECK(!b.busy(100));
    // The next poll drains the parked layout: its unclaimed result is discarded first, then the layout goes.
    CHECK(b.poll("b", 200, [] { return pa::Result{2, 1, true}; }, &r) == pa::State::kStarted);
    CHECK(destroyed);
    CHECK(discarded.count(1) == 1);
    b.wait_idle();
    // Ready: handed over once, then the key is new again.
    CHECK(b.poll("b", 200, nullptr, &r) == pa::State::kReady);
    CHECK(r.pipeline == 2 && r.generation == 1 && r.ok);
    CHECK(b.pending() == 0);
    // A parked layout with no build is destroyed at once.
    bool d2 = false;
    b.park(300, [&] { d2 = true; });
    CHECK(d2);
    // Failure: reported once with the result, then kFailed (no rebuild) for the cooldown, then built again.
    int runs = 0;
    auto fail = [&runs] { ++runs; return pa::Result{}; };
    CHECK(b.poll("f", 200, fail, &r) == pa::State::kStarted);
    b.wait_idle();
    CHECK(b.poll("f", 200, fail, &r) == pa::State::kFailed && !r.ok);
    for (unsigned i = 0; i < pa::kFailCooldown; ++i) CHECK(b.poll("f", 200, fail, &r) == pa::State::kFailed);
    CHECK(runs == 1);
    CHECK(b.poll("f", 200, fail, &r) == pa::State::kStarted);
    b.wait_idle();
    CHECK(runs == 2);
    // Several keys at once on two workers: all finish, each result its own.
    for (uint64_t k = 10; k < 20; ++k)
      CHECK(b.poll("k" + std::to_string(k), 200, [k] { return pa::Result{k, k, true}; }, &r) == pa::State::kStarted);
    b.wait_idle();
    for (uint64_t k = 10; k < 15; ++k) {
      CHECK(b.poll("k" + std::to_string(k), 200, nullptr, &r) == pa::State::kReady);
      CHECK(r.pipeline == k);
    }
    // join: the unclaimed results (k15..k19 and the failed "f") go to discard; the pool can start again.
    b.join();
    for (uint64_t k = 15; k < 20; ++k) CHECK(discarded.count(k) == 1);
    for (uint64_t k = 10; k < 15; ++k) CHECK(discarded.count(k) == 0);
    CHECK(b.poll("again", 200, [] { return pa::Result{50, 1, true}; }, &r) == pa::State::kStarted);
    b.wait_idle();
    CHECK(b.poll("again", 200, nullptr, &r) == pa::State::kReady && r.pipeline == 50);
    // join with a queued build that never ran: dropped (nothing to discard), no hang.
    std::atomic<bool> hold{false};
    std::atomic<int> running{0};
    auto held = [&hold, &running] {
      ++running;
      while (!hold) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return pa::Result{60, 1, true};
    };
    b.poll("h1", 200, held, &r);
    b.poll("h2", 200, held, &r);
    b.poll("h3", 200, [] { return pa::Result{61, 1, true}; }, &r);
    while (running < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // both workers busy, h3 queued
    std::thread release([&hold] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); hold = true; });
    b.join();
    release.join();
    CHECK(discarded.count(60) == 1);   // the two running ones finished and were discarded
    CHECK(discarded.count(61) == 0);   // the queued one never ran
  }
  CHECK(inits >= 2);

  // Monitor: windows of 4 presents; a burst from the interval where a build started to the first present with none
  // pending (its wall time includes the interval the start fell in), the longest present interval inside it.
  {
    pa::Monitor m;
    pa::Monitor::Window w;
    pa::Monitor::Burst b;
    // t: 0, 16, 32 idle; a build starts in (32, 48]; pending until the present at 1048 (a 1000 ms present gap at 1048).
    CHECK(m.tick(0, 0, 0, 4, &w, &b) == 0);
    CHECK(m.tick(0, 0, 16, 4, &w, &b) == 0);
    CHECK(m.tick(0, 0, 32, 4, &w, &b) == 0);
    CHECK(m.tick(1, 1, 48, 4, &w, &b) == pa::Monitor::kWindow);   // 4th present: the window; the burst runs
    CHECK(w.frames == 4 && w.waiting == 1 && w.started == 1 && w.pending == 1);
    CHECK(m.tick(2, 2, 64, 4, &w, &b) == 0);
    CHECK(m.tick(1, 2, 1064, 4, &w, &b) == 0);
    const unsigned ev = m.tick(0, 2, 1080, 4, &w, &b);
    CHECK(ev == pa::Monitor::kBurst);
    CHECK(b.builds == 2 && b.frames == 4);
    CHECK(b.ms == 1080 - 32);        // from the present before the start
    CHECK(b.longest_ms == 1000);
    CHECK(m.tick(0, 2, 1096, 4, &w, &b) == pa::Monitor::kWindow);
    CHECK(w.frames == 4 && w.waiting == 2 && w.started == 1 && w.pending == 0);
    // A build that starts and finishes between two presents is still a burst (pending 0 at the tick).
    CHECK(m.tick(0, 3, 1112, 4, &w, &b) == pa::Monitor::kBurst);
    CHECK(b.builds == 1 && b.frames == 1 && b.ms == 16);
    // Idle windows report zeros.
    for (int i = 0; i < 2; ++i) m.tick(0, 3, 1128 + 16 * i, 4, &w, &b);
    CHECK(m.tick(0, 3, 1160, 4, &w, &b) == pa::Monitor::kWindow);
    CHECK(w.waiting == 0 && w.started == 1 && w.pending == 0);   // the window holds the 1112 start
    for (int i = 0; i < 3; ++i) m.tick(0, 3, 1176 + 16 * i, 4, &w, &b);
    CHECK(m.tick(0, 3, 1224, 4, &w, &b) == pa::Monitor::kWindow);
    CHECK(w.waiting == 0 && w.started == 0 && w.pending == 0);
  }
  std::printf("pipeline_async_test: %d checks passed\n", checks);
  return 0;
}
