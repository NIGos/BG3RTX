// common/hair_groom_asset.h: index-only content ID (shared fixture with tests/hair_groom_test.py) and
// the .hgrm parser's range checks.
#include "../common/hair_groom_asset.h"
#include "../common/hair_groom_budget.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
using namespace bg3::hair_groom;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static void put(std::vector<uint8_t>& b, const void* p, size_t n) {
  const auto* c = static_cast<const uint8_t*>(p);
  b.insert(b.end(), c, c + n);
}
template <class T> static void put(std::vector<uint8_t>& b, T v) { put(b, &v, sizeof v); }

// One card of 2 triangles (6 indices), one LOD: 3 points on triangle 1, 2 segments.
static std::vector<uint8_t> asset(float radius = 0.0001f, uint32_t tri = 1, uint32_t last_segment = 1) {
  std::vector<uint8_t> b;
  put(b, "HGRM", 4);
  put<uint32_t>(b, 1); put<uint64_t>(b, 7); put<uint64_t>(b, 9);
  put<uint32_t>(b, 4); put<uint32_t>(b, 6); put<uint32_t>(b, 1); put<uint32_t>(b, 0);
  put<float>(b, 1.0f); put<uint32_t>(b, 3); put<uint32_t>(b, 2); put<uint32_t>(b, 0);
  for (int i = 0; i < 3; ++i) put<uint32_t>(b, tri);
  for (int i = 0; i < 3; ++i) { put<float>(b, 0.25f); put<float>(b, 0.25f); }
  for (int i = 0; i < 3; ++i) put<float>(b, radius);
  for (int i = 0; i < 3; ++i) put<uint8_t>(b, uint8_t(i * 100));
  put<uint32_t>(b, 0); put<uint32_t>(b, last_segment);
  return b;
}

int main() {
  const uint32_t quad[6] = {0, 1, 2, 0, 2, 3};
  CHECK(index_content_id([&](uint32_t i) { return quad[i]; }, 6) == 0xce53baad8d797436ull);
  // Offsets and index widths do not matter, only the referenced pattern.
  const uint32_t moved[6] = {10, 11, 12, 10, 12, 13};
  CHECK(index_content_id([&](uint32_t i) { return moved[i]; }, 6) == 0xce53baad8d797436ull);
  const uint32_t other[6] = {0, 1, 2, 0, 3, 2};
  CHECK(index_content_id([&](uint32_t i) { return other[i]; }, 6) != 0xce53baad8d797436ull);
  CHECK(index_content_id([&](uint32_t i) { return quad[i]; }, 4) == 0);

  // Join verdicts, in the order the layer tests them.
  CHECK(join_miss(false, true, true, true) == JoinMiss::NotIndexed);
  CHECK(join_miss(true, false, true, true) == JoinMiss::CountUnknown);
  CHECK(join_miss(true, true, false, false) == JoinMiss::NotHostMapped);
  CHECK(join_miss(true, true, true, false) == JoinMiss::NoFile);
  CHECK(join_miss(true, true, true, true) == JoinMiss::None);
  for (int m = 0; m < int(JoinMiss::Count); ++m) CHECK(std::string(join_miss_name(JoinMiss(m))) != "?");
  // UV join: exact, within tolerance, v flipped; a different sample or NaN does not match.
  float want[64], got[64];
  for (int i = 0; i < 64; ++i) want[i] = got[i] = 0.01f * float(i % 50);
  CHECK(uv_samples_match(got, want));
  got[5] += 5e-4f;
  CHECK(uv_samples_match(got, want));
  for (int s = 0; s < 32; ++s) got[2 * s + 1] = 1.0f - want[2 * s + 1];
  CHECK(uv_samples_match(got, want));
  got[10] += 0.01f;
  CHECK(!uv_samples_match(got, want));
  for (int i = 0; i < 64; ++i) got[i] = want[i];
  got[0] = std::nanf("");
  CHECK(!uv_samples_match(got, want));
  CHECK(uv_sample_corner(0, 6) == 0 && uv_sample_corner(31, 6) == 5 && uv_sample_corner(16, 120270) == 62074);

  Asset a;
  auto b = asset();
  CHECK(parse(b.data(), b.size(), a) && !a.has_uv);
  {   // v2: the same file with 64 UV floats after the header
    std::vector<uint8_t> v2(b.begin(), b.begin() + 40);
    const uint32_t two = 2;
    std::memcpy(v2.data() + 4, &two, 4);
    for (int i = 0; i < 64; ++i) put<float>(v2, 0.25f);
    v2.insert(v2.end(), b.begin() + 40, b.end());
    CHECK(parse(v2.data(), v2.size(), a) && a.has_uv && a.uv[63] == 0.25f && a.lods.size() == 1);
    put<float>(v2, 0.0f);
    CHECK(!parse(v2.data(), v2.size(), a));
  }
  CHECK(parse(b.data(), b.size(), a));
  CHECK(a.index_id == 9 && a.position_id == 7 && a.indices == 6 && a.lods.size() == 1);
  CHECK(a.lods[0].points == 3 && a.lods[0].segments == 2 && a.lods[0].root[2] == 200);
  uint32_t seg1;
  std::memcpy(&seg1, a.lods[0].segment + 1, 4);
  CHECK(seg1 == 1);
  b.push_back(0);
  CHECK(!parse(b.data(), b.size(), a));                       // trailing bytes
  b = asset(0.02f);
  CHECK(!parse(b.data(), b.size(), a));                       // radius beyond a hair clump
  b = asset(0.0001f, 2);
  CHECK(!parse(b.data(), b.size(), a));                       // triangle out of range
  b = asset(0.0001f, 1, 2);
  CHECK(!parse(b.data(), b.size(), a));                       // segment past the last point
  b = asset();
  CHECK(!parse(b.data(), b.size() - 1, a));                   // truncated
  b[0] = 'X';
  CHECK(!parse(b.data(), b.size(), a));
  // Header-only index entry: ids, counts and the LOD count, no arrays.
  b = asset();
  CHECK(parse_header(b.data(), 40, a) && a.index_id == 9 && a.indices == 6 && a.lod_count == 1 && a.lods.empty());
  CHECK(!parse_header(b.data(), 39, a));

  // Pool budget: a quarter of the capacity; evictions oldest first, never pinned or recently used ones.
  CHECK(pool_budget(2047ull << 20) == (2047ull << 20) / 4);
  std::vector<uint64_t> ev;
  std::vector<Resident> res = {{1, 40, 10, false}, {2, 30, 5, false}, {3, 50, 990, false}, {4, 20, 1, true}};
  CHECK(plan_evictions(res, 140, 10, 200, 1000, 120, ev) && ev.empty());
  CHECK(plan_evictions(res, 140, 70, 200, 1000, 120, ev) && ev == std::vector<uint64_t>{2});
  CHECK(plan_evictions(res, 140, 100, 200, 1000, 120, ev) && (ev == std::vector<uint64_t>{2, 1}));
  CHECK(!plan_evictions(res, 140, 140, 200, 1000, 120, ev) && ev.empty());
  CHECK(!plan_evictions(res, 0, 201, 200, 1000, 120, ev));
  {   // crowd plan (common/hair_groom_budget.h plan_crowd)
    std::vector<CrowdCandidate> c(4);
    const uint32_t seg[3] = {855000, 451000, 236000};   // Shadowheart LOD 0, 1/2, 1/4 at 2 mm
    for (auto& k : c) { std::copy(seg, seg + 3, k.segments); k.lods = 3; }
    c[0].size = 0.2f; c[0].want = 0;   // close
    c[1].size = 0.1f; c[1].want = 0;
    c[2].size = 0.05f; c[2].want = 1;
    c[3].size = 0.01f; c[3].want = 2;   // far
    CHECK((plan_crowd(c, 0) == std::vector<int>{0, 0, 1, 2}));                 // off: every screen-size LOD
    CHECK((plan_crowd(c, 4000000) == std::vector<int>{0, 0, 1, 2}));           // enough for all
    CHECK((plan_crowd(c, 1600000) == std::vector<int>{0, 1, 2, -1}));          // the far one keeps its cards
    CHECK((plan_crowd(c, 900000) == std::vector<int>{0, -1, -1, -1}));         // never coarser than needed for the biggest
    CHECK((plan_crowd(c, 100000) == std::vector<int>{-1, -1, -1, -1}));        // nothing fits: cards
    std::swap(c[0], c[3]);                                                       // order comes from size, not position
    CHECK((plan_crowd(c, 1600000) == std::vector<int>{-1, 1, 2, 0}));
  }
  std::printf("hair_groom_asset_test: all passed\n");
  return 0;
}
