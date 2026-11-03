#include "../layer/mesh_content_id.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>
using namespace bg3::mesh_content;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static Sample get(const Input& in) { Sample s; CHECK(sample(in, s)); return s; }
int main() {
  std::array<float, 12> p{0,0,0, 1,0,0, 1,1,0, 0,1,0};
  std::array<uint16_t, 6> ix{0,1,2,0,2,3};
  Input in{{reinterpret_cast<const uint8_t*>(p.data()), sizeof p},
           {reinterpret_cast<const uint8_t*>(ix.data()), sizeof ix}, 12, 6, 2, 0};
  const auto a = get(in); const auto id = hash(a);
  CHECK(id == 0x5f673b88e4625698ull); // canonical v1 fixture for a future GR2 decoder
  // Same data, distinct buffers, index width, padding and vertex offset.
  std::vector<uint8_t> other(16 + 4 * 20, 0xcd);
  for (unsigned v = 0; v < 4; ++v) std::memcpy(other.data() + 16 + v * 20, p.data() + v * 3, 12);
  std::array<uint32_t, 6> wide{7,8,9,7,9,10};
  Input moved{{other.data() + 16, other.size() - 16},
              {reinterpret_cast<const uint8_t*>(wide.data()), sizeof wide},20,6,4,-7};
  CHECK(get(moved) == a); CHECK(hash(get(moved)) == id);
  std::array<uint8_t, 6> narrow{0,1,2,0,2,3};
  Input byte_indices = in; byte_indices.indices = {narrow.data(), narrow.size()}; byte_indices.index_bytes = 1;
  CHECK(get(byte_indices) == a);
  CHECK(a.words[1] == 4 && a.words[2] == 6);
  p[0] = 0.5f; CHECK(hash(get(in)) != id); p[0] = 0;
  ix[1] = 3; CHECK(hash(get(in)) != id); ix[1] = 1;
  const auto saved = in;
  in.count = 3; CHECK(hash(get(in)) != id); in = saved;
  in.indices.size = 2; Sample rejected; CHECK(!sample(in, rejected)); in = saved;
  in.positions.size = 10; CHECK(!sample(in, rejected)); in = saved;
  in.vertex_offset = -1; CHECK(!sample(in, rejected)); in = saved;
  in.index_bytes = 3; CHECK(!sample(in, rejected)); in = saved;
  in.count = 5; CHECK(!sample(in, rejected)); in = saved;
  uint32_t nan = 0x7fc00000; std::memcpy(p.data(), &nan, 4); CHECK(!sample(in, rejected)); p[0] = 0;
  in.positions.data = nullptr; CHECK(!sample(in, rejected)); in = saved;
  Input direct{in.positions, {}, 12, 3, 0, 1}; CHECK(get(direct).words[1] == 3);
  Captures captures;
  captures.add(id, a); captures.add(id, a); CHECK(captures.current.size() == 1);
  captures.next(); captures.add(id, get(moved)); CHECK(captures.stable == 1 && captures.collisions == 0);
  Sample different = a; ++different.words[3];
  captures.add(id, different); CHECK(captures.collisions == 1 && captures.stable == 0); // injected hash collision
  captures.clear(); CHECK(captures.previous.empty() && captures.current.empty());

  std::set<uint64_t> ids;
  constexpr unsigned n = 100000;
  for (unsigned i = 0; i < n; ++i) {
    p[0] = float(i); ids.insert(hash(get(in)));
  }
  CHECK(ids.size() == n);
  std::printf("synthetic: %u meshes, %zu hash collisions (sampled position mutation)\n", n, n - ids.size());
  // Sampling is explicitly lossy: unsampled changes must not be advertised as
  // a verified join. Pin that limitation alongside the collision-rate test.
  std::vector<float> big(4096 * 3);
  std::vector<uint32_t> indices(12288);
  for (unsigned i = 0; i < big.size(); ++i) big[i] = float(i) * 0.125f;
  for (unsigned i = 0; i < indices.size(); ++i) indices[i] = i % 4096;
  Input bench{{reinterpret_cast<const uint8_t*>(big.data()), big.size()*4},
              {reinterpret_cast<const uint8_t*>(indices.data()), indices.size()*4}, 12, uint32_t(indices.size()), 4, 0};
  const auto before = get(bench); big[3 * 10] += 1; CHECK(get(bench) == before);
  constexpr unsigned repeats = 2000;
  uint64_t sink = 0;
  const auto start = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < repeats; ++i) {
    big[0] = float(i);
    const auto s = get(bench); const auto h = hash(s); sink ^= h;
    captures.next(); captures.add(h, s);
  }
  const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / repeats;
  std::printf("CPU content identity: %.3f us/captured mesh (4096 vertices, 12288 indices; span scan + 32 samples + hash + capture registry), sink %llu\n", us, (unsigned long long)sink);
  std::printf("mesh_content_id_test passed; fixture id %016llx\n", (unsigned long long)id);
}
