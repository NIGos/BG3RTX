// Pins the M5.3 water composition contracts (common/water_compose.h, common/water_fs_patch.h):
// the record buffer arithmetic, the publication, the B10G11R11 ownership check against both
// rounding modes of a software encoder, and the FS patch's refusals. Optional arguments:
//   water_compose_test <dump dir, e.g. captures/shaders> [<output dir>]
// patches every dumped game module of the id table (<hash>.spv, FNV-1a hash checked against the
// table key) and writes water_fs_<hash hi>_patched.spv for spirv-val / spirv-cross (CMake passes
// captures/shaders when it exists).
#include "../common/water_compose.h"
#include "../common/water_fs_patch.h"
#include "../common/water_policy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
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

  CHECK(fs_ids(0x99c1fcef82b253c1ull) == &kFsIds[1]);   // S8 ExtraWaves
  CHECK(fs_ids(0xc4e92ccdb08dd777ull) == &kFsIds[2]);   // S8 NoCaustics_Waves

  // 4b. The composition's algebra against the FS's end of main (ISSUE 3 of m5w3: is A L / B = 0.28
  //     a unit factor?). A and B are the exact derivative of the FS colour along its probe term:
  //     replacing k Sp by L in the FS gives what the composition gives, for any L; L = k Sp is the
  //     identity. So the composed change is a T X E (L - k Sp), with no free factor (no k in A, no
  //     second E, no exposure). Values: the S8 water's ranges (F0 0.0196, k = 9.8e-5 * 100, the
  //     log's mean C ~ 0.3 and B ~ 0.014), plus extremes.
  {
    const FsTerms base{0.25f, 0.04f, 0.3f, 2.2f, 0.09f, 0.0101725f, 0.93f, 0.002f, 0.97f, 0.003f, 0.8f};
    for (float a : {0.0f, 0.35f, 0.8f, 1.0f})
      for (float e : {0.0196f, 0.09f, 0.6f})
        for (float k : {0.0101725f, 1.0f})
          for (float lscale : {0.0f, 0.28f, 1.0f, 3.0f}) {
            FsTerms f = base;
            f.a = a, f.e = e, f.k = k;
            const float c = fs_colour(f);
            const float A = record_a(f), B = record_b(f);
            const float L = lscale * f.k * f.sp;   // the traced radiance, in the lighting's units
            FsTerms g = f;
            g.sp = L / f.k;                          // the FS with the probe radiance that L means
            const float want = fs_colour(g);
            const float got = compose(c, A, L, B);
            const float tol = 4e-6f * (1.0f + std::fabs(c) + B);
            CHECK(std::fabs(got - (want > 0.0f ? want : 0.0f)) <= tol);
            if (lscale == 1.0f) CHECK(std::fabs(got - c) <= tol);   // a ray that sees the probe: no change
            CHECK(std::fabs((A * L) / (B > 0.0f ? B : 1.0f) - (B > 0.0f ? lscale : 0.0f)) <= 1e-4f);
            // The FS's dC/dSp, numerically (C is linear in Sp: a full step is exact): a T X k E = B / Sp
            // (B carries k, A does not).
            FsTerms h = f;
            h.sp = f.sp * 2.0f;
            const float dcdsp = (fs_colour(h) - c) / (h.sp - f.sp);
            CHECK(std::fabs(dcdsp - B / f.sp) <= 2e-3f * (B / f.sp) + 1e-6f);
            CHECK(std::fabs(dcdsp - A * f.k) <= 2e-3f * (A * f.k) + 1e-6f);
          }
    // m5w3 per composed pixel: B 0.01383, A L 0.00452: L / (k Sp) = 0.327 is what the hits saw.
    CHECK(ratio_bin(0.00452f, 0.01383f) == kCsRatioLow);
    CHECK(ratio_bin(1.0f, 1.0f) == kCsRatioMid && ratio_bin(0.5f, 1.0f) == kCsRatioMid && ratio_bin(2.0f, 1.0f) == kCsRatioMid);
    CHECK(ratio_bin(2.01f, 1.0f) == kCsRatioHigh && ratio_bin(1.0f, 0.0f) == 0u);
  }

  // 4c. The statistics words (shaders/rt/water_compose.hlsl mirrors them): six tiers after the ray
  //     count, before the skin / clamped words; the m5w3 shader wrote tier k at 6 + k (tier 1 into
  //     the ray count).
  CHECK(kCsTier1 == kCsRays + 1 && kCsTier1 + 5 < kCsSkin && kCsSkin + 1 == kCsClamped);
  CHECK(kCsComposed < kCsRatioLow && kCsRatioHigh < 32u);

  // 4d. Which lighting a forward water pass composes with (BUG 1 of m5w3: 117 of 120 passes refused).
  CHECK(lighting_use(0, 100) == LightingUse::kNone);           // no lighting yet, or the last one skipped
  CHECK(lighting_use(100, 100) == LightingUse::kThisFrame);    // lighting recorded first
  CHECK(lighting_use(99, 100) == LightingUse::kEarlier);       // the pass recorded before the lighting's flush
  CHECK(lighting_use(100 - kLightingLagFrames, 100) == LightingUse::kEarlier);   // 2 presents per app frame + 1
  CHECK(lighting_use(100 - kLightingLagFrames - 1, 100) == LightingUse::kNone);  // lighting stopped
  CHECK(lighting_use(101, 100) == LightingUse::kNone);         // never from the future
  CHECK(kLightingLagFrames == 2 + 1);

  // 5. The dumped modules in the given directory: every table entry whose dump is there.
  unsigned patched = 0;
  for (const FsIds& fi : kFsIds) {
    if (argc < 2) break;
    char name[64];
    std::snprintf(name, sizeof name, "/%016llx.spv", (unsigned long long)fi.fs);
    std::ifstream f(std::string(argv[1]) + name, std::ios::binary);
    if (!f) {
      std::printf("no dump of %016llx: its patch is not exercised\n", (unsigned long long)fi.fs);
      continue;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(!bytes.empty() && bytes.size() % 4 == 0);
    CHECK(fnv(bytes.data(), bytes.size()) == fi.fs);
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    const uint64_t addr = 0x0000123456789a00ull;
    const PatchStatus st = patch_fs(words.data(), words.size(), fi, addr, cap, out);
    std::printf("patch %016llx %s: %zu -> %zu words, bound %u -> %u\n", (unsigned long long)fi.fs,
                patch_status_name(st), words.size(), out.size(), words[3], out.empty() ? 0u : out[3]);
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
    CHECK(patch_fs(out.data(), out.size(), fi, addr, cap, twice) == PatchStatus::kAlreadyPatched);
    // A table that does not fit (types swapped) is refused.
    FsIds wrong = fi;
    std::swap(wrong.a, wrong.X);
    CHECK(patch_fs(words.data(), words.size(), wrong, addr, cap, twice) == PatchStatus::kIdMismatch);
    wrong = fi;
    wrong.pos = fi.N;
    CHECK(patch_fs(words.data(), words.size(), wrong, addr, cap, twice) == PatchStatus::kIdMismatch);
    if (argc >= 3) {
      std::snprintf(name, sizeof name, "/water_fs_%08x_patched.spv", unsigned(fi.fs >> 32));
      std::ofstream o(std::string(argv[2]) + name, std::ios::binary);
      o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size() * 4));
    }
    ++patched;
  }
  // Another table's fi never fit a module (they name values of other types or nothing).
  if (patched == kFsIdsCount && argc >= 2) {
    for (const FsIds& fi : kFsIds) {
      char name[64];
      std::snprintf(name, sizeof name, "/%016llx.spv", (unsigned long long)fi.fs);
      std::ifstream f(std::string(argv[1]) + name, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      std::vector<uint32_t> words(bytes.size() / 4);
      std::memcpy(words.data(), bytes.data(), bytes.size());
      for (const FsIds& other : kFsIds) {
        if (&other == &fi) continue;
        FsIds swapped = other;
        swapped.fs = fi.fs;
        std::vector<uint32_t> tmp;
        CHECK(patch_fs(words.data(), words.size(), swapped, 0x1000, cap, tmp) == PatchStatus::kIdMismatch);
      }
    }
  }
  if (!patched) std::printf("no dumped module given: the patch of the game modules is not exercised\n");
  std::printf("water_compose_test: %u checks passed\n", checks);
  return 0;
}
