// Diagnostic candidate identity only. Never sufficient to qualify a material.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace bg3::mesh_content {
constexpr uint32_t kSamples = 32;
struct Bytes { const uint8_t* data{}; uint64_t size{}; };
struct Input {
  Bytes positions, indices;
  uint32_t stride{}, count{}, index_bytes{}; // 0 index_bytes: non-indexed
  int64_t vertex_offset{};
};
struct Sample {
  // Version, referenced vertex span, index count, then 32 (index, float3 bits).
  // LE uint32 words, FNV-1a 64. Index widths/padding/handles are excluded.
  std::array<uint32_t, 3 + kSamples * 4> words{};
  bool operator==(const Sample&) const = default;
};
inline uint64_t hash(const Sample& s) {
  uint64_t h = 14695981039346656037ull;
  for (uint32_t w : s.words) for (unsigned b = 0; b < 4; ++b) {
    h = (h ^ uint8_t(w >> (b * 8))) * 1099511628211ull;
  }
  return h ? h : 1; // zero means unavailable
}
inline bool sample(const Input& in, Sample& out) {
  out = {};
  if (!in.positions.data || in.stride < 12 || !in.count || in.count % 3 ||
      (in.index_bytes != 0 && in.index_bytes != 1 && in.index_bytes != 2 && in.index_bytes != 4) ||
      (in.index_bytes && (!in.indices.data || uint64_t(in.count) * in.index_bytes > in.indices.size))) return false;
  auto index = [&](uint32_t i) {
    if (in.index_bytes == 1) return uint32_t(in.indices.data[i]);
    if (in.index_bytes == 2) { uint16_t v; std::memcpy(&v, in.indices.data + uint64_t(i) * 2, 2); return uint32_t(v); }
    if (in.index_bytes == 4) { uint32_t v; std::memcpy(&v, in.indices.data + uint64_t(i) * 4, 4); return v; }
    return i;
  };
  uint32_t lo = 0, hi = in.count - 1;
  if (in.index_bytes) {
    lo = UINT32_MAX; hi = 0;
    // Draws do not carry a vertex count: derive the referenced span, without
    // reading positions or inventing a count from the backing allocation.
    for (uint32_t i = 0; i < in.count; ++i) { const auto v = index(i); lo = (std::min)(lo, v); hi = (std::max)(hi, v); }
  }
  if (in.vertex_offset > INT64_MAX - hi) return false;
  const int64_t first = in.vertex_offset + lo, last = in.vertex_offset + hi;
  if (first < 0 || last < first || uint64_t(last) > UINT64_MAX / in.stride ||
      uint64_t(last) * in.stride > in.positions.size || in.positions.size - uint64_t(last) * in.stride < 12 ||
      uint64_t(hi) - lo + 1 > UINT32_MAX) return false;
  out.words[0] = 1; out.words[1] = hi - lo + 1; out.words[2] = in.index_bytes ? in.count : 0;
  for (uint32_t s = 0; s < kSamples; ++s) {
    const uint32_t at = uint32_t(uint64_t(s) * (in.count - 1) / (kSamples - 1));
    const uint32_t v = index(at);
    auto* w = out.words.data() + 3 + s * 4;
    w[0] = v - lo;
    std::memcpy(w + 1, in.positions.data + uint64_t(in.vertex_offset + v) * in.stride, 12);
    for (unsigned c = 1; c <= 3; ++c) if ((w[c] & 0x7f800000u) == 0x7f800000u) return false;
  }
  return true;
}
// Two adjacent capture sets, bounded by the current draw count. A repeated
// handle is irrelevant; stability requires the same ID AND sampled words.
struct Captures {
  std::unordered_map<uint64_t, Sample> previous, current;
  uint64_t collisions{}, stable{};
  void next() { previous = std::move(current); current.clear(); collisions = stable = 0; }
  void add(uint64_t id, const Sample& s) {
    auto [it, inserted] = current.emplace(id, s);
    if (!inserted) {
      if (!(it->second == s)) {
        const auto old = previous.find(id);
        if (old != previous.end() && old->second == it->second) --stable;
        ++collisions;
        it->second.words[0] = 0; // disputed IDs cannot count as stable
      }
      return;
    }
    const auto old = previous.find(id);
    if (old != previous.end()) { if (old->second == s) ++stable; else ++collisions; }
  }
  void clear() { previous.clear(); current.clear(); collisions = stable = 0; }
};
} // namespace bg3::mesh_content
