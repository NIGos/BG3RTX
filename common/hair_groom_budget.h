#pragma once
// rt_hair_groom mesh-pool budget (layer/bvhsplit.cpp groom_*): groom arrays share the mesh pool with the cached
// meshes, so they get at most a quarter of its capacity, and unused LODs are evicted oldest first to make room.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bg3::hair_groom {

// A quarter of the mesh pool's capacity (the larger of the configured cap and the device-budget cap is never
// used: the pool itself is limited by the smaller one). The rest stays for the cached meshes, which filled the
// pool to 2047 MB in Test Dialogue3 without any groom.
inline uint64_t pool_budget(uint64_t pool_capacity) { return pool_capacity / 4; }

struct Resident {
  uint64_t key = 0;        // the LOD (groom id, LOD index) or a slot's points
  uint64_t bytes = 0;
  uint64_t last_used = 0;  // frame
  bool pinned = false;     // used by this frame's instances: never evicted
};

// The residents to evict (oldest last use first, unpinned, unused for at least min_age frames) so that
// used - freed + need <= budget. False when even evicting every candidate cannot make room: the caller then
// keeps the card path for that draw and evicts nothing.
inline bool plan_evictions(std::vector<Resident> residents, uint64_t used, uint64_t need, uint64_t budget,
                           uint64_t now, uint64_t min_age, std::vector<uint64_t>& evict) {
  evict.clear();
  if (need > budget) return false;
  if (used + need <= budget) return true;
  std::sort(residents.begin(), residents.end(), [](const Resident& a, const Resident& b) { return a.last_used < b.last_used; });
  uint64_t freed = 0;
  for (const Resident& r : residents) {
    if (used - freed + need <= budget) break;
    if (r.pinned || now < r.last_used + min_age) continue;
    evict.push_back(r.key);
    freed += r.bytes;
  }
  if (used - freed + need <= budget) return true;
  evict.clear();
  return false;
}

// rt_hair_groom_lod_keep (docs/RTX_HAIR_PRIMARY.md 5i): the LOD a groom slot uses this frame. want: its screen-size
// (or crowd) LOD; built: the LOD its BLAS holds (-1: none); want_ready / built_ready: their arrays resident in the pool.
// Without the key a wanted LOD still uploading drops the slot (no groom that frame: its draws go back to cards and its
// next publishes differ, the flash while hair or camera move across a LOD threshold). With it the built LOD stays until
// the wanted one is resident. -1: no groom this frame.
inline int lod_this_frame(int want, int built, bool want_ready, bool built_ready, bool keep) {
  if (want_ready) return want;
  if (keep && built >= 0 && built != want && built_ready) return built;
  return -1;
}

// The screen-size LOD vote of a groom slot (layer/bvhsplit.cpp groom_pick_lod): a change needs kLodVoteFrames consecutive
// frames wanting it (no flicker at a threshold). lod: the slot's LOD (-1: none yet, the wanted one is taken). first_measure
// (rt_hair_warmup, docs/RTX_HAIR_PRIMARY.md 5o): this frame holds the slot's first screen-size measurement. Its LOD until
// then is the no-bounds default (LOD 0, the bounds come from the groom's own points), not a choice at a threshold, so the
// measured LOD is taken at once instead of after a whole vote (a second upload and full build ~30-50 frames into a load).
constexpr int kLodVoteFrames = 30;
inline int lod_vote(int lod, int want, int& votes, bool first_measure) {
  if (lod < 0) return want;
  if (want == lod) { votes = 0; return lod; }
  if (first_measure) { votes = 0; return want; }
  return ++votes >= kLodVoteFrames ? (votes = 0, want) : lod;
}

// rt_hair_pixel_lod (docs/RTX_HAIR_PRIMARY.md 5v): the camera strand trace's keep for one groom, as steps below the
// groom's own LOD keep: level j traces the strands kept at lod_keep / 2^j with radii x 2^j (a subset of the groom's
// strands: the keep hash is nested). Level 0 is today's groom. The finest level whose strands are at least k render px
// wide: d0_px / (lod_keep / 2^j) >= k (d0_px: the LOD 0 median strand diameter in render px at the groom's distance),
// never coarser than 1/32 of the full groom (the decode-time ladder, hair_groom_v3.h kV3ExtraKeeps). A head under
// kPixelLodMinHeadPx render px keeps level 0: the study (docs/progress/claude-hair-pixel-lod-study.md) measured silhouette
// growth of +12 to +23% at 40 px and +3 to +6% at 80 px for k 1.0; 60 is the midpoint of the failing and the passing
// size. Hysteresis: a quarter octave of the diameter around the level's bounds (prev: the level the groom traces, -1
// none); the 30-frame lod_vote runs on the result in the layer.
constexpr float kPixelLodMinHeadPx = 60.0f, kPixelLodMinKeep = 1.0f / 32, kPixelLodHysteresis = 0.25f;
constexpr int kPixelLodMaxLevel = 5;   // 1 -> 1/32
inline int pixel_lod_level(float d0_px, float head_px, float lod_keep, float k, int prev) {
  if (!(d0_px > 0) || !(k > 0) || !(lod_keep > 0) || !(head_px >= kPixelLodMinHeadPx)) return 0;
  // x: the level that just reaches k, as a real number: lod_keep / 2^x = d0_px / k.
  const float x = std::log2(lod_keep * k / d0_px);
  int top = 0;   // the deepest level the ladder allows for this groom
  while (top < kPixelLodMaxLevel && lod_keep / float(1 << (top + 1)) >= kPixelLodMinKeep * 0.999f) ++top;
  auto clampl = [&](int j) { return j < 0 ? 0 : j > top ? top : j; };
  const int want = clampl(int(std::ceil(x - 1e-6f)));
  if (prev < 0 || prev > top) return want;
  // Keep the previous level while x stays within a quarter octave of the interval (prev - 1, prev] it was picked for.
  if (x > float(prev - 1) - kPixelLodHysteresis && x <= float(prev) + kPixelLodHysteresis) return clampl(prev);
  return want;
}
inline float pixel_lod_keep(float lod_keep, int level) { return lod_keep / float(1 << (level < 0 ? 0 : level)); }

// Crowd plan (rt_hair_groom_crowd_ksegments, docs/RTX_HAIR_PRIMARY.md increment 5): the groom instances of a frame
// share a budget of LSS segments (refit time and BLAS memory scale with segments; trace time scales with the hair
// pixels/rays and only logarithmically with segments). Largest on screen first, each at its screen-size LOD or the
// first coarser LOD that still fits; one that fits at no LOD keeps its cards (-1). budget 0: no plan (every
// instance its screen-size LOD).
struct CrowdCandidate {
  float size = 0;             // screen-size measure (bounds diagonal / distance, as groom_pick_lod)
  int want = 0;               // its screen-size LOD
  uint32_t segments[4] = {};  // segments per LOD (0: no such LOD)
  int lods = 0;
};
inline std::vector<int> plan_crowd(const std::vector<CrowdCandidate>& c, uint64_t budget_segments) {
  std::vector<int> lod(c.size(), -1);
  std::vector<size_t> order(c.size());
  for (size_t i = 0; i < c.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return c[a].size > c[b].size; });
  uint64_t left = budget_segments;
  for (size_t i : order) {
    const CrowdCandidate& k = c[i];
    if (budget_segments == 0) { lod[i] = (std::min)((std::max)(k.want, 0), (std::max)(k.lods - 1, 0)); continue; }
    for (int l = (std::max)(k.want, 0); l < k.lods && l < 4; ++l) {
      if (k.segments[l] && k.segments[l] <= left) {
        lod[i] = l;
        left -= k.segments[l];
        break;
      }
    }
  }
  return lod;
}

}  // namespace bg3::hair_groom
