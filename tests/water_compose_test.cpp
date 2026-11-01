// Pins the M5.3 water composition contracts (common/water_compose.h, common/water_fs_patch.h):
// the record buffer arithmetic, the publication, the B10G11R11 ownership check against both
// rounding modes of a software encoder, and the FS patch's refusals. Optional arguments:
//   water_compose_test <dumped 7b44a567771ecbec.spv> [<patched output .spv>]
// patches the dumped game module (FNV-1a hash checked against the table key) and writes the
// result for spirv-val / spirv-cross (CMake passes the dump when captures/ has it).
#include "../common/water_compose.h"
#include "../common/water_fs_patch.h"
#include "../common/water_policy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace bg3::water;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

// Unsigned small float with `mbits` mantissa bits and 5 exponent bits (B10G11R11 channels).
static float encode(float v, int mbits, bool nearest) {
  if (!(v > 0.0f)) return 0.0f;
  const float maxv = std::ldexp(2.0f - std::ldexp(1.0f, -mbits), 15);
  if (v >= maxv) return maxv;
  int e = std::ilogb(v);
  if (e < -14) e = -14;   // denormals: fixed step 2^(-14 - mbits)
  const float step = std::ldexp(1.0f, e - mbits);
  const float q = nearest ? std::nearbyint(v / step) : std::floor(v / step);
  return q * step;
}

static uint64_t fnv(const void* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t*>(p)[i]) * 1099511628211ull;
  return h;
}

int main(int argc, char** argv) {
  // 1. Buffer arithmetic: header + capacity records + the sink.
  CHECK(buffer_bytes(10) == kHeaderBytes + 11 * kRecordBytes);
  const uint32_t cap = capacity_of_mb(160);
  CHECK(buffer_bytes(cap) <= (160ull << 20));
  CHECK(buffer_bytes(cap + 1) > (160ull << 20));
  CHECK(cap >= 2560u * 1440u && cap >= 3440u * 1440u);   // the renders this default covers
  CHECK(capacity_of_mb(0) == capacity_of_mb(1));
  CHECK(kRecordBytes == 32 && kHeaderBytes % 16 == 0);

  // 2. Publication: invisible until complete, then exact.
  ComposeBuffer pub{}, got{};
  CHECK(!read_buffer(&pub, got));
  CHECK(!read_buffer(nullptr, got));
  publish_buffer(&pub, 0x1234, buffer_bytes(99), 99);
  CHECK(read_buffer(&pub, got) && got.buffer == 0x1234 && got.capacity == 99);
  ComposeBuffer small = pub;
  small.bytes = buffer_bytes(99) - 1;
  CHECK(!read_buffer(&small, got));

  // 3. Ownership check: every encodable value passes against its own encoding (both rounding
  //    modes, all three channels, across the range), and a value one ulp-pair away fails.
  for (int mb : {6, 5})
    for (bool nearest : {true, false})
      for (float v = 1e-7f; v < 6.0e4f; v *= 1.0137f) {
        CHECK(same_encoded(v, encode(v, mb, nearest), mb));
        const float other = encode(v * (1.0f + std::ldexp(3.0f, -mb)), mb, nearest);   // 3 ulps up
        if (v > 1e-3f) CHECK(!same_encoded(v, other, mb));
      }
  CHECK(same_encoded(0.0f, 0.0f, 5));
  CHECK(!same_encoded(-1.0f, 0.0f, 5));
  CHECK(!same_encoded(NAN, 0.0f, 5));
  CHECK(!same_encoded(1.0f, INFINITY, 5));

  // 4. Patch refusals (never a partial module).
  std::vector<uint32_t> out;
  const FsIds& ids = kFsIds[0];
  const uint32_t junk[8] = {0x07230203u, 0x10300, 0, 100, 0, 0, 0, 0};
  CHECK(patch_fs(nullptr, 0, ids, 0, 1, out) == PatchStatus::kNotSpirv && out.empty());
  CHECK(patch_fs(junk, 8, ids, 0, 1, out) == PatchStatus::kNotSpirv && out.empty());
  const uint32_t not_spirv[6] = {1, 2, 3, 4, 5, 6};
  CHECK(patch_fs(not_spirv, 6, ids, 0, 1, out) == PatchStatus::kNotSpirv);
  CHECK(fs_ids(0x7b44a567771ecbecull) == &kFsIds[0]);
  CHECK(fs_ids(0x1d4bff2e04a42c4cull) == nullptr);   // audited for capture, not for composition
  for (const FsIds& f : kFsIds) {
    bool audited = false;
    for (const Audited& a : kAudited) audited |= a.fs == f.fs;
    CHECK(audited);
  }

  // 5. The dumped module, when given.
  if (argc >= 2) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(!bytes.empty() && bytes.size() % 4 == 0);
    CHECK(fnv(bytes.data(), bytes.size()) == ids.fs);
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    const uint64_t addr = 0x0000123456789a00ull;
    const PatchStatus st = patch_fs(words.data(), words.size(), ids, addr, cap, out);
    std::printf("patch %s: %zu -> %zu words, bound %u -> %u\n", patch_status_name(st), words.size(), out.size(),
                words[3], out.empty() ? 0u : out[3]);
    CHECK(st == PatchStatus::kApplied);
    CHECK(out.size() > words.size() && out[3] > words[3]);
    // Every original instruction is still there, in order (the patch only inserts).
    size_t q = 5;
    for (size_t p = 5; p < words.size();) {
      const uint32_t n = words[p] >> 16;
      while (q < out.size() && !(out[q] == words[p] && std::memcmp(&out[q], &words[p], n * 4) == 0)) {
        const uint32_t m = out[q] >> 16;
        CHECK(m != 0);
        q += m;
      }
      if (q >= out.size()) {
        // The memory model is the only rewritten instruction.
        CHECK((words[p] & 0xffffu) == 14u);
        q = 5;
        while (q < out.size() && (out[q] & 0xffffu) != 14u) q += out[q] >> 16;
        CHECK(q < out.size() && out[q + 1] == 5348u && out[q + 2] == words[p + 2]);
      }
      q += out[q] >> 16;
      p += n;
    }
    // Patching twice is refused (the capability is there).
    std::vector<uint32_t> twice;
    CHECK(patch_fs(out.data(), out.size(), ids, addr, cap, twice) == PatchStatus::kAlreadyPatched);
    // A table that does not fit (types swapped) is refused.
    FsIds wrong = ids;
    std::swap(wrong.a, wrong.X);
    CHECK(patch_fs(words.data(), words.size(), wrong, addr, cap, twice) == PatchStatus::kIdMismatch);
    wrong = ids;
    wrong.pos = ids.N;
    CHECK(patch_fs(words.data(), words.size(), wrong, addr, cap, twice) == PatchStatus::kIdMismatch);
    if (argc >= 3) {
      std::ofstream o(argv[2], std::ios::binary);
      o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size() * 4));
    }
  } else {
    std::printf("no dumped module given: the patch of the game module is not exercised\n");
  }
  std::printf("water_compose_test: %u checks passed\n", checks);
  return 0;
}
