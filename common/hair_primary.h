// Primary strand hair (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): the frame record the strand fragment
// variants read through a buffer device address baked as a specialization constant. Layer: layer/hair_primary.cpp
// (variants, record) and layer/bvhsplit.cpp (the per-frame header after the groom TLAS build). Shader twin:
// shaders/rt/hair_primary.hlsli (kHp* constants). Header-only, no Vulkan: unit-tested in hair_primary_test.
#pragma once
#include <cstdint>
#include <cstring>

namespace hair_primary {

// Specialization constants of the variant fragment shaders (ids next to the RT spec range, unused elsewhere).
constexpr uint32_t kSpecRecordLo = 1100, kSpecRecordHi = 1101;
// Header (the fixed buffer whose address is baked), little-endian uint32 words:
//  0 frame tag (frame index + 1 of the groom build; a pixel entry with another tag is stale)
//  1 render width, 2 render height, 3 groom instance count (rows of the table below)
//  4..5 groom TLAS device address (lo, hi), 6..7 mesh pool device address, 8..9 pixel array device address
//  10..13 the G-buffer viewport x, y, width, height (float; a flipped viewport has a negative height)
//  14..15 reserved (0)
// Table at kTableOffset: per groom instance k (the groom TLAS instance index) four byte offsets into the mesh pool:
//  points (float3), point UV (float2), segment start indices (uint), previous points (float3; 0: none yet).
constexpr uint32_t kHeaderWords = 16;
constexpr uint32_t kMaxGrooms = 64;   // bvhsplit kGroomMax
constexpr uint32_t kTableOffset = kHeaderWords * 4;
constexpr uint32_t kRowBytes = 16;
constexpr uint32_t kHeaderBytes = kTableOffset + kMaxGrooms * kRowBytes;   // 1088, one vkCmdUpdateBuffer
// Pixel entry (16 B): key = groom k << 24 | segment (0xFFFFFFFF: no strand on this ray), parameter v along the segment
// (float), native depth (float), frame tag.
constexpr uint32_t kPixelBytes = 16;
constexpr uint32_t kNoStrand = 0xFFFFFFFFu;
constexpr uint32_t kMaxSegment = (1u << 24) - 1u;

// The header's device address for the core (persist(kPersistKey, sizeof(Persist)); 0 until the layer made it):
// gi/hair_fibre.hlsl (rt_hair_fibre) reads the same record. No ABI change.
constexpr const char* kPersistKey = "bg3rtx.hair_primary";
struct Persist { uint64_t header; uint32_t version, pad; };

struct Row { uint32_t points, uv, starts, prev; };
struct Header {
  uint32_t tag, width, height, count;
  uint64_t tlas, pool, pixels;
  float viewport[4];
  uint32_t reserved[2];
};
static_assert(sizeof(Header) == kHeaderWords * 4, "header words");

// The bytes one vkCmdUpdateBuffer writes: header then `count` rows (the rest of the table is not read).
inline uint32_t pack(const Header& h, const Row* rows, uint32_t count, uint8_t* out /* kHeaderBytes */) {
  if (count > kMaxGrooms) count = kMaxGrooms;
  Header c = h;
  c.count = count;
  std::memcpy(out, &c, sizeof c);
  std::memcpy(out + kTableOffset, rows, count * sizeof(Row));
  return (kTableOffset + count * kRowBytes + 3u) & ~3u;
}

inline uint32_t key(uint32_t groom, uint32_t segment) {
  return groom < 255u && segment <= kMaxSegment ? (groom << 24) | segment : kNoStrand;
}
inline uint64_t pixel_bytes(uint32_t width, uint32_t height) { return uint64_t(width) * height * kPixelBytes; }

// Fragment shader hashes of the static CHAR_Hair material (docs/NATIVE_HAIR.md): the G-buffer writer gets the strand
// variant, the camera Depth prepass a no-depth-write clone (the strands write their own depth in the G-buffer pass,
// after this frame's groom TLAS exists). Instanced hair (0ca5ee93..., other bindings) keeps its cards.
constexpr uint64_t kDeferredStaticFs = 0x42a95e17ec3adc51ull;
constexpr uint64_t kDepthStaticFs = 0x5a03e5b678b2d040ull;
// The velocity pass's static hair FS (motion vectors): the strand variant hair_primary_velocity.hlsl.
constexpr uint64_t kVelocityStaticFs = 0xb70cadc5dd3adcbdull;

}  // namespace hair_primary
