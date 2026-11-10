// Primary strand hair (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): the frame record the strand fragment
// variants read through a buffer device address baked as a specialization constant. Layer: layer/hair_primary.cpp
// (variants, record) and layer/bvhsplit.cpp (the per-frame header after the groom TLAS build). Shader twin:
// shaders/rt/hair_primary.hlsli (kHp* constants). Header-only, no Vulkan: unit-tested in hair_primary_test.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
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
//  bit 3: M3.2 two-ray coverage, read only by a G-buffer variant built with specialization kSpecIdHairGroomCoverage = 1,
//  bit 4: 5g RT0 = the card's own normal, bit 5: 5g RT3 = the world-space strand tangent; both read only by a G-buffer
//  variant built with specialization kSpecIdHairShadingSpace = 1)
// Table at kTableOffset: per groom instance k (the groom TLAS instance index) four byte offsets into the mesh pool:
//  points (float3), point UV (float2), segment start indices (uint), previous points (float3; 0: none yet).
constexpr uint32_t kFlagTips = 1u << 0, kFlagCamera = 1u << 1, kFlagPrevCamera = 1u << 2, kFlagCoverage = 1u << 3;
constexpr uint32_t kFlagMacroNormal = 1u << 4, kFlagTangentWorld = 1u << 5;
// 5u rt_hair_groom_nearest: the candidate-filtered nearest-strand trace (shaders/rt/hair_primary_nearest.hlsli), read only by
// a G-buffer variant built with specialization kSpecIdHairGroomNearest = 1 (the live off-switch, as kFlagCoverage).
constexpr uint32_t kFlagNearest = 1u << 6;
// 5y rt_hair_mv_card: the strand motion vector from the card's own skinned motion, read only by a velocity variant built
// with specialization kSpecIdHairMvCard = 1. Fallback: only where the row has no valid previous points; All: every pixel.
constexpr uint32_t kFlagMvCardFallback = 1u << 7, kFlagMvCardAll = 1u << 8;
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
// A shared entry's trace covers only the slots that swap this frame: probe draws (refused slots) trace privately and
// store nothing, so "with a strand" is 0 in a frame where every draw was refused, whatever the probes accepted.
constexpr uint32_t kTracedOffset = kTransmitOffset + kMaxGrooms * 16;   // 1792
constexpr uint32_t kRaySampleOffset = kTracedOffset + 16;
// Then accepted strand pixels per compact draw-key slot for the four newest applied tags (tag, tag - 1, tag - 2, tag - 3): the count is recorded
// after this frame's G-buffer pass but may run on the GPU before the last G-buffer passes (hv7 2026-11-04: the record
// held tag - 2 when the count looked for tag; every count read 0). The layer takes the newest tag with entries.
constexpr uint32_t kCountTags = 4;
constexpr uint32_t kTagCountOffset = kRaySampleOffset + 64;   // 1872
constexpr uint32_t kCardCountOffset = kTagCountOffset + kCountTags * kMaxGrooms * 4;
// Then (review 2026-11-08, item 3) the strand diagnostics for the same four tag windows as the markers: the count of a
// current-frame header runs before this frame's G-buffer, so the frame it judges is tag - 1 (the newest tag with
// markers). Per tag j: traced entries and those with a strand (2 words), per groom k the strand pixels and the
// pool-bound refusals; and per tag the card pixels whose strand trace skipped a card-kept groom's strand (mask holes,
// a lower bound: the card marker of a later fragment may overwrite the flag). The C2 colour sample at kTransmitOffset
// carries w = j + 1 for the tag window its sample was written in (0: none).
constexpr uint32_t kTagTracedOffset = kCardCountOffset + kCountTags * kMaxGrooms * 4;   // 3920
constexpr uint32_t kTagPixelOffset = kTagTracedOffset + kCountTags * 8;                 // 3952
constexpr uint32_t kTagRefusedOffset = kTagPixelOffset + kCountTags * kMaxGrooms * 4;   // 4976
constexpr uint32_t kMaskHoleOffset = kTagRefusedOffset + kCountTags * kMaxGrooms * 4;   // 6000
constexpr uint32_t kCountBytes = kMaskHoleOffset + kCountTags * 4;                      // 6016
static_assert(kTagTracedOffset == 3920 && kTagPixelOffset == 3952 && kTagRefusedOffset == 4976 && kMaskHoleOffset == 6000,
              "hair_primary_count.hlsl tag-window offsets");
// Then (5u, rt_hair_coverage_union; only the HP_COUNT_UNION=1 request of the count shader writes it) per tag window j and
// draw slot the accepted strand pixels whatever the card marker of that pixel holds: every pixel whose accepted word is
// (tag << 8) | slot. The words above count only the accepted pixels whose card marker is the same word (strand AND card),
// which falls with the pixel footprint (5u); this one is the quantity the bound was derived for (strand opacity over card
// opacity, tools/hair_groom/coverage_bound.py). The buffer always has the region: zero unless that request ran.
constexpr uint32_t kUnionCountOffset = kCountBytes;                                         // 6016
constexpr uint32_t kCountBytesAll = kUnionCountOffset + kCountTags * kMaxGrooms * 4;        // 7040
static_assert(kUnionCountOffset == 6016, "hair_primary_count.hlsl hair_primary_union_count_offset");
// The card-marker flag of a pixel whose strand trace skipped another groom (its slot neither swaps nor is this draw's).
constexpr uint32_t kMarkerSkipped = 0x20;
// The accepted-marker flag of a card-mode G-buffer pixel (round 3 M2): the swapped velocity variant writes its card MV.
constexpr uint32_t kMarkerCard = 0x40;
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
// Slot masks at kSlotMaskOffset (review 2026-11-08, items 1/2), one word: bits 0-15 the draw slots whose G-buffer
// draws swap to strands this frame (a strand of any other slot is skipped by the pixel trace: a card-kept groom's
// strands no longer hide a swapped draw's own), bits 16-31 the slots whose recorded strand draws must draw their card
// (the submit-time header no longer holds their groom). Written with the header; record-time headers: 0x0000FFFF.
constexpr uint32_t kCrumbAddrOffset = kRayDebugOffset + kRayDebugBytes, kKeyMapOffset = kCrumbAddrOffset + 16;   // 3232, 3248
constexpr uint32_t kSlotMaskOffset = kKeyMapOffset + kMaxGrooms * 4, kHeaderAllocAll = kSlotMaskOffset + 16;   // 3504, 3520
static_assert(kKeyMapOffset == 3248, "hair_primary_gbuffer.hlsl hair_primary_key_map_offset");
static_assert(kSlotMaskOffset == 3504, "hair_primary_gbuffer.hlsl hair_primary_slot_mask_offset");
constexpr uint32_t kAllSwapSlots = 0xFFFFu;
inline uint32_t slot_masks(uint32_t swap_slots, uint32_t card_slots) {
  return (swap_slots & 0xFFFFu) | ((card_slots & 0xFFFFu) << 16);
}
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
  // gbuffer_*: the key's G-buffer strand variant drew. ready_*: the G-buffer passed every swap gate except the
  // prepass card-depth drop (DepthLatch::dropped). The G-buffer swaps only after this key's prepass dropped its card
  // depth (5f3b8a88), and the prepass drops only after a G-buffer swap: with gbuffer_prev alone neither ever went first
  // (2026-11-07 hair-noswap: 0 prepass, 0 G-buffer strand draws while the coverage evidence passed). A ready G-buffer
  // also lets the prepass go first; if that G-buffer then refuses, its card draws take the native-depth fallback.
  std::unordered_set<uint64_t> published, published_prev, gbuffer_now, gbuffer_prev, ready_now, ready_prev;
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
    ready_prev.swap(ready_now);
    ready_now.clear();
  }
  // A device or scene reset: the published sets go, retired draws stay retired until their retry frame.
  void clear() {
    published.clear(); published_prev.clear(); gbuffer_now.clear(); gbuffer_prev.clear(); ready_now.clear(); ready_prev.clear();
    strikes.clear();
  }
  bool retired(uint64_t key) const {
    const auto it = failed.find(key);
    return it != failed.end() && now < it->second.until;
  }
  bool groom_stable(uint64_t key) const { return published.count(key) && published_prev.count(key) && !retired(key); }
  bool allow_gbuffer(uint64_t key) const { return groom_stable(key); }
  bool allow_prepass(uint64_t key) const {
    return groom_stable(key) && (gbuffer_prev.count(key) || ready_prev.count(key));
  }
  bool allow_velocity(uint64_t key) const { return groom_stable(key) && gbuffer_prev.count(key); }
  void gbuffer_drew(uint64_t key) { gbuffer_now.insert(key); }
  void gbuffer_ready(uint64_t key) { if (key) ready_now.insert(key); }
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
// rt_hair_primary_hold: the offline extreme ratios and the bound's noise formula at a live footprint
// (S strand, C card samples of one measurement). At the offline footprint (C 9397) it is the 0.0192 above;
// at 430 card pixels (2026-11-07 hair-flicker, draw 9861) about 0.088, which the strict bound ignores.
constexpr double kCoverRatioMin = 0.9105471772550376, kCoverRatioMax = 1.0752640637962045;
inline double cover_sigma(uint64_t strand, uint64_t card) {
  if (!card) return 0.0;
  const double s = double(strand), c = double(card);
  return (std::sqrt(s) + (s / c) * std::sqrt(c)) / c;
}
// The hold's age limit: the strict freshness plus one full count readback ring (layer/hair_primary.cpp kCountSlots 4
// slots, each covering kCountTags tags), the longest a swapping key can go without a new measurement while every
// count slot is still in flight.
constexpr uint64_t kHoldCountSlots = 4, kHoldFrames = kFreshFrames + kHoldCountSlots * kCountTags;
// A tag applied by two G-buffer frames cannot identify a measured frame.
struct GbFrame { uint64_t frame = 0; uint32_t tag = 0; uint64_t keys[kMaxGrooms] = {}, draw_keys[kDrawSlots] = {};
                 uint32_t swapped_slots = 0;
                 uint64_t pub_frame = 0; };   // 5z: the build frame of the publish applied at submit (0: not known)   // draw slots whose G-buffer draw this frame was swapped to strands
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
  // Review 2026-11-08 item 19: a G-buffer draw that needed its card depth back and had no fallback pipeline (an orphan:
  // that draw lost its hair) blocks this key's prepass drop for 300 frames, as a pipeline with neither variant does.
  void block(uint64_t key, uint64_t frame) { if (key) uncovered[key] = frame; }
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
  // strand: the accepted strand pixels the lower bound judges; inside: those whose card marker is the same word (the
  // upper bound's count, and the lower one's too unless rt_hair_coverage_union gives `strand` the union count, 5u).
  struct Evidence {
    uint64_t frame = 0, card = 0, strand = 0, inside = 0;
    double ratio() const { return card ? double(strand) / double(card) : 0.0; }
    double inside_ratio() const { return card ? double(inside) / double(card) : 0.0; }
  };
  std::unordered_map<uint64_t, Evidence> latest;
  std::unordered_map<uint64_t, uint64_t> incomplete;
  // rt_hair_primary_hold (hysteresis, off: the strict gate above, unchanged). swapping: the G-buffer frame of the key's
  // last strand swap. A measurement with no card pixels (the draw hidden in that counted frame) carries no ratio and
  // does not replace the previous one. A swapping key keeps swapping while its newest measurement is inside the bounds
  // widened by that measurement's own count noise (the bound's formula at the live footprint) and at most kHoldFrames
  // old; it returns to cards on an outside measurement, an invalidation, or when the measurement gets older.
  bool hold = false;
  // rt_hair_coverage_min_pixels (off; 5u audit): a measurement with fewer than kMinEvidenceCard card pixels is no
  // evidence (it replaces nothing), as a measurement without card pixels under the hold. hs1 095322 key 5364: 1..55 card
  // pixels, union ratio 1.056..6.000, switches by "coverage stale" every ~30 frames. Below 64 pixels the count noise of
  // one measurement exceeds any margin (cover_sigma(64, 64) = 0.25), the watchdogs and the dropout counter already ignore
  // such draws (kDropoutMinCard). Such hair keeps whatever its last real evidence allowed until that ages out, then cards,
  // steady until it is drawn at 64 pixels or more again.
  bool min_pixels = false;
  static constexpr uint64_t kMinEvidenceCard = 64;   // == kDropoutMinCard (static_assert there)
  // rt_hair_coverage_footprint (off): every measurement, also a new swap's, is judged by the same bound at its own
  // footprint: [kCoverRatioMin - sigma, kCoverRatioMax + sigma], sigma = cover_sigma(S, C) of that measurement. At the
  // offline footprint (9397 card samples) that is kCoverMin/kCoverMax; small draws (100-1800 pixels, 2026-11-07:
  // three per frame refused 'coverage low') get their own count noise. A real mismatch stays refused (0.60 of 430
  // pixels, 0.31 of 50).
  bool footprint = false;
  std::unordered_map<uint64_t, uint64_t> swapping;
  // rt_hair_swap_latch (off; docs/RTX_HAIR_PRIMARY.md 5t): a key whose own strands failed its bound (a measurement of a
  // frame it was swapped in, outside the bounds a swapping key is judged by) stays on cards until its footprint
  // changes materially, instead of being retried on the next passing probe measurement (strands, verdict, cards,
  // probe, strands: a periodic flicker). The key is still measured every frame by the masked probe (never displayed).
  // Reference footprint: the card pixels of its first card-frame measurement after the failure (same kind of
  // measurement as the ones compared with it); 0 while that is pending. Material: the two counts differ by more than
  // their summed count noise sqrt(C) + sqrt(C0) (per-count variance <= mean, the model of cover_sigma).
  bool latch = false;
  std::unordered_map<uint64_t, uint64_t> latched;
  uint64_t latches = 0, releases = 0;
  // rt_hair_coverage_confirm (off; docs/RTX_HAIR_PRIMARY.md 5u): a held swapping key goes to cards only after
  // kCoverConfirm measurements in a row outside the swapping-key bounds (the hold's bound at each measurement's own
  // footprint), not on one. Same rule and reason as kWatchdogDrawBad: a G-buffer frame applies one header, so a fault of
  // one publish (hs1 085153: be12's slot exchange, `[2 rows: 0+0]` on single frames) makes one bad measurement; by the time
  // it is read (5-6 frames later) that frame's hole is already shown and the card frame it causes is a second artefact.
  // Count noise at the bound fails two measurements in a row at p^2 instead of p. A real loss still fails on the second.
  bool confirm = false;
  std::unordered_map<uint64_t, uint32_t> bad_run;
  static constexpr uint32_t kCoverConfirm = 2;
  static bool outside_swapping(uint64_t card, uint64_t strand, uint64_t inside) {
    const double s = cover_sigma(strand, card), c = double(card);
    return double(strand) < (std::min)(kCoverMin, kCoverRatioMin - s) * c ||
           double(inside) > (std::max)(kCoverMax, kCoverRatioMax + s) * c;
  }
  static bool footprint_changed(uint64_t card, uint64_t reference) {
    const double d = card > reference ? double(card - reference) : double(reference - card);
    return d > std::sqrt(double(card)) + std::sqrt(double(reference));
  }
  void swapped(uint64_t key, uint64_t frame) { if (key && frame > swapping[key]) swapping[key] = frame; }
  bool is_swapping(uint64_t key, uint64_t now) const {
    const auto it = swapping.find(key);
    return hold && it != swapping.end() && now >= it->second && now - it->second <= 2;   // this or the last 2 G-buffers
  }
  void invalidate(uint64_t key, uint64_t frame) {
    if (key && frame > incomplete[key]) incomplete[key] = frame;
  }
  // was_swapped: the measured G-buffer frame drew this key as strands (its own markers, not the probe's). inside: the
  // strand pixels inside the card markers when `strand` is the union count (5u); omitted: the same count.
  void observe(uint64_t key, uint64_t frame, uint64_t card, uint64_t strand, bool was_swapped = false,
               uint64_t inside = UINT64_MAX) {
    if (!key) return;
    if (hold && !card) return;
    if (min_pixels && card < kMinEvidenceCard) return;
    if (inside == UINT64_MAX) inside = strand;
    auto& e = latest[key];
    const bool newest = frame >= e.frame;
    if (newest) e = {frame, card, strand, inside};
    if (confirm && newest && card) {
      uint32_t& b = bad_run[key];
      b = outside_swapping(card, strand, inside) ? b + 1 : 0;
    }
    if (!latch || !card) return;
    if (was_swapped) {
      const double s = hold || footprint ? cover_sigma(strand, card) : 0.0;
      const double r = double(strand) / double(card), r_in = double(inside) / double(card);
      if (r < (std::min)(kCoverMin, kCoverRatioMin - s) || r_in > (std::max)(kCoverMax, kCoverRatioMax + s)) {
        if (!latched.count(key)) ++latches;
        latched[key] = 0;
      }
      return;
    }
    const auto l = latched.find(key);
    if (l == latched.end()) return;
    if (!l->second) l->second = card;
    else if (footprint_changed(card, l->second)) { latched.erase(l); ++releases; }
  }
  // Why allow() refuses (the swap-refusal tally of the period line); Ok when it allows.
  enum class Why : uint8_t { Ok, None, Invalidated, Stale, Low, High, Latched };
  Why why(uint64_t key, uint64_t now, double bound = kCoverMin, double upper = kCoverMax, bool use_hold = true) const {
    const auto it = latest.find(key);
    if (it == latest.end()) return Why::None;
    if (latch && latched.count(key)) return Why::Latched;
    const auto& e = it->second;
    const auto bad = incomplete.find(key);
    if (bad != incomplete.end() && e.frame <= bad->second) return Why::Invalidated;
    const bool held = use_hold && is_swapping(key, now);
    if (!e.frame || now < e.frame || now - e.frame > (held ? kHoldFrames : kFreshFrames) || !e.card) return Why::Stale;
    if (held || (footprint && use_hold)) {
      const double s = cover_sigma(e.strand, e.card);
      bound = (std::min)(bound, kCoverRatioMin - s);
      upper = (std::max)(upper, kCoverRatioMax + s);
    }
    const Why w = e.ratio() < bound ? Why::Low : e.inside_ratio() > upper ? Why::High : Why::Ok;
    if (confirm && held && w != Why::Ok) {   // one bad measurement of a swapping key: not yet confirmed
      const auto b = bad_run.find(key);
      if (b == bad_run.end() || b->second < kCoverConfirm) return Why::Ok;
    }
    return w;
  }
  bool allow(uint64_t key, uint64_t now, double bound = kCoverMin, double upper = kCoverMax) const {
    return why(key, now, bound, upper) == Why::Ok;
  }
  void clear() { latest.clear(); incomplete.clear(); swapping.clear(); latched.clear(); bad_run.clear(); }
};

// Main-view hair draws that kept their cards, by the first gate that refused them (layer/hair_primary.cpp
// hair_primary_swap), per pass (0 G-buffer, 1 velocity, 2 prepass). Printed in the 300-frame period line.
enum class Refusal : uint8_t {
  Publish, Quiet, CoverNone, CoverInvalidated, CoverStale, CoverLow, CoverHigh, Strike, Order, NoSlot, Passes,
  Applied, PrepassDrop, NotDropped, NoStrand, Watchdog, CoverLatched, Count
};
inline const char* refusal_name(Refusal r) {
  static const char* const names[] = {"publish", "load quiet", "coverage none", "coverage invalidated", "coverage stale",
                                      "coverage low", "coverage high", "strike", "order 0", "no slot", "passes mask",
                                      "not applied/live", "prepass drop", "card depth not dropped", "no strand variant",
                                      "watchdog", "coverage latched"};
  static_assert(sizeof names / sizeof names[0] == size_t(Refusal::Count), "one name per refusal");
  return r < Refusal::Count ? names[size_t(r)] : "?";
}
inline Refusal coverage_refusal(CoverageGate::Why w) {
  return w == CoverageGate::Why::None ? Refusal::CoverNone : w == CoverageGate::Why::Invalidated ? Refusal::CoverInvalidated
       : w == CoverageGate::Why::Stale ? Refusal::CoverStale : w == CoverageGate::Why::Low ? Refusal::CoverLow
       : w == CoverageGate::Why::Latched ? Refusal::CoverLatched : Refusal::CoverHigh;
}
struct RefusalTally {
  uint64_t n[3][size_t(Refusal::Count)] = {};
  void add(int pass, Refusal r) { if (pass >= 0 && pass < 3 && r < Refusal::Count) ++n[pass][size_t(r)]; }
  void reset() { *this = RefusalTally{}; }
  // " G-buffer: coverage low 812, strike 3; velocity: ...; prepass: ..." (non-zero reasons only; "none" if empty).
  template <size_t N> void format(char (&out)[N]) const {
    static const char* const passes[] = {"G-buffer:", "velocity:", "prepass:"};
    size_t at = 0;
    out[0] = 0;
    for (int p = 0; p < 3; ++p) {
      bool first = true;
      for (size_t r = 0; r < size_t(Refusal::Count); ++r) if (n[p][r] && at < N) {
        const int w = std::snprintf(out + at, N - at, "%s%s %s %llu", first ? (at ? "; " : " ") : ",", first ? passes[p] : "",
                                    refusal_name(Refusal(r)), (unsigned long long)n[p][r]);
        if (w > 0) at += size_t(w);
        if (at >= N) at = N - 1;
        first = false;
      }
    }
    if (!at) std::snprintf(out, N, " none");
  }
};

// Strand/card switches per hair draw key (docs/RTX_HAIR_PRIMARY.md 5t), always on, also in lean builds: one line per
// 300 frames, "[hair primary] switches ...". A switch is a key whose main-view G-buffer draw was strands in one frame
// and cards in the next one it was drawn in (or the reverse); a key not drawn for more than kSwitchGap frames starts a
// new run without a switch. The cause of a strands->cards switch is the first gate that refused the card frame's draw
// (hair_primary_swap's Refusal). Run lengths (frames a key stayed on strands or on cards between two switches) show
// the oscillation's phases: a frame watchdog cycle is card runs of about kWatchdogFrames and strand runs of the
// verdict latency.
enum class SwitchCause : uint8_t { FrameWatchdog, KeyWatchdog, CoverLow, CoverHigh, CoverStale, Latched, NotApplied, Live,
                                   Other, Count };
constexpr uint64_t kSwitchGap = 2;
inline const char* switch_cause_name(SwitchCause c) {
  static const char* const names[] = {"frame watchdog", "key watchdog", "coverage low", "coverage high",
                                      "coverage stale/none/invalidated", "coverage latched",
                                      "header not applied (camera gate, order)", "live toggle", "other"};
  static_assert(sizeof names / sizeof names[0] == size_t(SwitchCause::Count), "one name per cause");
  return c < SwitchCause::Count ? names[size_t(c)] : "?";
}
// key_watchdog: the watchdog refusal was this key's own (rt_hair_watchdog_draw or rt_hair_watchdog_one_key).
inline SwitchCause switch_cause(Refusal r, bool key_watchdog) {
  switch (r) {
    case Refusal::Watchdog: return key_watchdog ? SwitchCause::KeyWatchdog : SwitchCause::FrameWatchdog;
    case Refusal::CoverLow: return SwitchCause::CoverLow;
    case Refusal::CoverHigh: return SwitchCause::CoverHigh;
    case Refusal::CoverNone: case Refusal::CoverInvalidated: case Refusal::CoverStale: return SwitchCause::CoverStale;
    case Refusal::CoverLatched: return SwitchCause::Latched;
    case Refusal::Applied: return SwitchCause::NotApplied;
    default: return SwitchCause::Other;
  }
}
struct SwapTransitions {
  struct Key {
    uint64_t last = 0, run_start = 0, pre_frame = 0;
    bool seen = false, strands = false, pre_set = false;
    SwitchCause pre = SwitchCause::Other;
    // The period: switches, card pixels (the footprint) and ratios of the measured frames, [0] on cards (probe), [1] strands.
    uint32_t to_cards = 0, to_strands = 0, n[2] = {};
    uint64_t card_min = 0, card_max = 0;
    double r_min[2] = {}, r_max[2] = {};
  };
  using Entry = std::pair<const uint64_t, Key>;
  std::unordered_map<uint64_t, Key> keys;
  uint64_t to_cards[size_t(SwitchCause::Count)] = {}, to_strands = 0, frames = 0, all_cards = 0;
  uint64_t frame = 0;
  uint32_t f_strands = 0, f_cards = 0;
  std::vector<uint32_t> runs[2];   // completed run lengths of the period: [0] card runs, [1] strand runs
  // Frame watchdog trips of the period: draws under / above their own bound in the tripped frames, frames whose strands
  // wrote under a tenth of the card pixels, and the last trip.
  uint64_t wd_trips = 0, wd_key_trips = 0, wd_bad = 0, wd_good = 0, wd_lost = 0, wd_frame = 0, wd_s = 0, wd_c = 0;
  double wd_bound = 0.0;
  void close_frame() {
    if (f_strands + f_cards) { ++frames; all_cards += !f_strands; }
    f_strands = f_cards = 0;
  }
  // The prepass of `key` kept its card depth for this reason: the G-buffer draw that follows is then refused for the
  // card depth alone (Refusal::NotDropped), which is not the cause.
  void prepass_refused(uint64_t key, uint64_t now, SwitchCause cause) {
    if (!key) return;
    Key& k = keys[key];
    k.pre = cause; k.pre_frame = now; k.pre_set = true;
  }
  // One main-view G-buffer draw of `key` in frame `now`: strands, or cards for `cause` (the first draw of a key per
  // frame counts). handshake: refused only because the card depth was still there.
  void gbuffer(uint64_t key, uint64_t now, bool strands, SwitchCause cause = SwitchCause::Other, bool handshake = false) {
    if (!key) return;
    if (now != frame) { close_frame(); frame = now; }
    Key& k = keys[key];
    if (k.seen && k.last == now) return;
    ++(strands ? f_strands : f_cards);
    if (!k.seen || now > k.last + kSwitchGap) { k.seen = true; k.strands = strands; k.run_start = now; }
    else if (k.strands != strands) {
      auto& done = runs[k.strands ? 1 : 0];
      if (done.size() < 4096) done.push_back(uint32_t(now - k.run_start));
      if (strands) { ++to_strands; ++k.to_strands; }
      else {
        if (handshake && k.pre_set && k.pre_frame + kSwitchGap >= now) cause = k.pre;
        ++to_cards[size_t(cause < SwitchCause::Count ? cause : SwitchCause::Other)];
        ++k.to_cards;
      }
      k.strands = strands; k.run_start = now;
    }
    k.last = now;
  }
  // The live switch turned the strands off: a key that was on strands goes to cards (other draws are not tracked).
  void live_off(uint64_t key, uint64_t now) {
    const auto it = keys.find(key);
    if (it != keys.end() && it->second.seen && it->second.strands) gbuffer(key, now, false, SwitchCause::Live);
  }
  // One coverage measurement of `key`: card and strand pixels of a counted frame; swapped: it drew strands there.
  void measure(uint64_t key, uint64_t card, uint64_t strand, bool swapped) {
    if (!key || !card) return;
    Key& k = keys[key];
    const double r = double(strand) / double(card);
    const int i = swapped ? 1 : 0;
    if (!(k.n[0] + k.n[1])) k.card_min = k.card_max = card;
    if (!k.n[i]++) k.r_min[i] = k.r_max[i] = r;
    k.r_min[i] = (std::min)(k.r_min[i], r); k.r_max[i] = (std::max)(k.r_max[i], r);
    k.card_min = (std::min)(k.card_min, card); k.card_max = (std::max)(k.card_max, card);
  }
  void watchdog_trip(uint64_t gframe, uint64_t accepted, uint64_t card, double bound, uint32_t good, uint32_t bad) {
    ++wd_trips; wd_bad += bad; wd_good += good; wd_lost += accepted * 10 < card;
    wd_frame = gframe; wd_s = accepted; wd_c = card; wd_bound = bound;
  }
  static void run_stats(std::vector<uint32_t> v, uint32_t out[3]) {   // min, median, max
    out[0] = out[1] = out[2] = 0;
    if (v.empty()) return;
    std::sort(v.begin(), v.end());
    out[0] = v.front(); out[1] = v[v.size() / 2]; out[2] = v.back();
  }
  uint64_t switches() const {
    uint64_t n = to_strands;
    for (uint64_t c : to_cards) n += c;
    return n;
  }
  // The period line's text after "[hair primary] switches (last N frames):".
  template <size_t N> void format(char (&out)[N], uint64_t latched_now = 0) const {
    size_t at = 0;
    out[0] = 0;
    auto put = [&](const char* fmt, auto... a) {
      if (at >= N - 1) return;
      const int w = std::snprintf(out + at, N - at, fmt, a...);
      if (w > 0) at = (std::min)(at + size_t(w), N - 1);
    };
    put(" %llu strands->cards (", (unsigned long long)(switches() - to_strands));
    for (size_t c = 0; c < size_t(SwitchCause::Count); ++c)
      put("%s%s %llu", c ? ", " : "", switch_cause_name(SwitchCause(c)), (unsigned long long)to_cards[c]);
    uint32_t sr[3], cr[3];
    run_stats(runs[1], sr); run_stats(runs[0], cr);
    put("), %llu cards->strands; %llu frames with hair draws, %llu with every draw on cards; strand runs %zu: "
        "%u/%u/%u frames (min/median/max), card runs %zu: %u/%u/%u; frame watchdog trips %llu (their draws under own "
        "bound %llu, above %llu; frames under a tenth of the card pixels %llu; last: frame %llu, %llu/%llu, bound %.4f), "
        "key watchdog trips %llu; latched keys %llu; worst keys (prefix: switches, ratio as strands min..max (frames), "
        "as cards min..max (frames), card pixels min..max):",
        (unsigned long long)to_strands, (unsigned long long)frames, (unsigned long long)all_cards, runs[1].size(), sr[0],
        sr[1], sr[2], runs[0].size(), cr[0], cr[1], cr[2], (unsigned long long)wd_trips, (unsigned long long)wd_bad,
        (unsigned long long)wd_good, (unsigned long long)wd_lost, (unsigned long long)wd_frame, (unsigned long long)wd_s,
        (unsigned long long)wd_c, wd_bound, (unsigned long long)wd_key_trips, (unsigned long long)latched_now);
    const auto count = [](const Entry* e) { return e->second.to_cards + e->second.to_strands; };
    const Entry* worst[3] = {};
    for (const Entry& kv : keys) {
      const Entry* cand = &kv;
      if (!count(cand)) continue;
      for (const Entry*& w : worst) {
        if (!w) { w = cand; break; }
        if (count(cand) > count(w) || (count(cand) == count(w) && cand->first < w->first)) std::swap(w, cand);
      }
    }
    if (!worst[0]) put(" none");
    for (const Entry* w : worst) if (w) {
      const Key& k = w->second;
      put(" %04llx: %u, %.3f..%.3f (%u), %.3f..%.3f (%u), %llu..%llu;", (unsigned long long)(w->first >> 48),
          k.to_cards + k.to_strands, k.r_min[1], k.r_max[1], k.n[1], k.r_min[0], k.r_max[0], k.n[0],
          (unsigned long long)k.card_min, (unsigned long long)k.card_max);
    }
  }
  // A new period: the counters go, each key's current run stays (a run is counted in the period it ends in).
  void reset_period() {
    for (uint64_t& c : to_cards) c = 0;
    to_strands = frames = all_cards = 0;
    runs[0].clear(); runs[1].clear();
    wd_trips = wd_key_trips = wd_bad = wd_good = wd_lost = 0;
    for (auto it = keys.begin(); it != keys.end();) {
      Key& k = it->second;
      if (k.last + 600 < frame) { it = keys.erase(it); continue; }
      k.to_cards = k.to_strands = k.n[0] = k.n[1] = 0;
      k.card_min = k.card_max = 0;
      k.r_min[0] = k.r_min[1] = k.r_max[0] = k.r_max[1] = 0.0;
      ++it;
    }
  }
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
// rt_hair_warmup (docs/RTX_HAIR_PRIMARY.md 5o): the quiet window ends on a completion signal instead of the frame count.
// The scene build reports each frame whether the groom state is settled: grooms published, every matched hair draw
// joined (no UV readback pending), its LOD resident and chosen (no upload, no LOD vote pending), refit (no full build)
// and keyed. kWarmupSettledFrames such frames in a row end the window; kLoadQuietFrames stays the upper bound. 30 is
// the LOD vote period (hair_groom::kLodVoteFrames): a groom settled that long has held its LOD through a whole vote,
// and the window cannot end within hc1's 10 frames between a first publish and the loss. A frame without a report
// (no groom published) up to kWarmupGapFrames apart does not break the run (the build may skip a frame index).
constexpr uint64_t kWarmupSettledFrames = 30, kWarmupGapFrames = 2;
struct LoadGuard {
  uint64_t image = 0, identity = 0, quiet_until = 0;
  uint64_t boundary = 0;                        // frame of the last boundary
  uint64_t settled_since = 0, settled_last = 0;   // the current run of settled reports (0: none)
  bool early = false;                           // the window ended on the settled signal
  // rt_hair_warmup: one report per scene build. True when this report ends the quiet window.
  bool settle(uint64_t now, bool settled) {
    if (!quiet(now) || now < boundary) return false;
    if (!settled) { settled_since = settled_last = 0; return false; }
    if (!settled_last || now > settled_last + kWarmupGapFrames) settled_since = now;
    settled_last = now;
    if (now < settled_since + kWarmupSettledFrames) return false;
    quiet_until = now;
    early = true;
    return true;
  }
  // The G-buffer depth image of this frame; true at a boundary (the first image, or a different one). identity: the
  // layer's creation id of that image (tracker ImageRec::id; 0 unknown). Review 2026-11-08 item 14: a new level may
  // destroy the depth image and get the same handle for its new one; the creation id differs, so that is a boundary too.
  bool observe(uint64_t depth_image, uint64_t now, uint64_t image_identity = 0) {
    if (!depth_image) return false;
    if (depth_image == image && (!image_identity || !identity || image_identity == identity)) {
      if (image_identity) identity = image_identity;
      return false;
    }
    image = depth_image;
    identity = image_identity;
    quiet_until = now + kLoadQuietFrames;
    boundary = now;
    settled_since = settled_last = 0;
    early = false;
    return true;
  }
  bool quiet(uint64_t now) const { return now < quiet_until; }
  // rt_hair_live_warmup (off; docs/RTX_HAIR_PRIMARY.md 5u audit): the live switch turned the strands on. Every groom
  // slot is rebuilt then (layer/bvhsplit.cpp: a slot built without per-point UVs gets a new points range and a full
  // build), starts its LOD vote again and re-joins its UVs: the state a level load leaves. The same quiet window,
  // ended by the same settled signal (rt_hair_warmup) or kLoadQuietFrames. The depth image is unchanged (no boundary).
  void restart(uint64_t now) {
    quiet_until = now + kLoadQuietFrames;
    boundary = now;
    settled_since = settled_last = 0;
    early = false;
  }
};

// 5u observable (hl1 161106: existing keys lost their strands at the frame a new hair draw first appeared, before any
// groom of it existed): the groom TLAS a strand G-buffer really traces against the header applied to it. Each groom
// TLAS build records its address, its build (recording id), frame and the key of each instance in instance order
// (layer/bvhsplit.cpp: instance k is header row k by construction). At the G-buffer's submit the applied header names a
// TLAS address and its own build; when the newest build of that address is another one and is already submitted (one
// queue: it runs before this G-buffer), the G-buffer traces that TLAS under the header's rows and key map. Diagnostic,
// CPU only, with detailed logging.
struct TlasCheck {
  struct Build { uint64_t rec = 0, frame = 0; uint32_t n = 0; uint64_t keys[kMaxGrooms] = {}; };
  struct Result {
    bool other = false;          // the TLAS at the header's address is another build's, already submitted
    uint32_t n_tlas = 0, n_header = 0;
    int first_diff = -1;         // first instance whose key differs from the header row's (-1: none, counts equal)
    uint64_t key_tlas = 0, key_header = 0, frame_tlas = 0;
  };
  std::unordered_map<uint64_t, Build> built;
  uint64_t checked = 0, other = 0, differ = 0;
  void build(uint64_t address, uint64_t rec, uint64_t frame, const uint64_t* keys, uint32_t n) {
    if (!address) return;
    Build& b = built[address];
    b.rec = rec; b.frame = frame; b.n = (std::min)(n, kMaxGrooms);
    for (uint32_t i = 0; i < kMaxGrooms; ++i) b.keys[i] = i < b.n ? keys[i] : 0;
  }
  template <class Submitted>
  Result check(uint64_t address, uint64_t header_rec, const uint64_t* header_keys, uint32_t header_n, Submitted submitted) {
    Result r;
    const auto it = built.find(address);
    if (!address || it == built.end()) return r;
    ++checked;
    const Build& b = it->second;
    if (b.rec == header_rec || !submitted(b.rec)) return r;   // the header's own build is what this G-buffer traces
    r.other = true;
    ++other;
    r.n_tlas = b.n; r.n_header = (std::min)(header_n, kMaxGrooms); r.frame_tlas = b.frame;
    for (uint32_t i = 0; i < (std::max)(r.n_tlas, r.n_header); ++i) {
      const uint64_t t = i < r.n_tlas ? b.keys[i] : 0, h = i < r.n_header ? header_keys[i] : 0;
      if (t != h) { r.first_diff = int(i); r.key_tlas = t; r.key_header = h; break; }
    }
    if (r.first_diff >= 0) ++differ;
    return r;
  }
  void reset_period() { checked = other = differ = 0; }
};

// rt_hair_debug_late_arrival (dev only, off: 0; docs/RTX_HAIR_PRIMARY.md 5u): reproduces a groom that arrives while the
// other keys already swap (hs1 125217: three characters joined 41 frames after a live switch-on, after the warm-up had
// ended). After every live strands switch-on the hair draws with the key's index count get no groom (no join, no
// instance, not counted by the warm-up) for kLateArrivalFrames: one frame more than the longest warm-up window
// (kLoadQuietFrames), so the release always falls after the warm-up's end, whatever its settled signal did.
constexpr uint64_t kLateArrivalFrames = kLoadQuietFrames + 1;
struct LateArrival {
  bool armed = false, released = true;
  uint64_t on = 0;
  uint32_t held = 0;   // draws held in the current window (one per frame and draw)
  void switch_on(uint64_t now) { armed = true; released = false; on = now; held = 0; }
  bool holding(uint64_t now) const { return armed && now >= on && now - on < kLateArrivalFrames; }
  // One draw with the selected index count in frame `now`: true to hold its groom. *start / *release: log once each.
  bool draw(uint64_t now, bool* start, bool* release) {
    *start = *release = false;
    if (holding(now)) { *start = held++ == 0; return true; }
    if (armed && !released && held) { released = true; *release = true; }
    return false;
  }
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

// rt_hair_groom_primary_current (needs rt_hair_groom_primary_order): the G-buffer pass is recorded before this frame's
// scene build, so at recording only the previous frame's publish is known (the strands then trail the animated cards
// by one frame and their motion vectors are the previous frame's motion). The G-buffer instead copies its header from a
// host-visible slot written at vkQueueSubmit: the record-time publish (always safe), upgraded to this frame's publish
// when its build is ordered before the G-buffer on the GPU (submitted earlier, or earlier in the same submit) and it
// carries the same grooms in the same draw slots (the draw-time swap decisions used the record-time ones).
enum class CurrentPick : uint8_t { Current, NotRecorded, AfterGbuffer, Incompatible, Count };
// Strand dropouts (docs/RTX_HAIR_PRIMARY.md 5h): a draw swapped to strands in a G-buffer frame whose strand pass wrote
// under a tenth of its card pixels (the same-pass marker counts of hair_primary_count.hlsl). card: the draw's card
// pixels with that frame's tag, accepted: its strand pixels; last_card: its card pixels in the last counted frame (a
// draw whose markers are all missing is a dropout only if it had card pixels before); traced / hit: the frame's pixel
// entries sealed with the counted tag, and those with a strand. The first check that can explain it.
enum class Dropout : uint8_t { None, NoMarker, NoTrace, Missed, Rejected, Unattributed, Count };
constexpr uint32_t kDropoutMinCard = 64;   // fewer card pixels: too little evidence (a few pixels at the screen edge)
static_assert(CoverageGate::kMinEvidenceCard == kDropoutMinCard, "one evidence floor for the gate and the watchdogs");
inline const char* dropout_name(Dropout d) {
  static const char* const names[] = {"none", "no card marker (header tag)", "no trace (camera rebase)",
                                      "rays missed (groom TLAS)", "rejected after the hit (owner, card band, tips)",
                                      "no strand pixel (trace counts are of another frame)"};
  return d < Dropout::Count ? names[size_t(d)] : "?";
}
// traced_known: the traced/hit counts are of the judged frame's tag (the count shader counts them for its own tag only).
inline Dropout dropout_reason(uint32_t card, uint32_t accepted, uint32_t last_card, uint32_t traced, uint32_t hit,
                              bool traced_known = true) {
  if (card == 0) return last_card >= kDropoutMinCard ? Dropout::NoMarker : Dropout::None;
  if (card < kDropoutMinCard || uint64_t(accepted) * 10 >= card) return Dropout::None;
  if (!traced_known) return Dropout::Unattributed;
  if (traced == 0) return Dropout::NoTrace;
  if (hit == 0) return Dropout::Missed;
  return Dropout::Rejected;
}
// rt_hair_primary_frame_tag (docs/RTX_HAIR_PRIMARY.md 5i): the tag a G-buffer frame's header carries. The strand
// passes seal pixel entries and card/strand markers with it, and a sealed entry is reused without a new trace. With the
// publish's tag two G-buffer frames can carry one tag (frame N takes this frame's publish at submit, frame N + 1 falls
// back to the same publish: different grooms, not recorded, build after the G-buffer): N + 1 then reuses N's entries,
// traced with N's camera, jitter and pose (holes and stale strands while anything moves) and its markers merge with N's
// (ambiguous evidence, coverage invalidated, cards for a while). With the key: the G-buffer frame itself, unique.
inline uint32_t gbuffer_tag(bool frame_tags, uint32_t publish_tag, uint64_t gbuffer_frame) {
  return frame_tags ? uint32_t(gbuffer_frame) : publish_tag;
}
// The header's flags word (Header::flags, word 15) of packed header bytes.
inline uint32_t header_flags(const uint8_t* bytes) {
  uint32_t f;
  std::memcpy(&f, bytes + 15 * 4, 4);
  return f;
}
// rt_hair_primary_camera_gate: the strand shaders discard every fragment of a header without its build camera
// (hp_rebase), after the prepass dropped the card depth: such a header is never applied (cards that frame).
inline bool header_usable(const uint8_t* bytes) { return (header_flags(bytes) & kFlagCamera) != 0; }

inline const char* current_pick_name(CurrentPick p) {
  static const char* const names[] = {"current", "not recorded", "build after the G-buffer",
                                      "different grooms (this frame's publish, missing draws as cards)"};
  return p < CurrentPick::Count ? names[size_t(p)] : "?";
}
// recorded: this frame's publish exists; submitted_before: its build went down in an earlier submit; build_index /
// gbuffer_index: positions in this submit's command buffer list (-1: not in it).
inline CurrentPick current_pick(bool recorded, bool submitted_before, int build_index, int gbuffer_index, bool compatible) {
  if (!recorded) return CurrentPick::NotRecorded;
  if (!submitted_before && (build_index < 0 || gbuffer_index < 0 || build_index >= gbuffer_index)) return CurrentPick::AfterGbuffer;
  return compatible ? CurrentPick::Current : CurrentPick::Incompatible;
}
// The same grooms in the same draw slots (the order of the header rows may differ).
inline bool same_grooms(const uint64_t* keys_a, const DrawSlots& slots_a, const uint64_t* keys_b, const DrawSlots& slots_b) {
  for (uint32_t i = 0; i < kDrawSlots; ++i) if (slots_a.keys[i] != slots_b.keys[i]) return false;
  for (int pass = 0; pass < 2; ++pass) {
    const uint64_t* x = pass ? keys_b : keys_a;
    const uint64_t* y = pass ? keys_a : keys_b;
    for (uint32_t k = 0; k < kMaxGrooms; ++k) if (x[k]) {
      bool found = false;
      for (uint32_t j = 0; j < kMaxGrooms && !found; ++j) found = y[j] == x[k];
      if (!found) return false;
    }
  }
  return true;
}
// 5i: a submit-time upgrade from the record-time header p to this frame's publish c is safe when every key p maps to a
// draw slot keeps that slot in c (the swapped draws were recorded with those slots) and is still published in c.
inline bool upgrade_safe(const uint64_t* keys_p, const DrawSlots& slots_p, const uint64_t* keys_c, const DrawSlots& slots_c) {
  for (uint32_t i = 0; i < kDrawSlots; ++i) if (slots_p.keys[i] && slots_c.keys[i] != slots_p.keys[i]) return false;
  for (uint32_t k = 0; k < kMaxGrooms; ++k) if (keys_p[k]) {
    bool found = false;
    for (uint32_t j = 0; j < kMaxGrooms && !found; ++j) found = keys_c[j] == keys_p[k];
    if (!found) return false;
  }
  return true;
}
// Review 2026-11-08 item 1: the header a G-buffer reads must be the version of the groom BLASes and points it traces.
// Every build refits the BLASes and rewrites the points in place, so once a newer build is ordered before the G-buffer
// on the GPU (submitted earlier, or earlier in the same submit) only that newest one is consistent: the record-time
// header would trace its older TLAS over refit BLASes, rebased with its older camera. ordered_publish: the newest staged
// publish newer than the record-time tag whose build is ordered before the G-buffer (-1: none, the record-time header is
// still the GPU's version). in_submit(i): the position of staged publish i's recording in this submit (-1: not in it).
template <class InSubmit>
inline int ordered_publish(const Staged* staged, const bool* submitted, int n, uint32_t record_tag, int gbuffer_index,
                           InSubmit in_submit) {
  int best = -1;
  for (int i = 0; i < n; ++i) {
    if (!staged[i].tag || staged[i].tag <= record_tag) continue;
    const int at = submitted[i] ? -1 : in_submit(i);
    if (!submitted[i] && (at < 0 || gbuffer_index < 0 || at >= gbuffer_index)) continue;
    if (best < 0 || staged[i].tag > staged[best].tag) best = i;
  }
  return best;
}
// The submit-time header from that publish for draws recorded against the record-time one: the draws were specialized
// with the record-time draw slots, so each row maps to its key's record-time slot (a key the record-time header did
// not slot: no slot, its strands accepted by no draw), and a record-time slot whose key has no row in the new publish
// draws its card (card_slots; the strand variant then writes the card's native G-buffer and depth: card depth is not
// lost although the prepass dropped it). usable false (no build camera) or another pixel record size: every slot cards.
struct Upgrade { uint32_t key_map[kMaxGrooms]; uint32_t card_slots; };
inline Upgrade upgrade_plan(const DrawSlots& record_slots, const uint64_t* keys_c, bool usable) {
  Upgrade u{};
  for (uint32_t k = 0; k < kMaxGrooms; ++k) u.key_map[k] = keys_c[k] ? record_slots.find(keys_c[k]) : kDrawSlots;
  for (uint32_t i = 0; i < kDrawSlots; ++i) {
    if (!record_slots.keys[i]) continue;
    bool found = false;
    for (uint32_t k = 0; k < kMaxGrooms && !found; ++k) found = keys_c[k] == record_slots.keys[i];
    if (!found || !usable) u.card_slots |= 1u << i;
  }
  if (!usable) for (auto& m : u.key_map) m = kDrawSlots;
  return u;
}

// Round 3 H2 (layer/bvhsplit.cpp groom instances): whether a groom row publishes its draw key (and primary ray mask
// 0xFF). A new slot waits for its second points update (the first copies stale previous points); a slot that has
// published its key keeps it on the frame its LOD switches (prev_frames 1: this build wrote the new LOD's points, UVs and
// BLAS before the TLAS). Dropping it there freed the draw slot, made the submit draw the card and refused the next
// frames: a strand-card-strand flash on every LOD change.
inline bool groom_primary_key(bool has_uv, uint64_t hp_key, uint32_t prev_frames, bool keyed) {
  return has_uv && hp_key && (prev_frames > 1 || (keyed && prev_frames >= 1));
}
// rt_hair_shared_key (docs/RTX_HAIR_PRIMARY.md 5i, 2026-11-09): the G-buffer swap is keyed by the draw's index range
// (hair_primary_draw_key), the groom by its occurrence: two characters with the same hairstyle share one key. A key
// published by one character's groom swapped the other character's draw too while its own groom was out of the publish
// (LOD switch, upload, held or refused soup, first update): that draw found no strand on its card and wrote 0 pixels.
// row_keys: each groom row's published key (0: none); draw_keys: the key of every groom-matched hair draw of this
// capture. A key keeps its rows only when it has at least as many keyed rows as draws; otherwise every row with it is
// cleared (cards for all its draws this frame). Returns the number of rows cleared.
inline uint32_t shared_key_complete(uint64_t* row_keys, uint32_t rows, const uint64_t* draw_keys, uint32_t draws) {
  uint32_t cleared = 0;
  for (uint32_t k = 0; k < rows; ++k) {
    const uint64_t key = row_keys[k];
    if (!key) continue;
    uint32_t have = 0, want = 0;
    for (uint32_t r = 0; r < rows; ++r) have += row_keys[r] == key;
    for (uint32_t d = 0; d < draws; ++d) want += draw_keys[d] == key;
    if (have >= want) continue;
    for (uint32_t r = k; r < rows; ++r) if (row_keys[r] == key) { row_keys[r] = 0; ++cleared; }
  }
  return cleared;
}
// rt_hair_row_stable (docs/RTX_HAIR_PRIMARY.md 5w, live hl1 164930): a groom's row (its groom TLAS instance index, header
// row and InstInfo row) used to be its position in the build's dynamic draw list: sorted by key only while the camera
// is still and holds are on, otherwise the game's capture draw order, which changes from frame to frame (11 of 11 keyed
// rows held another key in the first frame of a camera move, the strands of every key rejected). With the key a groom
// (its dynamic slot key) keeps its row while it is live; a row whose groom left is given to another groom only `lag`
// build frames later, when no frame in flight can still read a header or TLAS that holds the old groom there (the scene
// ring: one groom TLAS per scene slot). Rows between live ones stay empty (an inactive instance, key 0).
struct GroomRows {
  uint64_t key[kMaxGrooms] = {};    // the groom holding the row (0: never used)
  uint64_t last[kMaxGrooms] = {};   // the last build frame it was live in the row
  uint32_t early = 0;               // rows reused before the lag (every row taken: more than kMaxGrooms grooms in `lag` frames)
  // live: this build's groom keys in any order (count <= kMaxGrooms); rows_out[i]: the row of live[i]. now: the build
  // frame, increasing and never 0. Returns the row count (the highest live row + 1).
  uint32_t assign(const uint64_t* live, uint32_t count, uint64_t now, uint64_t lag, uint32_t* rows_out) {
    uint32_t rows = 0;
    for (uint32_t i = 0; i < count; ++i) rows_out[i] = kMaxGrooms;
    for (uint32_t i = 0; i < count; ++i)   // the grooms that keep their row first: a newcomer never takes it
      for (uint32_t r = 0; r < kMaxGrooms; ++r)
        if (key[r] == live[i] && last[r] && last[r] != now) { last[r] = now; rows_out[i] = r; break; }
    for (uint32_t i = 0; i < count; ++i) {
      if (rows_out[i] == kMaxGrooms) {
        uint32_t pick = kMaxGrooms, oldest = kMaxGrooms;
        for (uint32_t r = 0; r < kMaxGrooms && pick == kMaxGrooms; ++r) {
          if (last[r] == now) continue;
          if (!last[r] || last[r] + lag < now) pick = r;
          else if (oldest == kMaxGrooms || last[r] < last[oldest]) oldest = r;
        }
        if (pick == kMaxGrooms) { pick = oldest; if (pick < kMaxGrooms) ++early; }
        if (pick == kMaxGrooms) continue;   // more grooms than rows (the caller caps the count)
        key[pick] = live[i]; last[pick] = now; rows_out[i] = pick;
      }
      rows = rows_out[i] + 1 > rows ? rows_out[i] + 1 : rows;
    }
    return rows;
  }
};
// Round 3 M1: the row's previous points are valid only when they are the previous build frame's (the velocity variant
// pairs them with the previous publish's camera): a slot that skipped updates (held, soup refused, crowd cards) returns
// with points from an older frame. last_update: the build frame of the slot's last update before this one.
inline bool groom_prev_points(uint32_t prev_frames_after, uint64_t last_update, uint64_t now) {
  return prev_frames_after > 1 && last_update && last_update + 1 == now;
}
// 5y (rt_hair_mv_card counter line): why a published groom row carries no previous points (row.w 0: the velocity variant
// then writes camera-only motion, or with kFlagMvCardFallback the card's). prev_frames_after: the points range's updates
// including this one; last_update: the build frame of the slot's update before this one (0: none); now: this build frame.
enum class PrevRow : uint32_t { Valid = 0, FirstUpdate, SameFrame, Skipped, Count };
inline PrevRow prev_row_state(uint32_t prev_frames_after, uint64_t last_update, uint64_t now) {
  if (prev_frames_after <= 1 || !last_update) return PrevRow::FirstUpdate;   // new LOD, upload, reallocated points, first match
  if (last_update >= now) return PrevRow::SameFrame;                         // two updates under one build frame
  if (last_update + 1 != now) return PrevRow::Skipped;                       // held, refused or card frames in between
  return PrevRow::Valid;
}

// Diagnostic (docs/RTX_HAIR_PRIMARY.md 5q): a groom slot whose points left their own bounds between two consecutive
// builds. A dynamic slot is the n-th draw of its mesh in the capture (layer/bvhsplit.cpp bvh_classify), so when the game
// swaps the order of two characters with one hairstyle each slot receives the other character's points in place. world:
// the bounds centre plus that build's camera; diagonal: the larger of the two bounds' diagonals. Derived, not tuned: a
// head that moves more than its own hair's diagonal in one frame is not animation (0.3 m per frame is 18 m/s at 60 fps).
inline bool groom_bounds_jump(const float prev_world[3], uint64_t prev_frame, const float world[3], uint64_t frame,
                              float diagonal) {
  if (!prev_frame || frame != prev_frame + 1 || !(diagonal > 0.0f)) return false;
  const float dx = world[0] - prev_world[0], dy = world[1] - prev_world[1], dz = world[2] - prev_world[2];
  return dx * dx + dy * dy + dz * dz > diagonal * diagonal;
}

// rt_hair_slot_stable (docs/RTX_HAIR_PRIMARY.md 5s): the occurrence index of the hair draws of one mesh, by continuity
// with the place each occurrence had in the capture before instead of by draw order. prev: the occurrences still alive
// (ascending) and their last world position; cur: this capture's draws in draw order. Pairs are taken nearest first
// (correct while a head moves less than half the distance to the other one per frame); a draw without a partner (a new
// character) takes the lowest occurrence nobody holds, so with no history the result is the draw order. An occurrence
// whose character left this capture stays reserved for it (its slot is held or expires on its own).
// out[i]: the occurrence of draw i. Returns the draws whose occurrence differs from their draw order.
constexpr uint32_t kSlotGroup = 16;   // draws of one mesh matched per capture (more: draw order, as before)
struct SlotSeen { uint32_t occurrence = 0; double pos[3] = {}; };
inline uint32_t stable_occurrences(const SlotSeen* prev, uint32_t prev_n, const double (*cur)[3], uint32_t n, uint32_t* out) {
  for (uint32_t i = 0; i < n; ++i) out[i] = i;
  if (n > kSlotGroup || prev_n > kSlotGroup) return 0;
  uint32_t cur_done = 0, prev_done = 0;
  for (;;) {
    double best = 0;
    int bi = -1, bp = -1;
    for (uint32_t i = 0; i < n; ++i) {
      if (cur_done >> i & 1u) continue;
      for (uint32_t p = 0; p < prev_n; ++p) {
        if (prev_done >> p & 1u) continue;
        const double dx = cur[i][0] - prev[p].pos[0], dy = cur[i][1] - prev[p].pos[1], dz = cur[i][2] - prev[p].pos[2];
        const double d = dx * dx + dy * dy + dz * dz;
        if (bi < 0 || d < best) { best = d; bi = int(i); bp = int(p); }
      }
    }
    if (bi < 0) break;
    out[bi] = prev[bp].occurrence;
    cur_done |= 1u << bi; prev_done |= 1u << bp;
  }
  for (uint32_t i = 0; i < n; ++i) {
    if (cur_done >> i & 1u) continue;
    for (uint32_t o = 0;; ++o) {
      bool taken = false;
      for (uint32_t p = 0; p < prev_n; ++p) taken |= prev[p].occurrence == o;
      for (uint32_t j = 0; j < n; ++j) taken |= (cur_done >> j & 1u) && out[j] == o;
      if (!taken) { out[i] = o; break; }
    }
    cur_done |= 1u << i;
  }
  uint32_t moved = 0;
  for (uint32_t i = 0; i < n; ++i) moved += out[i] != i;
  return moved;
}

// Round 3 L4: the whole per-pending-header decision at vkQueueSubmit (layer/hair_primary.cpp hair_primary_submit_begin),
// composed from ordered_publish and upgrade_plan so it is unit-tested as the layer runs it. Staged publish i: its pick,
// submitted flag, header_usable, pixel count and keys. frame_known: the G-buffer frame's GbFrame is this recording's
// (its swapped_slots are then the swap mask; otherwise every slot counts).
struct SubmitDecision { int publish = -1; CurrentPick pick = CurrentPick::NotRecorded; Upgrade plan{}; uint32_t masks = 0; };
template <class InSubmit>
inline SubmitDecision submit_decision(const Staged* staged, const bool* submitted, const bool* usable, const uint32_t* pixels,
                                      const uint64_t (*keys)[kMaxGrooms], int n, uint32_t record_tag, uint32_t record_pixels,
                                      const DrawSlots& record_slots, int gbuffer_index, InSubmit in_submit, bool frame_known,
                                      uint32_t swapped_slots) {
  SubmitDecision d;
  d.publish = ordered_publish(staged, submitted, n, record_tag, gbuffer_index, in_submit);
  if (d.publish >= 0) {
    d.plan = upgrade_plan(record_slots, keys[d.publish], usable[d.publish] && pixels[d.publish] == record_pixels);
    d.pick = d.plan.card_slots ? CurrentPick::Incompatible : CurrentPick::Current;
  } else {
    for (uint32_t k = 0; k < kMaxGrooms; ++k) d.plan.key_map[k] = kDrawSlots;
    bool newer = false;
    for (int i = 0; i < n; ++i) newer |= staged[i].tag > record_tag;
    d.pick = newer ? CurrentPick::AfterGbuffer : CurrentPick::NotRecorded;
  }
  d.masks = slot_masks(frame_known ? swapped_slots & ~d.plan.card_slots : kAllSwapSlots, d.plan.card_slots);
  return d;
}

// Review 2026-11-08 item 19: a frame-level watchdog over the swapped draws of the counted G-buffer frame. card: their
// card pixels (a draw with no marker at all counts its last frame's card pixels, as the dropout counter does), accepted:
// their strand pixels. It trips when the frame's total ratio is below the lowest bound any of its draws may swap with:
// the strict kCoverMin, or the offline minimum ratio minus the largest one-frame count noise of its measured draws
// (cover_sigma, the hold's and footprint bound's own formula). So it never trips while every draw passes its own gate,
// and it does trip on what the per-key gate under rt_hair_primary_hold cannot see: draws whose markers are all missing
// (a measurement without card pixels carries no ratio there), e.g. a header whose strands all discard before their
// markers. Those draws count only when no swapped draw of the frame left a marker (a whole frame without evidence; one
// draw without pixels next to measured ones may simply have left the view). Tripped: no swap for kWatchdogFrames
// G-buffer frames (the readback ring plus freshness: decisions taken before this evidence arrived).
constexpr uint64_t kWatchdogFrames = kHoldFrames;
// rt_hair_watchdog_one_key (off; 5i 2026-11-09): a frame whose only draw under its own bound is one key, next to at
// least one swapped draw above its own bound, does not trip the frame: that key alone keeps its cards for
// kWatchdogFrames (key_active), every other draw keeps its strands. hc1 2026-11-09: one shared-key draw with 0 strand
// pixels put every hair of the frame on cards twice a second.
struct Watchdog {
  uint64_t until = 0, trips = 0, card = 0, accepted = 0, unmarked = 0;
  double max_sigma = 0.0;
  bool one_key = false;   // rt_hair_watchdog_one_key
  uint32_t good = 0, bad = 0;
  uint64_t bad_key = 0, spared = 0;
  std::unordered_map<uint64_t, uint64_t> key_until;
  void begin() { card = accepted = unmarked = 0; max_sigma = 0.0; good = bad = 0; bad_key = 0; }
  // One swapped draw of the judged frame: its card and strand pixels (card 0 with last_card: no marker at all).
  void draw(uint32_t c, uint32_t s, uint32_t last_card, uint64_t key = 0) {
    if (c) max_sigma = (std::max)(max_sigma, cover_sigma(s, c));
    card += c;
    accepted += s;
    if (!c && last_card >= kDropoutMinCard) unmarked += last_card;
    if (c >= kDropoutMinCard) {   // the draw against its own bound (the per-draw watchdog's term)
      if (double(s) >= (std::min)(kCoverMin, kCoverRatioMin - cover_sigma(s, c)) * double(c)) ++good;
      else { ++bad; bad_key = key; }   // one call per draw slot: each key once per judged frame
    }
  }
  double bound() const { return (std::min)(kCoverMin, kCoverRatioMin - max_sigma); }
  // True when the frame trips the watchdog.
  bool judge(uint64_t now) {
    if (!card) card = unmarked;
    if (card < kDropoutMinCard || double(accepted) >= bound() * double(card)) return false;
    if (one_key && bad == 1 && good >= 1 && bad_key) {
      for (auto it = key_until.begin(); it != key_until.end();) it = it->second <= now ? key_until.erase(it) : std::next(it);
      key_until[bad_key] = now + kWatchdogFrames;
      ++spared;
      return false;
    }
    until = now + kWatchdogFrames;
    ++trips;
    return true;
  }
  bool active(uint64_t now) const { return now < until; }
  bool key_active(uint64_t key, uint64_t now) const {
    const auto it = key_until.find(key);
    return it != key_until.end() && now < it->second;
  }
};

// rt_hair_watchdog_draw (off; docs/RTX_HAIR_PRIMARY.md 5i item 11): the item 19 watchdog judged per draw key instead of
// per frame. A swapped draw is bad when its strand pixels fall under its own bound (the frame watchdog's per-draw term:
// min(kCoverMin, kCoverRatioMin - cover_sigma) of its own measurement); a draw without any marker counts its last card
// pixels with no strands, but only when no swapped draw of the frame left a marker (the frame watchdog's rule: one draw
// without pixels next to measured ones may simply have left the view). Fewer than kDropoutMinCard card pixels: no
// evidence, the key's run is unchanged. A key trips after kWatchdogDrawBad bad judged G-buffer frames in a row and keeps
// its cards for kWatchdogFrames; every other draw keeps swapping.
// kWatchdogDrawBad = 2: a G-buffer frame applies exactly one header (GbFrame: one tag, unique_frame), so a fault of one
// publish (its header camera or rows) can make at most one judged frame bad; a second bad frame in a row needs a fault
// that outlives a publish, which is what the hold is for.
constexpr uint32_t kWatchdogDrawBad = 2;
struct DrawWatchdog {
  struct Key { uint32_t bad = 0; uint64_t last = 0, until = 0; };
  struct Seen { uint64_t key = 0; uint32_t card = 0, accepted = 0, last_card = 0; };
  std::unordered_map<uint64_t, Key> keys;
  Seen seen[kDrawSlots] = {};
  uint32_t n = 0;
  bool marked = false;
  uint64_t trips = 0;
  void begin() { n = 0; marked = false; }
  void draw(uint64_t key, uint32_t c, uint32_t s, uint32_t last_card) {
    if (key && n < kDrawSlots) seen[n++] = {key, c, s, last_card};
    marked |= c != 0;
  }
  // The judged G-buffer frame `frame` (each frame judged once per key): the number of keys that tripped now.
  uint32_t judge(uint64_t frame) {
    uint32_t tripped = 0;
    for (uint32_t i = 0; i < n; ++i) {
      const Seen& d = seen[i];
      uint32_t card = d.card;
      double sigma = 0.0;
      if (card) sigma = cover_sigma(d.accepted, card);
      else if (!marked && d.last_card >= kDropoutMinCard) card = d.last_card;
      if (card < kDropoutMinCard) continue;
      Key& k = keys[d.key];
      if (k.last >= frame) continue;
      k.last = frame;
      if (double(d.accepted) >= (std::min)(kCoverMin, kCoverRatioMin - sigma) * double(card)) { k.bad = 0; continue; }
      if (++k.bad < kWatchdogDrawBad) continue;
      k.bad = 0;
      k.until = frame + kWatchdogFrames;
      ++trips;
      ++tripped;
    }
    return tripped;
  }
  bool active(uint64_t key, uint64_t now) const {
    const auto it = keys.find(key);
    return it != keys.end() && now < it->second.until;
  }
};

// 5u (docs/RTX_HAIR_PRIMARY.md): the coverage measurements of the period by footprint, always on, lean too, CPU only:
// one line per period, "[hair primary] coverage by footprint". It separates the causes of a strand key failing its bound:
// A, the measured ratio falling with the footprint: S/C (strand pixels inside the card markers, the gate's count) per
// card-pixel bin, against H/C (the shared pixel trace's hits on the key's own groom rows, swapped draws: what the
// strands cover, whatever the card marker says) and U/C (every accepted strand pixel; only with rt_hair_coverage_union);
// B, draws without strands (dropouts: judged count under a tenth of >= kDropoutMinCard card pixels), frame-wide (every
// measured swapped draw of a frame, at least two) or not, with nothing traced on their rows (header, TLAS, rebase) or
// traced and then rejected (ownership, key map, row UVs), on keys with two groom rows (5s); C, count noise: own-bound
// fails in a bin whose mean judged ratio is well inside the bound.
constexpr uint32_t kFootprintBins = 5;
inline uint32_t footprint_bin(uint64_t card) {
  return card < 256 ? 0u : card < 1024 ? 1u : card < 4096 ? 2u : card < 16384 ? 3u : 4u;
}
struct FootprintStats {
  struct Bin {   // [0] card frames (the masked probe), [1] strand frames (the key's own swapped draw)
    uint64_t n[2] = {}, unions[2] = {}, fails = 0;
    double inside[2] = {}, uni[2] = {}, judged = 0.0, traced = 0.0;
  };
  Bin bins[kFootprintBins];
  uint64_t dropouts = 0, frame_wide = 0, untraced = 0, rejected = 0, two_row = 0, frames = 0;
  uint32_t f_marked = 0, f_dropped = 0;
  void begin() { f_marked = f_dropped = 0; }
  // One measured draw slot of the judged frame. inside: S; traced: H (meaningful for swapped draws only: a probe traces
  // privately and stores nothing); uni: U, or < 0 when the count did not run the union request; judged: the count the
  // gate judged (U with rt_hair_coverage_union, else S); rows: the groom rows of the key in that frame.
  void draw(uint64_t card, uint64_t inside, uint64_t traced, int64_t uni, uint64_t judged, bool swapped, uint32_t rows) {
    if (card < kDropoutMinCard) return;
    Bin& b = bins[footprint_bin(card)];
    const int i = swapped ? 1 : 0;
    const double c = double(card);
    ++b.n[i];
    b.inside[i] += double(inside) / c;
    if (uni >= 0) { ++b.unions[i]; b.uni[i] += double(uni) / c; }
    if (!swapped) return;
    b.judged += double(judged) / c;
    b.traced += double(traced) / c;
    // The bound a held swapping key is judged by (CoverageGate::why under rt_hair_primary_hold).
    if (double(judged) < (std::min)(kCoverMin, kCoverRatioMin - cover_sigma(judged, card)) * c) ++b.fails;
    ++f_marked;
    if (judged * 10 < card) {   // dropout_reason's rule, on the judged count
      ++dropouts; ++f_dropped;
      ++(traced * 10 < card ? untraced : rejected);
      two_row += rows >= 2;
    }
  }
  void end_frame() {
    ++frames;
    if (f_marked >= 2 && f_dropped == f_marked) ++frame_wide;
  }
  // The period line's text after "[hair primary] coverage by footprint (last N frames):".
  template <size_t N> void format(char (&out)[N]) const {
    static const char* const names[kFootprintBins] = {"64-255", "256-1023", "1024-4095", "4096-16383", "16384+"};
    size_t at = 0;
    out[0] = 0;
    auto put = [&](const char* fmt, auto... a) {
      if (at >= N - 1) return;
      const int w = std::snprintf(out + at, N - at, fmt, a...);
      if (w > 0) at = (std::min)(at + size_t(w), N - 1);
    };
    auto mean = [](double sum, uint64_t n) { return n ? sum / double(n) : 0.0; };
    for (uint32_t i = 0; i < kFootprintBins; ++i) {
      const Bin& b = bins[i];
      if (!(b.n[0] + b.n[1])) continue;
      put(" card pixels %s: strands %llu, S/C %.3f, judged %.3f, H/C %.3f, U/C %.3f (%llu), own-bound fails %llu; cards %llu, "
          "S/C %.3f, U/C %.3f (%llu) |", names[i], (unsigned long long)b.n[1], mean(b.inside[1], b.n[1]),
          mean(b.judged, b.n[1]), mean(b.traced, b.n[1]), mean(b.uni[1], b.unions[1]), (unsigned long long)b.unions[1],
          (unsigned long long)b.fails, (unsigned long long)b.n[0], mean(b.inside[0], b.n[0]), mean(b.uni[0], b.unions[0]),
          (unsigned long long)b.unions[0]);
    }
    put(" dropouts %llu (frame-wide %llu of %llu judged frames, nothing traced %llu, traced then rejected %llu, keys on two "
        "rows %llu)", (unsigned long long)dropouts, (unsigned long long)frame_wide, (unsigned long long)frames,
        (unsigned long long)untraced, (unsigned long long)rejected, (unsigned long long)two_row);
  }
  void reset_period() { *this = FootprintStats{}; }
};

// Review 2026-11-08 item 12: rt_hair_groom_cascade_drop removes the hair cards from the game's sun cascades. The hair's
// sun shadow then exists only where the core's RT sun shadow replaces the game's mask: the core publishes the frame of
// its last applied RT sun shadow (frame_index + 1) in this persist key; the drop needs it from this or the last frame.
constexpr const char* kSunRtPersistKey = "bg3rtx.rt_sun_applied";
inline bool sun_rt_fresh(uint64_t applied_frame, uint64_t now) {
  return applied_frame != 0 && applied_frame + 2 >= now + 1;
}

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
// Static hair DepthShadow FS (docs/NATIVE_HAIR.md): the sun cascade card draws (M2.2 rt_hair_groom_cascade_drop).
constexpr uint64_t kShadowStaticFs = 0x34951669ed4b3b0full;

// M2.2 (docs/RTX_HAIR_SHADOW_MAPS.md option 1): a static hair card draw in a sun cascade pass is drawn with the
// depth-write-free clone (no card silhouette in the game's shadow map; the XFB capture and vertex stage unchanged)
// when the key is on, its draw key (the index range, the same in the camera and cascade passes) is in the groom set
// published this or the previous frame (the RT scene then holds its strands), and the clone exists.
inline bool cascade_card_drop(bool key_on, bool sun_cascade, bool published_key, bool has_clone) {
  return key_on && sun_cascade && published_key && has_clone;
}
inline bool publish_fresh(uint64_t published_frame, uint64_t now) {
  return published_frame != 0 && published_frame <= now + 1 && published_frame + 1 >= now;
}

// M5 per-pass strand participation (the period line "[hair primary] frame N: ..."): every draw of a hair pipeline by
// pass and view. Main = the G-buffer depth image at the pixel record's size; other = portraits and other small views,
// which only draw cards today (one pixel record and header, sized and placed for the main view). card_only_groups:
// the (pass, view) groups that drew hair this period with no strand draw: 0 is "no card-only pass left".
struct Participation {
  enum Kind : uint32_t { kGbuffer = 0, kVelocity = 1, kPrepass = 2, kKinds = 3 };
  uint64_t main_total[kKinds] = {}, main_swapped[kKinds] = {}, other_total[kKinds] = {}, instanced = 0;
  uint64_t cascade_total = 0, cascade_dropped = 0;   // M2.2: static hair card draws in sun cascade passes
  uint64_t cascade_no_rt_sun = 0;                    // item 12: kept because no RT sun shadow was applied
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

// 5x diagnostic (branch claude/hair-pool-reuse): the build camera's step per frame, kept for the frame watchdog, which
// judges a G-buffer frame about 5 frames after it ran. note() records a publish's build frame and camera; step(F) is the
// distance from frame F's camera to the camera of the newest earlier noted frame (-1: F or an earlier frame not kept).
// Every hole of hl1 (125217, 164930, 180301, 180949) began the frame the runner's wheel zoom began.
// 5z diagnostic: the projection centre (eye) of a camera-relative view-projection, in that space. `vp` is the game's
// camera UBO at +128 as the shaders read it (row-major, clip_j = sum_k p_k vp[4k + j] + vp[12 + j], rt/xfb_to_world.hlsl):
// the point whose clip x, y and w are 0. Its distance from the origin is returned (the strand rays start at the origin);
// -1 when the matrix is singular there (orthographic).
inline float vp_eye(const float vp[16], float eye[3]) {
  const double r0[3] = {vp[0], vp[4], vp[8]}, r1[3] = {vp[1], vp[5], vp[9]}, r2[3] = {vp[3], vp[7], vp[11]};
  const double b[3] = {-double(vp[12]), -double(vp[13]), -double(vp[15])};
  auto cross = [](const double* a, const double* c, double* o) {
    o[0] = a[1] * c[2] - a[2] * c[1]; o[1] = a[2] * c[0] - a[0] * c[2]; o[2] = a[0] * c[1] - a[1] * c[0];
  };
  double c0[3], c1[3], c2[3];
  cross(r1, r2, c0); cross(r2, r0, c1); cross(r0, r1, c2);
  const double det = r0[0] * c0[0] + r0[1] * c0[1] + r0[2] * c0[2];
  if (!(std::fabs(det) > 1e-30)) return -1.0f;
  double d2 = 0;
  for (int i = 0; i < 3; ++i) {
    eye[i] = float((c0[i] * b[0] + c1[i] * b[1] + c2[i] * b[2]) / det);
    d2 += double(eye[i]) * eye[i];
  }
  return std::isfinite(d2) ? float(std::sqrt(d2)) : -1.0f;
}

struct CameraSteps {
  static constexpr uint32_t kKeep = 32;
  // eye: 5z, the projection centre of the build's camera-relative VP (vp_eye; -1: not read). The strand passes start
  // their rays at the origin of that space (hp_rebase), which is right only while the eye sits there.
  struct Entry { uint64_t frame = 0; float pos[3] = {}; float eye = -1.0f; };
  Entry e[kKeep] = {};
  void note(uint64_t frame, const float pos[3], float eye = -1.0f) {
    if (!frame) return;
    Entry& x = e[frame % kKeep];
    x.frame = frame;
    for (int i = 0; i < 3; ++i) x.pos[i] = pos[i];
    x.eye = eye;
  }
  const Entry* find(uint64_t frame) const {
    const Entry& x = e[frame % kKeep];
    return frame && x.frame == frame ? &x : nullptr;
  }
  // The distance between the build cameras of two frames (-1: either not kept).
  float between(uint64_t a, uint64_t b) const {
    const Entry* x = find(a);
    const Entry* y = find(b);
    if (!x || !y) return -1.0f;
    float d2 = 0;
    for (int i = 0; i < 3; ++i) d2 += (x->pos[i] - y->pos[i]) * (x->pos[i] - y->pos[i]);
    return std::sqrt(d2);
  }
  float step(uint64_t frame) const {
    const Entry* now = nullptr;
    const Entry* prev = nullptr;
    for (const Entry& x : e) {
      if (!x.frame) continue;
      if (x.frame == frame) now = &x;
      else if (x.frame < frame && (!prev || x.frame > prev->frame)) prev = &x;
    }
    if (!now || !prev) return -1.0f;
    float d2 = 0;
    for (int i = 0; i < 3; ++i) d2 += (now->pos[i] - prev->pos[i]) * (now->pos[i] - prev->pos[i]);
    return std::sqrt(d2);
  }
};

// 6a rt_hair_stale_publish (docs/RTX_HAIR_PRIMARY.md 6a). hl1 210357: all 5 frame watchdog trips were G-buffer frames
// that applied the previous build frame's publish (k = 1) while the camera moved (header camera one step, 0.34-0.70 m,
// from the frame's): the newest publish ordered before the G-buffer on the queue is all the GPU holds (the builds rewrite
// the points and BLASes in place), and its rebased trace (hp_rebase) lost the strands in exactly those frames. The
// submit-time check: the distance between the applied publish's build camera and the G-buffer's own camera (its camera
// UBO +208 read at the submit, else the build camera noted for its frame; -1 unknown). Over kStaleCameraTol the frame
// draws its cards (card mode of every recorded slot: card G-buffer and card depth, no coverage evidence, no watchdog
// judgement) instead of tracing a publish of another camera. The tolerance is the card band's fixed term
// (hair_primary_max_radius, hp_on_card): a strand displaced by less stays inside its card's band.
constexpr float kStaleCameraTol = 0.01f;
enum class Stale : uint8_t { Own, SameCamera, Moved, Unknown, Count };
inline const char* stale_name(Stale s) {
  static const char* const names[] = {"own build frame, same camera", "older publish, same camera",
                                      "camera moved since the publish", "camera not known"};
  return s < Stale::Count ? names[size_t(s)] : "?";
}
// pub_frame: the applied publish's build frame (0: none); gb_frame: the G-buffer frame; offset_m: the camera distance.
inline Stale stale_publish(uint64_t pub_frame, uint64_t gb_frame, float offset_m) {
  if (!pub_frame) return Stale::Unknown;
  if (offset_m < 0.0f || !std::isfinite(offset_m)) return pub_frame == gb_frame ? Stale::Own : Stale::Unknown;
  if (offset_m > kStaleCameraTol) return Stale::Moved;
  return pub_frame == gb_frame ? Stale::Own : Stale::SameCamera;
}
// The card-mode slots the check adds: every recorded slot when the key is on and the camera moved or is unknown for an
// older publish (a frame of its own publish with an unknown camera keeps the old behaviour: hp_rebase gives org 0).
inline uint32_t stale_card_slots(bool enabled, Stale s, const DrawSlots& recorded) {
  if (!enabled || (s != Stale::Moved && s != Stale::Unknown)) return 0u;
  uint32_t m = 0;
  for (uint32_t i = 0; i < kDrawSlots; ++i) if (recorded.keys[i]) m |= 1u << i;
  return m;
}
inline float camera_distance(const float* a, const float* b) {
  double d2 = 0;
  for (int i = 0; i < 3; ++i) d2 += (double(a[i]) - b[i]) * (double(a[i]) - b[i]);
  return std::isfinite(d2) ? float(std::sqrt(d2)) : -1.0f;
}

}  // namespace hair_primary
