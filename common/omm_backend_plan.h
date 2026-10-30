#pragma once
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

// EXT wire layout only. This does not certify alpha classification or GPU
// sampling. The caller uploads these exact descriptor bytes, without remapping.
namespace bg3::omm {
struct Triangle { uint32_t dataOffset; uint16_t subdivisionLevel, format; };
struct Usage { uint32_t count, subdivisionLevel, format; };
struct Plan { std::vector<Usage> usage; uint32_t triangles{}; uint64_t dataBytes{}; };
inline bool make_plan(std::span<const Triangle> triangles, uint64_t dataBytes,
                      uint32_t maxFourStateLevel, Plan& out) {
  out = {};
  if (triangles.empty() || triangles.size() > UINT32_MAX || !dataBytes) return false;
  for (const auto& t : triangles) {
    // Four-state only. Never promote UNKNOWN to a two-state known state.
    if (t.format != 2 || t.subdivisionLevel > maxFourStateLevel || t.subdivisionLevel > 12) return false;
    const uint64_t bits = uint64_t(2) << (2u * t.subdivisionLevel);
    const uint64_t bytes = (bits + 7) / 8;
    if (t.dataOffset > dataBytes || bytes > dataBytes - t.dataOffset) return false;
    bool found = false;
    for (auto& u : out.usage) if (u.subdivisionLevel == t.subdivisionLevel && u.format == t.format) {
      ++u.count; found = true; break;
    }
    if (!found) out.usage.push_back({1, t.subdivisionLevel, t.format});
  }
  out.triangles = static_cast<uint32_t>(triangles.size()); out.dataBytes = dataBytes;
  return true;
}
inline bool input_ranges(const Plan& p, uint64_t dataAddress, uint64_t dataBytes,
                         uint64_t triangleAddress, uint64_t triangleBytes) {
  return p.triangles && p.dataBytes && dataAddress && triangleAddress &&
    dataAddress % 256 == 0 && triangleAddress % 256 == 0 &&
    dataBytes >= p.dataBytes && triangleBytes >= uint64_t(p.triangles) * sizeof(Triangle) &&
    dataBytes <= UINT64_MAX - dataAddress && triangleBytes <= UINT64_MAX - triangleAddress;
}
// Serial values are actual SAME-QUEUE submission/completion values, never frame
// counters. Before retirement the owner must mark every referencing BLAS use.
struct Lifetime {
  uint64_t lastUse{}; bool submitted{}, retiring{};
  bool use(uint64_t serial) {
    if (!serial || retiring || (submitted && serial < lastUse)) return false;
    lastUse = serial; submitted = true; return true;
  }
  bool retire() { if (!submitted || retiring) return false; retiring = true; return true; }
  bool releasable(uint64_t completed) const { return submitted && retiring && completed >= lastUse; }
};
}
