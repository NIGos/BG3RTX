// rt_cascade_casters (docs/TRACK_D_RT.md 6.7 item 2, "Cascade-caster capture"): the pure
// parts of the capture of one sun shadow cascade into the ray tracing scene, unit-tested
// in tests/cascade_casters_test.cpp. The Vulkan side is layer/geocapture.cpp (capture,
// dedupe), layer/rtscene.cpp (clip -> world) and layer/bvhsplit.cpp (BLAS + instance).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cascade {

// TLAS instance mask of the cascade-only soup (every other instance keeps 0xFF; the
// shaders' cascade variants trace 0x01 where the soup must not be seen).
constexpr uint8_t kMask = 0x02;
// instanceCustomIndex of the cascade instance: the sun's cascade variant recognises its
// candidates by it (rt_common.hlsli kRtCascadeId). Other instances use their TLAS index.
constexpr uint32_t kCustomIndex = 0xFFFFFFu;

inline uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

// The cascade passes (docs/FRAME_MAP.md 10, AUDIT_ENGINE_DATA R1): depth-only, D16, square
// 2048, one layer each of one layered depth image. Returns the cascade index (the view's
// base layer), -1 if the pass is not one. The far shadow cache draws into images of at
// most 2 layers, hence min_layers 3.
inline int pass_layer(uint32_t color_count, bool depth_d16, uint32_t width, uint32_t height, uint32_t image_layers,
                      uint32_t view_base_layer, uint32_t min_layers = 3) {
  if (color_count != 0 || !depth_d16 || width != height || width < 1024) return -1;
  if (image_layers < min_layers || view_base_layer >= image_layers) return -1;
  return int(view_base_layer);
}

// What identifies a draw in the camera pass and in the cascade pass: the mesh (vertex
// binding 0, index buffer and range) and what places it (the instance stream binding of
// an instanced pipeline, else the per-draw UBO at set 1 binding 0, buffer and offset).
// Two draws with equal keys draw the same triangles at the same place.
struct DrawId {
  uint32_t kind = 0;          // 0 draw, 1 indexed
  uint32_t count = 0, instances = 0, first = 0, first_instance = 0;
  int32_t vertex_offset = 0;
  uint64_t vb = 0, vb_offset = 0;
  uint64_t ib = 0, ib_offset = 0;
  uint32_t ib_type = 0;
  uint64_t place = 0, place_offset = 0;   // instance stream or per-draw UBO; 0 unknown
  uint64_t instance = 0;  // canonical geometry/placement identity, independent of UBO allocation
  double position[3] = {}, position_error[3] = {};
  bool position_known = false;
};
inline uint64_t draw_key(const DrawId& d) {
  uint64_t h = 1469598103934665603ull;
  for (uint64_t v : {uint64_t(d.kind), uint64_t(d.count), uint64_t(d.instances), uint64_t(d.first),
                     uint64_t(d.first_instance), uint64_t(int64_t(d.vertex_offset)), d.vb, d.vb_offset, d.place,
                     d.place_offset})
    h = mix(h, v);
  if (d.kind == 1) h = mix(mix(mix(h, d.ib), d.ib_offset), d.ib_type);
  return h ? h : 1;
}

// The mesh alone (no instance count, no placement): an alpha-tested cascade draw whose mesh the
// camera pass also drew is left to the camera pass's copy, which runs the real alpha test
// (the cascade copy is solid: leaves would cast polygons; user, 2026-10-29).
inline uint64_t mesh_key(const DrawId& d) {
  uint64_t h = 1099511628211ull;
  for (uint64_t v : {uint64_t(d.kind), uint64_t(d.count), uint64_t(d.first), uint64_t(int64_t(d.vertex_offset)),
                     d.vb, d.vb_offset})
    h = mix(h, v);
  if (d.kind == 1) h = mix(mix(mix(h, d.ib), d.ib_offset), d.ib_type);
  return h ? h : 1;
}

// Audited class-B layouts (TRACK_D_SCENE_BUILD 1): current model mat4 at
// byte 0, pulled vertex base at 64 in shadow VSs and 96 in camera VSs.
// Unknown matrix layouts, wind and terrain never use the relaxed identity.
inline uint32_t model_pull_offset(uint64_t vs) {
  switch(vs) {
    case 0x1cf36b97d3067b6cull: case 0xe940736827d5308cull:
    case 0x41a168e2d8cfcaedull: case 0x61239daf5091675aull:
    case 0x17970d7c67f7c124ull: case 0x13a9c9f5334dcafdull:
    case 0x3927925b960e89b7ull: case 0x940da3120a9511d1ull: return 64;
    case 0x3812bc980438747full: case 0xf111e2921f4e2715ull:
    case 0xb1e188951a0c8ec2ull: case 0x1ed985dd808dbc45ull:
    case 0x939d1385fa7f4200ull: return 96;
    default: return 0;
  }
}
inline uint64_t model_instance_key(const uint32_t model[16], uint32_t pull_index,
                                   uint64_t pull_buffer, uint64_t pull_offset) {
  uint64_t h=1469598103934665603ull;
  for(unsigned i=0;i<16;++i) if(i<12 || i>14) h=mix(h,model[i]);
  h=mix(h,pull_index);
  if(pull_index) h=mix(mix(h,pull_buffer),pull_offset);
  return h ? h : 1;
}
inline uint64_t mesh_instance_key(const DrawId& d) {
  if(!d.instance || !d.position_known) return 0;
  // Material UBO addresses are pass-local allocations, not geometry identity.
  // Retain the vertex source/range: a compute-skinned output is NOT a stable
  // skinning source. Without its producer lineage, use static-only instead.
  uint64_t h=mix(mesh_key(d),d.instance);
  h=mix(mix(h,d.instances),d.first_instance);
  return h ? h : 1;
}

// Model translation and the pass origin were each rounded to float by the
// engine. Recover absolute placement in double; the uncertainty is exactly the
// sum of their half-ULPs, rather than a scene-dependent distance threshold.
inline double float_roundoff(float value) {
  const double up=double(std::nextafter(value,INFINITY))-double(value);
  const double down=double(value)-double(std::nextafter(value,-INFINITY));
  return std::fmax(up,down)*0.5;
}
inline bool model_position(DrawId& d,const uint32_t model[16],const float origin[3]) {
  d.position_known=false;
  for(unsigned i=0;i<16;++i) {
    float value;std::memcpy(&value,model+i,sizeof value);
    if(!std::isfinite(value)) return false;
  }
  for(unsigned k=0;k<3;++k) {
    float relative;std::memcpy(&relative,model+12+k,sizeof relative);
    if(!std::isfinite(origin[k])) return false;
    d.position[k]=double(relative)+double(origin[k]);
    d.position_error[k]=float_roundoff(relative)+float_roundoff(origin[k]);
    if(!std::isfinite(d.position_error[k])) return false;
  }
  return d.position_known=true;
}

// Keep candidates in same-frame buckets. Nontranslation model words, mesh and
// pulled instance are exact; only absolute placement has the
// derived float-input uncertainty above. Never borrow a previous-frame model.
class InstanceSets {
 public:
  void insert(uint64_t frame,const DrawId& d) {
    const uint64_t key=mesh_instance_key(d);if(!key) return;
    const unsigned i=unsigned(frame&1);
    if(frame_[i]!=frame) {frame_[i]=frame;sets_[i].clear();}
    sets_[i][key].push_back(d);
  }
  bool contains(uint64_t frame,const DrawId& d) const {
    const uint64_t key=mesh_instance_key(d);if(!key) return false;
    const unsigned i=unsigned(frame&1);if(frame_[i]!=frame) return false;
    const auto found=sets_[i].find(key);if(found==sets_[i].end()) return false;
    for(const DrawId& other:found->second) {
      bool same=true;
      for(unsigned k=0;k<3;++k)
        same=same && std::fabs(d.position[k]-other.position[k])<=d.position_error[k]+other.position_error[k];
      if(same) return true;
    }
    return false;
  }
 private:
  std::unordered_map<uint64_t,std::vector<DrawId>> sets_[2];
  uint64_t frame_[2]={UINT64_MAX,UINT64_MAX};
};

// Preserve the existing legacy opaque-cascade policy when a richer current
// material stream is captured alongside it. Raw cutouts never become legacy
// opaque casters merely because the current path knows their triangle range.
enum class LegacyCaster { keep, alpha_skip, duplicate };
// Conservative class-A subset: affine instance inputs, no VS deformation and no
// potentially compute-written position stream. Unknown includes per-draw static
// meshes: do not guess that an unrecognised character/cloth path is static.
inline bool static_candidate(bool affine_instance, bool position_known, bool storage_position) {
  return affine_instance && position_known && !storage_position;
}

// Compare membership, not draw order or the cascade matrix. Seed again after a
// gap, a cascade switch or a policy toggle; those are not consecutive samples.
class SetChanges {
 public:
  bool observe(uint64_t frame, bool filtered, const std::unordered_set<uint64_t>& keys) {
    const bool changed = frame_ != UINT64_MAX && frame == frame_ + 1 && filtered == filtered_ && keys != keys_;
    if (frame_ == UINT64_MAX || frame >= frame_) {
      frame_ = frame; filtered_ = filtered; keys_ = keys;
    }
    return changed;
  }
 private:
  uint64_t frame_ = UINT64_MAX;
  bool filtered_ = false;
  std::unordered_set<uint64_t> keys_;
};
inline LegacyCaster legacy_caster(bool discards,int alpha_mode,bool known_cutout,
                                  bool alpha_dedupe,bool mesh_duplicate,bool draw_duplicate) {
  if(discards && alpha_mode!=1 && (known_cutout || alpha_mode!=2)) return LegacyCaster::alpha_skip;
  if((discards && alpha_dedupe && mesh_duplicate) || draw_duplicate) return LegacyCaster::duplicate;
  return LegacyCaster::keep;
}

// The camera pass's draw keys of the last two frames (the camera pass and the cascades are
// recorded on different threads, in either order: a cascade draw is a duplicate if the
// camera pass drew it in this frame or the last one).
class KeySets {
 public:
  void insert(uint64_t frame, uint64_t key) { set_for(frame).insert(key); }
  bool contains(uint64_t frame, uint64_t key) const {
    for (int i = 0; i < 2; ++i)
      if ((frame_[i] == frame || frame_[i] + 1 == frame) && sets_[i].count(key)) return true;
    return false;
  }
  size_t size(uint64_t frame) const {
    for (int i = 0; i < 2; ++i)
      if (frame_[i] == frame) return sets_[i].size();
    return 0;
  }

 private:
  std::unordered_set<uint64_t>& set_for(uint64_t frame) {
    const int i = int(frame & 1);
    if (frame_[i] != frame) {
      frame_[i] = frame;
      sets_[i].clear();
    }
    return sets_[i];
  }
  std::unordered_set<uint64_t> sets_[2];
  uint64_t frame_[2] = {UINT64_MAX - 1, UINT64_MAX - 1};
};

// The cascade's view-projection as the VS uses it: rows v[8..11] of the camera-layout UBO
// (set 0 binding 2, _m2 at byte 128, row-major from GLSL): clip_j = sum_k W_k m[k][j] + m[3][j],
// then clip.y negated. m[r] = {x, y, z, w} of row r.
struct Ortho {
  float w_err = 0;       // max |w column - (0, 0, 0, 1)|: 0 for an orthographic matrix
  float det = 0;         // of the x, y, z rows (the 3x3 the inversion solves)
  float extent[3] = {};  // metres covered by clip x in [-1, 1], y in [-1, 1], z in [0, 1]
  bool ok = false;       // orthographic and invertible
};
inline Ortho ortho_check(const float m[4][4]) {
  Ortho o;
  o.w_err = std::fabs(m[0][3]);
  for (int r = 1; r < 3; ++r) o.w_err = std::fmax(o.w_err, std::fabs(m[r][3]));
  o.w_err = std::fmax(o.w_err, std::fabs(m[3][3] - 1.0f));
  // Columns j = 0, 1, 2 of the 3x3 (the clip x, y, z rows of the system M W = b).
  auto col = [&](int j, float out[3]) { for (int k = 0; k < 3; ++k) out[k] = m[k][j]; };
  float a[3], b[3], c[3];
  col(0, a), col(1, b), col(2, c);
  o.det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
  auto len = [](const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
  o.extent[0] = len(a) > 0 ? 2.0f / len(a) : 0.0f;
  o.extent[1] = len(b) > 0 ? 2.0f / len(b) : 0.0f;
  o.extent[2] = len(c) > 0 ? 1.0f / len(c) : 0.0f;
  o.ok = o.w_err < 1e-5f && std::fabs(o.det) > 1e-30f && std::isfinite(o.det);
  return o;
}

// CPU mirror of shaders/rt/xfb_to_world.hlsl with XFB_ORTHO: clip -> camera-relative world
// from the x, y and z rows (the w row of an orthographic matrix is (0, 0, 0, 1) and carries
// no information); false where the vertex's w is not what the matrix gives (not this
// matrix's output) or the result is not finite.
inline bool clip_to_world(const float m[4][4], const float clip[4], float out[3]) {
  const float r0[3] = {m[0][0], m[1][0], m[2][0]}, r1[3] = {m[0][1], m[1][1], m[2][1]},
              r2[3] = {m[0][2], m[1][2], m[2][2]};
  auto cross = [](const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0];
  };
  float c0[3], c1[3], c2[3];
  cross(r1, r2, c0), cross(r2, r0, c1), cross(r0, r1, c2);
  const float det = r0[0] * c0[0] + r0[1] * c0[1] + r0[2] * c0[2];
  if (!(std::fabs(det) > 1e-30f)) return false;
  const float b[3] = {clip[0] - m[3][0], -clip[1] - m[3][1], clip[2] - m[3][2]};
  for (int k = 0; k < 3; ++k) out[k] = (c0[k] * b[0] + c1[k] * b[1] + c2[k] * b[2]) / det;
  const float wc = out[0] * m[0][3] + out[1] * m[1][3] + out[2] * m[2][3] + m[3][3];
  bool ok = std::fabs(wc - clip[3]) <= 1e-3f * std::fabs(wc) + 1e-6f;
  for (int k = 0; k < 3; ++k) ok = ok && std::isfinite(out[k]) && std::fabs(out[k]) < 1e7f;
  return ok;
}

// Camera-relative space is world minus the camera position (UBO byte 208), world axes:
// a point captured relative to `captured_cam` is at p + (captured_cam - current_cam)
// relative to `current_cam`. The cascade instance's TLAS translation.
inline void rebase(const float captured_cam[3], const float current_cam[3], float t[3]) {
  for (int k = 0; k < 3; ++k) t[k] = captured_cam[k] - current_cam[k];
}

// A fragment shader that can discard (OpKill, OpTerminateInvocation, OpDemoteToHelperInvocation):
// in a cascade pass it alpha-tests, and the soup would make its cutouts solid.
inline bool spirv_fragment_discards(const uint32_t* code, size_t words) {
  if (!code || words < 5 || code[0] != 0x07230203u) return false;
  bool fragment = false, discards = false;
  for (size_t i = 5; i < words;) {
    const uint32_t op = code[i] & 0xffffu, n = code[i] >> 16;
    if (!n || i + n > words) break;
    if (op == 15 && n >= 2 && code[i + 1] == 4) fragment = true;   // OpEntryPoint Fragment
    if (op == 252 || op == 4416 || op == 5380) discards = true;
    i += n;
  }
  return fragment && discards;
}

}  // namespace cascade
