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
template<class Key, class Value> struct DecodeSlots {
  enum class Ask { Start, Pending, Busy, Ready, Failed };
  struct Slot { bool done = false, ok = false; uint64_t asked = 0; Value value; };
  std::map<Key, Slot> slots;
  size_t capacity = 4;
  uint64_t stale = 120;
  // Start: the caller queues the decode of `key` now. Ready: `*out` has the value and the slot is gone.
  Ask ask(const Key& key, uint64_t now, Value* out) {
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
        s = s->second.done && now >= s->second.asked && now - s->second.asked > stale ? slots.erase(s) : std::next(s);
    if (slots.size() >= capacity) return Ask::Busy;
    slots[key].asked = now;
    return Ask::Start;
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
