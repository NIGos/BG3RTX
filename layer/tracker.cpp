// Always-on, cheap bookkeeping of the objects the frame graph refers to:
// shader modules (by SPIR-V hash), pipelines, images, views, framebuffers,
// render passes and descriptor set contents.
#include "host.h"

#include <cstring>

namespace bg3 {

using HookMap = std::unordered_map<std::string, PFN_vkVoidFunction>;

Tracker& tracker() {
  static Tracker t;
  return t;
}

static bool is_image_type(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || t == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
         t == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || t == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
}
static bool is_buffer_type(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
         t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

static void decode_write(const VkWriteDescriptorSet& w, std::vector<DescEntry>& out) {
  for (uint32_t i = 0; i < w.descriptorCount; ++i) {
    DescEntry e{w.dstBinding, w.dstArrayElement + i, w.descriptorType, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0,
                VK_IMAGE_LAYOUT_UNDEFINED};
    if (is_image_type(w.descriptorType) && w.pImageInfo) {
      e.view = w.pImageInfo[i].imageView;
      e.layout = w.pImageInfo[i].imageLayout;
    }
    else if (is_buffer_type(w.descriptorType) && w.pBufferInfo) {
      e.buffer = w.pBufferInfo[i].buffer;
      e.offset = w.pBufferInfo[i].offset;
      e.range = w.pBufferInfo[i].range;
    } else continue;
    out.push_back(e);
  }
}

// Caller holds tracker().mu exclusively.
static void merge(VkDescriptorSet set, const std::vector<DescEntry>& src) {
  auto& dst = tracker().sets[set];
  for (const auto& e : src) {
    auto [it, inserted] = dst.insert_or_assign(desc_key(e.binding, e.element), e);
    if (inserted && is_dynamic_type(e.type)) ++tracker().dynamic_counts[set];
  }
}

void track_set_write(const VkWriteDescriptorSet& w) {
  std::vector<DescEntry> entries;
  decode_write(w, entries);
  if (entries.empty()) return;
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  merge(w.dstSet, entries);
}

static std::vector<DescEntry> decode_template_locked(VkDescriptorUpdateTemplate tpl, const void* data) {
  std::vector<DescEntry> out;
  auto it = tracker().templates.find(tpl);
  if (it == tracker().templates.end()) return out;
  auto base = static_cast<const uint8_t*>(data);
  for (const auto& t : it->second) {
    for (uint32_t i = 0; i < t.descriptorCount; ++i) {
      const uint8_t* p = base + t.offset + i * t.stride;
      DescEntry e{t.dstBinding, t.dstArrayElement + i, t.descriptorType, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0,
                  VK_IMAGE_LAYOUT_UNDEFINED};
      if (is_image_type(t.descriptorType)) {
        auto ii = reinterpret_cast<const VkDescriptorImageInfo*>(p);
        e.view = ii->imageView;
        e.layout = ii->imageLayout;
      }
      else if (is_buffer_type(t.descriptorType)) {
        auto b = reinterpret_cast<const VkDescriptorBufferInfo*>(p);
        e.buffer = b->buffer;
        e.offset = b->offset;
        e.range = b->range;
      } else continue;
      out.push_back(e);
    }
  }
  return out;
}

void track_set_template(VkDescriptorSet set, VkDescriptorUpdateTemplate tpl, const void* data) {
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  auto entries = decode_template_locked(tpl, data);
  if (!entries.empty()) merge(set, entries);
}

std::vector<DescEntry> decode_push(const VkWriteDescriptorSet* writes, uint32_t count) {
  std::vector<DescEntry> out;
  for (uint32_t i = 0; i < count; ++i) decode_write(writes[i], out);
  return out;
}

std::vector<DescEntry> decode_push_template(VkDescriptorUpdateTemplate tpl, const void* data) {
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  return decode_template_locked(tpl, data);
}

// ---- hooks ----

static VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* ci,
                                                         const VkAllocationCallbacks* alloc, VkShaderModule* out) {
  diag(DIAG_SHADER_MODULES);
  VkResult r = device_of(device)->vk.CreateShaderModule(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    uint64_t h = hash64(ci->pCode, ci->codeSize);
    shader_dump(h, ci->pCode, ci->codeSize);
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().modules[*out] = h;
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyShaderModule(VkDevice device, VkShaderModule m, const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyShaderModule(device, m, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().modules.erase(m);
}

// Stage hash from the module, or from SPIR-V chained inline (maintenance5 /
// VK_EXT_graphics_pipeline_library style).
static uint64_t stage_hash_locked(const VkPipelineShaderStageCreateInfo& st) {
  if (st.module) {
    auto it = tracker().modules.find(st.module);
    return it == tracker().modules.end() ? 0 : it->second;
  }
  for (auto p = static_cast<const VkBaseInStructure*>(st.pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) {
      auto smci = reinterpret_cast<const VkShaderModuleCreateInfo*>(p);
      uint64_t h = hash64(smci->pCode, smci->codeSize);
      shader_dump(h, smci->pCode, smci->codeSize);
      return h;
    }
  return 0;
}

static void assign_stage(PipelineRec& rec, VkShaderStageFlagBits stage, uint64_t h) {
  if (!h) return;
  if (stage == VK_SHADER_STAGE_VERTEX_BIT) rec.vs = h;
  else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) rec.fs = h;
  else if (stage == VK_SHADER_STAGE_COMPUTE_BIT) rec.cs = h;
  else rec.other = h;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                              const VkGraphicsPipelineCreateInfo* cis,
                                                              const VkAllocationCallbacks* alloc, VkPipeline* out) {
  LARGE_INTEGER t0, t1, f;
  QueryPerformanceCounter(&t0);
  VkResult r = device_of(device)->vk.CreateGraphicsPipelines(device, cache, count, cis, alloc, out);
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&f);
  diag(DIAG_PIPELINES, count);
  diag(DIAG_PIPELINE_US, uint64_t((t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart));
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  for (uint32_t i = 0; i < count; ++i) {
    if (!out[i]) continue;
    PipelineRec rec{tracker().next_pipeline_id++, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0, 0, 0, cis[i].renderPass,
                    cis[i].subpass};
    for (uint32_t s = 0; s < cis[i].stageCount; ++s)
      assign_stage(rec, cis[i].pStages[s].stage, stage_hash_locked(cis[i].pStages[s]));
    for (auto p = static_cast<const VkBaseInStructure*>(cis[i].pNext); p; p = p->pNext)
      if (p->sType == VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR) {
        auto lib = reinterpret_cast<const VkPipelineLibraryCreateInfoKHR*>(p);
        for (uint32_t l = 0; l < lib->libraryCount; ++l) {
          auto it = tracker().pipelines.find(lib->pLibraries[l]);
          if (it == tracker().pipelines.end()) continue;
          if (!rec.vs) rec.vs = it->second.vs;
          if (!rec.fs) rec.fs = it->second.fs;
          if (!rec.render_pass) rec.render_pass = it->second.render_pass;
        }
      }
    tracker().pipelines[out[i]] = rec;
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                             const VkComputePipelineCreateInfo* cis,
                                                             const VkAllocationCallbacks* alloc, VkPipeline* out) {
  LARGE_INTEGER t0, t1, f;
  QueryPerformanceCounter(&t0);
  VkResult r = device_of(device)->vk.CreateComputePipelines(device, cache, count, cis, alloc, out);
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&f);
  diag(DIAG_PIPELINES, count);
  diag(DIAG_PIPELINE_US, uint64_t((t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart));
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  for (uint32_t i = 0; i < count; ++i) {
    if (!out[i]) continue;
    PipelineRec rec{tracker().next_pipeline_id++, VK_PIPELINE_BIND_POINT_COMPUTE, 0, 0, 0, 0, VK_NULL_HANDLE, 0};
    rec.cs = stage_hash_locked(cis[i].stage);
    tracker().pipelines[out[i]] = rec;
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyPipeline(VkDevice device, VkPipeline p, const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyPipeline(device, p, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().pipelines.erase(p);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice device, const VkImageCreateInfo* ci,
                                                  const VkAllocationCallbacks* alloc, VkImage* out) {
  // Render targets get TRANSFER_SRC so the tracer can read them back. Decided
  // once per process: the flag must not change while images exist.
  static const bool force_src = cfg_int("force_transfer_src", 1) != 0;
  VkImageCreateInfo patched = *ci;
  if (force_src && (ci->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_STORAGE_BIT)))
    patched.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  // BG3's AO/SSR target (RGB10A2 render target) gets STORAGE so the GI can
  // fold its visibility into the game's own ambient occlusion.
  static const bool force_storage = cfg_int("force_storage_rgb10a2", 1) != 0;
  if (force_storage && ci->format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 && (ci->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    patched.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  VkResult r = device_of(device)->vk.CreateImage(device, &patched, alloc, out);
  if (r == VK_SUCCESS) {
    ImageRec rec{tracker().next_image_id++, patched, false};
    rec.ci.pNext = nullptr;
    rec.ci.pQueueFamilyIndices = nullptr;
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().images[*out] = rec;
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice device, VkImage img, const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyImage(device, img, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().images.erase(img);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice device, const VkImageViewCreateInfo* ci,
                                                      const VkAllocationCallbacks* alloc, VkImageView* out) {
  VkResult r = device_of(device)->vk.CreateImageView(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().views[*out] = ViewRec{ci->image, ci->format, ci->subresourceRange};
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyImageView(VkDevice device, VkImageView v, const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyImageView(device, v, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().views.erase(v);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* ci,
                                                        const VkAllocationCallbacks* alloc, VkFramebuffer* out) {
  VkResult r = device_of(device)->vk.CreateFramebuffer(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    std::vector<VkImageView> views;
    if (!(ci->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) && ci->pAttachments)
      views.assign(ci->pAttachments, ci->pAttachments + ci->attachmentCount);
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().framebuffers[*out] = std::move(views);
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice device, VkFramebuffer fb, const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyFramebuffer(device, fb, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().framebuffers.erase(fb);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* ci,
                                                       const VkAllocationCallbacks* alloc, VkRenderPass* out) {
  VkResult r = device_of(device)->vk.CreateRenderPass(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    RenderPassRec rec;
    for (uint32_t i = 0; i < ci->attachmentCount; ++i) {
      rec.formats.push_back(ci->pAttachments[i].format);
      rec.final_layouts.push_back(ci->pAttachments[i].finalLayout);
    }
    for (uint32_t s = 0; s < ci->subpassCount; ++s) {
      RenderPassRec::Subpass sp;
      for (uint32_t c = 0; c < ci->pSubpasses[s].colorAttachmentCount; ++c)
        sp.color.push_back(ci->pSubpasses[s].pColorAttachments[c].attachment);
      if (ci->pSubpasses[s].pDepthStencilAttachment) sp.depth = ci->pSubpasses[s].pDepthStencilAttachment->attachment;
      rec.subpasses.push_back(sp);
    }
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().render_passes[*out] = std::move(rec);
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2* ci,
                                                        const VkAllocationCallbacks* alloc, VkRenderPass* out) {
  VkResult r = device_of(device)->vk.CreateRenderPass2(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    RenderPassRec rec;
    for (uint32_t i = 0; i < ci->attachmentCount; ++i) {
      rec.formats.push_back(ci->pAttachments[i].format);
      rec.final_layouts.push_back(ci->pAttachments[i].finalLayout);
    }
    for (uint32_t s = 0; s < ci->subpassCount; ++s) {
      RenderPassRec::Subpass sp;
      for (uint32_t c = 0; c < ci->pSubpasses[s].colorAttachmentCount; ++c)
        sp.color.push_back(ci->pSubpasses[s].pColorAttachments[c].attachment);
      if (ci->pSubpasses[s].pDepthStencilAttachment) sp.depth = ci->pSubpasses[s].pDepthStencilAttachment->attachment;
      rec.subpasses.push_back(sp);
    }
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().render_passes[*out] = std::move(rec);
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyRenderPass(VkDevice device, VkRenderPass rp, const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyRenderPass(device, rp, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().render_passes.erase(rp);
}

static VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice device, uint32_t wc, const VkWriteDescriptorSet* w,
                                                       uint32_t cc, const VkCopyDescriptorSet* c) {
  device_of(device)->vk.UpdateDescriptorSets(device, wc, w, cc, c);
  diag(DIAG_DESC_WRITES, wc + cc);
  for (uint32_t i = 0; i < wc; ++i) track_set_write(w[i]);
  if (cc) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    for (uint32_t i = 0; i < cc; ++i) {
      auto src = tracker().sets.find(c[i].srcSet);
      if (src == tracker().sets.end()) continue;
      std::vector<DescEntry> moved;
      for (uint32_t k = 0; k < c[i].descriptorCount; ++k) {
        auto e = src->second.find(desc_key(c[i].srcBinding, c[i].srcArrayElement + k));
        if (e == src->second.end()) continue;
        DescEntry d = e->second;
        d.binding = c[i].dstBinding;
        d.element = c[i].dstArrayElement + k;
        moved.push_back(d);
      }
      merge(c[i].dstSet, moved);
    }
  }
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorUpdateTemplate(VkDevice device,
                                                                     const VkDescriptorUpdateTemplateCreateInfo* ci,
                                                                     const VkAllocationCallbacks* alloc,
                                                                     VkDescriptorUpdateTemplate* out) {
  VkResult r = device_of(device)->vk.CreateDescriptorUpdateTemplate(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().templates[*out].assign(ci->pDescriptorUpdateEntries,
                                     ci->pDescriptorUpdateEntries + ci->descriptorUpdateEntryCount);
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate t,
                                                                  const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyDescriptorUpdateTemplate(device, t, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().templates.erase(t);
}

static VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet set,
                                                                  VkDescriptorUpdateTemplate tpl, const void* data) {
  device_of(device)->vk.UpdateDescriptorSetWithTemplate(device, set, tpl, data);
  diag(DIAG_DESC_WRITES);
  track_set_template(set, tpl, data);
}

// Swapchain images are not created through vkCreateImage; register them here.
static std::mutex g_sc_mu;
static std::unordered_map<VkSwapchainKHR, VkSwapchainCreateInfoKHR> g_swapchains;

static VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* ci,
                                                         const VkAllocationCallbacks* alloc, VkSwapchainKHR* out) {
  // force_present_mode: -1 leaves the game's choice. 0 (IMMEDIATE) decouples
  // test runs from the display, which matters when it is asleep.
  VkSwapchainCreateInfoKHR patched = *ci;
  headless_on_swapchain();
  int force = cfg_int("force_present_mode", -1);
  if (force >= 0) patched.presentMode = VkPresentModeKHR(force);
  ci = &patched;
  VkResult r = device_of(device)->vk.CreateSwapchainKHR(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    log("[layer] swapchain %ux%u format %d colorspace %d images>=%u present mode %d usage 0x%x", ci->imageExtent.width,
        ci->imageExtent.height, ci->imageFormat, ci->imageColorSpace, ci->minImageCount, ci->presentMode,
        ci->imageUsage);
    std::lock_guard<std::mutex> lock(g_sc_mu);
    g_swapchains[*out] = *ci;
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR sc, uint32_t* count,
                                                            VkImage* images) {
  VkResult r = device_of(device)->vk.GetSwapchainImagesKHR(device, sc, count, images);
  if (images && (r == VK_SUCCESS || r == VK_INCOMPLETE)) {
    VkSwapchainCreateInfoKHR sci{};
    {
      std::lock_guard<std::mutex> lock(g_sc_mu);
      auto it = g_swapchains.find(sc);
      if (it != g_swapchains.end()) sci = it->second;
    }
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    for (uint32_t i = 0; i < *count; ++i) {
      if (tracker().images.count(images[i])) continue;
      ImageRec rec{tracker().next_image_id++, {}, true};
      rec.ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      rec.ci.imageType = VK_IMAGE_TYPE_2D;
      rec.ci.format = sci.imageFormat;
      rec.ci.extent = {sci.imageExtent.width, sci.imageExtent.height, 1};
      rec.ci.mipLevels = 1;
      rec.ci.arrayLayers = sci.imageArrayLayers;
      rec.ci.samples = VK_SAMPLE_COUNT_1_BIT;
      rec.ci.usage = sci.imageUsage;
      tracker().images[images[i]] = rec;
    }
  }
  return r;
}

void register_tracker_hooks(HookMap& m) {
  auto add = [&](const char* name, PFN_vkVoidFunction fn) {
    m[std::string("vk") + name] = fn;
    m[std::string("vk") + name + "KHR"] = fn;
  };
#define HOOK(n) add(#n, reinterpret_cast<PFN_vkVoidFunction>(&n))
  HOOK(CreateShaderModule);
  HOOK(DestroyShaderModule);
  HOOK(CreateGraphicsPipelines);
  HOOK(CreateComputePipelines);
  HOOK(DestroyPipeline);
  HOOK(CreateImage);
  HOOK(DestroyImage);
  HOOK(CreateImageView);
  HOOK(DestroyImageView);
  HOOK(CreateFramebuffer);
  HOOK(DestroyFramebuffer);
  HOOK(CreateRenderPass);
  HOOK(CreateRenderPass2);
  HOOK(DestroyRenderPass);
  HOOK(UpdateDescriptorSets);
  HOOK(CreateDescriptorUpdateTemplate);
  HOOK(DestroyDescriptorUpdateTemplate);
  HOOK(UpdateDescriptorSetWithTemplate);
#undef HOOK
  m["vkCreateSwapchainKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&CreateSwapchainKHR);
  m["vkGetSwapchainImagesKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&GetSwapchainImagesKHR);
}

}  // namespace bg3
