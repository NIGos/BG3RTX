// rt_hair_groom_primary (docs/RTX_HAIR_PRIMARY.md): the visible hair of matched static CHAR_Hair draws becomes the
// strand groom. At pipeline creation every static hair pipeline gets a variant: the camera Depth prepass a clone
// without depth writes (the cards leave no depth; their XFB capture is unchanged), the G-buffer the strand fragment
// shader shaders/rt/hair_primary_gbuffer.hlsl with depth GREATER_OR_EQUAL + writes (reversed Z), the velocity pass
// (FS b70cadc5) rt/hair_primary_velocity.hlsl (strand motion from last frame's points, no depth writes). At draw time a
// matched draw (its index range is in the groom set of the latest build) is drawn with the variant. The strand
// shader reads the frame record (common/hair_primary.h) through the header address baked as specialization
// constants 1100/1101; bvhsplit publishes the header after the groom TLAS build (hair_primary_publish).
// Variants exist only with linear swept spheres, shaderInt64 and fragmentStoresAndAtomics; the key is a live A/B.
#include "host.h"

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
bool g_frag_stores = false;            // fragmentStoresAndAtomics enabled by the game
std::mutex g_mu;
HpBuf g_header, g_pixels;              // header: fixed (its address is in every variant); pixels: render size
bool g_header_tried = false;
bool g_pixels_fresh = false;           // just allocated: zero it before the next header update
uint32_t g_px_w = 0, g_px_h = 0;
std::unordered_map<VkShaderModule, uint64_t> g_mods;           // the two native hair FS modules
std::unordered_map<VkPipeline, VkPipeline> g_variant;         // game pipeline -> variant
enum class Pass : uint8_t { Prepass, Gbuffer, Velocity };
std::unordered_map<VkPipeline, Pass> g_pass_of;               // variant -> its pass
// Index ranges of the grooms in the last two published TLASes: a draw is swapped only when its groom is in both, so
// the prepass (recorded before this frame's build) and the G-buffer (after it) agree on a draw that just appeared.
std::unordered_set<uint64_t> g_keys, g_keys_prev;
uint64_t g_published = 0;                                     // frame_index + 1 of the last publish with grooms
struct Stats { uint64_t prepass = 0, gbuffer = 0, velocity = 0, refused = 0, publishes = 0; } g_st;
uint64_t g_logged_pipes = 0;

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
  bool stores = ci->pEnabledFeatures && ci->pEnabledFeatures->fragmentStoresAndAtomics;
  for (auto p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
      stores |= reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p)->features.fragmentStoresAndAtomics != VK_FALSE;
  g_frag_stores = stores;
}

void hair_primary_device_destroyed(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (dd != g_dd) return;
  for (auto& [game, v] : g_variant) dd->vk.DestroyPipeline(dd->device, v, nullptr);
  g_variant.clear();
  g_pass_of.clear();
  g_mods.clear();
  for (HpBuf* b : {&g_header, &g_pixels}) {
    if (b->b) dd->vk.DestroyBuffer(dd->device, b->b, nullptr);
    if (b->m) dd->vk.FreeMemory(dd->device, b->m, nullptr);
    *b = HpBuf{};
  }
  g_dd = nullptr;
  g_header_tried = false;
}

void hair_primary_note_module(VkShaderModule m, uint64_t hash) {
  if (hash != hair_primary::kDeferredStaticFs && hash != hair_primary::kDepthStaticFs &&
      hash != hair_primary::kVelocityStaticFs)
    return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_mods[m] = hash;
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
  return cfg_hot(CfgHot::rt_hair_groom_primary) != 0 && cfg_hot(CfgHot::rt_hair_groom) != 0 && g_header.b != VK_NULL_HANDLE;
}

bool hair_primary_swap(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  if (args.kind != 1 || !s->pipe[0] || !hair_primary_wanted()) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  const auto it = g_variant.find(s->pipe[0]);
  // A groom published this or last frame (the build of this frame may still be recorded on another thread).
  if (it == g_variant.end() || g_published + 2 < frame_index() + 1) return false;
  // Only the main camera's passes (portraits and other small passes keep their cards: the record is the main view's).
  if (s->pass.width != g_px_w || s->pass.height != g_px_h) return false;
  const uint64_t key = hair_primary_draw_key(s, args);
  if (!g_keys.count(key) || !g_keys_prev.count(key)) return false;
  g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
  const Pass pass = g_pass_of[it->second];
  ++(pass == Pass::Gbuffer ? g_st.gbuffer : pass == Pass::Velocity ? g_st.velocity : g_st.prepass);
  return true;
}

void hair_primary_unswap(VkCommandBuffer cmd, const CmdState* s) {
  if (g_dd && s->pipe[0]) g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipe[0]);
}

void hair_primary_publish(VkCommandBuffer cmd, uint64_t tlas, uint64_t pool, const hair_primary::Row* rows,
                          const uint64_t* keys, uint32_t count, uint32_t width, uint32_t height, const float* viewport) {
  if (!hair_primary_wanted()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  DeviceData* dd = g_dd;
  if (!dd) return;
  const uint64_t now = frame_index();
  if (!count || !tlas || !width || !height || !viewport || viewport[2] == 0.0f || viewport[3] == 0.0f) {
    g_keys.clear();
    g_keys_prev.clear();
    return;
  }
  if (width != g_px_w || height != g_px_h || !g_pixels.b) {
    release(g_pixels);
    g_px_w = g_px_h = 0;
    if (!make(g_pixels, hair_primary::pixel_bytes(width, height),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "hair primary pixels")) {
      log("[hair primary] pixel record %ux%u could not be allocated: hair keeps its cards", width, height);
      g_keys.clear();
      g_keys_prev.clear();
      return;
    }
    g_px_w = width;
    g_px_h = height;
    g_pixels_fresh = true;
    log("[hair primary] pixel record %ux%u: %.1f MB", width, height, double(g_pixels.size) / 1048576.0);
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
  g_keys_prev.swap(g_keys);
  g_keys.clear();
  for (uint32_t i = 0; i < count && i < hair_primary::kMaxGrooms; ++i)
    if (keys[i]) g_keys.insert(keys[i]);
  g_published = now + 1;
  ++g_st.publishes;
  if (now % 300 == 0)
    log("[hair primary] frame %llu: %u grooms published; last 300 frames: %llu publishes, %llu prepass, %llu "
        "G-buffer and %llu velocity strand draws, %llu pipelines refused",
        (unsigned long long)now, count, (unsigned long long)g_st.publishes, (unsigned long long)g_st.prepass,
        (unsigned long long)g_st.gbuffer, (unsigned long long)g_st.velocity, (unsigned long long)g_st.refused);
  if (now % 300 == 0) g_st.publishes = g_st.prepass = g_st.gbuffer = g_st.velocity = 0;
}

}  // namespace bg3
