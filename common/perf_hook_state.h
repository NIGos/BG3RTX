#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace bg3::perf {
// Own values only: no pointer to command state or tracker storage survives a call.
// The owner publishes a new generation after every immutable registry mutation.
template<class Key, class Value> struct GenerationCache {
  Key key{};
  uint64_t generation = 0;
  bool populated = false;
  Value value{};
  template<class Load> const Value& get(const Key& k, uint64_t g, Load&& load) {
    if (!populated || generation != g || !(key == k)) {
      value = load(); key = k; generation = g; populated = true;
    }
    return value;
  }
};
struct MatrixSource {
  std::array<const uint8_t*, 3> rows{};
  bool operator==(const MatrixSource&) const = default;
};
struct MatrixHash {
  size_t operator()(const MatrixSource& s) const {
    size_t h = 0;
    for (auto p : s.rows) h ^= std::hash<const uint8_t*>{}(p) + size_t(0x9e3779b9) + (h << 6) + (h >> 2);
    return h;
  }
};
struct MatrixValue {
  std::array<float, 12> rows{};
  bool valid = false;
};
inline bool valid_matrix(const float* m) {
  for (int i = 0; i < 12; ++i) if (!(m[i] > -1e7f && m[i] < 1e7f)) return false;
  float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
              m[2] * (m[4] * m[9] - m[5] * m[8]);
  return std::fabs(det) > 1e-12f;
}
// The destination semantics are shared by cached and uncached gathers. Never
// mutate the cached rows: an invalid job and an instance may share the source.
inline void scatter_matrix(uint8_t* dst, const float* rows, bool valid, bool instance) {
  std::memcpy(dst, rows, 48);
  if (instance) dst[51] = valid ? 0xFF : 0;
  else if (!valid) {
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(dst, &invalid, sizeof invalid);
  }
}
// Construct once per submit. Exact row addresses identify the three 16-byte
// ranges, even with distinct strides, overlapping fixups or different counts.
class SubmitMatrices {
  std::unordered_map<MatrixSource, MatrixValue, MatrixHash> values;
public:
  template<class Load> const MatrixValue& get(const MatrixSource& source, Load&& load) {
    auto [it, inserted] = values.try_emplace(source);
    if (inserted) {
      for (int k = 0; k < 3; ++k) load(it->second.rows.data() + k * 4, source.rows[k]);
      it->second.valid = valid_matrix(it->second.rows.data());
    }
    return it->second;
  }
  size_t size() const { return values.size(); }
};
} // namespace bg3::perf
