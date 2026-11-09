// bg3rtx_hairconv input (.hcap, tools/hair_groom/hcap.py) and output (.hgrm v3, tools/hair_groom/storage.py encode_v3,
// read by common/hair_groom_v3.h).
#include "hairconv.h"
#include "../../common/hair_groom_v3.h"
#include "../../common/hair_capture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <compressapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <thread>

#pragma comment(lib, "Cabinet.lib")

namespace hairconv {

namespace {
bool read_all(const std::string& path, std::vector<uint8_t>& out) {
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
  _fseeki64(f, 0, SEEK_END);
  const long long n = _ftelli64(f);
  _fseeki64(f, 0, SEEK_SET);
  out.resize(size_t(std::max(0LL, n)));
  const bool ok = n >= 0 && fread(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
}  // namespace

bool read_hcap(const std::string& path, Input& in, std::string* error) {
  std::vector<uint8_t> raw;
  auto fail = [&](const char* why) { if (error) *error = path + ": " + why; return false; };
  if (!read_all(path, raw)) return fail("unreadable");
  // v1: tools/hair_groom/hcap.py (RGBA8 mask). v2: the layer's capture (common/hair_capture.h: mask format, BC3 blocks).
  if (raw.size() < 8 || std::memcmp(raw.data(), "HCAP", 4) != 0) return fail("not an .hcap");
  const uint32_t version = rd<uint32_t>(&raw[4]);
  if (version != 1 && version != 2) return fail("unknown .hcap version");
  const size_t kHead = version == 1 ? 4 + 8 * 4 + 8 + 6 * 4 : bg3::hair_capture::kHeaderBytes;
  if (raw.size() < kHead) return fail("truncated");
  const uint32_t n = rd<uint32_t>(&raw[8]), v = rd<uint32_t>(&raw[12]), w = rd<uint32_t>(&raw[16]), h = rd<uint32_t>(&raw[20]);
  const uint32_t flags = rd<uint32_t>(&raw[24]);
  in.grid[0] = int(rd<uint32_t>(&raw[28]));
  in.grid[1] = int(rd<uint32_t>(&raw[32]));
  in.material = rd<uint64_t>(&raw[36]);
  for (int k = 0; k < 3; ++k) {
    in.head.centre[k] = rd<float>(&raw[44 + 4 * k]);
    in.head.radii[k] = rd<float>(&raw[56 + 4 * k]);
  }
  in.has_head = flags & 1;
  in.has_grid = (flags & 2) && in.grid[0] > 0 && in.grid[1] > 0;
  if (!in.has_head) in.head = Head{};
  uint32_t mask_format = bg3::hair_capture::kMaskRgba8;
  uint64_t mask_bytes = uint64_t(w) * h * 4;
  if (version == 2) {
    mask_format = rd<uint32_t>(&raw[68]);
    mask_bytes = rd<uint32_t>(&raw[72]);
    in.stretch[0] = rd<float>(&raw[76]); in.stretch[1] = rd<float>(&raw[80]); in.stretch[2] = rd<float>(&raw[84]);
    const uint64_t want = mask_format == bg3::hair_capture::kMaskBc3 ? bg3::hair_capture::bc3_bytes(w, h) : uint64_t(w) * h * 4;
    if (mask_format > bg3::hair_capture::kMaskBc3 || mask_bytes != want) return fail("mask size does not match its format");
  }
  const uint64_t need = kHead + uint64_t(n) * 4 + uint64_t(v) * 20 + mask_bytes;
  if (raw.size() != need || !n || n % 3 || !v || !w || !h) return fail("sizes do not match");
  size_t at = kHead;
  in.mesh.tris.resize(n);
  std::memcpy(in.mesh.tris.data(), &raw[at], size_t(n) * 4); at += size_t(n) * 4;
  in.mesh.pos.resize(size_t(v) * 3);
  std::memcpy(in.mesh.pos.data(), &raw[at], size_t(v) * 12); at += size_t(v) * 12;
  in.mesh.uv.resize(size_t(v) * 2);
  std::memcpy(in.mesh.uv.data(), &raw[at], size_t(v) * 8); at += size_t(v) * 8;
  in.mask.w = int(w); in.mask.h = int(h);
  if (mask_format == bg3::hair_capture::kMaskBc3) bg3::hair_capture::decode_bc3(&raw[at], w, h, in.mask.rgba);
  else in.mask.rgba.assign(raw.begin() + at, raw.end());
  for (uint32_t i : in.mesh.tris)
    if (i >= v) return fail("index out of range");
  return true;
}

namespace {
uint64_t fnv_words(const std::vector<uint32_t>& words) {
  uint64_t h = 14695981039346656037ull;
  for (uint32_t w : words)
    for (int b = 0; b < 4; ++b) h = (h ^ uint8_t(w >> (8 * b))) * 1099511628211ull;
  return h ? h : 1;
}
}  // namespace

uint64_t content_id(const Mesh& m) {
  const size_t count = m.tris.size();
  uint32_t lo = UINT32_MAX, hi = 0;
  for (uint32_t i : m.tris) { lo = std::min(lo, i); hi = std::max(hi, i); }
  std::vector<uint32_t> words = {1, hi - lo + 1, uint32_t(count)};
  for (uint32_t s = 0; s < bg3::hair_groom::kSamples; ++s) {
    const uint32_t v = m.tris[size_t(s) * (count - 1) / (bg3::hair_groom::kSamples - 1)];
    words.push_back(v - lo);
    for (int a = 0; a < 3; ++a) { uint32_t bits; std::memcpy(&bits, &m.pos[size_t(v) * 3 + a], 4); words.push_back(bits); }
  }
  return fnv_words(words);
}

uint64_t index_id(const Mesh& m) {
  return bg3::hair_groom::index_content_id([&](uint32_t i) { return m.tris[i]; }, uint32_t(m.tris.size()));
}

// ---------------------------------------------------------------- v3 writer
namespace {
constexpr double kBaryPad = 1.0 / 512;
constexpr int kLogSteps = 2048;
constexpr double kLogMin = -32.0;
const double kPixel = 2 * 0.6 * std::tan(10.0 * 3.14159265358979323846 / 180.0) / 2160;
const double kThreshold = kPixel / 255;

uint32_t quant_bary(float b, int bits) {
  const double scale = double((1u << bits) - 1);
  const double x = (double(b) + kBaryPad) / (1 + 2 * kBaryPad);
  return uint32_t(std::clamp(std::nearbyint(x * scale), 0.0, scale));
}
float dequant_bary(uint32_t q, int bits) {
  return float(double(q) / double((1u << bits) - 1) * (1 + 2 * kBaryPad) - kBaryPad);
}
uint16_t quant_radius(float r) {
  const double code = std::nearbyint((std::log2(double(r)) - kLogMin) * kLogSteps);
  return uint16_t(std::clamp(code, 0.0, 65535.0));
}

int bits_for(const Mesh& m, const Groom& g) {
  std::vector<char> mark(m.triangles(), 0);
  for (uint32_t t : g.triangle) mark[t] = 1;
  double reach = 0;
  for (uint32_t t = 0; t < m.triangles(); ++t) {
    if (!mark[t]) continue;
    const float* p0 = &m.pos[size_t(m.tris[t * 3]) * 3];
    const float* p1 = &m.pos[size_t(m.tris[t * 3 + 1]) * 3];
    const float* p2 = &m.pos[size_t(m.tris[t * 3 + 2]) * 3];
    double e1 = 0, e2 = 0;
    for (int a = 0; a < 3; ++a) {
      const double d1 = double(p1[a]) - p0[a], d2 = double(p2[a]) - p0[a];
      e1 += d1 * d1; e2 += d2 * d2;
    }
    reach = std::max(reach, std::sqrt(e1) + std::sqrt(e2));
  }
  const double need = reach * (1 + 2 * kBaryPad) / (2 * kThreshold);
  return int(std::min(std::max(std::ceil(std::log2(need + 1)), 8.0), 24.0));
}

bool degenerate(const Groom& g, const std::vector<uint32_t>& offsets, int bits) {
  for (size_t s = 0; s + 1 < offsets.size(); ++s)
    for (uint32_t i = offsets[s]; i + 1 < offsets[s + 1]; ++i)
      if (g.triangle[i] == g.triangle[i + 1] &&
          dequant_bary(quant_bary(g.bary[size_t(i) * 3 + 1], bits), bits) == dequant_bary(quant_bary(g.bary[size_t(i + 1) * 3 + 1], bits), bits) &&
          dequant_bary(quant_bary(g.bary[size_t(i) * 3 + 2], bits), bits) == dequant_bary(quant_bary(g.bary[size_t(i + 1) * 3 + 2], bits), bits))
        return true;
  return false;
}

// Byte planes of `width`-byte little-endian values.
void shuffle(const std::vector<uint64_t>& z, int width, std::vector<uint8_t>& out) {
  const size_t n = z.size();
  out.resize(n * width);
  for (int b = 0; b < width; ++b)
    for (size_t i = 0; i < n; ++i) out[size_t(b) * n + i] = uint8_t(z[i] >> (8 * b));
}
inline uint64_t zigzag(int64_t d) { return uint64_t((d << 1) ^ (d >> 63)); }

bool lzms(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
  out.clear();
  if (in.empty()) return true;
  COMPRESSOR_HANDLE h = nullptr;
  if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) return false;
  out.resize(in.size() + in.size() / 2 + (1 << 20));
  SIZE_T n = 0;
  const bool ok = Compress(h, in.data(), in.size(), out.data(), out.size(), &n);
  CloseCompressor(h);
  out.resize(ok ? n : 0);
  return ok;
}
void put(std::vector<uint8_t>& b, const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
template <class T> void put(std::vector<uint8_t>& b, T v) { put(b, &v, sizeof v); }
}  // namespace

int write_v3(const std::string& path, const Mesh& m, const Groom& g0, std::string* error) {
  auto fail = [&](const std::string& why) { if (error) *error = why; return 0; };
  const float keeps[3] = {1.0f, 0.5f, 0.25f};
  Groom lods[3] = {g0, thin(g0, 0.5f), thin(g0, 0.25f)};
  for (const Groom& l : lods)
    for (float r : l.radii)
      if (!(r > 0 && r < 0.01f)) return fail("radius outside (0, 10 mm): the layer would reject the file");
  const uint32_t P = g0.points_count(), S = g0.strands();
  if (!S) return fail("no strands");
  int bits = bits_for(m, g0);
  while (bits < 24 && degenerate(g0, g0.offsets, bits)) ++bits;
  // Streams of LOD 0 (storage.v3_streams).
  std::vector<std::vector<uint8_t>> streams(7);
  std::vector<uint64_t> z(P);
  for (uint32_t s = 0; s < S; ++s) {
    const uint32_t len = g0.offsets[s + 1] - g0.offsets[s];
    if (len > 65535) return fail("strand longer than 65535 points");
    streams[0].push_back(uint8_t(std::min<uint32_t>(len, 255)));
    if (len >= 255) put<uint16_t>(streams[1], uint16_t(len));
  }
  auto delta = [&](auto value, auto wrap) {
    for (uint32_t s = 0; s < S; ++s) {
      int64_t prev = 0;
      for (uint32_t i = g0.offsets[s]; i < g0.offsets[s + 1]; ++i) {
        const int64_t v = int64_t(value(i));
        z[i] = wrap(v - prev);
        prev = v;
      }
    }
  };
  delta([&](uint32_t i) { return g0.triangle[i]; }, [](int64_t d) { return zigzag(d); });
  shuffle(z, 4, streams[2]);
  const int width = bits <= 16 ? 2 : 4;
  for (int k = 0; k < 2; ++k) {
    const int64_t half = int64_t(1) << (bits - 1), mod = int64_t(1) << bits;
    delta([&](uint32_t i) { return quant_bary(g0.bary[size_t(i) * 3 + 1 + k], bits); },
          [&](int64_t d) { int64_t w = ((d + half) % mod + mod) % mod - half; return zigzag(w); });
    shuffle(z, width, streams[3 + k]);
  }
  delta([&](uint32_t i) { return quant_radius(g0.radii[i]); },
        [](int64_t d) { int64_t w = ((d + 32768) % 65536 + 65536) % 65536 - 32768; return zigzag(w); });
  shuffle(z, 2, streams[5]);
  for (uint32_t s = 0; s < S; ++s) {
    int prev = 0;
    for (uint32_t i = g0.offsets[s]; i < g0.offsets[s + 1]; ++i) {
      const int v = int(std::clamp(std::nearbyint(g0.root[i] * 255.0f), 0.0f, 255.0f));
      streams[6].push_back(uint8_t((v - prev) & 0xFF));
      prev = v;
    }
  }
  // Header (asset.write: v2 header + UV samples, version 3), LOD table, codec words, streams.
  std::vector<uint8_t> out;
  put(out, "HGRM", 4);
  put<uint32_t>(out, 3);
  put<uint64_t>(out, content_id(m));
  put<uint64_t>(out, index_id(m));
  put<uint32_t>(out, m.vertices());
  put<uint32_t>(out, uint32_t(m.tris.size()));
  put<uint32_t>(out, 3);
  put<uint32_t>(out, 0);
  const size_t count = m.tris.size();
  for (uint32_t s = 0; s < bg3::hair_groom::kSamples; ++s) {
    const uint32_t v = m.tris[size_t(s) * (count - 1) / (bg3::hair_groom::kSamples - 1)];
    put<float>(out, m.uv[size_t(v) * 2]);
    put<float>(out, m.uv[size_t(v) * 2 + 1]);
  }
  for (int k = 0; k < 3; ++k) {
    put<float>(out, keeps[k]);
    put<uint32_t>(out, lods[k].points_count());
    put<uint32_t>(out, lods[k].points_count() - lods[k].strands());
    put<uint32_t>(out, 0);
  }
  put<uint32_t>(out, 1);
  put<uint32_t>(out, uint32_t(bits));
  put<uint32_t>(out, S);
  put<uint32_t>(out, 7);
  // The 7 streams compress independently: one thread each (LZMS dominates the write time).
  std::vector<std::vector<uint8_t>> packed(streams.size());
  std::vector<char> ok(streams.size(), 0);
  {
    std::vector<std::thread> pool;
    for (size_t k = 0; k < streams.size(); ++k) pool.emplace_back([&, k] { ok[k] = lzms(streams[k], packed[k]); });
    for (auto& t : pool) t.join();
  }
  for (size_t k = 0; k < streams.size(); ++k) {
    if (!ok[k]) return fail("LZMS compression failed");
    put<uint64_t>(out, streams[k].size());
    put<uint64_t>(out, packed[k].size());
    put(out, packed[k].data(), packed[k].size());
  }
  const std::string tmp = path + "." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetCurrentThreadId()) + ".tmp";
  FILE* f = nullptr;
  if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return fail("cannot write " + tmp);
  const bool written = fwrite(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  if (!written || !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileA(tmp.c_str());
    return fail("cannot replace " + path);
  }
  return bits;
}

}  // namespace hairconv
