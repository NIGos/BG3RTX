#pragma once
// Offline VT alpha masks for the RT alpha test (rt_vt_alpha_masks, docs/P4_OMM.md 11.2): the parsing and
// matching the layer does (layer/alphatest.cpp), CPU only and unit-tested (tests/vt_alpha_masks_test.cpp).
// Files: data/assetdb/vt_alpha/ written by tools/assets/vt_alpha_masks.py from the game's tile sets (LSLib):
//   vt_alpha_index.txt  "<tileset> <p0.x> <p0.y> <p0.z> <p0.w> <file>" per line, p0 as float32 bit patterns in hex
//                       ('#' starts a comment); p0 is the VT record's uv transform (AlphaRec p0), tileset the
//                       record's SSBO entry m0 row 3 column 3 (AlphaRec t[15]).
//   <file>.vtalpha      "BG3VTA1\0", uint32 width, height, levels, then per level the BC4 blocks of that level
//                       (ceil(w/4) x ceil(h/4) x 8 bytes, row-major): the alpha halves of the tile set's BC3 blocks.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace bg3::vt_alpha {

struct IndexEntry { uint32_t tileset = 0; uint32_t p0[4] = {}; std::string file; };

// One index line; false for comments, blank or malformed lines (file names have no spaces).
inline bool parse_index_line(const std::string& line, IndexEntry* out) {
  if (line.empty() || line[0] == '#') return false;
  unsigned t = 0, a = 0, b = 0, c = 0, d = 0;
  char name[260] = {};
#ifdef _MSC_VER
  if (sscanf_s(line.c_str(), "%u %x %x %x %x %259s", &t, &a, &b, &c, &d, name, unsigned(sizeof name)) != 6) return false;
#else
  if (std::sscanf(line.c_str(), "%u %x %x %x %x %259s", &t, &a, &b, &c, &d, name) != 6) return false;
#endif
  out->tileset = t;
  out->p0[0] = a; out->p0[1] = b; out->p0[2] = c; out->p0[3] = d;
  out->file = name;
  return out->file.find("..") == std::string::npos && out->file.find_first_of("/\\:") == std::string::npos;
}

struct Level { uint32_t width, height; uint64_t offset, bytes; };   // offset into the file
struct Mask { uint32_t width = 0, height = 0; std::vector<Level> levels; };

constexpr uint32_t kMaxSize = 16384, kMaxLevels = 15;

// Validates a .vtalpha file's header and level table against its size; false on any inconsistency.
inline bool parse_mask(const uint8_t* data, uint64_t size, Mask* out) {
  static const char kMagic[8] = {'B', 'G', '3', 'V', 'T', 'A', '1', '\0'};
  if (!data || size < 20 || std::memcmp(data, kMagic, 8) != 0) return false;
  uint32_t w, h, n;
  std::memcpy(&w, data + 8, 4); std::memcpy(&h, data + 12, 4); std::memcpy(&n, data + 16, 4);
  if (!w || !h || w > kMaxSize || h > kMaxSize || !n || n > kMaxLevels) return false;
  Mask m; m.width = w; m.height = h;
  uint64_t off = 20;
  for (uint32_t l = 0; l < n; ++l) {
    const uint32_t lw = (w >> l) ? (w >> l) : 1, lh = (h >> l) ? (h >> l) : 1;
    const uint64_t bytes = uint64_t((lw + 3) / 4) * ((lh + 3) / 4) * 8;
    if (off + bytes > size) return false;
    m.levels.push_back({lw, lh, off, bytes});
    off += bytes;
    if (lw == 1 && lh == 1 && l + 1 < n) return false;   // more levels than the chain has
  }
  if (off != size) return false;
  *out = std::move(m);
  return true;
}

// The record a mask replaces: a VT record (kind 1) with the same tile set and the same uv transform bits.
inline bool matches(const IndexEntry& e, uint32_t kind, const float p0[4], float tileset) {
  if (kind != 1 || !(tileset >= 0.0f) || tileset != float(uint32_t(tileset)) || uint32_t(tileset) != e.tileset) return false;
  for (int i = 0; i < 4; ++i) {
    uint32_t bits; std::memcpy(&bits, &p0[i], 4);
    if (bits != e.p0[i]) return false;
  }
  return true;
}

}  // namespace bg3::vt_alpha
