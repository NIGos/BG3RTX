#pragma once
// M5.3 water composition (docs/PLAN_M5_WATER.md M5.3, rt_water_compose, default 0): what the
// layer (layer/water.cpp) and the core (core/water.cpp, shaders/rt/water_compose.hlsl) share.
//
// The capture ("the minimal extra copy"): the factors of the probe term are not reconstructable
// after the forward water pass (the FS's normal needs its interpolated UV and tangent frame,
// its opacity and fog need the flow-mapped textures at that UV), so the audited water FS writes
// them itself. common/water_fs_patch.h adds, at the end of its main, eight 32-bit stores into
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

namespace bg3::water {

inline constexpr uint32_t kHeaderBytes = 64;
inline constexpr uint32_t kRecordBytes = 32;
inline constexpr uint32_t kRecordWords = kRecordBytes / 4;

inline constexpr const char* kComposeKey = "bg3rtx.water_compose";
inline constexpr uint32_t kComposeVersion = 1;

// Published once by the layer when it created the buffer (before any variant exists); the
// buffer lives until the device is destroyed. version is written last (release).
struct ComposeBuffer {
  uint32_t version;     // kComposeVersion once valid
  uint32_t capacity;    // records (without the sink)
  uint64_t buffer;      // VkBuffer (STORAGE_BUFFER | SHADER_DEVICE_ADDRESS | TRANSFER_DST)
  uint64_t bytes;       // its size
  uint32_t pad[10];
};
static_assert(sizeof(ComposeBuffer) == 64, "ComposeBuffer layout is shared by the layer and the core");

inline uint64_t buffer_bytes(uint32_t capacity) {
  return uint64_t(kHeaderBytes) + (uint64_t(capacity) + 1) * kRecordBytes;
}
// Records that fit in `mb` MiB (header and sink included), at least 1.
inline uint32_t capacity_of_mb(int mb) {
  const uint64_t bytes = uint64_t(mb < 1 ? 1 : mb > 2048 ? 2048 : mb) << 20;
  const uint64_t n = (bytes - kHeaderBytes) / kRecordBytes;
  return n > 1 ? uint32_t(n - 1) : 1u;
}

inline void publish_buffer(ComposeBuffer* dst, uint64_t buffer, uint64_t bytes, uint32_t capacity) {
  dst->buffer = buffer;
  dst->bytes = bytes;
  dst->capacity = capacity;
  std::atomic_ref<uint32_t>(dst->version).store(kComposeVersion, std::memory_order_release);
}
inline bool read_buffer(const ComposeBuffer* src, ComposeBuffer& out) {
  if (!src) return false;
  const uint32_t v = std::atomic_ref<uint32_t>(const_cast<ComposeBuffer*>(src)->version).load(std::memory_order_acquire);
  if (v != kComposeVersion) return false;
  out = *src;
  return out.buffer != 0 && out.capacity != 0 && out.bytes >= buffer_bytes(out.capacity);
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
struct FsTerms {
  float refr, direct, kd, sp, e, k, x, ge, t, s, a;
};
inline float fs_colour(const FsTerms& f) {
  const float lw = f.direct + (f.kd + f.sp * f.e) * f.k;
  return f.refr + ((lw * f.x + f.ge) * f.t + f.s - f.refr) * f.a;
}
inline float record_a(const FsTerms& f) { return f.x * (f.a * f.t) * f.e; }
inline float record_b(const FsTerms& f) { return f.x * (f.a * f.t * f.k) * (f.sp * f.e); }
inline float compose(float c, float a, float l, float b) {
  const float v = c + a * l - b;
  return v > 0.0f ? v : 0.0f;
}

// ---- Which lighting a forward water pass composes with (core/water.cpp compose_at_pass_end) ----
// run_post (and so the lighting's copy) is recorded when the lighting's command buffer ENDS
// (core/gi.cpp flush), and the game records the forward water pass in another command buffer,
// most often before that: in m5w3 (S8, 11-01) 117 of 120 passes found only the previous frame's
// lighting stash. The GPU still runs the frame's lighting before its forward pass (the water draws
// over the lit opaque), so everything the compose reads from the lighting is GPU-current: one
// radiance / depth image and ONE camera block that the copy overwrites each frame, the scene TLAS
// + set taken at the pass (hand_out stamps it for the frame that records it). The stash only gives
// the push constants and the statistics slot, so the latest lighting may be this frame's or the
// previous one's. Frame indices advance at each present: with frame generation 2 presents per app
// frame, plus one present that may land between the lighting's and the pass's recording, the
// previous app frame's lighting is at most 3 indices old. A lighting that did not copy (water_rf or
// compose_at_lighting skipped) clears the stash (frame 0): the GPU copy would be a frame stale.
inline constexpr uint64_t kLightingLagFrames = 3;
enum class LightingUse : uint32_t { kNone = 0, kThisFrame, kEarlier };
inline LightingUse lighting_use(uint64_t stash_frame, uint64_t now) {
  if (!stash_frame || stash_frame > now) return LightingUse::kNone;
  if (stash_frame == now) return LightingUse::kThisFrame;
  return stash_frame + kLightingLagFrames >= now ? LightingUse::kEarlier : LightingUse::kNone;
}

// Statistics words of shaders/rt/water_compose.hlsl (cStats), mirrored there.
inline constexpr uint32_t kCsRays = 7, kCsTier1 = 8;   // tiers 1..6 at kCsTier1 + tier - 1
inline constexpr uint32_t kCsSkin = 14, kCsClamped = 15, kCsComposed = 28;
inline constexpr uint32_t kCsRatioLow = 29, kCsRatioMid = 30, kCsRatioHigh = 31;   // A L / B per composed pixel
inline constexpr float kRatioLow = 0.5f, kRatioHigh = 2.0f;
inline uint32_t ratio_bin(float al, float b) {   // 0 none (B = 0), else the word
  if (!(b > 0.0f)) return 0;
  const float r = al / b;
  return r < kRatioLow ? kCsRatioLow : r > kRatioHigh ? kCsRatioHigh : kCsRatioMid;
}

}  // namespace bg3::water
