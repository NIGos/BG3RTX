// rt_hair_groom_primary (docs/RTX_HAIR_PRIMARY.md): the visible hair of matched static CHAR_Hair draws becomes the
// strand groom. At pipeline creation every static hair pipeline gets a variant: the camera Depth prepass a clone
// without depth writes (the cards leave no depth; their XFB capture is unchanged), the G-buffer the strand fragment
// shader shaders/rt/hair_primary_gbuffer.hlsl with depth GREATER_OR_EQUAL + writes (reversed Z), the velocity pass
// (FS b70cadc5) rt/hair_primary_velocity.hlsl (strand motion from last frame's points, no depth writes). At draw time a
// matched draw (its index range is in the groom set of the latest build) is drawn with the variant. The strand
// shader reads the frame record (common/hair_primary.h) through the header address baked as specialization
// constants 1100/1101; bvhsplit publishes the header after the groom TLAS build (hair_primary_publish).
// Variants exist only with linear swept spheres, shaderInt64 and fragmentStoresAndAtomics. Since 5m (docs/RTX_HAIR_PRIMARY.md,
// common/hair_live.h) the header and the variants are made on every such device whether or not the key is on (about
// 0.1 s of pipeline creation at the first level load, logged as "strand variants"), and rt_hair_groom_primary /
// rt_hair_groom (both gated by the live switch rt_hair) decide each frame whether matched draws swap.
#include "host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../common/hair_live.h"
#include "../common/hair_primary.h"
#include "../common/shader_variant.h"
#include "hair_groom.h"

namespace bg3 {

namespace {

struct HpBuf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  VkDeviceAddress addr = 0;
  void* map = nullptr;   // host-visible buffers only (the breadcrumbs)
};

DeviceData* g_dd = nullptr;
bool g_startup_enabled = false;        // a device was created: the header and variants are attempted (5m: always)
hair_live::Toggle g_live;              // g_mu: the live switch (common/hair_live.h)
std::atomic<bool> g_live_on{false};    // g_live.on, read without g_mu (strands_on)
std::atomic<uint64_t> g_live_frame{~0ull};   // frame_index of the last live_tick
std::atomic<bool> g_live_ever{false};  // strands were on at least once on this device (staged headers may exist)
uint64_t g_off_restores = 0;           // native draws given back their card depth while strands were off (period line)
uint64_t g_variant_us = 0, g_variant_pipes = 0;   // strand variant creation: CPU time and pipelines (startup cost)
bool g_frag_stores = false;            // fragmentStoresAndAtomics enabled by the game
std::mutex g_mu;
HpBuf g_header, g_pixels, g_crumbs;              // header: fixed (its address is in every variant); pixels: render size
bool g_header_tried = false;
bool g_pixels_fresh = false;           // just allocated: zero it before the next header update
uint32_t g_px_w = 0, g_px_h = 0;
std::unordered_map<VkShaderModule, uint64_t> g_mods;           // the native hair FS modules (erased when destroyed)
std::unordered_set<VkShaderModule> g_inst_mods;                // instanced hair G-buffer FS modules (counted only, M5)
// Every game pipeline with a hair FS: participation kind (hair_primary::Participation::Kind) + 4 when instanced.
std::unordered_map<VkPipeline, uint32_t> g_hair_pipes;
hair_primary::Participation g_part;
// M2.2 rt_hair_groom_cascade_drop: game static hair DepthShadow pipeline -> clone without depth writes.
std::unordered_map<VkPipeline, VkPipeline> g_cascade_clone;
std::unordered_set<uint64_t> g_pub_keys;   // draw keys of the last publish
uint64_t g_pub_frame = 0;                  // its frame_index + 1 (0: none)
hair_primary::LoadGuard g_load;                               // level-load boundary (the G-buffer depth image)
std::unordered_map<VkPipeline, VkPipeline> g_variant;         // game pipeline -> variant
enum class Pass : uint8_t { Prepass, Gbuffer, Velocity };
std::unordered_map<VkPipeline, Pass> g_pass_of;               // game pipeline -> its pass (with a g_variant entry)
// Index ranges of the grooms in the last two published TLASes: a draw is swapped only when its groom is in both, so
// the prepass (recorded before this frame's build) and the G-buffer (after it) agree on a draw that just appeared.
hair_primary::SwapGate g_gate;   // which draws are swapped (common/hair_primary.h SwapGate)
// hair_primary_swap's lock-free leave (common/hair_primary.h DrawFilter), added to under g_mu: every pipeline that
// entered g_hair_pipes, g_variant or g_cascade_clone, and every draw key that entered g_gate.published or g_depth.keys.
hair_primary::DrawFilter g_pipe_filter, g_key_filter;
hair_primary::CoverageGate g_coverage;
constexpr uint32_t kProbeAfter = 1u << 31;
struct DrawVariants { VkPipeline strand[hair_primary::kDrawSlots] = {}, probe[hair_primary::kDrawSlots] = {}; };
std::unordered_map<VkPipeline, DrawVariants> g_draw_variants;
std::unordered_map<VkPipeline, VkPipeline> g_fallback;
struct CoveragePeriod {
  bool probed = false; uint32_t swapped = 0; uint64_t last_swap = UINT64_MAX;
  double used_min = 0, used_max = 0; uint32_t decisions = 0;
};
std::unordered_map<uint64_t, CoveragePeriod> g_coverage_period;
uint32_t g_ambiguous = 0;
uint64_t g_fallbacks = 0;
hair_primary::DrawSlots g_slots, g_live_slots;
std::unordered_set<uint64_t> g_without_slot;
uint64_t g_published = 0;                                     // frame_index + 1 of the last publish with grooms
struct Stats { uint64_t prepass = 0, gbuffer = 0, velocity = 0, publishes = 0, orphan = 0; } g_st;
hair_primary::PeriodFailures g_failures;
std::unordered_map<uint64_t, uint64_t> g_last_swap_frame;   // key -> frame_index of its last G-buffer strand swap
uint64_t g_swap_resumes = 0;                 // swaps that resumed after card frames (strand/card flicker, period line)
uint64_t g_held_swaps = 0;                    // rt_hair_primary_hold: G-buffer swap frames only the hold allowed (period line)
hair_primary::RefusalTally g_refusals;        // main-view hair draws kept as cards, by first refusing gate (period line)
hair_primary::SwapTransitions g_switches;     // strand/card switches per draw key (5t; the lean period line "switches")
// Keys whose prepass card depth went; native draws must restore it during the frame latch.
hair_primary::DepthLatch g_depth;
std::unordered_set<uint64_t> g_depth_safe_keys;
struct DepthOrphan { uint64_t key, prepass, draw, pipeline; const char* reason; };
DepthOrphan g_depth_orphans[4] = {};
uint32_t g_depth_orphan_count = 0;
uint64_t g_logged_pipes = 0;
// Fail-safe feedback (SwapGate::feedback): strand pixels per groom of the last frame's pixel record, counted on the GPU
// (rt/hair_primary_count.hlsl) at the next publish, copied to a host-visible ring and read kCountSlots - 1 publishes
// later. With them the keys and whether each one's G-buffer variant ran in the counted frame.
constexpr uint32_t kCountSlots = 4;
static_assert(kCountSlots == hair_primary::kHoldCountSlots, "the hold age counts one full readback ring");
struct CountSlot { uint64_t frame = 0, gframe = 0, rec = 0; bool valid = false, uni = false, dense = false; uint32_t tag = 0;
                   uint64_t keys[hair_primary::kMaxGrooms] = {};
                   uint8_t lods[hair_primary::kMaxGrooms] = {};
                   bool drew[hair_primary::kMaxGrooms] = {}; VkBuffer rb = VK_NULL_HANDLE; VkDeviceMemory rb_mem = VK_NULL_HANDLE;
                   const uint32_t* map = nullptr; };
CountSlot g_cnt[kCountSlots];
uint32_t g_cnt_next = 0;
HpBuf g_counts;                          // device local: 64 uints
VkDescriptorSetLayout g_cnt_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_cnt_pl = VK_NULL_HANDLE;
VkPipeline g_cnt_pipes[2] = {};          // [0] rt/hair_primary_count.hlsl, [1] its HP_COUNT_UNION=1 request (5u)
uint64_t g_cnt_gens[2] = {};
uint32_t g_last_tag = 0, g_last_n = 0;   // the previous publish: its tag, pixel count and keys per groom k
uint64_t g_last_keys[hair_primary::kMaxGrooms] = {};
uint8_t g_last_lods[hair_primary::kMaxGrooms] = {};
// Diagnostics of the last read count (logged with the 300-frame line and at each strike): per groom k of the counted
// frame its draw key, LOD, strand pixels and pool-bound refusals.
struct CountSeen { uint64_t frame = 0; uint64_t keys[hair_primary::kMaxGrooms] = {}; uint8_t lods[hair_primary::kMaxGrooms] = {};
                   uint32_t pixels[hair_primary::kMaxGrooms] = {}, refused[hair_primary::kMaxGrooms] = {};
                   uint32_t traced = 0, hit = 0, tag = 0, evidence_tag = 0, tag_totals[hair_primary::kCountTags] = {}, accepted_totals[hair_primary::kCountTags] = {};
                   uint64_t evidence_frame = 0;
                   float ray[16] = {};
                   hair_primary::RebaseSample rebase;
                   hair_primary::Stamp stamp = hair_primary::Stamp::Unread; uint32_t stamp_tag = 0; };   // 6d   // 6c: the judged frame's sampled ray (fresh: its own tag)
CountSeen g_seen;
hair_primary::RebaseStats g_rebase;   // 6c, per period
// C2 shadow transmittance: each draw's measured a_f (strand colour samples of the counted frames), by draw key.
std::unordered_map<uint64_t, hair_primary::TransmitMean> g_transmit;
uint64_t g_transmit_rows = 0;   // groom rows given a measured transmittance since the last 300-frame line
uint64_t g_refused_total = 0;
uint64_t g_misaligned_logged = 0;   // counted frames whose strand pixels all carried an adjacent frame's tag
uint64_t g_adjacent_window = 0, g_adjacent_total = 0;   // counted frames with any adjacent-tag strand pixel
// rt_hair_groom_primary_order (common/hair_primary.h pick_publish): the publishes waiting for their G-buffer pass,
// and what was applied. The header bytes of one publish and the per-groom keys/LODs the count of its frame needs.
struct StagedHeader {
  hair_primary::Staged pick;
  uint64_t recording = 0;
  bool submitted = false;
  uint32_t n = 0, pixels = 0;
  uint8_t bytes[hair_primary::kHeaderBytes] = {};
  hair_primary::DrawSlots slots;
  uint32_t key_map[hair_primary::kMaxGrooms] = {};
  float cam[8] = {};   // the build camera (kCameraOffset; kFlagCamera), the previous publish's (kFlagPrevCamera)
  uint64_t keys[hair_primary::kMaxGrooms] = {};
  uint8_t lods[hair_primary::kMaxGrooms] = {};
  // Item 11 proof: the build's camera UBO, re-read once at the vkQueueSubmit that carries the publish's recording.
  VkBuffer ubo = VK_NULL_HANDLE;
  VkDeviceSize ubo_offset = 0;
  bool cam_checked = false;
  bool cam_gpu = false;   // rt_hair_header_cam_gpu: the build wrote both header cameras on the GPU (apply_header skips them)
};
// rt_hair_header_cam_gpu: the build's camera UBO copied on the GPU (geo_copy_camera, 256 bytes; position at byte 208).
// Each publish first moves the previous publish's position from here to kPrevCameraOffset, then writes its own.
HpBuf g_cam_gpu;
// 6d (detailed logging only): the build stamp cell and its per-G-buffer ring (common/hair_primary.h build_stamp).
HpBuf g_stamp;
uint64_t g_stamp_n[size_t(hair_primary::Stamp::Count)] = {};
uint64_t g_cam_gpu_pub = 0;   // publish frame + 1 of the last GPU camera copy (0: the slot holds none)
// Item 11 (period line "header safety"): publishes whose camera UBO was re-read at the submit of their build, those whose
// re-read differs from the record-time read st.cam (the GPU's xfb_to_world reads the UBO after the submit), and the
// largest distance between the two.
// gpu: publishes whose header cameras the build wrote on the GPU (rt_hair_header_cam_gpu).
struct CamCheck { uint64_t checked = 0, differ = 0, gpu = 0; double max_m = 0.0; } g_cam_check;
// 5y (rt_hair_mv_card counter line): publishes with kFlagCamera, those without kFlagPrevCamera and why.
struct PrevCamStats { uint64_t pubs = 0, gap = 0, gpu = 0; } g_prev_cam;
constexpr int kStaged = 3;
StagedHeader g_staged[kStaged];
int g_staged_next = 0;
// rt_hair_groom_primary_current (common/hair_primary.h CurrentPick): host-visible header slots the G-buffer copies
// from, one per G-buffer recording (a ring far longer than the frames in flight), and their pending choices.
constexpr uint32_t kHeaderSlots = 16;
HpBuf g_hdr_slots;
struct PendingHeader {
  uint64_t cmd = 0, recording = 0, frame = 0;   // the G-buffer recording, its frame (GbFrame::frame)
  uint32_t tag = 0, n = 0, pixels = 0;          // the record-time publish
  uint32_t seal = 0;                            // rt_hair_primary_frame_tag: the frame's own tag (0: the publish's)
  uint32_t pass = 0;                            // 6i: the serial of its scene pass (GbFrame::pass)
  uint64_t keys[hair_primary::kMaxGrooms] = {};
  hair_primary::DrawSlots slots;
  bool open = false;
  bool slotless = false;   // the record-time header (current 0, diagnostic): only checked for a newer ordered build
};
PendingHeader g_pending[kHeaderSlots];
uint32_t g_pending_next = 0;
uint64_t g_current_picks[size_t(hair_primary::CurrentPick::Count)] = {};   // G-buffer submits per choice (period line)
// 5h strand dropouts (period line): publishes without the build camera (and those whose build camera was recovered
// from the build's own frame), G-buffer passes that kept cards because their header had none, and swapped draws whose
// strand pass wrote under a tenth of their card pixels, by first explaining check, with the first four frames.
struct DropoutStats {
  uint64_t no_camera = 0, recovered = 0, gated = 0, upgrades_gated = 0, draws = 0;
  uint64_t why[size_t(hair_primary::Dropout::Count)] = {};
  // rows / row_pixels: the judged frame's strand pixels of the first two groom rows carrying the key (5q: one or both).
  struct First { uint64_t frame, key; uint32_t card, accepted; hair_primary::Dropout why; uint32_t rows = 0, row_pixels[2] = {}; } first[4] = {};
  uint32_t firsts = 0;
} g_drop;
std::unordered_map<uint64_t, uint32_t> g_last_card;   // per draw key: its card pixels in the last counted frame
// Review 2026-11-08 diagnostics (period lines): card pixels whose strand trace skipped a card-kept groom's strand (item
// 2, the holes the old trace left), C2 colour samples taken (item 20), G-buffer submits that applied a header older
// than a build ordered before them (item 1: only the record-time diagnostic path can still do that), submit-time
// upgrades that drew some draws as cards, and the frame watchdog (item 19).
uint64_t g_mask_holes = 0, g_colour_samples = 0, g_header_mismatch = 0, g_card_upgrades = 0, g_card_slots = 0;
hair_primary::Watchdog g_watchdog;
uint64_t g_watchdog_logged = 0, g_watchdog_period = 0;
// 5u: the coverage measurements by footprint (lean period line), and rt_hair_coverage_union (read once per publish): the
// count runs the union request and the gate and the frame watchdog judge every accepted strand pixel instead of only those
// inside the card markers. The pipeline of each request ([0] default, [1] union) and its shader generation.
hair_primary::FootprintStats g_footprint;
hair_primary::LateArrival g_late;
int g_nearest_compiled = -1, g_nearest_flag_logged = -1, g_nearest_compiled_logged = -2;
// 6g rt_hair_card_fill: g_fill_compiled: constant 1053 in the strand G-buffer pipelines (-1: none created yet);
// g_card_fill: the per-pixel card fill runs (compiled in and header flag kFlagCardFill at the last publish), so the
// frame watchdog does not put the hair on cards and a held key needs kFillConfirm low measurements (hair_primary.h).
int g_fill_compiled = -1, g_fill_logged = -2;
bool g_card_fill = false;
hair_primary::TlasCheck g_tlas_check;   // 5u observable (detailed logging)
hair_primary::CameraSteps g_cam_steps;   // 5x: build camera step per frame (frame watchdog line)
float eye_of(uint64_t frame) { const auto* x = g_cam_steps.find(frame); return x ? x->eye : -1.0f; }
uint64_t g_tlas_check_logged = 0;   // 5u: specialization 1047 at the last strand pipeline creation   // rt_hair_debug_late_arrival (dev only)
bool g_cfg_union = false;
// Probe refusals since the last coverage line: no publish of this frame ordered before the prepass, load quiet window.
float g_last_pub_camera[3] = {};   // the last publish's build camera (frame + 0 when it had none)
uint64_t g_last_pub_camera_frame = 0;
bool g_order = false;                       // the last publish was staged (the key on)
std::unordered_map<uint64_t, uint64_t> g_key_seen;   // draw key -> frame_index of its last publish (round 3 L3)
// Warm-up lines (docs/RTX_HAIR_PRIMARY.md 5o; always on, CPU only): frames from a level load to its first strand swap,
// and per draw key from its first publish after the load to its first strand G-buffer swap (0 once swapped).
std::unordered_map<uint64_t, uint64_t> g_first_pub;
struct Warm { bool logged = true; uint64_t first_swaps = 0, wait_sum = 0, wait_max = 0; } g_warm;
constexpr uint64_t kKeyForgetFrames = 600;
int g_cfg_passes = 15;                   // rt_hair_primary_passes, read at each publish (round 3 L5)
bool g_cfg_cascade_drop = false;            // rt_hair_groom_cascade_drop, likewise
// 6h rt_hair_header_scene_pass: only the scene G-buffer pass (5 colour targets, the hair draws) applies the header,
// never the decal pass after it. 6i: g_labels holds the label state (applied_frame: frame_index + 1 of the last
// G-buffer pass that applied or refused; applied_decal: that label was taken by a decal pass, key off) and gives a
// second scene pass recording of one present interval its own header and tag.
hair_primary::HeaderLabels g_labels;
uint64_t& g_applied_frame = g_labels.applied_frame;
bool g_cfg_scene_pass = false;
hair_primary::HeaderPassStats g_header_pass;
std::unordered_set<uint64_t> g_live_keys;   // the keys of the header the G-buffer reads (order mode: swaps need them)
uint64_t g_order_none = 0;                  // G-buffer passes with no publish ordered before them (no swap)
void apply_header(VkCommandBuffer cmd, const StagedHeader& st, int slot = -1, uint32_t tag = 0);
uint64_t g_applied_cmd = 0, g_applied_recording = 0;   // the recording and tag of the last header applied
uint32_t g_applied_tag = 0;
// Per G-buffer frame (frame_index + 1 of its before_pass, order mode): the tag and keys of the header it applied. The
// count of that frame's pixel record seals with that tag and attributes groom k to that header's key k.
using hair_primary::GbFrame;
GbFrame g_gb_frames[16];
// GPU cost of the strand G-buffer draws: a timestamp pair around each (BOTTOM_OF_PIPE: the draw's own completion
// after the work before it), kTimed draws per frame, a ring of kTimeSlots frames; read kTimeSlots - 1 frames later.
constexpr uint32_t kTimed = 64, kTimeSlots = 4;
VkQueryPool g_qp = VK_NULL_HANDLE;
float g_ts_period = 1.0f;
uint32_t g_timed[kTimeSlots] = {};      // pairs written in each slot's frame
uint64_t g_slot_frame[kTimeSlots] = {}; // frame_index + 1 the slot was reset for
double g_ms_sum = 0;                    // strand G-buffer draw ms over the timed frames of the window
uint32_t g_ms_frames = 0;
uint64_t g_stats_start = 0;

bool make(HpBuf& out, VkDeviceSize size, VkBufferUsageFlags usage, const char* what, bool host = false) {
  DeviceData* dd = g_dd;
  out = HpBuf{};
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &out.b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, out.b, &req);
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &fi};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                                         : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out = HpBuf{};
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
  if (host && dd->vk.MapMemory(dd->device, out.m, 0, VK_WHOLE_SIZE, 0, &out.map) == VK_SUCCESS) std::memset(out.map, 0, size);
  out.size = size;
  out.addr = rt_buffer_address(out.b);
  fault_track(what, out.addr, size);
  return out.addr != 0;
}

// Host-visible, mapped (the feedback readback; transfer destination only).
bool make_readback(CountSlot& s) {
  DeviceData* dd = g_dd;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = hair_primary::kCountBytesAll + hair_primary::kDenseBytesAll;   // 6f: the dense samples behind the counts
  bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &s.rb) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, s.rb, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  void* p = nullptr;
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &s.rb_mem) != VK_SUCCESS ||
      dd->vk.BindBufferMemory(dd->device, s.rb, s.rb_mem, 0) != VK_SUCCESS ||
      dd->vk.MapMemory(dd->device, s.rb_mem, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS) {
    if (s.rb_mem) dd->vk.FreeMemory(dd->device, s.rb_mem, nullptr);
    dd->vk.DestroyBuffer(dd->device, s.rb, nullptr);
    s.rb = VK_NULL_HANDLE;
    s.rb_mem = VK_NULL_HANDLE;
    return false;
  }
  s.map = static_cast<const uint32_t*>(p);
  return true;
}

// g_mu held. The counting pipeline (push descriptors: the pixel record, the counts; push: tag, pixel count). v 1: the
// union request (5u, rt_hair_coverage_union), which also writes kUnionCountOffset.
bool count_pipeline(int v) {
  Bg3ShaderBlob blob{};
  if (!shader_get(v ? "rt/hair_primary_count.hlsl?HP_COUNT_UNION=1" : "rt/hair_primary_count.hlsl", "main", "cs_6_6", &blob))
    return false;
  VkPipeline& g_cnt_pipe = g_cnt_pipes[v];
  uint64_t& g_cnt_gen = g_cnt_gens[v];
  if (g_cnt_pipe && g_cnt_gen == blob.generation) return true;
  auto& vk = g_dd->vk;
  if (!g_cnt_dsl) {
    VkDescriptorSetLayoutBinding bd[2];
    for (uint32_t i = 0; i < 2; ++i) bd[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = 2;
    ci.pBindings = bd;
    if (vk.CreateDescriptorSetLayout(g_dd->device, &ci, nullptr, &g_cnt_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pc.setLayoutCount = 1;
    pc.pSetLayouts = &g_cnt_dsl;
    pc.pushConstantRangeCount = 1;
    pc.pPushConstantRanges = &range;
    if (vk.CreatePipelineLayout(g_dd->device, &pc, nullptr, &g_cnt_pl) != VK_SUCCESS) return false;
  }
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mi.codeSize = blob.size_bytes;
  mi.pCode = blob.code;
  VkShaderModule module{};
  if (vk.CreateShaderModule(g_dd->device, &mi, nullptr, &module) != VK_SUCCESS) return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main"};
  ci.layout = g_cnt_pl;
  VkPipeline pipe{};
  const VkResult r = vk.CreateComputePipelines(g_dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe);
  vk.DestroyShaderModule(g_dd->device, module, nullptr);
  if (r != VK_SUCCESS) return false;
  if (g_cnt_pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_cnt_pipe));
  g_cnt_pipe = pipe;
  g_cnt_gen = blob.generation;
  return true;
}

// 6f rt_hair_dense_sample: the judged frame's samples (common/hair_primary.h DenseSample), copied from the tail of the
// pixel record behind the counts. Per period DenseStats; per sample a line for moving and watchdog frames.
bool g_cfg_dense = false;
hair_primary::DenseStats g_dense;
struct DenseAnchor { uint32_t tag = 0, prev_tag = 0; double pos[3] = {}, prev[3] = {}; };
std::unordered_map<uint64_t, DenseAnchor> g_dense_anchor;   // by draw key: its groom's anchor in the world, last sample
std::unordered_map<uint64_t, uint64_t> g_dense_first;       // by draw key: the first judged G-buffer frame that drew it
float g_dense_cam[3] = {};                                  // the G-buffer camera of the last sampled frame, and its tag
uint32_t g_dense_cam_tag = 0, g_dense_lines = 0;
// 6h: sample lines per stats period (hx1 180928: 600 a session were used up by the first 16 moving frames, none left
// for the other windows), one of a slot's four cells per frame in turn; a pair of samples at most this many tags apart.
constexpr uint32_t kDenseLines = 96, kDensePairTags = 4;
void dense_frame(const CountSlot& s, const GbFrame& gf, bool tripped) {
  using namespace hair_primary;
  const uint32_t* w = s.map + kCountBytesAll / 4;
  DenseSample smp[kDenseSamples];
  const DenseSample* first = nullptr;
  for (uint32_t i = 0; i < kDenseSamples; ++i) {
    smp[i] = dense_parse(w + i * kDenseWords, gf.tag);
    if (smp[i].valid) { if (!first) first = smp + i; }
    else if (w[i * kDenseWords + 1] == gf.tag) ++g_dense.torn;
  }
  for (uint32_t k = 0; k < kDrawSlots; ++k) if (gf.draw_keys[k]) g_dense_first.emplace(gf.draw_keys[k], gf.frame);
  if (!first) return;
  ++g_dense.frames;
  // The camera step per frame from the cameras the GPU read (not the CPU's labels): unknown after a gap.
  float step = -1.0f;
  const uint32_t dt = gf.tag - g_dense_cam_tag;
  if (g_dense_cam_tag && dt >= 1 && dt <= kDensePairTags) {
    float d2 = 0;
    for (int i = 0; i < 3; ++i) d2 += (first->cam[i] - g_dense_cam[i]) * (first->cam[i] - g_dense_cam[i]);
    step = std::sqrt(d2) / float(dt);
  }
  std::memcpy(g_dense_cam, first->cam, sizeof g_dense_cam);
  g_dense_cam_tag = gf.tag;
  const bool moving = step > kDenseMovingStep;
  for (uint32_t i = 0; i < kDenseSamples; ++i) {
    const DenseSample& d = smp[i];
    if (!d.valid) continue;
    const uint64_t key = gf.draw_keys[d.slot];
    float drift = -1.0f;
    uint32_t drift_tag = 0;
    if (key && d.owner_row != 0xFF) {
      double a[3];
      d.anchor_world(a);
      // The first sample of a key in this frame moves the key's anchor on; every sample compares with the frame before.
      DenseAnchor& last = g_dense_anchor[key];
      if (last.tag != gf.tag) {
        last.prev_tag = last.tag; std::memcpy(last.prev, last.pos, sizeof last.prev);
        last.tag = gf.tag; std::memcpy(last.pos, a, sizeof a);
      }
      if (last.prev_tag && gf.tag - last.prev_tag <= kDensePairTags) {
        double d2 = 0;
        for (int j = 0; j < 3; ++j) d2 += (a[j] - last.prev[j]) * (a[j] - last.prev[j]);
        drift = float(std::sqrt(d2));
        drift_tag = last.prev_tag;
      }
    }
    if (step >= 0) g_dense.add(d, moving, drift);
    if ((moving || tripped) && g_dense_lines < kDenseLines && (i & 3u) == (gf.tag & 3u)) {
      ++g_dense_lines;
      const auto fs = g_dense_first.find(key);
      log_stats("[hair primary] dense sample: G-buffer frame %llu%s px (%u %u) slot %u key %04llx LOD %d, key first drawn %lld "
          "frames before: %s%s%s; origin %.4f m, ray against view x projection %.3f mrad, card %.3f m, hit %.3f m, band %.3f m, "
          "strand point %.1f mm off its ray (-1: none); hit row %d (slot %d), own row %d of %u; anchor moved %.4f m in the "
          "world since tag %u (-1: no pair), card %.3f m from its anchor; camera step %.3f m",
          (unsigned long long)gf.frame, tripped ? " (watchdog)" : "", d.x, d.y, d.slot, (unsigned long long)(key >> 48),
          d.owner_row < kMaxGrooms ? int(s.lods[d.owner_row]) : -1,
          fs == g_dense_first.end() ? -1ll : (long long)gf.frame - (long long)fs->second, dense_reason_name(d.reason),
          d.reused ? ", reused entry" : "", d.priv ? ", private trace" : "", d.origin_m(), double(d.sin_angle) * 1e3, d.t_card,
          d.t_hit, d.band, d.off_ray >= 0 ? double(d.off_ray) * 1e3 : -1.0, d.hit() ? int(d.hit_row) : -1,
          d.hit_slot == 0xFF ? -1 : int(d.hit_slot), d.owner_row == 0xFF ? -1 : int(d.owner_row), d.rows, drift, drift_tag,
          d.card_to_anchor(), step);
    }
  }
}
void dense_side(const hair_primary::DenseStats::Side& d, char* out, size_t size) {
  using hair_primary::DenseReason;
  auto r = [&](DenseReason x) { return (unsigned long long)d.reason[size_t(x)]; };
  std::snprintf(out, size, "%llu samples: accepted %llu, no strand on the ray %llu, strand of another draw %llu, segment or "
      "pool bound %llu, outside the card band %llu, no point UVs %llu, tips %llu; reused entries %llu; "
      "largest: origin %.4f m, ray against view x projection %.3f mrad, strand point off its ray %.1f mm fresh / %.1f mm "
      "reused, anchor moved %.4f m in the world (%llu pairs), card %.3f m from its anchor",
      (unsigned long long)d.n, r(DenseReason::Accepted), r(DenseReason::Miss), r(DenseReason::Owner), r(DenseReason::Segment),
      r(DenseReason::CardBand), r(DenseReason::NoUv), r(DenseReason::Tips),
      (unsigned long long)d.reused, d.max_origin, double(d.max_sin) * 1e3, double(d.max_off_fresh) * 1e3,
      double(d.max_off_reused) * 1e3, d.max_drift, (unsigned long long)d.drifts, d.max_card_anchor);
}

// g_mu held, outside a render pass, before this publish's header update and SwapGate::publish. Reads the oldest
// slot's counts into the gate, then counts the previous frame's pixel record into it. False: no feedback possible
// (the caller then stops every swap: fail safe).
bool count_feedback(VkCommandBuffer cmd, uint64_t now, uint64_t rec) {
  DeviceData* dd = g_dd;
  CountSlot& s = g_cnt[g_cnt_next];
  if (s.valid && hair_primary::readback_state(s.frame, life_gpu_done(), bvh_rec_submitted(s.rec)) == hair_primary::ReadbackState::Drop) s.valid = false;
  if (s.valid) {
    if (hair_primary::readback_state(s.frame, life_gpu_done(), bvh_rec_submitted(s.rec)) == hair_primary::ReadbackState::Wait) return true;   // still in flight: no new count this frame
    uint32_t total = 0, other_total = 0, other[hair_primary::kMaxGrooms];
    // The judged G-buffer frame (review 2026-11-08 items 3/4): the newest tag window with markers, mapped to the unique
    // G-buffer frame that applied it. Not s.gframe: the count runs in the scene build, which the current-frame header
    // orders before this frame's G-buffer on the GPU, so it reads the previous G-buffer frame's markers and entries
    // (2026-11-08 hair-flash2: every swapped draw was reported as "no card marker"). Coverage, dropouts, traced/hit,
    // strand pixels and the C2 colour samples all come from that one window, attributed with that frame's rows.
    const uint32_t* tc = s.map + hair_primary::kTagCountOffset / 4;
    const uint32_t* cc = s.map + hair_primary::kCardCountOffset / 4;
    uint32_t card_totals[hair_primary::kCountTags] = {};
    hair_primary::count_marker_totals(cc, tc, card_totals, g_seen.accepted_totals);
    std::memcpy(g_seen.tag_totals, card_totals, sizeof card_totals);
    const int jn = hair_primary::newest_counted_tag(card_totals);
    bool ambiguous = false;
    const GbFrame* gf = jn < 0 ? nullptr : hair_primary::unique_frame(g_gb_frames, 16, s.tag - uint32_t(jn), ambiguous);
    const uint32_t j = gf ? uint32_t(jn) : 0u;
    const uint64_t* row_keys = gf ? gf->keys : s.keys;
    g_seen.frame = s.frame;
    g_seen.tag = s.tag;
    g_seen.traced = s.map[hair_primary::kTagTracedOffset / 4 + 2 * j];
    g_seen.hit = s.map[hair_primary::kTagTracedOffset / 4 + 2 * j + 1];
    g_seen.evidence_tag = gf ? gf->tag : 0;
    std::memcpy(g_seen.ray, s.map + hair_primary::kRaySampleOffset / 4, sizeof g_seen.ray);
    g_seen.rebase = hair_primary::rebase_sample(g_seen.ray, gf ? gf->tag : 0u);
    g_rebase.add(g_seen.rebase);
    g_seen.stamp = hair_primary::Stamp::Unread;
    g_seen.stamp_tag = 0;
    if (gf && g_stamp.map) {   // 6d: the build whose points the judged G-buffer traced against the publish it applied
      uint32_t entry[2];
      std::memcpy(entry, static_cast<const uint8_t*>(g_stamp.map) + hair_primary::stamp_entry(gf->tag), sizeof entry);
      g_seen.stamp = hair_primary::build_stamp(entry, gf->tag, uint32_t(gf->pub_frame + 1));
      g_seen.stamp_tag = entry[0];
      ++g_stamp_n[size_t(g_seen.stamp)];
    }
    if (gf) g_mask_holes += s.map[hair_primary::kMaskHoleOffset / 4 + j];
    for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) {
      g_seen.keys[k] = row_keys[k];
      g_seen.lods[k] = s.lods[k];
      g_seen.pixels[k] = s.map[hair_primary::kTagPixelOffset / 4 + j * 64 + k];
      g_seen.refused[k] = s.map[hair_primary::kTagRefusedOffset / 4 + j * 64 + k];
      other[k] = s.map[hair_primary::kOtherOffset / 4 + k];
      g_refused_total += g_seen.refused[k];
      total += s.map[k];
      float t[4];
      std::memcpy(t, s.map + hair_primary::kTransmitOffset / 4 + k * 4, sizeof t);
      // C2: only the judged frame's window (w = j + 1), its groom k mapped with that frame's rows; once per sample.
      if (gf && row_keys[k] && t[3] == float(j + 1)) { g_transmit[row_keys[k]].add(t); ++g_colour_samples; }
      other_total += other[k];
    }
    if (ambiguous) {
      if (cfg_int("rt_stats_period_frames", 0) <= 0 || s.frame > g_stats_start) ++g_ambiguous;
      for (const GbFrame& f : g_gb_frames) if (f.tag == s.tag - uint32_t(jn))
        for (uint64_t key : f.keys) if (key) g_coverage.invalidate(key, f.frame);
    }
    // 5u: the strand count each decision judges. Today's: the accepted pixels inside the card markers (tc). With
    // rt_hair_coverage_union and a count that ran the union request: every accepted pixel of the slot (uc), the bound's
    // own quantity; the upper bound keeps the count inside the markers (CoverageGate::Evidence::inside).
    const uint32_t* uc = s.uni ? s.map + hair_primary::kUnionCountOffset / 4 : nullptr;
    const bool judge_union = g_cfg_union && uc;
    auto judged = [&](uint32_t k) { return judge_union ? uc[j * 64 + k] : tc[j * 64 + k]; };
    if (gf) {
      g_footprint.begin();
      for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) if (gf->draw_keys[k]) {
        const uint64_t key = gf->draw_keys[k];
        const bool was_swapped = (gf->swapped_slots & (1u << k)) != 0;
        const uint32_t card = cc[j * 64 + k], inside = tc[j * 64 + k];
        g_coverage.observe(key, gf->frame, card, judged(k), inside);
        g_switches.measure(key, card, judged(k), was_swapped);
        uint64_t traced = 0;   // the shared trace's hits on this key's groom rows in the judged window (5u H)
        uint32_t rows = 0;
        for (uint32_t r = 0; r < hair_primary::kMaxGrooms; ++r)
          if (row_keys[r] == key) { traced += g_seen.pixels[r]; ++rows; }
        g_footprint.draw(card, inside, traced, uc ? int64_t(uc[j * 64 + k]) : -1, judged(k), was_swapped, rows);
      }
      g_footprint.end_frame();
      g_seen.evidence_frame = gf->frame;
      // 5h dropouts and the frame watchdog (item 19): the swapped draws of the judged frame against their card pixels
      // (same pass, same tag). The trace counts are of that frame's window too: always attributed (item 4).
      bool wd_tripped = false;
      g_watchdog.begin();
      for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) {
        const uint64_t key = gf->draw_keys[k];
        if (!key) continue;
        const uint32_t card = cc[j * 64 + k], acc = tc[j * 64 + k];
        if (gf->swapped_slots & (1u << k)) {
          const auto last = g_last_card.find(key);
          const uint32_t last_card = last == g_last_card.end() ? 0u : last->second;
          const auto why = hair_primary::dropout_reason(card, acc, last_card, g_seen.traced, g_seen.hit, true);
          if (why != hair_primary::Dropout::None) {
            ++g_drop.draws; ++g_drop.why[size_t(why)];
            if (g_drop.firsts < 4) {
              auto& f = g_drop.first[g_drop.firsts++];
              f = {gf->frame, key, card, acc, why};
              for (uint32_t r = 0; r < hair_primary::kMaxGrooms; ++r)
                if (g_seen.keys[r] == key && f.rows++ < 2) f.row_pixels[f.rows - 1] = g_seen.pixels[r];
            }
          }
          g_watchdog.draw(card, judged(k), last_card);   // 5u: the judged count (acc unless union)
        }
        if (card) g_last_card[key] = card;
      }
      if (g_watchdog.judge(gf->frame)) {
        wd_tripped = true;
        ++g_watchdog_period;
        g_switches.watchdog_trip(gf->frame, g_watchdog.accepted, g_watchdog.card, g_watchdog.bound(), g_watchdog.good,
                                 g_watchdog.bad);
        if (g_watchdog_logged++ < 8)
          log("[hair primary] watchdog: G-buffer frame %llu swapped draws wrote %llu strand pixels of %llu card pixels "
              "(bound %.4f): cards for %llu frames; build camera step %.3f m at that frame, %.3f m the frame before "
              "(-1: not kept); applied the publish of build frame %llu (%lld frames before), header camera %.3f m from "
              "build frame %llu's; VP eye %.3f m from the origin at that publish, %.3f m at build frame %llu; sampled "
              "strand ray of that frame on the GPU: rebase origin %.3f m (%.3f m along the ray; -1: no sample; not 0: the "
              "header camera is not the groom points' origin), card distance %.3f m, hit distance %.3f m (-1: miss); groom "
              "points and BLASes on the GPU at that G-buffer: of %s (build stamp %lld, applied publish %llu; frame + 1)",
              (unsigned long long)gf->frame, (unsigned long long)g_watchdog.accepted,
              (unsigned long long)g_watchdog.card, g_watchdog.bound(),
              (unsigned long long)(g_card_fill ? 0 : hair_primary::kWatchdogFrames),   // 6g: the card fill shows the cards
              g_cam_steps.step(gf->frame), g_cam_steps.step(gf->frame - 1), (unsigned long long)gf->pub_frame,
              (long long)gf->frame - (long long)gf->pub_frame, g_cam_steps.between(gf->pub_frame, gf->frame),
              (unsigned long long)gf->frame, eye_of(gf->pub_frame), eye_of(gf->frame), (unsigned long long)gf->frame,
              g_seen.rebase.origin_m, g_seen.rebase.along_m, g_seen.rebase.card_m, g_seen.rebase.hit_m,
              hair_primary::stamp_name(g_seen.stamp), (long long)g_seen.stamp_tag, (unsigned long long)(gf->pub_frame + 1));
      }
      if (s.dense) dense_frame(s, *gf, wd_tripped);   // 6f
    }
    // A counted frame whose strand pixels all carry an adjacent frame's tag: the G-buffer ran against another publish
    // (submission order). No strike from it; logged so a live run shows how often (rate-limited).
    if (other_total && (cfg_int("rt_stats_period_frames", 0) <= 0 || s.frame > g_stats_start)) ++g_adjacent_window;   // counted frames with adjacent-tag strand pixels (the 300-frame line)
    if (!total && other_total && g_misaligned_logged++ % 300 == 0)
      log_stats("[hair primary] counted frame %llu: its %u strand pixels carry an adjacent frame's tag (the G-buffer ran "
          "against another publish): no strike (%llu such frames)", (unsigned long long)s.frame, other_total,
          (unsigned long long)g_misaligned_logged);
    s.valid = false;
  }
  if (!g_last_tag || g_pixels_fresh || g_last_n != g_px_w * g_px_h) return true;
  if (!s.rb && !make_readback(s)) return false;
  if (!g_counts.b && !make(g_counts, hair_primary::kCountBytesAll,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           "hair primary counts"))
    return false;
  // 5u: the union request when rt_hair_coverage_union is on and it builds; else today's (the slot then holds no union).
  const int cnt_v = g_cfg_union && count_pipeline(1) ? 1 : 0;
  if (!cnt_v && !count_pipeline(0)) return false;
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
  dd->vk.CmdFillBuffer(cmd, g_counts.b, 0, VK_WHOLE_SIZE, 0u);
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  const VkDescriptorBufferInfo bi[2] = {{g_pixels.b, 0, VK_WHOLE_SIZE}, {g_counts.b, 0, VK_WHOLE_SIZE}};
  VkWriteDescriptorSet wr[2];
  for (uint32_t i = 0; i < 2; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstBinding = i;
    wr[i].descriptorCount = 1;
    wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr[i].pBufferInfo = bi + i;
  }
  dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_cnt_pipes[cnt_v]);
  dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_cnt_pl, 0, 2, wr);
  // The tag the counted G-buffer frame (this frame's) applied; without one (header order prepass, or no G-buffer pass
  // applied this frame) the newest applied tag, for the diagnostics only: no strand evidence from that count.
  const GbFrame& gf = g_gb_frames[(now + 1) % 16];
  const bool counted = gf.frame == now + 1;
  // rt_hair_groom_primary_current: the G-buffer header is decided at submit (this frame's publish, tag gf.frame, or the
  // record-time one, gf.frame - 1): count from the newer, so both fall in the kCountTags window.
  const bool current = counted && cfg_int("rt_hair_groom_primary_current", 0) != 0 && g_hdr_slots.b;
  const uint32_t tag = current ? (std::max)(gf.tag, uint32_t(gf.frame)) : counted ? gf.tag : g_applied_cmd == uint64_t(cmd) && g_applied_recording == cmd_recording(cmd) ? g_applied_tag : g_last_tag;
  const uint32_t push[4] = {tag, g_last_n, uint32_t(g_header.addr), uint32_t(g_header.addr >> 32)};
  dd->vk.CmdPushConstants(cmd, g_cnt_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
  dd->vk.CmdDispatch(cmd, (g_last_n + 255) / 256, 1, 1);
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0,
                            nullptr);
  const VkBufferCopy c{0, 0, hair_primary::kCountBytesAll};
  dd->vk.CmdCopyBuffer(cmd, g_counts.b, s.rb, 1, &c);
  // 6f rt_hair_dense_sample: the samples the last G-buffer's fragments stored behind the pixel record (device-local),
  // copied behind the counts; its own barriers, nothing recorded with the key off.
  s.dense = g_cfg_dense && g_pixels.size >= VkDeviceSize(g_last_n) * 24 + hair_primary::kDenseBytesAll;
  if (s.dense) {
    VkMemoryBarrier db{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    db.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    db.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &db, 0, nullptr, 0,
                              nullptr);
    const VkBufferCopy dc{VkDeviceSize(g_last_n) * 24, hair_primary::kCountBytesAll, hair_primary::kDenseBytesAll};
    dd->vk.CmdCopyBuffer(cmd, g_pixels.b, s.rb, 1, &dc);
    db.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    db.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &db, 0, nullptr, 0,
                              nullptr);
  }
  s.gframe = counted ? gf.frame : 0;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) {
    s.keys[k] = counted ? gf.keys[k] : g_last_keys[k];
    s.lods[k] = g_last_lods[k];
    s.drew[k] = g_last_keys[k] && g_gate.gbuffer_now.count(g_last_keys[k]) != 0;   // the counted frame's G-buffer
  }
  s.frame = now; s.rec = rec;
  s.tag = tag;
  s.uni = cnt_v == 1;
  s.valid = true;
  g_cnt_next = (g_cnt_next + 1) % kCountSlots;
  // The compute shader's pixel reads before this frame's G-buffer writes them.
  mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr,
                            0, nullptr);
  return true;
}

void release(HpBuf& b) {
  if (!b.b) return;
  fault_untrack(b.addr);
  defer_destroy(BG3_DEFER_BUFFER, uint64_t(b.b));
  defer_destroy(BG3_DEFER_MEMORY, uint64_t(b.m));
  b = HpBuf{};
}

// g_mu held. The header buffer, once per device: its address is baked into every variant.
bool header_ready() {
  if (!g_startup_enabled) return false;
  if (g_header_tried) return g_header.b != VK_NULL_HANDLE;
  g_header_tried = true;
  const auto* info = static_cast<const Bg3DeviceInfo*>(persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  const char* why = !g_dd ? "no device" : !rt_enabled() ? "rt=0" : !hair_groom::lss_enabled() ? "no linear swept spheres"
                  : !info || !(info->features & BG3_DEV_INT64) ? "shaderInt64 not enabled (sharc_device=1 enables it)"
                  : !g_frag_stores ? "fragmentStoresAndAtomics not enabled by the game"
                  : nullptr;
  if (!why && !make(g_header, hair_primary::kHeaderAllocAll, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    "hair primary header"))
    why = "header buffer allocation failed";
  if (why) {
    if (cfg_int("rt_hair_groom_primary", 0) != 0) log("[hair primary] UNAVAILABLE (%s): matched hair keeps its cards", why);
    else log("[hair primary] strand variants not prepared (%s): RTX Hair unavailable in this session", why);
    return false;
  }
  log("[hair primary] header at 0x%llx: static hair pipelines get strand variants", (unsigned long long)g_header.addr);
  if (auto* p = static_cast<hair_primary::Persist*>(persist(hair_primary::kPersistKey, sizeof(hair_primary::Persist))))
    *p = hair_primary::Persist{uint64_t(g_header.addr), 1u, 0u};   // gi/hair_fibre.hlsl (rt_hair_fibre)
  return true;
}

const VkPipelineDepthStencilStateCreateInfo* depth_state(const VkGraphicsPipelineCreateInfo& ci, bool* dynamic_depth) {
  *dynamic_depth = false;
  if (ci.pDynamicState)
    for (uint32_t d = 0; d < ci.pDynamicState->dynamicStateCount; ++d) {
      const VkDynamicState s = ci.pDynamicState->pDynamicStates[d];
      *dynamic_depth |= s == VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE || s == VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE ||
                        s == VK_DYNAMIC_STATE_DEPTH_COMPARE_OP || s == VK_DYNAMIC_STATE_STENCIL_WRITE_MASK ||
                        s == VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE || s == VK_DYNAMIC_STATE_STENCIL_OP;
    }
  return ci.pDepthStencilState;
}

// g_mu held, outside a render pass: writes the header of `st` into `cmd` and makes it the one the counts pair with.
// Earlier fragment reads of the header/pixels before the write; the TLAS build, pool writes and header update (also
// in command buffers submitted earlier on this queue) before the G-buffer pass's fragment shaders.
// The four header regions apply_header writes (the rest of the header buffer is the GPU's), into a slot image.
void header_image(const StagedHeader& st, uint8_t* out) {
  std::memcpy(out, st.bytes, st.n);
  std::memcpy(out + hair_primary::kCameraOffset, st.cam, sizeof st.cam);
  const uint64_t a[2] = {g_crumbs.map ? uint64_t(g_crumbs.addr) : 0u, 0u};
  std::memcpy(out + hair_primary::kCrumbAddrOffset, a, sizeof a);
  std::memcpy(out + hair_primary::kKeyMapOffset, st.key_map, sizeof st.key_map);
  // Record time: every slot's strands count, no slot draws its card (the submit may narrow both, submit_begin).
  const uint32_t masks = hair_primary::slot_masks(hair_primary::kAllSwapSlots, 0u);
  std::memcpy(out + hair_primary::kSlotMaskOffset, &masks, 4);
}

// rt_hair_header_cam_gpu (item 11), g_mu held, in the build's command buffer outside a render pass: the header's build
// camera becomes the position of the camera UBO the build's xfb_to_world converts with on the GPU, by construction
// (the CPU read at record time may predate the game's last UBO write before the submit). prev: the GPU slot holds the
// previous publish's camera, moved to kPrevCameraOffset first. The builds rewrite the groom points in place, so the
// header camera is the last executed build's, like the points every G-buffer pass traces. False: nothing usable was
// recorded (the caller keeps the CPU cameras).
bool header_cam_gpu(VkCommandBuffer cmd, bool prev) {
  DeviceData* dd = g_dd;
  if (!g_cam_gpu.b && !make(g_cam_gpu, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            "hair primary build camera"))
    return false;
  // Earlier readers of the header cameras (strand passes, the core's hair readers) and of the slot before the writes.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  if (prev) {
    const VkBufferCopy c{208, hair_primary::kPrevCameraOffset, 12};
    dd->vk.CmdCopyBuffer(cmd, g_cam_gpu.b, g_header.b, 1, &c);
    mb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr,
                              0, nullptr);
  }
  if (!rt_copy_build_camera(cmd, g_cam_gpu.b)) return false;   // ends with compute write -> transfer read
  const VkBufferCopy c{208, hair_primary::kCameraOffset, 12};
  dd->vk.CmdCopyBuffer(cmd, g_cam_gpu.b, g_header.b, 1, &c);
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0,
                            nullptr);
  return true;
}

// slot >= 0 (rt_hair_groom_primary_current): the header comes from host slot `slot` of g_hdr_slots, written now with
// `st` and possibly upgraded at vkQueueSubmit (hair_primary_submit_begin) to this frame's publish.
void apply_header(VkCommandBuffer cmd, const StagedHeader& st, int slot, uint32_t tag) {
  if (!tag) tag = st.pick.tag;   // rt_hair_primary_frame_tag: the G-buffer frame's own tag (header word 0)
  DeviceData* dd = g_dd;
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr,
                            0, nullptr);
  // the breadcrumb buffer's address (0: none; created on first use, host-visible)
  if (!g_crumbs.b) make(g_crumbs, hair_primary::kCrumbBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "hair primary breadcrumbs", true);
  if (slot >= 0) {
    const VkDeviceSize base = VkDeviceSize(slot) * hair_primary::kHeaderAllocAll;
    header_image(st, static_cast<uint8_t*>(g_hdr_slots.map) + base);
    std::memcpy(static_cast<uint8_t*>(g_hdr_slots.map) + base, &tag, 4);
    VkBufferCopy regions[5] = {{base, 0, hair_primary::kHeaderBytes},   // full size: a submit upgrade may add grooms
                               {base + hair_primary::kCameraOffset, hair_primary::kCameraOffset, sizeof st.cam},
                               {base + hair_primary::kCrumbAddrOffset, hair_primary::kCrumbAddrOffset, 16},
                               {base + hair_primary::kKeyMapOffset, hair_primary::kKeyMapOffset, sizeof st.key_map},
                               {base + hair_primary::kSlotMaskOffset, hair_primary::kSlotMaskOffset, 4}};
    if (st.cam_gpu) regions[1] = regions[4];   // rt_hair_header_cam_gpu: the builds wrote both cameras on the GPU
    dd->vk.CmdCopyBuffer(cmd, g_hdr_slots.b, g_header.b, st.cam_gpu ? 4 : 5, regions);
  } else {
    uint8_t bytes[hair_primary::kHeaderBytes];
    std::memcpy(bytes, st.bytes, st.n);
    std::memcpy(bytes, &tag, 4);
    dd->vk.CmdUpdateBuffer(cmd, g_header.b, 0, st.n, bytes);
    if (!st.cam_gpu)   // rt_hair_header_cam_gpu: the build wrote both cameras on the GPU
      dd->vk.CmdUpdateBuffer(cmd, g_header.b, hair_primary::kCameraOffset, sizeof st.cam, st.cam);   // both cameras
    const uint64_t a[2] = {g_crumbs.map ? uint64_t(g_crumbs.addr) : 0u, 0u};
    dd->vk.CmdUpdateBuffer(cmd, g_header.b, hair_primary::kCrumbAddrOffset, sizeof a, a);
    dd->vk.CmdUpdateBuffer(cmd, g_header.b, hair_primary::kKeyMapOffset, sizeof st.key_map, st.key_map);
    const uint32_t masks = hair_primary::slot_masks(hair_primary::kAllSwapSlots, 0u);
    dd->vk.CmdUpdateBuffer(cmd, g_header.b, hair_primary::kSlotMaskOffset, 4, &masks);
  }
  g_live_slots = st.slots;
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  dd->vk.CmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  if (g_stamp.b && g_stamp.map && slot >= 0) {   // 6d: the last executed build's stamp, as this G-buffer finds it
    const uint32_t at = hair_primary::stamp_entry(tag), words[2] = {0u, tag};
    std::memcpy(static_cast<uint8_t*>(g_stamp.map) + at, words, sizeof words);
    VkMemoryBarrier sb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    sb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &sb, 0, nullptr, 0, nullptr);
    const VkBufferCopy c{0, at, 4};
    dd->vk.CmdCopyBuffer(cmd, g_stamp.b, g_stamp.b, 1, &c);
  }
  g_last_tag = tag;
  // The tag this recording's strand passes seal with: the count recorded later in the same recording (the scene build
  // follows the G-buffer pass) counts that frame's entries, not a newer publish applied by another recording.
  g_applied_cmd = uint64_t(cmd);
  g_applied_recording = cmd_recording(cmd);
  g_applied_tag = tag;
  g_last_n = st.pixels;
  std::memcpy(g_last_keys, st.keys, sizeof g_last_keys);
  std::memcpy(g_last_lods, st.lods, sizeof g_last_lods);
  g_live_keys.clear();
  for (uint64_t k : st.keys)
    if (k) g_live_keys.insert(k);
}

// The tracker's creation id of a game image (0: unknown). Not under g_mu (the tracker lock is taken first elsewhere).
uint64_t image_identity(uint64_t image) {
  if (!image) return 0;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  const auto it = tracker().images.find(VkImage(image));
  return it == tracker().images.end() ? 0 : it->second.id;
}

// Detect the boundary before any camera prepass can trace an old scene's header.
// The publish path also calls this for the first image / missing prepass metadata.
void observe_depth(uint64_t image, uint64_t now, uint64_t identity) {
  if (!g_load.observe(image, now, identity)) return;
  g_watchdog = hair_primary::Watchdog{};
  g_footprint = hair_primary::FootprintStats{};
  g_switches = hair_primary::SwapTransitions{};
  g_gate.clear();
  g_coverage.clear(); g_last_swap_frame.clear();
  g_depth_safe_keys.clear();
  g_depth = {}; g_depth_orphan_count = 0;

  for (auto& gf : g_gb_frames) gf = GbFrame{};

  for (auto& st : g_staged) st.pick = {};
  g_live_keys.clear();
  g_slots = {}; g_live_slots = {};
  g_last_tag = 0;
  g_pixels_fresh = true;
  g_transmit.clear();
  g_last_card.clear();                       // 5i (review 4): no dropout verdict against another scene's draws
  g_first_pub.clear(); g_warm.logged = false;   // 5o: this load's warm-up line at its first strand swap
  for (CountSlot& c : g_cnt) c.valid = false;   // counts of the old pixel record are no evidence
  log("[hair primary] level load (first or new G-buffer images): strand swaps paused for %llu frames, groom state reset",
      (unsigned long long)hair_primary::kLoadQuietFrames);
}

// The breadcrumbs (common/hair_primary.h kCrumbAddrOffset): violations by site and the last noted addresses.
void crumbs_log(const char* prefix) {
  if (!g_crumbs.map) return;
  const auto* c = static_cast<const uint32_t*>(g_crumbs.map);
  char v[512] = "";
  int at = 0;
  for (uint32_t i = 0; i < 2 * hair_primary::kCrumbSites && at < 480; ++i) {
    const uint32_t* r = c + i * 4;
    if (r[3]) at += std::snprintf(v + at, sizeof v - at, " %s%u:0x%llx", r[3] == 1 ? "violation site " : "note ", i,
                                  (unsigned long long)((uint64_t(r[2]) << 32) | r[1]));
  }
  log("%s hair primary breadcrumbs:%s", prefix, at ? v : " none");
}

bool stats_due(uint64_t now) {
  if (cfg_int("rt_stats_period_frames", 0) <= 0) return now % 300 == 0;
  return now > g_stats_start && (now - g_stats_start) % stats_period(300) == 0;
}
void coverage_log(uint64_t now) {
  if (!stats_due(now)) return;
  // 5t: the strand/card switches of the period. log(), not log_stats: the lean layer prints it too (one line per period,
  // only while hair draws were seen).
  if (g_switches.frames || g_switches.switches() || g_switches.wd_trips) {
    char sw[2000];
    g_switches.format(sw);
    log("[hair primary] switches (last %u frames):%s", stats_period(300), sw);
  }
  g_switches.reset_period();
  // 5u: the measurements by footprint (lean too, one line per period while any draw was measured).
  if (g_footprint.frames) {
    char fp[1600];
    g_footprint.format(fp);
    log("[hair primary] coverage by footprint (last %u frames, judged %s):%s", stats_period(300),
        g_cfg_union ? "union (rt_hair_coverage_union)" : "inside the card markers", fp);
  }
  g_footprint.reset_period();
  char per[1200] = {};
  size_t at = 0;
  uint32_t shown = 0, probed = 0, swapped = 0, below = 0, below_own = 0;
  double minimum = 0.0;
  bool first = true;
  for (const auto& [key, period] : g_coverage_period) {
    probed += period.probed;
    swapped += period.swapped != 0;
    if (period.decisions)
      log("[hair primary] coverage used %016llx: ratio %.6f..%.6f; %u swap decisions",
          (unsigned long long)key, period.used_min, period.used_max, period.decisions);
    const auto it = g_coverage.latest.find(key);
    const hair_primary::CoverageGate::Evidence e = it == g_coverage.latest.end() ? hair_primary::CoverageGate::Evidence{} : it->second;
    const double ratio = e.ratio();
    if (first || ratio < minimum) minimum = ratio;
    first = false;
    below += !e.card || ratio < hair_primary::kCoverMin || ratio > hair_primary::kCoverMax;
    // rt_hair_coverage_footprint: the lower bound at this measurement's own footprint (the strict one is printed below).
    const double own = (std::min)(hair_primary::kCoverMin, hair_primary::kCoverRatioMin - hair_primary::cover_sigma(e.strand, e.card));
    const double own_up = (std::max)(hair_primary::kCoverMax, hair_primary::kCoverRatioMax + hair_primary::cover_sigma(e.strand, e.card));
    below_own += !e.card || ratio < own || ratio > own_up;
    if (shown++ < 12) at += std::snprintf(per + at, sizeof per - at, " %04llx:%u/%u=%.3f,%u,used=%.3f..%.3f,own=%.3f",
        (unsigned long long)(key >> 48), unsigned((std::min)(e.strand, uint64_t(UINT32_MAX))),
        unsigned((std::min)(e.card, uint64_t(UINT32_MAX))), ratio, period.swapped, period.used_min, period.used_max, own);
  }
  log_stats("[hair primary] coverage (last %u frames): %u draws probed, %u swapped; per draw (key prefix: newest accepted strand pixels/unique card pixels = ratio, swapped frames, used ratio min..max):%s; ratio min %.3f, bound %.3f, upper %.3f, draws outside bounds %u (outside their own footprint bounds %u); "
      "ambiguous tags %u; native depth fallbacks %llu; draws without a slot %u",
      stats_period(300), probed, swapped, per, minimum, hair_primary::kCoverMin, hair_primary::kCoverMax, below, below_own, g_ambiguous, (unsigned long long)g_fallbacks, unsigned(g_without_slot.size()));
  g_ambiguous = 0; g_fallbacks = 0; g_without_slot.clear();
  g_coverage_period.clear();
  if (logging::stats_enabled(cfg_int)) crumbs_log("[hair primary]");
}

}  // namespace

void hair_primary_prev_camera_stats(uint64_t* publishes, uint64_t* no_prev_gap, uint64_t* no_prev_gpu) {
  std::lock_guard<std::mutex> lock(g_mu);
  *publishes = g_prev_cam.pubs; *no_prev_gap = g_prev_cam.gap; *no_prev_gpu = g_prev_cam.gpu;
  g_prev_cam = {};
}

void hair_primary_stats_reset() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_stats_start = frame_index();
  g_switches.reset_period();
  g_st = Stats{}; g_failures.reset(); g_depth_orphan_count = 0; g_coverage_period.clear();
  g_part = hair_primary::Participation{}; g_refusals.reset(); g_held_swaps = 0; g_swap_resumes = 0; std::fill_n(g_current_picks, 4, uint64_t(0));
  g_ambiguous = 0; g_fallbacks = 0; g_without_slot.clear();
  g_adjacent_window = 0; g_transmit_rows = 0; g_ms_sum = 0; g_ms_frames = 0;
  g_header_mismatch = g_card_upgrades = g_card_slots = g_mask_holes = g_watchdog_period = g_colour_samples = 0;
  std::fill_n(g_slot_frame, kTimeSlots, uint64_t(0));
  // Pending coverage is functional swap evidence; retain it but exclude its
  // diagnostic attribution from the fresh window.
}

uint64_t hair_primary_draw_key(const CmdState* s, const DrawArgs& args) {
  // The index range: the same in the prepass, G-buffer and velocity draws of one hair mesh (the pipeline and the
  // vertex streams differ between passes).
  uint64_t k = 0xcbf29ce484222325ull;
  for (uint64_t v : {uint64_t(s->ib), uint64_t(s->ib_offset), uint64_t(args.first), uint64_t(args.count),
                     uint64_t(int64_t(args.vertex_offset))})
    k = (k ^ v) * 0x100000001b3ull;
  return k;
}

void hair_primary_device_created(DeviceData* dd, const VkDeviceCreateInfo* ci) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_dd) return;
  g_dd = dd;
  // 5m: no startup latch. The header and the strand variants are prepared on every capable device (header_ready), so
  // RTX Hair turns on and off live; the cost of preparing them is logged with the first variants.
  g_startup_enabled = true;
  g_live = hair_live::Toggle{};
  g_live_on = false; g_live_ever = false;
  g_live_frame = ~0ull;
  bool stores = ci->pEnabledFeatures && ci->pEnabledFeatures->fragmentStoresAndAtomics;
  for (auto p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
      stores |= reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p)->features.fragmentStoresAndAtomics != VK_FALSE;
  g_frag_stores = stores;
  VkPhysicalDeviceProperties props{};
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
  g_ts_period = props.limits.timestampPeriod > 0 ? props.limits.timestampPeriod : 1.0f;
}

void hair_primary_device_destroyed(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (dd != g_dd) return;
  for (auto& [game, v] : g_variant) dd->vk.DestroyPipeline(dd->device, v, nullptr);
  g_variant.clear();
  for (auto& [game, v] : g_draw_variants)
    for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) {
      if (v.strand[k]) dd->vk.DestroyPipeline(dd->device, v.strand[k], nullptr);
      if (v.probe[k]) dd->vk.DestroyPipeline(dd->device, v.probe[k], nullptr);
    }
  g_draw_variants.clear();
  for (auto& [game, v] : g_fallback) dd->vk.DestroyPipeline(dd->device, v, nullptr);
  g_fallback.clear();
  g_coverage.clear(); g_last_swap_frame.clear();
  g_depth_safe_keys.clear();
  g_depth = {}; g_depth_orphan_count = 0;
  g_coverage_period.clear();
  for (auto& st : g_staged) st = StagedHeader{};
  g_staged_next = 0;
  g_live_keys.clear();
  g_slots = {}; g_live_slots = {}; g_without_slot.clear();
  g_gate = hair_primary::SwapGate{};
  g_load = hair_primary::LoadGuard{};
  g_first_pub.clear(); g_warm = Warm{};
  g_order = false;
  g_labels = hair_primary::HeaderLabels{};
  g_published = 0;
  g_pass_of.clear();
  g_mods.clear();
  g_inst_mods.clear();
  g_hair_pipes.clear();
  for (auto& [game, c] : g_cascade_clone) dd->vk.DestroyPipeline(dd->device, c, nullptr);
  g_cascade_clone.clear();
  g_pub_keys.clear();
  g_pub_frame = 0;
  g_part = hair_primary::Participation{}; g_refusals.reset(); g_held_swaps = 0; g_swap_resumes = 0; std::fill_n(g_current_picks, 4, uint64_t(0));
  for (CountSlot& s : g_cnt) {
    if (s.rb) dd->vk.DestroyBuffer(dd->device, s.rb, nullptr);
    if (s.rb_mem) dd->vk.FreeMemory(dd->device, s.rb_mem, nullptr);
    s = CountSlot{};
  }
  if (g_counts.b) dd->vk.DestroyBuffer(dd->device, g_counts.b, nullptr);
  if (g_counts.m) dd->vk.FreeMemory(dd->device, g_counts.m, nullptr);
  g_counts = HpBuf{};
  for (VkPipeline& p : g_cnt_pipes) {
    if (p) dd->vk.DestroyPipeline(dd->device, p, nullptr);
    p = VK_NULL_HANDLE;
  }
  for (uint64_t& g : g_cnt_gens) g = 0;
  if (g_cnt_pl) dd->vk.DestroyPipelineLayout(dd->device, g_cnt_pl, nullptr);
  if (g_cnt_dsl) dd->vk.DestroyDescriptorSetLayout(dd->device, g_cnt_dsl, nullptr);
  g_cnt_pl = VK_NULL_HANDLE;
  g_cnt_dsl = VK_NULL_HANDLE;
  g_last_tag = 0;
  if (g_qp) dd->vk.DestroyQueryPool(dd->device, g_qp, nullptr);
  g_qp = VK_NULL_HANDLE;
  for (HpBuf* b : {&g_header, &g_pixels, &g_crumbs, &g_hdr_slots, &g_cam_gpu, &g_stamp}) {
    if (b->b) dd->vk.DestroyBuffer(dd->device, b->b, nullptr);
    if (b->m) dd->vk.FreeMemory(dd->device, b->m, nullptr);
    *b = HpBuf{};
  }
  g_cam_gpu_pub = 0;
  g_dd = nullptr;
  g_startup_enabled = false;
  g_live = hair_live::Toggle{};
  g_live_on = false; g_live_ever = false;
  g_header_tried = false;
}

void hair_primary_note_module(VkShaderModule m, uint64_t hash) {
  const bool ours = hash == hair_primary::kDeferredStaticFs || hash == hair_primary::kDepthStaticFs ||
                    hash == hair_primary::kVelocityStaticFs || hash == hair_primary::kShadowStaticFs;
  std::lock_guard<std::mutex> lock(g_mu);
  // A reused handle must never keep a hair hash (it would give a non-hair pipeline a strand variant).
  if (ours) g_mods[m] = hash;
  else g_mods.erase(m);
  if (hash == hair_primary::kDeferredInstancedFs) g_inst_mods.insert(m);
  else g_inst_mods.erase(m);
}

void hair_primary_forget_module(VkShaderModule m) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_mods.erase(m);
  g_inst_mods.erase(m);
}

// Review 2026-11-08 round 3 M5: the clones are created outside g_mu (up to 33 pipelines per static hair G-buffer pipeline
// stalled every draw hook and publish behind the driver's compiler), then inserted under it. The game cannot destroy
// out[i] before this hook returns, so the handles stay valid in between. The generic G-buffer variant (no draw slot)
// was never bound (G-buffer draws bind strand[slot]): not created; g_variant holds VK_NULL_HANDLE for it.
void hair_primary_note_pipelines(VkDevice device, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo* cis,
                                 const VkPipeline* out) {
  struct Plan { uint32_t i; int fs; uint64_t hash; };
  std::vector<Plan> plans;
  uint64_t header_addr = 0;
  const VkDeviceFuncs* vkp = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if ((g_mods.empty() && g_inst_mods.empty()) || !g_dd || device != g_dd->device) return;
    vkp = &g_dd->vk;
    for (uint32_t i = 0; i < n; ++i) {
      const VkGraphicsPipelineCreateInfo& ci = cis[i];
      int fs = -1;
      uint64_t hash = 0;
      bool instanced = false;
      for (uint32_t k = 0; k < ci.stageCount; ++k)
        if (ci.pStages[k].stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
          const auto it = g_mods.find(ci.pStages[k].module);
          if (it != g_mods.end()) fs = int(k), hash = it->second;
          instanced |= g_inst_mods.count(ci.pStages[k].module) != 0;
        }
      // M5 participation: every hair pipeline, whether or not it gets a variant below.
      if (out[i] && (fs >= 0 || instanced) && hash != hair_primary::kShadowStaticFs) g_pipe_filter.add(uint64_t(out[i]));
      if (out[i] && (fs >= 0 || instanced) && hash != hair_primary::kShadowStaticFs)
        g_hair_pipes[out[i]] = instanced ? 4u + hair_primary::Participation::kGbuffer
                             : hash == hair_primary::kDeferredStaticFs ? uint32_t(hair_primary::Participation::kGbuffer)
                             : hash == hair_primary::kVelocityStaticFs ? uint32_t(hair_primary::Participation::kVelocity)
                                                                       : uint32_t(hair_primary::Participation::kPrepass);
      if (fs < 0 || !out[i] || !header_ready()) continue;
      plans.push_back({i, fs, hash});
    }
    header_addr = g_header.addr;
  }
  if (plans.empty()) return;
  const VkDeviceFuncs& vk = *vkp;
  struct Made {
    VkPipeline game = VK_NULL_HANDLE, cascade = VK_NULL_HANDLE, fallback = VK_NULL_HANDLE, variant = VK_NULL_HANDLE;
    Pass pass = Pass::Prepass;
    bool has_variant = false, has_draw = false;
    DrawVariants draw;
  };
  std::vector<Made> made;
  uint64_t refused = 0;
  std::vector<std::string> notes;   // logged under the lock (rate limits are shared)
  const auto t_begin = std::chrono::steady_clock::now();   // 5m: the always-on startup cost of the strand variants
  for (const Plan& pl : plans) {
    const VkGraphicsPipelineCreateInfo& ci = cis[pl.i];
    const int fs = pl.fs;
    const uint64_t hash = pl.hash;
    Made m;
    m.game = out[pl.i];
    if (hash == hair_primary::kShadowStaticFs) {   // M2.2: the cascade clone, depth writes off, everything else native
      bool lib = false, dyn = false;
      for (auto p = static_cast<const VkBaseInStructure*>(ci.pNext); p; p = p->pNext)
        lib |= p->sType == VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR ||
               p->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
      const VkPipelineDepthStencilStateCreateInfo* sds = depth_state(ci, &dyn);
      if (lib || dyn || !sds) continue;
      VkPipelineDepthStencilStateCreateInfo nd = *sds;
      nd.depthWriteEnable = VK_FALSE;
      VkGraphicsPipelineCreateInfo cc = ci;
      cc.pDepthStencilState = &nd;
      cc.flags &= ~VkPipelineCreateFlags(VK_PIPELINE_CREATE_DERIVATIVE_BIT);
      cc.basePipelineHandle = VK_NULL_HANDLE; cc.basePipelineIndex = -1;
      VkPipeline clone = VK_NULL_HANDLE;
      if (vk.CreateGraphicsPipelines(device, cache, 1, &cc, nullptr, &clone) == VK_SUCCESS && clone) {
        m.cascade = clone;
        made.push_back(m);
      }
      continue;
    }
    const Pass pass = hash == hair_primary::kDeferredStaticFs ? Pass::Gbuffer
                    : hash == hair_primary::kVelocityStaticFs ? Pass::Velocity : Pass::Prepass;
    m.pass = pass;
    bool library = false, dynamic_depth = false;
    for (auto p = static_cast<const VkBaseInStructure*>(ci.pNext); p; p = p->pNext)
      library |= p->sType == VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR ||
                 p->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
    const VkPipelineDepthStencilStateCreateInfo* ds = depth_state(ci, &dynamic_depth);
    if (pass == Pass::Gbuffer && !library && ds) {
      VkPipelineDepthStencilStateCreateInfo restore = *ds;
      restore.depthTestEnable = VK_TRUE; restore.depthWriteEnable = VK_TRUE;
      restore.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
      std::vector<VkDynamicState> states;
      VkPipelineDynamicStateCreateInfo dynamic{};
      if (ci.pDynamicState) {
        dynamic = *ci.pDynamicState;
        for (uint32_t d = 0; d < dynamic.dynamicStateCount; ++d) {
          const VkDynamicState state = dynamic.pDynamicStates[d];
          if (state != VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE && state != VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE &&
              state != VK_DYNAMIC_STATE_DEPTH_COMPARE_OP) states.push_back(state);
        }
        dynamic.dynamicStateCount = uint32_t(states.size()); dynamic.pDynamicStates = states.data();
      }
      VkGraphicsPipelineCreateInfo native = ci;
      native.pDepthStencilState = &restore;
      native.pDynamicState = ci.pDynamicState ? &dynamic : nullptr;
      native.flags &= ~VkPipelineCreateFlags(VK_PIPELINE_CREATE_DERIVATIVE_BIT);
      native.basePipelineHandle = VK_NULL_HANDLE; native.basePipelineIndex = -1;
      VkPipeline fallback = VK_NULL_HANDLE;
      if (vk.CreateGraphicsPipelines(device, cache, 1, &native, nullptr, &fallback) == VK_SUCCESS) m.fallback = fallback;
    }
    if (library || dynamic_depth || !ds) {
      ++refused;
      char t[200];
      std::snprintf(t, sizeof t, "[hair primary] FS %016llx pipeline refused (%s): its draws keep the cards", (unsigned long long)hash,
                    library ? "pipeline library" : dynamic_depth ? "dynamic depth state" : "no depth state");
      notes.push_back(t);
      if (m.fallback) made.push_back(m);
      continue;
    }
    std::vector<VkPipelineShaderStageCreateInfo> stages(ci.pStages, ci.pStages + ci.stageCount);
    VkShaderModule mod = VK_NULL_HANDLE;
    // M3.2 rt_hair_groom_coverage: the two-ray pair is compiled in when the key is on at pipeline creation (header flag
    // kFlagCoverage then switches it live); the velocity FS has no constant 1042, Vulkan ignores the entry there.
    const uint32_t coverage = cfg_int("rt_hair_groom_coverage", 0) != 0 ? 1u : 0u;
    // 5g rt_hair_macro_normal / rt_hair_tangent_world: always compiled in since 5m (a header load and two uniform
    // branches per strand fragment, shaders/rt/hair_primary_gbuffer.hlsl); header flags kFlagMacroNormal /
    // kFlagTangentWorld switch each one live, and with both flags off the output is that of constant 0.
    const uint32_t shading_space = 1u;
    // 5u rt_hair_groom_nearest: the candidate-filtered nearest-strand trace, compiled in when the key is on at pipeline
    // creation (header flag kFlagNearest then switches it live; the velocity FS has no constant 1047).
    const uint32_t nearest = cfg_int("rt_hair_groom_nearest", 0) != 0 ? 1u : 0u;
    // 6f rt_hair_dense_sample: constant 1052 of the per-slot G-buffer variants, added only when the key is on at
    // pipeline creation (key off: the specialization as before).
    const uint32_t dense = pass == Pass::Gbuffer && cfg_int("rt_hair_dense_sample", 0) != 0 ? 1u : 0u;
    // 6g rt_hair_card_fill: constant 1053 of the per-slot strand pipelines (not the masked probes), added only when the
    // key is on at pipeline creation (header flag kFlagCardFill then switches it live; key off: the specialization as before).
    const uint32_t fill = pass == Pass::Gbuffer && cfg_int("rt_hair_card_fill", 0) != 0 ? 1u : 0u;
    // 5y rt_hair_mv_card: the card-motion path of the velocity variant, compiled in when the key is on at pipeline
    // creation (header flags kFlagMvCardFallback / kFlagMvCardAll then switch it live); off: the specialization as before.
    const uint32_t mv_card = pass == Pass::Velocity && cfg_int("rt_hair_mv_card", 0) != 0 ? 1u : 0u;
    const uint32_t spec_words[6] = {uint32_t(header_addr), uint32_t(header_addr >> 32), coverage, shading_space, nearest, mv_card};
    VkSpecializationMapEntry spec_map[6] = {{hair_primary::kSpecRecordLo, 0, 4}, {hair_primary::kSpecRecordHi, 4, 4},
                                            {shader_variant::kSpecIdHairGroomCoverage, 8, 4},
                                            {shader_variant::kSpecIdHairShadingSpace, 12, 4},
                                            {shader_variant::kSpecIdHairGroomNearest, 16, 4},
                                            {shader_variant::kSpecIdHairMvCard, 20, 4}};
    uint32_t spec_n = nearest ? 5u : 4u;
    if (mv_card) spec_map[spec_n++] = {shader_variant::kSpecIdHairMvCard, 20, 4};
    const VkSpecializationInfo spec{spec_n, spec_map, sizeof spec_words, spec_words};
    VkPipelineDepthStencilStateCreateInfo dsv = *ds;
    if (pass != Pass::Prepass) {
      Bg3ShaderBlob blob{};
      const char* file = pass == Pass::Gbuffer ? "rt/hair_primary_gbuffer.hlsl" : "rt/hair_primary_velocity.hlsl";
      if (!shader_get(file, "main", "ps_6_6", &blob)) {
        ++refused;
        notes.push_back("[hair primary] strand fragment shader unavailable: hair keeps its cards");
        if (m.fallback) made.push_back(m);
        continue;
      }
      VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      mci.codeSize = blob.size_bytes;
      mci.pCode = blob.code;
      if (vk.CreateShaderModule(device, &mci, nullptr, &mod) != VK_SUCCESS) { if (m.fallback) made.push_back(m); continue; }
      stages[size_t(fs)].module = mod;
      stages[size_t(fs)].pName = "main";
      stages[size_t(fs)].pSpecializationInfo = &spec;
      // G-buffer: strands write their own depth on top of whatever the prepass left (no card depth there).
      // Velocity: the strand depth the G-buffer wrote passes, nearer geometry does not; no writes.
      char t[320];
      std::snprintf(t, sizeof t, "[hair primary] %s hair pipeline: native depth test %u write %u compare %d -> strands test 1 "
                    "write %u compare GREATER_OR_EQUAL; nearest-strand trace compiled in (specialization 1047) %u; dense "
                    "samples (specialization 1052) %u; card fill (specialization 1053) %u",
                    pass == Pass::Gbuffer ? "G-buffer" : "velocity", ds->depthTestEnable, ds->depthWriteEnable,
                    int(ds->depthCompareOp), unsigned(pass == Pass::Gbuffer), pass == Pass::Gbuffer ? nearest : 0u, dense, fill);
      if (pass == Pass::Gbuffer) g_nearest_compiled = int(nearest);
      if (pass == Pass::Gbuffer) g_fill_compiled = int(fill);
      notes.push_back(std::string("pipe:") + t);
      dsv.depthTestEnable = VK_TRUE;
      dsv.depthWriteEnable = pass == Pass::Gbuffer ? VK_TRUE : VK_FALSE;
      dsv.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    } else {
      dsv.depthWriteEnable = VK_FALSE;
    }
    {   // 5n (rr_hair_strand_aa): which hair passes write the stencil tags the game's dither resolve (7401d785) reads.
      static int stencil_notes = 0;
      if (stencil_notes < 6) {
        ++stencil_notes;
        char t[200];
        std::snprintf(t, sizeof t, "[hair primary] %s hair pipeline stencil: test %u, write mask 0x%02x, pass op %d, reference 0x%02x",
                      pass == Pass::Gbuffer ? "G-buffer" : pass == Pass::Velocity ? "velocity" : "prepass", ds->stencilTestEnable,
                      ds->front.writeMask, int(ds->front.passOp), ds->front.reference);
        notes.push_back(t);
      }
    }
    VkGraphicsPipelineCreateInfo vci = ci;
    vci.pStages = stages.data();
    vci.pDepthStencilState = &dsv;
    vci.flags &= ~VkPipelineCreateFlags(VK_PIPELINE_CREATE_DERIVATIVE_BIT);
    vci.basePipelineHandle = VK_NULL_HANDLE;
    vci.basePipelineIndex = -1;
    if (pass != Pass::Gbuffer) {
      VkPipeline v = VK_NULL_HANDLE;
      const VkResult r = vk.CreateGraphicsPipelines(device, cache, 1, &vci, nullptr, &v);
      if (mod) vk.DestroyShaderModule(device, mod, nullptr);
      if (r != VK_SUCCESS || !v) {
        ++refused;
        char t[160];
        std::snprintf(t, sizeof t, "[hair primary] variant pipeline creation failed (%d): hair keeps its cards", int(r));
        notes.push_back(t);
        continue;
      }
      m.variant = v;
      m.has_variant = true;
      made.push_back(m);
      continue;
    }
    // G-buffer: one strand and one masked-probe pipeline per draw slot (specialization 1102), no generic variant.
    VkPipelineColorBlendStateCreateInfo blend{};
    std::vector<VkPipelineColorBlendAttachmentState> attachments;
    if (ci.pColorBlendState) {
      blend = *ci.pColorBlendState;
      if (blend.attachmentCount) attachments.assign(blend.pAttachments, blend.pAttachments + blend.attachmentCount);
      for (auto& attachment : attachments) attachment.colorWriteMask = 0;
      blend.pAttachments = attachments.data();
    }
    for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) {
      dsv.front = ds->front; dsv.back = ds->back;
      // Pipeline specialization supplies exact draw-key attribution before tracing,
      // without changing the game's descriptor or push-constant layouts.
      const uint32_t words[8] = {spec_words[0], spec_words[1], k, coverage, shading_space, nearest, dense, fill};
      VkSpecializationMapEntry map[8] = {spec_map[0], spec_map[1], {hair_primary::kSpecDrawSlot, 8, 4},
                                         {shader_variant::kSpecIdHairGroomCoverage, 12, 4},
                                         {shader_variant::kSpecIdHairShadingSpace, 16, 4},
                                         {shader_variant::kSpecIdHairGroomNearest, 20, 4}};
      uint32_t map_n = nearest ? 6u : 5u;
      if (dense) map[map_n++] = {shader_variant::kSpecIdHairDenseSample, 24, 4};
      const VkSpecializationInfo draw_spec{map_n, map, dense ? sizeof words - 4 : sizeof words - 8, words};
      // 6g: the strand pipeline alone gets the card fill (the probe writes neither colour nor depth: it only counts).
      if (fill) map[map_n] = {shader_variant::kSpecIdHairCardFill, 28, 4};
      const VkSpecializationInfo fill_spec{map_n + 1u, map, sizeof words, words};
      stages[size_t(fs)].pSpecializationInfo = fill ? &fill_spec : &draw_spec;
      dsv.depthWriteEnable = VK_TRUE;
      vci.pColorBlendState = ci.pColorBlendState;
      vk.CreateGraphicsPipelines(device, cache, 1, &vci, nullptr, &m.draw.strand[k]);
      stages[size_t(fs)].pSpecializationInfo = &draw_spec;
      dsv.depthWriteEnable = VK_FALSE;
      dsv.front.writeMask = dsv.back.writeMask = 0;
      vci.pColorBlendState = ci.pColorBlendState ? &blend : nullptr;
      vk.CreateGraphicsPipelines(device, cache, 1, &vci, nullptr, &m.draw.probe[k]);
    }
    if (mod) vk.DestroyShaderModule(device, mod, nullptr);
    m.has_variant = m.has_draw = true;
    made.push_back(m);
  }
  const uint64_t spent_us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - t_begin).count());
  uint64_t pipes = 0;
  for (const Made& m : made) {
    pipes += (m.cascade != VK_NULL_HANDLE) + (m.fallback != VK_NULL_HANDLE) + (m.variant != VK_NULL_HANDLE);
    for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) pipes += (m.draw.strand[k] != VK_NULL_HANDLE) + (m.draw.probe[k] != VK_NULL_HANDLE);
  }
  std::lock_guard<std::mutex> lock(g_mu);
  for (const std::string& t : notes) {
    const bool pipe = t.rfind("pipe:", 0) == 0;
    if (pipe ? g_logged_pipes++ < 6 : g_failures.refused < 8) log("%s", t.c_str() + (pipe ? 5 : 0));
  }
  {
    // Powers of two of the running pipeline count: a few lines per session with the total CPU time so far.
    const uint64_t before = g_variant_pipes;
    g_variant_pipes += pipes;
    g_variant_us += spent_us;
    for (uint64_t mark = 64; mark <= (1ull << 20); mark <<= 1)
      if (before < mark && g_variant_pipes >= mark) {
        log("[hair primary] strand variants: %llu pipelines in %.1f ms of pipeline creation so far (prepared whether or not "
            "RTX Hair is on)", (unsigned long long)g_variant_pipes, double(g_variant_us) / 1000.0);
        break;
      }
  }
  g_failures.refused += refused;
  auto replace = [](std::unordered_map<VkPipeline, VkPipeline>& map, VkPipeline game, VkPipeline v) {
    auto old = map.find(game);
    if (old != map.end() && old->second) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(old->second));
    map[game] = v;
  };
  for (const Made& m : made) {
    if (m.cascade || m.has_variant) g_pipe_filter.add(uint64_t(m.game));
    if (m.cascade) replace(g_cascade_clone, m.game, m.cascade);
    if (m.fallback) replace(g_fallback, m.game, m.fallback);
    if (!m.has_variant) continue;
    replace(g_variant, m.game, m.variant);   // VK_NULL_HANDLE for the G-buffer: the draw variants below are bound
    g_pass_of[m.game] = m.pass;
    if (m.has_draw) {
      auto old = g_draw_variants.find(m.game);
      if (old != g_draw_variants.end())
        for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) {
          if (old->second.strand[k]) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(old->second.strand[k]));
          if (old->second.probe[k]) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(old->second.probe[k]));
        }
      g_draw_variants[m.game] = m.draw;
    }
  }
}

void hair_primary_forget_pipeline(VkPipeline p) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_hair_pipes.erase(p);
  auto cc = g_cascade_clone.find(p);
  if (cc != g_cascade_clone.end()) { defer_destroy(BG3_DEFER_PIPELINE, uint64_t(cc->second)); g_cascade_clone.erase(cc); }
  auto variants = g_draw_variants.find(p);
  if (variants != g_draw_variants.end()) {
    for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) {
      if (variants->second.strand[k]) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(variants->second.strand[k]));
      if (variants->second.probe[k]) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(variants->second.probe[k]));
    }
    g_draw_variants.erase(variants);
  }
  auto fallback = g_fallback.find(p);
  if (fallback != g_fallback.end()) {
    defer_destroy(BG3_DEFER_PIPELINE, uint64_t(fallback->second)); g_fallback.erase(fallback);
  }
  auto it = g_variant.find(p);
  if (it == g_variant.end()) return;
  g_pass_of.erase(p);
  if (it->second) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(it->second));
  g_variant.erase(it);
}

uint32_t hair_primary_transmit(uint64_t key) {
  // Word 2 serves both rt_hair_groom_shadow_t (shadow rays) and rt_hair_groom_ao_t (AO/GI rays, M2.3).
  if (!key || (cfg_int("rt_hair_groom_shadow_t", 0) == 0 && cfg_int("rt_hair_groom_ao_t", 0) == 0)) return 0u;
  std::lock_guard<std::mutex> lock(g_mu);
  const auto it = g_transmit.find(key);
  const uint32_t w = it == g_transmit.end() ? 0u : it->second.packed();
  if (w) ++g_transmit_rows;
  return w;
}

HairPrimarySubmit hair_primary_submit_begin(const std::vector<VkCommandBuffer>& cmds) {
  HairPrimarySubmit recordings{};
  // Nothing was ever staged before strands first turned on (a session with RTX Hair off pays no submit work).
  if (!g_startup_enabled || !g_live_ever.load(std::memory_order_acquire)) return recordings;
  std::lock_guard<std::mutex> lock(g_mu);
  for (VkCommandBuffer cmd : cmds) {
    const uint64_t recording = cmd_recording(cmd);
    for (int i = 0; i < kStaged; ++i) {
      auto& st = g_staged[i];
      if (st.pick.tag && st.pick.cmd == uint64_t(cmd) && st.recording == recording) {
        recordings.headers[i] = recording;
        // Item 11 proof: the camera the build's xfb_to_world will read on the GPU (the UBO as it is now, after every host
        // write before this submit) against the record-time read the header carries.
        float now_cam[3];
        if (!st.cam_checked && st.ubo && read_mapped(st.ubo, st.ubo_offset + 208, now_cam, sizeof now_cam)) {
          st.cam_checked = true;
          ++g_cam_check.checked;
          const double dx = double(now_cam[0]) - st.cam[0], dy = double(now_cam[1]) - st.cam[1], dz = double(now_cam[2]) - st.cam[2];
          if (std::memcmp(now_cam, st.cam, sizeof now_cam) != 0) {
            ++g_cam_check.differ;
            g_cam_check.max_m = (std::max)(g_cam_check.max_m, std::sqrt(dx * dx + dy * dy + dz * dz));
          }
        }
      }
    }
  }
  // rt_hair_groom_primary_current: a G-buffer recording of this submit with a header slot. Its slot holds the
  // record-time publish; upgrade it to this frame's when that build is ordered before it on the GPU (host writes
  // before vkQueueSubmit are visible to the submitted work).
  for (PendingHeader& p : g_pending) {
    if (!p.open) continue;
    if (!p.slotless && !g_hdr_slots.map) { p.open = false; continue; }
    int g = -1;
    for (size_t k = 0; k < cmds.size() && g < 0; ++k)
      if (uint64_t(cmds[k]) == p.cmd && cmd_recording(cmds[k]) == p.recording) g = int(k);
    if (g < 0) continue;
    p.open = false;
    // Review 2026-11-08 item 1: every build refits the groom BLASes and rewrites the points in place. Once a build newer
    // than the record-time header is ordered before this G-buffer (an earlier submit, or earlier in this one), only the
    // newest such publish describes what the G-buffer will trace; the record-time header would trace its older TLAS
    // over refit BLASes with its older camera. So that publish is always applied: draws whose key it lacks draw their
    // card (slot mask bits 16-31, the strand variant's card mode: card G-buffer and card depth), the rest trace it.
    hair_primary::Staged picks[kStaged];
    bool submitted[kStaged], usable[kStaged];
    uint32_t pixels[kStaged];
    uint64_t keys[kStaged][hair_primary::kMaxGrooms];
    for (int i = 0; i < kStaged; ++i) {
      picks[i] = g_staged[i].pick; submitted[i] = g_staged[i].submitted; pixels[i] = g_staged[i].pixels;
      usable[i] = hair_primary::header_usable(g_staged[i].bytes);
      std::memcpy(keys[i], g_staged[i].keys, sizeof keys[i]);
    }
    GbFrame& gf = g_gb_frames[p.frame % 16];
    const bool frame_known = gf.frame == p.frame && gf.pass == p.pass;   // 6i: not a later pass's GbFrame of this label
    hair_primary::SubmitDecision d = hair_primary::submit_decision(picks, submitted, usable, pixels, keys, kStaged, p.tag,
        p.pixels, p.slots, g, [&](int i) {
          for (size_t k = 0; k < cmds.size(); ++k)
            if (uint64_t(cmds[k]) == g_staged[i].pick.cmd && cmd_recording(cmds[k]) == g_staged[i].recording) return int(k);
          return -1;
        }, frame_known, gf.swapped_slots);
    const int ci = d.publish;
    if (p.slotless) {   // rt_hair_groom_primary_current 0 (diagnostic): the record-time header cannot follow the build
      if (ci >= 0) ++g_header_mismatch;
      continue;
    }
    const StagedHeader* c = ci >= 0 ? &g_staged[ci] : nullptr;
    const uint32_t slot = uint32_t(&p - g_pending);
    uint8_t* image = static_cast<uint8_t*>(g_hdr_slots.map) + VkDeviceSize(slot) * hair_primary::kHeaderAllocAll;
    const uint32_t card_slots = d.plan.card_slots;
    const hair_primary::CurrentPick pick = d.pick;
    if (c && logging::stats_enabled(cfg_int)) {   // 5u observable: the TLAS this G-buffer traces against its header
      uint64_t address = 0;
      std::memcpy(&address, c->bytes + 16, 8);   // header words 4-5: the groom TLAS
      const auto r = g_tlas_check.check(address, c->pick.rec, c->keys, c->n, [](uint64_t rec) { return bvh_rec_submitted(rec); });
      if (r.other && g_tlas_check_logged++ < 16)
        log_stats("[hair primary] groom TLAS check: G-buffer frame %llu applies the publish of tag %u but its TLAS 0x%llx was "
                  "rebuilt at frame %llu by another build already submitted: instances %u, header rows %u, first differing "
                  "instance %d (TLAS key %04llx, header key %04llx)", (unsigned long long)p.frame, c->pick.tag,
                  (unsigned long long)address, (unsigned long long)r.frame_tlas, r.n_tlas, r.n_header, r.first_diff,
                  (unsigned long long)(r.key_tlas >> 48), (unsigned long long)(r.key_header >> 48));
    }
    if (c) {
      if (!usable[ci]) ++g_drop.upgrades_gated;
      const hair_primary::Upgrade& u = d.plan;
      header_image(*c, image);
      if (p.seal) std::memcpy(image, &p.seal, 4);   // rt_hair_primary_frame_tag: the frame keeps its own tag
      std::memcpy(image + hair_primary::kKeyMapOffset, u.key_map, sizeof u.key_map);   // the recorded draws' slots
      if (card_slots) {
        ++g_card_upgrades;
        for (uint32_t i = 0; i < hair_primary::kDrawSlots; ++i) g_card_slots += (card_slots >> i) & 1u;
      }
      if (frame_known) {
        gf.tag = p.seal ? p.seal : c->pick.tag;
        gf.pub_frame = c->pick.tag ? c->pick.tag - 1u : 0u;   // 5z: Header tag = publish frame + 1
        std::memcpy(gf.keys, c->keys, sizeof gf.keys);             // the rows the G-buffer now reads (count attribution)
        std::memcpy(gf.draw_keys, p.slots.keys, sizeof gf.draw_keys);   // markers carry the recorded slots
        for (uint32_t i = 0; i < hair_primary::kDrawSlots; ++i) if (card_slots & (1u << i)) gf.draw_keys[i] = 0;
        gf.swapped_slots &= ~card_slots;   // their cards: neither coverage evidence nor dropouts this frame
      }
    }
    ++g_current_picks[size_t(pick)];
    // Item 2: the slots whose G-buffer draws swapped (all recorded now): the pixel trace skips any other slot's strands.
    std::memcpy(image + hair_primary::kSlotMaskOffset, &d.masks, 4);
    if (frame_known)   // the record-time check of a tag applied by two G-buffer frames, with the final tag
      for (const GbFrame& older : g_gb_frames) if (older.frame && older.frame != gf.frame && older.tag == gf.tag)
        for (uint64_t key : older.keys) if (key) g_coverage.invalidate(key, gf.frame);
  }
  return recordings;
}

void hair_primary_submit_end(const HairPrimarySubmit& recordings, bool success) {
  if (!g_startup_enabled || !success || !g_live_ever.load(std::memory_order_acquire)) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (int i = 0; i < kStaged; ++i)
    if (recordings.headers[i] && g_staged[i].recording == recordings.headers[i]) g_staged[i].submitted = true;
}

void hair_primary_fault_log() {   // after a device loss: the mapped breadcrumbs stay readable (no lock: the device is gone)
  crumbs_log("[fault]");
}

namespace {
// Review 2026-11-08 item 13: the frame_index until which a prepass card-depth drop may still reach a G-buffer draw
// (DepthLatch: [p, p + 2]). While it runs the draw hook stays on even when rt_hair_groom was just turned off live, so
// that G-buffer draw still gets its native depth back (the fallback pipeline) instead of drawing without card depth.
std::atomic<uint64_t> g_depth_latch_until{0};

// 5m live switch (common/hair_live.h), once per frame_index: the settings change at a present, so one decision holds
// for every draw recorded under that frame_index. rt_hair_groom and rt_hair_groom_primary read 0 while rt_hair is 0
// (layer/util.cpp hair_gated) or under the master RT A/B.
void live_tick() {
  if (!g_startup_enabled) return;
  const uint64_t now = frame_index();
  if (g_live_frame.load(std::memory_order_acquire) == now) return;
  const bool requested = cfg_hot(CfgHot::rt_hair_groom) != 0 && cfg_hot(CfgHot::rt_hair_groom_primary) != 0;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_live_frame.load(std::memory_order_relaxed) == now || !g_dd) return;
  const bool reserved = g_header.b != VK_NULL_HANDLE;
  const hair_live::Edge edge = g_live.observe(requested, reserved || !g_header_tried, now);
  if (edge == hair_live::Edge::On) {
    // Publishes staged before the switch are never applied and the swap gate starts empty: the first swaps need two
    // fresh publishes, as after a level load (no 300-frame pause: the G-buffer images are the same).
    for (auto& st : g_staged) st.pick = {};
    g_gate.clear();
    g_live_keys.clear();
    g_last_tag = 0;
    for (CountSlot& c : g_cnt) c.valid = false;
    g_live_ever.store(true, std::memory_order_release);
    // rt_hair_live_warmup (5u audit): every groom slot rebuilds now (UVs), votes its LOD and re-joins: the level-load
    // warm-up window (ended by rt_hair_warmup's settled signal, at most kLoadQuietFrames).
    g_late.switch_on(now);   // rt_hair_debug_late_arrival: re-armed at every switch-on
    const bool warm = cfg_int("rt_hair_live_warmup", 0) != 0;
    if (warm) g_load.restart(now);
    log("[hair primary] live: strands ON at frame %llu (switch %u): swaps resume after %s",
        (unsigned long long)now, g_live.ons,
        warm ? "the groom state settles (rt_hair_live_warmup, as after a level load)" : "two fresh groom publishes");
  } else if (edge == hair_live::Edge::Off) {
    log("[hair primary] live: strands OFF at frame %llu (switch %u): cards from this frame; card depth restored "
        "through frame %llu", (unsigned long long)now, g_live.offs, (unsigned long long)g_depth_latch_until.load());
  } else if (edge == hair_live::Edge::Unavailable) {
    log("[hair primary] live: RTX Hair requested, but this session has no strand variants (see 'strand variants not "
        "prepared'): restart the game to enable");
  }
  g_live_on.store(g_live.on && reserved, std::memory_order_release);
  if (!g_live.on && g_live_ever.load(std::memory_order_relaxed) && stats_due(now)) {
    // The off windows' period line (the on windows have the 'grooms published' line of hair_primary_publish).
    log_stats("[hair primary] frame %llu: RTX Hair off (live): 0 grooms published; last %u frames: 0 prepass, 0 G-buffer and "
        "0 velocity strand draws; card depth restored by %llu native draws; switches on %u, off %u",
        (unsigned long long)now, stats_period(300), (unsigned long long)g_off_restores, g_live.ons, g_live.offs);
    g_off_restores = 0;
  }
  g_live_frame.store(now, std::memory_order_release);
}

// g_mu may be held: never ticks (the unlocked entry points hair_primary_wanted, hair_primary_before_pass and
// hair_primary_publish tick first).
bool strands_on() {
  return g_live_on.load(std::memory_order_acquire) && g_header.b != VK_NULL_HANDLE;
}
}  // namespace

void hair_primary_groom_tlas_built(uint64_t address, uint64_t rec, const uint64_t* keys, uint32_t n) {
  if (!logging::stats_enabled(cfg_int)) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_tlas_check.build(address, rec, frame_index(), keys, n);
}

bool hair_primary_late_hold(uint32_t index_count) {
  const int want = cfg_int("rt_hair_debug_late_arrival", 0);
  if (want <= 0 || index_count != uint32_t(want)) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  const uint64_t now = frame_index();
  bool start, release;
  const bool hold = g_late.draw(now, &start, &release);
  if (start)
    log("[hair primary] late arrival: groom of the %u-index hair draws held from frame %llu for %llu frames after the "
        "switch-on at frame %llu (rt_hair_debug_late_arrival)", index_count, (unsigned long long)now,
        (unsigned long long)(g_late.on + hair_primary::kLateArrivalFrames - now), (unsigned long long)g_late.on);
  if (release)
    log("[hair primary] late arrival: groom of the %u-index hair draws released at frame %llu, %llu frames after the "
        "switch-on (%u draw frames held)", index_count, (unsigned long long)now, (unsigned long long)(now - g_late.on),
        g_late.held);
  return hold;
}

bool hair_primary_wanted() {
  live_tick();
  return strands_on() || (g_startup_enabled && g_header.b != VK_NULL_HANDLE && frame_index() <= g_depth_latch_until.load());
}

// g_mu held, outside a render pass, in the main G-buffer pass's own command buffer (hair_primary_before_pass): read the
// oldest timestamp slot (its frame is done: kTimeSlots - 1 frames back), then reset it for this frame. Reset here, not
// in the publish's command buffer: a G-buffer recorded before the publish was never timed (hc2 2026-11-04: strand
// draws every period, no 'strand G-buffer draws GPU' line).
static void timing_before_gbuffer(VkCommandBuffer cmd, uint64_t now) {
  if (!logging::stats_enabled(cfg_int)) return;
  DeviceData* dd = g_dd;
  if (g_slot_frame[now % kTimeSlots] == now + 1) return;   // once per frame
  if (!g_qp) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kTimed * kTimeSlots * 2;
    if (dd->vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
  }
  if (g_qp) {
    const uint32_t slot = uint32_t(now % kTimeSlots);
    if (g_slot_frame[slot] && g_timed[slot]) {
      uint64_t r[kTimed * 4];
      const uint32_t n = g_timed[slot];
      if (dd->vk.GetQueryPoolResults(dd->device, g_qp, slot * kTimed * 2, n * 2, sizeof r, r, 16,
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) >= 0) {
        double ms = 0;
        bool all = true;
        for (uint32_t i = 0; i < n; ++i) {
          const uint64_t* a = r + i * 4;   // begin value, availability, end value, availability
          all = all && a[1] && a[3];
          if (a[1] && a[3] && a[2] >= a[0]) ms += double(a[2] - a[0]) * g_ts_period * 1e-6;
        }
        if (all) { g_ms_sum += ms; ++g_ms_frames; }
      }
    }
    dd->vk.CmdResetQueryPool(cmd, g_qp, slot * kTimed * 2, kTimed * 2);
    g_timed[slot] = 0;
    g_slot_frame[slot] = now + 1;
  }
}

void hair_primary_before_pass(VkCommandBuffer cmd, const Bg3PassInfo& p) {
  live_tick();
  if (!strands_on()) {
    // 5m: level loads are tracked while strands are off too, so a switch-on in a scene already loaded starts at once
    // instead of treating the first G-buffer image it sees as a level load (rhl1 2026-11-09: a 300-frame pause, 10 s).
    if (g_header.b != VK_NULL_HANDLE && p.depth_image && uint64_t(p.depth_image) == geo_gbuffer_depth_image()) {
      const uint64_t identity = image_identity(uint64_t(p.depth_image));   // before g_mu (tracker lock order)
      std::lock_guard<std::mutex> lock(g_mu);
      if (g_dd) observe_depth(uint64_t(p.depth_image), frame_index(), identity);
    }
    return;
  }
  if (p.depth_image && uint64_t(p.depth_image) == geo_gbuffer_depth_image()) {
    const uint64_t identity = image_identity(uint64_t(p.depth_image));   // before g_mu (tracker lock order)
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_dd) {
      observe_depth(uint64_t(p.depth_image), frame_index(), identity);
      g_depth.prune(frame_index());
      if (p.color_count >= 4 && p.width == g_px_w && p.height == g_px_h) {
        timing_before_gbuffer(cmd, frame_index());
      }
    }
  }
  if (!g_order) return;
  // The main G-buffer pass: its colour targets and the G-buffer depth image (the prepass writes the same image without
  // colour targets), at the pixel record's size.
  if (p.color_count < 4 || !p.depth_image || uint64_t(p.depth_image) != geo_gbuffer_depth_image()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_dd || p.width != g_px_w || p.height != g_px_h) return;
  const uint64_t frame = frame_index() + 1;
  // Once per frame label. 6h: with rt_hair_header_scene_pass never at the decal pass (it took the next label when a
  // present fell between the two passes, and the next frame's hair then traced the publish before over this build's
  // BLASes); read here, so the key switches live.
  const bool scene_key = cfg_int("rt_hair_header_scene_pass", 0) != 0;
  if (scene_key != g_cfg_scene_pass) g_header_pass = hair_primary::HeaderPassStats{};   // a period line counts one state
  g_cfg_scene_pass = scene_key;
  // 6i: with the key a second scene pass recording under the same label (two G-buffer frames in one present interval)
  // applies again, with its own tag, pending header and GbFrame; the first one's submit then leaves the GbFrame alone.
  const uint64_t hitches = g_header_pass.taken_scene + g_header_pass.again;
  const bool applies = g_labels.begin(p.color_count, g_cfg_scene_pass, frame, uint64_t(cmd), cmd_recording(cmd), g_header_pass);
  // 6k: a hitch frame (two scene G-buffer pass recordings in one present interval) was never seen in a live run
  // (0 in hx1 190905 and 194658; its handling is proven by the CTest only). The first one of a session is logged with
  // log(), so a user's log has it without detailed logging; the period line's counters need detailed logging.
  static bool hitch_logged = false;
  if (!hitch_logged && g_header_pass.taken_scene + g_header_pass.again != hitches) {
    hitch_logged = true;
    log("[hair primary] hitch frame: a second scene G-buffer pass was recorded under frame label %llu (two G-buffer "
        "frames in one present interval), the first of this session: %s (rt_hair_header_scene_pass %d). Logged once; "
        "the header pass period line counts them with detailed logging.", (unsigned long long)frame,
        applies ? "it applied its own strand header and tag" : "it ran on the first pass's strand header and tag",
        int(g_cfg_scene_pass));
  }
  if (!applies) return;
  hair_primary::Staged picks[kStaged];
  const bool frame_tags = cfg_int("rt_hair_primary_frame_tag", 0) != 0;
  for (int i = 0; i < kStaged; ++i) {
    const auto& st = g_staged[i];
    const bool same_recording = st.pick.cmd == uint64_t(cmd) && st.recording == cmd_recording(cmd) &&
                                bvh_rec_state(st.pick.rec) == 0;
    picks[i] = st.submitted || same_recording ? st.pick : hair_primary::Staged{};
    // A header without its build camera discards every strand fragment (hp_rebase): never applied (cards that frame).
    if (picks[i].tag && !hair_primary::header_usable(st.bytes)) { picks[i] = hair_primary::Staged{}; ++g_drop.gated; }
  }
  int i = hair_primary::pick_publish(picks, kStaged, uint64_t(cmd), [](uint64_t rec) { return bvh_rec_submitted(rec); });
  // This frame's or the previous frame's publish (the strand shaders rebase it to this camera, hp_rebase); an older one
  // may reference ranges freed by a LOD change (bvhsplit kGroomFreeLag covers 2 frames of age, not more).
  if (i >= 0 && g_staged[i].pick.tag + 1 < frame) i = -1;
  // 5m: never a publish staged before strands last turned on (its TLAS may be refit or freed since).
  if (i >= 0 && (g_staged[i].pick.tag == 0 || !g_live.publish_usable(g_staged[i].pick.tag - 1))) i = -1;
  if (i < 0) {
    g_live_keys.clear();   // no swap this frame; the prepass of the next one keeps its cards too
    if (g_order_none++ % 300 == 0)
      log("[hair primary] G-buffer pass with no groom publish ordered before it: cards this frame (%llu such passes)",
          (unsigned long long)g_order_none);
    return;
  }
  // rt_hair_groom_primary_current: the header from a host slot, decided again at vkQueueSubmit. Off (a developer
  // diagnostic since review 2026-11-08: the record-time header cannot follow a build submitted before the G-buffer)
  // the submit only counts such frames (header/BLAS mismatch frames in the period line).
  const bool current = cfg_int("rt_hair_groom_primary_current", 0) != 0;
  int slot = -1;
  const bool have_slots = current &&
      (g_hdr_slots.b || make(g_hdr_slots, VkDeviceSize(kHeaderSlots) * hair_primary::kHeaderAllocAll,
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "hair primary header slots", true)) && g_hdr_slots.map;
  if (current && !have_slots) {   // fail closed: without a submit-time header no strand swap this frame
    g_live_keys.clear();
    return;
  }
  const uint32_t seal = g_labels.seal(frame_tags, g_staged[i].pick.tag);
  {
    const int ring = int(g_pending_next);
    g_pending_next = (g_pending_next + 1) % kHeaderSlots;
    if (have_slots) slot = ring;
    PendingHeader& p = g_pending[ring];
    p = PendingHeader{};
    p.cmd = uint64_t(cmd); p.recording = cmd_recording(cmd); p.frame = frame;
    p.tag = g_staged[i].pick.tag; p.n = g_staged[i].n; p.pixels = g_staged[i].pixels;
    p.seal = frame_tags ? seal : 0u;
    p.pass = g_labels.pass;
    std::memcpy(p.keys, g_staged[i].keys, sizeof p.keys);
    p.slots = g_staged[i].slots;
    p.slotless = !have_slots;
    p.open = true;
  }
  apply_header(cmd, g_staged[i], slot, seal);
  GbFrame& gf = g_gb_frames[frame % 16];
  gf.frame = frame;
  gf.pass = g_labels.pass;
  gf.swapped_slots = 0;

  gf.tag = seal;
  gf.pub_frame = g_staged[i].pick.tag ? g_staged[i].pick.tag - 1u : 0u;   // the record-time publish until a submit upgrade
  // A tag applied by two G-buffer frames: with a submit-time header the final tag is known only then (the check runs
  // there, hair_primary_submit_begin).
  if (slot < 0)
    for (const GbFrame& older : g_gb_frames) if (older.frame && older.frame != frame && older.tag == gf.tag)
      for (uint64_t key : older.keys) if (key) g_coverage.invalidate(key, frame);

  std::memcpy(gf.keys, g_staged[i].keys, sizeof gf.keys);
  std::memcpy(gf.draw_keys, g_staged[i].slots.keys, sizeof gf.draw_keys);
}

// g_mu held. Called only at a native G-buffer draw with no depth fallback.
void depth_orphan(uint64_t key, uint64_t now, VkPipeline pipeline, const char* reason) {
  if (!g_depth.orphan(key, now, false, false)) return;
  ++g_st.orphan;
  g_depth.block(key, now);   // item 19: no more prepass drops for this key until it has a fallback again (300 frames)
  if (g_depth_orphan_count < 4)
    g_depth_orphans[g_depth_orphan_count++] = {key, g_depth.keys.at(key), now, uint64_t(pipeline), reason};
}


static bool hair_primary_swap_locked(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  if (args.kind != 1 || !s->pipe[0] || !hair_primary_wanted()) return false;
  // M2.2: a sun cascade pass (depth-only D16 cascade array layer), decided before the lock (tracker lock inside).
  const bool sun = s->pass.color_count == 0 && s->pass.depth_format == VK_FORMAT_D16_UNORM && geo_cascade_layer(s->pass) >= 0;
  std::lock_guard<std::mutex> lock(g_mu);
  if (sun) {
    const auto cc = g_cascade_clone.find(s->pipe[0]);
    if (cc == g_cascade_clone.end()) return false;
    ++g_part.cascade_total;
    const uint64_t key = hair_primary_draw_key(s, args);
    // Item 12: the card leaves the game's cascade only while the core's RT sun shadow (which traces the strands) was
    // applied this or the last frame and the strands are on: elsewhere the game's mask is the hair's only sun shadow.
    static const volatile uint64_t* sun_rt = static_cast<const volatile uint64_t*>(persist(hair_primary::kSunRtPersistKey, sizeof(uint64_t)));
    const bool sun_ok = strands_on() && sun_rt && hair_primary::sun_rt_fresh(*sun_rt, frame_index());
    if (!sun_ok && g_cfg_cascade_drop) ++g_part.cascade_no_rt_sun;
    if (!hair_primary::cascade_card_drop(g_cfg_cascade_drop && sun_ok, true,
                                         hair_primary::publish_fresh(g_pub_frame, frame_index() + 1) && g_pub_keys.count(key),
                                         true)) return false;
    s->hp_coverage = s->hp_query = UINT32_MAX;
    g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cc->second);
    ++g_part.cascade_dropped;
    return true;
  }
  s->hp_coverage = s->hp_query = UINT32_MAX;
  const uint64_t now = frame_index();
  const bool main_view = s->pass.width == g_px_w && s->pass.height == g_px_h &&
                         uint64_t(s->pass.depth_image) == geo_gbuffer_depth_image();
  if (!strands_on()) {
    // Item 13: strands turned off live after a prepass dropped this key's card depth: its G-buffer draw still restores
    // the native depth (fallback pipeline), as any refused swap does; nothing else runs.
    if (!main_view || s->pass.color_count < 4) return false;
    const uint64_t key = hair_primary_draw_key(s, args);
    if (!g_switches.keys.empty()) g_switches.live_off(key, now);
    if (!g_depth.needs_card_depth(key, now, false)) return false;
    const auto fb = g_fallback.find(s->pipe[0]);
    if (fb == g_fallback.end()) { depth_orphan(key, now, s->pipe[0], "no fallback (strands off)"); return false; }
    g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fb->second);
    ++g_fallbacks;
    ++g_off_restores;
    return true;
  }
  const auto hp = g_hair_pipes.find(s->pipe[0]);
  if (hp != g_hair_pipes.end()) g_part.draw(hp->second & 3u, main_view, hp->second >= 4u);
  if (!main_view) return false;
  const auto it = g_variant.find(s->pipe[0]);
  const uint64_t key = hair_primary_draw_key(s, args);
  // Round 3 M4: a draw of a non-hair pipeline matters only when its index range is a published groom key or one whose
  // prepass card depth went (a sibling draw of the same mesh); every other game draw leaves at once (it used to add an
  // `uncovered` entry per draw key, which prune then walked for 300 frames).
  if (hp == g_hair_pipes.end() && it == g_variant.end() && !g_gate.published.count(key) &&
      !g_gate.published_prev.count(key) && !g_depth.keys.count(key))
    return false;
  g_key_filter.add(key);   // before g_depth.prepass below may add it (the publish adds g_gate's keys)
  if (it == g_variant.end()) {
    if (s->pass.color_count >= 4) {
      g_depth.observe_pipeline(key, now, false, g_fallback.count(s->pipe[0]) != 0);
      if (g_live_keys.count(key)) g_coverage.invalidate(key, now + 1);
      if (g_depth.needs_card_depth(key, now, false)) {
        const auto fb = g_fallback.find(s->pipe[0]);
        if (fb != g_fallback.end()) {
          g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fb->second);
          ++g_fallbacks; return true;
        }
        depth_orphan(key, now, s->pipe[0], "no variant");
      }
    }
    return false;
  }
  const Pass pass = g_pass_of[s->pipe[0]];
  const uint32_t draw = g_live_slots.find(key);
  if (draw == hair_primary::kDrawSlots && g_live_keys.count(key)) g_without_slot.insert(key);
  const auto dv = g_draw_variants.find(s->pipe[0]);
  // Native cards receive a masked replay; swapped strands write the same two markers directly.
  const bool probe = pass == Pass::Gbuffer && g_order && g_applied_frame == now + 1 && g_live_keys.count(key) &&
                     !g_load.quiet(now) && dv != g_draw_variants.end() && draw < hair_primary::kDrawSlots && dv->second.probe[draw];
  // The first gate that refuses this draw (period line "swap refusals"); the decision itself is unchanged.
  using hair_primary::Refusal;
  Refusal why = Refusal::Count;
  bool other_gate = false;   // 6k: a gate other than the strike and the card depth refused
  auto refuse = [&](Refusal r) {
    if (why == Refusal::Count) why = r;
    other_gate |= r != Refusal::Strike && r != Refusal::NotDropped;
  };
  const auto cover = g_coverage.why(key, now + 1);
  if (g_published + 2 < now + 1) refuse(Refusal::Publish);
  else if (g_load.quiet(now)) refuse(Refusal::Quiet);
  else if (!hair_primary::fill_spares(Refusal::Watchdog, g_card_fill) && g_watchdog.active(now + 1))
    refuse(Refusal::Watchdog);   // item 19: a counted frame lost its strands
  else if (cover != hair_primary::CoverageGate::Why::Ok) refuse(hair_primary::coverage_refusal(cover));
  else if (!(pass == Pass::Gbuffer ? g_gate.allow_gbuffer(key) : pass == Pass::Velocity ? g_gate.allow_velocity(key)
                                                                                         : g_gate.allow_prepass(key)))
    refuse(Refusal::Strike);
  bool allowed = why == Refusal::Count;
  // Header order prepass (rt_hair_groom_primary_order 0): the G-buffer may read another frame's header than the probe
  // proved (hc2 2026-11-04: adjacent tags in 296 of 300 counted frames, the player half bald). Cards, fail closed.
  // Header order prepass: no pass swaps (the G-buffer would read another frame's header than the evidence; a prepass
  // clone without card depth next to card G-buffer draws loses the hair: order turned off after order-1 swaps).
  if (!g_order || draw == hair_primary::kDrawSlots) { allowed = false; refuse(!g_order ? Refusal::Order : Refusal::NoSlot); }
  const int passes = g_cfg_passes;   // round 3 L5: read once per publish
  {   // rt_hair_primary_passes (diagnostic bitmask, default 15): 1 G-buffer, 2 velocity, 4 prepass clone, 8 coverage probe
    if (!(passes & (pass == Pass::Gbuffer ? 1 : pass == Pass::Velocity ? 2 : 4))) { allowed = false; refuse(Refusal::Passes); }
  }
  if (g_order && pass != Pass::Prepass &&
      (!hair_primary::header_applied_for(g_applied_frame, now, pass == Pass::Velocity, g_cfg_scene_pass) || !g_live_keys.count(key))) {
    allowed = false; refuse(Refusal::Applied);
  }
  // The prepass drops the card depth only after a G-buffer pass applied a header holding the key: the most recent
  // G-buffer apply (g_applied_frame: frame_index + 1 of that G-buffer). The game records the next frame's prepass before
  // the present that advances frame_index, so that apply is this frame's number or the next (an equality with now
  // refused every prepass: hv7 2026-11-04, 0 prepass strand draws next to 187 G-buffer ones).
  if (g_order && pass == Pass::Prepass && !hair_primary::prepass_drop_allowed(g_applied_frame, now, g_live_keys.count(key) != 0,
      g_depth_safe_keys.count(key) != 0, g_depth.allow_prepass(key, now))) {
    allowed = false; refuse(Refusal::PrepassDrop);
  }
  if (pass == Pass::Gbuffer && !g_depth.dropped(key, now)) { allowed = false; refuse(Refusal::NotDropped); }
  if (pass == Pass::Gbuffer && !probe && !allowed && g_live_keys.count(key)) g_coverage.invalidate(key, now + 1);
  if (pass == Pass::Gbuffer && g_fallback.count(s->pipe[0])) g_depth_safe_keys.insert(key);
  if (pass == Pass::Gbuffer && (draw == hair_primary::kDrawSlots || dv == g_draw_variants.end() || !dv->second.strand[draw])) {
    allowed = false; refuse(Refusal::NoStrand);
  }
  if (!allowed) g_refusals.add(pass == Pass::Gbuffer ? 0 : pass == Pass::Velocity ? 1 : 2, why);
  if (pass != Pass::Velocity) {   // 5t: this key's strand/card switches (the decision itself is unchanged)
    const auto cause = hair_primary::switch_cause(why);
    if (pass == Pass::Gbuffer)
      g_switches.gbuffer(key, now, allowed, cause, why == Refusal::NotDropped || why == Refusal::PrepassDrop);
    else if (!allowed) g_switches.prepass_refused(key, now, cause);
  }
  // Every G-buffer gate passed but the card depth was still there: the next prepass may drop it (SwapGate ready_*).
  // 6k (SwapGate::early): also when the only other refusal is the strike of a groom in its first publish.
  if (pass == Pass::Gbuffer && draw < hair_primary::kDrawSlots && dv != g_draw_variants.end() && dv->second.strand[draw] &&
      (why == Refusal::NotDropped || (why == Refusal::Strike && !other_gate && g_gate.early && g_gate.arriving(key))))
    g_gate.gbuffer_ready(key);
  if (pass == Pass::Gbuffer)
    g_depth.observe_pipeline(key, now, draw < hair_primary::kDrawSlots && dv != g_draw_variants.end() && dv->second.strand[draw],
                             g_fallback.count(s->pipe[0]) != 0);
  if (pass == Pass::Gbuffer && g_depth.needs_card_depth(key, now, allowed)) {
    const auto fb = g_fallback.find(s->pipe[0]);
    if (fb == g_fallback.end()) { depth_orphan(key, now, s->pipe[0], "no fallback"); return false; }
    g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fb->second);
    ++g_fallbacks;
    if (probe && (passes & 8)) { s->hp_coverage = kProbeAfter; g_coverage_period[key].probed = true; }
    return true;
  }
  if (!allowed) {
    // Cards drawn; the masked strand variant follows the native draw (hair_primary_unswap) for the strand samples.
    if (probe && (passes & 8)) {   // bit 8 off: no masked probe draw (no strand evidence)
      s->hp_coverage = kProbeAfter;
      g_coverage_period[key].probed = true;
      return true;
    }
    return false;
  }
  if (pass == Pass::Gbuffer) {
    g_gate.gbuffer_drew(key);
    {   // 5o warm-up lines: this key's first strand swap since its first publish after the load, and the load's first
      const auto fp = g_first_pub.find(key);
      if (fp != g_first_pub.end() && fp->second) {
        const uint64_t waited = now >= fp->second ? now - fp->second : 0;
        ++g_warm.first_swaps; g_warm.wait_sum += waited; g_warm.wait_max = (std::max)(g_warm.wait_max, waited);
        fp->second = 0;
      }
      if (!g_warm.logged) {
        g_warm.logged = true;
        log("[hair primary] warm-up: level load at frame %llu: load quiet ended at frame %llu (+%llu, %s), first strand "
            "G-buffer swap at frame %llu, %llu frames after the load (rt_hair_warmup %d)",
            (unsigned long long)g_load.boundary, (unsigned long long)g_load.quiet_until,
            (unsigned long long)(g_load.quiet_until - g_load.boundary), g_load.early ? "groom state settled" : "frame count",
            (unsigned long long)now, (unsigned long long)(now - g_load.boundary), cfg_int("rt_hair_warmup", 0));
      }
    }
    if (g_gb_frames[(now + 1) % 16].frame == now + 1) g_gb_frames[(now + 1) % 16].swapped_slots |= 1u << draw;
    g_coverage.swapped(key, now + 1);
    {
      uint64_t& last = g_last_swap_frame[key];
      if (last && now > last + 1 && now - last <= 300) ++g_swap_resumes;
      last = now;
    }
    auto& period = g_coverage_period[key];
    if (period.last_swap != now) {
      ++period.swapped; period.last_swap = now;
      // rt_hair_primary_hold: a swap the strict gate would have refused (the period line's "held").
      if ((g_coverage.hold || g_coverage.footprint) && g_coverage.why(key, now + 1, hair_primary::kCoverMin, hair_primary::kCoverMax, false) !=
                                 hair_primary::CoverageGate::Why::Ok)
        ++g_held_swaps;
    }
  }
  auto& decision = g_coverage_period[key];
  const double used = g_coverage.latest.at(key).ratio();
  if (!decision.decisions++) decision.used_min = decision.used_max = used;
  decision.used_min = (std::min)(decision.used_min, used);
  decision.used_max = (std::max)(decision.used_max, used);
  g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass == Pass::Gbuffer ? dv->second.strand[draw] : it->second);
  ++(pass == Pass::Gbuffer ? g_st.gbuffer : pass == Pass::Velocity ? g_st.velocity : g_st.prepass);
  g_part.swapped(pass == Pass::Gbuffer ? hair_primary::Participation::kGbuffer
                 : pass == Pass::Velocity ? hair_primary::Participation::kVelocity : hair_primary::Participation::kPrepass);
  if (pass == Pass::Prepass) {
    g_depth.prepass(key, now);
    if (g_depth_latch_until.load() < now + 2) g_depth_latch_until = now + 2;
  }
  if (probe) g_coverage_period[key].probed = true;   // the swapped strand draw writes its pixel entries itself
  const uint32_t ts = uint32_t(now % kTimeSlots);
  if (pass == Pass::Gbuffer && g_qp && g_slot_frame[ts] == now + 1 && g_timed[ts] < kTimed) {
    s->hp_query = (ts * kTimed + g_timed[ts]++) * 2u;
    g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, s->hp_query);
  }
  return true;
}

bool hair_primary_swap(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  // Strands on, and neither the pipeline nor the index range ever mattered (no hair FS, no variant, no cascade clone;
  // not a published groom key, no dropped card depth): the locked path below returns false for such a draw in every
  // pass with no other effect (hp_coverage and hp_query are already UINT32_MAX outside a swap). About 7000 game draws
  // per frame at the inn took g_mu (and the tracker lock in D16 passes) for this answer.
  if (args.kind == 1 && s->pipe[0] && strands_on() && !g_pipe_filter.maybe(uint64_t(s->pipe[0])) &&
      !g_key_filter.maybe(hair_primary_draw_key(s, args)))
    return false;
  return hair_primary_swap_locked(cmd, s, args);
}

void hair_primary_unswap(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_dd) return;
  if (s->hp_query != UINT32_MAX) g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, s->hp_query + 1);
  s->hp_query = UINT32_MAX;
  if (s->hp_coverage != UINT32_MAX) {
    if (s->hp_coverage == kProbeAfter) {
      const uint64_t key = hair_primary_draw_key(s, args);
      const uint32_t draw = g_live_slots.find(key);
      const auto it = g_draw_variants.find(s->pipe[0]);
      if (it != g_draw_variants.end() && draw < hair_primary::kDrawSlots && it->second.probe[draw]) {
        g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second.probe[draw]);
        g_dd->vk.CmdDrawIndexed(cmd, args.count, args.instances, args.first, args.vertex_offset, args.first_instance);
      }
    }
    s->hp_coverage = UINT32_MAX;
  }
  if (s->pipe[0]) g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipe[0]);
}

// rt_hair_warmup (docs/RTX_HAIR_PRIMARY.md 5o): bvhsplit's per-build settled signal; ends the level-load quiet window
// after hair_primary::kWarmupSettledFrames settled frames instead of kLoadQuietFrames.
void hair_primary_warmup(bool settled) {
  if (!strands_on()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_dd) return;
  const uint64_t now = frame_index();
  if (g_load.settle(now, settled))
    log("[hair primary] warm-up: load quiet ended at frame %llu, %llu frames after the level load (groom state settled "
        "for %llu frames, rt_hair_warmup)", (unsigned long long)now, (unsigned long long)(now - g_load.boundary),
        (unsigned long long)hair_primary::kWarmupSettledFrames);
}

void hair_primary_publish(VkCommandBuffer cmd, uint64_t rec, uint64_t tlas, uint64_t pool, uint64_t pool_bytes,
                          const hair_primary::Row* rows, const uint64_t* keys, const uint8_t* lods, uint32_t count,
                          uint32_t width, uint32_t height, const float* viewport) {
  if (!strands_on()) return;
  const uint64_t depth_image = geo_gbuffer_depth_image(), identity = image_identity(depth_image);
  std::lock_guard<std::mutex> lock(g_mu);
  DeviceData* dd = g_dd;
  if (!dd) return;
  g_cfg_passes = cfg_int("rt_hair_primary_passes", 15);                   // the draw hook's keys (round 3 L5)
  g_cfg_cascade_drop = cfg_int("rt_hair_groom_cascade_drop", 0) != 0;
  count =(std::min)(count, hair_primary::kMaxGrooms);
  const uint64_t now = frame_index();
  observe_depth(depth_image, now, identity);
  if (!count || !tlas || !width || !height || !viewport || viewport[2] == 0.0f || viewport[3] == 0.0f) {
    g_gate.clear();
    return;
  }
  if (hair_primary::coverage_bytes(width, height) > UINT32_MAX) { g_gate.clear(); return; }
  if (width != g_px_w || height != g_px_h || !g_pixels.b) {
    release(g_pixels);
    g_px_w = g_px_h = 0;
    // 6f: the dense samples live behind the entries and markers (always allocated: 6 KB; written only by a variant
    // specialized with kSpecIdHairDenseSample).
    if (!make(g_pixels, hair_primary::coverage_bytes(width, height) + hair_primary::kDenseBytesAll,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              "hair primary pixels")) {
      log("[hair primary] pixel record %ux%u could not be allocated: hair keeps its cards", width, height);
      g_gate.clear();
      return;
    }
    g_px_w = width;
    g_px_h = height;
    // Round 3 L1: the header still holds the old record's address (freed after the defer lag): tag 0 makes every strand
    // reader discard until a header with the new record is applied.
    dd->vk.CmdFillBuffer(cmd, g_header.b, 0, 4, 0u);
    g_coverage.clear(); g_last_swap_frame.clear();
    g_pixels_fresh = true;
    log("[hair primary] pixel record %ux%u: %.1f MB", width, height, double(g_pixels.size) / 1048576.0);
  }
  if (!count_feedback(cmd, now, rec)) {
    if (g_failures.feedback++ < 8) log("[hair primary] strand feedback unavailable: hair keeps its cards");
    g_gate.clear();
    return;
  }
  if (g_pixels_fresh) {   // tag 0 is never a frame tag
    dd->vk.CmdFillBuffer(cmd, g_pixels.b, 0, VK_WHOLE_SIZE, 0u);
    dd->vk.CmdFillBuffer(cmd, g_header.b, hair_primary::kColourOffset, hair_primary::kMaxGrooms * hair_primary::kColourBytes, 0u);
    g_pixels_fresh = false;
  }
  hair_primary::Header h{};
  StagedHeader st;
  // The build's own frame and camera (5i, review 2/3): rt_after_capture stored them under the frame_index of the build's
  // start; a present on the queue thread may have advanced frame_index since (a re-read gave a header without camera,
  // a tag shared with the next build and an off-by-one previous camera). The tag, the build camera and the previous
  // publish's link all use that frame. No camera at all (unreadable UBO): kFlagCamera stays off and the header is
  // never applied (before_pass, hair_primary_submit_begin): cards that frame, never a frame without hair.
  uint64_t build_frame = 0;
  const bool cam_ok = rt_current_build_camera(st.cam, &build_frame);
  if (!cam_ok) ++g_drop.no_camera;
  else if (build_frame != now) ++g_drop.recovered;
  if (cam_ok && !rt_current_build_ubo(&st.ubo, &st.ubo_offset)) st.ubo = VK_NULL_HANDLE;
  if (cam_ok) {   // 5x/5z diagnostics (the frame watchdog line): the build camera and the eye of its camera-relative VP
    float vp[16], eye[3];
    const bool vp_ok = st.ubo && read_mapped(st.ubo, st.ubo_offset + 128, vp, sizeof vp);
    g_cam_steps.note(build_frame, st.cam, vp_ok ? hair_primary::vp_eye(vp, eye) : -1.0f);
  }
  const uint64_t pub = cam_ok ? build_frame : now;
  h.tag = uint32_t(pub + 1);
  if (logging::stats_enabled(cfg_int) &&   // 6d: this build's stamp, in the command buffer that rewrites the points
      (g_stamp.b || make(g_stamp, hair_primary::kStampBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         "hair primary build stamp", true))) {
    VkMemoryBarrier sb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    sb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    sb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &sb, 0, nullptr, 0, nullptr);
    dd->vk.CmdFillBuffer(cmd, g_stamp.b, 0, 4, h.tag);
  }
  h.flags = cfg_int("rt_hair_groom_tips", 0) != 0 ? hair_primary::kFlagTips : 0u;
  if (cfg_int("rt_hair_groom_coverage", 0) != 0) h.flags |= hair_primary::kFlagCoverage;   // M3.2, live off-switch
  if (cfg_int("rt_hair_groom_nearest", 0) != 0) h.flags |= hair_primary::kFlagNearest;     // 5u, live off-switch
  {   // 5u: which trace the strand G-buffer runs, logged when it changes (the header flag needs specialization 1047)
    const int now_flag = (h.flags & hair_primary::kFlagNearest) != 0 ? 1 : 0;
    if (now_flag != g_nearest_flag_logged || g_nearest_compiled != g_nearest_compiled_logged) {
      g_nearest_flag_logged = now_flag;
      g_nearest_compiled_logged = g_nearest_compiled;
      log("[hair primary] nearest-strand trace: header flag %d at publish frame %llu; specialization 1047 in the strand "
          "G-buffer pipelines %d (-1: none created yet): the %s trace runs", now_flag, (unsigned long long)frame_index(),
          g_nearest_compiled, now_flag && g_nearest_compiled == 1 ? "nearest-strand" : "restart-loop");
    }
  }
  if (cfg_int("rt_hair_card_fill", 0) != 0) h.flags |= hair_primary::kFlagCardFill;   // 6g, live once compiled in
  g_card_fill = (h.flags & hair_primary::kFlagCardFill) != 0 && g_fill_compiled == 1;
  g_coverage.confirm_runs = g_card_fill ? hair_primary::kFillConfirm : hair_primary::CoverageGate::kCoverConfirm;
  if (int(g_card_fill) + 2 * g_fill_compiled != g_fill_logged) {
    g_fill_logged = int(g_card_fill) + 2 * g_fill_compiled;
    log("[hair primary] card fill: header flag %d at publish frame %llu; specialization 1053 in the strand G-buffer "
        "pipelines %d (-1: none created yet): a card-marked pixel without a strand %s; frame watchdog %s, a swapping key "
        "returns to cards after %u low measurements in a row", (h.flags & hair_primary::kFlagCardFill) != 0 ? 1 : 0,
        (unsigned long long)frame_index(), g_fill_compiled, g_card_fill ? "draws its card" : "is discarded",
        g_card_fill ? "reports only" : "switches to cards", unsigned(g_coverage.confirm_runs));
  }
  if (cfg_int("rt_hair_macro_normal", 0) != 0) h.flags |= hair_primary::kFlagMacroNormal;   // 5g, live once compiled in
  if (cfg_int("rt_hair_tangent_world", 0) != 0) h.flags |= hair_primary::kFlagTangentWorld;
  {   // 5y rt_hair_mv_card (live: 1 fallback only, 2 every strand pixel; the velocity variant needs specialization 1049)
    const int mv_card = cfg_int("rt_hair_mv_card", 0);
    if (mv_card == 1) h.flags |= hair_primary::kFlagMvCardFallback;
    if (mv_card >= 2) h.flags |= hair_primary::kFlagMvCardAll;
  }
  h.width = width;
  h.height = height;
  h.tlas = tlas;
  h.pool = pool;
  h.pool_bytes = uint32_t((std::min)(pool_bytes, uint64_t(UINT32_MAX)));
  h.pixels = g_pixels.addr;
  std::memcpy(h.viewport, viewport, sizeof h.viewport);
  // The build camera: the origin of this publish's camera-relative groom points (the strand passes rebase their rays to
  // their own camera). Called inside the scene build: rt_after_capture holds the scene lock.
  if (cam_ok && std::isfinite(st.cam[0]) && std::isfinite(st.cam[1]) && std::isfinite(st.cam[2]))
    h.flags |= hair_primary::kFlagCamera;
  // The rows' previous points are the last update's: the publish of the previous frame, with its build camera.
  const bool prev_cpu = g_last_pub_camera_frame && g_last_pub_camera_frame + 1 == pub;
  // rt_hair_header_cam_gpu (item 11): both header cameras written on the GPU from the build's UBO; the previous one only
  // when the previous publish's copy is in the GPU slot (that publish ran the copy, one frame back).
  const bool prev_gpu = prev_cpu && g_cam_gpu_pub == pub;
  st.cam_gpu = (h.flags & hair_primary::kFlagCamera) && cfg_int("rt_hair_header_cam_gpu", 0) != 0 && header_cam_gpu(cmd, prev_gpu);
  g_cam_gpu_pub = st.cam_gpu ? pub + 1 : 0;
  g_cam_check.gpu += st.cam_gpu;
  if (prev_cpu && (!st.cam_gpu || prev_gpu)) {
    std::memcpy(st.cam + 4, g_last_pub_camera, sizeof g_last_pub_camera);
    h.flags |= hair_primary::kFlagPrevCamera;
  }
  if (h.flags & hair_primary::kFlagCamera) {   // 5y counter line
    ++g_prev_cam.pubs;
    if (!(h.flags & hair_primary::kFlagPrevCamera)) ++(prev_cpu ? g_prev_cam.gpu : g_prev_cam.gap);
  }
  g_last_pub_camera_frame = (h.flags & hair_primary::kFlagCamera) ? pub : 0;
  std::memcpy(g_last_pub_camera, st.cam, sizeof g_last_pub_camera);
  g_pub_keys.clear();   // M2.2: the groom set this publish puts in the RT scene
  for (uint32_t k = 0; k < count; ++k) if (keys[k]) g_pub_keys.insert(keys[k]);
  g_pub_frame = count ? now + 1 : 0;
  st.pick = {h.tag, rec, uint64_t(cmd)};
  st.recording = cmd_recording(cmd);
  st.n = hair_primary::pack(h, rows, count, st.bytes);
  st.pixels = width * height;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) st.keys[k] = k < count ? keys[k] : 0;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) st.lods[k] = k < count ? lods[k] : 0;
  g_slots.publish(keys, count);
  st.slots = g_slots;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) st.key_map[k] = g_slots.find(st.keys[k]);
  g_staged[g_staged_next] = st;
  g_staged_next = (g_staged_next + 1) % kStaged;
  if (cfg_int("rt_hair_groom_primary_order", 0) != 0) {
    // Applied in the G-buffer's own command buffer (hair_primary_before_pass); kept in the ring until then.
    g_order = true;
  } else {
    g_order = false;
    apply_header(cmd, st);
  }
  for (uint32_t k = 0; k < count && k < hair_primary::kMaxGrooms; ++k) if (keys[k]) g_key_filter.add(keys[k]);
  g_gate.publish(keys, (std::min)(count, hair_primary::kMaxGrooms), now);
  // Round 3 L3: the per-key maps grew all session; drop keys not published for kKeyForgetFrames.
  for (uint32_t k = 0; k < count; ++k) if (keys[k]) g_key_seen[keys[k]] = now;
  for (uint32_t k = 0; k < count; ++k) if (keys[k]) g_first_pub.try_emplace(keys[k], now);   // 5o warm-up line
  if (now % 300 == 0) {
    auto unseen = [&](uint64_t key) {
      const auto it = g_key_seen.find(key);
      return it == g_key_seen.end() || it->second + kKeyForgetFrames < now;
    };
    auto prune = [&](auto& map) { for (auto it = map.begin(); it != map.end();) it = unseen(it->first) ? map.erase(it) : std::next(it); };
    prune(g_transmit); prune(g_last_card); prune(g_last_swap_frame); prune(g_first_pub);
    prune(g_coverage.latest); prune(g_coverage.incomplete); prune(g_coverage.swapping);
    prune(g_coverage.bad_run);
    for (auto it = g_key_seen.begin(); it != g_key_seen.end();) it = it->second + kKeyForgetFrames < now ? g_key_seen.erase(it) : std::next(it);
  }
  g_coverage.hold = cfg_int("rt_hair_primary_hold", 0) != 0;   // once per publish (hysteresis, CoverageGate)
  g_coverage.footprint = cfg_int("rt_hair_coverage_footprint", 0) != 0;   // the bound at each measurement's footprint
  g_cfg_union = cfg_int("rt_hair_coverage_union", 0) != 0;               // 5u: judge every accepted strand pixel
  g_cfg_dense = cfg_int("rt_hair_dense_sample", 0) != 0;                  // 6f: copy and log the dense samples
  g_coverage.confirm = cfg_int("rt_hair_coverage_confirm", 0) != 0;       // 5u: two bad measurements in a row
  g_coverage.keep = cfg_int("rt_hair_coverage_keep", 0) != 0;             // 6i: evidence does not expire by age
  g_gate.early = g_coverage.keep;                                         // 6k: and its handshake is not waited for twice
  if (!g_coverage.confirm) g_coverage.bad_run.clear();
  // Round 3 (aa98b960) had joined this statement into the comment above: the publish gate then refused every
  // strand swap (main-view hair stayed cards). Keep it on its own line.
  g_published = now + 1;
  ++g_st.publishes;
  coverage_log(now);
  if (stats_due(now) && g_seen.frame && (cfg_int("rt_stats_period_frames", 0) <= 0 || g_seen.frame > g_stats_start)) {
    char per[640];
    int at = 0;
    uint32_t refused = 0;
    for (uint32_t k = 0; k < hair_primary::kMaxGrooms && at < 600; ++k) {
      refused += g_seen.refused[k];
      if (g_seen.keys[k])
        at += std::snprintf(per + at, sizeof per - at, " %u:%u/LOD%u(%04llx)", k, g_seen.pixels[k], unsigned(g_seen.lods[k]),
                            (unsigned long long)(g_seen.keys[k] >> 48));
    }
    g_adjacent_total += g_adjacent_window;
    {
      const float* r = g_seen.ray;
      // The evidence window (tag - ev): all accepted strand pixels over all card pixels of that G-buffer frame (-1: none).
      const uint32_t ev = g_seen.evidence_tag ? g_seen.tag - g_seen.evidence_tag : hair_primary::kCountTags;
      // Traced/hit are the shared pixel entries: their trace covers only the slots that swapped (header slot mask). A
      // frame whose draws were all refused (watchdog, coverage) draws masked probes that trace privately, unstored: then
      // 0 entries hold a strand while the probes' accepted pixels below are near their card pixels (2026-11-09 rv3-s8).
      log_stats("[hair primary] strand traces in counted frame %llu: %u traced pixel entries, %u with a swapped draw's "
          "strand (shared trace; probe draws trace privately: see accepted pixels); sampled ray: "
          "origin (%.4f %.4f %.4f), card distance %.3f, direction (%.4f %.4f %.4f), committed %.0f, hit distance %.3f, "
          "groom %.0f, segment %.0f, tag %.0f, camera (%.3f %.3f %.3f), header camera (%.3f %.3f %.3f); counted with tag %u, "
          "header tag now %u; card pixels "
          "with tag %u/%u/%u/%u: %u/%u/%u/%u; accepted pixels: %u/%u/%u/%u, evidence from tag %u (G-buffer frame %llu: "
          "same-pass unique pixel counts), evidence strand share %.3f",
          (unsigned long long)g_seen.frame, g_seen.traced,
          g_seen.hit, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14],
          r[12] - r[0], r[13] - r[1], r[14] - r[2],   // origin = camera - header camera (hp_rebase)
          g_seen.tag, g_last_tag, g_seen.tag, g_seen.tag - 1, g_seen.tag - 2, g_seen.tag - 3, g_seen.tag_totals[0],
          g_seen.tag_totals[1], g_seen.tag_totals[2], g_seen.tag_totals[3],
          g_seen.accepted_totals[0], g_seen.accepted_totals[1], g_seen.accepted_totals[2], g_seen.accepted_totals[3], g_seen.evidence_tag,
          (unsigned long long)g_seen.evidence_frame, ev < hair_primary::kCountTags && g_seen.tag_totals[ev] ?
          double(g_seen.accepted_totals[ev]) / double(g_seen.tag_totals[ev]) : -1.0);
    }
    log_stats("[hair primary] counted frame %llu: strand pixels per groom (LOD, draw key prefix):%s; pool-bound refusals %u "
        "(session %llu); counted frames with adjacent-tag pixels %llu (session %llu), header order %s",
        (unsigned long long)g_seen.frame, at ? per : " none", refused, (unsigned long long)g_refused_total,
        (unsigned long long)g_adjacent_window, (unsigned long long)g_adjacent_total, g_order ? "G-buffer" : "prepass");
    g_adjacent_window = 0;
  }
  if (stats_due(now)) {
    char refusals[640];
    g_refusals.format(refusals);
    log_stats("[hair primary] frame %llu: %u grooms published; last %u frames: %llu publishes, %llu prepass, %llu "
        "G-buffer and %llu velocity strand draws, %llu pipelines refused, %llu feedback failures; prepass card depth dropped without its G-buffer "
        "swap %llu; hair draws strand/total: main G-buffer %llu/%llu, velocity %llu/%llu, prepass %llu/%llu; other views "
        "G-buffer %llu, velocity %llu, prepass %llu; instanced %llu; card-only pass groups %u; sun cascade hair card draws "
        "%llu, without card depth %llu (kept, no RT sun shadow %llu); swap refusals (main-view hair draws kept as cards, first refusing gate):%s; coverage hold %s, footprint bound %s, swap frames the strict bound would refuse %llu, strand swaps resumed after card frames %llu; G-buffer headers at submit (rt_hair_groom_primary_current): this frame's publish %llu, record-time because not recorded %llu, build after the G-buffer %llu, different grooms %llu",
        (unsigned long long)now, count, stats_period(300), (unsigned long long)g_st.publishes, (unsigned long long)g_st.prepass,
        (unsigned long long)g_st.gbuffer, (unsigned long long)g_st.velocity, (unsigned long long)g_failures.refused, (unsigned long long)g_failures.feedback,
        (unsigned long long)g_st.orphan, (unsigned long long)g_part.main_swapped[0], (unsigned long long)g_part.main_total[0],
        (unsigned long long)g_part.main_swapped[1], (unsigned long long)g_part.main_total[1],
        (unsigned long long)g_part.main_swapped[2], (unsigned long long)g_part.main_total[2],
        (unsigned long long)g_part.other_total[0], (unsigned long long)g_part.other_total[1],
        (unsigned long long)g_part.other_total[2], (unsigned long long)g_part.instanced, g_part.card_only_groups(),
        (unsigned long long)g_part.cascade_total, (unsigned long long)g_part.cascade_dropped,
        (unsigned long long)g_part.cascade_no_rt_sun, refusals,
        g_coverage.hold ? "on" : "off", g_coverage.footprint ? "on" : "off", (unsigned long long)g_held_swaps, (unsigned long long)g_swap_resumes,
        (unsigned long long)g_current_picks[0], (unsigned long long)g_current_picks[1],
        (unsigned long long)g_current_picks[2], (unsigned long long)g_current_picks[3]);
    char per[768] = {};
    size_t at = 0;
    for (uint32_t i = 0; i < g_depth_orphan_count; ++i) {
      const auto& o = g_depth_orphans[i];
      at += std::snprintf(per + at, sizeof per - at, " %04llx:prepass=%llu,draw=%llu,%s,pipeline=0x%llx",
          (unsigned long long)(o.key >> 48), (unsigned long long)o.prepass, (unsigned long long)o.draw, o.reason, (unsigned long long)o.pipeline);
    }
    log_stats("[hair primary] depth orphans (last %u frames, first 4; key prefix:prepass frame_index,draw frame_index,reason,pipeline):%s",
        stats_period(300), at ? per : " none");
    {
      char first[520] = {};
      size_t fa = 0;
      for (uint32_t i = 0; i < g_drop.firsts; ++i) {
        const auto& d = g_drop.first[i];
        fa += std::snprintf(first + fa, sizeof first - fa, " %llu:%04llx:%u/%u:%s", (unsigned long long)d.frame,
                            (unsigned long long)(d.key >> 48), d.card, d.accepted, hair_primary::dropout_name(d.why));
        if (d.rows > 1 && fa < sizeof first)   // a key on two groom rows (two characters): which of them lost its strands
          fa += std::snprintf(first + fa, sizeof first - fa, " [%u rows: %u+%u]", d.rows, d.row_pixels[0], d.row_pixels[1]);
      }
      using hair_primary::Dropout;
      log_stats("[hair primary] dropouts (last %u frames): %llu swapped G-buffer draws wrote under a tenth of their card "
          "pixels (no card marker %llu, no trace %llu, rays missed %llu, rejected after the hit %llu, unattributed %llu); first (G-buffer frame:"
          "key prefix:card/strand pixels:check):%s; publishes without the build camera %llu (recovered %llu); camera gate: "
          "headers refused %llu, submit upgrades kept back %llu",
          stats_period(300), (unsigned long long)g_drop.draws, (unsigned long long)g_drop.why[size_t(Dropout::NoMarker)],
          (unsigned long long)g_drop.why[size_t(Dropout::NoTrace)], (unsigned long long)g_drop.why[size_t(Dropout::Missed)],
          (unsigned long long)g_drop.why[size_t(Dropout::Rejected)], (unsigned long long)g_drop.why[size_t(Dropout::Unattributed)],
          g_drop.firsts ? first : " none",
          (unsigned long long)g_drop.no_camera, (unsigned long long)g_drop.recovered, (unsigned long long)g_drop.gated,
          (unsigned long long)g_drop.upgrades_gated);
      g_drop = DropoutStats{};
    }
    log_stats("[hair primary] groom TLAS check (last %u frames): %llu G-buffer submits checked, %llu traced a TLAS rebuilt "
              "by another build already submitted, %llu of them with instance keys other than the header's rows",
              stats_period(300), (unsigned long long)g_tlas_check.checked, (unsigned long long)g_tlas_check.other,
              (unsigned long long)g_tlas_check.differ);
    g_tlas_check.reset_period();
    log_stats("[hair primary] header safety (last %u frames): header/BLAS mismatch frames %llu (record-time header older "
        "than a build ordered before its G-buffer), submit headers with card draws %llu (draw slots %llu), mask-hole pixels "
        "%llu (strand traces that skipped a card-kept groom), watchdog trips %llu, "
        "C2 colour samples %llu; build camera "
        "re-read at submit (item 11): %llu publishes checked, %llu differ from the record-time read (max %.4f m), %llu "
        "header cameras copied on the GPU",
        stats_period(300), (unsigned long long)g_header_mismatch, (unsigned long long)g_card_upgrades,
        (unsigned long long)g_card_slots, (unsigned long long)g_mask_holes, (unsigned long long)g_watchdog_period,
        (unsigned long long)g_colour_samples, (unsigned long long)g_cam_check.checked,
        (unsigned long long)g_cam_check.differ, g_cam_check.max_m, (unsigned long long)g_cam_check.gpu);
    // 6c: the header camera against the groom points' origin, measured on the GPU (no label, no CPU camera read).
    log_stats("[hair primary] rebase origin (last %u frames): %llu counted frames with a sampled strand ray, %llu with a "
        "non-zero origin (largest %.3f m): there the header camera was not the origin of the groom points traced; "
        "rt_hair_header_cam_gpu %d", stats_period(300), (unsigned long long)g_rebase.sampled,
        (unsigned long long)g_rebase.moved, g_rebase.max_m, cfg_int("rt_hair_header_cam_gpu", 0) != 0 ? 1 : 0);
    g_rebase = hair_primary::RebaseStats{};
    {   // 6h: which pass applied the header, and the scene passes that ran on another pass's header
      const hair_primary::HeaderPassStats& hs = g_header_pass;
      log_stats("[hair primary] header pass (last %u frames, rt_hair_header_scene_pass %d): applied at the scene G-buffer "
          "pass %llu, at the decal pass %llu (decal passes passed over %llu); scene G-buffer passes without their own "
          "header: label taken by the decal pass %llu (their hair traced the publish before over this build's BLASes), "
          "by another scene pass %llu (two G-buffer frames in one present interval); second scene passes of an interval "
          "with their own header and tag %llu",
          stats_period(300), int(g_cfg_scene_pass), (unsigned long long)hs.scene, (unsigned long long)hs.decal,
          (unsigned long long)hs.decal_skipped, (unsigned long long)hs.taken_decal, (unsigned long long)hs.taken_scene,
          (unsigned long long)hs.again);
      g_header_pass = hair_primary::HeaderPassStats{};
    }
    if (g_cfg_dense) {   // 6f
      char still[720], moving[720];
      dense_side(g_dense.side[0], still, sizeof still);
      dense_side(g_dense.side[1], moving, sizeof moving);
      log_stats("[hair primary] dense samples (last %u frames, rt_hair_dense_sample 1): %llu sampled frames, %llu torn "
          "samples; still: %s | moving (camera step over %.2f m a frame, from the G-buffer cameras): %s",
          stats_period(300), (unsigned long long)g_dense.frames, (unsigned long long)g_dense.torn, still,
          double(hair_primary::kDenseMovingStep), moving);
      g_dense = hair_primary::DenseStats{};
      g_dense_lines = 0;
    }
    log_stats("[hair primary] build stamp (last %u frames): counted G-buffer frames whose groom points and BLASes were of "
        "the applied publish's build %llu, of a newer build %llu (older TLAS and rows over refit BLASes), of an older "
        "build %llu, not read %llu", stats_period(300), (unsigned long long)g_stamp_n[1], (unsigned long long)g_stamp_n[2],
        (unsigned long long)g_stamp_n[3], (unsigned long long)g_stamp_n[0]);
    for (uint64_t& n : g_stamp_n) n = 0;
    g_header_mismatch = g_card_upgrades = g_card_slots = g_mask_holes = g_watchdog_period = g_colour_samples = 0;
    g_cam_check = CamCheck{};
    g_depth_orphan_count = 0;
  }
  if (stats_due(now) && (cfg_int("rt_hair_groom_shadow_t", 0) != 0 || cfg_int("rt_hair_groom_ao_t", 0) != 0)) {
    // rt_hair_groom_shadow_t participation: the draws with a measured a_f and the groom rows that carried one.
    float lo = 1.0f, hi = 0.0f, sum[3] = {0, 0, 0};
    for (const auto& [key, t] : g_transmit) {
      const float m = (t.a[0] + t.a[1] + t.a[2]) / 3.0f;
      lo = (std::min)(lo, m), hi = (std::max)(hi, m);
      for (int c = 0; c < 3; ++c) sum[c] += t.a[c];
    }
    const float n = g_transmit.empty() ? 1.0f : float(g_transmit.size());
    log_stats("[hair primary] shadow transmittance: %zu draws measured, a_f mean %.3f %.3f %.3f (per draw %.3f..%.3f); "
        "last %u frames: %llu groom rows with a_f",
        g_transmit.size(), sum[0] / n, sum[1] / n, sum[2] / n, g_transmit.empty() ? 0.0f : lo, hi,
        stats_period(300), (unsigned long long)g_transmit_rows);
    g_transmit_rows = 0;
  }
  if (stats_due(now) && g_warm.first_swaps) {
    log_stats("[hair primary] warm-up: last %u frames: %llu draws swapped to strands for the first time since the level "
        "load; frames from their first publish: mean %.1f, max %llu",
        stats_period(300), (unsigned long long)g_warm.first_swaps, double(g_warm.wait_sum) / double(g_warm.first_swaps),
        (unsigned long long)g_warm.wait_max);
    g_warm.first_swaps = g_warm.wait_sum = g_warm.wait_max = 0;
  }
  if (stats_due(now) && g_ms_frames)
    log_stats("[hair primary] strand G-buffer draws GPU %.3f ms/frame (average of %u frames, per-draw timestamps)",
        g_ms_sum / g_ms_frames, g_ms_frames);
  if (stats_due(now)) g_part = hair_primary::Participation{}, g_refusals.reset(), g_held_swaps = 0, g_swap_resumes = 0, std::fill_n(g_current_picks, 4, uint64_t(0));
  if (stats_due(now)) g_failures.reset(), g_st.orphan = 0, g_st.publishes = g_st.prepass = g_st.gbuffer = g_st.velocity = 0, g_ms_sum = 0, g_ms_frames = 0;
}

}  // namespace bg3
