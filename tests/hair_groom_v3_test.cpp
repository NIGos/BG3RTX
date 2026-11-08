// common/hair_groom_v3.h: the C++ decoder of .hgrm v3 against the Python reference (tools/hair_groom/storage.py).
//   hair_groom_v3_test <fixture dir>               tests/fixtures/hair_v3: groom.v3.hgrm vs groom.ref.hgrm (v2 layout)
//   hair_groom_v3_test <fixture dir> --dir <dir>   also every v3 file in <dir>: decode time per style, and against
//                                                  <name>.ref (written by the converter dry run) when present
// Quantisation bound: the decoder must give the reference's arrays (triangle, root, segments exact; barycentrics
// and radius within 1e-7 / 1e-6 relative: both come from the same float64 expressions, only exp2 may differ by an ulp).
#include "../common/hair_groom_v3.h"
#include "../common/hair_groom_lzms.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace bg3::hair_groom;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static std::vector<uint8_t> load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static double g_inflate_ms = 0;
static bool timed_inflate(const uint8_t* src, size_t stored, uint8_t* dst, size_t raw) {
  const auto t0 = std::chrono::steady_clock::now();
  const bool ok = lzms_inflate(src, stored, dst, raw);
  g_inflate_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return ok;
}

// Every LOD of `v3` decoded and compared with the v2 reference file `ref` (empty: decode only). Returns ms.
static double check_file(const std::vector<uint8_t>& v3, const std::vector<uint8_t>& ref, const char* name) {
  Asset h;
  uint64_t hb = 0;
  CHECK(parse_header(v3.data(), v3.size(), h, &hb) && h.version == 3 && h.has_uv);
  Asset r;
  const bool with_ref = !ref.empty();
  if (with_ref) CHECK(parse(ref.data(), ref.size(), r) && r.lods.size() == h.lod_count && r.index_id == h.index_id);
  double ms = 0;
  for (uint32_t k = 0; k < h.lod_count; ++k) {
    std::vector<uint8_t> bytes;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = decode_v3_lod(v3.data(), v3.size(), k, timed_inflate, bytes);
    ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!ok) std::printf("FAIL decode %s LOD %u\n", name, k);
    CHECK(ok);
    Lod l;
    uint64_t at = 0;
    CHECK(parse_lod(bytes.data(), bytes.size(), at, h.indices / 3, l) && at == bytes.size());
    if (!with_ref) continue;
    const Lod& e = r.lods[k];
    CHECK(l.points == e.points && l.segments == e.segments && l.keep == e.keep);
    double bary = 0, rad = 0;
    for (uint32_t i = 0; i < l.points; ++i) {
      CHECK(l.triangle[i] == e.triangle[i] && l.root[i] == e.root[i]);
      bary = std::fmax(bary, std::fabs(double(l.bary[2 * i]) - e.bary[2 * i]));
      bary = std::fmax(bary, std::fabs(double(l.bary[2 * i + 1]) - e.bary[2 * i + 1]));
      rad = std::fmax(rad, std::fabs(double(l.radius[i]) / e.radius[i] - 1));
    }
    for (uint32_t i = 0; i < l.segments; ++i) CHECK(l.segment[i] == e.segment[i]);
    if (bary > 1e-7 || rad > 1e-6) std::printf("FAIL %s LOD %u: bary %.3g radius %.3g\n", name, k, bary, rad);
    CHECK(bary <= 1e-7 && rad <= 1e-6);
  }
  return ms;
}

int main(int argc, char** argv) {
  CHECK(argc >= 2);
  const std::string fx = argv[1];
  const auto v3 = load(fx + "/groom.v3.hgrm"), ref = load(fx + "/groom.ref.hgrm");
  CHECK(!v3.empty() && !ref.empty());
  check_file(v3, ref, "fixture");
  // Malformed inputs are refused, never read out of range.
  std::vector<uint8_t> bytes;
  for (size_t cut : {size_t(40), size_t(300), v3.size() / 2, v3.size() - 1}) {
    std::vector<uint8_t> t(v3.begin(), v3.begin() + cut);
    CHECK(!decode_v3_lod(t.data(), t.size(), 0, lzms_inflate, bytes));
  }
  {
    std::vector<uint8_t> t = v3;
    t.push_back(0);   // trailing byte
    CHECK(!decode_v3_lod(t.data(), t.size(), 0, lzms_inflate, bytes));
    t = v3;
    t[kV3HeaderBytes + 16 * 3] = 9;   // unknown codec
    CHECK(!decode_v3_lod(t.data(), t.size(), 0, lzms_inflate, bytes));
    t = v3;
    t[kV3HeaderBytes + 16 * 3 + 4] = 30;   // barycentric bits out of range
    CHECK(!decode_v3_lod(t.data(), t.size(), 0, lzms_inflate, bytes));
    t = v3;
    t[kV3HeaderBytes + 4] ^= 1;   // LOD 0 points no longer match the strands
    CHECK(!decode_v3_lod(t.data(), t.size(), 0, lzms_inflate, bytes));
    CHECK(!decode_v3_lod(v3.data(), v3.size(), 3, lzms_inflate, bytes));   // no such LOD
    Asset a;
    CHECK(!parse(v3.data(), v3.size(), a));   // the v2 parser refuses v3
  }
  // Real grooms (the converter's dry run output): decode time per style and the reference comparison.
  for (int i = 2; i + 1 < argc; ++i) {
    if (std::string(argv[i]) != "--dir") continue;
    const std::string dir = argv[i + 1];
    WIN32_FIND_DATAA fd{};
    HANDLE f = FindFirstFileA((dir + "\\*.hgrm").c_str(), &fd);
    CHECK(f != INVALID_HANDLE_VALUE);
    int n = 0, refs = 0;
    double total = 0, worst = 0;
    do {
      const std::string path = dir + "\\" + fd.cFileName;
      const auto file = load(path), r = load(path + ".ref");
      Asset h;
      if (!parse_header(file.data(), file.size(), h) || h.version != 3) continue;
      g_inflate_ms = 0;
      const double ms = check_file(file, r, fd.cFileName);
      std::printf("%s: %zu bytes, %u LODs decoded in %.1f ms (LZMS %.1f ms)%s\n", fd.cFileName, file.size(), h.lod_count, ms,
                  g_inflate_ms, r.empty() ? "" : ", equal to the Python reference");
      ++n;
      refs += !r.empty();
      total += ms;
      worst = std::fmax(worst, ms);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    CHECK(n > 0);
    std::printf("%d v3 grooms (%d with a reference): decode %.1f ms per style mean, %.1f ms max (all LODs)\n", n, refs,
                total / n, worst);
  }
  std::printf("hair groom v3: fixture decodes to the Python reference, malformed files refused\n");
  return 0;
}
