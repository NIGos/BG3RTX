// common/hair_groom_asset.h: index-only content ID (shared fixture with tests/hair_groom_test.py) and
// the .hgrm parser's range checks.
#include "../common/hair_groom_asset.h"
#include <cstdio>
#include <cstdlib>
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

  Asset a;
  auto b = asset();
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
  std::printf("hair_groom_asset_test: all passed\n");
  return 0;
}
