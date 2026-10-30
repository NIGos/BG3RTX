#pragma once
// Conservative finite-ray coverage. A native shadow-map clip box is a bounded
// domain, NOT proof that empty space continues beyond its light-near plane.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <unordered_map>

namespace bg3::sun_coverage {
// ExecutionMode DepthReplacing (SPIR-V mode12) means interpolated triangle
// depth alone is not a complete native caster representation. Malformed input
// also withholds the certificate; it is not silently called ordinary opaque.
inline bool fragment_depth_replacing(const uint32_t* code, size_t words) {
  if (!code || words < 5 || code[0] != 0x07230203u) return true;
  bool fragment=false, depth=false;
  for(size_t i=5;i<words;) {
    const uint32_t n=code[i]>>16,op=code[i]&0xffffu;
    if (!n || n>words-i) return true;
    if (op==15 && n>=3 && code[i+1]==4) fragment=true;
    if (op==16 && n>=3 && code[i+2]==12) depth=true;
    i+=n;
  }
  return fragment && depth;
}
// Native global Frame CB set0/b0, eight scalar words. Its temporal dither phase
// is word2. Preserve the whole packet; a present counter is not a native epoch.
struct NativeEpoch {
  std::array<uint32_t, 8> words{};
  bool available = false;
};
inline bool valid(NativeEpoch e) {
  float phase;
  std::memcpy(&phase, &e.words[2], sizeof phase);
  return e.available && std::isfinite(phase) && phase >= 0 && phase < 16777216.0f &&
         phase == std::trunc(phase);
}
inline bool same_epoch(NativeEpoch a, NativeEpoch b) {
  return valid(a) && valid(b) && a.words == b.words;
}

// These are mandatory observations, not defaults inferred from a capture flag.
// Pending command buffers can be recorded as dependencies, but may not publish
// a complete certificate until their actual submission order is verified.
struct Witness {
  NativeEpoch camera, cascade, lighting;
  uint64_t camera_record = 0, cascade_record = 0, consumer_record = 0;
  bool ordered_submission = false;
  bool geometry_complete = false, material_coverage_complete = false;
  bool converted = false, isolated_clone = false;
};
inline bool complete(const Witness& w) {
  return w.camera_record && w.cascade_record && w.consumer_record &&
         w.camera_record != w.consumer_record && w.cascade_record != w.consumer_record &&
         same_epoch(w.camera, w.cascade) && same_epoch(w.camera, w.lighting) &&
         w.ordered_submission && w.geometry_complete && w.material_coverage_complete &&
         w.converted && w.isolated_clone;
}
// Only a single, actually submitted primary-CB entry is a proof here. Do not
// infer GPU order from CPU recording completion or from frame_index tags.
inline bool producers_before(std::span<const uint64_t> commands, uint64_t camera,
                             uint64_t cascade, uint64_t consumer) {
  if(!camera || !cascade || !consumer || camera==consumer || cascade==consumer) return false;
  size_t a=commands.size(),b=a,c=a;unsigned na=0,nb=0,nc=0;
  for(size_t i=0;i<commands.size();++i) {
    if(commands[i]==camera) {a=i;++na;}
    if(commands[i]==cascade) {b=i;++nb;}
    if(commands[i]==consumer) {c=i;++nc;}
  }
  return na==1 && nb==1 && nc==1 && a<c && b<c;
}
class CommandOwners {
  struct Owner {uint64_t command=0,generation=0;};
  std::unordered_map<uint64_t,Owner> owners_;
  std::unordered_map<uint64_t,uint64_t> generations_;
public:
  void record(uint64_t identity,uint64_t command) {owners_[identity]={command,generations_[command]};}
  void reset(uint64_t command) {++generations_[command];}
  void forget(uint64_t identity) {owners_.erase(identity);}
  bool matches(uint64_t identity,uint64_t command) const {
    const auto owner=owners_.find(identity);
    if(!identity || !command || owner==owners_.end() || owner->second.command!=command) return false;
    const auto generation=generations_.find(command);
    return generation!=generations_.end() && generation->second==owner->second.generation;
  }
  void clear() {owners_.clear();generations_.clear();}
};

// Clip x/y are [-1,1], z [0,1]. Rows transform camera-relative positions
// directly; the cascade/camera origin translation must already be included.
struct Volume { std::array<std::array<float, 4>, 3> clip{}; bool available = false; };
struct Ray { std::array<float, 3> origin{}, direction{}; float tmax = 0; };
inline bool contains_segment(const Volume& v, const Ray& r) {
  if (!v.available || !std::isfinite(r.tmax) || r.tmax < 0) return false;
  for (const auto& row : v.clip) for (float x : row) if (!std::isfinite(x)) return false;
  const auto& m = v.clip;
  const double det = double(m[0][0]) * (double(m[1][1])*m[2][2] - double(m[1][2])*m[2][1]) -
                     double(m[0][1]) * (double(m[1][0])*m[2][2] - double(m[1][2])*m[2][0]) +
                     double(m[0][2]) * (double(m[1][0])*m[2][1] - double(m[1][1])*m[2][0]);
  if (!std::isfinite(det) || det == 0) return false;
  double d2 = 0;
  for (unsigned k = 0; k < 3; ++k) {
    if (!std::isfinite(r.origin[k]) || !std::isfinite(r.direction[k])) return false;
    d2 += double(r.direction[k]) * r.direction[k];
  }
  if (!(d2 > 1e-20)) return false;
  for (unsigned axis = 0; axis < 3; ++axis) {
    double a = v.clip[axis][3], b = a, ma = std::abs(a), mb = ma;
    for (unsigned k = 0; k < 4; ++k)
      if (!std::isfinite(v.clip[axis][k])) return false;
    for (unsigned k = 0; k < 3; ++k) {
      const double p = double(v.clip[axis][k]) * r.origin[k];
      const double q = double(v.clip[axis][k]) *
                       (double(r.origin[k]) + double(r.direction[k]) * r.tmax);
      a += p; b += q; ma += std::abs(p);
      mb += std::abs(double(v.clip[axis][k])) *
            (std::abs(double(r.origin[k])) + std::abs(double(r.direction[k]) * r.tmax));
    }
    // Outward float32 arithmetic budget for the matching GPU endpoint transform:
    // endpoint + affine dot + magnitude sums (at most 24 rounded operations). This
    // is derived, not a world-space edge tolerance. Strict interior is required.
    constexpr double u = std::numeric_limits<float>::epsilon() / 2.0;
    constexpr double gamma = 24.0 * u / (1.0 - 24.0 * u);
    const double lo = axis == 2 ? 0.0 : -1.0;
    constexpr double underflow = 12.0 * std::numeric_limits<float>::min();
    const double ea = gamma * ma + underflow, eb = gamma * mb + underflow;
    if (!(a - ea > lo && a + ea < 1.0 && b - eb > lo && b + eb < 1.0)) return false;
  }
  return true;
}
} // namespace bg3::sun_coverage
