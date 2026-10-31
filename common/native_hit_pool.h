#pragma once
// Persistent material records of BVH mesh-pool instances (rt_hit_material; PLAN_M4
// "Hit material on pool instances"). Pool meshes are class A instance-matrix draws
// (TRACK_D_SCENE_BUILD 1): their depth prepass is not captured once cached, so they never
// get a per-frame material row. Their G-buffer draw (Base_VT_STI_DEF for the static VT
// family) is observed instead, joined to the pool mesh by its geometry (index range and
// position stream, not the VS: the depth and G-buffer VSs differ), and its material
// bindings are kept in a record that outlives the frame.
// Pure policy on plain numbers; the layer (hit_material.cpp, bvhsplit.cpp) keeps every
// Vulkan object. tests/native_hit_pool_test.cpp pins it.
#include "native_hit_qualify.h"
#include "native_vt_surface.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
namespace bg3::native_hit::pool {
// The pool pair: a deferred recipe of native_vt_surface.h that is not the audited per-draw
// pair (3812bc98/8995da1e, class B, never pooled). Today: VS 2d2ac3c3, FS 14678345.
inline const native_vt::Recipe* pool_recipe(uint64_t vs, uint64_t fs) {
  const auto* r = native_vt::recipe(fs);
  return r && r->vs == vs && !(vs == 0x3812bc980438747full && fs == 0x8995da1e4060f53bull) ? r : nullptr;
}
// Engine UBO (set 1 binding 0) bytes the recipe reads: the UV transform and the tile-set index.
inline uint32_t engine_bytes(const native_vt::Recipe& r) {
  return std::max(r.transform_offset + 16u, r.tileset_index_offset + 4u);
}
// Pipeline state of the G-buffer draw. Same rule as exact_material_pipeline without the
// XFB/basis terms (the pool record has no per-frame capture).
inline bool pool_material_pipeline(const MaterialPipelineFacts& p) {
  return p.material_state && p.depth_test == 1 && p.blend_mask == 0 &&
         (p.color_mask & kColorMaskRequired) == kColorMaskRequired && p.triangle_list;
}
// What a draw's per-vertex inputs read, independent of the VS. Indexed draws are keyed by
// the index range (one LOD of one mesh); the position stream is checked separately.
struct Geometry {
  bool indexed = false;
  uint32_t count = 0, first = 0;
  int32_t vertex_offset = 0;
  uint64_t ib = 0, ib_offset = 0;
  uint32_t ib_type = 0;
  uint64_t position_buffer = 0, position_offset = 0;  // bound buffer, binding + attribute offset
};
inline uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
inline uint64_t geometry_key(const Geometry& g) {
  uint64_t h = mix(mix(mix(0xcbf29ce484222325ull, g.indexed), g.count), g.first);
  h = mix(h, uint64_t(int64_t(g.vertex_offset)));
  h = g.indexed ? mix(mix(mix(h, g.ib), g.ib_offset), g.ib_type) : mix(mix(h, g.position_buffer), g.position_offset);
  return h ? h : 1;
}
inline uint64_t position_key(const Geometry& g) {
  return g.position_buffer ? mix(mix(0x84222325cbf29ce4ull, g.position_buffer), g.position_offset) | 1u : 0;
}
// Outcome of one pool draw's lookup. Counted per instance in "[hit material pool]".
enum class Status : uint8_t {
  Qualified,   // record with current bindings, joined to this mesh
  NoDeferred,  // no G-buffer draw of this geometry seen
  Family,      // its G-buffer draw is another native pair
  Pipeline,    // pair matches, pipeline state or draw kind does not
  Binding,     // bindings or engine parameters not captured
  Position,    // same index range, different (or unknown) position stream
  Ambiguous,   // two different materials/pairs for one geometry in one of the last two observed frames
  Stale,       // a view, image or sampler of the record is no longer current
  Replaced,    // the pool mesh changed since the record was observed
  Count
};
inline constexpr const char* kStatusName[] = {"qualified", "no G-buffer draw", "other pair", "pipeline", "bindings",
                                              "position stream", "ambiguous", "stale views", "mesh replaced"};
static_assert(sizeof(kStatusName) / sizeof(kStatusName[0]) == size_t(Status::Count));
struct Observation {
  uint64_t identity = 0;  // material identity (views, sampler, parameters) or the pair for Family
  uint64_t position = 0;  // position_key
  Status status = Status::Qualified;  // Qualified, Family, Pipeline or Binding
};
template <class Payload> struct Record {
  uint64_t frame = 0, prev_frame = 0;
  bool conflict = false, prev_conflict = false;
  Observation seen{};
  uint64_t mesh = 0;           // pool mesh generation bound at the first lookup
  uint64_t rebound = 0;        // the mesh changed at this frame: needs an observation at/after it
  bool stale = false;          // counted once per invalidation
  Payload payload{};
};
struct Counters {
  uint64_t draws[size_t(Status::Count)]{}, instances[size_t(Status::Count)]{};
  void clear() { *this = {}; }
};
struct Totals { uint64_t stored = 0, refreshed = 0, conflicts = 0, evicted = 0, replaced = 0, stale = 0, overflow = 0; };
template <class Payload> class Table {
 public:
  static constexpr size_t kMaxRecords = 16384;
  enum class Store { Fresh, Keep, Full };
  // One G-buffer draw. Fresh: the caller stores its payload (first observation this frame).
  Store observe(uint64_t key, uint64_t frame, const Observation& o) {
    auto it = records_.find(key);
    if (it == records_.end()) {
      if (records_.size() >= kMaxRecords) { ++totals_.overflow; return Store::Full; }
      it = records_.emplace(key, Record<Payload>{}).first;
      ++totals_.stored;
    } else if (it->second.frame == frame) {
      auto& r = it->second;
      const bool same = r.seen.identity == o.identity && r.seen.position == o.position && r.seen.status == o.status;
      if (!same && !r.conflict) { r.conflict = true; ++totals_.conflicts; }
      return Store::Keep;
    } else ++totals_.refreshed;
    auto& r = it->second;
    if (r.frame) { r.prev_frame = r.frame; r.prev_conflict = r.conflict; }
    r.frame = frame; r.conflict = false; r.seen = o; r.stale = false;
    return Store::Fresh;
  }
  // Capturing a draw's bindings costs CPU, so a qualified, current record is refreshed only
  // every kRefreshFrames. In a refresh frame every draw of the geometry is captured, so two
  // materials on one geometry are still found (then Ambiguous).
  static constexpr uint64_t kRefreshFrames = 30;
  bool capture_due(uint64_t key, uint64_t frame) const {
    auto it = records_.find(key);
    if (it == records_.end()) return true;
    const auto& r = it->second;
    return r.frame == frame || r.frame + kRefreshFrames <= frame || r.stale || r.seen.status != Status::Qualified ||
           r.conflict || r.prev_conflict || (r.rebound && r.frame < r.rebound);
  }
  Record<Payload>* find(uint64_t key) { auto it = records_.find(key); return it == records_.end() ? nullptr : &it->second; }
  // One pool draw (instances TLAS instances of mesh generation `mesh`). `current` tells whether
  // the record's resources are still alive; it is only called for an otherwise qualified record.
  template <class Current>
  Status lookup(uint64_t key, uint64_t mesh, uint64_t position, uint64_t frame, uint32_t instances, Current&& current) {
    Status s = classify(key, mesh, position, frame, current);
    counters_.draws[size_t(s)] += 1;
    counters_.instances[size_t(s)] += instances;
    return s;
  }
  // The pool mesh with this geometry was evicted: its record no longer describes a mesh.
  void evicted(uint64_t key) { if (records_.erase(key)) ++totals_.evicted; }
  const Counters& counters() const { return counters_; }
  const Totals& totals() const { return totals_; }
  size_t size() const { return records_.size(); }
  void end_frame() { counters_.clear(); }
  void clear() { records_.clear(); counters_.clear(); totals_ = {}; }

 private:
  template <class Current>
  Status classify(uint64_t key, uint64_t mesh, uint64_t position, uint64_t frame, Current& current) {
    auto* r = find(key);
    if (!r) return Status::NoDeferred;
    if (r->mesh != mesh) {
      if (r->mesh) { r->rebound = frame; ++totals_.replaced; }
      r->mesh = mesh;
    }
    if (r->rebound && r->frame < r->rebound) return Status::Replaced;
    if (r->conflict || r->prev_conflict) return Status::Ambiguous;
    if (r->seen.status != Status::Qualified) return r->seen.status;
    if (!position || r->seen.position != position) return Status::Position;
    if (!current(r->payload)) {
      if (!r->stale) { r->stale = true; ++totals_.stale; }
      return Status::Stale;
    }
    return Status::Qualified;
  }
  std::unordered_map<uint64_t, Record<Payload>> records_;
  Counters counters_;
  Totals totals_;
};
}  // namespace bg3::native_hit::pool
