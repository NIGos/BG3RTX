#pragma once
// rt_hair_groom_async: the bookkeeping of groom LOD decodes done on a worker thread (layer/hair_groom.cpp
// read_lod_async), shared with its unit test. Not thread-safe: the caller holds its own lock.
// User log 2026-11-14 22:12-22:27 (RTX Hair on): 228 synchronous decodes inside the scene build, median 47 ms,
// max 203 ms, 15.6 s in all; several per second when a group of characters comes into view.
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace bg3::hair_groom {
enum class LodDecode { Pending, Ready, Failed };

// At most `capacity` decodes are in flight or waiting to be taken (each holds one LOD's bytes). A finished
// decode nobody asked about for `stale` frames gives its slot to a new request; one in flight is never dropped.
// Each Start queues one wake-up of the worker; the worker asks take() which request to decode (r4-hitch round 3, hx1a
// 033523: one worker decoding every request in order kept a newly seen head on cards 18-21 frames).
template<class Key, class Value> struct DecodeSlots {
  enum class Ask { Start, Pending, Busy, Ready, Failed };
  struct Slot { bool done = false, ok = false, started = false; uint64_t asked = 0, since = 0; Value value; };
  std::map<Key, Slot> slots;
  size_t capacity = 4;
  uint64_t stale = 120;
  // The newest frame any ask named. A queued decode is still wanted while its LOD was asked in that frame or the one
  // before (groom_lod asks a wanted LOD at every build, and the build of `last` may not have reached its groom yet).
  uint64_t last = 0;
  bool wanted(const Slot& s) const { return s.asked + 1 >= last; }
  // Start: the caller queues the decode of `key` now. Ready: `*out` has the value and the slot is gone.
  Ask ask(const Key& key, uint64_t now, Value* out) {
    if (now > last) last = now;
    auto it = slots.find(key);
    if (it != slots.end()) {
      it->second.asked = now;
      if (!it->second.done) return Ask::Pending;
      const bool ok = it->second.ok;
      if (ok && out) *out = std::move(it->second.value);
      slots.erase(it);
      return ok ? Ask::Ready : Ask::Failed;
    }
    if (slots.size() >= capacity)
      for (auto s = slots.begin(); s != slots.end();)
        s = (s->second.done && now >= s->second.asked && now - s->second.asked > stale) ||
            (!s->second.started && !wanted(s->second)) ? slots.erase(s) : std::next(s);
    if (slots.size() >= capacity) return Ask::Busy;
    Slot& n = slots[key];
    n.asked = n.since = now;
    return Ask::Start;
  }
  // The worker's next decode: of the queued requests still wanted, the newest (the latest first ask; a camera move
  // that walks one groom through LOD 0, 2, 1 decodes 1, not all three). Queued requests nobody asks for any more are
  // forgotten (asked again later: a new Start). false: nothing to decode.
  bool take(Key* key) {
    bool found = false;
    uint64_t best = 0;
    for (auto s = slots.begin(); s != slots.end();) {
      if (s->second.started) { ++s; continue; }
      if (!wanted(s->second)) { s = slots.erase(s); continue; }
      if (!found || s->second.since >= best) { found = true; best = s->second.since; *key = s->first; }
      ++s;
    }
    if (found) slots[*key].started = true;
    return found;
  }
  // The decode could not be queued: forget the request (asked again later).
  void cancel(const Key& key) { slots.erase(key); }
  void finish(const Key& key, bool ok, Value&& value) {
    auto it = slots.find(key);
    if (it == slots.end()) return;
    it->second.done = true;
    it->second.ok = ok;
    if (ok) it->second.value = std::move(value);
  }
};
}  // namespace bg3::hair_groom
