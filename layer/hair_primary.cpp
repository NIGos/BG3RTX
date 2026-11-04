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
};

DeviceData* g_dd = nullptr;
bool g_startup_enabled = false;        // Restart safety gate; INI reload cannot enable strands.
bool g_frag_stores = false;            // fragmentStoresAndAtomics enabled by the game
std::mutex g_mu;
HpBuf g_probe_header;
HpBuf g_header, g_pixels;              // header: fixed (its address is in every variant); pixels: render size
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
// Precise coverage query pairs. Each slot belongs to one prepass command buffer: the
// reset and every query it arms are ordered on the GPU in that very recording.
hair_primary::CoverageGate g_coverage;
bool g_precise = false;
PFN_vkCmdBeginQuery g_begin_query = nullptr;
PFN_vkCmdEndQuery g_end_query = nullptr;
VkQueryPool g_coverage_qp = VK_NULL_HANDLE;
constexpr uint32_t kCoverageSlots = uint32_t(hair_primary::kCoverageLatency), kProbeDraws = 128;
struct ProbeSlot {
  uint64_t frame = 0, cmd = 0, recording = 0, submitted_frame = 0;
  uint32_t n = 0;
  bool ready = false;
  uint64_t keys[kProbeDraws] = {};
  std::unordered_set<uint64_t> header_keys, incomplete_keys;
};
ProbeSlot g_probe_slots[kCoverageSlots];
std::unordered_map<VkPipeline, VkPipeline> g_probe;
struct CoveragePeriod { bool probed = false; uint32_t swapped = 0; uint64_t last_swap = UINT64_MAX; };
std::unordered_map<uint64_t, CoveragePeriod> g_coverage_period;
uint64_t g_published = 0;                                     // frame_index + 1 of the last publish with grooms
struct Stats { uint64_t prepass = 0, gbuffer = 0, velocity = 0, refused = 0, publishes = 0; } g_st;
uint64_t g_logged_pipes = 0;
// Fail-safe feedback (SwapGate::feedback): strand pixels per groom of the last frame's pixel record, counted on the GPU
// (rt/hair_primary_count.hlsl) at the next publish, copied to a host-visible ring and read kCountSlots - 1 publishes
// later. With them the keys and whether each one's G-buffer variant ran in the counted frame.
constexpr uint32_t kCountSlots = 4;
struct CountSlot { uint64_t frame = 0; bool valid = false; uint64_t keys[hair_primary::kMaxGrooms] = {};
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
                   uint32_t pixels[hair_primary::kMaxGrooms] = {}, refused[hair_primary::kMaxGrooms] = {}; };
CountSeen g_seen;
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
  uint64_t keys[hair_primary::kMaxGrooms] = {};
  uint8_t lods[hair_primary::kMaxGrooms] = {};
};
constexpr int kStaged = 3;
StagedHeader g_staged[kStaged];
int g_staged_next = 0;
bool g_order = false;                       // the last publish was staged (the key on)
uint64_t g_applied_frame = 0;               // frame_index + 1 of the last G-buffer pass that applied (or refused)
std::unordered_set<uint64_t> g_live_keys;   // the keys of the header the G-buffer reads (order mode: swaps need them)
uint64_t g_order_none = 0;                  // G-buffer passes with no publish ordered before them (no swap)
void apply_header(VkCommandBuffer cmd, const StagedHeader& st);
// GPU cost of the strand G-buffer draws: a timestamp pair around each (BOTTOM_OF_PIPE: the draw's own completion
// after the work before it), kTimed draws per frame, a ring of kTimeSlots frames; read kTimeSlots - 1 frames later.
constexpr uint32_t kTimed = 64, kTimeSlots = 4;
VkQueryPool g_qp = VK_NULL_HANDLE;
float g_ts_period = 1.0f;
uint32_t g_timed[kTimeSlots] = {};      // pairs written in each slot's frame
uint64_t g_slot_frame[kTimeSlots] = {}; // frame_index + 1 the slot was reset for
double g_ms_sum = 0;                    // strand G-buffer draw ms over the timed frames of the window
uint32_t g_ms_frames = 0;

bool make(HpBuf& out, VkDeviceSize size, VkBufferUsageFlags usage, const char* what) {
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
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out = HpBuf{};
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
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
    for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) {
      g_seen.keys[k] = s.keys[k];
      g_seen.lods[k] = s.lods[k];
      g_seen.pixels[k] = s.map[k];
      g_seen.refused[k] = s.map[hair_primary::kRefusedOffset / 4 + k];
      other[k] = s.map[hair_primary::kOtherOffset / 4 + k];
      g_refused_total += g_seen.refused[k];
      total += s.map[k];
      other_total += other[k];
    }
    // A counted frame whose strand pixels all carry an adjacent frame's tag: the G-buffer ran against another publish
    // (submission order). No strike from it; logged so a live run shows how often (rate-limited).
    if (other_total) ++g_adjacent_window;   // counted frames with adjacent-tag strand pixels (the 300-frame line)
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
  const uint32_t push[4] = {g_last_tag, g_last_n, uint32_t(g_header.addr), uint32_t(g_header.addr >> 32)};
  dd->vk.CmdPushConstants(cmd, g_cnt_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
  dd->vk.CmdDispatch(cmd, (g_last_n + 255) / 256, 1, 1);
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0,
                            nullptr);
  const VkBufferCopy c{0, 0, hair_primary::kCountBytes};
  dd->vk.CmdCopyBuffer(cmd, g_counts.b, s.rb, 1, &c);
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) {
    s.keys[k] = g_last_keys[k];
    s.lods[k] = g_last_lods[k];
    s.drew[k] = g_last_keys[k] && g_gate.gbuffer_now.count(g_last_keys[k]) != 0;   // the counted frame's G-buffer
  }
  s.frame = now;
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
  if (g_header_tried) return g_header.b != VK_NULL_HANDLE && g_probe_header.b != VK_NULL_HANDLE;
  g_header_tried = true;
  const auto* info = static_cast<const Bg3DeviceInfo*>(persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  const char* why = !g_dd ? "no device" : !rt_enabled() ? "rt=0" : !hair_groom::lss_enabled() ? "no linear swept spheres"
                  : !info || !(info->features & BG3_DEV_INT64) ? "shaderInt64 not enabled (sharc_device=1 enables it)"
                  : !g_frag_stores ? "fragmentStoresAndAtomics not enabled by the game"
                  : !g_precise ? "occlusionQueryPrecise not enabled: no coverage proof, no swaps" : nullptr;
  if (!why && !make(g_header, hair_primary::kHeaderBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    "hair primary header"))
    why = "header buffer allocation failed";
  if (why) {
    log("[hair primary] UNAVAILABLE (%s): matched hair keeps its cards", why);
    return false;
  }
  if (!make(g_probe_header, hair_primary::kHeaderBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "hair primary probe header")) return false;
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
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  dd->vk.CmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  g_last_tag = st.pick.tag;
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
  for (auto& slot : g_probe_slots) { slot.ready = false; slot.n = 0; slot.header_keys.clear(); }
  for (auto& st : g_staged) st.pick = {};
  g_live_keys.clear();
  g_last_tag = 0;
  g_pixels_fresh = true;
  log("[hair primary] level load (first or new G-buffer images): strand swaps paused for %llu frames, groom state reset",
      (unsigned long long)hair_primary::kLoadQuietFrames);
}

// g_mu held. Read complete pairs only; unavailable queries never manufacture evidence.
// Do not recycle an in-flight slot even if its query values happen to be available.
void coverage_before_prepass(VkCommandBuffer cmd) {
  const uint64_t now = frame_index();
  ProbeSlot& slot = g_probe_slots[now % kCoverageSlots];
  if (slot.frame == now + 1) return;
  if (slot.frame) {
    const bool still_recorded = cmd_recording(reinterpret_cast<VkCommandBuffer>(slot.cmd)) == slot.recording;
    if (slot.submitted_frame && life_gpu_done() <= slot.submitted_frame + 1) return;
    // Pending recordings may still be submitted; completed reusable recordings may
    // be submitted again. Keep their query indices until reset/free invalidates them.
    if (still_recorded) return;
  }
  if (!g_coverage_qp) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_OCCLUSION;
    qi.queryCount = kCoverageSlots * kProbeDraws * 2;
    if (g_dd->vk.CreateQueryPool(g_dd->device, &qi, nullptr, &g_coverage_qp) != VK_SUCCESS) return;
  }
  if (slot.frame && slot.submitted_frame && slot.n) {
    struct Sum { uint64_t card = 0, strand = 0; bool available = true; };
    std::unordered_map<uint64_t, Sum> sums;
    uint64_t r[kProbeDraws * 4] = {};
    const VkResult result = g_dd->vk.GetQueryPoolResults(g_dd->device, g_coverage_qp,
        uint32_t(now % kCoverageSlots) * kProbeDraws * 2, slot.n * 2, sizeof r, r, 16,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result == VK_SUCCESS || result == VK_NOT_READY) {
      for (uint32_t i = 0; i < slot.n; ++i) {
        auto& sum = sums[slot.keys[i]];
        sum.card += r[4*i]; sum.strand += r[4*i+2];
        sum.available = sum.available && r[4*i+1] && r[4*i+3];
      }
      for (const auto& [key, sum] : sums)
        if (sum.available && !slot.incomplete_keys.count(key)) g_coverage.observe(key, slot.frame, sum.card, sum.strand);
    }
  }
  slot = ProbeSlot{};
  slot.frame = now + 1;
  slot.cmd = uint64_t(cmd);
  slot.recording = cmd_recording(cmd);
  const uint32_t first = uint32_t(now % kCoverageSlots) * kProbeDraws * 2;
  g_dd->vk.CmdResetQueryPool(cmd, g_coverage_qp, first, kProbeDraws * 2);
  // A separate immutable-for-this-pass record avoids the G-buffer header's adjacent-tag race.
  hair_primary::Staged picks[kStaged];
  for (int i = 0; i < kStaged; ++i) {
    const auto& st = g_staged[i];
    const bool submitted = st.submitted && bvh_rec_submitted(st.pick.rec);
    const bool same_recording = st.pick.cmd == uint64_t(cmd) && st.recording == cmd_recording(cmd) &&
                                bvh_rec_state(st.pick.rec) == 0;
    picks[i] = submitted || same_recording ? st.pick : hair_primary::Staged{};
  }
  const int i = hair_primary::pick_publish(picks, kStaged, uint64_t(cmd), [](uint64_t rec) { return bvh_rec_submitted(rec); });
  if (i < 0) return;
  const auto& st = g_staged[i];
  if (st.pick.tag + 2 < now + 1) return; // header lifetime, distinct from readback evidence freshness
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb, 0, nullptr, 0, nullptr);
  g_dd->vk.CmdUpdateBuffer(cmd, g_probe_header.b, 0, st.n, st.bytes);
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 1, &mb, 0, nullptr, 0, nullptr);
  for (uint64_t key : st.keys) if (key) slot.header_keys.insert(key);
  slot.ready = true;
}

void coverage_log(uint64_t now) {
  if (now % 300) return;
  char per[1200] = {};
  size_t at = 0;
  uint32_t shown = 0, probed = 0, swapped = 0, below = 0;
  double minimum = 0.0;
  bool first = true;
  for (const auto& [key, period] : g_coverage_period) {
    probed += period.probed;
    swapped += period.swapped != 0;
    const auto it = g_coverage.latest.find(key);
    const hair_primary::CoverageGate::Evidence e = it == g_coverage.latest.end() ? hair_primary::CoverageGate::Evidence{} : it->second;
    const double ratio = e.ratio();
    if (first || ratio < minimum) minimum = ratio;
    first = false;
    below += !e.card || ratio < hair_primary::kCoverMin;
    if (shown++ < 12) at += std::snprintf(per + at, sizeof per - at, " %04llx:%u/%u=%.3f,%u",
        (unsigned long long)(key >> 48), unsigned((std::min)(e.strand, uint64_t(UINT32_MAX))),
        unsigned((std::min)(e.card, uint64_t(UINT32_MAX))), ratio, period.swapped);
  }
  log("[hair primary] coverage (last 300 frames): %u draws probed, %u swapped; per draw (key prefix: strand/card samples = ratio, swapped frames):%s; ratio min %.3f, bound %.3f, draws below bound %u",
      probed, swapped, per, minimum, hair_primary::kCoverMin, below);
  g_coverage_period.clear();
}

}  // namespace

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
  g_precise = ci->pEnabledFeatures && ci->pEnabledFeatures->occlusionQueryPrecise;
  for (auto p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
      g_precise |= reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p)->features.occlusionQueryPrecise != VK_FALSE;
  g_begin_query = reinterpret_cast<PFN_vkCmdBeginQuery>(dd->gdpa(dd->device, "vkCmdBeginQuery"));
  g_end_query = reinterpret_cast<PFN_vkCmdEndQuery>(dd->gdpa(dd->device, "vkCmdEndQuery"));
  g_precise = g_precise && g_begin_query && g_end_query;
  VkPhysicalDeviceProperties props{};
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
  g_ts_period = props.limits.timestampPeriod > 0 ? props.limits.timestampPeriod : 1.0f;
}

void hair_primary_device_destroyed(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (dd != g_dd) return;
  for (auto& [game, v] : g_variant) dd->vk.DestroyPipeline(dd->device, v, nullptr);
  g_variant.clear();
  for (auto& [game, v] : g_probe) dd->vk.DestroyPipeline(dd->device, v, nullptr);
  g_probe.clear();
  if (g_coverage_qp) dd->vk.DestroyQueryPool(dd->device, g_coverage_qp, nullptr);
  g_coverage_qp = VK_NULL_HANDLE;
  for (auto& slot : g_probe_slots) slot = ProbeSlot{};
  g_coverage.clear();
  g_coverage_period.clear();
  for (auto& st : g_staged) st = StagedHeader{};
  g_staged_next = 0;
  g_live_keys.clear();
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
  for (HpBuf* b : {&g_header, &g_pixels, &g_probe_header}) {
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
    if (pass == Pass::Prepass) {
      Bg3ShaderBlob blob{};
      if (!shader_get("rt/hair_primary_probe.hlsl", "main", "ps_6_6", &blob)) continue;
      VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      mi.codeSize = blob.size_bytes; mi.pCode = blob.code;
      VkShaderModule pm = VK_NULL_HANDLE;
      if (vk.CreateShaderModule(device, &mi, nullptr, &pm) != VK_SUCCESS) continue;
      const uint32_t words[2] = {uint32_t(g_probe_header.addr), uint32_t(g_probe_header.addr >> 32)};
      const VkSpecializationInfo pspec{2, spec_map, sizeof words, words};
      stages[size_t(fs)].module = pm;
      stages[size_t(fs)].pName = "main";
      stages[size_t(fs)].pSpecializationInfo = &pspec;
      // Preserve native depth test, sample count and culling. No depth, stencil or colour stores.
      dsv.front.writeMask = dsv.back.writeMask = 0;
      VkPipelineColorBlendStateCreateInfo blend{};
      std::vector<VkPipelineColorBlendAttachmentState> attachments;
      if (ci.pColorBlendState) {
        blend = *ci.pColorBlendState;
        if (blend.attachmentCount) attachments.assign(blend.pAttachments, blend.pAttachments + blend.attachmentCount);
        for (auto& a : attachments) a.colorWriteMask = 0;
        blend.pAttachments = attachments.data();
        vci.pColorBlendState = &blend;
      }
      VkPipeline probe = VK_NULL_HANDLE;
      const VkResult pr = vk.CreateGraphicsPipelines(device, cache, 1, &vci, nullptr, &probe);
      vk.DestroyShaderModule(device, pm, nullptr);
      if (pr == VK_SUCCESS && probe) {
        auto old = g_probe.find(out[i]);
        if (old != g_probe.end()) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(old->second));
        g_probe[out[i]] = probe;
      }
    }
  }
}

void hair_primary_forget_pipeline(VkPipeline p) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto probe = g_probe.find(p);
  if (probe != g_probe.end()) {
    defer_destroy(BG3_DEFER_PIPELINE, uint64_t(probe->second));
    g_probe.erase(probe);
  }
  auto it = g_variant.find(p);
  if (it == g_variant.end()) return;
  g_pass_of.erase(it->second);
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(it->second));
  g_variant.erase(it);
}

HairPrimarySubmit hair_primary_submit_begin(const std::vector<VkCommandBuffer>& cmds) {
  HairPrimarySubmit recordings{};
  if (!g_startup_enabled) return recordings;
  std::lock_guard<std::mutex> lock(g_mu);
  for (VkCommandBuffer cmd : cmds) {
    const uint64_t recording = cmd_recording(cmd);
    for (uint32_t i = 0; i < kCoverageSlots; ++i) {
      const auto& slot = g_probe_slots[i];
      if (slot.frame && slot.cmd == uint64_t(cmd) && slot.recording == recording) recordings.queries[i] = recording;
    }
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
  for (uint32_t i = 0; i < kCoverageSlots; ++i)
    if (recordings.queries[i] && g_probe_slots[i].recording == recordings.queries[i])
      g_probe_slots[i].submitted_frame = frame_index() + 1;
  for (int i = 0; i < kStaged; ++i)
    if (recordings.headers[i] && g_staged[i].recording == recordings.headers[i]) g_staged[i].submitted = true;
}

bool hair_primary_wanted() {
  return g_startup_enabled && cfg_hot(CfgHot::rt_hair_groom) != 0 && g_header.b != VK_NULL_HANDLE && g_probe_header.b != VK_NULL_HANDLE;
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
      if (!p.color_count && p.width == g_px_w && p.height == g_px_h && g_precise) coverage_before_prepass(cmd);
      if (p.color_count >= 4 && p.width == g_px_w && p.height == g_px_h) timing_before_gbuffer(cmd, frame_index());
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
  const int i = hair_primary::pick_publish(picks, kStaged, uint64_t(cmd), [](uint64_t rec) { return bvh_rec_submitted(rec); });
  if (i < 0) {
    g_live_keys.clear();   // no swap this frame; the prepass of the next one keeps its cards too
    if (g_order_none++ % 300 == 0)
      log("[hair primary] G-buffer pass with no groom publish ordered before it: cards this frame (%llu such passes)",
          (unsigned long long)g_order_none);
    return;
  }
  apply_header(cmd, g_staged[i]);   // a staged publish may serve again (no newer one ordered before)
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
  ProbeSlot& slot = g_probe_slots[now % kCoverageSlots];
  if (it == g_variant.end()) {
    // An unsupported sibling material sharing the same index range must not be
    // omitted from a key's denominator while its supported siblings qualify it.
    if (!s->pass.color_count && (g_gate.published.count(key) || slot.header_keys.count(key))) {
      if (slot.frame == now + 1) slot.incomplete_keys.insert(key);
      g_coverage.invalidate(key, now + 1);
    }
    return false;
  }
  const Pass pass = g_pass_of[it->second];
  // Probe before any swap is allowed. The card query wraps the real native draw or its
  // no-depth-write clone; the duplicate trace draw follows after transform feedback ends.
  if (pass == Pass::Prepass && !s->pass.color_count && g_probe.count(s->pipe[0]) &&
      slot.ready && slot.frame == now + 1 && slot.cmd == uint64_t(cmd) &&
      slot.recording == cmd_recording(cmd) && slot.header_keys.count(key)) {
    if (slot.n >= kProbeDraws || s->hp_native_queries) {
      // Never qualify a key from a partial set of its draws in this frame.
      slot.incomplete_keys.insert(key);
      g_coverage.invalidate(key, now + 1);
      return false;
    }
    s->hp_coverage = (uint32_t(now % kCoverageSlots) * kProbeDraws + slot.n) * 2;
    slot.keys[slot.n++] = key;
    g_coverage_period[key].probed = true;
    g_begin_query(cmd, g_coverage_qp, s->hp_coverage, VK_QUERY_CONTROL_PRECISE_BIT);
  }
  if (pass == Pass::Prepass && s->hp_coverage == UINT32_MAX) {
    // Another prepass recording / refused probe / exhausted slot cannot contribute
    // only the successful subset of draws that share this index-range key.
    if (slot.frame == now + 1) slot.incomplete_keys.insert(key);
    g_coverage.invalidate(key, now + 1);
    return false;
  }
  const bool probing = s->hp_coverage != UINT32_MAX;
  if (g_published + 2 < now + 1 || g_load.quiet(now) ||
      !g_coverage.allow(key, now + 1)) return probing;
  if (!(pass == Pass::Gbuffer ? g_gate.allow_gbuffer(key) : pass == Pass::Velocity ? g_gate.allow_velocity(key)
                                                                                    : g_gate.allow_prepass(key))) return probing;
  // Header order prepass (rt_hair_groom_primary_order 0): the G-buffer may read another frame's header than the probe
  // proved (hc2 2026-11-04: adjacent tags in 296 of 300 counted frames, the player half bald). Cards, fail closed.
  if (!g_order && pass != Pass::Prepass) return probing;
  if (g_order && pass != Pass::Prepass && (g_applied_frame != now + 1 || !g_live_keys.count(key))) return probing;
  if (g_order && pass == Pass::Prepass && (g_applied_frame != now || !g_live_keys.count(key))) return probing;
  if (pass == Pass::Gbuffer) {
    g_gate.gbuffer_drew(key);
    auto& period = g_coverage_period[key];
    if (period.last_swap != now) { ++period.swapped; period.last_swap = now; }
  }
  g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
  ++(pass == Pass::Gbuffer ? g_st.gbuffer : pass == Pass::Velocity ? g_st.velocity : g_st.prepass);
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
    g_end_query(cmd, g_coverage_qp, s->hp_coverage);
    const auto it = g_probe.find(s->pipe[0]);
    if (it != g_probe.end()) {
      g_begin_query(cmd, g_coverage_qp, s->hp_coverage + 1, VK_QUERY_CONTROL_PRECISE_BIT);
      g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
      g_dd->vk.CmdDrawIndexed(cmd, args.count, args.instances, args.first, args.vertex_offset, args.first_instance);
      g_end_query(cmd, g_coverage_qp, s->hp_coverage + 1);
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
  const uint64_t now = frame_index();
  observe_depth(geo_gbuffer_depth_image(), now);
  if (!count || !tlas || !width || !height || !viewport || viewport[2] == 0.0f || viewport[3] == 0.0f) {
    g_gate.clear();
    return;
  }
  if (width != g_px_w || height != g_px_h || !g_pixels.b) {
    release(g_pixels);
    g_px_w = g_px_h = 0;
    if (!make(g_pixels, hair_primary::pixel_bytes(width, height),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "hair primary pixels")) {
      log("[hair primary] pixel record %ux%u could not be allocated: hair keeps its cards", width, height);
      g_gate.clear();
      return;
    }
    g_px_w = width;
    g_px_h = height;
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
    g_pixels_fresh = false;
  }
  hair_primary::Header h{};
  h.tag = uint32_t(now + 1);
  h.width = width;
  h.height = height;
  h.tlas = tlas;
  h.pool = pool;
  h.pool_bytes = uint32_t((std::min)(pool_bytes, uint64_t(UINT32_MAX)));
  h.pixels = g_pixels.addr;
  std::memcpy(h.viewport, viewport, sizeof h.viewport);
  StagedHeader st;
  st.pick = {h.tag, rec, uint64_t(cmd)};
  st.recording = cmd_recording(cmd);
  st.n = hair_primary::pack(h, rows, count, st.bytes);
  st.pixels = width * height;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) st.keys[k] = k < count ? keys[k] : 0;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) st.lods[k] = k < count ? lods[k] : 0;
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
  if (now % 300 == 0 && g_seen.frame) {
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
    log("[hair primary] counted frame %llu: strand pixels per groom (LOD, draw key prefix):%s; pool-bound refusals %u "
        "(session %llu); counted frames with adjacent-tag pixels %llu (session %llu), header order %s",
        (unsigned long long)g_seen.frame, at ? per : " none", refused, (unsigned long long)g_refused_total,
        (unsigned long long)g_adjacent_window, (unsigned long long)g_adjacent_total, g_order ? "G-buffer" : "prepass");
    g_adjacent_window = 0;
  }
  if (now % 300 == 0)
    log("[hair primary] frame %llu: %u grooms published; last 300 frames: %llu publishes, %llu prepass, %llu "
        "G-buffer and %llu velocity strand draws, %llu pipelines refused",
        (unsigned long long)now, count, (unsigned long long)g_st.publishes, (unsigned long long)g_st.prepass,
        (unsigned long long)g_st.gbuffer, (unsigned long long)g_st.velocity, (unsigned long long)g_st.refused);
  if (now % 300 == 0 && g_ms_frames)
    log("[hair primary] strand G-buffer draws GPU %.3f ms/frame (average of %u frames, per-draw timestamps)",
        g_ms_sum / g_ms_frames, g_ms_frames);
  if (now % 300 == 0) g_st.publishes = g_st.prepass = g_st.gbuffer = g_st.velocity = 0, g_ms_sum = 0, g_ms_frames = 0;
}

}  // namespace bg3
