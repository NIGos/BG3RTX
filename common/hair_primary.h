// Primary strand hair (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): the frame record the strand fragment
// variants read through a buffer device address baked as a specialization constant. Layer: layer/hair_primary.cpp
// (variants, record) and layer/bvhsplit.cpp (the per-frame header after the groom TLAS build). Shader twin:
// shaders/rt/hair_primary.hlsli (kHp* constants). Header-only, no Vulkan: unit-tested in hair_primary_test.
#pragma once
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hair_primary {

// A frame fence alone does not prove that a particular recording executed.
enum class ReadbackState { Wait, Drop, Ready };
inline ReadbackState readback_state(uint64_t frame, uint64_t done, bool submitted) {
  if (done <= frame || done - frame <= 1) return ReadbackState::Wait;
  return submitted ? ReadbackState::Ready : ReadbackState::Drop;
}
struct PeriodFailures {
  uint64_t refused = 0, feedback = 0;
  void reset() { refused = feedback = 0; }
};
inline void count_marker_totals(const uint32_t* cards, const uint32_t* accepted,
                               uint32_t* card_totals, uint32_t* accepted_totals) {
  for (uint32_t j = 0; j < 4; ++j) {
    card_totals[j] = accepted_totals[j] = 0;
    for (uint32_t k = 0; k < 64; ++k) {
      card_totals[j] += cards[j * 64 + k];
      accepted_totals[j] += accepted[j * 64 + k];
    }
  }
}

// Specialization constants of the variant fragment shaders (ids next to the RT spec range, unused elsewhere).
constexpr uint32_t kSpecRecordLo = 1100, kSpecRecordHi = 1101, kSpecDrawSlot = 1102;
// Header (the fixed buffer whose address is baked), little-endian uint32 words:
//  0 frame tag (frame index + 1 of the groom build; a pixel entry with another tag is stale)
//  1 render width, 2 render height, 3 groom instance count (rows of the table below)
//  4..5 groom TLAS device address (lo, hi), 6..7 mesh pool device address, 8..9 pixel array device address
//  10..13 the G-buffer viewport x, y, width, height (float; a flipped viewport has a negative height)
//  14 mesh pool size in bytes (the strand readers bound every pool read by it, hp_segment), 15 flags (bit 0: dithered
//  strand tips, bit 1: the build camera at kCameraOffset is valid, bit 2: the previous publish's at kPrevCameraOffset,
//  bit 3: M3.2 two-ray coverage, read only by a G-buffer variant built with specialization kSpecIdHairGroomCoverage = 1)
// Table at kTableOffset: per groom instance k (the groom TLAS instance index) four byte offsets into the mesh pool:
//  points (float3), point UV (float2), segment start indices (uint), previous points (float3; 0: none yet).
constexpr uint32_t kFlagTips = 1u << 0, kFlagCamera = 1u << 1, kFlagPrevCamera = 1u << 2, kFlagCoverage = 1u << 3;
constexpr uint32_t kHeaderWords = 16;
constexpr uint32_t kDrawSlots = 16;
constexpr uint32_t kMaxGrooms = 64;   // bvhsplit kGroomMax
constexpr uint32_t kTableOffset = kHeaderWords * 4;
constexpr uint32_t kRowBytes = 16;
constexpr uint32_t kHeaderBytes = kTableOffset + kMaxGrooms * kRowBytes;   // 1088, one vkCmdUpdateBuffer
// Counts buffer of the fail-safe (rt/hair_primary_count.hlsl): strand pixels per groom k, then per groom the strand
// pixels whose segment fails the readers' pool bound (hp_segment discards them; counted so the bound is never silent).
// Then per groom the strand pixels sealed with an adjacent frame's tag (tag - 1 or tag + 1): the G-buffer of the counted
// frame ran against another publish (the game submitted the prepass, which records the build, and the G-buffer in
// another order), so the counted tag says nothing about that groom's strands.
constexpr uint32_t kRefusedOffset = kMaxGrooms * 4, kOtherOffset = kRefusedOffset + kMaxGrooms * 4;
// Then (C2) per groom float4 (a_f rgb, 1 when measured) from its strand colour sample.
constexpr uint32_t kTransmitOffset = kOtherOffset + kMaxGrooms * 4;   // 768
// Then (diagnostics, 2026-11-04 strand traces found no strand) the counted frame's traced pixel entries (any sealed
// entry) and those with a strand, and one sampled ray of the strand G-buffer variant (kRayDebugOffset, 64 bytes).
constexpr uint32_t kTracedOffset = kTransmitOffset + kMaxGrooms * 16;   // 1792
constexpr uint32_t kRaySampleOffset = kTracedOffset + 16;
// Then accepted strand pixels per compact draw-key slot for the four newest applied tags (tag, tag - 1, tag - 2, tag - 3): the count is recorded
// after this frame's G-buffer pass but may run on the GPU before the last G-buffer passes (hv7 2026-11-04: the record
// held tag - 2 when the count looked for tag; every count read 0). The layer takes the newest tag with entries.
constexpr uint32_t kCountTags = 4;
constexpr uint32_t kTagCountOffset = kRaySampleOffset + 64;   // 1872
constexpr uint32_t kCardCountOffset = kTagCountOffset + kCountTags * kMaxGrooms * 4;
constexpr uint32_t kCountBytes = kCardCountOffset + kCountTags * kMaxGrooms * 4;
// The newest of the kCountTags tags (j = 0 newest) whose entries were found; -1 none.
inline int newest_counted_tag(const uint32_t totals[kCountTags]) {
  for (int j = 0; j < int(kCountTags); ++j)
    if (totals[j]) return j;
  return -1;
}
// Strand colour samples (C2 shadow transmittance), after the header in the same buffer, never written by the layer's
// update: per groom k 32 bytes, float4 (albedo rgb, fibre roughness) then the frame tag that wrote it. The strand
// G-buffer variant stores one fragment's values per 8 x 8 pixel block (last writer wins: a sample of the groom's
// colour); rt/hair_primary_count.hlsl turns the entries of the counted frame into a_f at the next publish.
constexpr uint32_t kColourOffset = kHeaderBytes, kColourBytes = 32;
constexpr uint32_t kHeaderAlloc = kColourOffset + kMaxGrooms * kColourBytes;   // 3136
// The publish's build camera (world position, float3 + pad: the origin of its camera-relative groom points), written
// with the header. The scene build, so the groom TLAS, is recorded after the G-buffer pass: the strand passes of frame
// N read the publish of frame N - 1 and start their rays at camera_N - camera_build in its frame (hp_rebase).
// Then (flag bit 2) the previous publish's build camera: the origin of the rows' previous points (velocity: the strand's
// pose of the last publish in the previous frame's camera space).
constexpr uint32_t kCameraOffset = kHeaderAlloc, kPrevCameraOffset = kCameraOffset + 16, kHeaderTotal = kCameraOffset + 32;   // 3168
static_assert(kCameraOffset == 3136 && kPrevCameraOffset == 3152, "shaders/rt/hair_primary_rebase.hlsli offsets");
// After the cameras: one ray of the strand G-buffer variant (fragments on a sparse 64-pixel grid, last writer wins):
// float4 (origin, card distance), float4 (direction, committed), float4 (hit distance, groom, segment, tag),
// float4 (this camera's position, 0). Copied into the counts by rt/hair_primary_count.hlsl and logged.
constexpr uint32_t kRayDebugOffset = kHeaderTotal, kRayDebugBytes = 64;
static_assert(kRayDebugOffset == 3168, "shaders/rt/hair_primary_rebase.hlsli hair_primary_ray_debug_offset");
// Key map at kKeyMapOffset: groom -> compact draw-key slot (kDrawSlots means no slot).
// Breadcrumbs (diagnostics, hv6/hv7 2026-11-04 device lost once strand rays hit): the device address of a small
// host-visible buffer of the layer. The strand G-buffer variant checks each explicit buffer access against its range
// and records a violation there (site, address; the access is skipped), and notes the last TLAS / pool / pixel
// addresses it used. Read every 300 frames and after a device loss (kCrumbSites sites x 16 bytes, then the notes).
constexpr uint32_t kCrumbAddrOffset = kRayDebugOffset + kRayDebugBytes, kKeyMapOffset = kCrumbAddrOffset + 16, kHeaderAllocAll = kKeyMapOffset + kMaxGrooms * 4;   // 3232, 3248, 3504
static_assert(kKeyMapOffset == 3248, "hair_primary_gbuffer.hlsl hair_primary_key_map_offset");
static_assert(kCardCountOffset == 2896, "hair_primary_count.hlsl hair_primary_card_count_offset");
constexpr uint32_t kCrumbSites = 16, kCrumbBytes = kCrumbSites * 16 * 2;
static_assert(kCrumbAddrOffset == 3232, "shaders/rt/hair_primary_rebase.hlsli hair_primary_crumb_offset");
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
  uint32_t pool_bytes, flags;
};
static_assert(sizeof(Header) == kHeaderWords * 4, "header words");

// Shadow transmittance of one strand crossing (docs/RTX_HAIR_PRIMARY.md C2), the groom InstInfo row's word 2: the
// fibre's forward-scattered fraction a_f per channel in 10 bits each, bit 31 set when measured. 0: not measured (or
// the key off): the strand blocks, as before. The visibility ray passes n crossings with d_f * prod(a_f) in luminance
// (rt_common.hlsli rt_groom_blocks), the dual-scattering transmittance T_f = d_f a_f^n of the fibre pass.
inline uint32_t pack_transmit(const float a[3]) {
  uint32_t w = 1u << 31;
  for (int c = 0; c < 3; ++c) {
    const float v = a[c] != a[c] ? 0.0f : a[c] < 0.0f ? 0.0f : a[c] > 1.0f ? 1.0f : a[c];
    w |= uint32_t(v * 1023.0f + 0.5f) << (10 * c);
  }
  return w;
}
// The groom's a_f over the measured frames: an exponential mean (each sample is one fragment's colour), weight 1/16.
struct TransmitMean {
  float a[3] = {0, 0, 0};
  uint32_t n = 0;
  void add(const float s[3]) {
    const float w = n == 0 ? 1.0f : 1.0f / 16.0f;
    for (int c = 0; c < 3; ++c) a[c] += (s[c] - a[c]) * w;
    ++n;
  }
  uint32_t packed() const { return n ? pack_transmit(a) : 0u; }
};

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
// Historical pixel-strike helper (retained for regression tests; the layer no longer calls feedback):
// GPU feedback (shaders/rt/hair_primary_count.hlsl): the strand pixels of each groom in a frame whose G-buffer variant
// ran for it. kStrikes such frames in a row with no strand pixel at all retire the draw: its cards come back (the
// strands are not where the cards are: wrong groom, wrong frame, or a ray bug), logged. Not for the session: a draw off
// screen or behind another groom also has no strand pixel, so it is retried after kRetryFrames, doubled at each new
// failure up to kRetryMaxFrames (hv5 2026-11-03: every draw of the counted frame struck at once, for good).
// A counted frame whose strand pixels carry an adjacent frame's tag is no evidence (the G-buffer ran against another
// publish), and the draws sharing one index-range key (two characters with the same hairstyle) are judged together.
constexpr uint32_t kStrikes = 2;
constexpr uint64_t kRetryFrames = 600, kRetryMaxFrames = 9600;
struct SwapGate {
  struct Retired { uint64_t until = 0, wait = 0; };
  std::unordered_set<uint64_t> published, published_prev, gbuffer_now, gbuffer_prev;
  std::unordered_map<uint64_t, Retired> failed;
  std::unordered_map<uint64_t, uint32_t> strikes;
  uint64_t now = 0;   // the frame of the last publish (retirements count from it)
  // A build published its groom set (once per frame, after the prepass).
  void publish(const uint64_t* keys, uint32_t n, uint64_t frame = 0) {
    now = frame ? frame : now + 1;
    published_prev.swap(published);
    published.clear();
    for (uint32_t i = 0; i < n; ++i)
      if (keys[i]) published.insert(keys[i]);
    gbuffer_prev.swap(gbuffer_now);
    gbuffer_now.clear();
  }
  // A device or scene reset: the published sets go, retired draws stay retired until their retry frame.
  void clear() { published.clear(); published_prev.clear(); gbuffer_now.clear(); gbuffer_prev.clear(); strikes.clear(); }
  bool retired(uint64_t key) const {
    const auto it = failed.find(key);
    return it != failed.end() && now < it->second.until;
  }
  bool groom_stable(uint64_t key) const { return published.count(key) && published_prev.count(key) && !retired(key); }
  bool allow_gbuffer(uint64_t key) const { return groom_stable(key); }
  bool allow_prepass(uint64_t key) const { return groom_stable(key) && gbuffer_prev.count(key); }
  bool allow_velocity(uint64_t key) const { return groom_stable(key) && gbuffer_prev.count(key); }
  void gbuffer_drew(uint64_t key) { gbuffer_now.insert(key); }
  // One counted frame of `key`: drew = its G-buffer variant ran that frame; pixels with the counted tag, other with
  // an adjacent frame's. True when it just got retired.
  bool feedback(uint64_t key, bool drew, uint32_t pixels, uint32_t other = 0) {
    if (!key || !drew || retired(key)) return false;
    if (pixels) {
      strikes.erase(key);
      return false;
    }
    if (other) return false;   // the frame is misaligned: no evidence either way
    if (++strikes[key] < kStrikes) return false;
    strikes.erase(key);
    Retired& r = failed[key];
    r.wait = r.wait ? (r.wait * 2 < kRetryMaxFrames ? r.wait * 2 : kRetryMaxFrames) : kRetryFrames;
    r.until = now + r.wait;
    return true;
  }
  // A whole counted frame: per groom k its key, whether its draw ran the variant, its pixels with the counted tag and
  // with an adjacent tag. Grooms sharing a key are summed first. Returns the keys retired by this frame.
  std::vector<uint64_t> feedback_frame(const uint64_t* keys, const bool* drew, const uint32_t* pixels, const uint32_t* other,
                                       uint32_t n) {
    std::vector<uint64_t> order, out;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> sum;   // key -> (pixels, other)
    std::unordered_set<uint64_t> ran;
    for (uint32_t k = 0; k < n; ++k) {
      if (!keys[k]) continue;
      if (!sum.count(keys[k])) order.push_back(keys[k]);
      auto& s = sum[keys[k]];
      s.first += pixels[k];
      s.second += other[k];
      if (drew[k]) ran.insert(keys[k]);
    }
    for (uint64_t key : order) {
      const auto& s = sum[key];
      if (feedback(key, ran.count(key) != 0, uint32_t(s.first > 0xFFFFFFFFull ? 0xFFFFFFFFull : s.first),
                   uint32_t(s.second > 0xFFFFFFFFull ? 0xFFFFFFFFull : s.second)))
        out.push_back(key);
    }
    return out;
  }
};

// Coverage evidence is independent of publication and pixel-record strikes. Keep the existing conservative
// latency budget: up to three older tags, three publishes of readback, plus scheduling slack.
constexpr uint64_t kCoverageLatency = 4, kStrandLatency = 3 + 3, kFreshFrames = kCoverageLatency + kStrandLatency + 2;
// Batch-4, 877 passing styles / 2631 views, regenerated from unchanged cached geometry:
// minimum S/C = 8556.228538087944 / 9396.798707214493 = 0.9105471772550376
// (HAIR_HUM_F_Minthara_Mesh_LOD1, side). One-frame sample-count noise, first-order
// ratio propagation: sigma <= (sqrt(S) + (S/C)*sqrt(C))/C = 0.01923694227747121.
// This allows unknown covariance between counts, with per-count variance <= mean
// under independent spatial Bernoulli sampling. It is modeled, not measured dither
// noise. kCoverMin = min ratio - sigma; no fitted tolerance. See docs/RTX_HAIR_PRIMARY.md
// 5b and docs/progress/claude-hair-coverage-bound.json (input state SHA-256, full provenance).
constexpr double kCoverMin = 0.8913102349775665;
// Upper side (2026-11-04: the strands may not cover more than the cards either: hv7 measured 2.55 with overdrawn
// occlusion samples): the maximum offline ratio 1.0752640637962045 (2631 views) plus the same modelled noise. The live
// ratio is unique accepted strand pixels over unique card pixels in the same G-buffer pass.
constexpr double kCoverMax = 1.0752640637962045 + 0.01923694227747121;
// A tag applied by two G-buffer frames cannot identify a measured frame.
struct GbFrame { uint64_t frame = 0; uint32_t tag = 0; uint64_t keys[kMaxGrooms] = {}, draw_keys[kDrawSlots] = {}; };
inline const GbFrame* unique_frame(const GbFrame* frames, uint32_t n, uint32_t tag, bool& ambiguous) {
  const GbFrame* found = nullptr;
  ambiguous = false;
  for (uint32_t i = 0; i < n; ++i) if (frames[i].frame && frames[i].tag == tag) {
    if (found) { ambiguous = true; return nullptr; }
    found = frames + i;
  }
  return found;
}
// A prepass at p can feed G-buffer recording at p through p + 2. Membership
// survives every sibling draw; absent draws and expired latches are not orphans.
struct DepthLatch {
  std::unordered_map<uint64_t, uint64_t> keys, uncovered;
  void prune(uint64_t frame) {
    for (auto it = keys.begin(); it != keys.end();)
      if (frame > it->second && frame - it->second > 2) it = keys.erase(it); else ++it;
    for (auto it = uncovered.begin(); it != uncovered.end();)
      if (frame > it->second && frame - it->second > 300) it = uncovered.erase(it); else ++it;
  }
  void observe_pipeline(uint64_t key, uint64_t frame, bool variant, bool fallback) {
    if (key && !variant && !fallback) uncovered[key] = frame;
  }
  bool allow_prepass(uint64_t key, uint64_t frame) const {
    const auto it = uncovered.find(key);
    return it == uncovered.end() || (frame > it->second && frame - it->second > 300);
  }
  void prepass(uint64_t key, uint64_t frame) { keys[key] = frame; }
  bool dropped(uint64_t key, uint64_t frame) const {
    const auto it = keys.find(key);
    return it != keys.end() && frame >= it->second && frame - it->second <= 2;
  }
  bool needs_card_depth(uint64_t key, uint64_t frame, bool strands) const {
    return !strands && dropped(key, frame);
  }
  bool orphan(uint64_t key, uint64_t frame, bool strands, bool fallback) const {
    return !fallback && needs_card_depth(key, frame, strands);
  }
};
// G-buffer apply tags are frame_index + 1. Either recording order is valid.
inline bool prepass_drop_allowed(uint64_t applied, uint64_t now, bool live,
                                 bool depth_safe, bool sibling_safe) {
  return applied >= now && applied - now <= 1 && live && depth_safe && sibling_safe;
}
struct CoverageGate {
  struct Evidence {
    uint64_t frame = 0, card = 0, strand = 0;
    double ratio() const { return card ? double(strand) / double(card) : 0.0; }
  };
  std::unordered_map<uint64_t, Evidence> latest;
  std::unordered_map<uint64_t, uint64_t> incomplete;
  void invalidate(uint64_t key, uint64_t frame) {
    if (key && frame > incomplete[key]) incomplete[key] = frame;
  }
  void observe(uint64_t key, uint64_t frame, uint64_t card, uint64_t strand) {
    if (!key) return;
    auto& e = latest[key];
    if (frame >= e.frame) e = {frame, card, strand};
  }
  bool allow(uint64_t key, uint64_t now, double bound = kCoverMin, double upper = kCoverMax) const {
    const auto it = latest.find(key);
    if (it == latest.end()) return false;
    const auto& e = it->second;
    const auto bad = incomplete.find(key);
    if (bad != incomplete.end() && e.frame <= bad->second) return false;
    return e.frame && now >= e.frame && now - e.frame <= kFreshFrames && e.card && e.ratio() >= bound && e.ratio() <= upper;
  }
  void clear() { latest.clear(); incomplete.clear(); }
};

// rt_hair_groom_primary_order: the header is written in the G-buffer's own command buffer, right before its render
// pass, instead of in the prepass's (where the groom build is recorded). The game may submit the two in either order;
// the G-buffer then read an adjacent frame's header (hv5 2026-11-03: every groom struck at once). The header applied
// is the newest staged publish whose build is ordered before the G-buffer on the GPU: recorded earlier in the same
// command buffer, or in a command buffer already submitted (one queue). None: no strand swap this frame (fail closed).
struct Staged { uint32_t tag = 0; uint64_t rec = 0; uint64_t cmd = 0; };
template <class Submitted>
inline int pick_publish(const Staged* staged, int n, uint64_t gbuffer_cmd, Submitted submitted) {
  int best = -1;
  for (int i = 0; i < n; ++i) {
    const Staged& s = staged[i];
    if (!s.tag || !(s.cmd == gbuffer_cmd || submitted(s.rec))) continue;
    if (best < 0 || s.tag > staged[best].tag) best = i;
  }
  return best;
}

// Level loads (and resolution changes) recreate the game's G-buffer images. At such a boundary every published groom,
// G-buffer proof and feedback is dropped and no draw is swapped for kLoadQuietFrames frames: the scene's draws,
// buffers and descriptors are all new (hv1 2026-11-03 18:40: device lost 3 s after the selected-save reload, with
// the strand variants active). After it the usual rule holds again (two publishes, then the G-buffer, then the rest).
// The first image seen is a boundary too: with rt_hair_groom_primary latched at startup the menu publishes no groom, so
// the first publish comes in the middle of the first level load (hc1 2026-11-04 00:25: device lost 10 frames after the
// first publish, the level still streaming; strands enabled live after a load had survived).
constexpr uint64_t kLoadQuietFrames = 300;
struct LoadGuard {
  uint64_t image = 0, quiet_until = 0;
  // The G-buffer depth image of this frame; true at a boundary (the first image, or a different one).
  bool observe(uint64_t depth_image, uint64_t now) {
    if (!depth_image || depth_image == image) return false;
    image = depth_image;
    quiet_until = now + kLoadQuietFrames;
    return true;
  }
  bool quiet(uint64_t now) const { return now < quiet_until; }
};

inline uint32_t key(uint32_t groom, uint32_t segment) {
  return groom < 255u && segment <= kMaxSegment ? (groom << 24) | segment : kNoStrand;
}
// Retain slots for live keys before assigning newly seen keys to free slots.
struct DrawSlots {
  uint64_t keys[kDrawSlots] = {};
  uint32_t find(uint64_t key) const {
    for (uint32_t i = 0; i < kDrawSlots; ++i) if (key && keys[i] == key) return i;
    return kDrawSlots;
  }
  void publish(const uint64_t* live, uint32_t count) {
    for (auto& key : keys) {
      bool present = false;
      for (uint32_t k = 0; k < count; ++k) present |= key && key == live[k];
      if (!present) key = 0;
    }
    for (uint32_t k = 0; k < count; ++k) if (live[k] && find(live[k]) == kDrawSlots)
      for (auto& key : keys) if (!key) { key = live[k]; break; }
  }
};
inline uint64_t coverage_bytes(uint32_t width, uint32_t height) {
  return uint64_t(width) * height * (kPixelBytes + 8); // packed card and accepted words
}
inline uint64_t pixel_bytes(uint32_t width, uint32_t height) { return uint64_t(width) * height * kPixelBytes; }

// Fragment shader hashes of the static CHAR_Hair material (docs/NATIVE_HAIR.md): the G-buffer writer gets the strand
// variant, the camera Depth prepass a no-depth-write clone (the strands write their own depth in the G-buffer pass,
// after this frame's groom TLAS exists). Instanced hair (0ca5ee93..., other bindings) keeps its cards.
constexpr uint64_t kDeferredStaticFs = 0x42a95e17ec3adc51ull;
constexpr uint64_t kDepthStaticFs = 0x5a03e5b678b2d040ull;
// The velocity pass's static hair FS (motion vectors): the strand variant hair_primary_velocity.hlsl.
constexpr uint64_t kVelocityStaticFs = 0xb70cadc5dd3adcbdull;
// Instanced hair G-buffer FS (CHAR_Hair_STI_DEF, docs/NATIVE_HAIR.md): no strand variant (M5), counted only.
constexpr uint64_t kDeferredInstancedFs = 0x0ca5ee93d0f33b8cull;

// M5 per-pass strand participation (the period line "[hair primary] frame N: ..."): every draw of a hair pipeline by
// pass and view. Main = the G-buffer depth image at the pixel record's size; other = portraits and other small views,
// which only draw cards today (one pixel record and header, sized and placed for the main view). card_only_groups:
// the (pass, view) groups that drew hair this period with no strand draw: 0 is "no card-only pass left".
struct Participation {
  enum Kind : uint32_t { kGbuffer = 0, kVelocity = 1, kPrepass = 2, kKinds = 3 };
  uint64_t main_total[kKinds] = {}, main_swapped[kKinds] = {}, other_total[kKinds] = {}, instanced = 0;
  void draw(uint32_t kind, bool main_view, bool inst) {
    if (kind >= kKinds) return;
    if (inst) ++instanced;
    else ++(main_view ? main_total : other_total)[kind];
  }
  void swapped(uint32_t kind) { if (kind < kKinds) ++main_swapped[kind]; }
  uint32_t card_only_groups() const {
    uint32_t n = instanced != 0;
    for (uint32_t k = 0; k < kKinds; ++k) n += (main_total[k] && !main_swapped[k]) + (other_total[k] != 0);
    return n;
  }
};

}  // namespace hair_primary
