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
    if (!total && other_total && g_misaligned_logged++ % 300 == 0)
      log("[hair primary] counted frame %llu: its %u strand pixels carry an adjacent frame's tag (the G-buffer ran "
          "against another publish): no strike (%llu such frames)", (unsigned long long)s.frame, other_total,
          (unsigned long long)g_misaligned_logged);
    for (uint64_t key : g_gate.feedback_frame(s.keys, s.drew, s.map, other, hair_primary::kMaxGrooms)) {
      // The reason, with what every groom of the counted frame got: 0 everywhere (no ray found a strand), refusals
      // (the pool bound), or 0 next to grooms with pixels (off screen or behind another groom: retried later).
      char per[512];
      int at = 0;
      uint32_t refused = 0, lod = 0;
      for (uint32_t j = 0; j < hair_primary::kMaxGrooms && at < 470; ++j)
        if (s.keys[j]) {
          at += std::snprintf(per + at, sizeof per - at, " %u:%u+%u/LOD%u%s", j, s.map[j], other[j], unsigned(s.lods[j]),
                              s.keys[j] == key ? "*" : "");
          if (s.keys[j] == key) refused += g_seen.refused[j], lod = s.lods[j];
        }
      const auto& r = g_gate.failed[key];
      log("[hair primary] draw %016llx: its strands drew no pixel in %u frames: its cards come back for %llu frames "
          "(LOD %u, counted frame %llu: %u strand pixels in all, %u refused; per groom (counted+adjacent tag):%s)",
          (unsigned long long)key, hair_primary::kStrikes, (unsigned long long)r.wait, lod, (unsigned long long)s.frame,
          total, refused, at ? per : " none");
    }
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
  if (g_header_tried) return g_header.b != VK_NULL_HANDLE;
  g_header_tried = true;
  const auto* info = static_cast<const Bg3DeviceInfo*>(persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  const char* why = !g_dd ? "no device" : !rt_enabled() ? "rt=0" : !hair_groom::lss_enabled() ? "no linear swept spheres"
                  : !info || !(info->features & BG3_DEV_INT64) ? "shaderInt64 not enabled (sharc_device=1 enables it)"
                  : !g_frag_stores ? "fragmentStoresAndAtomics not enabled by the game" : nullptr;
  if (!why && !make(g_header, hair_primary::kHeaderBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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
                        s == VK_DYNAMIC_STATE_DEPTH_COMPARE_OP;
    }
  return ci.pDepthStencilState;
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
  VkPhysicalDeviceProperties props{};
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
  g_ts_period = props.limits.timestampPeriod > 0 ? props.limits.timestampPeriod : 1.0f;
}

void hair_primary_device_destroyed(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (dd != g_dd) return;
  for (auto& [game, v] : g_variant) dd->vk.DestroyPipeline(dd->device, v, nullptr);
  g_variant.clear();
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
  for (HpBuf* b : {&g_header, &g_pixels}) {
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
  }
}

void hair_primary_forget_pipeline(VkPipeline p) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_variant.find(p);
  if (it == g_variant.end()) return;
  g_pass_of.erase(it->second);
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(it->second));
  g_variant.erase(it);
}

bool hair_primary_wanted() {
  return g_startup_enabled && cfg_hot(CfgHot::rt_hair_groom) != 0 && g_header.b != VK_NULL_HANDLE;
}

bool hair_primary_swap(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  if (args.kind != 1 || !s->pipe[0] || !hair_primary_wanted()) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  const auto it = g_variant.find(s->pipe[0]);
  // A groom published this or last frame (the build of this frame may still be recorded on another thread).
  if (it == g_variant.end() || g_published + 2 < frame_index() + 1 || g_load.quiet(frame_index())) return false;
  // Only the main camera's passes (portraits and other small passes keep their cards: the record is the main view's).
  if (s->pass.width != g_px_w || s->pass.height != g_px_h) return false;
  const uint64_t key = hair_primary_draw_key(s, args);
  const Pass pass = g_pass_of[it->second];
  if (!(pass == Pass::Gbuffer ? g_gate.allow_gbuffer(key) : pass == Pass::Velocity ? g_gate.allow_velocity(key)
                                                                                    : g_gate.allow_prepass(key)))
    return false;
  if (pass == Pass::Gbuffer) g_gate.gbuffer_drew(key);
  g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
  ++(pass == Pass::Gbuffer ? g_st.gbuffer : pass == Pass::Velocity ? g_st.velocity : g_st.prepass);
  s->hp_query = UINT32_MAX;
  const uint64_t now = frame_index();
  const uint32_t slot = uint32_t(now % kTimeSlots);
  // Timed only after this frame's publish reset the slot (GPU order: the reset is in the prepass's buffer).
  if (pass == Pass::Gbuffer && g_qp && g_slot_frame[slot] == now + 1 && g_timed[slot] < kTimed) {
    s->hp_query = (slot * kTimed + g_timed[slot]++) * 2u;
    g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, s->hp_query);
  }
  return true;
}

void hair_primary_unswap(VkCommandBuffer cmd, CmdState* s) {
  if (!g_dd) return;
  if (s->hp_query != UINT32_MAX) g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, s->hp_query + 1);
  s->hp_query = UINT32_MAX;
  if (s->pipe[0]) g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipe[0]);
}

void hair_primary_publish(VkCommandBuffer cmd, uint64_t tlas, uint64_t pool, uint64_t pool_bytes, const hair_primary::Row* rows,
                          const uint64_t* keys, const uint8_t* lods, uint32_t count, uint32_t width, uint32_t height,
                          const float* viewport) {
  if (!hair_primary_wanted()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  DeviceData* dd = g_dd;
  if (!dd) return;
  const uint64_t now = frame_index();
  if (g_load.observe(geo_gbuffer_depth_image(), now)) {
    // A level load: nothing of the old scene is trusted (see LoadGuard).
    g_gate.clear();
    g_last_tag = 0;
    g_pixels_fresh = true;
    for (CountSlot& s : g_cnt) s.valid = false;
    log("[hair primary] level load (new G-buffer images): strand swaps paused for %llu frames, groom state reset",
        (unsigned long long)hair_primary::kLoadQuietFrames);
  }
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
  // Timing: read the oldest slot (its frame is done: kTimeSlots - 1 frames back), then reset it for this frame.
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
  uint8_t bytes[hair_primary::kHeaderBytes] = {};
  const uint32_t n = hair_primary::pack(h, rows, count, bytes);
  // Earlier frames' fragment reads of the header/pixels before this frame's writes, then this frame's TLAS build,
  // pool writes and header update before the G-buffer pass's fragment shaders.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr,
                            0, nullptr);
  dd->vk.CmdUpdateBuffer(cmd, g_header.b, 0, n, bytes);
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  dd->vk.CmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  g_gate.publish(keys, (std::min)(count, hair_primary::kMaxGrooms), now);
  g_last_tag = h.tag;
  g_last_n = width * height;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) g_last_keys[k] = k < count ? keys[k] : 0;
  for (uint32_t k = 0; k < hair_primary::kMaxGrooms; ++k) g_last_lods[k] = k < count ? lods[k] : 0;
  g_published = now + 1;
  ++g_st.publishes;
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
    log("[hair primary] counted frame %llu: strand pixels per groom (LOD, draw key prefix):%s; pool-bound refusals %u "
        "(session %llu)", (unsigned long long)g_seen.frame, at ? per : " none", refused, (unsigned long long)g_refused_total);
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
