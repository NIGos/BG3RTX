// Primary strand hair (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): the frame record the strand fragment
// variants read through a buffer device address baked as a specialization constant. Layer: layer/hair_primary.cpp
// (variants, record) and layer/bvhsplit.cpp (the per-frame header after the groom TLAS build). Shader twin:
// shaders/rt/hair_primary.hlsli (kHp* constants). Header-only, no Vulkan: unit-tested in hair_primary_test.
#pragma once
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace hair_primary {

// Specialization constants of the variant fragment shaders (ids next to the RT spec range, unused elsewhere).
constexpr uint32_t kSpecRecordLo = 1100, kSpecRecordHi = 1101;
// Header (the fixed buffer whose address is baked), little-endian uint32 words:
//  0 frame tag (frame index + 1 of the groom build; a pixel entry with another tag is stale)
//  1 render width, 2 render height, 3 groom instance count (rows of the table below)
//  4..5 groom TLAS device address (lo, hi), 6..7 mesh pool device address, 8..9 pixel array device address
//  10..13 the G-buffer viewport x, y, width, height (float; a flipped viewport has a negative height)
//  14 mesh pool size in bytes (the strand readers bound every pool read by it, hp_segment), 15 reserved (0)
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
  uint32_t pool_bytes, reserved;
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

// The groom point update reads the matched draw's card triangles from this frame's XFB soup:
// soup_off + (3 tri + k) * stride + 24 for tri < the groom's triangles. Only a draw captured into the soup this
// frame has a soup offset: a held draw's offset is a mesh-pool offset (its snapshot) and reading the soup there ran
// past the live capture (device lost 2026-11-03 17:01, read at +1.97 GB of a 2 GB soup). Fail closed: no update.
inline bool soup_update_ok(bool captured, bool held, uint64_t soup_off, uint32_t prims, uint32_t stride,
                           uint64_t soup_bytes, uint32_t groom_triangles) {
  if (!captured || held || stride < 24 || prims == 0 || groom_triangles == 0 || groom_triangles > prims) return false;
  const uint64_t need = uint64_t(prims) * 3u * stride;
  return soup_off <= soup_bytes && need <= soup_bytes - soup_off;
}

// Card hiding is fail safe: a draw's prepass card depth is dropped only when that draw's G-buffer strand variant ran
// in the last completed frame. A matched draw whose G-buffer pipeline has no variant (another material permutation,
// refused clone) keeps its cards in every pass (2026-11-03: the player's hair vanished: prepass hidden, the G-buffer
// card then failed its EQUAL depth test).
// GPU feedback (shaders/rt/hair_primary_count.hlsl): the strand pixels of each groom in a frame whose G-buffer variant
// ran for it. kStrikes such frames in a row with no strand pixel at all mark the draw failed for the session: its cards
// stay (the strands are not where the cards are: wrong groom, wrong frame, or a ray bug), logged once.
constexpr uint32_t kStrikes = 2;
struct SwapGate {
  std::unordered_set<uint64_t> published, published_prev, gbuffer_now, gbuffer_prev, failed;
  std::unordered_map<uint64_t, uint32_t> strikes;
  // A build published its groom set (once per frame, after the prepass).
  void publish(const uint64_t* keys, uint32_t n) {
    published_prev.swap(published);
    published.clear();
    for (uint32_t i = 0; i < n; ++i)
      if (keys[i]) published.insert(keys[i]);
    gbuffer_prev.swap(gbuffer_now);
    gbuffer_now.clear();
  }
  // A device or scene reset: the published sets go, the failed draws stay failed.
  void clear() { published.clear(); published_prev.clear(); gbuffer_now.clear(); gbuffer_prev.clear(); strikes.clear(); }
  bool groom_stable(uint64_t key) const { return published.count(key) && published_prev.count(key) && !failed.count(key); }
  bool allow_gbuffer(uint64_t key) const { return groom_stable(key); }
  bool allow_prepass(uint64_t key) const { return groom_stable(key) && gbuffer_prev.count(key); }
  bool allow_velocity(uint64_t key) const { return groom_stable(key) && gbuffer_prev.count(key); }
  void gbuffer_drew(uint64_t key) { gbuffer_now.insert(key); }
  // One counted frame of `key`: drew = its G-buffer variant ran that frame. True when it just failed.
  bool feedback(uint64_t key, bool drew, uint32_t pixels) {
    if (!key || !drew || failed.count(key)) return false;
    if (pixels) {
      strikes.erase(key);
      return false;
    }
    if (++strikes[key] < kStrikes) return false;
    failed.insert(key);
    return true;
  }
};

// Level loads (and resolution changes) recreate the game's G-buffer images. At such a boundary every published groom,
// G-buffer proof and feedback is dropped and no draw is swapped for kLoadQuietFrames frames: the scene's draws,
// buffers and descriptors are all new (hv1 2026-11-03 18:40: device lost 3 s after the selected-save reload, with
// the strand variants active). After it the usual rule holds again (two publishes, then the G-buffer, then the rest).
constexpr uint64_t kLoadQuietFrames = 300;
struct LoadGuard {
  uint64_t image = 0, quiet_until = 0;
  // The G-buffer depth image of this frame; true at a boundary (a different image after a first one).
  bool observe(uint64_t depth_image, uint64_t now) {
    if (!depth_image || depth_image == image) return false;
    const bool boundary = image != 0;
    image = depth_image;
    if (boundary) quiet_until = now + kLoadQuietFrames;
    return boundary;
  }
  bool quiet(uint64_t now) const { return now < quiet_until; }
};

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
