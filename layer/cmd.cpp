// Command buffer hooks: pass boundaries (where the core gets to inject work),
// pipeline/descriptor binding state, and draw/dispatch notes for the tracer.
#include "host.h"
#include "omm_submit.h"
#include "omm_content.h"
#include "omm_sampler.h"
#include "../common/cpu_descriptor_receipt.h"
#include "assetdb.h"
#include "cpu_detail.h"

#include <cstring>

namespace bg3 {
#if BG3RTX_DIAGNOSTICS
// cpu_timing=1: CPU time our geometry-capture hooks add while recording (P3 base frame rate).
// Slots: pass begin, pre-draw, the driver's draw call itself on captured draws, post-draw, pass end.
std::atomic<bool> g_cpu_timing{false};
std::atomic<bool> g_cpu_detail{false};
std::atomic<uint64_t> g_cpu_detail_ticks[CpuDetailCount];
std::atomic<uint64_t> g_cpu_ticks[7], g_cpu_draws{0};
static const char* const kCpuSlot[7] = {"pass before/begun", "pre-draw", "driver draw (XFB)", "post-draw", "pass ending", "pass after", "draw bookkeeping"};
struct CpuTimer {
  int slot; LARGE_INTEGER t0;
  explicit CpuTimer(int s) : slot(s) { if (g_cpu_timing.load(std::memory_order_relaxed)) QueryPerformanceCounter(&t0); else slot = -1; }
  ~CpuTimer() { if (slot >= 0) { LARGE_INTEGER t1; QueryPerformanceCounter(&t1); g_cpu_ticks[slot].fetch_add(uint64_t(t1.QuadPart - t0.QuadPart), std::memory_order_relaxed); } }
};
// Captured draws, counted only while cpu_timing=1 (the only reader is the report below).
static void count_cpu_draw() {
  if (g_cpu_timing.load(std::memory_order_relaxed)) g_cpu_draws.fetch_add(1, std::memory_order_relaxed);
}
void cmd_cpu_report(uint64_t frames) {
  g_cpu_timing.store(cfg_int("cpu_timing", 0) != 0, std::memory_order_relaxed);
  g_cpu_detail.store(g_cpu_timing.load(std::memory_order_relaxed) && cfg_int("detailed_logging",0)!=0,std::memory_order_relaxed);
  static uint64_t last = 0;
  static int mode = -1;
  static int cpu_opt2_mode = -1;
  static int cpu_opt3_mode = -1;
  static int cpu_opt4_mode = -1;
  const int current = cfg_hot(CfgHot::perf_hook_state);
  const int current_cpu_opt2=cfg_hot(CfgHot::cpu_opt2);
  const int current_cpu_opt3=cfg_hot(CfgHot::cpu_opt3);
  const int current_cpu_opt4=cfg_hot(CfgHot::cpu_opt4);
  static int reset_token = 0;
  const bool reset = stats_window::reset(cfg_int, reset_token);
  if (reset || !g_cpu_timing.load() || mode != current || cpu_opt2_mode!=current_cpu_opt2 || cpu_opt3_mode!=current_cpu_opt3 || cpu_opt4_mode!=current_cpu_opt4 || !last) {
    for (auto& ticks : g_cpu_ticks) ticks.exchange(0);
    for (auto& ticks : g_cpu_detail_ticks) ticks.exchange(0);
    g_cpu_draws.exchange(0); last = frames; mode = current; cpu_opt2_mode=current_cpu_opt2; cpu_opt3_mode=current_cpu_opt3; cpu_opt4_mode=current_cpu_opt4; return;
  }
  if (!g_cpu_timing.load() || frames - last < stats_period(300)) return;
  const uint64_t n = frames - last; last = frames;
  LARGE_INTEGER f; QueryPerformanceFrequency(&f);
  std::string o; double total = 0; char b[96];
  for (int i = 0; i < 7; ++i) {
    const double ms = double(g_cpu_ticks[i].exchange(0)) * 1000.0 / double(f.QuadPart) / double(n);
    total += ms; snprintf(b, sizeof b, "%s%s %.3f", i ? ", " : "", kCpuSlot[i], ms); o += b;
  }
  log("[cpu] capture hooks over %llu frames, ms/frame (all threads): total %.3f; %s; captured draws %.1f/frame; perf_hook_state %d",
      (unsigned long long)n, total, o.c_str(), double(g_cpu_draws.exchange(0)) / double(n), current);
  double detail[CpuDetailCount];for(int i=0;i<CpuDetailCount;++i)detail[i]=double(g_cpu_detail_ticks[i].exchange(0))*1000.0/double(f.QuadPart)/double(n);
  if(g_cpu_detail.load(std::memory_order_relaxed))log("[cpu] capture detail over %llu frames, ms/frame (nested): pipeline lookup %.3f, cascade identity %.3f, bvh classify %.3f, alpha records %.3f, material hooks %.3f; dispatch tracking pre %.3f, post %.3f (driver excluded)",
    (unsigned long long)n,detail[0],detail[1],detail[2],detail[3],detail[4],detail[5],detail[6]);
  if(g_cpu_detail.load(std::memory_order_relaxed))log("[cpu] capture attribution over %llu frames, ms/frame: prepass observe %.3f, cascade sets %.3f, draw plan %.3f, alpha lookup %.3f, opaque material %.3f, bvh record %.3f, xfb commands %.3f, cascade draw %.3f, emissive draw %.3f, alpha material %.3f",
    (unsigned long long)n,detail[7],detail[8],detail[9],detail[10],detail[11],detail[12],detail[13],detail[14],detail[15],detail[16]);
}
#else
// Diagnostics compiled out: no timers, cpu_timing has no effect on these hooks.
struct CpuTimer { explicit CpuTimer(int) {} };
static void count_cpu_draw() {}
void cmd_cpu_report(uint64_t) {}
#endif
}  // namespace bg3

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

uint64_t cmd_recording(VkCommandBuffer cmd){
  std::shared_lock lock(g_cmd_mu);auto i=g_cmds.find(cmd);
  return i==g_cmds.end()?0:std::atomic_ref<uint64_t>(i->second->recording).load(std::memory_order_acquire);
}
void cmd_forget(VkCommandBuffer cmd) {
  sl_discard_recording(cmd);
  alpha_discard_recording(cmd_recording(cmd));
  terrain_capture_discard(cmd);
  std::unique_lock<std::shared_mutex> lock(g_cmd_mu);
  auto it = g_cmds.find(cmd);
  if (it == g_cmds.end()) return;
  geo_cmd_discard(it->second);
  delete it->second;
  g_cmds.erase(it);
}

// Successful Vulkan reset/discard invalidates the exact recording even if the
// application never begins another one. Do not call BVH bookkeeping under cmd_mu.
static void discard_recordings(VkDevice device, VkCommandPool pool, bool destroy) {
  std::vector<VkCommandBuffer> commands;
  std::vector<uint64_t> recordings;
  {
    std::unique_lock lock(g_cmd_mu);
    for (auto it=g_cmds.begin(); it!=g_cmds.end();) {
      auto* state=it->second;
      if (state->allocation_device!=device || state->allocation_pool!=pool) { ++it; continue; }
      commands.push_back(it->first);
      recordings.push_back(state->recording);

      std::atomic_ref<uint64_t>(state->recording).store(0,std::memory_order_release);
      geo_cmd_discard(state);
      if (destroy) { delete state; it=g_cmds.erase(it); }
      else { state->begin_flags=0; ++it; }
    }
  }
  for (auto recording:recordings) { alpha_discard_recording(recording); }
  for (auto command:commands) { sl_discard_recording(command); terrain_capture_discard(command); bvh_cmd_reset(command); }
}
static void discard_recording(VkCommandBuffer command) {
  sl_discard_recording(command);
  alpha_discard_recording(cmd_recording(command));
  terrain_capture_discard(command);
  {
    std::unique_lock lock(g_cmd_mu);
    auto it=g_cmds.find(command);
    if (it!=g_cmds.end()) {
      std::atomic_ref<uint64_t>(it->second->recording).store(0,std::memory_order_release);
      it->second->begin_flags=0;
      geo_cmd_discard(it->second);
    }
  }
  bvh_cmd_reset(command);
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
  s->depth_cleared_empty=false;s->render_area=area;
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
  { CpuTimer t(0); geo_pass_before(cmd, s); }
  hair_primary_before_pass(cmd, s->pass);   // rt_hair_groom_primary_order: the G-buffer's groom header
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
      s->depth_cleared_empty=d<rp.load_ops.size() && rp.load_ops[d]==VK_ATTACHMENT_LOAD_OP_CLEAR &&
        d<info->clearValueCount && info->pClearValues[d].depthStencil.depth==0;
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
  if (s->geo || s->geo_ts >= 0) { CpuTimer t(5); geo_pass_after(cmd, s); }
  if (s->tracing) {
    trace_flush_segment(s);
    std::string dumps = trace_dump_pass(cmd, s);
    trace_event(s, "{\"op\":\"end\",\"draws\":" + std::to_string(s->pass.draw_count) + ",\"dumps\":[" + dumps + "]}");
  }
  // The hooks below may record work that reads this pass's attachments.
  for (uint32_t i = 0; i < s->pass.color_count; ++i) {
    life_note(s, (uint64_t)s->pass.color_images[i]);
    life_note(s, (uint64_t)s->pass.color_views[i]);
  }
  life_note(s, (uint64_t)s->pass.depth_image);
  life_note(s, (uint64_t)s->pass.depth_view);
  ngx_note_pass(cmd, s->pass, s->color_layouts, s->depth_layout);
  rrpost_pass_end(cmd, s->pass, s->color_layouts);
  sl_note_pass_end(cmd, s);
  core_on_pass_end(cmd, &s->pass);
  // Water compose runs in the core after the ordinary end dump. Capture its exact
  // target here: later passes may sample/copy it without ever attaching it again.
  if (s->tracing && water_is_pass(s->pass, s->pass.width, s->pass.height)) {
    const std::string dumps = trace_dump_pass(cmd, s, false, true);
    trace_event(s, "{\"op\":\"post_core\",\"dumps\":[" + dumps + "]}");
  }
  pan_pass_end(cmd, s);
}

static void note_draw(VkCommandBuffer cmd, const char* kind, const char* extra = nullptr, CmdState* resolved = nullptr) {
  CpuTimer timer(6);
  diag(DIAG_DRAW);
  CmdState* s = resolved ? resolved : cmd_state(cmd);
  if (!s) return;
  assetdb_note_draw(s->vs, s->fs);
  if (s->in_pass) {
    if (!s->pass.draw_count++) {
      s->pass.first_vs_hash = s->vs;
      s->pass.first_fs_hash = s->fs;
      sl_note_draw(cmd, s);
    }
    s->pass.last_fs_hash = s->fs;
  }
  if (s->tracing) trace_note_draw(cmd, s, 0, kind, extra);
}

static void after_dispatch(VkCommandBuffer cmd, CmdState* s, bool indirect, uint32_t x, uint32_t y, uint32_t z) {
  ++s->dispatches_since_pass;
  if (indirect) ++s->indirect_since_pass;
  s->last_cs = s->cs;
  Bg3DispatchInfo info{s->cs, indirect, {x, y, z},s->cs_native_validation};
  core_on_dispatch(cmd, &info);
}

// What the game has bound right now: descriptor sets (with dynamic offsets
// applied) or push descriptors.
bool query_binding(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, uint32_t element,
                   Bg3Binding* out) {
  return query_binding_resolved(cmd, bp, set, binding, element, out, nullptr);
}

static bool query_binding_uncached(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, uint32_t element,
                            Bg3Binding* out, const CmdState* resolved) {
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS && bp != VK_PIPELINE_BIND_POINT_COMPUTE) return false;
  CmdState* s = resolved ? const_cast<CmdState*>(resolved) : cmd_state(cmd);
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
  // Whoever asked is about to record work that uses it (the core's GI/RT
  // passes, RR before post, the alpha-test records).
  life_note(s, (uint64_t)e.view);
  life_note(s, (uint64_t)e.buffer);
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
      life_note(s, (uint64_t)v->second.image);
      auto im = tracker().images.find(v->second.image);
      if (im != tracker().images.end()) {
        out->width = im->second.ci.extent.width;
        out->height = im->second.ci.extent.height;
      }
    }
  }
  lock.unlock();   // (geo_buffer_size takes the capture's lock)
  if (out->buffer) {
    // A buffer descriptor we build from this must lie inside a live buffer. A set can
    // hold a destroyed buffer in a binding the game's pipeline does not use, and a
    // dynamic offset or VK_WHOLE_SIZE can run past the end: either is a wild read
    // (no robust buffer access). Every vkCreateBuffer is seen (geocapture.cpp), so an
    // unknown size means a destroyed buffer.
    static std::atomic<uint32_t> logged{0};
    const VkDeviceSize size = geo_buffer_size(out->buffer);
    if (!size || out->offset >= size) {
      if (logged++ < 20)
        log("[query] set %u binding %u: buffer %p %s (offset %llu, size %llu): not used", set, binding, (void*)out->buffer,
            size ? "offset past its end" : "destroyed or unknown", (unsigned long long)out->offset,
            (unsigned long long)size);
      *out = Bg3Binding{};
      return false;
    }
    if (out->range == VK_WHOLE_SIZE || out->range > size - out->offset) {
      if (out->range != VK_WHOLE_SIZE && logged++ < 20)
        log("[query] set %u binding %u: buffer %p range %llu at %llu exceeds its size %llu: clamped", set, binding,
            (void*)out->buffer, (unsigned long long)out->range, (unsigned long long)out->offset, (unsigned long long)size);
      out->range = size - out->offset;
    }
  }
  return true;
}

bool query_binding_resolved(VkCommandBuffer cmd,VkPipelineBindPoint bp,uint32_t set,uint32_t binding,uint32_t element,
                            Bg3Binding* out,const CmdState* resolved){
 if(!cfg_hot(CfgHot::cpu_opt4))return query_binding_uncached(cmd,bp,set,binding,element,out,resolved);
 if(bp!=VK_PIPELINE_BIND_POINT_GRAPHICS && bp!=VK_PIPELINE_BIND_POINT_COMPUTE)return false;
 auto* s=resolved?const_cast<CmdState*>(resolved):cmd_state(cmd);if(!s || set>=8)return false;
 const auto bi=bp_index(bp);uint64_t identity=0,version=0;
 // Push and update-after-bind descriptors have no immutable receipt. Ordinary
 // set ids survive frame boundaries, but never allocation/free/pool-reset reuse.
 if(s->push_set[bi]==set || !omm::descriptor_receipt(s->sets[bi][set],binding,element,identity,version))
  return query_binding_uncached(cmd,bp,set,binding,element,out,s);
 thread_local cpu_opt::DescriptorReceipts<Bg3Binding> cache;
 const cpu_opt::DescriptorReceipts<Bg3Binding>::Key key{identity,version,binding,element,
   buffer_resource_epoch(),material_resource_epoch(),uint64_t(s->sets[bi][set]),uint64_t(bp)};
 return cache.query(key,s->dyn[bi][set],*out,
  [&](Bg3Binding& value){return query_binding_uncached(cmd,bp,set,binding,element,&value,s);},
  [&](const Bg3Binding& value){cpu_opt::descriptor_uses(value,[&](uint64_t h){life_note(s,h);});});
}

// The descriptor set bound at (bind point, set), its layout and dynamic offsets.
bool query_set(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, VkDescriptorSet* out_set,
               VkDescriptorSetLayout* out_layout, uint32_t* dyn, uint32_t* dyn_count) {
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS && bp != VK_PIPELINE_BIND_POINT_COMPUTE) return false;
  CmdState* s = cmd_state(cmd);
  if (!s || set >= 8) return false;
  int b = bp_index(bp);
  if (s->push_set[b] == set) return false;
  VkDescriptorSet ds = s->sets[b][set];
  if (!ds) return false;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto l = tracker().set_layouts.find(ds);
  if (l == tracker().set_layouts.end() || !l->second) return false;
  const auto& d = s->dyn[b][set];
  uint32_t cap = dyn_count ? *dyn_count : 0;
  if (d.size() > cap) return false;
  for (size_t i = 0; i < d.size(); ++i) dyn[i] = d[i];
  if (dyn_count) *dyn_count = uint32_t(d.size());
  *out_set = ds;
  *out_layout = l->second;
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

static VKAPI_ATTR VkResult VKAPI_CALL CreateCommandPool(VkDevice device,const VkCommandPoolCreateInfo* ci,
    const VkAllocationCallbacks* allocator,VkCommandPool* pool) {
  const auto r=device_of(device)->vk.CreateCommandPool(device,ci,allocator,pool);
  if(r==VK_SUCCESS)omm::pool_created(*pool,ci->queueFamilyIndex);
  return r;
}
static VKAPI_ATTR void VKAPI_CALL DestroyCommandPool(VkDevice device,VkCommandPool pool,const VkAllocationCallbacks* allocator) {
  device_of(device)->vk.DestroyCommandPool(device,pool,allocator);
  omm::pool_discard(pool,true);
  discard_recordings(device,pool,true);
}
static VKAPI_ATTR VkResult VKAPI_CALL ResetCommandPool(VkDevice device,VkCommandPool pool,VkCommandPoolResetFlags flags) {
  const auto r=device_of(device)->vk.ResetCommandPool(device,pool,flags);
  if(r==VK_SUCCESS) { omm::pool_discard(pool,false); discard_recordings(device,pool,false); }
  return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL ResetCommandBuffer(VkCommandBuffer cmd,VkCommandBufferResetFlags flags) {
  const auto r=vk(cmd).ResetCommandBuffer(cmd,flags);
  if(r==VK_SUCCESS) { omm::command_discard(cmd); discard_recording(cmd); }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* ai,
                                                             VkCommandBuffer* out) {
  VkResult r = device_of(device)->vk.AllocateCommandBuffers(device, ai, out);
  if (r == VK_SUCCESS)
    for (uint32_t i = 0; i < ai->commandBufferCount; ++i) {
      omm::command_allocated(ai->commandPool,out[i]);
      bvh_cmd_reset(out[i]);   // a handle reused after its pool was destroyed: old records are dropped
      cmd_state(out[i], true)->game_alloc = true;
      cmd_state(out[i], true)->level = ai->level;
      cmd_state(out[i], true)->allocation_device = device;
      cmd_state(out[i], true)->allocation_pool = ai->commandPool;
    }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t n,
                                                     const VkCommandBuffer* cmds) {
  device_of(device)->vk.FreeCommandBuffers(device, pool, n, cmds);
  for (uint32_t i = 0; i < n; ++i)
    if (cmds[i]) {
      omm::command_discard(cmds[i],true);
      bvh_cmd_reset(cmds[i]);   // freed without (another) submit: our builds in it never run
      cmd_forget(cmds[i]);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* bi) {
  diag(DIAG_BEGIN_CMD);
  bvh_cmd_reset(cmd);   // recorded again: a previous recording not submitted is dropped
  VkResult r = vk(cmd).BeginCommandBuffer(cmd, bi);
  if(r==VK_SUCCESS){sl_discard_recording(cmd);alpha_discard_recording(cmd_recording(cmd));terrain_capture_discard(cmd);omm::command_discard(cmd);omm::command_begin(cmd,(bi->flags&VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)==0);}
  CmdState* s = cmd_state(cmd, true);
  static std::atomic<uint64_t> recordings{0};
  std::atomic_ref<uint64_t>(s->recording).store(++recordings,std::memory_order_release);
  s->begin_flags = r==VK_SUCCESS && bi ? bi->flags : 0;
  s->ours_since_event = false;
  if (r == VK_SUCCESS) fault_checkpoint(cmd, kCkCmdBegin, uint32_t(s->recording));
  s->in_pass = false;
  s->hp_coverage = s->hp_query = UINT32_MAX;
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
  geo_cmd_discard(s);   // an effects pass left open by the previous recording
  s->geo = {};
  s->geo_material_active=s->geo_material_no_writes=false;
  em_forget_cmd(cmd);   // D-EM: draws of an earlier recording that was never submitted
  water_forget_cmd(cmd);   // M5.1 water, likewise
  s->geo_ts = -1;
  s->vb.fill({});
  s->ib = VK_NULL_HANDLE;
  s->ib_offset = 0;
  s->has_viewport = false;
  s->has_scissor=false;
  s->sl_layouts.clear();
  s->sl_input_layouts.clear();
  s->sl_cam_valid = false;
  s->sl_mvec_copy = {};
  s->sl_depth_copy = {};
  s->sl_hudless = {}; s->sl_ui = {};
  s->sl_mvec_slot = UINT32_MAX;
  s->refs.clear();
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer cmd) {
  CmdState* s = cmd_state(cmd);
  if (s && !s->in_pass) core_on_cmd_end(cmd);
  if (s && s->tracing) trace_flush_segment(s);
  omm::feedback_before_end(cmd,s && !s->in_pass);
  const auto r=vk(cmd).EndCommandBuffer(cmd);
  omm::command_end(cmd,r==VK_SUCCESS);
  if (r != VK_SUCCESS) discard_recording(cmd);
  return r;
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info,
                                                     VkSubpassContents contents) {
  diag(DIAG_BEGIN_PASS);
  CmdState* s = describe_classic(cmd, info);
  vk(cmd).CmdBeginRenderPass(cmd, info, contents);
  if (s->geo) { CpuTimer t(0); geo_pass_begun(cmd, s); }
  if (s->tracing) trace_event(s, pass_json(s->pass));
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass2(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info,
                                                      const VkSubpassBeginInfo* sbi) {
  diag(DIAG_BEGIN_PASS);
  CmdState* s = describe_classic(cmd, info);
  vk(cmd).CmdBeginRenderPass2(cmd, info, sbi);
  if (s->geo) { CpuTimer t(0); geo_pass_begun(cmd, s); }
  if (s->tracing) trace_event(s, pass_json(s->pass));
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer cmd) {
  if (CmdState* gs = cmd_state(cmd); gs && gs->geo) { CpuTimer t(4); geo_pass_ending(cmd, gs); }
  vk(cmd).CmdEndRenderPass(cmd);
  end_pass(cmd);
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass2(VkCommandBuffer cmd, const VkSubpassEndInfo* sei) {
  if (CmdState* gs = cmd_state(cmd); gs && gs->geo) { CpuTimer t(4); geo_pass_ending(cmd, gs); }
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
    s->depth_cleared_empty=info->pDepthAttachment->loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR &&
      info->pDepthAttachment->clearValue.depthStencil.depth==0;
  }
  pass_ready(cmd, s);
  vk(cmd).CmdBeginRendering(cmd, info);
  if (s->geo) { CpuTimer t(0); geo_pass_begun(cmd, s); }
  if (s->tracing) trace_event(s, pass_json(s->pass));
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRendering(VkCommandBuffer cmd) {
  if (CmdState* gs = cmd_state(cmd); gs && gs->geo) { CpuTimer t(4); geo_pass_ending(cmd, gs); }
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
    s->cs_native_validation = it == tracker().pipelines.end() ? 0 : it->second.native_validation;
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

// Barriers recorded into command buffers the game did not allocate (Streamline
// and NGX ones), logged with sl_log_barriers=N for the first N barriers.
static void log_foreign_barrier(VkCommandBuffer cmd, const char* kind, VkImage img, VkImageLayout from, VkImageLayout to,
                                uint32_t aspect, uint32_t qsrc, uint32_t qdst, uint32_t mips, uint32_t layers) {
  static std::atomic<int> n{0};
  if (n >= cfg_hot(CfgHot::sl_log_barriers)) return;
  CmdState* s = cmd_state(cmd);
  if (s && s->game_alloc) return;
  ++n;
  bool tracked = false;
  VkFormat fmt = VK_FORMAT_UNDEFINED;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto it = tracker().images.find(img);
    if (it != tracker().images.end()) {
      tracked = true;
      fmt = it->second.ci.format;
    }
  }
  log("[barrier] %s cmd %p image %p (%s fmt %d, tag %d) %d -> %d aspect 0x%x queues %u -> %u mips %u layers %u", kind,
      (void*)cmd, (void*)img, tracked ? "tracked" : "untracked", fmt, sl_watched(img), from, to, aspect, qsrc, qdst, mips,
      layers);
}

static VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags src,
                                                     VkPipelineStageFlags dst, VkDependencyFlags dep, uint32_t nm,
                                                     const VkMemoryBarrier* mb, uint32_t nb,
                                                     const VkBufferMemoryBarrier* bb, uint32_t ni,
                                                     const VkImageMemoryBarrier* ib) {
  for (uint32_t i = 0; i < ni; ++i)
    log_foreign_barrier(cmd, "v1", ib[i].image, ib[i].oldLayout, ib[i].newLayout, ib[i].subresourceRange.aspectMask,
                        ib[i].srcQueueFamilyIndex, ib[i].dstQueueFamilyIndex, ib[i].subresourceRange.levelCount,
                        ib[i].subresourceRange.layerCount);
  vk(cmd).CmdPipelineBarrier(cmd, src, dst, dep, nm, mb, nb, bb, ni, ib);
  CmdState* s = nullptr;
  for (uint32_t i = 0; i < ni; ++i)
    if (sl_watched(ib[i].image)) {
      if (!s) s = cmd_state(cmd, true);
      sl_note_barrier(s, ib[i].image, ib[i].newLayout);
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* info) {
  for (uint32_t i = 0; i < info->imageMemoryBarrierCount; ++i) {
    const auto& b = info->pImageMemoryBarriers[i];
    log_foreign_barrier(cmd, "v2", b.image, b.oldLayout, b.newLayout, b.subresourceRange.aspectMask,
                        b.srcQueueFamilyIndex, b.dstQueueFamilyIndex, b.subresourceRange.levelCount,
                        b.subresourceRange.layerCount);
  }
  vk(cmd).CmdPipelineBarrier2(cmd, info);
  CmdState* s = nullptr;
  for (uint32_t i = 0; i < info->imageMemoryBarrierCount; ++i) {
    const auto& b = info->pImageMemoryBarriers[i];
    if (sl_watched(b.image)) {
      if (!s) s = cmd_state(cmd, true);
      sl_note_barrier(s, b.image, b.newLayout);
    }
  }
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
  CmdState* gs = cmd_state(cmd);
  const bool geo = gs && gs->geo;
  if (geo) { CpuTimer t(1); geo_pre_draw(cmd, gs, DrawArgs{0, vc, ic, fv, 0, fi}); count_cpu_draw(); }
  if (geo) { CpuTimer t(2); vk(cmd).CmdDraw(cmd, vc, ic, fv, fi); } else vk(cmd).CmdDraw(cmd, vc, ic, fv, fi);
  if (geo) { CpuTimer t(3); geo_post_draw(cmd, gs); }
  note_draw(cmd, "draw", nullptr, cfg_hot(CfgHot::perf_hook_state) ? gs : nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer cmd, uint32_t n, uint32_t ic, uint32_t fi, int32_t vo,
                                                 uint32_t fin) {
  CmdState* gs = cmd_state(cmd);
  const bool geo = gs && gs->geo;
  // rt_hair_groom_primary: the strand variant is bound before the capture's XFB begins (no pipeline bind while
  // transform feedback is active) and the game's pipeline after it ended.
  const bool hp = gs && hair_primary_wanted() && hair_primary_swap(cmd, gs, DrawArgs{1, n, ic, fi, vo, fin});
  if (geo) { CpuTimer t(1); geo_pre_draw(cmd, gs, DrawArgs{1, n, ic, fi, vo, fin}); count_cpu_draw(); }
  if (geo) { CpuTimer t(2); vk(cmd).CmdDrawIndexed(cmd, n, ic, fi, vo, fin); } else vk(cmd).CmdDrawIndexed(cmd, n, ic, fi, vo, fin);
  if (geo) { CpuTimer t(3); geo_post_draw(cmd, gs); }
  if (hp) hair_primary_unswap(cmd, gs, DrawArgs{1, n, ic, fi, vo, fin});
  note_draw(cmd, "draw", nullptr, cfg_hot(CfgHot::perf_hook_state) ? gs : nullptr);
}

// ---- vertex/index buffer bindings and buffer writes (Track D BVH split) ----

static VKAPI_ATTR void VKAPI_CALL CmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first, uint32_t n, const VkBuffer* b,
                                                       const VkDeviceSize* o) {
  vk(cmd).CmdBindVertexBuffers(cmd, first, n, b, o);
  if (CmdState* s = cmd_state(cmd))
    for (uint32_t i = 0; i < n && first + i < CmdState::kMaxVertexBindings; ++i) s->vb[first + i] = {b[i], o[i], 0};
}

static void note_vb2(VkCommandBuffer cmd, uint32_t first, uint32_t n, const VkBuffer* b, const VkDeviceSize* o,
                     const VkDeviceSize* strides) {
  if (CmdState* s = cmd_state(cmd))
    for (uint32_t i = 0; i < n && first + i < CmdState::kMaxVertexBindings; ++i)
      s->vb[first + i] = {b[i], o[i], strides ? strides[i] : 0};
}

static VKAPI_ATTR void VKAPI_CALL CmdBindVertexBuffers2(VkCommandBuffer cmd, uint32_t first, uint32_t n, const VkBuffer* b,
                                                        const VkDeviceSize* o, const VkDeviceSize* sizes,
                                                        const VkDeviceSize* strides) {
  vk(cmd).CmdBindVertexBuffers2(cmd, first, n, b, o, sizes, strides);
  note_vb2(cmd, first, n, b, o, strides);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindVertexBuffers2EXT(VkCommandBuffer cmd, uint32_t first, uint32_t n,
                                                           const VkBuffer* b, const VkDeviceSize* o,
                                                           const VkDeviceSize* sizes, const VkDeviceSize* strides) {
  static std::atomic<PFN_vkCmdBindVertexBuffers2> next{nullptr};
  PFN_vkCmdBindVertexBuffers2 f = next.load();
  if (!f) {
    DeviceData* dd = device_of(cmd);
    f = reinterpret_cast<PFN_vkCmdBindVertexBuffers2>(dd->gdpa(dd->device, "vkCmdBindVertexBuffers2EXT"));
    if (!f) f = dd->vk.CmdBindVertexBuffers2;
    next = f;
  }
  f(cmd, first, n, b, o, sizes, strides);
  note_vb2(cmd, first, n, b, o, strides);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o, VkIndexType t) {
  vk(cmd).CmdBindIndexBuffer(cmd, b, o, t);
  if (CmdState* s = cmd_state(cmd)) {
    s->ib = b;
    s->ib_offset = o;
    s->ib_type = t;
  }
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t n,
                                                const VkBufferCopy* r) {
  vk(cmd).CmdCopyBuffer(cmd, src, dst, n, r);
  for (uint32_t i = 0; i < n; ++i) bvh_buffer_written(dst, r[i].dstOffset, r[i].size);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBuffer2(VkCommandBuffer cmd, const VkCopyBufferInfo2* info) {
  vk(cmd).CmdCopyBuffer2(cmd, info);
  for (uint32_t i = 0; i < info->regionCount; ++i)
    bvh_buffer_written(info->dstBuffer, info->pRegions[i].dstOffset, info->pRegions[i].size);
}

static VKAPI_ATTR void VKAPI_CALL CmdUpdateBuffer(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize o, VkDeviceSize size,
                                                  const void* data) {
  vk(cmd).CmdUpdateBuffer(cmd, dst, o, size, data);
  bvh_buffer_written(dst, o, size);
}

static VKAPI_ATTR void VKAPI_CALL CmdFillBuffer(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize o, VkDeviceSize size,
                                                uint32_t data) {
  vk(cmd).CmdFillBuffer(cmd, dst, o, size, data);
  bvh_buffer_written(dst, o, size);
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndirect(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o, uint32_t n,
                                                  uint32_t st) {
  CmdState* gs = cmd_state(cmd);
  const bool geo = gs && gs->geo;
  if (geo) { CpuTimer t(1); geo_pre_draw(cmd, gs, DrawArgs{2, 0, 0, 0, 0, 0}); count_cpu_draw(); }
  if (geo) { CpuTimer t(2); vk(cmd).CmdDrawIndirect(cmd, b, o, n, st); } else vk(cmd).CmdDrawIndirect(cmd, b, o, n, st);
  if (geo) { CpuTimer t(3); geo_post_draw(cmd, gs); }
  note_draw(cmd, "draw_indirect", nullptr, cfg_hot(CfgHot::perf_hook_state) ? gs : nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirect(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o, uint32_t n,
                                                         uint32_t st) {
  CmdState* gs = cmd_state(cmd);
  const bool geo = gs && gs->geo;
  if (geo) { CpuTimer t(1); geo_pre_draw(cmd, gs, DrawArgs{3, 0, 0, 0, 0, 0}); count_cpu_draw(); }
  if (geo) { CpuTimer t(2); vk(cmd).CmdDrawIndexedIndirect(cmd, b, o, n, st); } else vk(cmd).CmdDrawIndexedIndirect(cmd, b, o, n, st);
  if (geo) { CpuTimer t(3); geo_post_draw(cmd, gs); }
  note_draw(cmd, "draw_indirect", nullptr, cfg_hot(CfgHot::perf_hook_state) ? gs : nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndirectCount(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o, VkBuffer cb,
                                                       VkDeviceSize co, uint32_t mx, uint32_t st) {
  CmdState* gs = cmd_state(cmd);
  const bool geo = gs && gs->geo;
  if (geo) { CpuTimer t(1); geo_pre_draw(cmd, gs, DrawArgs{4, 0, 0, 0, 0, 0}); count_cpu_draw(); }
  if (geo) { CpuTimer t(2); vk(cmd).CmdDrawIndirectCount(cmd, b, o, cb, co, mx, st); } else vk(cmd).CmdDrawIndirectCount(cmd, b, o, cb, co, mx, st);
  if (geo) { CpuTimer t(3); geo_post_draw(cmd, gs); }
  note_draw(cmd, "draw_indirect", nullptr, cfg_hot(CfgHot::perf_hook_state) ? gs : nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirectCount(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o,
                                                              VkBuffer cb, VkDeviceSize co, uint32_t mx, uint32_t st) {
  CmdState* gs = cmd_state(cmd);
  const bool geo = gs && gs->geo;
  if (geo) { CpuTimer t(1); geo_pre_draw(cmd, gs, DrawArgs{5, 0, 0, 0, 0, 0}); count_cpu_draw(); }
  if (geo) { CpuTimer t(2); vk(cmd).CmdDrawIndexedIndirectCount(cmd, b, o, cb, co, mx, st); } else vk(cmd).CmdDrawIndexedIndirectCount(cmd, b, o, cb, co, mx, st);
  if (geo) { CpuTimer t(3); geo_post_draw(cmd, gs); }
  note_draw(cmd, "draw_indirect", nullptr, cfg_hot(CfgHot::perf_hook_state) ? gs : nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z) {
  diag(DIAG_DISPATCH);
  {CpuDetailTimer timer(CpuDetailDispatchPre);
  if (CmdState* pre = cmd_state(cmd)) {
    Bg3DispatchInfo info{pre->cs, false, {x, y, z},pre->cs_native_validation};
    rrpost_dispatch_pre(cmd, pre->cs);
    core_on_dispatch_pre(cmd, &info);
    assetdb_on_dispatch_pre(cmd);
  }
  }
  vk(cmd).CmdDispatch(cmd, x, y, z);
  CpuDetailTimer timer(CpuDetailDispatchPost);
  CmdState* s = cmd_state(cmd);
  if (!s) return;
  rrpost_dispatch_post(cmd, s->cs);
  if (s->tracing) {
    char dims[64];
    snprintf(dims, sizeof dims, "\"groups\":[%u,%u,%u]", x, y, z);
    trace_note_draw(cmd, s, 1, "dispatch", dims);
  }
  after_dispatch(cmd, s, false, x, y, z);
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer cmd, VkBuffer b, VkDeviceSize o) {
  diag(DIAG_DISPATCH);
  {CpuDetailTimer timer(CpuDetailDispatchPre);
  if (CmdState* pre = cmd_state(cmd)) {
    Bg3DispatchInfo info{pre->cs, true, {0, 0, 0},pre->cs_native_validation};
    rrpost_dispatch_pre(cmd, pre->cs);
    core_on_dispatch_pre(cmd, &info);
    assetdb_on_dispatch_pre(cmd);
  }
  }
  vk(cmd).CmdDispatchIndirect(cmd, b, o);
  CpuDetailTimer timer(CpuDetailDispatchPost);
  CmdState* s = cmd_state(cmd);
  if (!s) return;
  rrpost_dispatch_post(cmd, s->cs);
  if (s->tracing) trace_note_draw(cmd, s, 1, "dispatch_indirect", nullptr);
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
  const bool began=omm::content_before_image_write(cmd,d);
  vk(cmd).CmdCopyImage(cmd, s, sl, d, dl, n, r);
  omm::content_after_image_write(cmd,d,began);
  note_transfer(cmd, "copy", s, d);
}

static VKAPI_ATTR void VKAPI_CALL CmdBlitImage(VkCommandBuffer cmd, VkImage s, VkImageLayout sl, VkImage d,
                                               VkImageLayout dl, uint32_t n, const VkImageBlit* r, VkFilter f) {
  const bool began=omm::content_before_image_write(cmd,d);
  vk(cmd).CmdBlitImage(cmd, s, sl, d, dl, n, r, f);
  omm::content_after_image_write(cmd,d,began);
  note_transfer(cmd, "blit", s, d);
}

static VKAPI_ATTR void VKAPI_CALL CmdResolveImage(VkCommandBuffer cmd, VkImage s, VkImageLayout sl, VkImage d,
                                                  VkImageLayout dl, uint32_t n, const VkImageResolve* r) {
  const bool began=omm::content_before_image_write(cmd,d);
  vk(cmd).CmdResolveImage(cmd, s, sl, d, dl, n, r);
  omm::content_after_image_write(cmd,d,began);
  note_transfer(cmd, "resolve", s, d);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearColorImage(VkCommandBuffer cmd, VkImage img, VkImageLayout l,
                                                     const VkClearColorValue* c, uint32_t n,
                                                     const VkImageSubresourceRange* r) {
  const bool began=omm::content_before_image_write(cmd,img);
  vk(cmd).CmdClearColorImage(cmd, img, l, c, n, r);
  omm::content_after_image_write(cmd,img,began);
  note_transfer(cmd, "clear", VK_NULL_HANDLE, img);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearDepthStencilImage(VkCommandBuffer cmd, VkImage img, VkImageLayout l,
                                                            const VkClearDepthStencilValue* c, uint32_t n,
                                                            const VkImageSubresourceRange* r) {
  vk(cmd).CmdClearDepthStencilImage(cmd, img, l, c, n, r);
  note_transfer(cmd, "clear_depth", VK_NULL_HANDLE, img);
}

template<class F> static F image_copy_proc(VkCommandBuffer cmd,const char* name,const char* alias){
  auto* dd=device_of(cmd);auto fn=reinterpret_cast<F>(dd->gdpa(dd->device,name));if(!fn)fn=reinterpret_cast<F>(dd->gdpa(dd->device,alias));return fn;
}
static VKAPI_ATTR void VKAPI_CALL CmdCopyBufferToImage(VkCommandBuffer cmd,VkBuffer source,VkImage target,VkImageLayout layout,uint32_t count,const VkBufferImageCopy* copies){
  const bool began=omm::content_before_image_write(cmd,target);vk(cmd).CmdCopyBufferToImage(cmd,source,target,layout,count,copies);omm::content_after_image_write(cmd,target,began);
}
static VKAPI_ATTR void VKAPI_CALL CmdCopyBufferToImage2(VkCommandBuffer cmd,const VkCopyBufferToImageInfo2* info){
  auto fn=image_copy_proc<PFN_vkCmdCopyBufferToImage2>(cmd,"vkCmdCopyBufferToImage2","vkCmdCopyBufferToImage2KHR");if(!fn)return;const bool began=omm::content_before_image_write(cmd,info->dstImage);fn(cmd,info);omm::content_after_image_write(cmd,info->dstImage,began);
}
static VKAPI_ATTR void VKAPI_CALL CmdCopyImage2(VkCommandBuffer cmd,const VkCopyImageInfo2* info){
  auto fn=image_copy_proc<PFN_vkCmdCopyImage2>(cmd,"vkCmdCopyImage2","vkCmdCopyImage2KHR");if(!fn)return;const bool began=omm::content_before_image_write(cmd,info->dstImage);fn(cmd,info);omm::content_after_image_write(cmd,info->dstImage,began);
}
static VKAPI_ATTR void VKAPI_CALL CmdBlitImage2(VkCommandBuffer cmd,const VkBlitImageInfo2* info){
  auto fn=image_copy_proc<PFN_vkCmdBlitImage2>(cmd,"vkCmdBlitImage2","vkCmdBlitImage2KHR");if(!fn)return;const bool began=omm::content_before_image_write(cmd,info->dstImage);fn(cmd,info);omm::content_after_image_write(cmd,info->dstImage,began);
}
static VKAPI_ATTR void VKAPI_CALL CmdResolveImage2(VkCommandBuffer cmd,const VkResolveImageInfo2* info){
  auto fn=image_copy_proc<PFN_vkCmdResolveImage2>(cmd,"vkCmdResolveImage2","vkCmdResolveImage2KHR");if(!fn)return;const bool began=omm::content_before_image_write(cmd,info->dstImage);fn(cmd,info);omm::content_after_image_write(cmd,info->dstImage,began);
}
static VKAPI_ATTR void VKAPI_CALL CmdCopyMemoryToImageIndirectNV(VkCommandBuffer cmd,VkDeviceAddress address,uint32_t count,uint32_t stride,VkImage target,VkImageLayout layout,const VkImageSubresourceLayers* layers){
  auto fn=image_copy_proc<PFN_vkCmdCopyMemoryToImageIndirectNV>(cmd,"vkCmdCopyMemoryToImageIndirectNV","vkCmdCopyMemoryToImageIndirectNV");if(!fn)return;const bool began=omm::content_before_image_write(cmd,target,true);fn(cmd,address,count,stride,target,layout,layers);omm::content_after_image_write(cmd,target,began);
}
static VKAPI_ATTR void VKAPI_CALL CmdCopyMemoryToImageIndirectKHR(VkCommandBuffer cmd,const VkCopyMemoryToImageIndirectInfoKHR* info){
  auto fn=image_copy_proc<PFN_vkCmdCopyMemoryToImageIndirectKHR>(cmd,"vkCmdCopyMemoryToImageIndirectKHR","vkCmdCopyMemoryToImageIndirectKHR");if(!fn)return;const bool began=omm::content_before_image_write(cmd,info->dstImage,true);fn(cmd,info);omm::content_after_image_write(cmd,info->dstImage,began);
}

static VKAPI_ATTR void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer cmd, uint32_t n, const VkCommandBuffer* cmds) {
  if(auto* state=cmd_state(cmd);state && state->geo.kind==CmdState::GeoPass::Kind::Material)state->geo_material_no_writes=false;
  diag(DIAG_EXEC);
  if(auto* state=cmd_state(cmd);state&&state->tracing&&state->in_pass)terrain_capture_secondary(cmd,state);
  vk(cmd).CmdExecuteCommands(cmd, n, cmds);
  omm::command_execute(cmd,{cmds,n});
  CmdState* s = cmd_state(cmd);
  if (!s) return;
  // Streamline bookkeeping recorded in secondaries counts when the primary is submitted.
  for (uint32_t i = 0; i < n; ++i) {
    CmdState* sec = cmd_state(cmds[i]);
    if (!sec) continue;
    s->refs.insert(s->refs.end(), sec->refs.begin(), sec->refs.end());   // submitted with the primary
    for (auto& [img, lay] : sec->sl_layouts) sl_note_barrier(s, img, lay);
    for (auto& [img, lay] : sec->sl_input_layouts) sl_note_event_barrier(s, img, lay);
    if (sec->sl_cam_valid) {
      memcpy(s->sl_cam, sec->sl_cam, sizeof s->sl_cam);
      s->sl_cam_valid = true;
    }
    if (sec->sl_hudless.image) s->sl_hudless = sec->sl_hudless;
    if (sec->sl_ui.image) s->sl_ui = sec->sl_ui;
    if (sec->sl_mvec_copy.image) {
      s->sl_mvec_copy = sec->sl_mvec_copy;
      s->sl_depth_copy = sec->sl_depth_copy;
      s->sl_mvec_slot = sec->sl_mvec_slot;
    }
  }
  if (!s->tracing) return;
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

// ---- the game's events ----
// BG3 orders its frame with vkCmdSetEvent2 / vkCmdWaitEvents2, several with image layout
// transitions. An event only covers what was recorded before vkCmdSetEvent, so work of
// ours recorded between the game's SetEvent and WaitEvents (the post-lighting passes:
// D-LS, D-RF, the GI composite; rrpost steps) raced the transitions and the game's
// later writes: syncval reported vkCmdWaitEvents2KHR layout transitions of images "previously
// read/written by vkCmdDispatch" (204 of the 600 hazards), and D-LS (which runs after the
// lighting, unlike the sun pass, which runs before it) was the only shader that faulted.
//   - vkCmdSetEvent(2): the core's pending post-lighting work is recorded first (core
//     on_cmd_end = gi flush), inside the event's first synchronization scope.
//   - vkCmdWaitEvents(2): if work of ours was recorded since (rrpost), a barrier from
//     every stage we use orders it before the wait and its transitions.
static void flush_ours_before_event(VkCommandBuffer cmd) {
  CmdState* s = cmd_state(cmd);
  if (s && !s->in_pass && cfg_hot(CfgHot::rt_event_flush)) core_on_cmd_end(cmd);
}

static void barrier_ours_before_wait(VkCommandBuffer cmd) {
  CmdState* s = cmd_state(cmd);
  if (!s || !s->ours_since_event || !cfg_hot(CfgHot::rt_event_flush)) return;
  s->ours_since_event = false;
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  vk(cmd).CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdSetEvent(VkCommandBuffer cmd, VkEvent e, VkPipelineStageFlags mask) {
  flush_ours_before_event(cmd);
  vk(cmd).CmdSetEvent(cmd, e, mask);
}

static VKAPI_ATTR void VKAPI_CALL CmdSetEvent2(VkCommandBuffer cmd, VkEvent e, const VkDependencyInfo* dep) {
  flush_ours_before_event(cmd);
  vk(cmd).CmdSetEvent2(cmd, e, dep);
}

static VKAPI_ATTR void VKAPI_CALL CmdWaitEvents(VkCommandBuffer cmd, uint32_t n, const VkEvent* ev, VkPipelineStageFlags src,
                                                VkPipelineStageFlags dst, uint32_t mbc, const VkMemoryBarrier* mb,
                                                uint32_t bbc, const VkBufferMemoryBarrier* bb, uint32_t ibc,
                                                const VkImageMemoryBarrier* ib) {
  barrier_ours_before_wait(cmd);
  vk(cmd).CmdWaitEvents(cmd, n, ev, src, dst, mbc, mb, bbc, bb, ibc, ib);
  // BG3 performs sampled-input transitions in event waits too. Tracking only
  // pipeline barriers left FG/deferred readbacks with stale attachment layouts.
  if (cfg_hot(CfgHot::fg_input_copy) || cfg_hot(CfgHot::pan_inputs)) {
    CmdState* s = cmd_state(cmd, true);
    for (uint32_t i = 0; i < ibc; ++i) sl_note_event_barrier(s, ib[i].image, ib[i].newLayout);
  }
}

static VKAPI_ATTR void VKAPI_CALL CmdWaitEvents2(VkCommandBuffer cmd, uint32_t n, const VkEvent* ev,
                                                 const VkDependencyInfo* deps) {
  barrier_ours_before_wait(cmd);
  vk(cmd).CmdWaitEvents2(cmd, n, ev, deps);
  if (cfg_hot(CfgHot::fg_input_copy) || cfg_hot(CfgHot::pan_inputs)) {
    CmdState* s = cmd_state(cmd, true);
    for (uint32_t event = 0; event < n; ++event)
      for (uint32_t i = 0; i < deps[event].imageMemoryBarrierCount; ++i) {
        const auto& b = deps[event].pImageMemoryBarriers[i];
        sl_note_event_barrier(s, b.image, b.newLayout);
      }
  }
}

void register_cmd_hooks(HookMap& m) {
  auto add = [&](const char* name, PFN_vkVoidFunction fn) {
    m[std::string("vk") + name] = fn;
    m[std::string("vk") + name + "KHR"] = fn;
  };
#define HOOK(n) add(#n, reinterpret_cast<PFN_vkVoidFunction>(&n))
  HOOK(CreateCommandPool);
  HOOK(DestroyCommandPool);
  HOOK(ResetCommandPool);
  HOOK(ResetCommandBuffer);
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
  HOOK(CmdCopyImage2);
  HOOK(CmdCopyBufferToImage);
  HOOK(CmdCopyBufferToImage2);
  HOOK(CmdBlitImage);
  HOOK(CmdBlitImage2);
  HOOK(CmdResolveImage);
  HOOK(CmdResolveImage2);
  m["vkCmdCopyMemoryToImageIndirectNV"]=reinterpret_cast<PFN_vkVoidFunction>(&CmdCopyMemoryToImageIndirectNV);
  m["vkCmdCopyMemoryToImageIndirectKHR"]=reinterpret_cast<PFN_vkVoidFunction>(&CmdCopyMemoryToImageIndirectKHR);
  HOOK(CmdClearColorImage);
  HOOK(CmdClearDepthStencilImage);
  HOOK(CmdExecuteCommands);
  HOOK(CmdPipelineBarrier);
  HOOK(CmdPipelineBarrier2);
  HOOK(CmdBindVertexBuffers);
  HOOK(CmdBindVertexBuffers2);
  HOOK(CmdBindIndexBuffer);
  HOOK(CmdCopyBuffer);
  HOOK(CmdCopyBuffer2);
  HOOK(CmdUpdateBuffer);
  HOOK(CmdFillBuffer);
  HOOK(CmdSetEvent);
  HOOK(CmdSetEvent2);
  HOOK(CmdWaitEvents);
  HOOK(CmdWaitEvents2);
  m["vkCmdBindVertexBuffers2EXT"] = reinterpret_cast<PFN_vkVoidFunction>(&CmdBindVertexBuffers2EXT);
#undef HOOK
}

}  // namespace bg3
