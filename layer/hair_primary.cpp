// rt_hair_groom_primary (docs/RTX_HAIR_PRIMARY.md): the visible hair of matched static CHAR_Hair draws becomes the
// strand groom. At pipeline creation every static hair pipeline gets a variant: the camera Depth prepass a clone
// without depth writes (the cards leave no depth; their XFB capture is unchanged), the G-buffer the strand fragment
// shader shaders/rt/hair_primary_gbuffer.hlsl with depth GREATER_OR_EQUAL + writes (reversed Z), the velocity pass
// (FS b70cadc5) rt/hair_primary_velocity.hlsl (strand motion from last frame's points, no depth writes). At draw time a
// matched draw (its index range is in the groom set of the latest build) is drawn with the variant. The strand
// shader reads the frame record (common/hair_primary.h) through the header address baked as specialization
// constants 1100/1101; bvhsplit publishes the header after the groom TLAS build (hair_primary_publish).
// Variants exist only with linear swept spheres, shaderInt64 and fragmentStoresAndAtomics; activation is latched at device creation until hot activation is qualified.
#include "host.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../common/hair_primary.h"
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
bool g_startup_enabled = false;        // Restart safety gate; INI reload cannot enable strands.
bool g_frag_stores = false;            // fragmentStoresAndAtomics enabled by the game
std::mutex g_mu;
HpBuf g_header, g_pixels, g_crumbs;              // header: fixed (its address is in every variant); pixels: render size
bool g_header_tried = false;
bool g_pixels_fresh = false;           // just allocated: zero it before the next header update
uint32_t g_px_w = 0, g_px_h = 0;
std::unordered_map<VkShaderModule, uint64_t> g_mods;           // the native hair FS modules (erased when destroyed)
hair_primary::LoadGuard g_load;                               // level-load boundary (the G-buffer depth image)
std::unordered_map<VkPipeline, VkPipeline> g_variant;         // game pipeline -> variant
enum class Pass : uint8_t { Prepass, Gbuffer, Velocity };
std::unordered_map<VkPipeline, Pass> g_pass_of;               // variant -> its pass
// Index ranges of the grooms in the last two published TLASes: a draw is swapped only when its groom is in both, so
// the prepass (recorded before this frame's build) and the G-buffer (after it) agree on a draw that just appeared.
hair_primary::SwapGate g_gate;   // which draws are swapped (common/hair_primary.h SwapGate)
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
bool g_depth_fallback_ready = true; // any uncloneable native hair pipeline forbids later depth drops
hair_primary::DrawSlots g_slots, g_live_slots;
std::unordered_set<uint64_t> g_without_slot;
uint64_t g_published = 0;                                     // frame_index + 1 of the last publish with grooms
struct Stats { uint64_t prepass = 0, gbuffer = 0, velocity = 0, refused = 0, publishes = 0, orphan = 0; } g_st;
// Keys whose prepass card depth went (each G-buffer pass after it must swap them: else counted as orphan).
hair_primary::DepthLatch g_depth;
std::unordered_set<uint64_t> g_depth_resolved, g_depth_safe_keys, g_depth_failed;
struct DepthOrphan { uint64_t key, prepass, drop; };
DepthOrphan g_depth_orphans[4] = {};
uint32_t g_depth_orphan_count = 0;
uint64_t g_logged_pipes = 0;
// Fail-safe feedback (SwapGate::feedback): strand pixels per groom of the last frame's pixel record, counted on the GPU
// (rt/hair_primary_count.hlsl) at the next publish, copied to a host-visible ring and read kCountSlots - 1 publishes
// later. With them the keys and whether each one's G-buffer variant ran in the counted frame.
constexpr uint32_t kCountSlots = 4;
struct CountSlot { uint64_t frame = 0, gframe = 0; bool valid = false; uint32_t tag = 0; uint64_t keys[hair_primary::kMaxGrooms] = {};
                   uint8_t lods[hair_primary::kMaxGrooms] = {};
                   bool drew[hair_primary::kMaxGrooms] = {}; VkBuffer rb = VK_NULL_HANDLE; VkDeviceMemory rb_mem = VK_NULL_HANDLE;
                   const uint32_t* map = nullptr; };
CountSlot g_cnt[kCountSlots];
uint32_t g_cnt_next = 0;
HpBuf g_counts;                          // device local: 64 uints
VkDescriptorSetLayout g_cnt_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_cnt_pl = VK_NULL_HANDLE;
VkPipeline g_cnt_pipe = VK_NULL_HANDLE;
uint64_t g_cnt_gen = 0;
uint32_t g_last_tag = 0, g_last_n = 0;   // the previous publish: its tag, pixel count and keys per groom k
uint64_t g_last_keys[hair_primary::kMaxGrooms] = {};
uint8_t g_last_lods[hair_primary::kMaxGrooms] = {};
// Diagnostics of the last read count (logged with the 300-frame line and at each strike): per groom k of the counted
// frame its draw key, LOD, strand pixels and pool-bound refusals.
struct CountSeen { uint64_t frame = 0; uint64_t keys[hair_primary::kMaxGrooms] = {}; uint8_t lods[hair_primary::kMaxGrooms] = {};
                   uint32_t pixels[hair_primary::kMaxGrooms] = {}, refused[hair_primary::kMaxGrooms] = {};
                   uint32_t traced = 0, hit = 0, tag = 0, evidence_tag = 0, tag_totals[hair_primary::kCountTags] = {};
                   uint64_t evidence_frame = 0;
                   float ray[16] = {}; };
CountSeen g_seen;
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
};
constexpr int kStaged = 3;
StagedHeader g_staged[kStaged];
int g_staged_next = 0;
// Probe refusals since the last coverage line: no publish of this frame ordered before the prepass, load quiet window.
float g_last_pub_camera[3] = {};   // the last publish's build camera (frame + 0 when it had none)
uint64_t g_last_pub_camera_frame = 0;
bool g_order = false;                       // the last publish was staged (the key on)
uint64_t g_applied_frame = 0;               // frame_index + 1 of the last G-buffer pass that applied (or refused)
std::unordered_set<uint64_t> g_live_keys;   // the keys of the header the G-buffer reads (order mode: swaps need them)
uint64_t g_order_none = 0;                  // G-buffer passes with no publish ordered before them (no swap)
void apply_header(VkCommandBuffer cmd, const StagedHeader& st);
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
  bci.size = hair_primary::kCountBytes;
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

// g_mu held. The counting pipeline (push descriptors: the pixel record, the counts; push: tag, pixel count).
bool count_pipeline() {
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/hair_primary_count.hlsl", "main", "cs_6_6", &blob)) return false;
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

// g_mu held, outside a render pass, before this publish's header update and SwapGate::publish. Reads the oldest
// slot's counts into the gate, then counts the previous frame's pixel record into it. False: no feedback possible
// (the caller then stops every swap: fail safe).
bool count_feedback(VkCommandBuffer cmd, uint64_t now) {
  DeviceData* dd = g_dd;
  CountSlot& s = g_cnt[g_cnt_next];
  if (s.valid) {
    if (!(life_gpu_done() > s.frame + 1)) return true;   // still in flight: no new count this frame
    uint32_t total = 0, other_total = 0, other[hair_primary::kMaxGrooms];
    g_seen.frame = s.frame;
    g_seen.traced = s.map[hair_primary::kTracedOffset / 4];
    g_seen.tag = s.tag;
    g_seen.hit = s.map[hair_primary::kTracedOffset / 4 + 1];
    std::memcpy(g_seen.ray, s.map + hair_primary::kRaySampleOffset / 4, sizeof g_seen.ray);
    for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) {
      g_seen.keys[k] = s.keys[k];
      g_seen.lods[k] = s.lods[k];
      g_seen.pixels[k] = s.map[k];
      g_seen.refused[k] = s.map[hair_primary::kRefusedOffset / 4 + k];
      other[k] = s.map[hair_primary::kOtherOffset / 4 + k];
      g_refused_total += g_seen.refused[k];
      total += s.map[k];
      float t[4];
      std::memcpy(t, s.map + hair_primary::kTransmitOffset / 4 + k * 4, sizeof t);
      if (s.keys[k] && t[3] == 1.0f) g_transmit[s.keys[k]].add(t);
      other_total += other[k];
    }
    { // Both counts come from the same shader invocation set and tag.
      uint32_t totals[hair_primary::kCountTags] = {};
      const uint32_t* tc = s.map + hair_primary::kTagCountOffset / 4;
      const uint32_t* cc = s.map + hair_primary::kCardCountOffset / 4;
      for (uint32_t j = 0; j < hair_primary::kCountTags; ++j)
        for (uint32_t k = 0; k < 64; ++k) totals[j] += cc[j * 64 + k];
      std::memcpy(g_seen.tag_totals, totals, sizeof totals);
      const int j = hair_primary::newest_counted_tag(totals);
      bool ambiguous = false;
      const GbFrame* gf = j < 0 ? nullptr : hair_primary::unique_frame(g_gb_frames, 16, s.tag - uint32_t(j), ambiguous);
      g_seen.evidence_tag = gf ? gf->tag : 0;
      if (ambiguous) {
        if (cfg_int("rt_stats_period_frames", 0) <= 0 || s.frame > g_stats_start) ++g_ambiguous;
        for (const GbFrame& f : g_gb_frames) if (f.tag == s.tag - uint32_t(j))
          for (uint64_t key : f.keys) if (key) g_coverage.invalidate(key, f.frame);
      }
      if (gf) {
        for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) if (gf->draw_keys[k])
          g_coverage.observe(gf->draw_keys[k], gf->frame, cc[uint32_t(j)*64+k], tc[uint32_t(j)*64+k]);
        g_seen.evidence_frame = gf->frame;
      }
    }
    // A counted frame whose strand pixels all carry an adjacent frame's tag: the G-buffer ran against another publish
    // (submission order). No strike from it; logged so a live run shows how often (rate-limited).
    if (other_total && (cfg_int("rt_stats_period_frames", 0) <= 0 || s.frame > g_stats_start)) ++g_adjacent_window;   // counted frames with adjacent-tag strand pixels (the 300-frame line)
    if (!total && other_total && g_misaligned_logged++ % 300 == 0)
      log("[hair primary] counted frame %llu: its %u strand pixels carry an adjacent frame's tag (the G-buffer ran "
          "against another publish): no strike (%llu such frames)", (unsigned long long)s.frame, other_total,
          (unsigned long long)g_misaligned_logged);
    s.valid = false;
  }
  if (!g_last_tag || g_pixels_fresh || g_last_n != g_px_w * g_px_h) return true;
  if (!s.rb && !make_readback(s)) return false;
  if (!g_counts.b && !make(g_counts, hair_primary::kCountBytes,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           "hair primary counts"))
    return false;
  if (!count_pipeline()) return false;
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
  dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_cnt_pipe);
  dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_cnt_pl, 0, 2, wr);
  // The tag the counted G-buffer frame (this frame's) applied; without one (header order prepass, or no G-buffer pass
  // applied this frame) the newest applied tag, for the diagnostics only: no strand evidence from that count.
  const GbFrame& gf = g_gb_frames[(now + 1) % 16];
  const bool counted = gf.frame == now + 1;
  const uint32_t tag = counted ? gf.tag : g_applied_cmd == uint64_t(cmd) && g_applied_recording == cmd_recording(cmd) ? g_applied_tag : g_last_tag;
  const uint32_t push[4] = {tag, g_last_n, uint32_t(g_header.addr), uint32_t(g_header.addr >> 32)};
  dd->vk.CmdPushConstants(cmd, g_cnt_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
  dd->vk.CmdDispatch(cmd, (g_last_n + 255) / 256, 1, 1);
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0,
                            nullptr);
  const VkBufferCopy c{0, 0, hair_primary::kCountBytes};
  dd->vk.CmdCopyBuffer(cmd, g_counts.b, s.rb, 1, &c);
  s.gframe = counted ? gf.frame : 0;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) {
    s.keys[k] = counted ? gf.keys[k] : g_last_keys[k];
    s.lods[k] = g_last_lods[k];
    s.drew[k] = g_last_keys[k] && g_gate.gbuffer_now.count(g_last_keys[k]) != 0;   // the counted frame's G-buffer
  }
  s.frame = now;
  s.tag = tag;
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
    log("[hair primary] UNAVAILABLE (%s): matched hair keeps its cards", why);
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
void apply_header(VkCommandBuffer cmd, const StagedHeader& st) {
  DeviceData* dd = g_dd;
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr,
                            0, nullptr);
  dd->vk.CmdUpdateBuffer(cmd, g_header.b, 0, st.n, st.bytes);
  dd->vk.CmdUpdateBuffer(cmd, g_header.b, hair_primary::kCameraOffset, sizeof st.cam, st.cam);   // both cameras
  {   // the breadcrumb buffer's address (0: none; created on first use, host-visible)
    if (!g_crumbs.b) make(g_crumbs, hair_primary::kCrumbBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "hair primary breadcrumbs", true);
    const uint64_t a[2] = {g_crumbs.map ? uint64_t(g_crumbs.addr) : 0u, 0u};
    dd->vk.CmdUpdateBuffer(cmd, g_header.b, hair_primary::kCrumbAddrOffset, sizeof a, a);
  }
  g_live_slots = st.slots;
  dd->vk.CmdUpdateBuffer(cmd, g_header.b, hair_primary::kKeyMapOffset, sizeof st.key_map, st.key_map);
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  dd->vk.CmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  g_last_tag = st.pick.tag;
  // The tag this recording's strand passes seal with: the count recorded later in the same recording (the scene build
  // follows the G-buffer pass) counts that frame's entries, not a newer publish applied by another recording.
  g_applied_cmd = uint64_t(cmd);
  g_applied_recording = cmd_recording(cmd);
  g_applied_tag = st.pick.tag;
  g_last_n = st.pixels;
  std::memcpy(g_last_keys, st.keys, sizeof g_last_keys);
  std::memcpy(g_last_lods, st.lods, sizeof g_last_lods);
  g_live_keys.clear();
  for (uint64_t k : st.keys)
    if (k) g_live_keys.insert(k);
}

// Detect the boundary before any camera prepass can trace an old scene's header.
// The publish path also calls this for the first image / missing prepass metadata.
void observe_depth(uint64_t image, uint64_t now) {
  if (!g_load.observe(image, now)) return;
  g_gate.clear();
  g_coverage.clear();
  g_depth_safe_keys.clear();
  g_depth = {}; g_depth_resolved.clear(); g_depth_failed.clear(); g_depth_orphan_count = 0;

  for (auto& gf : g_gb_frames) gf = GbFrame{};

  for (auto& st : g_staged) st.pick = {};
  g_live_keys.clear();
  g_slots = {}; g_live_slots = {};
  g_last_tag = 0;
  g_pixels_fresh = true;
  g_transmit.clear();
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
  char per[1200] = {};
  size_t at = 0;
  uint32_t shown = 0, probed = 0, swapped = 0, below = 0;
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
    if (shown++ < 12) at += std::snprintf(per + at, sizeof per - at, " %04llx:%u/%u=%.3f,%u,used=%.3f..%.3f",
        (unsigned long long)(key >> 48), unsigned((std::min)(e.strand, uint64_t(UINT32_MAX))),
        unsigned((std::min)(e.card, uint64_t(UINT32_MAX))), ratio, period.swapped, period.used_min, period.used_max);
  }
  log("[hair primary] coverage (last %u frames): %u draws probed, %u swapped; per draw (key prefix: newest accepted strand pixels/unique card pixels = ratio, swapped frames, used ratio min..max):%s; ratio min %.3f, bound %.3f, upper %.3f, draws outside bounds %u; "
      "ambiguous tags %u; native depth fallbacks %llu; draws without a slot %u",
      stats_period(300), probed, swapped, per, minimum, hair_primary::kCoverMin, hair_primary::kCoverMax, below, g_ambiguous, (unsigned long long)g_fallbacks, unsigned(g_without_slot.size()));
  g_ambiguous = 0; g_fallbacks = 0; g_without_slot.clear();
  g_coverage_period.clear();
  crumbs_log("[hair primary]");
}

}  // namespace

void hair_primary_stats_reset() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_stats_start = frame_index();
  g_st = Stats{}; g_coverage_period.clear();
  g_ambiguous = 0; g_fallbacks = 0; g_without_slot.clear();
  g_adjacent_window = 0; g_transmit_rows = 0; g_ms_sum = 0; g_ms_frames = 0;
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
  g_startup_enabled = cfg_int("rt_hair_groom_primary", 0) != 0;
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
  g_depth_fallback_ready = true;
  g_coverage.clear();
  g_depth_safe_keys.clear();
  g_depth = {}; g_depth_resolved.clear(); g_depth_failed.clear(); g_depth_orphan_count = 0;
  g_coverage_period.clear();
  for (auto& st : g_staged) st = StagedHeader{};
  g_staged_next = 0;
  g_live_keys.clear();
  g_slots = {}; g_live_slots = {}; g_without_slot.clear();
  g_gate = hair_primary::SwapGate{};
  g_load = hair_primary::LoadGuard{};
  g_order = false;
  g_applied_frame = g_published = 0;
  g_pass_of.clear();
  g_mods.clear();
  for (CountSlot& s : g_cnt) {
    if (s.rb) dd->vk.DestroyBuffer(dd->device, s.rb, nullptr);
    if (s.rb_mem) dd->vk.FreeMemory(dd->device, s.rb_mem, nullptr);
    s = CountSlot{};
  }
  if (g_counts.b) dd->vk.DestroyBuffer(dd->device, g_counts.b, nullptr);
  if (g_counts.m) dd->vk.FreeMemory(dd->device, g_counts.m, nullptr);
  g_counts = HpBuf{};
  if (g_cnt_pipe) dd->vk.DestroyPipeline(dd->device, g_cnt_pipe, nullptr);
  if (g_cnt_pl) dd->vk.DestroyPipelineLayout(dd->device, g_cnt_pl, nullptr);
  if (g_cnt_dsl) dd->vk.DestroyDescriptorSetLayout(dd->device, g_cnt_dsl, nullptr);
  g_cnt_pipe = VK_NULL_HANDLE;
  g_cnt_pl = VK_NULL_HANDLE;
  g_cnt_dsl = VK_NULL_HANDLE;
  g_last_tag = 0;
  if (g_qp) dd->vk.DestroyQueryPool(dd->device, g_qp, nullptr);
  g_qp = VK_NULL_HANDLE;
  for (HpBuf* b : {&g_header, &g_pixels, &g_crumbs}) {
    if (b->b) dd->vk.DestroyBuffer(dd->device, b->b, nullptr);
    if (b->m) dd->vk.FreeMemory(dd->device, b->m, nullptr);
    *b = HpBuf{};
  }
  g_dd = nullptr;
  g_startup_enabled = false;
  g_header_tried = false;
}

void hair_primary_note_module(VkShaderModule m, uint64_t hash) {
  const bool ours = hash == hair_primary::kDeferredStaticFs || hash == hair_primary::kDepthStaticFs ||
                    hash == hair_primary::kVelocityStaticFs;
  std::lock_guard<std::mutex> lock(g_mu);
  // A reused handle must never keep a hair hash (it would give a non-hair pipeline a strand variant).
  if (ours) g_mods[m] = hash;
  else g_mods.erase(m);
}

void hair_primary_forget_module(VkShaderModule m) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_mods.erase(m);
}

void hair_primary_note_pipelines(VkDevice device, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo* cis,
                                 const VkPipeline* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_mods.empty() || !g_dd || device != g_dd->device) return;
  const VkDeviceFuncs& vk = g_dd->vk;
  for (uint32_t i = 0; i < n; ++i) {
    const VkGraphicsPipelineCreateInfo& ci = cis[i];
    int fs = -1;
    uint64_t hash = 0;
    for (uint32_t k = 0; k < ci.stageCount; ++k)
      if (ci.pStages[k].stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        const auto it = g_mods.find(ci.pStages[k].module);
        if (it != g_mods.end()) fs = int(k), hash = it->second;
      }
    if (fs < 0 || !out[i] || !header_ready()) continue;
    const Pass pass = hash == hair_primary::kDeferredStaticFs ? Pass::Gbuffer
                    : hash == hair_primary::kVelocityStaticFs ? Pass::Velocity : Pass::Prepass;
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
      if (vk.CreateGraphicsPipelines(device, cache, 1, &native, nullptr, &fallback) == VK_SUCCESS) {
        auto old = g_fallback.find(out[i]);
        if (old != g_fallback.end()) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(old->second));
        g_fallback[out[i]] = fallback;
      }
    }
    if (pass == Pass::Gbuffer && !g_fallback.count(out[i])) g_depth_fallback_ready = false;
    if (library || dynamic_depth || !ds) {
      if (g_st.refused++ < 8)
        log("[hair primary] FS %016llx pipeline refused (%s): its draws keep the cards", (unsigned long long)hash,
            library ? "pipeline library" : dynamic_depth ? "dynamic depth state" : "no depth state");
      continue;
    }
    std::vector<VkPipelineShaderStageCreateInfo> stages(ci.pStages, ci.pStages + ci.stageCount);
    VkShaderModule mod = VK_NULL_HANDLE;
    const uint32_t spec_words[2] = {uint32_t(g_header.addr), uint32_t(g_header.addr >> 32)};
    const VkSpecializationMapEntry spec_map[2] = {{hair_primary::kSpecRecordLo, 0, 4}, {hair_primary::kSpecRecordHi, 4, 4}};
    const VkSpecializationInfo spec{2, spec_map, sizeof spec_words, spec_words};
    VkPipelineDepthStencilStateCreateInfo dsv = *ds;
    if (pass != Pass::Prepass) {
      Bg3ShaderBlob blob{};
      const char* file = pass == Pass::Gbuffer ? "rt/hair_primary_gbuffer.hlsl" : "rt/hair_primary_velocity.hlsl";
      if (!shader_get(file, "main", "ps_6_6", &blob)) {
        if (g_st.refused++ < 8) log("[hair primary] strand fragment shader unavailable: hair keeps its cards");
        continue;
      }
      VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      mci.codeSize = blob.size_bytes;
      mci.pCode = blob.code;
      if (vk.CreateShaderModule(device, &mci, nullptr, &mod) != VK_SUCCESS) continue;
      stages[size_t(fs)].module = mod;
      stages[size_t(fs)].pName = "main";
      stages[size_t(fs)].pSpecializationInfo = &spec;
      // G-buffer: strands write their own depth on top of whatever the prepass left (no card depth there).
      // Velocity: the strand depth the G-buffer wrote passes, nearer geometry does not; no writes.
      if (g_logged_pipes++ < 6)
        log("[hair primary] %s hair pipeline: native depth test %u write %u compare %d -> strands test 1 write %u "
            "compare GREATER_OR_EQUAL", pass == Pass::Gbuffer ? "G-buffer" : "velocity", ds->depthTestEnable,
            ds->depthWriteEnable, int(ds->depthCompareOp), unsigned(pass == Pass::Gbuffer));
      dsv.depthTestEnable = VK_TRUE;
      dsv.depthWriteEnable = pass == Pass::Gbuffer ? VK_TRUE : VK_FALSE;
      dsv.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    } else {
      dsv.depthWriteEnable = VK_FALSE;
    }
    VkGraphicsPipelineCreateInfo vci = ci;
    vci.pStages = stages.data();
    vci.pDepthStencilState = &dsv;
    vci.flags &= ~VkPipelineCreateFlags(VK_PIPELINE_CREATE_DERIVATIVE_BIT);
    vci.basePipelineHandle = VK_NULL_HANDLE;
    vci.basePipelineIndex = -1;
    VkPipeline v = VK_NULL_HANDLE;
    const VkResult r = vk.CreateGraphicsPipelines(device, cache, 1, &vci, nullptr, &v);
    if (mod) vk.DestroyShaderModule(device, mod, nullptr);
    if (r != VK_SUCCESS || !v) {
      if (g_st.refused++ < 8) log("[hair primary] variant pipeline creation failed (%d): hair keeps its cards", int(r));
      continue;
    }
    auto it = g_variant.find(out[i]);
    if (it != g_variant.end()) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(it->second));
    g_variant[out[i]] = v;
    g_pass_of[v] = pass;
    if (pass == Pass::Gbuffer) {
      Bg3ShaderBlob blob{};
      VkShaderModule pm = VK_NULL_HANDLE;
      if (!shader_get("rt/hair_primary_gbuffer.hlsl", "main", "ps_6_6", &blob)) continue;
      VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      mi.codeSize = blob.size_bytes; mi.pCode = blob.code;
      if (vk.CreateShaderModule(device, &mi, nullptr, &pm) != VK_SUCCESS) continue;
      stages[size_t(fs)].module = pm;
      vci.pStages = stages.data();
      DrawVariants variants;
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
        const uint32_t words[3] = {spec_words[0], spec_words[1], k};
        const VkSpecializationMapEntry map[3] = {spec_map[0], spec_map[1], {hair_primary::kSpecDrawSlot, 8, 4}};
        const VkSpecializationInfo draw_spec{3, map, sizeof words, words};
        stages[size_t(fs)].pSpecializationInfo = &draw_spec;
        dsv.depthWriteEnable = VK_TRUE;
        vci.pColorBlendState = ci.pColorBlendState;
        vk.CreateGraphicsPipelines(device, cache, 1, &vci, nullptr, &variants.strand[k]);
        dsv.depthWriteEnable = VK_FALSE;
        dsv.front.writeMask = dsv.back.writeMask = 0;
        vci.pColorBlendState = ci.pColorBlendState ? &blend : nullptr;
        vk.CreateGraphicsPipelines(device, cache, 1, &vci, nullptr, &variants.probe[k]);
      }
      vk.DestroyShaderModule(device, pm, nullptr);
      auto old = g_draw_variants.find(out[i]);
      if (old != g_draw_variants.end())
        for (uint32_t k = 0; k < hair_primary::kDrawSlots; ++k) {
          if (old->second.strand[k]) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(old->second.strand[k]));
          if (old->second.probe[k]) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(old->second.probe[k]));
        }
      g_draw_variants[out[i]] = variants;
    }
  }
}

void hair_primary_forget_pipeline(VkPipeline p) {
  std::lock_guard<std::mutex> lock(g_mu);
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
  g_pass_of.erase(it->second);
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(it->second));
  g_variant.erase(it);
}

uint32_t hair_primary_transmit(uint64_t key) {
  if (!key || cfg_int("rt_hair_groom_shadow_t", 0) == 0) return 0u;
  std::lock_guard<std::mutex> lock(g_mu);
  const auto it = g_transmit.find(key);
  const uint32_t w = it == g_transmit.end() ? 0u : it->second.packed();
  if (w) ++g_transmit_rows;
  return w;
}

HairPrimarySubmit hair_primary_submit_begin(const std::vector<VkCommandBuffer>& cmds) {
  HairPrimarySubmit recordings{};
  if (!g_startup_enabled) return recordings;
  std::lock_guard<std::mutex> lock(g_mu);
  for (VkCommandBuffer cmd : cmds) {
    const uint64_t recording = cmd_recording(cmd);
    for (int i = 0; i < kStaged; ++i) {
      const auto& st = g_staged[i];
      if (st.pick.tag && st.pick.cmd == uint64_t(cmd) && st.recording == recording) recordings.headers[i] = recording;
    }
  }
  return recordings;
}

void hair_primary_submit_end(const HairPrimarySubmit& recordings, bool success) {
  if (!g_startup_enabled || !success) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (int i = 0; i < kStaged; ++i)
    if (recordings.headers[i] && g_staged[i].recording == recordings.headers[i]) g_staged[i].submitted = true;
}

void hair_primary_fault_log() {   // after a device loss: the mapped breadcrumbs stay readable (no lock: the device is gone)
  crumbs_log("[fault]");
}

bool hair_primary_wanted() {
  return g_startup_enabled && cfg_hot(CfgHot::rt_hair_groom) != 0 && g_header.b != VK_NULL_HANDLE;
}

// g_mu held, outside a render pass, in the main G-buffer pass's own command buffer (hair_primary_before_pass): read the
// oldest timestamp slot (its frame is done: kTimeSlots - 1 frames back), then reset it for this frame. Reset here, not
// in the publish's command buffer: a G-buffer recorded before the publish was never timed (hc2 2026-11-04: strand
// draws every period, no 'strand G-buffer draws GPU' line).
static void timing_before_gbuffer(VkCommandBuffer cmd, uint64_t now) {
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
  if (!hair_primary_wanted()) return;
  if (p.depth_image && uint64_t(p.depth_image) == geo_gbuffer_depth_image()) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_dd) {
      observe_depth(uint64_t(p.depth_image), frame_index());
      if (!p.color_count && p.width == g_px_w && p.height == g_px_h && g_depth.gbuffer_seen) {
        for (uint64_t key : g_depth_resolved) if (!g_depth_failed.count(key)) g_depth.resolved(key);
        g_depth_resolved.clear(); g_depth_failed.clear();
        for (const auto& [key, frame] : g_depth.previous) {
          if (g_depth_orphan_count == 4) break;
          g_depth_orphans[g_depth_orphan_count++] = {key, frame, frame_index()};
        }
        g_st.orphan += g_depth.begin();
      }
      if (p.color_count >= 4 && p.width == g_px_w && p.height == g_px_h) {
        g_depth.gbuffer();
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
  if (g_applied_frame == frame) return;   // once per frame: every G-buffer pass of the frame reads one header
  g_applied_frame = frame;
  hair_primary::Staged picks[kStaged];
  for (int i = 0; i < kStaged; ++i) {
    const auto& st = g_staged[i];
    const bool same_recording = st.pick.cmd == uint64_t(cmd) && st.recording == cmd_recording(cmd) &&
                                bvh_rec_state(st.pick.rec) == 0;
    picks[i] = st.submitted || same_recording ? st.pick : hair_primary::Staged{};
  }
  int i = hair_primary::pick_publish(picks, kStaged, uint64_t(cmd), [](uint64_t rec) { return bvh_rec_submitted(rec); });
  // This frame's or the previous frame's publish (the strand shaders rebase it to this camera, hp_rebase); an older one
  // may reference ranges freed by a LOD change (bvhsplit kGroomFreeLag covers 2 frames of age, not more).
  if (i >= 0 && g_staged[i].pick.tag + 1 < frame) i = -1;
  if (i < 0) {
    g_live_keys.clear();   // no swap this frame; the prepass of the next one keeps its cards too
    if (g_order_none++ % 300 == 0)
      log("[hair primary] G-buffer pass with no groom publish ordered before it: cards this frame (%llu such passes)",
          (unsigned long long)g_order_none);
    return;
  }
  apply_header(cmd, g_staged[i]);
  GbFrame& gf = g_gb_frames[frame % 16];
  gf.frame = frame;

  gf.tag = g_staged[i].pick.tag;
  for (const GbFrame& older : g_gb_frames) if (older.frame && older.frame != frame && older.tag == gf.tag)
    for (uint64_t key : older.keys) if (key) g_coverage.invalidate(key, frame);

  std::memcpy(gf.keys, g_staged[i].keys, sizeof gf.keys);
  std::memcpy(gf.draw_keys, g_staged[i].slots.keys, sizeof gf.draw_keys);
}

bool hair_primary_swap(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  if (args.kind != 1 || !s->pipe[0] || !hair_primary_wanted()) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  s->hp_coverage = s->hp_query = UINT32_MAX;
  const uint64_t now = frame_index();
  if (s->pass.width != g_px_w || s->pass.height != g_px_h ||
      uint64_t(s->pass.depth_image) != geo_gbuffer_depth_image()) return false;
  const auto it = g_variant.find(s->pipe[0]);
  const uint64_t key = hair_primary_draw_key(s, args);
  if (it == g_variant.end()) {
    if (s->pass.color_count >= 4) {
      if (g_live_keys.count(key)) g_coverage.invalidate(key, now + 1);
      if (g_depth.needs_card_depth(key, false)) {
        const auto fb = g_fallback.find(s->pipe[0]);
        if (fb != g_fallback.end()) {
          g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fb->second);
          ++g_fallbacks; g_depth_resolved.insert(key); return true;
        }
        g_depth_failed.insert(key);
      }
    }
    return false;
  }
  const Pass pass = g_pass_of[it->second];
  const uint32_t draw = g_live_slots.find(key);
  if (draw == hair_primary::kDrawSlots && g_live_keys.count(key)) g_without_slot.insert(key);
  const auto dv = g_draw_variants.find(s->pipe[0]);
  // Native cards receive a masked replay; swapped strands write the same two markers directly.
  const bool probe = pass == Pass::Gbuffer && g_order && g_applied_frame == now + 1 && g_live_keys.count(key) &&
                     !g_load.quiet(now) && dv != g_draw_variants.end() && draw < hair_primary::kDrawSlots && dv->second.probe[draw];
  bool allowed = !(g_published + 2 < now + 1 || g_load.quiet(now) || !g_coverage.allow(key, now + 1)) &&
                 (pass == Pass::Gbuffer ? g_gate.allow_gbuffer(key) : pass == Pass::Velocity ? g_gate.allow_velocity(key)
                                                                                         : g_gate.allow_prepass(key));
  // Header order prepass (rt_hair_groom_primary_order 0): the G-buffer may read another frame's header than the probe
  // proved (hc2 2026-11-04: adjacent tags in 296 of 300 counted frames, the player half bald). Cards, fail closed.
  // Header order prepass: no pass swaps (the G-buffer would read another frame's header than the evidence; a prepass
  // clone without card depth next to card G-buffer draws loses the hair: order turned off after order-1 swaps).
  if (!g_order || draw == hair_primary::kDrawSlots) allowed = false;
  const int passes = cfg_int("rt_hair_primary_passes", 15);
  {   // rt_hair_primary_passes (diagnostic bitmask, default 15): 1 G-buffer, 2 velocity, 4 prepass clone, 8 coverage probe
    if (!(passes & (pass == Pass::Gbuffer ? 1 : pass == Pass::Velocity ? 2 : 4))) allowed = false;
  }
  if (g_order && pass != Pass::Prepass && (g_applied_frame != now + 1 || !g_live_keys.count(key))) allowed = false;
  // The prepass drops the card depth only after a G-buffer pass applied a header holding the key: the most recent
  // G-buffer apply (g_applied_frame: frame_index + 1 of that G-buffer). The game records the next frame's prepass before
  // the present that advances frame_index, so that apply is this frame's number or the next (an equality with now
  // refused every prepass: hv7 2026-11-04, 0 prepass strand draws next to 187 G-buffer ones).
  if (g_order && pass == Pass::Prepass && (!g_depth_fallback_ready || !g_depth_safe_keys.count(key) || g_applied_frame < now || g_applied_frame > now + 1 || !g_live_keys.count(key)))
    allowed = false;
  if (pass == Pass::Gbuffer && !probe && !allowed && g_live_keys.count(key)) g_coverage.invalidate(key, now + 1);
  if (pass == Pass::Gbuffer && g_fallback.count(s->pipe[0])) g_depth_safe_keys.insert(key);
  if (pass == Pass::Gbuffer && (draw == hair_primary::kDrawSlots || dv == g_draw_variants.end() || !dv->second.strand[draw])) allowed = false;
  if (pass == Pass::Gbuffer && g_depth.needs_card_depth(key, allowed)) {
    const auto fb = g_fallback.find(s->pipe[0]);
    if (fb == g_fallback.end()) { g_depth_failed.insert(key); return false; }
    g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fb->second);
    ++g_fallbacks;
    g_depth_resolved.insert(key);
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
    g_depth_resolved.insert(key);
    auto& period = g_coverage_period[key];
    if (period.last_swap != now) { ++period.swapped; period.last_swap = now; }
  }
  auto& decision = g_coverage_period[key];
  const double used = g_coverage.latest.at(key).ratio();
  if (!decision.decisions++) decision.used_min = decision.used_max = used;
  decision.used_min = (std::min)(decision.used_min, used);
  decision.used_max = (std::max)(decision.used_max, used);
  g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass == Pass::Gbuffer ? dv->second.strand[draw] : it->second);
  ++(pass == Pass::Gbuffer ? g_st.gbuffer : pass == Pass::Velocity ? g_st.velocity : g_st.prepass);
  if (pass == Pass::Prepass) g_depth.prepass(key, now);
  if (probe) g_coverage_period[key].probed = true;   // the swapped strand draw writes its pixel entries itself
  const uint32_t ts = uint32_t(now % kTimeSlots);
  if (pass == Pass::Gbuffer && g_qp && g_slot_frame[ts] == now + 1 && g_timed[ts] < kTimed) {
    s->hp_query = (ts * kTimed + g_timed[ts]++) * 2u;
    g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, s->hp_query);
  }
  return true;
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

void hair_primary_publish(VkCommandBuffer cmd, uint64_t rec, uint64_t tlas, uint64_t pool, uint64_t pool_bytes,
                          const hair_primary::Row* rows, const uint64_t* keys, const uint8_t* lods, uint32_t count,
                          uint32_t width, uint32_t height, const float* viewport) {
  if (!hair_primary_wanted()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  DeviceData* dd = g_dd;
  if (!dd) return;
  count = (std::min)(count, hair_primary::kMaxGrooms);
  const uint64_t now = frame_index();
  observe_depth(geo_gbuffer_depth_image(), now);
  if (!count || !tlas || !width || !height || !viewport || viewport[2] == 0.0f || viewport[3] == 0.0f) {
    g_gate.clear();
    return;
  }
  if (hair_primary::coverage_bytes(width, height) > UINT32_MAX) { g_gate.clear(); return; }
  if (width != g_px_w || height != g_px_h || !g_pixels.b) {
    release(g_pixels);
    g_px_w = g_px_h = 0;
    if (!make(g_pixels, hair_primary::coverage_bytes(width, height),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "hair primary pixels")) {
      log("[hair primary] pixel record %ux%u could not be allocated: hair keeps its cards", width, height);
      g_gate.clear();
      return;
    }
    g_px_w = width;
    g_px_h = height;
    g_coverage.clear();
    g_pixels_fresh = true;
    log("[hair primary] pixel record %ux%u: %.1f MB", width, height, double(g_pixels.size) / 1048576.0);
  }
  if (!count_feedback(cmd, now)) {
    if (g_st.refused++ < 8) log("[hair primary] strand feedback unavailable: hair keeps its cards");
    g_gate.clear();
    return;
  }
  if (g_pixels_fresh) {   // tag 0 is never a frame tag
    dd->vk.CmdFillBuffer(cmd, g_pixels.b, 0, VK_WHOLE_SIZE, 0u);
    dd->vk.CmdFillBuffer(cmd, g_header.b, hair_primary::kColourOffset, hair_primary::kMaxGrooms * hair_primary::kColourBytes, 0u);
    g_pixels_fresh = false;
  }
  hair_primary::Header h{};
  h.tag = uint32_t(now + 1);
  h.flags = cfg_int("rt_hair_groom_tips", 0) != 0 ? hair_primary::kFlagTips : 0u;
  h.width = width;
  h.height = height;
  h.tlas = tlas;
  h.pool = pool;
  h.pool_bytes = uint32_t((std::min)(pool_bytes, uint64_t(UINT32_MAX)));
  h.pixels = g_pixels.addr;
  std::memcpy(h.viewport, viewport, sizeof h.viewport);
  StagedHeader st;
  // The build camera: the origin of this publish's camera-relative groom points (the strand passes of the next frame
  // rebase their rays to their own camera). Without it the strand shaders keep no strand (fail closed). Called inside
  // the scene build: rt_after_capture holds the scene lock (rt_build_camera would lock it again).
  if (rt_this_build_camera(now, st.cam) && std::isfinite(st.cam[0]) && std::isfinite(st.cam[1]) && std::isfinite(st.cam[2]))
    h.flags |= hair_primary::kFlagCamera;
  // The rows' previous points are the last update's: the publish of the previous frame, with its build camera.
  if (g_last_pub_camera_frame && g_last_pub_camera_frame + 1 == now) {
    std::memcpy(st.cam + 4, g_last_pub_camera, sizeof g_last_pub_camera);
    h.flags |= hair_primary::kFlagPrevCamera;
  }
  g_last_pub_camera_frame = (h.flags & hair_primary::kFlagCamera) ? now : 0;
  std::memcpy(g_last_pub_camera, st.cam, sizeof g_last_pub_camera);
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
  g_gate.publish(keys, (std::min)(count, hair_primary::kMaxGrooms), now);
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
      log("[hair primary] strand traces in counted frame %llu: %u traced pixel entries, %u with a strand; sampled ray: "
          "origin (%.4f %.4f %.4f), card distance %.3f, direction (%.4f %.4f %.4f), committed %.0f, hit distance %.3f, "
          "groom %.0f, segment %.0f, tag %.0f, camera (%.3f %.3f %.3f); counted with tag %u, header tag now %u; strand pixels "
          "with tag %u/%u/%u/%u: %u/%u/%u/%u, evidence from tag %u (G-buffer frame %llu: "
          "same-pass unique pixel counts)",
          (unsigned long long)g_seen.frame, g_seen.traced,
          g_seen.hit, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14],
          g_seen.tag, g_last_tag, g_seen.tag, g_seen.tag - 1, g_seen.tag - 2, g_seen.tag - 3, g_seen.tag_totals[0],
          g_seen.tag_totals[1], g_seen.tag_totals[2], g_seen.tag_totals[3], g_seen.evidence_tag,
          (unsigned long long)g_seen.evidence_frame);
    }
    log("[hair primary] counted frame %llu: strand pixels per groom (LOD, draw key prefix):%s; pool-bound refusals %u "
        "(session %llu); counted frames with adjacent-tag pixels %llu (session %llu), header order %s",
        (unsigned long long)g_seen.frame, at ? per : " none", refused, (unsigned long long)g_refused_total,
        (unsigned long long)g_adjacent_window, (unsigned long long)g_adjacent_total, g_order ? "G-buffer" : "prepass");
    g_adjacent_window = 0;
  }
  if (stats_due(now)) {
    log("[hair primary] frame %llu: %u grooms published; last %u frames: %llu publishes, %llu prepass, %llu "
        "G-buffer and %llu velocity strand draws, %llu pipelines refused; prepass card depth dropped without its G-buffer "
        "swap %llu",
        (unsigned long long)now, count, stats_period(300), (unsigned long long)g_st.publishes, (unsigned long long)g_st.prepass,
        (unsigned long long)g_st.gbuffer, (unsigned long long)g_st.velocity, (unsigned long long)g_st.refused,
        (unsigned long long)g_st.orphan);
    char per[320] = {};
    size_t at = 0;
    for (uint32_t i = 0; i < g_depth_orphan_count; ++i) {
      const auto& o = g_depth_orphans[i];
      at += std::snprintf(per + at, sizeof per - at, " %04llx:prepass=%llu,drop=%llu",
          (unsigned long long)(o.key >> 48), (unsigned long long)o.prepass, (unsigned long long)o.drop);
    }
    log("[hair primary] depth orphans (last %u frames, first 4; key prefix:prepass frame_index,drop frame_index):%s",
        stats_period(300), at ? per : " none");
    g_depth_orphan_count = 0;
  }
  if (stats_due(now) && cfg_int("rt_hair_groom_shadow_t", 0) != 0) {
    // rt_hair_groom_shadow_t participation: the draws with a measured a_f and the groom rows that carried one.
    float lo = 1.0f, hi = 0.0f, sum[3] = {0, 0, 0};
    for (const auto& [key, t] : g_transmit) {
      const float m = (t.a[0] + t.a[1] + t.a[2]) / 3.0f;
      lo = (std::min)(lo, m), hi = (std::max)(hi, m);
      for (int c = 0; c < 3; ++c) sum[c] += t.a[c];
    }
    const float n = g_transmit.empty() ? 1.0f : float(g_transmit.size());
    log("[hair primary] shadow transmittance: %zu draws measured, a_f mean %.3f %.3f %.3f (per draw %.3f..%.3f); "
        "last %u frames: %llu groom rows with a_f",
        g_transmit.size(), sum[0] / n, sum[1] / n, sum[2] / n, g_transmit.empty() ? 0.0f : lo, hi,
        stats_period(300), (unsigned long long)g_transmit_rows);
    g_transmit_rows = 0;
  }
  if (stats_due(now) && g_ms_frames)
    log("[hair primary] strand G-buffer draws GPU %.3f ms/frame (average of %u frames, per-draw timestamps)",
        g_ms_sum / g_ms_frames, g_ms_frames);
  if (stats_due(now)) g_st.orphan = 0, g_st.publishes = g_st.prepass = g_st.gbuffer = g_st.velocity = 0, g_ms_sum = 0, g_ms_frames = 0;
}

}  // namespace bg3
