// Track C v0: screen-space one-bounce diffuse GI added into BG3's diffuse
// lighting buffer right after its tiled-lighting compute and before the
// subsurface-scattering pass (docs/TRACK_C_GI.md).
//
// At every indirect dispatch the core asks the host what the game has bound;
// the tiled-lighting shader is recognised by its resources (set 1: RG16F
// normals at b1, albedo b2, material b3, R32F linear depth b5, R11G11B10F
// diffuse/specular storage outputs b10/b11; set 0: camera UBO b1, viewport
// UBO b4). Those exact descriptors are re-used by the GI passes, which run at
// the start of the next render pass in the same command buffer.
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "core.h"

namespace {

struct Snapshot {
  Bg3Binding cam, vp, nrm, alb, mat, dep, dif, spe;
};

struct Pipe {
  VkPipeline pipeline = VK_NULL_HANDLE;
  uint64_t generation = 0;
};

struct Push {
  uint32_t full[2], half[2];
  uint32_t frame;
  float strength, radius, thickness;
  uint32_t rays, steps, debug;
  float history;
};

std::mutex g_mu;
std::unordered_map<VkCommandBuffer, Snapshot> g_pending;
VkDescriptorSetLayout g_set_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
Pipe g_trace, g_temporal, g_composite;
Bg3Image g_raw{}, g_hist[2]{};
uint32_t g_w = 0, g_h = 0, g_hw = 0, g_hh = 0, g_parity = 0;
bool g_images_ready = false, g_logged = false;
uint64_t g_last_frame_run = 0;

bool snapshot(VkCommandBuffer cmd, Snapshot& s) {
  auto q = [&](uint32_t set, uint32_t b, Bg3Binding& o) {
    return host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, set, b, 0, &o);
  };
  if (!q(1, 10, s.dif) || s.dif.format != VK_FORMAT_B10G11R11_UFLOAT_PACK32 || !s.dif.view) return false;
  if (!q(1, 11, s.spe) || s.spe.format != VK_FORMAT_B10G11R11_UFLOAT_PACK32 || !s.spe.view) return false;
  if (!q(1, 1, s.nrm) || s.nrm.format != VK_FORMAT_R16G16_SFLOAT) return false;
  if (!q(1, 2, s.alb) || !q(1, 3, s.mat) || !q(1, 5, s.dep) || s.dep.format != VK_FORMAT_R32_SFLOAT) return false;
  if (!q(0, 1, s.cam) || !s.cam.buffer || !q(0, 4, s.vp) || !s.vp.buffer) return false;
  return s.nrm.width == s.dif.width && s.dep.width == s.dif.width && s.dif.width > 0;
}

void release_images() {
  for (Bg3Image* im : {&g_raw, &g_hist[0], &g_hist[1]}) {
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)im->view);
    host->defer_destroy(BG3_DEFER_IMAGE, (uint64_t)im->image);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)im->memory);
    *im = Bg3Image{};
  }
  g_images_ready = false;
  g_w = g_h = 0;
}

bool ensure_images(uint32_t w, uint32_t h) {
  if (w == g_w && h == g_h && g_raw.image) return true;
  release_images();
  uint32_t hw = (w + 1) / 2, hh = (h + 1) / 2;
  VkImageUsageFlags use = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (!host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_raw) ||
      !host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_hist[0]) ||
      !host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_hist[1])) {
    host->log("gi: image allocation failed");
    release_images();
    return false;
  }
  g_w = w, g_h = h, g_hw = hw, g_hh = hh;
  host->log("gi: resources for %ux%u (GI at %ux%u)", w, h, hw, hh);
  return true;
}

bool ensure_layout() {
  if (g_layout) return true;
  VkDescriptorSetLayoutBinding b[11];
  for (uint32_t i = 0; i < 11; ++i) {
    VkDescriptorType t = i <= 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                       : i <= 5 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[i] = {i, t, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  }
  VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  sci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  sci.bindingCount = 11;
  sci.pBindings = b;
  if (host->vk->CreateDescriptorSetLayout(host->device, &sci, nullptr, &g_set_layout) != VK_SUCCESS) return false;
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 64};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = 1;
  lci.pSetLayouts = &g_set_layout;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  return host->vk->CreatePipelineLayout(host->device, &lci, nullptr, &g_layout) == VK_SUCCESS;
}

bool refresh(Pipe& p, const char* file) {
  Bg3ShaderBlob blob{};
  if (!host->get_shader(file, "main", "cs_6_6", &blob)) return p.pipeline != VK_NULL_HANDLE;
  if (p.pipeline && blob.generation == p.generation) return true;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, &mci, 0, VK_SHADER_STAGE_COMPUTE_BIT,
               VK_NULL_HANDLE, "main", nullptr};
  pci.layout = g_layout;
  VkPipeline np = VK_NULL_HANDLE;
  if (host->vk->CreateComputePipelines(host->device, VK_NULL_HANDLE, 1, &pci, nullptr, &np) != VK_SUCCESS) {
    host->log("gi: %s pipeline creation failed; keeping the previous one", file);
    return p.pipeline != VK_NULL_HANDLE;
  }
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p.pipeline);
  p.pipeline = np;
  p.generation = blob.generation;
  host->log("gi: %s generation %llu", file, (unsigned long long)blob.generation);
  return true;
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void run(VkCommandBuffer cmd, const Snapshot& s) {
  const VkDeviceFuncs& vk = *host->vk;
  if (!ensure_layout() || !ensure_images(s.dif.width, s.dif.height)) return;
  bool ok = refresh(g_trace, "gi/gi_trace.hlsl") & refresh(g_temporal, "gi/gi_temporal.hlsl") &
            refresh(g_composite, "gi/gi_composite.hlsl");
  if (!ok) return;

  if (!g_images_ready) {
    VkImageMemoryBarrier ib[3];
    Bg3Image* ims[3] = {&g_raw, &g_hist[0], &g_hist[1]};
    for (int i = 0; i < 3; ++i) {
      ib[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      ib[i].srcAccessMask = 0;
      ib[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      ib[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      ib[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
      ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      ib[i].image = ims[i]->image;
      ib[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                          0, nullptr, 3, ib);
    g_images_ready = true;
  }

  Bg3Image& hin = g_hist[g_parity];
  Bg3Image& hout = g_hist[g_parity ^ 1];
  VkDescriptorBufferInfo cam{s.cam.buffer, s.cam.offset, s.cam.range};
  VkDescriptorBufferInfo vpb{s.vp.buffer, s.vp.offset, s.vp.range};
  VkDescriptorImageInfo img[9] = {
      {VK_NULL_HANDLE, s.nrm.view, s.nrm.layout}, {VK_NULL_HANDLE, s.alb.view, s.alb.layout},
      {VK_NULL_HANDLE, s.mat.view, s.mat.layout}, {VK_NULL_HANDLE, s.dep.view, s.dep.layout},
      {VK_NULL_HANDLE, s.dif.view, VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, s.spe.view, VK_IMAGE_LAYOUT_GENERAL},
      {VK_NULL_HANDLE, g_raw.view, VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, hin.view, VK_IMAGE_LAYOUT_GENERAL},
      {VK_NULL_HANDLE, hout.view, VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet w[11];
  for (uint32_t i = 0; i < 11; ++i) {
    w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[i].dstBinding = i;
    w[i].descriptorCount = 1;
    if (i <= 1) {
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w[i].pBufferInfo = i == 0 ? &cam : &vpb;
    } else {
      w[i].descriptorType = i <= 5 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      w[i].pImageInfo = &img[i - 2];
    }
  }

  Push pc{};
  pc.full[0] = g_w, pc.full[1] = g_h, pc.half[0] = g_hw, pc.half[1] = g_hh;
  pc.frame = uint32_t(host->frame_index());
  pc.strength = host->cfg_float("gi_strength", 1.0f);
  pc.radius = host->cfg_float("gi_radius", 0.5f);
  pc.thickness = host->cfg_float("gi_thickness", 0.03f);
  pc.rays = uint32_t(host->cfg_int("gi_rays", 2));
  pc.steps = uint32_t(host->cfg_int("gi_steps", 12));
  pc.debug = uint32_t(host->cfg_int("gi_debug", 0));
  pc.history = host->cfg_float("gi_history", 0.1f);

  // The tiled lighting just wrote diffuse/specular from compute.
  barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  auto dispatch = [&](Pipe& p, uint32_t w_, uint32_t h_) {
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
    vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_layout, 0, 11, w);
    vk.CmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
    vk.CmdDispatch(cmd, (w_ + 7) / 8, (h_ + 7) / 8, 1);
  };
  dispatch(g_trace, g_hw, g_hh);
  barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  dispatch(g_temporal, g_hw, g_hh);
  barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  dispatch(g_composite, g_w, g_h);
  // The SSS pass samples the diffuse buffer in its fragment shader.
  barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  g_parity ^= 1;
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

  if (!g_logged) {
    g_logged = true;
    host->log("gi: first injection, %ux%u; diffuse layout %d, normals layout %d, depth layout %d, cam UBO +%llu/%llu",
              g_w, g_h, s.dif.layout, s.nrm.layout, s.dep.layout, (unsigned long long)s.cam.offset,
              (unsigned long long)s.cam.range);
  }
}

}  // namespace

// Logs what a few indirect dispatches have bound, to diagnose recognition.
void describe(VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  static int n = 0;
  if (n >= host->cfg_int("gi_describe", 0)) return;
  ++n;
  char line[1024];
  int len = snprintf(line, sizeof line, "gi: dispatch cs %016llx:", (unsigned long long)d->cs_hash);
  const uint32_t probes[][2] = {{0, 1}, {0, 2}, {0, 4}, {1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 5}, {1, 9}, {1, 10}, {1, 11}};
  for (auto& p : probes) {
    Bg3Binding b{};
    bool ok = host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p[0], p[1], 0, &b);
    len += snprintf(line + len, sizeof line - len, " s%ub%u=%s", p[0], p[1],
                    !ok ? "-" : b.buffer ? "buf" : b.view ? "img" : "?");
    if (ok && b.view) len += snprintf(line + len, sizeof line - len, "(f%d %ux%u L%d)", b.format, b.width, b.height, b.layout);
    if (ok && b.buffer) len += snprintf(line + len, sizeof line - len, "(t%d +%llu)", b.type, (unsigned long long)b.offset);
  }
  host->log("%s", line);
}

void gi_on_dispatch(VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  if (!d->indirect || !host->cfg_int("gi", 0)) return;
  describe(cmd, d);
  Snapshot s;
  if (!snapshot(cmd, s)) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_pending[cmd] = s;
}

// The tiled lighting and the SSS pass that consumes it live in different
// command buffers, so the GI runs at the end of the lighting's own buffer, or
// at the first render pass after it if that comes first.
static void flush(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_pending.find(cmd);
  if (it == g_pending.end()) return;
  Snapshot s = it->second;
  g_pending.erase(it);
  uint64_t f = host->frame_index();
  if (f == g_last_frame_run || !host->cfg_int("gi", 0)) return;  // once per frame
  g_last_frame_run = f;
  run(cmd, s);
}

void gi_on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  if (p->indirect_dispatches_before) flush(cmd);
}

void gi_on_cmd_end(VkCommandBuffer cmd) { flush(cmd); }

void gi_on_frame(uint64_t) {}

void gi_release() {
  std::lock_guard<std::mutex> lock(g_mu);
  release_images();
  for (Pipe* p : {&g_trace, &g_temporal, &g_composite}) {
    host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p->pipeline);
    *p = Pipe{};
  }
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_set_layout);
  g_layout = VK_NULL_HANDLE;
  g_set_layout = VK_NULL_HANDLE;
  g_pending.clear();
}
