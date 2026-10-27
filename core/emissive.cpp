// D-EM debug views (rt_debug 16-18 (modes 6-8), docs/TRACK_D_RT.md 13): primary rays against the
// host's emissive scene (Bg3HostApi::rt_emissive, shaders/rt/em_common.hlsli), written
// over the lit diffuse at the end of the tiled lighting like the other rt_debug views,
// and compared with the game's own effects target (shaders/rt/em_debug.hlsl):
//   6 the emission our data gives at the primary hit (black elsewhere; compose then adds
//     the game's effects target on top, so emissive surfaces show about twice as bright
//     where the two agree);
//   7 black: the final image is the game's effects target alone (the reference for 6);
//   8 ratio ours/game where both see emission: green within 25%, red ours brighter, blue
//     ours darker; magenta where the game shows emission and our data has none.
// The statistics go into the build's buffer; the host logs them ("[em] projected check").
#include <cstring>

#include "core.h"

namespace {

VkDescriptorSetLayout g_push = VK_NULL_HANDLE, g_empty = VK_NULL_HANDLE, g_em_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE;
uint64_t g_gen = 0;
bool g_logged = false;

struct Push {
  uint32_t full[2];
  uint32_t mode;
  float tol;        // relative depth agreement of a visible emissive hit
  float thr;        // effects-target luminance counted as emission (rt_em_fx_thr)
  uint32_t has_cam2;   // binding 6 holds the lighting's set 0 b2 (absolute camera position)
  float pad[2];
  uint32_t fam;      // rt_em_dbg_fam: views 19/20 (modes 9/10)
  uint32_t pad2[3];
};

bool ensure(VkDescriptorSetLayout em_layout) {
  const VkDeviceFuncs& vk = *host->vk;
  VkDevice dev = host->device;
  if (g_layout && g_em_layout != em_layout) {   // the host's layout never changes in practice
    host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_pipe);
    host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_layout);
    g_pipe = VK_NULL_HANDLE;
    g_layout = VK_NULL_HANDLE;
  }
  if (!g_push) {
    VkDescriptorSetLayoutBinding b[7] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = 7;
    ci.pBindings = b;
    if (vk.CreateDescriptorSetLayout(dev, &ci, nullptr, &g_push) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutCreateInfo ei{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (vk.CreateDescriptorSetLayout(dev, &ei, nullptr, &g_empty) != VK_SUCCESS) return false;
  }
  if (!g_layout) {
    VkDescriptorSetLayout sets[4] = {g_push, g_empty, g_empty, em_layout};
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 4;
    li.pSetLayouts = sets;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vk.CreatePipelineLayout(dev, &li, nullptr, &g_layout) != VK_SUCCESS) return false;
    g_em_layout = em_layout;
    g_gen = 0;
  }
  Bg3ShaderBlob blob{};
  if (!host->get_shader("rt/em_debug.hlsl", "main", "cs_6_6", &blob)) return g_pipe != VK_NULL_HANDLE;
  if (g_pipe && blob.generation == g_gen) return true;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (vk.CreateShaderModule(dev, &mci, nullptr, &mod) != VK_SUCCESS) return g_pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main",
               nullptr};
  pci.layout = g_layout;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = vk.CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &p);
  vk.DestroyShaderModule(dev, mod, nullptr);
  if (r != VK_SUCCESS) {
    host->log("em_debug: pipeline creation failed (%d)", r);
    return g_pipe != VK_NULL_HANDLE;
  }
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_pipe);
  g_pipe = p;
  g_gen = blob.generation;
  return true;
}

void layout_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  host->vk->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                               0, nullptr, 1, &b);
}

}  // namespace

void em_debug(VkCommandBuffer cmd, const Bg3Binding& cam, const Bg3Binding& vp, const Bg3Binding& depth,
              const Bg3Binding& diffuse, const Bg3Binding& specular, const Bg3Binding& cam2, uint32_t w, uint32_t h,
              uint32_t mode) {
  if (!host->rt_emissive || (mode != 0 && (mode < 6 || mode > 10))) return;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  Bg3EmissiveInfo info{};
  if (!host->rt_emissive(cmd, &layout, &set, &info) || !set || !layout || !info.fx_view || !info.fx_image ||
      info.fx_width != w || info.fx_height != h || info.built_frame + 3 < host->frame_index())
    return;
  // The effects target is sampled, then left as its last pass left it (unknown: no view).
  const VkImageLayout fx_from = info.fx_layout;
  if (fx_from == VK_IMAGE_LAYOUT_UNDEFINED || info.fx_image == diffuse.image || info.fx_image == specular.image ||
      !ensure(layout))
    return;
  const VkDeviceFuncs& vk = *host->vk;
  const bool move = fx_from != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && fx_from != VK_IMAGE_LAYOUT_GENERAL;
  const VkImageLayout fx_in = move ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : fx_from;
  if (move) layout_barrier(cmd, info.fx_image, fx_from, fx_in);
  VkDescriptorBufferInfo cb{cam.buffer, cam.offset, cam.range}, vb{vp.buffer, vp.offset, vp.range};
  VkDescriptorImageInfo img[4] = {{VK_NULL_HANDLE, depth.view, depth.layout},
                                  {VK_NULL_HANDLE, diffuse.view, VK_IMAGE_LAYOUT_GENERAL},
                                  {VK_NULL_HANDLE, specular.view, VK_IMAGE_LAYOUT_GENERAL},
                                  {VK_NULL_HANDLE, info.fx_view, fx_in}};
  // The lighting's set 0 b2 (absolute camera position at byte 208), else the camera CB as a stand-in.
  const bool has_cam2 = cam2.buffer && cam2.range >= 224;
  VkDescriptorBufferInfo c2 = has_cam2 ? VkDescriptorBufferInfo{cam2.buffer, cam2.offset, 224} : cb;
  VkWriteDescriptorSet wr[7];
  for (uint32_t i = 0; i < 7; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstBinding = i;
    wr[i].descriptorCount = 1;
    if (i < 2 || i == 6) {
      wr[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      wr[i].pBufferInfo = i == 6 ? &c2 : i ? &vb : &cb;
    } else {
      wr[i].descriptorType = (i == 3 || i == 4) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      wr[i].pImageInfo = &img[i - 2];
    }
  }
  Push pc{{w, h}, mode, host->cfg_float("rt_em_tol", 0.01f), host->cfg_float("rt_em_fx_thr", 0.05f),
          has_cam2 ? 1u : 0u, {float(host->cfg_int("rt_em_filter", 0)), host->cfg_float("rt_em_aniso", 16.0f)},
          // The family's FS hash, top 32 bits, as two 16-bit decimal keys (cfg_int is decimal):
          // 304be1c7 is rt_em_dbg_fam_hi=12363 (0x304b), rt_em_dbg_fam_lo=57799 (0xe1c7).
          (uint32_t(host->cfg_int("rt_em_dbg_fam_hi", 0) & 0xffff) << 16) | uint32_t(host->cfg_int("rt_em_dbg_fam_lo", 0) & 0xffff),
          {0, 0, 0}};
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  // The scene was built by the host's own submission after the previous frame's (AS builds, compute).
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_layout, 0, 7, wr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_layout, 3, 1, &set, 0, nullptr);
  vk.CmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr,
                        0, nullptr);
  if (move) layout_barrier(cmd, info.fx_image, fx_in, fx_from);
  if (!g_logged) {
    g_logged = true;
    host->log("em_debug: mode %u at %ux%u; %u records, %u instances, %u lights; effects target layout %d", mode, w, h,
              info.records, info.instances, info.lights, fx_from);
  }
}

// The projected check without a view (mode 0: statistics only), every rt_em_check_frames
// frames, for the host's per-family calibration (rt_em_calib).
bool em_check_due() {
  if (!host->cfg_int("rt_em_calib", 1)) return false;
  const int every = host->cfg_int("rt_em_check_frames", 16);
  return every > 0 && host->frame_index() % uint64_t(every) == 0;
}

void em_release() {
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_pipe);
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_push);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_empty);
  g_pipe = VK_NULL_HANDLE;
  g_layout = VK_NULL_HANDLE;
  g_push = g_empty = g_em_layout = VK_NULL_HANDLE;
  g_logged = false;
}
