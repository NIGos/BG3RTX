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
// NotHostMapped: the index id cannot be read; the layer then matches by UV samples from its own soup
// (UvPending until the readback lands, UvMismatch when no file with that index count has those UVs).
enum class JoinMiss : uint8_t { None = 0, NotIndexed, CountUnknown, NotHostMapped, NoFile, UvPending, UvMismatch, Count };
inline JoinMiss join_miss(bool indexed, bool count_known, bool host_mapped, bool file_found) {
  if (!indexed) return JoinMiss::NotIndexed;
  if (!count_known) return JoinMiss::CountUnknown;   // no groom file has this index count (other style or LOD)
  if (!host_mapped) return JoinMiss::NotHostMapped;
  return file_found ? JoinMiss::None : JoinMiss::NoFile;
}

// The sampled corners of the UV join: corner floor(s * (count - 1) / 31) in index order (tools/hair_groom/asset.py).
inline uint32_t uv_sample_corner(uint32_t s, uint32_t count) { return uint32_t(uint64_t(s) * (count - 1) / (kSamples - 1)); }
// Captured UVs (the draw's soup, as the vertex shader wrote them) against a file's samples: equal within `tol`
// (the capture path's float rounding), with v as stored or flipped (1 - v). Non-finite captured UVs never match.
inline bool uv_samples_match(const float* captured, const float* expected, float tol = 1e-3f) {
  for (int flip = 0; flip < 2; ++flip) {
    bool ok = true;
    for (uint32_t s = 0; s < kSamples && ok; ++s) {
      const float u = captured[2 * s], v = captured[2 * s + 1];
      const float ev = flip ? 1.0f - expected[2 * s + 1] : expected[2 * s + 1];
      ok = u == u && v == v && (u - expected[2 * s]) <= tol && (expected[2 * s] - u) <= tol && (v - ev) <= tol && (ev - v) <= tol;
    }
    if (ok) return true;
  }
  return false;
}
inline const char* join_miss_name(JoinMiss m) {
  switch (m) {
    case JoinMiss::None: return "matched";
    case JoinMiss::NotIndexed: return "not an indexed draw";
    case JoinMiss::CountUnknown: return "no groom file with this index count (other style or LOD: run tools.hair_groom.batch)";
    case JoinMiss::NotHostMapped: return "index buffer not host-mapped";
    case JoinMiss::NoFile: return "index id has no groom file (same count, other mesh)";
    case JoinMiss::UvPending: return "UV join pending (soup readback)";
    case JoinMiss::UvMismatch: return "no groom file with this index count has these UVs (other mesh)";
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
  bool has_uv = false;
  float uv[kSamples * 2] = {};   // v2: UV0 of the sampled corners
  uint32_t version = 0;          // 1, 2 or 3 (3: compressed, common/hair_groom_v3.h)
  uint32_t lod_count = 0;        // from the header (parse_header leaves `lods` empty)
  uint32_t lod_segments[8] = {}; // per LOD, from lod_header (the crowd plan's costs before an upload)
  std::vector<Lod> lods;
};

// One LOD's 16-byte header at `at` (the first one at parse_header's header_bytes): its points and segments and where
// the next LOD's header starts (after keep, points, segments, 0 and the arrays: points x 17 bytes, segments x 4).
inline bool lod_header(const uint8_t* h16, uint64_t at, uint32_t* points, uint32_t* segments, uint64_t* next) {
  float keep;
  std::memcpy(&keep, h16, 4);
  std::memcpy(points, h16 + 4, 4);
  std::memcpy(segments, h16 + 8, 4);
  if (!(keep > 0 && keep <= 1) || *points < 2 || *segments == 0 || *segments >= *points) return false;
  *next = at + 16 + uint64_t(*points) * 17u + uint64_t(*segments) * 4u;
  return true;
}

// Layout (tools/hair_groom/asset.py): 'HGRM', version 1 or 2, position id u64, index id u64, card vertices,
// card indices, lods, 0; v2: 32 x (u, v) f32 corner samples; per LOD: keep f32, points, segments, 0, then the
// arrays above in that order.
// The header and UV samples only (the layer indexes every file at startup without reading its arrays).
// Sets `header_bytes` to where the LODs start.
inline bool parse_header(const uint8_t* bytes, uint64_t size, Asset& out, uint64_t* header_bytes = nullptr) {
  out = {};
  if (size < 40 || std::memcmp(bytes, "HGRM", 4) != 0) return false;
  uint32_t version, lods;
  std::memcpy(&version, bytes + 4, 4);
  std::memcpy(&out.position_id, bytes + 8, 8);
  std::memcpy(&out.index_id, bytes + 16, 8);
  std::memcpy(&out.vertices, bytes + 24, 4);
  std::memcpy(&out.indices, bytes + 28, 4);
  std::memcpy(&lods, bytes + 32, 4);
  if ((version < 1 || version > 3) || !out.indices || out.indices % 3 || !lods || lods > 8 || !out.index_id) return false;
  out.lod_count = lods;
  out.version = version;
  uint64_t at = 40;
  if (version >= 2) {   // v3 (common/hair_groom_v3.h) keeps v2's header and UV samples
    if (size - at < sizeof out.uv) return false;
    std::memcpy(out.uv, bytes + at, sizeof out.uv);
    at += sizeof out.uv;
    for (float x : out.uv)
      if (!(x == x) || x < -64.0f || x > 64.0f) return false;
    out.has_uv = true;
  }
  if (header_bytes) *header_bytes = at;
  return true;
}

// One LOD at `at` (its 16-byte header, then the arrays); advances `at` past it. `bytes` must outlive the views. Every
// range and index is checked; false on anything malformed. The layer reads and validates only the LOD it uploads
// (round 3 M6: it read and parsed the whole file for every LOD upload).
inline bool parse_lod(const uint8_t* bytes, uint64_t size, uint64_t& at, uint32_t triangles, Lod& out) {
  {
    if (at > size || size - at < 16) return false;
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
      // A zero-length segment (both points on the same triangle at the same barycentrics coincide in every pose) has
      // no tangent: malformed (review 2026-11-08 item 17; 0 in a 60-file sample of the shipped grooms, 61 M segments).
      uint32_t t0, t1;
      float b0[2], b1[2];
      std::memcpy(&t0, lod.triangle + v, 4);
      std::memcpy(&t1, lod.triangle + v + 1, 4);
      std::memcpy(b0, lod.bary + 2 * uint64_t(v), 8);
      std::memcpy(b1, lod.bary + 2 * uint64_t(v) + 2, 8);
      if (t0 == t1 && b0[0] == b1[0] && b0[1] == b1[1]) return false;
    }
    out = lod;
  }
  return true;
}

// `bytes` must outlive the views. Every range and index is checked; false on anything malformed.
inline bool parse(const uint8_t* bytes, uint64_t size, Asset& out) {
  uint64_t at = 0;
  if (!parse_header(bytes, size, out, &at) || out.version == 3) return false;   // v3: common/hair_groom_v3.h
  const uint32_t triangles = out.indices / 3, lods = out.lod_count;
  for (uint32_t l = 0; l < lods; ++l) {
    Lod lod;
    if (!parse_lod(bytes, size, at, triangles, lod)) return false;
    out.lods.push_back(lod);
  }
  return at == size;
}

}  // namespace bg3::hair_groom
