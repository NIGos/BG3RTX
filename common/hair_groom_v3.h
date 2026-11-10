#pragma once
// .hgrm version 3 (docs/RTX_HAIR_ROADMAP.md 2b; writer and reference decoder: tools/hair_groom/storage.py encode_v3 /
// decode_v3). The header and UV samples are v2's; then the LOD table (v2's 16-byte LOD headers: the layer's
// index-time segment counts), then LOD 0 as 7 separately compressed streams. The thinned LODs are rebuilt here
// exactly as convert.thin builds them. decode_v3_lod returns one LOD in the v2 LOD layout, so parse_lod validates
// it as before. Decompression is a callback (common/hair_groom_lzms.h: the Windows Compression API's LZMS), so this
// header stays platform-free and testable.
#include "hair_groom_asset.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace bg3::hair_groom {

constexpr uint32_t kV3 = 3, kV3CodecLzms = 1, kV3Streams = 7, kV3MaxBits = 24;
constexpr uint64_t kV3HeaderBytes = 40 + kSamples * 8;   // header + UV samples, as v2
constexpr double kV3BaryPad = 1.0 / 512, kV3LogSteps = 2048, kV3LogMin = -32;
constexpr uint64_t kThinSeed = 0x9E3779B9ull, kThinAdd = 0x632BE59BD9B4E019ull;
constexpr float kMaxThinRadius = 0.009f;   // convert.MAX_THIN_RADIUS

// bool(src, stored bytes, dst, raw bytes): dst receives exactly `raw` bytes.
using V3Inflate = bool (*)(const uint8_t*, size_t, uint8_t*, size_t);

struct V3Table {
  uint32_t lods = 0;
  float keep[8] = {};
  uint32_t points[8] = {}, segments[8] = {};
  uint64_t streams_at = 0;   // the codec words after the table
};

// The LOD table of a v3 file whose first `size` bytes are given (at least kV3HeaderBytes + 16 x lods).
inline bool v3_table(const uint8_t* bytes, uint64_t size, uint32_t lods, V3Table& t) {
  if (!lods || lods > 8 || size < kV3HeaderBytes + 16ull * lods) return false;
  t.lods = lods;
  uint64_t at = kV3HeaderBytes, next = 0;
  for (uint32_t k = 0; k < lods; ++k, at += 16) {
    std::memcpy(&t.keep[k], bytes + at, 4);
    if (!lod_header(bytes + at, at, &t.points[k], &t.segments[k], &next)) return false;
    if (k && (t.points[k] > t.points[0] || !(t.keep[k] < t.keep[k - 1]))) return false;
  }
  if (t.keep[0] != 1.0f) return false;
  t.streams_at = at;
  return true;
}

namespace v3detail {
inline int64_t unzigzag(uint64_t z) { return int64_t(z >> 1) ^ -int64_t(z & 1); }
// The value of byte-plane-shuffled `width`-byte little-endian element i.
inline uint64_t plane(const std::vector<uint8_t>& s, uint32_t width, uint64_t n, uint64_t i) {
  uint64_t v = 0;
  for (uint32_t b = 0; b < width; ++b) v |= uint64_t(s[b * n + i]) << (8 * b);
  return v;
}
}  // namespace v3detail

// rt_hair_pixel_lod (docs/RTX_HAIR_PRIMARY.md 5v): keeps below the stored table, rebuilt at decode time the same nested
// way as the table's thinned LODs (convert.thin: a strand kept at 1/8 is kept at 1/4, 1/2 and 1), radii widened by
// 1 / keep under kMaxThinRadius. No file changes: the 888 v3 files are read as they are.
constexpr float kV3ExtraKeeps[3] = {1.0f / 8, 1.0f / 16, 1.0f / 32};

// LOD `index` of a whole v3 file as v2 LOD bytes (16-byte header + triangle, bary, radius, root, segment arrays).
// False on anything malformed (sizes, codec, stream lengths, a strand length that does not add up).
// extra_keep > 0 (5v): instead of the table's LOD `index` (pass 0), the strands kept at that fraction; the table has no
// counts for it, so only a non-empty result is required.
inline bool decode_v3_lod(const uint8_t* bytes, uint64_t size, uint32_t index, V3Inflate inflate, std::vector<uint8_t>& out,
                          float extra_keep = 0.0f) {
  using namespace v3detail;
  out.clear();
  Asset h;
  uint64_t hb = 0;
  if (!inflate || !parse_header(bytes, size, h, &hb) || h.version != kV3 || hb != kV3HeaderBytes) return false;
  V3Table t;
  if (index >= h.lod_count || !v3_table(bytes, size, h.lod_count, t)) return false;
  const bool extra = extra_keep > 0.0f;
  if (extra && (index || !(extra_keep < 1.0f))) return false;
  uint64_t at = t.streams_at;
  if (size - at < 16) return false;
  uint32_t w[4];
  std::memcpy(w, bytes + at, 16);
  at += 16;
  const uint32_t codec = w[0], bits = w[1], strands = w[2], nstreams = w[3];
  const uint64_t P = t.points[0];
  if (codec != kV3CodecLzms || bits < 8 || bits > kV3MaxBits || nstreams != kV3Streams || !strands || strands > P ||
      t.segments[0] != P - strands)
    return false;
  std::vector<uint8_t> s[kV3Streams];
  for (uint32_t k = 0; k < kV3Streams; ++k) {
    uint64_t raw, stored;
    if (size - at < 16) return false;
    std::memcpy(&raw, bytes + at, 8);
    std::memcpy(&stored, bytes + at + 8, 8);
    at += 16;
    if (stored > size - at || raw > (uint64_t(1) << 31)) return false;
    s[k].resize(size_t(raw));
    if (raw && !inflate(bytes + at, size_t(stored), s[k].data(), size_t(raw))) return false;
    at += stored;
  }
  const uint32_t width = bits <= 16 ? 2 : 4;
  if (at != size || s[0].size() != strands || s[1].size() % 2 || s[2].size() != P * 4 || s[3].size() != P * width ||
      s[4].size() != P * width || s[5].size() != P * 2 || s[6].size() != P)
    return false;
  // Strand lengths (255 = the next u16 escape).
  std::vector<uint32_t> len(strands);
  uint64_t esc = 0, total = 0;
  for (uint32_t i = 0; i < strands; ++i) {
    uint32_t n = s[0][i];
    if (n == 255) {
      if (esc * 2 + 2 > s[1].size()) return false;
      n = uint32_t(s[1][esc * 2]) | uint32_t(s[1][esc * 2 + 1]) << 8;
      ++esc;
    }
    if (n < 2) return false;
    len[i] = n;
    total += n;
  }
  if (total != P || esc * 2 != s[1].size()) return false;
  // LOD 0, delta-decoded along each strand.
  std::vector<uint32_t> tri(P);
  std::vector<float> bary(P * 2), rad(P);
  std::vector<uint8_t> root(P);
  const uint64_t mask = (uint64_t(1) << bits) - 1;
  const double scale = double(mask);
  uint64_t p = 0;
  for (uint32_t i = 0; i < strands; ++i) {
    int64_t pt = 0;
    uint64_t pb[2] = {0, 0}, pr = 0;
    uint8_t po = 0;
    for (uint32_t j = 0; j < len[i]; ++j, ++p) {
      pt += unzigzag(plane(s[2], 4, P, p));
      if (pt < 0 || pt >= int64_t(h.indices / 3)) return false;
      tri[p] = uint32_t(pt);
      for (int k = 0; k < 2; ++k) {
        pb[k] = uint64_t(int64_t(pb[k]) + unzigzag(plane(s[3 + k], width, P, p))) & mask;
        bary[p * 2 + k] = float(double(pb[k]) / scale * (1.0 + 2.0 * kV3BaryPad) - kV3BaryPad);
      }
      pr = uint64_t(int64_t(pr) + unzigzag(plane(s[5], 2, P, p))) & 0xFFFF;
      rad[p] = float(std::exp2(double(pr) / kV3LogSteps + kV3LogMin));
      po = uint8_t(po + s[6][p]);
      root[p] = po;
    }
  }
  // The requested LOD: convert.thin's strand selection (nested, deterministic), radii widened by 1 / keep.
  const float keep = extra ? extra_keep : t.keep[index];
  const bool thinned = index || extra;
  std::vector<uint8_t> sel(strands, 1);
  if (thinned) {
    for (uint32_t i = 0; i < strands; ++i) {
      const uint64_t hsh = (uint64_t(i) * kThinSeed + kThinAdd) & 0xFFFFFFFFull;
      sel[i] = double(hsh) / 4294967296.0 < double(keep);
    }
  }
  uint64_t pts = 0, segs = 0;
  for (uint32_t i = 0; i < strands; ++i)
    if (sel[i]) { pts += len[i]; segs += len[i] - 1; }
  if (extra ? !segs : (pts != t.points[index] || segs != t.segments[index])) return false;
  out.resize(size_t(16 + pts * 17 + segs * 4));
  uint8_t* o = out.data();
  const uint32_t hdr[4] = {0, uint32_t(pts), uint32_t(segs), 0};
  std::memcpy(o, hdr, 16);
  std::memcpy(o, &keep, 4);
  uint8_t* ot = o + 16;
  uint8_t* ob = ot + pts * 4;
  uint8_t* orad = ob + pts * 8;
  uint8_t* oroot = orad + pts * 4;
  uint8_t* oseg = oroot + pts;
  uint64_t src = 0, q = 0;
  for (uint32_t i = 0; i < strands; ++i) {
    if (!sel[i]) { src += len[i]; continue; }
    for (uint32_t j = 0; j < len[i]; ++j, ++src, ++q) {
      std::memcpy(ot + q * 4, &tri[src], 4);
      std::memcpy(ob + q * 8, &bary[src * 2], 8);
      float r = rad[src];
      if (thinned) r = std::fmin(r / keep, kMaxThinRadius);
      std::memcpy(orad + q * 4, &r, 4);
      oroot[q] = root[src];
      if (j + 1 < len[i]) {
        const uint32_t first = uint32_t(q);
        std::memcpy(oseg, &first, 4);
        oseg += 4;
      }
    }
  }
  return true;
}

}  // namespace bg3::hair_groom
