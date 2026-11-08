// rt_omm_dyn_evict_frames (layer/bvhsplit.cpp, docs/P4_OMM.md 11.2i): LRU eviction of dynamic micro-maps.
// The cache never evicted, so once the backend budget was full new foliage got no micro-maps (2026-11-08: 682
// micro-maps, every new identity refused). With N > 0 a ready micro-map that no draw matched for N frames and that
// no per-draw BLAS was built with (a slot still holding its ticket) is released, oldest first, at most `max` per
// build. The backend destroys it once the GPU finished every registered use (omm::release_ticket); a scene slot
// that still lists it declines the accelerated TLAS (register_ticket_use refuses an owner-released ticket) and
// traces the reference scene. Off by default: it changes which micro-maps exist.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace bg3::omm_dyn {

struct EvictCandidate {
  uint64_t key = 0;
  uint64_t last_use = 0;   // frame a draw last matched it (its bake frame until then)
  bool ready = false;      // its bake ran (unready entries are discarded by their own path)
  bool referenced = false; // a per-draw BLAS was built with it
};

inline bool evictable(const EvictCandidate& c, uint64_t now, uint32_t frames) {
  return frames && c.ready && !c.referenced && now >= c.last_use && now - c.last_use >= frames;
}

// Keys to evict this build: evictable ones, least recently used first, at most `max` (0: none).
inline std::vector<uint64_t> pick_evictions(std::vector<EvictCandidate> all, uint64_t now, uint32_t frames, uint32_t max) {
  std::vector<uint64_t> out;
  if (!frames || !max) return out;
  all.erase(std::remove_if(all.begin(), all.end(), [&](const EvictCandidate& c) { return !evictable(c, now, frames); }),
            all.end());
  std::sort(all.begin(), all.end(), [](const EvictCandidate& a, const EvictCandidate& b) {
    return a.last_use != b.last_use ? a.last_use < b.last_use : a.key < b.key;
  });
  for (const EvictCandidate& c : all) {
    if (out.size() >= max) break;
    out.push_back(c.key);
  }
  return out;
}

}  // namespace bg3::omm_dyn
