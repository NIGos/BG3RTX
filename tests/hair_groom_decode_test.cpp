#include "../common/hair_groom_decode.h"
#include "../common/async_capture_queue.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>
using namespace bg3::hair_groom;
static unsigned checks;
static void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "groom decode check %u failed\n", checks); std::exit(1); } }
int main() {
  using Slots = DecodeSlots<int, std::vector<int>>;
  using Ask = Slots::Ask;
  Slots s;
  s.capacity = 2; s.stale = 10;
  std::vector<int> out;
  check(s.ask(1, 0, &out) == Ask::Start);
  check(s.ask(1, 1, &out) == Ask::Pending && out.empty());        // in flight: asked once, queued once
  check(s.ask(2, 1, &out) == Ask::Start);
  check(s.ask(3, 1, &out) == Ask::Busy && s.slots.size() == 2);   // bounded: never more than `capacity` buffers
  s.finish(1, true, {7, 8});
  const int* data = s.slots[1].value.data();
  check(s.ask(1, 2, &out) == Ask::Ready && out.size() == 2 && out.data() == data);   // moved: views into it stay valid
  check(s.slots.size() == 1 && s.ask(1, 3, &out) == Ask::Start);  // taken once; a later ask decodes again
  s.finish(1, false, {});
  check(s.ask(1, 4, &out) == Ask::Failed && s.slots.size() == 1);
  // A finished decode nobody takes (the groom left the view) yields its slot after `stale` frames; in flight never.
  check(s.ask(4, 5, &out) == Ask::Start);
  s.finish(4, true, {1});
  check(s.ask(5, 15, &out) == Ask::Busy);                         // 10 frames: not stale yet, 2 is still in flight
  check(s.ask(5, 16, &out) == Ask::Start && !s.slots.count(4) && s.slots.count(2));
  check(s.ask(6, 1000, &out) == Ask::Busy);                       // 2 and 5 in flight
  s.finish(9, true, {1});                                         // a result for a dropped slot is discarded
  check(s.slots.size() == 2);
  s.cancel(5);
  check(s.ask(6, 1000, &out) == Ask::Start);
  // With the worker queue: every request is answered once, none lost, on another thread.
  {
    Slots t;
    t.capacity = 4;
    std::mutex mu;
    std::atomic<int> decoded{0};
    AsyncCaptureQueue<int> q(8, [&](int&& key) {
      std::vector<int> v(1000, key);
      ++decoded;
      std::lock_guard lock(mu);
      t.finish(key, true, std::move(v));
    });
    int ready = 0;
    bool taken[16] = {};
    for (uint64_t frame = 0; ready < 16 && frame < 100000000; ++frame)
      for (int key = 0; key < 16; ++key) {
        if (taken[key]) continue;
        std::vector<int> v;
        std::unique_lock lock(mu);
        const Ask a = t.ask(key, frame, &v);
        if (a == Ask::Start) { lock.unlock(); if (!q.submit(int(key))) { lock.lock(); t.cancel(key); } }
        else if (a == Ask::Ready) { check(v.size() == 1000 && v[0] == key); taken[key] = true; ++ready; }
      }
    q.close();
    check(ready == 16 && decoded == 16);
  }
  std::printf("groom decode slots: %u checks\n", checks);
}
