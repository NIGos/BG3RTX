// Light BVH ("light tree") for many-light importance sampling: the CPU builder and the
// reference sampler (docs/DESIGN_SOTA_NEXT.md, item 1, "secondary vertices").
//
// Not wired into the layer or the core yet. It is the oracle the GPU builder and the HLSL
// sampler (planned: shaders/rt/light_tree.hlsli) are checked against: same node layout
// (LightTreeNodeGpu, 64 bytes), same importance function, same traversal. It can also build
// the tree of the runtime lights of the asset DB / Script Extender bridge on the CPU
// (core/ls_lights.cpp refreshes them every 8 frames, at most 1024 lights).
//
// Method: a binary BVH over lights whose nodes carry a bounding box, a bounding cone of
// emitter normals (theta_o) plus the emission spread (theta_e), the summed power and, for
// BG3, the largest light range (the game's lights have a finite radius: no light reaches
// beyond it). Built top-down with the binned surface-area-orientation heuristic (SAOH) of
// Conty Estevez & Kulla, "Importance Sampling of Many Lights with Adaptive Tree Splitting"
// (HPG 2018). Sampled by a single stochastic descent: at each node the two children are
// chosen in proportion to a conservative importance bound, as in Moreau, Pharr & Clarberg,
// "Dynamic Many-Light Sampling for Real-Time Ray Tracing" (HPG 2019) and pbrt-v4's
// BVHLightSampler. This is an independent implementation of those published methods.
//
// Unbiasedness requires: importance(node) > 0 wherever any light below it can contribute.
// Every zero below is a conservative test (range, emission cone, surface hemisphere), and
// tests/light_tree_test.cpp checks it against the exact contribution support.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bg3::lt {

constexpr float kPi = 3.14159265358979f;

struct V3 {
  float x = 0, y = 0, z = 0;
};
inline V3 v3(float x, float y, float z) { return V3{x, y, z}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) {
  float l = length(a);
  return l > 0 ? a * (1.0f / l) : V3{0, 0, 1};
}
inline V3 vmin(V3 a, V3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline V3 vmax(V3 a, V3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline float comp(V3 a, int k) { return k == 0 ? a.x : k == 1 ? a.y : a.z; }
inline float clamp1(float c) { return std::min(1.0f, std::max(-1.0f, c)); }
inline float safe_sqrt(float x) { return std::sqrt(std::max(0.0f, x)); }

// One emitter, in any consistent world space (the GPU version uses absolute world, R4).
//   point light:            cos_o = -1 (normals in every direction), cos_e = 0
//   spot light:             axis = spot direction, cos_o = 1, cos_e = cos(outer angle)
//   one-sided emitter (tri): axis = normal, cos_o = 1, cos_e = 0
//   unknown spot direction:  treat as a point light (conservative)
struct Light {
  V3 pos;                 // centre
  float extent = 0;       // half-size of its box (an emissive triangle cluster: its bounding half-size)
  V3 axis{0, 0, 1};
  float cos_o = -1;       // bounding cone of the emitter normals (cos theta_o)
  float cos_e = 0;        // emission spread beyond the normal cone (cos theta_e)
  float power = 0;        // >= 0; 0 lights are never sampled
  float range = 1e30f;    // no contribution beyond this distance from its box (BG3: the light radius)
  bool two_sided = false;
  uint32_t id = 0;        // caller's index (light array slot, record, emissive cluster)
};

struct Cone {
  V3 w{0, 0, 1};
  float cos_t = 1;   // cos of the half-angle; -1 = the whole sphere
  bool empty = true;
};

inline V3 rotate(V3 v, V3 axis, float angle) {   // Rodrigues, axis normalised
  float c = std::cos(angle), s = std::sin(angle);
  return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1 - c));
}

// Smallest-ish cone holding both (the classic union of bounding cones).
inline Cone cone_union(const Cone& a, const Cone& b) {
  if (a.empty) return b;
  if (b.empty) return a;
  float ta = std::acos(clamp1(a.cos_t)), tb = std::acos(clamp1(b.cos_t));
  float td = std::acos(clamp1(dot(a.w, b.w)));
  if (std::min(td + tb, kPi) <= ta) return a;
  if (std::min(td + ta, kPi) <= tb) return b;
  float to = 0.5f * (ta + td + tb);
  Cone r;
  r.empty = false;
  if (to >= kPi) {
    r.cos_t = -1;
    r.w = a.w;
    return r;
  }
  V3 ax = cross(a.w, b.w);
  if (dot(ax, ax) < 1e-12f) {   // opposite (or equal, handled above) axes
    r.cos_t = -1;
    r.w = a.w;
    return r;
  }
  r.w = normalize(rotate(a.w, normalize(ax), to - ta));
  r.cos_t = std::cos(to);
  return r;
}

struct Bounds {
  V3 bmin{1e30f, 1e30f, 1e30f}, bmax{-1e30f, -1e30f, -1e30f};
  Cone normals;
  float cos_e = 1;   // max spread (min cosine)
  float power = 0;
  float range = 0;
  bool two_sided = false;
  bool valid() const { return bmin.x <= bmax.x; }
};

inline Bounds bounds_of(const Light& l) {
  Bounds b;
  V3 e{l.extent, l.extent, l.extent};
  b.bmin = l.pos - e;
  b.bmax = l.pos + e;
  b.normals.w = normalize(l.axis);
  b.normals.cos_t = l.cos_o;
  b.normals.empty = false;
  b.cos_e = l.cos_e;
  b.power = l.power;
  b.range = l.range;
  b.two_sided = l.two_sided;
  return b;
}

inline Bounds bounds_union(const Bounds& a, const Bounds& b) {
  if (!a.valid()) return b;
  if (!b.valid()) return a;
  Bounds r;
  r.bmin = vmin(a.bmin, b.bmin);
  r.bmax = vmax(a.bmax, b.bmax);
  r.normals = cone_union(a.normals, b.normals);
  r.cos_e = std::min(a.cos_e, b.cos_e);
  r.power = a.power + b.power;
  r.range = std::max(a.range, b.range);
  r.two_sided = a.two_sided || b.two_sided;
  return r;
}

// cos(max(0, a - b)) from the cosines and sines of a and b (angles in [0, pi]).
inline float cos_sub_clamped(float sin_a, float cos_a, float sin_b, float cos_b) {
  if (cos_a > cos_b) return 1;   // a < b
  return cos_a * cos_b + sin_a * sin_b;
}
inline float sin_sub_clamped(float sin_a, float cos_a, float sin_b, float cos_b) {
  if (cos_a > cos_b) return 0;
  return sin_a * cos_b - cos_a * sin_b;
}

// Conservative importance of a node for a receiver at p with normal n (n = 0: no
// hemisphere test, e.g. two-sided or translucent receivers). 0 only where no light under
// the node can contribute: outside every range, outside every emission cone, or entirely
// below the receiver's hemisphere.
inline float importance(const Bounds& b, V3 p, V3 n) {
  if (!b.valid() || b.power <= 0) return 0;
  // Range: the distance from p to the box exceeds the largest light range.
  V3 q = vmin(vmax(p, b.bmin), b.bmax);
  if (length(p - q) > b.range) return 0;
  V3 c = (b.bmin + b.bmax) * 0.5f;
  float r = 0.5f * length(b.bmax - b.bmin);   // bounding sphere of the box
  V3 pc = p - c;
  float d2 = dot(pc, pc);
  // Directions from the box's bounding sphere to p lie within theta_b of the centre direction.
  float cos_b = -1, sin_b = 0;
  if (d2 > r * r) {
    float s2 = r * r / d2;
    cos_b = safe_sqrt(1 - s2);
    sin_b = std::sqrt(s2);
  }
  V3 wi = d2 > 0 ? pc * (1.0f / std::sqrt(d2)) : V3{0, 0, 1};   // from the lights to p
  // Emission: the angle between p's direction and the normal cone, reduced by the cone's
  // half-angle theta_o and by theta_b, must stay within theta_e.
  float cos_w = dot(b.normals.w, wi);
  if (b.two_sided) cos_w = std::abs(cos_w);
  float sin_w = safe_sqrt(1 - cos_w * cos_w);
  float cos_o = b.normals.cos_t, sin_o = safe_sqrt(1 - cos_o * cos_o);
  float cos_x = cos_sub_clamped(sin_w, cos_w, sin_o, cos_o);
  float sin_x = sin_sub_clamped(sin_w, cos_w, sin_o, cos_o);
  float cos_p = cos_sub_clamped(sin_x, cos_x, sin_b, cos_b);
  if (cos_p <= b.cos_e) return 0;
  float imp = b.power * cos_p / std::max(d2, r * r + 1e-8f);
  // Receiver hemisphere: the light directions lie within theta_b of -wi.
  if (dot(n, n) > 0) {
    float cos_i = -dot(normalize(n), wi);
    float sin_i = safe_sqrt(1 - cos_i * cos_i);
    float cos_pi = cos_sub_clamped(sin_i, cos_i, sin_b, cos_b);
    if (cos_pi <= 0) return 0;
    imp *= cos_pi;
  }
  return std::max(imp, 0.0f);
}

// Orientation measure of SAOH (Conty Estevez & Kulla 2018, eq. 1).
inline float orientation_measure(float cos_o, float cos_e) {
  float to = std::acos(clamp1(cos_o)), te = std::acos(clamp1(cos_e));
  float tw = std::min(to + te, kPi);
  float so = std::sin(to);
  return 2 * kPi * (1 - std::cos(to)) +
         kPi / 2 * (2 * tw * so - std::cos(to - 2 * tw) - 2 * to * so + std::cos(to));
}

inline float surface_area(const Bounds& b) {
  V3 d = b.bmax - b.bmin;
  return 2 * (d.x * d.y + d.y * d.z + d.z * d.x);
}

// GPU node, 64 bytes (planned shaders/rt/light_tree.hlsli; std430 compatible).
//   interior: child 0 is the next node, `link` the index of child 1;
//   leaf:     `link` is the index into the light list (Light::id is the caller's id).
struct LightTreeNodeGpu {
  float bmin[3];
  float power;
  float bmax[3];
  float range;
  float axis[3];
  float cos_o;
  float cos_e;
  uint32_t link;
  uint32_t flags;   // bit 0 leaf, bit 1 two-sided
  uint32_t pad;
};
static_assert(sizeof(LightTreeNodeGpu) == 64, "LightTreeNodeGpu is 64 bytes");
constexpr uint32_t kNodeLeaf = 1u, kNodeTwoSided = 2u;

class LightTree {
 public:
  // Builds over the lights with power > 0 (the others are left out: they can never be
  // sampled, and pmf() returns 0 for them).
  void build(const std::vector<Light>& lights, int bins = 12) {
    lights_.clear();
    nodes_.clear();
    trail_.clear();
    bins_ = std::max(bins, 2);
    for (const Light& l : lights)
      if (l.power > 0 && std::isfinite(l.power)) lights_.push_back(l);
    trail_.assign(lights_.size(), {0, 0});
    slot_of_id_.clear();
    if (lights_.empty()) return;
    std::vector<uint32_t> idx(lights_.size());
    for (uint32_t i = 0; i < idx.size(); ++i) idx[i] = i;
    nodes_.reserve(2 * lights_.size());
    build_rec(idx, 0, uint32_t(idx.size()), 0, 0);
    for (uint32_t i = 0; i < lights_.size(); ++i) slot_of_id_.push_back({lights_[i].id, i});
    std::sort(slot_of_id_.begin(), slot_of_id_.end());
  }

  bool empty() const { return nodes_.empty(); }
  const std::vector<LightTreeNodeGpu>& nodes() const { return nodes_; }
  const std::vector<Light>& lights() const { return lights_; }   // leaf order (`link` indexes this)
  uint32_t depth() const {
    uint32_t d = 0;
    for (const auto& t : trail_) d = std::max(d, t.second);
    return d;
  }

  // Samples one light for a receiver at (p, n) with u in [0, 1). Returns the index into
  // lights() and its probability; false when no light can contribute.
  bool sample(V3 p, V3 n, float u, uint32_t* light, float* pmf) const {
    if (nodes_.empty()) return false;
    uint32_t i = 0;
    float prob = 1;
    if (importance(node_bounds(nodes_[0]), p, n) <= 0) return false;
    while (!(nodes_[i].flags & kNodeLeaf)) {
      uint32_t c0 = i + 1, c1 = nodes_[i].link;
      float i0 = importance(node_bounds(nodes_[c0]), p, n), i1 = importance(node_bounds(nodes_[c1]), p, n);
      if (i0 <= 0 && i1 <= 0) return false;
      float p0 = i0 / (i0 + i1);
      if (u < p0) {
        u = std::min(u / p0, 0x1.fffffep-1f);
        prob *= p0;
        i = c0;
      } else {
        u = std::min((u - p0) / (1 - p0), 0x1.fffffep-1f);
        prob *= 1 - p0;
        i = c1;
      }
    }
    *light = nodes_[i].link;
    *pmf = prob;
    return prob > 0;
  }

  // Probability that sample() returns lights()[index] for (p, n).
  float pmf(uint32_t index, V3 p, V3 n) const {
    if (index >= lights_.size() || nodes_.empty()) return 0;
    if (importance(node_bounds(nodes_[0]), p, n) <= 0) return 0;
    uint64_t bits = trail_[index].first;
    uint32_t i = 0;
    float prob = 1;
    for (uint32_t k = 0; k < trail_[index].second; ++k) {
      uint32_t c0 = i + 1, c1 = nodes_[i].link;
      float i0 = importance(node_bounds(nodes_[c0]), p, n), i1 = importance(node_bounds(nodes_[c1]), p, n);
      if (i0 + i1 <= 0) return 0;
      bool right = (bits >> k) & 1u;
      prob *= (right ? i1 : i0) / (i0 + i1);
      i = right ? c1 : c0;
    }
    return prob;
  }

  // Index into lights() of the caller's id, or UINT32_MAX.
  uint32_t index_of_id(uint32_t id) const {
    auto it = std::lower_bound(slot_of_id_.begin(), slot_of_id_.end(), std::pair<uint32_t, uint32_t>{id, 0});
    return it != slot_of_id_.end() && it->first == id ? it->second : UINT32_MAX;
  }

  static Bounds node_bounds(const LightTreeNodeGpu& n) {
    Bounds b;
    b.bmin = {n.bmin[0], n.bmin[1], n.bmin[2]};
    b.bmax = {n.bmax[0], n.bmax[1], n.bmax[2]};
    b.normals.w = {n.axis[0], n.axis[1], n.axis[2]};
    b.normals.cos_t = n.cos_o;
    b.normals.empty = false;
    b.cos_e = n.cos_e;
    b.power = n.power;
    b.range = n.range;
    b.two_sided = (n.flags & kNodeTwoSided) != 0;
    return b;
  }

 private:
  std::vector<Light> lights_;
  std::vector<LightTreeNodeGpu> nodes_;
  std::vector<std::pair<uint64_t, uint32_t>> trail_;   // per light: path bits (1 = child 1), depth
  std::vector<std::pair<uint32_t, uint32_t>> slot_of_id_;
  int bins_ = 12;

  static LightTreeNodeGpu to_node(const Bounds& b) {
    LightTreeNodeGpu n{};
    n.bmin[0] = b.bmin.x, n.bmin[1] = b.bmin.y, n.bmin[2] = b.bmin.z;
    n.bmax[0] = b.bmax.x, n.bmax[1] = b.bmax.y, n.bmax[2] = b.bmax.z;
    n.axis[0] = b.normals.w.x, n.axis[1] = b.normals.w.y, n.axis[2] = b.normals.w.z;
    n.cos_o = b.normals.cos_t;
    n.cos_e = b.cos_e;
    n.power = b.power;
    n.range = b.range;
    n.flags = b.two_sided ? kNodeTwoSided : 0u;
    return n;
  }

  float cost(const Bounds& b, float kr) const {
    return b.power * orientation_measure(b.normals.cos_t, b.cos_e) * std::max(surface_area(b), 1e-6f) * kr;
  }

  // Writes the subtree of idx[lo, hi) at nodes_.size(); `bits`/`depth` are the path to it.
  void build_rec(std::vector<uint32_t>& idx, uint32_t lo, uint32_t hi, uint64_t bits, uint32_t depth) {
    Bounds all;
    for (uint32_t k = lo; k < hi; ++k) all = bounds_union(all, bounds_of(lights_[idx[k]]));
    const uint32_t me = uint32_t(nodes_.size());
    nodes_.push_back(to_node(all));
    if (hi - lo == 1 || depth >= 63) {   // depth 63: the trail is 64 bits (never reached in practice)
      // (depth >= 63 with several lights would lose lights; the split below always halves at worst,
      // so 2^63 lights would be needed.)
      nodes_[me].flags |= kNodeLeaf;
      nodes_[me].link = idx[lo];
      trail_[idx[lo]] = {bits, depth};
      return;
    }
    // Binned SAOH over the centroids, every axis.
    V3 cmin{1e30f, 1e30f, 1e30f}, cmax{-1e30f, -1e30f, -1e30f};
    for (uint32_t k = lo; k < hi; ++k) cmin = vmin(cmin, lights_[idx[k]].pos), cmax = vmax(cmax, lights_[idx[k]].pos);
    V3 ext = all.bmax - all.bmin;
    float emax = std::max({ext.x, ext.y, ext.z});
    float best = 1e38f;
    int best_axis = -1, best_bin = -1;
    std::vector<Bounds> bin(bins_);
    std::vector<Bounds> right(bins_);
    for (int a = 0; a < 3; ++a) {
      float c0 = comp(cmin, a), c1 = comp(cmax, a);
      if (!(c1 > c0)) continue;
      for (auto& b : bin) b = Bounds{};
      for (uint32_t k = lo; k < hi; ++k) {
        int bi = std::min(bins_ - 1, int(bins_ * (comp(lights_[idx[k]].pos, a) - c0) / (c1 - c0)));
        bin[bi] = bounds_union(bin[bi], bounds_of(lights_[idx[k]]));
      }
      // Conty Estevez & Kulla's regularisation: prefer splitting the long axis.
      float kr = emax / std::max(comp(ext, a), 1e-6f);
      Bounds acc;
      for (int i = bins_ - 1; i >= 1; --i) acc = bounds_union(acc, bin[i]), right[i] = acc;
      acc = Bounds{};
      for (int i = 0; i < bins_ - 1; ++i) {
        acc = bounds_union(acc, bin[i]);
        if (!acc.valid() || !right[i + 1].valid()) continue;
        float c = cost(acc, kr) + cost(right[i + 1], kr);
        if (c < best) best = c, best_axis = a, best_bin = i;
      }
    }
    uint32_t mid;
    if (best_axis >= 0) {
      float c0 = comp(cmin, best_axis), c1 = comp(cmax, best_axis);
      auto it = std::partition(idx.begin() + lo, idx.begin() + hi, [&](uint32_t li) {
        int bi = std::min(bins_ - 1, int(bins_ * (comp(lights_[li].pos, best_axis) - c0) / (c1 - c0)));
        return bi <= best_bin;
      });
      mid = uint32_t(it - idx.begin());
    } else {
      mid = lo;   // every centroid coincides
    }
    if (mid == lo || mid == hi) mid = lo + (hi - lo) / 2;   // degenerate: split the list in half
    build_rec(idx, lo, mid, bits, depth + 1);
    nodes_[me].link = uint32_t(nodes_.size());
    build_rec(idx, mid, hi, bits | (uint64_t(1) << depth), depth + 1);
  }
};

}  // namespace bg3::lt
