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

// ---- M5.4b: the water FS's point / spot loops and our per-light recompute, on the CPU ----
struct V3 { float x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static V3 operator*(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
static float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float sat(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
static float pow5(float x) { return std::pow(std::fmax(std::fabs(x), 9.9999997e-05f), 5.0f); }
static V3 norm3(V3 a) { return a * (1.0f / std::sqrt(dot3(a, a))); }
struct Light { V3 pos; float inv_r; V3 col; float cos_outer; V3 dir; float cos_inner; float m3, m4, m5; uint32_t chan, flags; float m8; };
struct Surf { V3 P, N, V, dif; float f0, a, nov; uint32_t channel; };

// The FS (captures/alpha_cache/c4e92ccdb08dd777.glsl lines 725-905 points, 916-1070 spots; the other
// two audited FS have the same loops under other ids), one light, as written there: fma where it has
// fma, its clamps and guards. `s` is the light's atlas term (mix(1, pcf, min(fade, m8)) when m7 bit 0),
// `exposure` set 2 b0 _m0[0]. Adds to (spec, diff) as the loop's carried sums.
static void fs_light(const Light& l, bool spot, const Surf& sf, float exposure, float s, V3& spec, V3& diff) {
  if ((l.chan & (1u << (sf.channel & 31u))) == 0u) return;
  const V3 L = l.pos - sf.P;
  const float d2 = dot3(L, L);
  const V3 Ld = L * (1.0f / std::sqrt(std::fmax(d2, 1.0000000133514319600180897396058e-10f)));
  const float ir2 = l.inv_r * l.inv_r;
  if (!(d2 < 1.0f / ir2)) return;
  const V3 h0 = sf.V + Ld;
  const V3 H = h0 * (1.0f / std::sqrt(std::fmax(dot3(h0, h0), 1.0000000133514319600180897396058e-10f)));
  const float loh = sat(dot3(Ld, H)), noh = sat(dot3(sf.N, H)), nol = sat(dot3(sf.N, Ld));
  const float bias = std::fabs(l.m5);
  float k;
  if (spot) {
    const float c = dot3(Ld * -1.0f, norm3(l.dir));
    float kk = 0.0f;
    if (c > l.cos_outer) {
      const float x = d2 * ir2;
      const float att0 = std::pow(std::fmax(std::fabs(sat(std::fma(-x, x, 1.0f))), 9.9999997473787516355514526367188e-05f), l.m4);
      const float att = l.m5 < 0.0f ? att0 * std::fma(4.0f, ir2, bias) : att0 * ((1.0f / std::fmax(d2, 9.9999997473787516355514526367188e-05f)) + bias);
      const float t = sat((c - l.cos_outer) / std::fmax(l.cos_inner - l.cos_outer, 9.9999997473787516355514526367188e-05f));
      kk = nol * (att * (t * t));
    }
    k = 0.3183098733425140380859375f * kk;
  } else {
    const float x = d2 * ir2;
    const float att0 = std::pow(std::fmax(std::fabs(sat(std::fma(-x, x, 1.0f))), 9.9999997473787516355514526367188e-05f), l.m4);
    const float att = l.m5 < 0.0f ? att0 * std::fma(4.0f, ir2, bias) : att0 * ((1.0f / std::fmax(d2, 9.9999997473787516355514526367188e-05f)) + bias);
    k = 0.079577468335628509521484375f * (nol * att);
  }
  if ((l.flags & 1u) != 0u) k = k * s;   // _1779 * mix(1.0, _1910, min(_1671, m8)): the whole factor given as s
  if (!(nol > 0.0f)) return;
  const V3 col = l.col * (k * (l.m3 < 0.0f ? exposure : 1.0f));
  const float a = sf.a;
  const float fd = std::fma((2.0f * loh) * loh, a, 0.5f * a) - 1.0f;
  const float k2h = (a + 1.0f) * 0.5f, k2 = k2h * k2h, a2 = a * a, a4 = a2 * a2;
  const float dn = std::fma(std::fma(noh, a4, -noh), noh, 1.0f);
  const float F = sf.f0 + (1.0f - sf.f0) * pow5(1.0f - loh);
  const float vis = 0.5f / std::fma(nol, std::sqrt(std::fma(std::fma(-sf.nov, k2, sf.nov), sf.nov, k2)),
                                    sf.nov * std::sqrt(std::fma(std::fma(-nol, k2, nol), nol, k2)));
  const float sterm = F * (vis * (a4 * (0.3183098733425140380859375f / (dn * dn))));
  spec = spec + col * sterm;   // fma(col, F * ..., spec)
  const float burley = (1.0f + fd * pow5(1.0f - nol)) * (1.0f + fd * pow5(1.0f - sf.nov));
  diff = diff + col * sf.dif * (0.3183098733425140380859375f * (burley * (1.0f + (0.662251651287078857421875f - 1.0f) * a)));
}
// Ours: shaders/rt/bg3_lights.hlsli bg3_light_eval, statement by statement (the D-LS recompute of the
// deferred lighting f469db78, reused unchanged for the water). d + s, or false when not evaluated.
static bool our_light(const Light& l, bool spot, const Surf& sf, float exposure, V3& c) {
  c = {0, 0, 0};
  if ((l.chan & (1u << (sf.channel & 31u))) == 0u) return false;
  const V3 L = l.pos - sf.P;
  const float d2 = dot3(L, L);
  const V3 Ld = L * (1.0f / std::sqrt(std::fmax(d2, 1.0000000e-10f)));
  const float ir2 = l.inv_r * l.inv_r;
  if (!(d2 < 1.0f / ir2)) return false;
  const V3 H0 = sf.V + Ld;
  const V3 H = H0 * (1.0f / std::sqrt(std::fmax(dot3(H0, H0), 1.0000000e-10f)));
  const float loh = sat(dot3(Ld, H)), noh = sat(dot3(sf.N, H)), nol = sat(dot3(sf.N, Ld));
  const float x = d2 * ir2;
  float att = std::pow(std::fmax(std::fabs(sat(1.0f - x * x)), 9.9999997e-05f), l.m4);
  const float bias = std::fabs(l.m5);
  att *= l.m5 < 0.0f ? (4.0f * ir2 + bias) : (1.0f / std::fmax(d2, 9.9999997e-05f) + bias);
  float k;
  if (spot) {
    const float cc = dot3(Ld * -1.0f, norm3(l.dir));
    if (!(cc > l.cos_outer)) return false;
    const float t = sat((cc - l.cos_outer) / std::fmax(l.cos_inner - l.cos_outer, 9.9999997e-05f));
    k = 0.31830987f * (nol * (att * (t * t)));
  } else {
    k = 0.07957747f * (nol * att);
  }
  if (!(nol > 0.0f)) return false;
  const V3 col = l.col * (k * (l.m3 < 0.0f ? exposure : 1.0f));
  const float a = sf.a;
  const float fd = (2.0f * loh * loh * a + 0.5f * a) - 1.0f;
  float k2 = (a + 1.0f) * 0.5f;
  k2 *= k2;
  float a4 = a * a;
  a4 *= a4;
  const float dn = (noh * a4 - noh) * noh + 1.0f;
  const float F = sf.f0 + (1.0f - sf.f0) * pow5(1.0f - loh);
  const float vis = 0.5f / (nol * std::sqrt((sf.nov - sf.nov * k2) * sf.nov + k2) + sf.nov * std::sqrt((nol - nol * k2) * nol + k2));
  const V3 s = col * (F * (vis * (a4 * (0.31830987f / (dn * dn)))));
  const float burley = (1.0f + fd * pow5(1.0f - nol)) * (1.0f + fd * pow5(1.0f - sf.nov));
  const V3 d = col * (sf.dif * (0.31830987f * (burley * (1.0f + (0.66225165f - 1.0f) * a))));
  c = d + s;
  return true;
}
// IEEE half rounding to nearest (the record's packHalf2x16), within the normal and subnormal range.
static float to_half(float v) {
  if (!(std::fabs(v) > 0.0f) || !std::isfinite(v)) return v;
  int e = std::ilogb(v);
  if (e < -14) e = -14;
  const float step = std::ldexp(1.0f, e - 10);
  return std::nearbyint(v / step) * step;
}
static uint32_t lcg(uint32_t& s) { return s = s * 1664525u + 1013904223u; }
static float u01(uint32_t& s) { return float(lcg(s) >> 8) * (1.0f / 16777216.0f); }

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
  // M5.4: the sun record, the same arithmetic at 40 B; the default still covers 2560 x 1440.
  CHECK(kRecordBytesSun == 40 && kRecordWordsSun == kRecordWords + 2);
  CHECK(buffer_bytes(10, kRecordBytesSun) == kHeaderBytes + 11 * kRecordBytesSun);
  const uint32_t cap_sun = capacity_of_mb(160, kRecordBytesSun);
  CHECK(buffer_bytes(cap_sun, kRecordBytesSun) <= (160ull << 20) && buffer_bytes(cap_sun + 1, kRecordBytesSun) > (160ull << 20));
  CHECK(cap_sun >= 2560u * 1440u);
  // M5.4b: the 96-B record (sun words + 14 local words) gets the key's pixel count (MB scaled by 3).
  CHECK(kRecordBytesLs == 96 && kRecordWordsLs == kRecordWordsSun + 14);
  CHECK(record_mb(160, kRecordBytesLs) == 480 && record_mb(160, kRecordBytesSun) == 160 && record_mb(160, kRecordBytes) == 160);
  const uint32_t cap_ls = capacity_of_mb(record_mb(160, kRecordBytesLs), kRecordBytesLs);
  CHECK(cap_ls >= cap - 2 && cap_ls >= 2560u * 1440u && buffer_bytes(cap_ls, kRecordBytesLs) <= (480ull << 20));
  // M5.4c: the game-shadow words after each sun form (8 B); the stride is published with the flag, an older
  // layer's 0 flags (or a non-sun form) means none.
  {
    ComposeBuffer b{};
    b.record_bytes = kRecordBytesSun, b.flags = kBufGsh;
    CHECK(gsh_of(b) && stride_of(b) == 48u);
    b.record_bytes = kRecordBytesLs;
    CHECK(stride_of(b) == 104u);
    b.record_bytes = kRecordBytesRefr;
    CHECK(stride_of(b) == 112u);
    b.record_bytes = kRecordBytes;
    CHECK(!gsh_of(b) && stride_of(b) == kRecordBytes);   // no sun form: no words
    b.record_bytes = kRecordBytesRefr, b.flags = 0;
    CHECK(!gsh_of(b) && stride_of(b) == kRecordBytesRefr);
    CHECK(kGshWords == 2 && record_mb(160, kRecordBytesSun + kGshBytes) == 160);
    const uint32_t s3 = kRecordBytesRefr + kGshBytes, cg = capacity_of_mb(record_mb(160, s3), s3);
    CHECK(cg >= 2560u * 1440u && buffer_bytes(cg, s3) <= (uint64_t(record_mb(160, s3)) << 20));
    ComposeBuffer pub{}, got{};
    publish_buffer(&pub, 0x1234, buffer_bytes(cg, s3), cg, kRecordBytesRefr, kBufGsh);
    CHECK(read_buffer(&pub, got) && stride_of(got) == s3);
    pub.bytes = buffer_bytes(cg, kRecordBytesRefr);   // a buffer sized without the words is refused
    CHECK(!read_buffer(&pub, got));
  }

  // 2. Publication: invisible until complete, then exact.
  ComposeBuffer pub{}, got{};
  CHECK(!read_buffer(&pub, got));
  CHECK(!read_buffer(nullptr, got));
  publish_buffer(&pub, 0x1234, buffer_bytes(99), 99);
  CHECK(read_buffer(&pub, got) && got.buffer == 0x1234 && got.capacity == 99);
  ComposeBuffer small = pub;
  small.bytes = buffer_bytes(99) - 1;
  CHECK(!read_buffer(&small, got));
  CHECK(record_bytes_of(got) == kRecordBytes);   // an M5.3 publication (and an older layer's 0)
  ComposeBuffer old = pub;
  old.record_bytes = 0;
  CHECK(record_bytes_of(old) == kRecordBytes);
  ComposeBuffer pub_sun{};
  publish_buffer(&pub_sun, 0x5678, buffer_bytes(99, kRecordBytesSun), 99, kRecordBytesSun);
  CHECK(read_buffer(&pub_sun, got) && record_bytes_of(got) == kRecordBytesSun);
  ComposeBuffer short_sun = pub_sun;   // a 32-B sized buffer cannot hold 40-B records
  short_sun.bytes = buffer_bytes(99);
  CHECK(!read_buffer(&short_sun, got));
  ComposeBuffer pub_ls{};
  publish_buffer(&pub_ls, 0x9abc, buffer_bytes(99, kRecordBytesLs), 99, kRecordBytesLs);
  CHECK(read_buffer(&pub_ls, got) && record_bytes_of(got) == kRecordBytesLs);
  ComposeBuffer short_ls = pub_ls;   // a 40-B sized buffer cannot hold 96-B records
  short_ls.bytes = buffer_bytes(99, kRecordBytesSun);
  CHECK(!read_buffer(&short_ls, got));
  ComposeBuffer odd = pub_ls;        // an unknown size reads as M5.3's
  odd.record_bytes = 64;
  CHECK(record_bytes_of(odd) == kRecordBytes);

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
  // The pre-audit of 2026-11-01 (tools/water_audit.py derives each row; water_audit_test pins them).
  CHECK(kFsIdsCount == 11);
  CHECK(fs_ids(0x2bd6cb9de954c2f9ull) == &kFsIds[7]);   // Flymm Cargos basement, instanced
  CHECK(fs_ids(0x269de9047e587ea2ull) == &kFsIds[8]);   // the same template, static
  // M5.4c: the game's sun shadow map in Lsun, mapped with its Vg (the only rows with one).
  CHECK(fs_ids(0x06e66a6fbd88a6b3ull) == &kFsIds[9] && kFsIds[9].vg == 1365);   // PL_noCaustics_Shadows (S3 camp)
  CHECK(fs_ids(0xced49ee370940201ull) == &kFsIds[10] && kFsIds[10].vg == 1869);  // huge_PL_Shadow
  for (uint32_t i = 0; i < 9; ++i) CHECK(kFsIds[i].vg == 0);
  CHECK(fs_ids(0x7813e701eb642285ull) == nullptr);      // Debris: a mask scales the probe term
  for (uint32_t i = 0; i < kFsIdsCount; ++i)
    for (uint32_t j = i + 1; j < kFsIdsCount; ++j) CHECK(kFsIds[i].fs != kFsIds[j].fs);

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

  // 4e. M5.4 sun visibility: D = a T X Dsun is the FS's dC/dDsun times its unshadowed sun term and
  //     R = C - D the FS with its sun at 0, so shadow(C, R, V) is the FS with its sun visibility 1
  //     replaced by V, for any V in [0, 1]; V = 1 (lit, unknown) is the identity; only the visibility
  //     changes (same unshadowed radiance). With M5.3 on as well, the two terms are independent:
  //     compose then shadow = the FS with both swapped.
  {
    const FsTerms base{0.25f, 0.04f, 0.3f, 2.2f, 0.09f, 0.0101725f, 0.93f, 0.002f, 0.97f, 0.003f, 0.8f, 0.6f};
    for (float a : {0.0f, 0.35f, 1.0f})
      for (float sun : {0.0f, 0.02f, 0.6f, 40.0f})   // 40: a sun glint
        for (float v : {0.0f, 0.25f, 1.0f}) {
          FsTerms f = base;
          f.a = a, f.sun = sun;
          const float c = fs_colour(f), D = record_d(f), R = record_r(f);
          FsTerms g = f;
          g.sun = sun * v;
          const float want = fs_colour(g);
          const float tol = 4e-6f * (1.0f + std::fabs(c) + D);
          CHECK(std::fabs(shadow(c, R, v) - (want > 0.0f ? want : 0.0f)) <= tol);
          if (v == 1.0f) CHECK(shadow(c, R, v) == c);   // lit / unknown: bit-identical
          CHECK(D >= 0.0f && D <= c - f.refr * (1.0f - f.a) + tol);   // D is part of C, never more
          // dC/dDsun numerically (C is linear in the sun term): D / Dsun.
          FsTerms h = f;
          h.sun = sun + 1.0f;
          CHECK(std::fabs((fs_colour(h) - c) - record_d(h) + D) <= tol);
          // Both swaps at once (rt_water_compose and rt_water_shadows): the shader's (occluded ? R : C)
          // + A L - B.
          const float A = record_a(f), B = record_b(f), L = 0.4f * f.k * f.sp;
          FsTerms both = g;
          both.sp = L / f.k;
          const float wb = fs_colour(both);
          CHECK(std::fabs(shadow(c + A * L - B, R + A * L - B, v) - (wb > 0.0f ? wb : 0.0f)) <= tol + 4e-6f * B);
        }
    CHECK(shadow(0.1f, -0.001f, 0.0f) == 0.0f);   // an f32-rounded R below 0 clamps, never negative
    CHECK(shadow(0.1f, NAN, 1.0f) == 0.1f);       // lit: the target's value, whatever the record holds
    CHECK(kCsSunRec == 32u && kCsSunClamped < kCsSunSumD && kCsSunSumRemoved + 2 == kCsSunWords);
    CHECK(kCsSunSumD % 2 == 0 && kCsSunSumRemoved % 2 == 0 && kCsSunWords * 4 <= 256);   // 64-bit pairs, one slot
    CHECK(kCsRatioHigh < kCsSunRec && (kFlagRf & kFlagSun) == 0);
  }

  // 4j. M5.4 through the real encodings (m5w4 S8 2026-11-01: the shadowed pool's sun-dominated pixels
  //     black or bright, 1213 clamped records per frame). The FS writes C to the B10G11R11 target
  //     (6 / 5 mantissa bits, round to nearest or toward zero) and the record in half. At V = 0 the
  //     compose must give the FS evaluated with its sun colour set to 0, within the record's half
  //     rounding of that value (2^-11, relative) plus the f32 rounding of C - D in the FS (a few ulps
  //     of C): whatever the sun's share of C, up to the glint (C ~ 1000, rest ~ 0.05). The formula
  //     before this fix, C_target - half(D), misses it by up to one ulp of the target's C: where the
  //     sun is 98% or more of C that is the whole rest (the check counts those cases: it fails).
  {
    uint32_t seed = 4242u, n = 0, old_bad = 0, old_clamped = 0;
    for (int i = 0; i < 20000; ++i) {
      FsTerms f{};
      f.refr = 0.3f * u01(seed) * u01(seed);
      f.direct = 0.05f * u01(seed) * u01(seed);          // the local lights
      f.kd = 0.5f * u01(seed), f.sp = 4.0f * u01(seed), f.e = 0.02f + 0.5f * u01(seed);
      f.k = (i & 1) ? 0.0101725f : 1.0f;
      f.x = 0.5f + 0.5f * u01(seed), f.ge = 0.01f * u01(seed), f.t = 0.6f + 0.4f * u01(seed);
      f.s = 0.01f * u01(seed), f.a = (i % 5 == 0) ? 1.0f : u01(seed);
      f.sun = std::pow(10.0f, -3.0f + 6.5f * u01(seed));   // 1e-3 .. 3e3: up to the glint
      FsTerms g = f;
      g.sun = 0.0f;                                         // the FS with the sun colour set to 0
      const float want = fs_colour(g), c = fs_colour(f), D = record_d(f);
      if (!(c > 0.0f) || c > 6.0e4f) continue;
      ++n;
      const float R = to_half(record_r(f));                // the record's w8 / w9 (f32 C - D, then half)
      const float tol = std::ldexp(std::fabs(want), -11) + 8.0f * std::ldexp(c + D, -24) + 1e-7f;
      for (int mb : {6, 5})
        for (bool nearest : {true, false}) {
          const float ct = encode(c, mb, nearest);         // the target the compose reads (owned: same_encoded)
          CHECK(same_encoded(c, ct, mb));
          CHECK(std::fabs(shadow(ct, R, 0.0f) - (want > 0.0f ? want : 0.0f)) <= tol);
          CHECK(shadow(ct, R, 1.0f) == ct);                 // lit: the target bit for bit
          const float old = ct - to_half(D);
          old_clamped += old < 0.0f;
          old_bad += std::fabs((old > 0.0f ? old : 0.0f) - want) > tol;
        }
    }
    std::printf("4j: %u FS samples x 4 encodings; C_target - D: %u beyond the half tolerance, %u clamped at 0\n", n,
                old_bad, old_clamped);
    CHECK(n > 19000 && old_bad > 0 && old_clamped > 0);   // the check catches the old formula
    // The m5w4 pool's glint pixel: C = 976 with a rest of 0.05 (red, 6 bits): one target ulp is 8.
    FsTerms f{};   // a = X = T = k = 1: C = Lw = ambient 0.05 + sun 975.95
    f.x = 1.0f, f.t = 1.0f, f.k = 1.0f, f.a = 1.0f, f.kd = 0.05f, f.sun = 975.95f;
    const float c = fs_colour(f), ct = encode(c, 6, true);
    CHECK(std::fabs(c - 976.0f) < 1e-3f && std::fabs(ct - c) <= 8.0f);
    CHECK(std::fabs(shadow(ct, to_half(record_r(f)), 0.0f) - 0.05f) < 1e-4f);
  }

  // 4k. M5.4c the game's sun shadow (the FS model of PL_noCaustics_Shadows / huge_PL_Shadow: Lsun = colour *
  //     (Vg * clouds * b0), so the sun term is D = Vg D_u). gsh_shadow(C, R, Dg, V) is the FS with its Vg
  //     replaced by V, for any V in [0, 1]; V = Vg is C (the energy rule); V = Vg = 1 is C bit for bit (Dg = 0);
  //     V = 0 is R whatever Vg (the M5.4 occluded case).
  {
    const FsTerms base{0.25f, 0.04f, 0.3f, 2.2f, 0.09f, 0.0101725f, 0.93f, 0.002f, 0.97f, 0.003f, 0.8f, 0.6f};
    for (float a : {0.0f, 0.35f, 1.0f})
      for (float sun : {0.0f, 0.02f, 0.6f, 40.0f})
        for (float vg : {1.0f, 0.6f, 0.05f, 1.6e-9f})   // 1.6e-9: pow(1e-4, 2.2), the FS's floor
          for (float v : {0.0f, 0.25f, vg, 1.0f}) {
            FsTerms f = base;
            f.a = a, f.sun = sun, f.vg = vg;
            const float c = fs_colour(f), D = record_d(f), R = record_r(f), Dg = record_dg(f);
            FsTerms g = f;
            g.vg = v;
            const float want = fs_colour(g);
            FsTerms u = f;
            u.vg = 1.0f;
            const float Du = record_d(u);
            const float tol = 4e-6f * (1.0f + std::fabs(c) + Du);
            CHECK(std::fabs(gsh_shadow(c, R, Dg, v) - (want > 0.0f ? want : 0.0f)) <= tol);
            CHECK(std::fabs((D + Dg) - Du) <= 4e-6f * (1.0f + Du));   // Dg = D_u - D
            if (v == vg) CHECK(std::fabs(gsh_shadow(c, R, Dg, v) - c) <= tol);
            if (v == 1.0f && vg == 1.0f) CHECK(Dg == 0.0f && gsh_shadow(c, R, Dg, v) == c);
            if (v == 0.0f) CHECK(std::fabs(gsh_shadow(c, R, Dg, v) - (R > 0.0f ? R : 0.0f)) <= tol);
          }
    // The record's halves and the target's encoding at V = Vg: C' = Vg (Ct + Dg') + (1 - Vg) R' differs from
    // C by Vg (Ct - C) + Vg (Dg' - Dg) + (1 - Vg) (R' - R): at most the target's ulp and the halves' 2^-11.
    uint32_t seed = 777u, n = 0;
    for (int i = 0; i < 20000; ++i) {
      FsTerms f{};
      f.refr = 0.3f * u01(seed) * u01(seed);
      f.direct = 0.05f * u01(seed) * u01(seed);
      f.kd = 0.5f * u01(seed), f.sp = 4.0f * u01(seed), f.e = 0.02f + 0.5f * u01(seed);
      f.k = (i & 1) ? 0.0101725f : 1.0f;
      f.x = 0.5f + 0.5f * u01(seed), f.ge = 0.01f * u01(seed), f.t = 0.6f + 0.4f * u01(seed);
      f.s = 0.01f * u01(seed), f.a = (i % 5 == 0) ? 1.0f : u01(seed);
      f.sun = std::pow(10.0f, -3.0f + 6.0f * u01(seed));
      const int vk = i % 4;
      f.vg = vk == 0 ? 1.0f : vk == 1 ? u01(seed) : vk == 2 ? std::pow(u01(seed), 2.2f) : 1.6e-9f;
      if (!(f.vg > 0.0f)) f.vg = 1.6e-9f;
      const float c = fs_colour(f);
      if (!(c > 0.0f) || c > 6.0e4f) continue;
      const float R = record_r(f), Dg = record_dg(f), Rh = to_half(R), Dgh = to_half(Dg), vh = to_half(f.vg);
      if (!std::isfinite(Dg) || std::fabs(Dg) > 6.0e4f) continue;   // beyond a half: the compose keeps C (invalid)
      ++n;
      FsTerms lit = f;
      lit.vg = 1.0f;
      const float c1 = fs_colour(lit);
      const float h = std::ldexp(1.0f, -11), e32 = 8.0f * std::ldexp(c1 + std::fabs(R), -24) + 1e-7f;
      for (int mb : {6, 5})
        for (bool nearest : {true, false}) {
          const float ct = encode(c, mb, nearest);
          const float ulp = std::fabs(ct - c);
          // V = Vg (a soft PCF value: the expectation of the traced V): C within the rounding.
          const float at_vg = gsh_shadow(ct, Rh, Dgh, f.vg);
          CHECK(std::fabs(at_vg - c) <= f.vg * (ulp + h * std::fabs(Dg)) + (1.0f - f.vg) * h * std::fabs(R) + e32);
          if (f.vg == 1.0f) CHECK(Dgh == 0.0f && gsh_shadow(ct, Rh, Dgh, 1.0f) == ct);   // bit for bit
          // Lit: the FS at visibility 1; occluded: the FS at 0 (R), whatever the game's Vg.
          CHECK(std::fabs(gsh_shadow(ct, Rh, Dgh, 1.0f) - c1) <= ulp + h * std::fabs(Dg) + e32);
          CHECK(gsh_shadow(ct, Rh, Dgh, 0.0f) == (Rh > 0.0f ? Rh : 0.0f));
          CHECK(vh >= 0.0f && vh <= 1.0f);   // the record's Vg half stays a visibility
        }
    }
    std::printf("4k: %u game-shadowed FS samples x 4 encodings\n", n);
    CHECK(n > 19000);
    CHECK(kCsGshRec == kCsSunWords && kCsGshWords <= kCsLsRec && kCsGshSumDg % 2 == 0 && kCsGshSumRestored % 2 == 0 &&
          kCsGshSumVg % 2 == 0 && kCsGshSumVg + 2 == kCsGshWords);
    CHECK((kFlagGsh & (kFlagRf | kFlagSun | kFlagLs | kFlagRefr)) == 0);
  }

  // 4g. M5.4b sum match, offline (the FS model): over random froxel lists at water points (points and
  //     spots, channels, ranges, cones, back-facing lights, both falloff models, the global scale, some
  //     shadow-mapped lights with their atlas term), the record's L (the FS's loops, rounded to half as
  //     the patch stores it) equals our per-light sum (bg3_light_eval, with s_k for the shadow-mapped
  //     ones) within kLsSumTol in every checked record. A dropped or an extra light is never within.
  {
    uint32_t seed = 777;
    unsigned checked = 0, within = 0, dropped_within = 0, extra_far = 0, dropped_checked = 0;
    for (int rec = 0; rec < 20000; ++rec) {
      Surf sf{};
      sf.P = {u01(seed) * 40.0f - 20.0f, -1.5f - u01(seed) * 3.0f, u01(seed) * 40.0f - 20.0f};   // water below the camera
      sf.N = norm3({u01(seed) * 0.3f - 0.15f, 1.0f, u01(seed) * 0.3f - 0.15f});
      sf.V = norm3(sf.P * -1.0f);
      sf.dif = {0.02f + 0.1f * u01(seed), 0.03f + 0.1f * u01(seed), 0.04f + 0.1f * u01(seed)};
      sf.f0 = 0.16f * 0.35f * 0.35f;    // 0.0196, the S8 water
      sf.a = rec % 3 == 0 ? 0.09f : 0.09f + 0.6f * u01(seed);   // the FS's clamp, the sharpest lobe
      sf.nov = std::fmax(dot3(sf.N, sf.V), 9.9999997e-06f);
      sf.channel = lcg(seed) % 4u;
      const float exposure = 0.000407f;
      const int n = 1 + int(lcg(seed) % 6u), np = int(lcg(seed) % uint32_t(n + 1));
      V3 fs_s{0, 0, 0}, fs_d{0, 0, 0}, ours{0, 0, 0}, ours_drop{0, 0, 0};
      bool dropped_any = false;
      for (int i = 0; i < n; ++i) {
        Light l{};
        l.pos = sf.P + V3{u01(seed) * 16.0f - 8.0f, u01(seed) * 6.0f - 1.0f, u01(seed) * 16.0f - 8.0f};
        l.inv_r = 1.0f / (3.0f + 10.0f * u01(seed));
        l.col = {50.0f * u01(seed), 40.0f * u01(seed), 30.0f * u01(seed)};
        l.dir = norm3(sf.P - l.pos + V3{u01(seed) - 0.5f, u01(seed) - 0.5f, u01(seed) - 0.5f});
        l.cos_outer = 0.5f + 0.3f * u01(seed);
        l.cos_inner = l.cos_outer + 0.15f * u01(seed);
        l.m3 = lcg(seed) % 2u ? -1.0f : 1.0f;
        l.m4 = 1.0f + 2.0f * u01(seed);
        l.m5 = (lcg(seed) % 2u ? -1.0f : 1.0f) * (0.01f + u01(seed));
        l.chan = lcg(seed) % 8u ? 0xfu : 0x2u;
        l.flags = lcg(seed) % 5u == 0u ? 1u : 0u;
        const bool spot = i >= np;
        const float s = (l.flags & 1u) ? u01(seed) : 1.0f;
        fs_light(l, spot, sf, exposure, s, fs_s, fs_d);
        V3 c;
        if (our_light(l, spot, sf, exposure, c)) {
          ours = ours + c * s;
          if (i == 0 && dot3(c, {1, 1, 1}) > 0.0f) dropped_any = true;   // the comparison below drops light 0
          else ours_drop = ours_drop + c * s;
        }
      }
      const V3 L = fs_s + fs_d;
      const float rec_l[3] = {to_half(L.x), to_half(L.y), to_half(L.z)};
      const float sum[3] = {ours.x, ours.y, ours.z};
      const LsBin b = ls_check_bin(sum, rec_l);
      if (b != LsBin::kNone) ++checked, within += b == LsBin::kWithin;
      if (b != LsBin::kNone && b != LsBin::kWithin)
        std::printf("4g record %d: sum %g %g %g, L %g %g %g\n", rec, sum[0], sum[1], sum[2], rec_l[0], rec_l[1], rec_l[2]);
      if (dropped_any && b == LsBin::kWithin) {
        const float sd[3] = {ours_drop.x, ours_drop.y, ours_drop.z};
        const LsBin bd = ls_check_bin(sd, rec_l);
        // A light whose share is below the precision cannot be told apart; above it, it is.
        float mx = std::fmax(std::fmax(rec_l[0], rec_l[1]), rec_l[2]), lost = 0.0f;
        for (int k = 0; k < 3; ++k) lost = std::fmax(lost, std::fabs(sum[k] - sd[k]));
        if (lost > 2.0f * kLsSumTol * mx) ++dropped_checked, dropped_within += bd == LsBin::kWithin;
      }
      if (b == LsBin::kNone) {   // a light the FS did not add: an extra recomputed one is far
        const float extra[3] = {sum[0] + 0.01f, sum[1], sum[2]};
        extra_far += ls_check_bin(extra, rec_l) == LsBin::kFar;
      }
    }
    std::printf("4g sum check: %u records checked, %u within %.4f; %u with a dropped light, %u of them within; "
                "%u extra lights far\n", checked, within, double(kLsSumTol), dropped_checked, dropped_within, extra_far);
    CHECK(checked > 5000 && within == checked);   // about half the lists light the point at all
    CHECK(dropped_checked > 1000 && dropped_within == 0);
    CHECK(extra_far > 0);
    const float z3[3] = {0, 0, 0}, tiny[3] = {1e-6f, 0, 0}, one[3] = {1, 1, 1}, far[3] = {1.2f, 1, 1};
    const float near[3] = {1.0005f, 1, 1}, mid[3] = {1.05f, 1, 1}, nan3[3] = {NAN, 0, 0};
    CHECK(ls_check_bin(z3, z3) == LsBin::kNone && ls_check_bin(tiny, z3) == LsBin::kNone);
    CHECK(ls_check_bin(one, one) == LsBin::kWithin && ls_check_bin(near, one) == LsBin::kWithin);
    CHECK(ls_check_bin(mid, one) == LsBin::kMid && ls_check_bin(far, one) == LsBin::kFar);
    CHECK(ls_check_bin(one, z3) == LsBin::kFar && ls_check_bin(nan3, one) == LsBin::kFar);
    // The tolerance is the half's: its rounding (2^-11 relative) is inside, two ulps are not.
    CHECK(kLsSumTol == std::ldexp(1.0f, -10) && kLsCheckMin == std::ldexp(1.0f, -14));
  }

  // 4h. M5.4b removal: F = a T X is the FS's dC/dL, so ls_shadow(C, F, sum_k (1 - V_k) c_k) is the FS
  //     with its unshadowed lights' visibility 1 replaced by V_k (shadow-mapped lights keep theirs);
  //     all V_k = 1 is the identity, bit for bit; with the sun and the reflection swaps, independent.
  {
    const FsTerms base{0.25f, 0.04f, 0.3f, 2.2f, 0.09f, 0.0101725f, 0.93f, 0.002f, 0.97f, 0.003f, 0.8f, 0.6f, 0.0f};
    const float ck[4] = {0.3f, 2.0f, 0.05f, 7.0f};   // the lights' terms; light 3 is shadow-mapped (kept)
    for (float a : {0.0f, 0.35f, 1.0f})
      for (uint32_t vis = 0; vis < 8; ++vis) {   // V_0..V_2 in {0, 1}
        FsTerms f = base;
        f.a = a;
        f.local = ck[0] + ck[1] + ck[2] + ck[3] * 0.4f;   // the FS: s_3 = 0.4 for the mapped light
        const float c = fs_colour(f), F = record_f(f);
        float removed = 0.0f, kept = ck[3] * 0.4f;
        for (int k = 0; k < 3; ++k) ((vis >> k) & 1u) ? kept += ck[k] : removed += ck[k];
        FsTerms g = f;
        g.local = kept;
        const float want = fs_colour(g);
        const float tol = 4e-6f * (1.0f + std::fabs(c) + F * f.local);
        CHECK(std::fabs(ls_shadow(c, F, removed) - (want > 0.0f ? want : 0.0f)) <= tol);
        if (vis == 7u) CHECK(ls_shadow(c, F, 0.0f) == c);   // every light lit: bit-identical
        CHECK(F * f.local <= c - f.refr * (1.0f - f.a) + tol);   // the local term is part of C
        // dC/dL numerically (C is linear in L): F.
        FsTerms h = f;
        h.local = f.local + 1.0f;
        CHECK(std::fabs((fs_colour(h) - c) - F) <= tol);
        // With the sun (V = 0) and the reflection swapped as well: the FS with all three changed.
        FsTerms all = g;
        all.sun = 0.0f;
        const float L = 0.4f * f.k * f.sp;
        all.sp = L / f.k;
        const float wa = fs_colour(all);
        const float AL_B = record_a(f) * L - record_b(f);
        const float got = ls_shadow(shadow(c + AL_B, record_r(f) + AL_B, 0.0f), F, removed);
        // shadow() clamps at 0 between the terms; the shader clamps once: equal whenever nothing clamps.
        if (wa > 0.0f) CHECK(std::fabs(got - wa) <= tol + 4e-6f * (record_b(f) + record_d(f)));
      }
    CHECK(ls_shadow(0.1f, 2.0f, 1.0f) == 0.0f);
  }
  // 4i. The M5.4b statistics words: after the sun's, inside the 128-word slot, sums as 64-bit pairs.
  CHECK(kCsLsRec >= kCsSunWords && kCsLsChkSh < kCsLsSumL && kCsLsSumBox + 2 == kCsLsChkShWithin);
  CHECK(kCsLsChkShWithin + 1 == kCsLsWords && kCsLsWords <= kCsSlotWordsLs && kCsSlotWordsLs * 4 == 512);
  for (uint32_t w : {kCsLsSumL, kCsLsSumRemoved, kCsLsSumChk, kCsLsSumChkL, kCsLsSumBox}) CHECK(w % 2 == 0);
  CHECK((kFlagLs & (kFlagRf | kFlagSun)) == 0);

  // 4j. M5.5 step 1 refraction. (1 - a) is the FS's dC/dRefr: refract_swap(C, 1 - a, Lc, Refr) is the FS
  //     with its screen sample Refr replaced by Lc (the colour copy at the traced hit), for any Lc; Lc = Refr
  //     is the identity bit for bit; a = 1 (opaque water) never changes C. Together with the other swaps
  //     (independent terms of the FS) the single clamp equals the FS with every swap.
  {
    const FsTerms base{0.25f, 0.04f, 0.3f, 2.2f, 0.0196f, 0.0101725f, 0.93f, 0.002f, 0.97f, 0.003f, 0.5f, 0.07f, 0.02f};
    uint32_t seed = 77u;
    for (int it = 0; it < 4000; ++it) {
      FsTerms f = base;
      f.a = it % 7 == 0 ? 1.0f : it % 11 == 0 ? 0.0f : u01(seed);
      f.refr = 4.0f * u01(seed) * u01(seed);
      f.t = 0.5f + 0.5f * u01(seed);
      const float c = fs_colour(f);
      const float rw = record_rw(f);
      const float lc = it % 13 == 0 ? f.refr : 4.0f * u01(seed) * u01(seed);
      FsTerms g = f;
      g.refr = lc;
      const float want = fs_colour(g), got = refract_swap(c, rw, lc, f.refr);
      const float tol = 4e-6f * (1.0f + std::fabs(c) + f.refr + lc);
      CHECK(std::fabs(got - (want > 0.0f ? want : 0.0f)) <= tol);
      if (lc == f.refr) CHECK(got == (c > 0.0f ? c : 0.0f));   // the identity, bit for bit
      if (f.a == 1.0f) CHECK(got == (c > 0.0f ? c : 0.0f));     // opaque: the weight is exactly 0
      // dC/dRefr numerically (C is linear in Refr): 1 - a.
      FsTerms h = f;
      h.refr = f.refr + 1.0f;
      CHECK(std::fabs((fs_colour(h) - c) - rw) <= 4e-6f * (1.0f + std::fabs(c)));
      // With the reflection swap in the same expression: the FS with both swaps.
      const float L = 0.6f * f.k * f.sp;
      FsTerms both = g;
      both.sp = L / f.k;
      const float wb = fs_colour(both);
      const float gb = refract_swap(c + record_a(f) * L - record_b(f), rw, lc, f.refr);
      if (wb > 0.0f) CHECK(std::fabs(gb - wb) <= tol + 4e-6f * record_b(f));
    }
    // n from the FS's own F0: the audited water's 0.16 x 0.35^2 = 0.0196 gives water's index.
    float n = 0.0f;
    CHECK(ior_from_f0(0.16f * 0.35f * 0.35f, n) && std::fabs(n - 1.3256f) < 5e-4f);
    CHECK(std::fabs(((n - 1) / (n + 1)) * ((n - 1) / (n + 1)) - 0.0196f) < 1e-6f);   // the round trip
    CHECK(ior_from_f0(0.0f, n) && n == 1.0f);                                         // no interface: no bend
    CHECK(!ior_from_f0(1.0f, n) && !ior_from_f0(-0.1f, n) && !ior_from_f0(std::nanf(""), n));
    // Snell: n sin(theta_t) = sin(theta_i), the refracted ray in the plane of incidence, unit length,
    // below the surface, bent towards the normal; normal incidence goes straight through.
    for (int it = 0; it < 2000; ++it) {
      const float th = 1.5f * u01(seed), ph = 6.2831853f * u01(seed);
      const float nn[3] = {0.0f, 1.0f, 0.0f};
      const float in[3] = {std::sin(th) * std::cos(ph), -std::cos(th), std::sin(th) * std::sin(ph)};
      float t[3];
      CHECK(refract_dir(in, nn, 1.0f / 1.3256f, t));
      const float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
      CHECK(std::fabs(len - 1.0f) < 1e-5f && t[1] < 0.0f);
      const float st = std::sqrt(t[0] * t[0] + t[2] * t[2]);
      CHECK(std::fabs(1.3256f * st - std::sin(th)) < 2e-5f && st <= std::sin(th) + 1e-6f);
      CHECK(std::fabs(t[0] * in[2] - t[2] * in[0]) < 2e-5f);   // same azimuth: in the plane of incidence
    }
    const float down[3] = {0.0f, -1.0f, 0.0f}, up[3] = {0.0f, 1.0f, 0.0f};
    float t[3];
    CHECK(refract_dir(down, up, 1.0f / 1.3256f, t) && t[0] == 0.0f && t[2] == 0.0f && std::fabs(t[1] + 1.0f) < 1e-6f);
    const float graze[3] = {0.999f, -0.0447f, 0.0f};
    CHECK(!refract_dir(graze, up, 1.3256f, t));   // water to air beyond the critical angle: total internal reflection
    // The statistics words: after the local-light words, inside the 128-word slot, sums as 64-bit pairs.
    CHECK(kCsRrRec >= kCsLsWords && kCsRrNoCopy < kCsRrSumGame && kCsRrSumRwTraced + 2 == kCsRrWords);
    CHECK(kCsRrWords <= kCsSlotWordsLs);
    for (uint32_t w : {kCsRrSumGame, kCsRrSumNew, kCsRrSumAll, kCsRrSumShift, kCsRrSumRwHidden, kCsRrSumRwTraced})
      CHECK(w % 2 == 0);
    CHECK((kFlagRefr & (kFlagRf | kFlagSun | kFlagLs)) == 0);
    // The record buffer: 104-B records get the same pixel count as the 32-B ones (record_mb), and the
    // buffer description round-trips the size (record_bytes_of).
    const uint32_t cap_rr = capacity_of_mb(record_mb(160, kRecordBytesRefr), kRecordBytesRefr);
    CHECK(cap_rr >= cap - 2 && buffer_bytes(cap_rr, kRecordBytesRefr) <= (520ull << 20));
    CHECK(record_mb(160, kRecordBytesLs) == 480 && record_mb(160, kRecordBytesSun) == 160 && record_mb(160, kRecordBytes) == 160);
    ComposeBuffer rb{};
    rb.record_bytes = kRecordBytesRefr;
    CHECK(record_bytes_of(rb) == kRecordBytesRefr);
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
  CHECK(lighting_use(100 - kLightingLagFrames, 100) == LightingUse::kEarlier);   // 1 + the frames recorded ahead
  CHECK(lighting_use(100 - kLightingLagFrames - 1, 100) == LightingUse::kNone);  // lighting stopped
  CHECK(lighting_use(101, 100) == LightingUse::kNone);         // never from the future
  // Frame indices count the app's presents (DLSS-G's generated ones do not advance them): 1 x (1 + 2).
  CHECK(kIndicesPerAppFrame == 1 && kLightingLagFrames == 1 + kRecordAheadFrames);
  CHECK(lighting_refusal(0, 100) == LightingRefusal::kCleared);
  CHECK(lighting_refusal(100 - kLightingLagFrames - 1, 100) == LightingRefusal::kOld);
  CHECK(lighting_refusal(101, 100) == LightingRefusal::kAhead);

  // 4d2. The scene pair of a pass (the 11-01 flicker: "no scene" after scene-wait timeouts).
  {
    int newer_calls = 0, keep_calls = 0;
    bool newer_ok = true, keep_ok = true;
    auto take = [&] { ++newer_calls; return newer_ok; };
    auto keep = [&] { ++keep_calls; return keep_ok; };
    auto pick = [&](LightingUse u, bool cur_ok, bool is_stash, uint64_t built, uint64_t now) {
      newer_calls = keep_calls = 0;
      return pick_scene(u, cur_ok, is_stash, built, now, take, keep);
    };
    CHECK(pick(LightingUse::kNone, true, true, 100, 100) == ScenePick::kNone && !newer_calls && !keep_calls);
    CHECK(pick(LightingUse::kThisFrame, false, false, 0, 100) == ScenePick::kStash && !newer_calls && !keep_calls);
    CHECK(pick(LightingUse::kEarlier, true, true, 98, 100) == ScenePick::kStash && !newer_calls && !keep_calls);
    CHECK(pick(LightingUse::kEarlier, true, false, 100, 100) == ScenePick::kNewer && newer_calls == 1 && !keep_calls);
    // The refused case of 11-01: a newer build of the previous frame (the lighting's wait timed out).
    // rt_scene_set is never asked (it would wait on the recording thread); the lighting's pair is kept.
    CHECK(pick(LightingUse::kEarlier, true, false, 99, 100) == ScenePick::kStashKept && !newer_calls && keep_calls == 1);
    CHECK(pick(LightingUse::kEarlier, false, false, 0, 100) == ScenePick::kStashKept && !newer_calls && keep_calls == 1);
    newer_ok = false;   // this frame's pair unavailable: the lighting's pair still
    CHECK(pick(LightingUse::kEarlier, true, false, 100, 100) == ScenePick::kStashKept && newer_calls == 1 && keep_calls == 1);
    keep_ok = false;    // the lighting's slot retired: refused (counted, an uncomposed frame)
    CHECK(pick(LightingUse::kEarlier, false, false, 0, 100) == ScenePick::kNone);
  }

  // 4d3. Uncomposed frames and on/off flips per frame index.
  {
    ComposeContinuity c;
    c.note(10, true), c.note(11, true), c.note(11, false);   // a refused extra pass in a composed frame
    c.note(12, false);                                        // a flip
    c.note(13, true), c.note(14, false), c.note(14, false);  // one more, two passes
    c.note(20, false);                                        // after a gap without water: no flip
    c.note(21, true);
    c.close();
    CHECK(c.frames == 7 && c.composed_frames == 4 && c.uncomposed == 3 && c.flips == 2);
    c.reset_counts();
    CHECK(c.frames == 0 && c.uncomposed == 0 && c.flips == 0);
  }

  // 4e. The statistics' group reduction (the 7.2 ms of m5w3-fg2): a group's sum in one 32-bit
  //     groupshared word, then one 64-bit add, gives the same counter as the per-wave adds it
  //     replaced, for any values up to the shader's clamp (worst case: every lane at the clamp).
  {
    CHECK(uint64_t(uint32_t(kCsGroupSumMax)) == kCsGroupSumMax);
    CHECK(float(10000.0f * 4096.0f + 0.5f) <= float(kCsQuantMax) + 4.0f &&
          kCsGroupLanes * (kCsQuantMax + 4) < (1ull << 32));   // float rounding of the quantiser stays inside
    uint32_t lo_w = 0xfffff000u, hi_w = 7, lo_g = lo_w, hi_g = hi_w;   // start near a carry
    uint64_t want = (uint64_t(hi_w) << 32) | lo_w;
    uint32_t seed = 12345;
    for (int g = 0; g < 4000; ++g) {
      uint32_t gsum = 0;
      for (int wave = 0; wave < 2; ++wave) {   // 32-lane waves: two per 8x8 group
        uint64_t wsum = 0;
        for (int l = 0; l < 32; ++l) {
          seed = seed * 1664525u + 1013904223u;
          const uint64_t q = (g % 7 == 0) ? kCsQuantMax : (seed >> 8) % (kCsQuantMax + 1);
          wsum += q;
        }
        add64(lo_w, hi_w, uint32_t(wsum));
        gsum += uint32_t(wsum);
        want += wsum;
      }
      add64(lo_g, hi_g, gsum);
    }
    CHECK(lo_w == lo_g && hi_w == hi_g);
    CHECK(((uint64_t(hi_g) << 32) | lo_g) == want);
  }

  // 4f. "[water] compose GPU": pair 0 the lighting copy, the rest the compose dispatches.
  {
    const uint64_t q[12] = {100, 1, 300, 1, 1000, 1, 1500, 1, 2000, 1, 2100, 1};
    ComposeMs ms;
    CHECK(compose_ms(q, 3, 0.001, ms) && std::fabs(ms.copy - 0.2) < 1e-12 && std::fabs(ms.compose - 0.6) < 1e-12);
    CHECK(compose_ms(q, 1, 0.001, ms) && ms.compose == 0.0);
    uint64_t bad[12];
    std::memcpy(bad, q, sizeof q);
    bad[7] = 0;   // an end timestamp not available
    CHECK(!compose_ms(bad, 3, 0.001, ms));
    std::memcpy(bad, q, sizeof q);
    bad[6] = 900;   // reversed
    CHECK(!compose_ms(bad, 3, 0.001, ms));
    CHECK(!compose_ms(q, 0, 0.001, ms));
  }

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
    // M5.4 off: the M5.3 module word for word (FNV-1a of the patch made by main 92c71e0, same addr / cap).
    // The pins exist for the three FS mapped before the code paths were extended (they prove each
    // extension leaves the earlier module unchanged); the FS mapped later go through the same calls
    // (the off-paths are compared call against call below) and print their hashes.
    const bool pinned = fi.fs == 0x7b44a567771ecbecull || fi.fs == 0x99c1fcef82b253c1ull || fi.fs == 0xc4e92ccdb08dd777ull;
    const uint64_t m53 = fi.fs == 0x7b44a567771ecbecull ? 0x7118fb90f4599508ull
                       : fi.fs == 0x99c1fcef82b253c1ull ? 0xb0d6c3fdb58567e3ull
                       : fi.fs == 0xc4e92ccdb08dd777ull ? 0x59718e7c5505cc1dull : 0;
    if (pinned) CHECK(fnv(out.data(), out.size() * 4) == m53);
    // M5.4 on: the sun words (two more stores and their values) on top of the same module.
    std::vector<uint32_t> out_sun;
    const PatchStatus st_sun = patch_fs(words.data(), words.size(), fi, addr, cap_sun, out_sun, true);
    std::printf("patch %016llx with the sun term %s: %zu -> %zu words\n", (unsigned long long)fi.fs,
                patch_status_name(st_sun), words.size(), out_sun.size());
    CHECK(st_sun == PatchStatus::kApplied && out_sun.size() > out.size() && out_sun[3] > out[3]);
    {
      FsIds nosun = fi;   // a sun id of the wrong type is refused only when the sun words are asked for
      nosun.sun_m = fi.sun_d;
      std::vector<uint32_t> tmp;
      CHECK(patch_fs(words.data(), words.size(), nosun, addr, cap_sun, tmp, true) == PatchStatus::kIdMismatch);
      CHECK(patch_fs(words.data(), words.size(), nosun, addr, cap, tmp) == PatchStatus::kApplied);
    }
    // M5.4b on: the local-light words on top of the sun's (14 more stores); off, the M5.4 module word
    // for word (the call with sun only is unchanged by the ls code: pinned against the same call).
    std::vector<uint32_t> out_ls, out_sun2;
    const PatchStatus st_ls = patch_fs(words.data(), words.size(), fi, addr, cap_ls, out_ls, false, true);
    std::printf("patch %016llx with the sun and local-light terms %s: %zu -> %zu words\n", (unsigned long long)fi.fs,
                patch_status_name(st_ls), words.size(), out_ls.size());
    CHECK(st_ls == PatchStatus::kApplied && out_ls.size() > out_sun.size() && out_ls[3] > out_sun[3]);
    CHECK(patch_fs(words.data(), words.size(), fi, addr, cap_sun, out_sun2, true, false) == PatchStatus::kApplied &&
          out_sun2 == out_sun);
    std::printf("patch %016llx with the sun term: FNV-1a %016llx\n", (unsigned long long)fi.fs,
                (unsigned long long)fnv(out_sun.data(), out_sun.size() * 4));
    // FNV-1a of the M5.4 sun patch, same addr / cap: words 8-9 hold R = C - D and lum(D) since the m5w4
    // fix (branch worktree-agent-abd9303a6e22b012e; fc07ad4 stored D: e865b2c8..., e3d58680..., 3e2eb195...).
    const uint64_t m54 = fi.fs == 0x7b44a567771ecbecull ? 0x3bd081bf3d86accdull
                       : fi.fs == 0x99c1fcef82b253c1ull ? 0x3e33b8e12f2960c4ull
                       : fi.fs == 0xc4e92ccdb08dd777ull ? 0x206d5c9203866002ull : 0;
    if (pinned) CHECK(fnv(out_sun.data(), out_sun.size() * 4) == m54);
    {
      FsIds bad = fi;   // a local id of the wrong type is refused only when the local words are asked for
      bad.ls_chan = fi.ls_d12;
      std::vector<uint32_t> tmp;
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_ls, tmp, true, true) == PatchStatus::kIdMismatch);
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_sun, tmp, true) == PatchStatus::kApplied);
      bad = fi;
      bad.ls_z = fi.pos;   // the vec3 position is not the float depth input
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_ls, tmp, true, true) == PatchStatus::kIdMismatch);
      bad = fi;
      bad.ls_d12 = fi.C;   // a vec3 of main that is not a loop sum still types; the table is the claim (spirv-val)
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_ls, tmp, true, true) == PatchStatus::kApplied);
    }
    // M5.4b off M5.5: the local-light patch word for word as before the refraction code (FNV-1a of the LS patch,
    // same addr / cap; 48e1347 made 5389606c..., 9e156a50..., 092432cf... before the m5w4 fix's sun words R, lum D).
    const uint64_t m54b = fi.fs == 0x7b44a567771ecbecull ? 0x80c211df1fd943d2ull
                        : fi.fs == 0x99c1fcef82b253c1ull ? 0xe8f4bc47583553a9ull
                        : fi.fs == 0xc4e92ccdb08dd777ull ? 0x891555672bce545cull : 0;
    if (pinned) CHECK(fnv(out_ls.data(), out_ls.size() * 4) == m54b);
    // M5.5 on: the refraction words on top of the local-light record (2 more stores).
    const uint32_t cap_rr = capacity_of_mb(record_mb(160, kRecordBytesRefr), kRecordBytesRefr);
    std::vector<uint32_t> out_rr, out_ls2;
    const PatchStatus st_rr = patch_fs(words.data(), words.size(), fi, addr, cap_rr, out_rr, false, false, true);
    std::printf("patch %016llx with the refraction words %s: %zu -> %zu words\n", (unsigned long long)fi.fs,
                patch_status_name(st_rr), words.size(), out_rr.size());
    CHECK(st_rr == PatchStatus::kApplied && out_rr.size() > out_ls.size() && out_rr[3] > out_ls[3]);
    CHECK(patch_fs(words.data(), words.size(), fi, addr, cap_ls, out_ls2, true, true, false) == PatchStatus::kApplied &&
          out_ls2 == out_ls);
    {
      FsIds bad = fi;   // the refraction id must be a vec4 of main; refused only when its words are asked for
      bad.refr = fi.C;
      std::vector<uint32_t> tmp;
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_rr, tmp, true, true, true) == PatchStatus::kIdMismatch);
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_ls, tmp, true, true) == PatchStatus::kApplied);
      bad.refr = fi.fog;   // a vec4 of main that is not the copy sample still types; the table is the claim
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_rr, tmp, true, true, true) == PatchStatus::kApplied);
    }
    // M5.4c on: the game-shadow words after each sun form (2 more stores); off, every module above is the
    // call's own (pinned). A game-shadowed FS (vg) without them stores lum(D) = 0 (one more constant).
    const uint32_t s_sun = kRecordBytesSun + kGshBytes, s_ls = kRecordBytesLs + kGshBytes, s_rr = kRecordBytesRefr + kGshBytes;
    const uint32_t cg_sun = capacity_of_mb(record_mb(160, s_sun), s_sun), cg_ls = capacity_of_mb(record_mb(160, s_ls), s_ls);
    const uint32_t cg_rr = capacity_of_mb(record_mb(160, s_rr), s_rr);
    std::vector<uint32_t> out_gsun, out_gls, out_grr;
    CHECK(patch_fs(words.data(), words.size(), fi, addr, cg_sun, out_gsun, true, false, false, true) == PatchStatus::kApplied &&
          out_gsun.size() > out_sun.size());
    CHECK(patch_fs(words.data(), words.size(), fi, addr, cg_ls, out_gls, false, true, false, true) == PatchStatus::kApplied &&
          out_gls.size() > out_ls.size());
    CHECK(patch_fs(words.data(), words.size(), fi, addr, cg_rr, out_grr, false, false, true, true) == PatchStatus::kApplied &&
          out_grr.size() > out_rr.size());
    {
      std::vector<uint32_t> tmp;   // gsh implies the sun form
      CHECK(patch_fs(words.data(), words.size(), fi, addr, cg_sun, tmp, false, false, false, true) == PatchStatus::kApplied &&
            tmp == out_gsun);
      FsIds bad = fi;   // Vg must be a float of main; refused only with a sun form
      bad.vg = fi.N;
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cg_sun, tmp, true, false, false, true) == PatchStatus::kIdMismatch);
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap_sun, tmp, true) == PatchStatus::kIdMismatch);
      CHECK(patch_fs(words.data(), words.size(), bad, addr, cap, tmp) == PatchStatus::kApplied && tmp == out);
      if (fi.vg) {
        FsIds novg = fi;
        novg.vg = 0;
        std::vector<uint32_t> a, b;
        CHECK(patch_fs(words.data(), words.size(), novg, addr, cap_sun, a, true) == PatchStatus::kApplied);
        CHECK(patch_fs(words.data(), words.size(), fi, addr, cap_sun, b, true) == PatchStatus::kApplied);
        CHECK(b.size() == a.size() + 3 && b != a && b == out_sun);   // + OpConstantNull float: the skip's lum(D) = 0
        std::vector<uint32_t> g0;   // with the words, an FS without vg stores Dg = 0, Vg = 1 (no division)
        CHECK(patch_fs(words.data(), words.size(), novg, addr, cg_sun, g0, true, false, false, true) == PatchStatus::kApplied &&
              g0.size() < out_gsun.size());
      }
    }
    std::printf("patch %016llx with the game-shadow words (%s): sun %zu, local %zu, refraction %zu words\n",
                (unsigned long long)fi.fs, fi.vg ? "Vg" : "no game shadow", out_gsun.size(), out_gls.size(), out_grr.size());
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
      std::snprintf(name, sizeof name, "/water_fs_%08x_patched_sun.spv", unsigned(fi.fs >> 32));
      std::ofstream os(std::string(argv[2]) + name, std::ios::binary);
      os.write(reinterpret_cast<const char*>(out_sun.data()), std::streamsize(out_sun.size() * 4));
      std::snprintf(name, sizeof name, "/water_fs_%08x_patched_ls.spv", unsigned(fi.fs >> 32));
      std::ofstream ol(std::string(argv[2]) + name, std::ios::binary);
      ol.write(reinterpret_cast<const char*>(out_ls.data()), std::streamsize(out_ls.size() * 4));
      std::snprintf(name, sizeof name, "/water_fs_%08x_patched_refr.spv", unsigned(fi.fs >> 32));
      std::ofstream orr(std::string(argv[2]) + name, std::ios::binary);
      orr.write(reinterpret_cast<const char*>(out_rr.data()), std::streamsize(out_rr.size() * 4));
      const std::pair<const char*, const std::vector<uint32_t>*> gm[] = {
          {"sun_gsh", &out_gsun}, {"ls_gsh", &out_gls}, {"refr_gsh", &out_grr}};
      for (const auto& [kind, mod] : gm) {
        std::snprintf(name, sizeof name, "/water_fs_%08x_patched_%s.spv", unsigned(fi.fs >> 32), kind);
        std::ofstream og(std::string(argv[2]) + name, std::ios::binary);
        og.write(reinterpret_cast<const char*>(mod->data()), std::streamsize(mod->size() * 4));
      }
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
        // One body under two hashes (the static and instanced PL_noCaustics: another engine CB layout)
        // has the same ids: that table fits by construction.
        if (std::memcmp(&swapped.a, &fi.a, offsetof(FsIds, refr) + sizeof fi.refr - offsetof(FsIds, a)) == 0) continue;
        std::vector<uint32_t> tmp;
        CHECK(patch_fs(words.data(), words.size(), swapped, 0x1000, cap, tmp) == PatchStatus::kIdMismatch);
      }
    }
  }
  if (!patched) std::printf("no dumped module given: the patch of the game modules is not exercised\n");
  std::printf("water_compose_test: %u checks passed\n", checks);
  return 0;
}
