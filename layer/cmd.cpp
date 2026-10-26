// Command buffer hooks: pass boundaries (where the core gets to inject work),
// pipeline/descriptor binding state, and draw/dispatch notes for the tracer.
#include "host.h"

namespace bg3 {

using HookMap = std::unordered_map<std::string, PFN_vkVoidFunction>;

static std::shared_mutex g_cmd_mu;
static std::unordered_map<VkCommandBuffer, CmdState*> g_cmds;

CmdState* cmd_state(VkCommandBuffer cmd, bool create) {
  {
    std::shared_lock<std::shared_mutex> lock(g_cmd_mu);
    auto it = g_cmds.find(cmd);
    if (it != g_cmds.end()) return it->second;
  }
  if (!create) return nullptr;
  std::unique_lock<std::shared_mutex> lock(g_cmd_mu);
  auto& s = g_cmds[cmd];
  if (!s) s = new CmdState;
  return s;
}

void cmd_forget(VkCommandBuffer cmd) {
  std::unique_lock<std::shared_mutex> lock(g_cmd_mu);
  auto it = g_cmds.find(cmd);
  if (it == g_cmds.end()) return;
  delete it->second;
  g_cmds.erase(it);
}

static VkDeviceFuncs& vk(VkCommandBuffer cmd) { return device_of(cmd)->vk; }
static int bp_index(VkPipelineBindPoint bp) { return bp == VK_PIPELINE_BIND_POINT_COMPUTE ? 1 : 0; }

static void fill_attachment(Bg3PassInfo& p, VkImageView view, bool depth) {
  if (!view) return;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto it = tracker().views.find(view);
  VkImage img = it == tracker().views.end() ? VK_NULL_HANDLE : it->second.image;
  VkFormat fmt = it == tracker().views.end() ? VK_FORMAT_UNDEFINED : it->second.format;
  if (depth) {
    p.depth_view = view;
    p.depth_image = img;
    p.depth_format = fmt;
  } else if (p.color_count < 8) {
    p.color_views[p.color_count] = view;
    p.color_images[p.color_count] = img;
    p.color_formats[p.color_count] = fmt;
    ++p.color_count;
  }
}

static std::string pass_json(const Bg3PassInfo& p) {
  std::string j = "{\"op\":\"pass\",\"ordinal\":" + std::to_string(p.ordinal) + ",\"w\":" + std::to_string(p.width) +
                  ",\"h\":" + std::to_string(p.height) + ",\"color\":[";
  for (uint32_t i = 0; i < p.color_count; ++i) j += (i ? ",\"" : "\"") + image_ref(p.color_images[i]) + "\"";
  j += "],\"depth\":\"" + (p.depth_image ? image_ref(p.depth_image) : std::string()) + "\"}";
  return j;
}

static void begin_pass(VkCommandBuffer cmd, CmdState* s, const VkRect2D& area) {
  s->in_pass = true;
  s->pass = Bg3PassInfo{};
  s->pass.ordinal = s->pass_ordinal++;
  s->pass.width = area.extent.width;
  s->pass.height = area.extent.height;
  for (auto& l : s->color_layouts) l = VK_IMAGE_LAYOUT_UNDEFINED;
  s->depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  s->pass.dispatches_before = s->dispatches_since_pass;
  s->pass.indirect_dispatches_before = s->indirect_since_pass;
  s->pass.last_cs_hash_before = s->last_cs;
  count_pass();
}

// The pass is described before the game's begin is forwarded, so the core can
// still record compute work ahead of it.
static void pass_ready(VkCommandBuffer cmd, CmdState* s) {
  for (int i = 0; i < 8; ++i) s->pass.color_layouts[i] = s->color_layouts[i];
  s->pass.depth_layout = s->depth_layout;
  core_on_pass_begin(cmd, &s->pass);
}

static CmdState* describe_classic(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info) {
  CmdState* s = cmd_state(cmd, true);
  begin_pass(cmd, s, info->renderArea);

  std::vector<VkImageView> views;
  for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
      auto ab = reinterpret_cast<const VkRenderPassAttachmentBeginInfo*>(p);
      views.assign(ab->pAttachments, ab->pAttachments + ab->attachmentCount);
    }
  RenderPassRec rp;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    if (views.empty()) {
      auto fb = tracker().framebuffers.find(info->framebuffer);
      if (fb != tracker().framebuffers.end()) views = fb->second;
    }
    auto it = tracker().render_passes.find(info->renderPass);
    if (it != tracker().render_passes.end()) rp = it->second;
  }
  if (!rp.subpasses.empty()) {
    for (uint32_t a : rp.subpasses[0].color)
      if (a < views.size()) {
        uint32_t slot = s->pass.color_count;
        fill_attachment(s->pass, views[a], false);
        if (slot < 8 && a < rp.final_layouts.size()) s->color_layouts[slot] = rp.final_layouts[a];
      }
    uint32_t d = rp.subpasses[0].depth;
    if (d < views.size()) {
      fill_attachment(s->pass, views[d], true);
      if (d < rp.final_layouts.size()) s->depth_layout = rp.final_layouts[d];
    }
  }
  pass_ready(cmd, s);
  return s;
}

static void end_pass(VkCommandBuffer cmd) {
  CmdState* s = cmd_state(cmd);
  if (!s || !s->in_pass) return;
  s->in_pass = false;
  s->dispatches_since_pass = s->indirect_since_pass = 0;
  s->last_cs = 0;
  if (s->tracing) {
    trace_flush_segment(s);
    std::string dumps = trace_dump_pass(cmd, s);
    trace_event(s, "{\"op\":\"end\",\"draws\":" + std::to_string(s->pass.draw_count) + ",\"dumps\":[" + dumps + "]}");
  }
  ngx_note_pass(cmd, s->pass, s->color_layouts, s->depth_layout);
  core_on_pass_end(cmd, &s->pass);
}

static void note_draw(VkCommandBuffer cmd, const char* kind, const char* extra = nullptr) {
  diag(DIAG_DRAW);
  CmdState* s = cmd_state(cmd);
  if (!s) return;
  if (s->in_pass) {
    if (!s->pass.draw_count++) {
      s->pass.first_vs_hash = s->vs;
      s->pass.first_fs_hash = s->fs;
    }
    s->pass.last_fs_hash = s->fs;
  }
  if (s->tracing) trace_note_draw(s, 0, kind, extra);
}

static void after_dispatch(VkCommandBuffer cmd, CmdState* s, bool indirect, uint32_t x, uint32_t y, uint32_t z) {
  ++s->dispatches_since_pass;
  if (indirect) ++s->indirect_since_pass;
  s->last_cs = s->cs;
  Bg3DispatchInfo info{s->cs, indirect, {x, y, z}};
  core_on_dispatch(cmd, &info);
}

// What the game has bound right now: descriptor sets (with dynamic offsets
// applied) or push descriptors.
bool query_binding(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, uint32_t element,
                   Bg3Binding* out) {
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS && bp != VK_PIPELINE_BIND_POINT_COMPUTE) return false;
  CmdState* s = cmd_state(cmd);
  if (!s || set >= 8) return false;
  int b = bp_index(bp);
  DescEntry e{};
  bool found = false;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  if (s->push_set[b] == set) {
    for (const auto& p : s->push[b])
      if (p.binding == binding && p.element == element) { e = p; found = true; }
  } else if (VkDescriptorSet ds = s->sets[b][set]) {
    auto it = tracker().sets.find(ds);
    if (it != tracker().sets.end()) {
      auto ent = it->second.find(desc_key(binding, element));
      if (ent != it->second.end()) {
        e = ent->second;
        found = true;
        if (is_dynamic_type(e.type)) {
          // Index of this descriptor among the set layout's dynamic ones, in binding order.
          uint32_t index = set_dynamic_index_locked(ds, binding, element);
          if (index == UINT32_MAX) {
            index = 0;
            for (const auto& [k, other] : it->second)
              if (is_dynamic_type(other.type) && k < desc_key(binding, element)) ++index;
          }
          if (index < s->dyn[b][set].size()) e.offset += s->dyn[b][set][index];
        }
      }
    }
  }
  if (!found) return false;
  *out = Bg3Binding{};
  out->type = e.type;
  out->buffer = e.buffer;
  out->offset = e.offset;
  out->range = e.range;
  out->view = e.view;
  out->layout = e.layout;
  if (e.view) {
    auto v = tracker().views.find(e.view);
    if (v != tracker().views.end()) {
      out->image = v->second.image;
      out->format = v->second.format;
      auto im = tracker().images.find(v->second.image);
      if (im != tracker().images.end()) {
        out->width = im->second.ci.extent.width;
        out->height = im->second.ci.extent.height;
      }
    }
  }
  return true;
}

void restore_bindings(VkCommandBuffer cmd, VkPipelineBindPoint bp) {
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS && bp != VK_PIPELINE_BIND_POINT_COMPUTE) return;
  CmdState* s = cmd_state(cmd);
  if (!s) return;
  int b = bp_index(bp);
  if (s->pipe[b]) vk(cmd).CmdBindPipeline(cmd, bp, s->pipe[b]);
  // Replay the game's own bind calls in order: exact dynamic offsets, and
  // later calls re-establish whatever an earlier, wider call disturbed.
  for (const auto& c : s->calls[b])
    vk(cmd).CmdBindDescriptorSets(cmd, bp, c.layout, c.first, uint32_t(c.sets.size()), c.sets.data(),
                                  uint32_t(c.dyn.size()), c.dyn.data());
}

// ---- hooks ----

static VKAPI_ATTR VkResult VKAPI_CALL AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* ai,
                                                             VkCommandBuffer* out) {
  VkResult r = device_of(device)->vk.AllocateCommandBuffers(device, ai, out);
  if (r == VK_SUCCESS)
    for (uint32_t i = 0; i < ai->commandBufferCount; ++i) cmd_state(out[i], true);
  return r;
}

static VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t n,
                                                     const VkCommandBuffer* cmds) {
  device_of(device)->vk.FreeCommandBuffers(device, pool, n, cmds);
  for (uint32_t i = 0; i < n; ++i)
    if (cmds[i]) cmd_forget(cmds[i]);
}

static VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* bi) {
  diag(DIAG_BEGIN_CMD);
  VkResult r = vk(cmd).BeginCommandBuffer(cmd, bi);
  CmdState* s = cmd_state(cmd, true);
  s->in_pass = false;
  s->pass_ordinal = 0;
  s->pipe[0] = s->pipe[1] = VK_NULL_HANDLE;
  s->pipe_id[0] = s->pipe_id[1] = 0;
  s->vs = s->fs = s->cs = 0;
  s->sets[0].fill(VK_NULL_HANDLE);
  s->sets[1].fill(VK_NULL_HANDLE);
  s->push[0].clear();
  s->push[1].clear();
  s->push_set[0] = s->push_set[1] = UINT32_MAX;
  s->calls[0].clear();
  s->calls[1].clear();
  for (auto& bp : s->dyn)
    for (auto& v : bp) v.clear();
  s->dispatches_since_pass = s->indirect_since_pass = 0;
  s->last_cs = 0;
  s->tracing = trace_recording();
  s->events.clear();
  s->dumps.clear();
  s->seg_key.clear();
  s->seg_count = 0;
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer cmd) {
  CmdState* s = cmd_state(cmd);
  if (s && !s->in_pass) core_on_cmd_end(cmd);
  if (s && s->tracing) trace_flush_segment(s);
  return vk(cmd).EndCommandBuffer(cmd);
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info,
                                                     VkSubpassContents contents) {
  diag(DIAG_BEGIN_PASS);
  CmdState* s = describe_classic(cmd, info);
  vk(cmd).CmdBeginRenderPass(cmd, info, contents);
  if (s->tracing) trace_event(s, pass_json(s->pass));
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass2(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info,
                                                      const VkSubpassBeginInfo* sbi) {
  diag(DIAG_BEGIN_PASS);
  CmdState* s = describe_classic(cmd, info);
  vk(cmd).CmdBeginRenderPass2(cmd, info, sbi);
  if (s->tracing) trace_event(s, pass_json(s->pass));
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer cmd) {
  vk(cmd).CmdEndRenderPass(cmd);
  end_pass(cmd);
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass2(VkCommandBuffer cmd, const VkSubpassEndInfo* sei) {
  vk(cmd).CmdEndRenderPass2(cmd, sei);
  end_pass(cmd);
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info) {
  diag(DIAG_BEGIN_RENDERING);
  CmdState* s = cmd_state(cmd, true);
  begin_pass(cmd, s, info->renderArea);
  for (uint32_t i = 0; i < info->colorAttachmentCount; ++i) {
    uint32_t slot = s->pass.color_count;
    fill_attachment(s->pass, info->pColorAttachments[i].imageView, false);
    if (slot < 8 && s->pass.color_count > slot) s->color_layouts[slot] = info->pColorAttachments[i].imageLayout;
  }
  if (info->pDepthAttachment && info->pDepthAttachment->imageView) {
    fill_attachment(s->pass, info->pDepthAttachment->imageView, true);
    s->depth_layout = info->pDepthAttachment->imageLayout;
  }
  pass_ready(cmd, s);
  vk(cmd).CmdBeginRendering(cmd, info);
  if (s->tracing) trace_event(s, pass_json(s->pass));
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRendering(VkCommandBuffer cmd) {
  vk(cmd).CmdEndRendering(cmd);
  end_pass(cmd);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint bp, VkPipeline pipe) {
  diag(DIAG_BIND_PIPE);
  vk(cmd).CmdBindPipeline(cmd, bp, pipe);
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS && bp != VK_PIPELINE_BIND_POINT_COMPUTE) return;
  CmdState* s = cmd_state(cmd, true);
  int i = bp_index(bp);
  s->pipe[i] = pipe;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto it = tracker().pipelines.find(pipe);
  s->pipe_id[i] = it == tracker().pipelines.end() ? 0 : it->second.id;
  if (i == 0) {
    s->vs = it == tracker().pipelines.end() ? 0 : it->second.vs;
    s->fs = it == tracker().pipelines.end() ? 0 : it->second.fs;
  } else {
    s->cs = it == tracker().pipelines.end() ? 0 : it->second.cs;
  }
}

static VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint bp,
                                                        VkPipelineLayout layout, uint32_t first, uint32_t count,
                                                        const VkDescriptorSet* sets, uint32_t dyn_count,
                                                        const uint32_t* dyn) {
  vk(cmd).CmdBindDescriptorSets(cmd, bp, layout, first, count, sets, dyn_count, dyn);
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS && bp != VK_PIPELINE_BIND_POINT_COMPUTE) return;
  CmdState* s = cmd_state(cmd, true);
  int b = bp_index(bp);
  // Dynamic offsets are consumed set by set, in binding order within a set.
  uint32_t used = 0;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  for (uint32_t i = 0; i < count && first + i < 8; ++i) {
    s->sets[b][first + i] = sets[i];
    s->set_layout[b][first + i] = layout;
    auto& d = s->dyn[b][first + i];
    d.clear();
    uint32_t n = set_dynamic_count_locked(sets[i]);
    for (uint32_t k = 0; k < n && used < dyn_count; ++k) d.push_back(dyn[used++]);
  }
  auto& calls = s->calls[b];
  if (calls.size() >= 32) calls.erase(calls.begin());
  calls.push_back({layout, first, std::vector<VkDescriptorSet>(sets, sets + count),
                   std::vector<uint32_t>(dyn, dyn + dyn_count)});
}

static VKAPI_ATTR void VKAPI_CALL CmdPushDescriptorSet(VkCommandBuffer cmd, VkPipelineBindPoint bp,
                                                       VkPipelineLayout layout, uint32_t set, uint32_t count,
                                                       const VkWriteDescriptorSet* writes) {
  vk(cmd).CmdPushDescriptorSet(cmd, bp, layout, set, count, writes);
  CmdState* s = cmd_state(cmd, true);
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS && bp != VK_PIPELINE_BIND_POINT_COMPUTE) return;
  s->push[bp_index(bp)] = decode_push(writes, count);
  s->push_set[bp_index(bp)] = set;
}

static VKAPI_ATTR void VKAPI_CALL CmdPushDescriptorSetWithTemplate(VkCommandBuffer cmd, VkDescriptorUpdateTemplate tpl,
                                                                   VkPipelineLayout layout, uint32_t set,
                                                                   const void* data) {
  vk(cmd).CmdPushDescriptorSetWithTemplate(cmd, tpl, layout, set, data);
  CmdState* s = cmd_state(cmd, true);
  // The template carries the bind point; graphics is by far the common case.
  s->push[0] = decode_push_template(tpl, data);
  s->push_set[0] = set;
}

static VKAPI_ATTR void VKAPI_CALL CmdDraw(VkCommandBuffer cmd, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) {
  vk(cmd).CmdDraw(cmd, vc, ic, fv, fi);
  note_draw(cmd, "draw");
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer cmd, uint32_t n, uint32_t ic, uint32_t fi, int32_t vo,
                                                 uint32_t fin) {
  vk(cmd).CmdDrawIndexed(cmd, n, ic, fi, vo, fin);
  note_draw(cmd, "draw");
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndirect(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o, uint32_t n,
                                                  uint32_t st) {
  vk(cmd).CmdDrawIndirect(cmd, b, o, n, st);
  note_draw(cmd, "draw_indirect");
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirect(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o, uint32_t n,
                                                         uint32_t st) {
  vk(cmd).CmdDrawIndexedIndirect(cmd, b, o, n, st);
  note_draw(cmd, "draw_indirect");
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndirectCount(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o, VkBuffer cb,
                                                       VkDeviceSize co, uint32_t mx, uint32_t st) {
  vk(cmd).CmdDrawIndirectCount(cmd, b, o, cb, co, mx, st);
  note_draw(cmd, "draw_indirect");
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirectCount(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o,
                                                              VkBuffer cb, VkDeviceSize co, uint32_t mx, uint32_t st) {
  vk(cmd).CmdDrawIndexedIndirectCount(cmd, b, o, cb, co, mx, st);
  note_draw(cmd, "draw_indirect");
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z) {
  diag(DIAG_DISPATCH);
  if (CmdState* pre = cmd_state(cmd)) {
    Bg3DispatchInfo info{pre->cs, false, {x, y, z}};
    core_on_dispatch_pre(cmd, &info);
  }
  vk(cmd).CmdDispatch(cmd, x, y, z);
  CmdState* s = cmd_state(cmd);
  if (!s) return;
  if (s->tracing) {
    char dims[64];
    snprintf(dims, sizeof dims, "\"groups\":[%u,%u,%u]", x, y, z);
    trace_note_draw(s, 1, "dispatch", dims);
  }
  after_dispatch(cmd, s, false, x, y, z);
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o) {
  diag(DIAG_DISPATCH);
  if (CmdState* pre = cmd_state(cmd)) {
    Bg3DispatchInfo info{pre->cs, true, {0, 0, 0}};
    core_on_dispatch_pre(cmd, &info);
  }
  vk(cmd).CmdDispatchIndirect(cmd, b, o);
  CmdState* s = cmd_state(cmd);
  if (!s) return;
  if (s->tracing) trace_note_draw(s, 1, "dispatch_indirect", nullptr);
  after_dispatch(cmd, s, true, 0, 0, 0);
}

static void note_transfer(VkCommandBuffer cmd, const char* op, VkImage src, VkImage dst) {
  CmdState* s = cmd_state(cmd);
  if (!s || !s->tracing) return;
  trace_flush_segment(s);
  std::string j = std::string("{\"op\":\"") + op + "\"";
  if (src) j += ",\"src\":\"" + image_ref(src) + "\"";
  if (dst) j += ",\"dst\":\"" + image_ref(dst) + "\"";
  trace_event(s, j + "}");
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyImage(VkCommandBuffer cmd, VkImage s, VkImageLayout sl, VkImage d,
                                               VkImageLayout dl, uint32_t n, const VkImageCopy* r) {
  vk(cmd).CmdCopyImage(cmd, s, sl, d, dl, n, r);
  note_transfer(cmd, "copy", s, d);
}

static VKAPI_ATTR void VKAPI_CALL CmdBlitImage(VkCommandBuffer cmd, VkImage s, VkImageLayout sl, VkImage d,
                                               VkImageLayout dl, uint32_t n, const VkImageBlit* r, VkFilter f) {
  vk(cmd).CmdBlitImage(cmd, s, sl, d, dl, n, r, f);
  note_transfer(cmd, "blit", s, d);
}

static VKAPI_ATTR void VKAPI_CALL CmdResolveImage(VkCommandBuffer cmd, VkImage s, VkImageLayout sl, VkImage d,
                                                  VkImageLayout dl, uint32_t n, const VkImageResolve* r) {
  vk(cmd).CmdResolveImage(cmd, s, sl, d, dl, n, r);
  note_transfer(cmd, "resolve", s, d);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearColorImage(VkCommandBuffer cmd, VkImage img, VkImageLayout l,
                                                     const VkClearColorValue* c, uint32_t n,
                                                     const VkImageSubresourceRange* r) {
  vk(cmd).CmdClearColorImage(cmd, img, l, c, n, r);
  note_transfer(cmd, "clear", VK_NULL_HANDLE, img);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearDepthStencilImage(VkCommandBuffer cmd, VkImage img, VkImageLayout l,
                                                            const VkClearDepthStencilValue* c, uint32_t n,
                                                            const VkImageSubresourceRange* r) {
  vk(cmd).CmdClearDepthStencilImage(cmd, img, l, c, n, r);
  note_transfer(cmd, "clear_depth", VK_NULL_HANDLE, img);
}

static VKAPI_ATTR void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer cmd, uint32_t n, const VkCommandBuffer* cmds) {
  diag(DIAG_EXEC);
  vk(cmd).CmdExecuteCommands(cmd, n, cmds);
  CmdState* s = cmd_state(cmd);
  if (!s || !s->tracing) return;
  trace_flush_segment(s);
  // Secondaries are never submitted on their own, so their events are inlined
  // into the primary here, in execution order.
  for (uint32_t i = 0; i < n; ++i) {
    CmdState* sec = cmd_state(cmds[i]);
    char b[96];
    snprintf(b, sizeof b, "{\"op\":\"secondary\",\"cmd\":\"%p\",\"traced\":%s}", static_cast<void*>(cmds[i]),
             sec && sec->tracing ? "true" : "false");
    trace_event(s, b);
    if (sec && sec->tracing) {
      trace_flush_segment(sec);
      for (auto& e : sec->events) trace_event(s, e);
    }
  }
}

void register_cmd_hooks(HookMap& m) {
  auto add = [&](const char* name, PFN_vkVoidFunction fn) {
    m[std::string("vk") + name] = fn;
    m[std::string("vk") + name + "KHR"] = fn;
  };
#define HOOK(n) add(#n, reinterpret_cast<PFN_vkVoidFunction>(&n))
  HOOK(AllocateCommandBuffers);
  HOOK(FreeCommandBuffers);
  HOOK(BeginCommandBuffer);
  HOOK(EndCommandBuffer);
  HOOK(CmdBeginRenderPass);
  HOOK(CmdBeginRenderPass2);
  HOOK(CmdEndRenderPass);
  HOOK(CmdEndRenderPass2);
  HOOK(CmdBeginRendering);
  HOOK(CmdEndRendering);
  HOOK(CmdBindPipeline);
  HOOK(CmdBindDescriptorSets);
  HOOK(CmdPushDescriptorSet);
  HOOK(CmdPushDescriptorSetWithTemplate);
  HOOK(CmdDraw);
  HOOK(CmdDrawIndexed);
  HOOK(CmdDrawIndirect);
  HOOK(CmdDrawIndexedIndirect);
  HOOK(CmdDrawIndirectCount);
  HOOK(CmdDrawIndexedIndirectCount);
  HOOK(CmdDispatch);
  HOOK(CmdDispatchIndirect);
  HOOK(CmdCopyImage);
  HOOK(CmdBlitImage);
  HOOK(CmdResolveImage);
  HOOK(CmdClearColorImage);
  HOOK(CmdClearDepthStencilImage);
  HOOK(CmdExecuteCommands);
#undef HOOK
}

}  // namespace bg3
