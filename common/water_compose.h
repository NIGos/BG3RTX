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

}  // namespace bg3::water
