#pragma once
// M5.3 water composition (docs/PLAN_M5_WATER.md M5.3, rt_water_compose, default 0): what the
// layer (layer/water.cpp) and the core (core/water.cpp, shaders/rt/water_compose.hlsl) share.
//
// The capture ("the minimal extra copy"): the factors of the probe term are not reconstructable
// after the forward water pass (the FS's normal needs its interpolated UV and tangent frame,
// its opacity and fog need the flow-mapped textures at that UV), so the audited water FS writes
// them itself. common/water_fs_patch.h adds, at the end of its main, eight (M5.4: ten) 32-bit stores into
// one device buffer (a physical storage buffer address baked into the patched module; the
// game's pipeline layout and descriptor sets are untouched). The layer swaps the variant in for
// the game's pipeline around each audited water draw while the key is on; its colour output is
// bit-identical (the patch only adds stores and EarlyFragmentTests, valid because the FS
// neither discards nor writes depth).
//
// Buffer: kHeaderBytes of header (word 0 pitch = render width, written by the core before the
// forward passes, which also zeroes the whole buffer once), then capacity + 1 records of
// kRecordBytes (the last one a sink for pixels beyond pitch/capacity). The capacity and the
// address are baked into the patched module, so a store never leaves the buffer.
// Record of pixel (x, y) at kHeaderBytes + (y * pitch + x) * kRecordBytes, the FS's own values:
//   w0 half2(A.r, A.g)  w1 half2(A.b, B.r)  w2 half2(B.g, B.b)
//   w3 half2(N.x, N.y)  w4 half2(N.z, C.r)  w5 half2(C.g, C.b)
//   w6 asuint(t)        w7 unorm2x16(1, roughness): low half 0xffff = written, 0 = consumed
// with G = a * T * X (opacity _784, fog transmittance _4096.w, atmosphere _2063), k = _3799
// (set 2 b0 _m0[0] * 100: the probe-to-lighting scale), E = F0 * DFG.x + DFG.y (the FS's LUT b25
// at (N.V, roughness)), A = G * E, B = G * k * Ks (Ks = _3379, the probe radiance times E after the
// local-probe loop: the FS adds Ks * k), N = _1207,
// C = _4335 (the colour before the FS's max(0) and the attachment encoding), t = |_5| (camera
// distance), roughness = _1480. The compose pass replaces the probe term where a traced ray
// resolves: C' = max(C + A * L - B, 0) (no double count: B is the FS's own term, subtracted
// exactly; unresolved rays keep it, C' = C). L, the traced hit's lit radiance (gDiffuse +
// gSpecular), is in the lighting's units, in which a probe of radiance S contributes S * E * k
// (D-EN4, TRACK_D_REFLECTIONS 10.1: b11 holds S * k): so A carries no k and B does, and a ray
// that sees exactly what the probe holds (L = S k) leaves C unchanged.
//
// Ownership check: storage writes of overlapping water fragments are unordered, the attachment
// is not, so a record is used only when the target pixel holds its C (same_encoded below,
// the B10G11R11 precision). The compose pass consumes each record (w7 = 0).
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace bg3::water {

inline constexpr uint32_t kHeaderBytes = 64;
inline constexpr uint32_t kRecordBytes = 32;
inline constexpr uint32_t kRecordWords = kRecordBytes / 4;
// M5.4 (rt_water_shadows on when the water pipelines are created): the record grows by two words,
//   w8 half2(R.r, R.g)  w9 half2(R.b, lum(D))
// D = G * Dsun, Dsun = the FS's sun term in Lw (diffuse + specular, cloud shadow and k included,
// NO geometric visibility: the audited water FS sample no cascade, only the clouds), so dC/dDsun = G,
// and R = C - D (in the FS, f32) is the FS's colour with sun visibility 0. The compose replaces the
// FS's sun visibility (1) by the traced V: C' = V C + (1 - V) R, i.e. R where the sun ray is occluded,
// C (bit for bit) where it is lit. R and not D is recorded: C - D formed in the compose from the
// B10G11R11 target (one ulp = 2^-6 / 2^-5 of C) loses the rest wherever the sun is most of C (the
// glint), which turned the shadowed S8 pool's sun-dominated pixels black or bright (m5w4 2026-11-01:
// 1213 clamped records per frame). lum(D) > 0 marks a sun term (statistics, debug view 26). Without
// the key at creation the patch, the buffer and the compose shader are the M5.3 ones, word for word.
inline constexpr uint32_t kRecordBytesSun = 40;
inline constexpr uint32_t kRecordWordsSun = kRecordBytesSun / 4;
// M5.4b (rt_water_ls_shadows on when the water pipelines are created): the sun record plus the FS's
// local-light inputs, 14 more words (96 B):
//   w10 half2(F.r, F.g)      w11 half2(F.b, f0)         F = G * m (dC/d(Ld + Ls)), f0 = the FS's F0.x
//   w12 half2(L.r, L.g)      w13 half2(L.b, 1 + chan)   L = Ld12 + Ls12: the point + spot loops' sums
//   w14 half2(dif.r, dif.g)  w15 half2(dif.b, box)      the FS's diffuse colour; box = lum(F (Lloc - L))
//   w16 asuint(z)            the FS's froxel depth input (location 0, log2-sliced by set 2 b26)
//   w17..19 asuint(P)        the FS's camera-relative position _5 (f32: the light vectors)
//   w20..22 asuint(N)        the FS's shading normal (f32: GGX at roughness 0.09 needs it, below)
//   w23 asuint(roughness)    f32
// chan = the draw's light channel (set 1 b0 _m1 & 31), 1 + chan > 0 marks the words written; box is
// the projector (third) loop's share of the local term, which stays the FS's. The compose walks the
// same froxel list as the FS (the forward pass's own set 2 b0/b10/b11/b12/b26/b27/b28, the D-LS
// structures of shaders/rt/bg3_lights.hlsli: identical decorations), recomputes each light's term
// c_k with the FS's own BRDF (bg3_light_eval) and the game's atlas term s_k for shadow-mapped lights,
// checks sum c_k s_k == L, and traces the lights the game leaves unshadowed:
//   C' = max((occluded ? R : C) [+ A L - B] - F * sum_k (1 - V_k) c_k, 0).
inline constexpr uint32_t kRecordBytesLs = 96;
inline constexpr uint32_t kRecordWordsLs = kRecordBytesLs / 4;
// M5.5 step 1 (rt_water_refraction on when the water pipelines are created): the M5.4b record plus the
// FS's refraction sample and its weight, 2 more words (104 B):
//   w24 half2(Refr.r, Refr.g)   w25 half2(Refr.b, 1 - a)
// Refr = the FS's screen-space refraction sample (its colour copy, set 2 b4, at the pixel plus the
// wave offset: _4332 / _4530 / _3668), 1 - a = dC/dRefr (C = mix(Refr, surface, a), a scalar). The
// compose traces the refracted ray (Snell at the FS's own N, n from the FS's own F0) and, where its hit
// resolves on screen, reads the same colour copy there: C' = max(C + (1 - a) (Copy(hit) - Refr), 0).
// The medium (the game's opacity a over the view-depth difference) is unchanged: only WHERE the bed
// is seen changes. Unresolved rays (hidden, off screen, miss) keep the FS's Refr.
inline constexpr uint32_t kRecordBytesRefr = 104;
inline constexpr uint32_t kRecordWordsRefr = kRecordBytesRefr / 4;
// M5.4c (rt_water_game_shadow with rt_water_shadows on when the water pipelines are created): two more
// words after the sun form's last (the 40-, 96- or 104-byte record), for the FS that sample the game's
// cascade shadow map (common/water_fs_patch.h FsIds::vg: PL_noCaustics_Shadows, huge_PL_Shadow):
//   wG0 half2(Dg.r, Dg.g)   wG1 half2(Dg.b, Vg)
// Their Lsun carries the game's sun visibility Vg (pow(PCF, 2.2) over the cascades) as a scalar factor, so
// the FS's sun term is D = Vg D_u (D_u the same term at visibility 1: every operation from Vg to the colour
// is a product, derived and checked by tools/water_audit.py) and Dg = D (1 - Vg) / Vg = D_u - D is the sun
// light the game's shadow removed. R = C - D (w8-w9) is the FS at sun visibility 0 as before. The compose
// swaps Vg for the traced V: C' = V (C + Dg) + (1 - V) R = R + V D_u (gsh_shadow below): lit gives C + Dg,
// occluded R, unknown C. V = Vg gives C (exactly where Vg = 1: Dg = 0). FS without a Vg store Dg = 0, Vg = 1.
// Without the words, a game-shadowed FS stores lum(D) = 0: the sun step skips it (its game shadow stays).
inline constexpr uint32_t kGshBytes = 8;
inline constexpr uint32_t kGshWords = kGshBytes / 4;
inline constexpr uint32_t kBufGsh = 1u;   // ComposeBuffer::flags: the records carry the M5.4c words

inline constexpr const char* kComposeKey = "bg3rtx.water_compose";
inline constexpr uint32_t kComposeVersion = 1;

// Published once by the layer when it created the buffer (before any variant exists); the
// buffer lives until the device is destroyed. version is written last (release).
struct ComposeBuffer {
  uint32_t version;     // kComposeVersion once valid
  uint32_t capacity;    // records (without the sink)
  uint64_t buffer;      // VkBuffer (STORAGE_BUFFER | SHADER_DEVICE_ADDRESS | TRANSFER_DST)
  uint64_t bytes;       // its size
  uint32_t record_bytes;   // kRecordBytes, kRecordBytesSun (M5.4), kRecordBytesLs (M5.4b) or kRecordBytesRefr (M5.5); 0 (an older layer) = kRecordBytes
  uint32_t flags;          // kBufGsh (M5.4c: + kGshBytes per record, sun forms only); 0 from an older layer
  uint32_t pad[8];
};
static_assert(sizeof(ComposeBuffer) == 64, "ComposeBuffer layout is shared by the layer and the core");

inline uint64_t buffer_bytes(uint32_t capacity, uint32_t record_bytes = kRecordBytes) {
  return uint64_t(kHeaderBytes) + (uint64_t(capacity) + 1) * record_bytes;
}
// Records that fit in `mb` MiB (header and sink included), at least 1.
inline uint32_t capacity_of_mb(int mb, uint32_t record_bytes = kRecordBytes) {
  const uint64_t bytes = uint64_t(mb < 1 ? 1 : mb > 2048 ? 2048 : mb) << 20;
  const uint64_t n = (bytes - kHeaderBytes) / record_bytes;
  return n > 1 ? uint32_t(n - 1) : 1u;
}
inline uint32_t record_bytes_of(const ComposeBuffer& b) {
  return b.record_bytes == kRecordBytesSun || b.record_bytes == kRecordBytesLs || b.record_bytes == kRecordBytesRefr
             ? b.record_bytes
             : kRecordBytes;
}
// The record buffer's MiB for rt_water_compose_mb (sized for 32-B records): the 96-B records of M5.4b
// (and M5.5's 104-B ones) get the same pixel count (160 -> 480 / 520 MB covers 5.2 Mpx); M5.3 / M5.4
// use the key as is.
inline int record_mb(int mb, uint32_t record_bytes) {
  return record_bytes >= kRecordBytesLs ? int((int64_t(mb) * record_bytes + kRecordBytes - 1) / kRecordBytes) : mb;
}

// M5.4c: whether the records carry the game-shadow words (a sun form only), and the record stride.
inline bool gsh_of(const ComposeBuffer& b) { return (b.flags & kBufGsh) != 0 && record_bytes_of(b) >= kRecordBytesSun; }
inline uint32_t stride_of(const ComposeBuffer& b) { return record_bytes_of(b) + (gsh_of(b) ? kGshBytes : 0u); }

// All record layouts keep the FS written marker in word 7.
inline uint8_t coverage_at(const uint32_t* records, std::size_t pixel, uint32_t stride) {
  return (records[pixel * (stride / 4) + 7] & 0xffffu) != 0u ? 1 : 0;
}

inline void publish_buffer(ComposeBuffer* dst, uint64_t buffer, uint64_t bytes, uint32_t capacity,
                           uint32_t record_bytes = kRecordBytes, uint32_t flags = 0) {
  dst->buffer = buffer;
  dst->bytes = bytes;
  dst->capacity = capacity;
  dst->record_bytes = record_bytes;
  dst->flags = flags;
  std::atomic_ref<uint32_t>(dst->version).store(kComposeVersion, std::memory_order_release);
}
inline bool read_buffer(const ComposeBuffer* src, ComposeBuffer& out) {
  if (!src) return false;
  const uint32_t v = std::atomic_ref<uint32_t>(const_cast<ComposeBuffer*>(src)->version).load(std::memory_order_acquire);
  if (v != kComposeVersion) return false;
  out = *src;
  return out.buffer != 0 && out.capacity != 0 && out.bytes >= buffer_bytes(out.capacity, stride_of(out));
}

// Whether a B10G11R11 channel value t (read back from the attachment) is the encoding of the
// FS's value c >= 0: within one unit in the last place of the channel's format at c (6 mantissa
// bits for R and G, 5 for B; the conversion may round to nearest or toward zero, so one ulp
// covers both), and the smallest normal 2^-14 for values below it (denormals). Mirrored in
// shaders/rt/water_compose.hlsl same_encoded(); derived from the format, not tuned.
inline bool same_encoded(float c, float t, int mantissa_bits) {
  if (!(c >= 0.0f) || !(t >= 0.0f) || !std::isfinite(c) || !std::isfinite(t)) return false;
  const float m = c > 6.103515625e-05f ? c : 6.103515625e-05f;   // 2^-14
  const float ulp = std::ldexp(1.0f, std::ilogb(m) - mantissa_bits);
  return std::fabs(t - c) <= ulp;
}

// ---- The FS's end of main and the composition, per channel (pinned by tests/water_compose_test) ----
// The audited FS (common/water_fs_patch.h table): C = mix(Refr, ((direct + (Kd + Ks) k) X + Ge) T + S, a),
// Ks = Sp E (Sp the probe radiance at R, E = F0 DFG.x + DFG.y), k = exposure (set 2 b0 _m0[0], the same
// pre-exposure as the lighting's s1b13[0]) * 100. dC/dKs = a T X k exactly, so the record's
// A = a T X E and B = a T X k Ks, and the composition C' = max(C + A L - B, 0) swaps k Sp for L
// (the hit's lit radiance, in the lighting's pre-exposed units, in which the lighting's own probe
// term is Sp k). L = k Sp gives C' = C; the energy change is a T X E (L - k Sp): a darker traced
// hit than the probe's radiance in that direction, never a unit factor.
// M5.6: E is the interface DFG reflectance; opacity partitions only the column.
struct SurfacePartition { float interface_weight, column_weight, transmission_weight; };
inline SurfacePartition surface_partition(float e, float a) {
  return {e, (1.0f - e) * a, (1.0f - e) * (1.0f - a)};
}
struct FsTerms {
  float refr, direct, kd, sp, e, k, x, ge, t, s, a;
  float sun = 0.0f;   // M5.4: the sun term of Lw (direct holds the local lights), visibility 1 in the FS
  float local = 0.0f; // M5.4b: the point + spot lights' sum L of Lw (the FS's own per-light visibility)
  float vg = 1.0f;    // M5.4c: the game's sun visibility in Lsun (1: the FS samples no shadow map)
};
inline float fs_colour(const FsTerms& f) {
  const float lw = f.direct + f.sun * f.vg + f.local + (f.kd + f.sp * f.e) * f.k;
  return f.refr + ((lw * f.x + f.ge) * f.t + f.s - f.refr) * f.a;
}
inline float record_a(const FsTerms& f) { return f.x * (f.a * f.t) * f.e; }
inline float record_b(const FsTerms& f) { return f.x * (f.a * f.t * f.k) * (f.sp * f.e); }
inline float compose(float c, float a, float l, float b) {
  const float v = c + a * l - b;
  return v > 0.0f ? v : 0.0f;
}
// ---- M5.4 sun visibility (rt_water_shadows; shaders/rt/water_compose.hlsl WATER_SHADOWS) ----
// D = a T X Dsun = dC/dDsun * Dsun: the record's sun word, the colour the FS's unshadowed sun adds.
// The FS lights the water with sun visibility 1 (no cascade sample in any audited water FS; the
// cloud shadow is part of its sun radiance and stays). The compose swaps that 1 for the traced
// visibility V of one ray towards the sun disc: C' = V C + (1 - V) R with R = C - D the FS's colour
// without its sun term, recorded by the FS itself (with M5.3 on, the reflection's A L - B is added in
// the same expression: the two terms are independent in the FS). V = 1 (lit, or an aborted / unknown
// ray: the FS's own value) leaves C bit-identical; only the visibility changes, never the unshadowed
// radiance. C here is the target's value (B10G11R11), R the record's (half of the FS's f32 C - D).
inline float record_d(const FsTerms& f) { return f.x * (f.a * f.t) * (f.sun * f.vg); }   // the FS's D (Vg in it)
inline float record_r(const FsTerms& f) { return fs_colour(f) - record_d(f); }   // the FS's f32 C - D
inline float shadow(float c, float r, float v) {
  if (v == 1.0f) return c;
  const float s = v * c + (1.0f - v) * r;
  return s > 0.0f ? s : 0.0f;
}
// ---- M5.4c the game's sun shadow (rt_water_game_shadow; water_compose.hlsl WATER_GSH) ----
// The game-shadowed FS (FsIds::vg): D = Vg D_u, so the record's Dg = D (1 - Vg) / Vg (in the FS, f32) is the
// sun light the game's shadow removed, and C + Dg the FS at sun visibility 1. The compose swaps Vg for the
// traced V: C' = V (C + Dg) + (1 - V) R = R + V D_u. V = 1 with Vg = 1 (Dg = 0) is C bit for bit; V = Vg is C
// within the record's rounding (R and Dg halves); V = 0 is R, the same as M5.4 (independent of Vg).
inline float record_dg(const FsTerms& f) { return record_d(f) * ((1.0f - f.vg) / f.vg); }
inline float gsh_shadow(float c, float r, float dg, float v) {
  if (v == 1.0f && dg == 0.0f) return c;
  const float s = v * (c + dg) + (1.0f - v) * r;
  return s > 0.0f ? s : 0.0f;
}
// ---- M5.4b local-light visibility (rt_water_ls_shadows; water_compose.hlsl WATER_LS) ----
// F = a T X m = dC/dL (the record's w10-w11): the FS adds its local sums with the same factor as the
// sun's. The compose removes F * sum_k (1 - V_k) c_k over the lights the game leaves unshadowed
// (no atlas map: the FS's visibility 1), so C' is the FS with V_k in place of that 1; lit, unknown
// and shadow-mapped lights keep their term bit for bit (C' = C when nothing is occluded).
inline float record_f(const FsTerms& f) { return f.x * (f.a * f.t); }   // m = 1 in the audited FS (mix(1, 1, b26 _m1))
inline float ls_shadow(float c, float f, float removed_sum) {
  const float r = c - f * removed_sum;
  return r > 0.0f ? r : 0.0f;
}
// The sum check (the M5.4b gate): the recomputed sum_k c_k s_k against the record's L, per pixel, as
// max_c |sum - L| / max_c L. The record's L is a half: rounding to nearest is within half an ulp,
// 2^-11 relative, and the recompute reads the FS's f32 inputs (P, N, roughness, z: exact) so it
// differs from the FS's f32 evaluation by a few f32 ulps (~1e-6). One half ulp at the bottom of a
// binade (2^-10) bounds both: within it = the same sum. Checked only where max L is a normal half
// (>= 2^-14: below, the half's step is absolute, not relative).
inline constexpr float kLsSumTol = 1.0f / 1024.0f;
inline constexpr float kLsSumFar = 0.1f;          // the log's third bin: a missing / extra light, not precision
inline constexpr float kLsCheckMin = 6.103515625e-05f;   // 2^-14
enum class LsBin : uint32_t { kNone = 0, kWithin, kMid, kFar };
// Relative to the larger of the two maxima: a record without a local term and a recomputed one (an
// extra light) is far, not unchecked. Mirrored in shaders/rt/water_compose.hlsl ls_check_bin.
inline LsBin ls_check_bin(const float sum[3], const float rec[3]) {
  float mx = 0.0f, err = 0.0f;
  for (int c = 0; c < 3; ++c) {
    if (!std::isfinite(sum[c]) || !std::isfinite(rec[c])) return LsBin::kFar;
    mx = rec[c] > mx ? rec[c] : mx;
    mx = sum[c] > mx ? sum[c] : mx;
    const float e = std::fabs(sum[c] - rec[c]);
    err = e > err ? e : err;
  }
  if (!(mx >= kLsCheckMin)) return LsBin::kNone;
  const float r = err / mx;
  return r <= kLsSumTol ? LsBin::kWithin : r <= kLsSumFar ? LsBin::kMid : LsBin::kFar;
}

// ---- M5.5 step 1 refraction (rt_water_refraction; water_compose.hlsl WATER_REFR) ----
// The FS's end of main is C = Refr + (surface - Refr) a: dC/dRefr = 1 - a, exactly (a scalar). The
// record's w25 high half. Swapping the FS's screen sample Refr for the colour copy at the refracted
// ray's hit Lc: C' = max(C + (1 - a) (Lc - Refr), 0) is the FS with Refr replaced by Lc; Lc = Refr is
// the identity bit for bit (the added term is exactly 0).
inline float record_rw(const FsTerms& f) { return 1.0f - f.a; }
inline float refract_swap(float c, float rw, float lc, float refr) {
  const float v = c + rw * (lc - refr);
  return v > 0.0f ? v : 0.0f;
}
// The relative index of refraction from the FS's own F0 (normal incidence, air to the medium):
// F0 = ((n - 1) / (n + 1))^2, so n = (1 + sqrt(F0)) / (1 - sqrt(F0)). The audited water F0 is
// 0.16 Reflectance_Base^2 = 0.0196 (Reflectance_Base 0.35): n = 1.3256, water's 1.333 within the
// game's rounding of its own parameter. Derived, not a constant of ours: a material with another F0
// refracts by its own. F0 outside [0, 1) has no index (the caller keeps the FS's sample).
inline bool ior_from_f0(float f0, float& n) {
  if (!(f0 >= 0.0f) || !(f0 < 1.0f)) return false;
  const float s = std::sqrt(f0);
  n = (1.0f + s) / (1.0f - s);
  return std::isfinite(n);
}
// HLSL refract(i, n, eta) (i the incident direction, n facing against it, eta = n1 / n2), in f32 as
// the shader evaluates it; false on total internal reflection (not reachable from air: eta < 1).
inline bool refract_dir(const float i[3], const float n[3], float eta, float t[3]) {
  const float c = -(i[0] * n[0] + i[1] * n[1] + i[2] * n[2]);
  const float k = 1.0f - eta * eta * (1.0f - c * c);
  if (k < 0.0f) return false;
  const float m = eta * c - std::sqrt(k);
  for (int j = 0; j < 3; ++j) t[j] = eta * i[j] + m * n[j];
  return true;
}
// Statistics words of the WATER_REFR variant (the 128-word slot, after the local-light words):
// transmissive records (owned, valid, refraction words ok, 1 - a > 0), opaque (1 - a = 0), invalid
// words, traced, the tiers (screen hit = composed, hidden, off screen, miss, aborted), not traced (N.V
// <= 0 or no index), clamped, records without a usable copy (its size is not the target's); then
// 64-bit sums x 4096: lum((1 - a) Refr) over composed, lum((1 - a) Lc) over composed, lum((1 - a)
// Refr) over transmissive records, the hit's screen shift in pixels over composed, (1 - a) over hidden
// and over traced (the transmission-weighted hidden share).
inline constexpr uint32_t kCsRrRec = 96, kCsRrOpaque = 97, kCsRrInvalid = 98, kCsRrTraced = 99, kCsRrHit = 100;
inline constexpr uint32_t kCsRrHidden = 101, kCsRrOff = 102, kCsRrMiss = 103, kCsRrAborted = 104, kCsRrNotTraced = 105;
inline constexpr uint32_t kCsRrClamped = 106, kCsRrNoCopy = 107;
inline constexpr uint32_t kCsRrSumGame = 108, kCsRrSumNew = 110, kCsRrSumAll = 112, kCsRrSumShift = 114;
inline constexpr uint32_t kCsRrSumRwHidden = 116, kCsRrSumRwTraced = 118;
inline constexpr uint32_t kCsSurfaceClean = 120, kCsSurfaceInvalid = 121;
inline constexpr uint32_t kCsRrWords = 120;
static_assert(kCsRrWords <= 128, "the refraction words fit the 128-word slot");

// ---- Which lighting a forward water pass composes with (core/water.cpp compose_at_pass_end) ----
// run_post (and so the lighting's copy) is recorded when the lighting's command buffer ENDS
// (core/gi.cpp flush), and the game records the forward water pass in another command buffer,
// most often before that: in m5w3 (S8, 11-01) 117 of 120 passes found only the previous frame's
// lighting stash. The GPU still runs the frame's lighting before its forward pass (the water draws
// over the lit opaque), so everything the compose reads from the lighting is GPU-current: one
// radiance / depth image and ONE camera block that the copy overwrites each frame, the scene TLAS
// + set taken at the pass (hand_out stamps it for the frame that records it). The stash only gives
// the push constants and the statistics slot, so the latest lighting may be this frame's or the
// previous one's. Frame indices advance at the app's presents only (layer/layer.cpp QueuePresentKHR):
// the frames DLSS-G generates are presented by Streamline below the layer (layer/streamline.cpp
// w_QueuePresentKHR) and do not advance them (the [sl] DLSS-G line: 60 indices, 120 displayed at 2x),
// so frame generation adds no lag. The bound: the previous app frame's lighting (1) plus the frames
// the CPU may record ahead of the GPU (2) = 3 indices. A lighting that did not copy (water_rf or
// compose_at_lighting skipped) clears the stash (frame 0): the GPU copy would be stale.
inline constexpr uint64_t kIndicesPerAppFrame = 1, kRecordAheadFrames = 2;
inline constexpr uint64_t kLightingLagFrames = kIndicesPerAppFrame * (1 + kRecordAheadFrames);
enum class LightingUse : uint32_t { kNone = 0, kThisFrame, kEarlier };
inline LightingUse lighting_use(uint64_t stash_frame, uint64_t now) {
  if (!stash_frame || stash_frame > now) return LightingUse::kNone;
  if (stash_frame == now) return LightingUse::kThisFrame;
  return stash_frame + kLightingLagFrames >= now ? LightingUse::kEarlier : LightingUse::kNone;
}
// Borrowed lights, TLAS and camera must describe one lighting frame. The immediately
// previous frame is usable only when compose shifts the ray into that frame's space.
// Failure selects ordinary composition, preserving shadows and refraction.
// Scene/material retention does not retain the game's lighting buffers, views or
// descriptor sets. Same-frame inputs are safe; F-1 inputs only when this frame binds
// the very same lighting list (then the game still owns them after a level unload).
inline constexpr bool hit_shade_inputs_alive(uint64_t lighting_frame, uint64_t now, bool seen_this_frame) {
  return lighting_frame && (lighting_frame == now || seen_this_frame);
}
inline constexpr bool hit_shade_frame_ok(uint64_t lighting_frame, uint64_t now, bool same_tlas,
                                         bool same_lights, bool shifted) {
  return lighting_frame && same_tlas && same_lights &&
      (lighting_frame == now || (shifted && now > lighting_frame && now - lighting_frame == 1));
}
// F-1 must select the lighting stash, even if this frame already has a newer
// TLAS. Validate/retain that exact generation before replacing the ordinary pair.
inline constexpr bool hit_shade_previous_frame(uint64_t lighting_frame,uint64_t now,bool enabled,bool shift) {
  return enabled && shift && lighting_frame && now>lighting_frame && now-lighting_frame==1;
}
// WATER_LS and hit shading share b22..28. A borrowed lighting list can replace
// the forward list only if its identity matches; otherwise keep native LS energy.
inline constexpr bool hit_shade_local_shadows(bool shade,bool previous,bool same_lights) {
  return !shade || !previous || same_lights;
}
inline constexpr uint32_t kCsHitAttempt=57,kCsHitHidden=58,kCsHitOffscreen=59,kCsHitReject=60,kCsHitEnd=64;

// Why lighting_use refused (the "[water] compose stability" line): the stash cleared, too old, ahead.
enum class LightingRefusal : uint32_t { kCleared = 0, kOld, kAhead };
inline LightingRefusal lighting_refusal(uint64_t stash_frame, uint64_t now) {
  return !stash_frame ? LightingRefusal::kCleared : stash_frame > now ? LightingRefusal::kAhead : LightingRefusal::kOld;
}

// The scene pair (TLAS + set 1) a pass composes with (core/water.cpp compose_at_pass_end). A pass on
// this frame's lighting uses the stash's pair (handed out this frame). A pass on an earlier lighting
// looks at the current scene (rt_scene, which stamps the current slot): the stash's own -> the stash;
// a newer one built this frame -> rt_scene_set (it never waits then); otherwise the stash's pair, the
// one the GPU-current lighting traced, kept alive for this frame (rt_omm_scene_reference stamps that
// slot unless it is retired). Before, that last case was refused: after a scene-wait timeout the
// lighting traced the previous build, its frame's late build then committed with an index below the
// next pass's, and that pass left the water uncomposed for one frame (the in-game flicker of 11-01:
// 51 to 84 "no scene" refusals per 300 frames at S8 with 116 to 133 timeouts per 600).
// take_newer() and keep_stash() are the host calls; take_newer is never called for a build older
// than this frame (rt_scene_set would wait for this frame's scene on the game's recording thread).
enum class ScenePick : uint32_t { kStash = 0, kNewer, kStashKept, kNone };

// Water shaders do not consume OMM proof bindings. Resolve the immutable
// baseline of the selected generation and register this recording's use.
// The query's bool reports acceleration permission, NOT reference validity.
// Missing/retired tuples must never fall back to the accelerated input handle.
template<class Handle,class Query>
bool reference_scene(Handle selected,uint64_t expected,Handle& reference,uint64_t& identity,Query query){
  reference={};identity=expected;
  if(selected!=Handle{})query(selected,&reference,&identity);
  if(reference==Handle{} || !identity || (expected && identity!=expected)){
    reference={};identity=0;return false;
  }
  return true;
}
// Publish a borrowed hit-shading tuple only after both providers validate the
// lighting generation. A failed query must not partially replace ordinary compose.
template<class Handle,class Set,class Reference,class Material>
bool reference_hit_scene(Handle selected,Set alpha,uint64_t expected,Handle& reference,Set& materials,
                         Reference query,Material material){
  reference={};materials={};Handle candidate{};Set candidate_material{};uint64_t identity=0;
  if(!expected || alpha==Set{} || !reference_scene(selected,expected,candidate,identity,query) ||
     !material(candidate,alpha,identity,&candidate_material) || candidate_material==Set{})return false;
  reference=candidate;materials=candidate_material;return true;
}

template <class TakeNewer, class KeepStash>
ScenePick pick_scene(LightingUse use, bool cur_ok, bool cur_is_stash, uint64_t built, uint64_t now, TakeNewer take_newer,
                     KeepStash keep_stash) {
  if (use == LightingUse::kNone) return ScenePick::kNone;
  if (use == LightingUse::kThisFrame || (cur_ok && cur_is_stash)) return ScenePick::kStash;
  if (cur_ok && built >= now && take_newer()) return ScenePick::kNewer;
  return keep_stash() ? ScenePick::kStashKept : ScenePick::kNone;
}

// Only the earlier stash pair needs the outgoing lighting camera. A newer pair
// was built for the camera now; refused scenes never dispatch.
inline bool compose_shift_needed(LightingUse use, ScenePick pick) {
  return use == LightingUse::kEarlier && (pick == ScenePick::kStash || pick == ScenePick::kStashKept);
}

// Composed / uncomposed per frame index of the water passes: a frame with a refused pass and no
// composed one is uncomposed (the game's probe term and unshadowed water on screen); right after a
// composed frame it is an on/off flip. Gate: 0 uncomposed frames in every window with water.
struct ComposeContinuity {
  uint64_t frame = 0;
  bool composed = false, refused = false, prev_composed = false;
  uint64_t frames = 0, composed_frames = 0, uncomposed = 0, flips = 0;
  void close() {
    if (!frame) return;
    ++frames;
    if (composed) {
      ++composed_frames;
    } else if (refused) {
      ++uncomposed;
      if (prev_composed) ++flips;
    }
    prev_composed = composed;
    composed = refused = false;
  }
  void note(uint64_t now, bool ok) {
    if (now != frame) {
      const bool next = now == frame + 1;
      close();
      if (!next) prev_composed = false;   // frames without a water pass in between: no flip
      frame = now;
    }
    (ok ? composed : refused) = true;
  }
  void reset_counts() { frames = composed_frames = uncomposed = flips = 0; }
};

// Statistics words of shaders/rt/water_compose.hlsl (cStats), mirrored there.
inline constexpr uint32_t kCsRays = 7, kCsTier1 = 8;   // tiers 1..6 at kCsTier1 + tier - 1
inline constexpr uint32_t kCsSkin = 14, kCsClamped = 15, kCsComposed = 28;
inline constexpr uint32_t kCsRatioLow = 29, kCsRatioMid = 30, kCsRatioHigh = 31;   // A L / B per composed pixel
inline constexpr float kRatioLow = 0.5f, kRatioHigh = 2.0f;
// M5.4 shadow words (WATER_SHADOWS variant only; the slot is 64 words): sun records (owned, valid,
// D > 0), traced rays, occluded, unknown (kept), no sun term (owned, valid, D = 0), invalid D,
// clamped, then 64-bit sums x 4096 of lum(D) and of the removed lum((1 - V) D) over sun records.
inline constexpr uint32_t kCsSunRec = 32, kCsSunTraced = 33, kCsSunOccluded = 34, kCsSunUnknown = 35;
inline constexpr uint32_t kCsSunNone = 36, kCsSunInvalid = 37, kCsSunClamped = 38;
inline constexpr uint32_t kCsSunSumD = 40, kCsSunSumRemoved = 42;   // 64-bit pairs
inline constexpr uint32_t kCsSunWords = 44;
// M5.4c words (WATER_GSH variants only, after the sun's, inside the 64-word slot of WATER_SHADOWS): records
// with a game sun shadow (sun records with Vg < 1), restored (lit by the traced ray where the game shadowed:
// C + Dg), occluded where the game left it lit (Vg = 1: R, as M5.4), invalid words; then 64-bit sums x 4096
// of lum(Dg) over game-shadowed records, of the restored lum(Dg), and of Vg over game-shadowed records.
inline constexpr uint32_t kCsGshRec = 44, kCsGshRestored = 45, kCsGshOccLit = 46, kCsGshInvalid = 47;
inline constexpr uint32_t kCsGshSumDg = 48, kCsGshSumRestored = 50, kCsGshSumVg = 52;   // 64-bit pairs
inline constexpr uint32_t kCsGshWords = 54;
static_assert(kCsGshWords <= 64, "the M5.4c words fit the WATER_SHADOWS slot and precede the local words (64)");
// Fail-closed composition (every variant): a record whose new colour C' has a non-finite channel keeps the
// game's C (written back unchanged) and is counted here. Inputs that can produce one: L or the colour copy
// (another pass's NaN), a half-packed record word at 65504+, a refraction copy read of an Inf. m5p2 Flymm
// 11-01 ("+828 non-finite") was not one: every RR colour input of the run had 0; the counted pixels were a
// 2048x2048 D32F depth image (dumped 9 times per frame) whose infinite texels vary frame to frame with every
// key off (3328..3527). The word sits after the M5.4c ones (the base and sun variants reduce 57 words).
inline constexpr uint32_t kCsNonFinite = 56, kCsGsWordsMin = kCsNonFinite + 1;
static_assert(kCsGshWords <= kCsNonFinite && kCsNonFinite < 64, "a word no variant uses (the local words start at 64)");
// The decision, mirrored by shaders/rt/water_compose.hlsl (Cn): write C' = max(Cu, 0) only where a term
// changed the pixel and every channel of Cu is finite (max(NaN, 0) is 0 or NaN by implementation).
inline bool compose_writes(bool changed, const float cu[3]) {
  return changed && std::isfinite(cu[0]) && std::isfinite(cu[1]) && std::isfinite(cu[2]);
}
// M5.4b local-light words (WATER_LS variant only; the slot grows to 128 words): local records (owned,
// valid, words written, a local term), no local term, invalid words, then per record the counts of the
// lights the FS evaluates (channel, range, cone, N.L), of those with a game shadow map (kept), traced
// rays, occluded, unknown (kept); records with an occluded light; the sum check's records and bins
// (common: ls_check_bin); clamped; lists cut at kMaxFroxelLights; then 64-bit sums x 4096 of lum(F L),
// of the removed lum, of lum(F sum) and lum(F L) over checked records, of the projector share.
inline constexpr uint32_t kCsLsRec = 64, kCsLsNone = 65, kCsLsInvalid = 66, kCsLsLights = 67, kCsLsShadowed = 68;
inline constexpr uint32_t kCsLsTraced = 69, kCsLsOccluded = 70, kCsLsUnknown = 71, kCsLsOccRec = 72;
inline constexpr uint32_t kCsLsCheck = 73, kCsLsWithin = 74, kCsLsMid = 75, kCsLsFar = 76, kCsLsClamped = 77;
inline constexpr uint32_t kCsLsCut = 78;
inline constexpr uint32_t kCsLsChkSh = 79;          // checked records with a shadow-mapped light (its atlas term recomputed)
inline constexpr uint32_t kCsLsSumL = 80, kCsLsSumRemoved = 82, kCsLsSumChk = 84, kCsLsSumChkL = 86, kCsLsSumBox = 88;
inline constexpr uint32_t kCsLsChkShWithin = 90;    // ... of them within kLsSumTol
inline constexpr uint32_t kCsLsWords = 91;
inline constexpr uint32_t kCsSlotWordsLs = 128;   // core/water.cpp kCStatBytes / 4
// Flags (ComposeParams.a.w): bit 0 the traced reflection (rt_water_compose: M5.3 always sets it),
// bit 1 the traced sun visibility (rt_water_shadows, WATER_SHADOWS variant only), bit 2 the traced
// local-light visibility (rt_water_ls_shadows, WATER_LS variant only). Each bit both traces and
// applies its term; a clear bit leaves its term the FS's.
// Bit 3 the traced refraction (rt_water_refraction, WATER_REFR variant only; cleared per pass without the
// water draw's colour copy).
// Bit 4 the game's sun shadow replaced (rt_water_game_shadow, WATER_GSH variants only: with bit 1, a
// game-shadowed record lit by the traced ray gets C + Dg; clear, the game's Vg stays where the ray is lit).
inline constexpr uint32_t kFlagRf = 1u, kFlagSun = 2u, kFlagLs = 4u, kFlagRefr = 8u, kFlagGsh = 16u, kFlagSceneShift = 32u, kFlagHitShade = 64u;
inline uint32_t ratio_bin(float al, float b) {   // 0 none (B = 0), else the word
  if (!(b > 0.0f)) return 0;
  const float r = al / b;
  return r < kRatioLow ? kCsRatioLow : r > kRatioHigh ? kCsRatioHigh : kCsRatioMid;
}

// ---- The statistics' cost (shaders/rt/water_compose.hlsl cstat_flush, core/water.cpp) ----
// cStats is device-local and every word is reduced per 8x8 group in groupshared before one global
// atomic (m5w3-fg2: per-wave atomics into host-visible memory cost 7.2 ms at 275k records). The
// 64-bit sums (words 16..27, lo/hi) are added per group into a 32-bit groupshared word: exact while
// the group's sum of quantised values (each <= 1e4 * 4096 + 0.5, the shader's clamp) fits 32 bits.
inline constexpr uint32_t kCsGroupLanes = 64;                 // numthreads(8, 8, 1)
inline constexpr uint64_t kCsQuantMax = 10000ull * 4096ull;   // uint(clamp(v, 0, 1e4) * 4096 + 0.5)
inline constexpr uint64_t kCsGroupSumMax = kCsGroupLanes * kCsQuantMax;
static_assert(kCsGroupSumMax < (1ull << 32), "a group's quantised sum must be exact in a 32-bit groupshared word");
// The 64-bit counter add of the flush: lo += s, carry into hi (the shader's InterlockedAdd pair).
inline void add64(uint32_t& lo, uint32_t& hi, uint32_t s) {
  const uint32_t old = lo;
  lo = old + s;
  if (uint32_t(old + s) < old) ++hi;
}

// "[water] compose GPU": the timestamps of one frame slot, pairs (start, end) with availability
// (q[4 i] start, q[4 i + 1] its availability, q[4 i + 2] end, q[4 i + 3]): pair 0 the lighting copy,
// pairs 1..n-1 the compose dispatches. False when any pair is unavailable or reversed.
struct ComposeMs {
  double copy = 0, compose = 0;
};
inline bool compose_ms(const uint64_t* q, uint32_t pairs, double ms_per_tick, ComposeMs& out) {
  out = ComposeMs{};
  for (uint32_t i = 0; i < pairs; ++i) {
    const uint64_t* p = q + i * 4;
    if (!p[1] || !p[3] || p[2] < p[0]) return false;
    (i == 0 ? out.copy : out.compose) += double(p[2] - p[0]) * ms_per_tick;
  }
  return pairs != 0 && out.copy + out.compose < 1000.0;
}

inline constexpr bool compose_plain_counter(uint32_t i) {
  return i==kCsSurfaceClean || i==kCsSurfaceInvalid || i<16 || (i>27 && i<kCsSunSumD) || (i>=kCsGshRec && i<kCsGshSumDg) ||
      (i>=kCsLsRec && i<kCsLsSumL) || i==kCsLsChkShWithin || i==kCsNonFinite ||
      (i==55 || (i>=kCsHitAttempt && i<kCsHitEnd)) || (i>=kCsRrRec && i<kCsRrSumGame);
}

}  // namespace bg3::water
