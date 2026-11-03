#pragma once
// rt_hair_groom (docs/RTX_HAIR_INTEGRATION.md): the strand groom of one hair mesh, generated on the
// player's PC by `python -m tools.hair_groom.asset` into data/assetdb/hair_grooms/<index id>.hgrm.
// Header-only parser and the index-only content ID the layer uses to join a captured hair draw to it.
#include <cstdint>
#include <cstring>
#include <vector>

namespace bg3::hair_groom {

// Index-only content ID (version 2 of layer/mesh_content_id.h's words): skinned hair draws have no
// host-mapped float3 positions, but their index ranges are host data. Words: 2, referenced vertex
// span, index count, then the 32 sampled indices (minus the lowest) at corner
// floor(s * (count - 1) / 31). FNV-1a 64 over the little-endian words; 0 maps to 1.
constexpr uint32_t kSamples = 32;
template <class Index>
uint64_t index_content_id(Index&& index, uint32_t count) {
  if (count < 3 || count % 3) return 0;
  uint32_t lo = UINT32_MAX, hi = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t v = index(i);
    lo = v < lo ? v : lo;
    hi = v > hi ? v : hi;
  }
  uint64_t h = 14695981039346656037ull;
  auto word = [&](uint32_t w) {
    for (unsigned b = 0; b < 4; ++b) h = (h ^ uint8_t(w >> (b * 8))) * 1099511628211ull;
  };
  word(2);
  word(hi - lo + 1);
  word(count);
  for (uint32_t s = 0; s < kSamples; ++s) word(index(uint32_t(uint64_t(s) * (count - 1) / (kSamples - 1))) - lo);
  return h ? h : 1;
}

// Why a captured native hair draw got no groom (layer log and counters; tests/hair_groom_asset_test.cpp).
enum class JoinMiss : uint8_t { None = 0, NotIndexed, CountUnknown, NotHostMapped, NoFile, Count };
inline JoinMiss join_miss(bool indexed, bool count_known, bool host_mapped, bool file_found) {
  if (!indexed) return JoinMiss::NotIndexed;
  if (!count_known) return JoinMiss::CountUnknown;   // no groom file has this index count (other style or LOD)
  if (!host_mapped) return JoinMiss::NotHostMapped;
  return file_found ? JoinMiss::None : JoinMiss::NoFile;
}
inline const char* join_miss_name(JoinMiss m) {
  switch (m) {
    case JoinMiss::None: return "matched";
    case JoinMiss::NotIndexed: return "not an indexed draw";
    case JoinMiss::CountUnknown: return "no groom file with this index count (other style or LOD: run tools.hair_groom.batch)";
    case JoinMiss::NotHostMapped: return "index buffer not host-mapped";
    case JoinMiss::NoFile: return "index id has no groom file (same count, other mesh)";
    default: return "?";
  }
}

struct Lod {
  float keep = 1;            // share of the strands (radii widened by 1 / keep)
  uint32_t points = 0, segments = 0;
  const uint32_t* triangle = nullptr;   // [points] card triangle in native index order
  const float* bary = nullptr;          // [points * 2] weights of the triangle's 2nd and 3rd vertex
  const float* radius = nullptr;        // [points] metres
  const uint8_t* root = nullptr;        // [points] 0 root .. 255 tip
  const uint32_t* segment = nullptr;    // [segments] first point (segment = points i, i + 1)
};
struct Asset {
  uint64_t position_id = 0, index_id = 0;
  uint32_t vertices = 0, indices = 0;
  std::vector<Lod> lods;
};

// Layout v1 (tools/hair_groom/asset.py): 'HGRM', version 1, position id u64, index id u64, card vertices,
// card indices, lods, 0; per LOD: keep f32, points, segments, 0, then the arrays above in that order.
// `bytes` must outlive the views. Every range and index is checked; false on anything malformed.
inline bool parse(const uint8_t* bytes, uint64_t size, Asset& out) {
  out = {};
  if (size < 40 || std::memcmp(bytes, "HGRM", 4) != 0) return false;
  uint32_t version, vertices, indices, lods, pad;
  std::memcpy(&version, bytes + 4, 4);
  std::memcpy(&out.position_id, bytes + 8, 8);
  std::memcpy(&out.index_id, bytes + 16, 8);
  std::memcpy(&vertices, bytes + 24, 4);
  std::memcpy(&indices, bytes + 28, 4);
  std::memcpy(&lods, bytes + 32, 4);
  std::memcpy(&pad, bytes + 36, 4);
  if (version != 1 || !indices || indices % 3 || !lods || lods > 8 || !out.index_id) return false;
  out.vertices = vertices;
  out.indices = indices;
  const uint32_t triangles = indices / 3;
  uint64_t at = 40;
  for (uint32_t l = 0; l < lods; ++l) {
    if (size - at < 16) return false;
    Lod lod;
    std::memcpy(&lod.keep, bytes + at, 4);
    std::memcpy(&lod.points, bytes + at + 4, 4);
    std::memcpy(&lod.segments, bytes + at + 8, 4);
    at += 16;
    if (!(lod.keep > 0 && lod.keep <= 1) || lod.points < 2 || lod.segments == 0 || lod.segments >= lod.points) return false;
    const uint64_t p = lod.points, s = lod.segments;
    if ((size - at) < p * (4 + 8 + 4 + 1) + s * 4) return false;
    // Arrays are 4-byte aligned except root (last per-point array): segment follows it unaligned.
    lod.triangle = reinterpret_cast<const uint32_t*>(bytes + at); at += p * 4;
    lod.bary = reinterpret_cast<const float*>(bytes + at); at += p * 8;
    lod.radius = reinterpret_cast<const float*>(bytes + at); at += p * 4;
    lod.root = bytes + at; at += p;
    lod.segment = reinterpret_cast<const uint32_t*>(bytes + at); at += s * 4;
    for (uint64_t i = 0; i < p; ++i) {
      uint32_t t;
      float b[2], r;
      std::memcpy(&t, lod.triangle + i, 4);
      std::memcpy(b, lod.bary + 2 * i, 8);
      std::memcpy(&r, lod.radius + i, 4);
      if (t >= triangles || !(b[0] >= -1e-3f && b[1] >= -1e-3f && b[0] + b[1] <= 1.001f) || !(r > 0 && r < 0.01f)) return false;
    }
    for (uint64_t i = 0; i < s; ++i) {
      uint32_t v;
      std::memcpy(&v, lod.segment + i, 4);
      if (v + 1 >= p) return false;
    }
    out.lods.push_back(lod);
  }
  return at == size;
}

}  // namespace bg3::hair_groom
