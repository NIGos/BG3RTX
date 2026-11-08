// rt_omm_dyn: memory of the OMM backend's refusals (layer/bvhsplit.cpp, docs/P4_OMM.md 11.2c).
// The backend (layer/omm_backend.cpp) refuses a micro-map once its live bytes plus the new micro-map's storage and
// scratch pass its fixed budget. Micro-maps live until teardown, so once the budget is full every new identity was
// retried every frame: three dedicated VkDeviceMemory buffers, a bake dispatch and three fault-range log lines per
// attempt, then the refusal (2026-11-08 session: 36 refusals per build, scene wait 2 -> 18 ms).
// A refusal of `tris` triangles at live bytes L proves that every bake of >= tris triangles is refused while the live
// bytes stay >= L (storage and scratch grow with the triangle count at one format and level). Those attempts are
// skipped until the live bytes drop below L or `retry_frames` pass (other refusal causes: allocation failure).
// Smaller bakes are still tried. retry_frames 0: no memory (every bake is attempted, the old behaviour).
#pragma once
#include <cstdint>

namespace bg3::omm_dyn {

struct Refusal {
  uint32_t min_tris = 0;     // smallest refused bake; 0: none remembered
  uint64_t frame = 0;        // frame of the latest refusal
  uint64_t live = 0;         // backend live bytes at the first refusal of this memory

  // Clears the memory when it no longer proves anything (live bytes dropped, retry period passed).
  void refresh(uint64_t now, uint64_t live_bytes, uint32_t retry_frames) {
    if (!min_tris) return;
    if (!retry_frames || live_bytes < live || now >= frame + retry_frames) *this = Refusal{};
  }
  // False: a bake of `tris` triangles would be refused like a remembered one (skip it, count it deferred).
  bool allow(uint32_t tris) const { return !min_tris || tris < min_tris; }
  void refused(uint32_t tris, uint64_t now, uint64_t live_bytes, uint32_t retry_frames) {
    if (!retry_frames || !tris) return;
    if (!min_tris) live = live_bytes;
    else if (live_bytes < live) live = live_bytes;
    min_tris = min_tris ? (tris < min_tris ? tris : min_tris) : tris;
    frame = now;
  }
};

}  // namespace bg3::omm_dyn
