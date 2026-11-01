#pragma once
// Reuse of the hit-material provider's transient GPU buffers (rt_hit_material_recycle=1,
// docs/TRACK_D_SCENE_BUILD.md "Hit-material CPU on the scene commit path").
//
// Every frame the provider made a deferred packet (rows + up to 4 x 32 MB XFB chunks), a query
// (rows, up to 256 MB of copies, pool UBO, remap counters) and up to 8 UV/basis jobs, and freed
// the retired ones of a few frames earlier: vkAllocateMemory/vkFreeMemory of hundreds of MB per
// frame on the G-buffer and lighting recording threads, under the provider's locks that the
// scene prepass hooks and the scene commit also take. A buffer that retired (no GPU user left:
// the same point where it was freed) goes to this pool instead and the next request of the same
// rounded size, usage and memory kind takes it. Its contents are undefined, as a fresh
// allocation's are: every user writes what it reads (rows are cleared and get their frame
// header each use), so rows of another frame are still rejected by the header check.
// Pure CPU bookkeeping; the owner creates and destroys the actual buffers and locks the pool.
#include <cstdint>
#include <vector>
namespace bg3::native_hit::recycle {
struct Key {
  uint64_t bytes{};
  uint32_t usage{};
  bool host{};
  bool operator==(const Key&) const = default;
};
// Few distinct sizes, so a released buffer fits the next request of a similar size: at least
// 64 KiB, powers of two up to 1 MiB, then multiples of a sixteenth of the enclosing power of
// two P (P < 2 x request, so at most 12.5% above it; 8 sizes per octave). 0 stays 0 (no buffer).
inline uint64_t round_bytes(uint64_t bytes) {
  constexpr uint64_t kMin = 64ull << 10, kPow2Max = 1ull << 20;
  if (!bytes) return 0;
  if (bytes <= kMin) return kMin;
  uint64_t p = 1;
  while (p < bytes && p < (1ull << 62)) p <<= 1;
  if (p <= kPow2Max) return p;
  const uint64_t step = p >> 4;
  return (bytes + step - 1) / step * step;
}
// The most a pool keeps: one frame's largest transient set (deferred 4 x 32 MB + query 256 MB
// = 384 MB) plus the 12.5% rounding, rounded up to 512 MB. In steady play a released buffer is
// taken again within the few frames in flight; one idle for kMaxIdleFrames (one 120-frame stats
// window) belongs to work that stopped (key off, menus) and is freed.
inline constexpr uint64_t kCapBytes = 512ull << 20;
inline constexpr uint64_t kMaxIdleFrames = 120;
template <class H>
class Pool {
 public:
  explicit Pool(uint64_t capBytes = kCapBytes, uint64_t maxIdle = kMaxIdleFrames) : cap_(capBytes), idle_(maxIdle) {}
  // A retained buffer of exactly this key (the most recently released one), or false.
  bool take(const Key& k, H& out) {
    for (size_t i = entries_.size(); i-- > 0;) {
      if (!(entries_[i].key == k)) continue;
      out = entries_[i].handle;
      retained_ -= entries_[i].key.bytes;
      entries_.erase(entries_.begin() + ptrdiff_t(i));
      ++reused_;
      return true;
    }
    return false;
  }
  // rt_hit_material_lean_hooks: the smallest retained buffer of this usage and memory kind that holds
  // k.bytes and is at most twice it (users give explicit ranges; a larger buffer is never read past
  // them). Sizes that move every frame (the query copies while the camera turns) then reuse instead of
  // allocating a new rounded size and evicting an old one (sw3 2026-11-01: 1.2-1.8 allocations and
  // frees per frame, 31-40 MB, with exact keys).
  bool take_fit(const Key& k, H& out) {
    size_t best = entries_.size();
    for (size_t i = 0; i < entries_.size(); ++i) {
      const Key& e = entries_[i].key;
      if (e.usage != k.usage || e.host != k.host || e.bytes < k.bytes || e.bytes > 2 * k.bytes) continue;
      if (best == entries_.size() || e.bytes < entries_[best].key.bytes) best = i;
    }
    if (best == entries_.size()) return false;
    out = entries_[best].handle;
    retained_ -= entries_[best].key.bytes;
    entries_.erase(entries_.begin() + ptrdiff_t(best));
    ++reused_;
    return true;
  }
  // Keeps a retired buffer. Over the cap the oldest ones go to `evict` (the caller frees them);
  // one larger than the whole cap is evicted at once.
  void give(const Key& k, const H& h, uint64_t frame, std::vector<H>& evict) {
    if (k.bytes > cap_) { evict.push_back(h); return; }
    entries_.push_back({k, h, frame});
    retained_ += k.bytes;
    while (retained_ > cap_ && !entries_.empty()) {
      retained_ -= entries_.front().key.bytes;
      evict.push_back(entries_.front().handle);
      entries_.erase(entries_.begin());
    }
  }
  // Buffers released more than maxIdle frames before `frame` go to `evict`.
  void trim(uint64_t frame, std::vector<H>& evict) {
    size_t w = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (frame > entries_[i].frame + idle_) { retained_ -= entries_[i].key.bytes; evict.push_back(entries_[i].handle); continue; }
      entries_[w++] = entries_[i];
    }
    entries_.resize(w);
  }
  void clear(std::vector<H>& evict) {
    for (const auto& e : entries_) evict.push_back(e.handle);
    entries_.clear();
    retained_ = 0;
  }
  uint64_t retained() const { return retained_; }
  size_t size() const { return entries_.size(); }
  uint64_t reused() const { return reused_; }

 private:
  struct Entry { Key key; H handle; uint64_t frame; };
  std::vector<Entry> entries_;
  uint64_t cap_, idle_, retained_{}, reused_{};
};
}  // namespace bg3::native_hit::recycle
