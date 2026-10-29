// Always-on, cheap bookkeeping of the objects the frame graph refers to:
// shader modules (by SPIR-V hash), pipelines, images, views, framebuffers,
// render passes and descriptor set contents.
#include "host.h"

#include <algorithm>
#include <cstring>
#include <deque>

namespace bg3 {

using HookMap = std::unordered_map<std::string, PFN_vkVoidFunction>;

Tracker& tracker() {
  static Tracker t;
  return t;
}

// ---------------------------------------------------------------------------
// Lifetime of game objects our GPU work references (host.h, life_*)
//
// BG3 recreates its render targets at level-load transitions without waiting
// for the frame in flight: it destroys images that the previous frame's still
// pending command buffer reads (through push descriptors, which the validation
// layer does not track). Our work recorded into that command buffer (RR before
// post, the RR guides, the GI/RT passes) reads the same images through our own
// descriptor sets and views, so it would read freed memory on the GPU. The
// destroy hooks therefore wait for the fence of the present that followed the
// object's last submission before letting the destroy through.
// ---------------------------------------------------------------------------
namespace {
struct LifeRef {
  uint64_t frame;   // frame_index() at submit: covered by the fence of present frame + 1
  uint64_t seq;     // submit sequence number (device-idle waits)
  VkQueue queue;
};
struct FrameFence {
  uint64_t frame;   // present number
  VkFence fence;
  VkQueue queue;
  int waiters;
};
std::mutex g_life_mu;
std::unordered_map<uint64_t, LifeRef> g_life;   // handle -> its last submission
std::deque<FrameFence> g_fences;                // presents not yet seen complete, in order
std::vector<VkFence> g_free_fences;
uint64_t g_life_done = 0;                       // presents <= this completed on the GPU
VkQueue g_present_queue = VK_NULL_HANDLE;       // the queue g_life_done is about
std::atomic<uint64_t> g_submit_seq{0};
std::atomic<uint64_t> g_idle_seq{0};
// Statistics (logged with every wait and every ~10 s while waits happen).
uint64_t g_life_waits = 0, g_life_noop = 0, g_life_unfenced = 0, g_life_timeouts = 0;
double g_life_wait_ms = 0.0, g_life_wait_max = 0.0;
uint64_t g_life_logged = 0;
std::atomic<uint64_t> g_life_submit_ticks{0}, g_life_noted{0};   // bookkeeping cost at submit
}  // namespace

void life_note(CmdState* s, uint64_t h) {
  if (!s || !h) return;
  if (!s->refs.empty() && s->refs.back() == h) return;
  s->refs.push_back(h);
}

void life_note(VkCommandBuffer cmd, uint64_t h) { life_note(cmd_state(cmd), h); }

void life_on_submit(VkQueue queue, const std::vector<VkCommandBuffer>& cmds) {
  const uint64_t seq = ++g_submit_seq;
  const uint64_t frame = frame_index();
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  struct Tick {
    LARGE_INTEGER& t0;
    LARGE_INTEGER& t1;
    ~Tick() {
      QueryPerformanceCounter(&t1);
      g_life_submit_ticks += uint64_t(t1.QuadPart - t0.QuadPart);
    }
  } tick{t0, t1};
  std::lock_guard<std::mutex> lock(g_life_mu);
  static VkQueue seen[4] = {};
  bool any = false;
  for (VkCommandBuffer c : cmds) {
    CmdState* s = cmd_state(c);
    if (!s) continue;
    any |= !s->refs.empty();
    g_life_noted += s->refs.size();
    for (uint64_t h : s->refs) g_life[h] = LifeRef{frame, seq, queue};
  }
  if (any) {
    bool known = false;
    for (VkQueue q : seen) known |= q == queue;
    for (VkQueue& q : seen)
      if (!known && !q) {
        q = queue;
        log("[life] work of ours is submitted on queue %p", (void*)queue);
        break;
      }
  }
}

// Highest present number whose fence has signalled: all work submitted to the
// present queue up to that present has finished on the GPU.
uint64_t life_gpu_done() {
  std::lock_guard<std::mutex> lock(g_life_mu);
  return g_life_done;
}

// `h` is in use by work of ours submitted in frame `frame` (e.g. a scene slot's
// alpha set handed out to the RT passes, recorded in another command buffer than
// the one that noted the views): a destroy waits for the present fence after it.
void life_extend(uint64_t h, uint64_t frame) {
  if (!h) return;
  std::lock_guard<std::mutex> lock(g_life_mu);
  if (!g_present_queue) return;   // no present fence yet: nothing to wait on anyway
  LifeRef& r = g_life[h];
  if (r.frame >= frame && r.queue == g_present_queue) return;
  r.frame = (std::max)(r.frame, frame);
  r.seq = g_submit_seq.load() + 1;   // a later device-idle wait still covers it
  r.queue = g_present_queue;
}

// Status of our latest present fence (a 0-timeout wait), for the fault poll thread
// (devfault.cpp): VK_ERROR_DEVICE_LOST once the device is lost, even if the game
// never calls Vulkan again. VK_SUCCESS when there is nothing to poll.
bool g_life_stopped = false;
VkResult life_poll_fence(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_life_mu);
  if (g_life_stopped || !dd) return VK_ERROR_INITIALIZATION_FAILED;   // the device is gone: stop polling
  if (g_fences.empty()) return VK_SUCCESS;
  return dd->vk.WaitForFences(dd->device, 1, &g_fences.back().fence, VK_TRUE, 0);
}
size_t life_pending_fences() {
  std::lock_guard<std::mutex> lock(g_life_mu);
  return g_fences.size();
}

uint64_t life_idle_begin() { return g_submit_seq.load(); }
void life_idle_end(uint64_t seq_before) {
  uint64_t cur = g_idle_seq.load();
  while (cur < seq_before && !g_idle_seq.compare_exchange_weak(cur, seq_before)) {
  }
}

void life_on_present(DeviceData* dd, VkQueue queue) {
  const uint64_t frame = frame_index();
  std::unique_lock<std::mutex> lock(g_life_mu);
  bool lost = false;   // reported after the lock (devfault.cpp reads life_gpu_done)
  // Retire completed presents (in order; one with a waiter stays until it leaves).
  while (!g_fences.empty()) {
    FrameFence& f = g_fences.front();
    const VkResult fr = dd->vk.WaitForFences(dd->device, 1, &f.fence, VK_TRUE, 0);   // a status poll
    lost = lost || fr == VK_ERROR_DEVICE_LOST;
    if (fr != VK_SUCCESS) break;
    g_life_done = (std::max)(g_life_done, f.frame);
    g_present_queue = f.queue;
    if (f.waiters) break;
    if (dd->vk.ResetFences(dd->device, 1, &f.fence) == VK_SUCCESS) g_free_fences.push_back(f.fence);
    else dd->vk.DestroyFence(dd->device, f.fence, nullptr);
    g_fences.pop_front();
  }
  static VkQueue logged_q = VK_NULL_HANDLE;
  if (logged_q != queue) {
    logged_q = queue;
    log("[life] presents on queue %p: its fences cover the work submitted to that queue", (void*)queue);
  }
  // This present's fence: all work submitted to `queue` before it.
  VkFence fence = VK_NULL_HANDLE;
  if (!g_free_fences.empty()) {
    fence = g_free_fences.back();
    g_free_fences.pop_back();
  } else {
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (dd->vk.CreateFence(dd->device, &fci, nullptr, &fence) != VK_SUCCESS) fence = VK_NULL_HANDLE;
  }
  if (fence) {
    if (dd->vk.QueueSubmit(queue, 0, nullptr, fence) == VK_SUCCESS) g_fences.push_back({frame, fence, queue, 0});
    else g_free_fences.push_back(fence);   // never submitted: still unsignaled
  }
  // Drop entries whose last submission is known complete.
  if ((frame & 255) == 0) {
    const uint64_t idle = g_idle_seq.load();
    for (auto it = g_life.begin(); it != g_life.end();)
      it = (it->second.frame + 1 <= g_life_done || it->second.seq <= idle) ? g_life.erase(it) : std::next(it);
  }
  static ULONGLONG last = 0, last_cost = 0;
  static uint64_t last_waits = 0, last_unfenced = 0, cost_frame = 0;
  ULONGLONG now = GetTickCount64();
  if (!last_cost) last_cost = now, cost_frame = frame;
  if (now - last_cost >= 60000 && frame > cost_frame) {
    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    const double frames = double(frame - cost_frame);
    log("[life] bookkeeping: %.1f references noted and %.3f ms CPU at submit per frame (over %.0f frames); %zu objects "
        "tracked, %zu present fences pending",
        double(g_life_noted.exchange(0)) / frames, double(g_life_submit_ticks.exchange(0)) * 1000.0 / double(fq.QuadPart) / frames,
        frames, g_life.size(), g_fences.size());
    last_cost = now;
    cost_frame = frame;
  }
  if ((g_life_waits != last_waits || g_life_unfenced != last_unfenced) && now - last > 10000) {
    last = now;
    last_waits = g_life_waits;
    last_unfenced = g_life_unfenced;
    log("[life] so far: %llu destroys waited for in-flight work (%.3f ms total, max %.3f ms, %llu timeouts), %llu "
        "found it already complete, %llu with no fence yet (submitted this frame); %zu objects tracked",
        (unsigned long long)g_life_waits, g_life_wait_ms, g_life_wait_max, (unsigned long long)g_life_timeouts,
        (unsigned long long)g_life_noop, (unsigned long long)g_life_unfenced, g_life.size());
  }
  lock.unlock();
  if (lost) fault_on_result(dd, VK_ERROR_DEVICE_LOST, "the present fence poll");
}

void life_device_destroyed(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_life_mu);
  g_life_stopped = true;
  if (!g_fences.empty()) dd->vk.DeviceWaitIdle(dd->device);
  for (const FrameFence& f : g_fences) dd->vk.DestroyFence(dd->device, f.fence, nullptr);
  for (VkFence f : g_free_fences) dd->vk.DestroyFence(dd->device, f, nullptr);
  g_fences.clear();
  g_free_fences.clear();
  g_life.clear();
}

void life_before_destroy(uint64_t h, const char* what) {
  if (!h) return;
  DeviceData* dd = main_device();
  if (!dd) return;
  FrameFence* f = nullptr;
  LifeRef r{};
  {
    std::lock_guard<std::mutex> lock(g_life_mu);
    auto it = g_life.find(h);
    if (it == g_life.end()) return;
    r = it->second;
    g_life.erase(it);
    const bool idle = r.seq <= g_idle_seq.load(), done = r.queue == g_present_queue && r.frame + 1 <= g_life_done;
    if (cfg_int("life_log", 0) && g_life_logged++ < 256)
      log("[life] destroy %s %p: last submitted in frame %llu (seq %llu, queue %p), now frame %llu, presents done %llu, "
          "idle seq %llu: %s",
          what, (void*)h, (unsigned long long)r.frame, (unsigned long long)r.seq, (void*)r.queue,
          (unsigned long long)frame_index(), (unsigned long long)g_life_done, (unsigned long long)g_idle_seq.load(),
          idle ? "complete (device idle since)" : done ? "complete (fence)" : "checking");
    if (idle || done) return;
    for (FrameFence& x : g_fences)
      if (x.frame >= r.frame + 1 && x.queue == r.queue) {
        f = &x;
        break;
      }
    if (!f) {
      // Submitted in this frame (no present since) or on a queue that never
      // presents: there is no fence of ours to wait on.
      ++g_life_unfenced;
      if (g_life_logged++ < 64)
        log("[life] the game destroys %s %p last used by our work in frame %llu (now %llu, queue %p): no fence covers "
            "it yet, not waited",
            what, (void*)h, (unsigned long long)r.frame, (unsigned long long)frame_index(), (void*)r.queue);
      return;
    }
    ++f->waiters;
  }
  LARGE_INTEGER t0, t1, fq;
  QueryPerformanceCounter(&t0);
  VkResult st = dd->vk.WaitForFences(dd->device, 1, &f->fence, VK_TRUE, 0);   // a status poll
  VkResult res = st;
  if (st == VK_TIMEOUT) res = dd->vk.WaitForFences(dd->device, 1, &f->fence, VK_TRUE, 2000000000ull);
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&fq);
  const double ms = double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(fq.QuadPart);
  std::lock_guard<std::mutex> lock(g_life_mu);
  const uint64_t fframe = f->frame;
  --f->waiters;
  if (res == VK_SUCCESS && f->queue == g_present_queue) g_life_done = (std::max)(g_life_done, fframe);
  if (st == VK_SUCCESS) {
    ++g_life_noop;
    return;
  }
  ++g_life_waits;
  if (res == VK_TIMEOUT) ++g_life_timeouts;
  g_life_wait_ms += ms;
  g_life_wait_max = (std::max)(g_life_wait_max, ms);
  if (g_life_logged++ < 64)
    log("[life] the game destroys %s %p that our work of frame %llu may still use (now frame %llu): waited %.3f ms for "
        "present %llu's fence (%s)",
        what, (void*)h, (unsigned long long)r.frame, (unsigned long long)frame_index(), ms, (unsigned long long)fframe,
        res == VK_SUCCESS ? "complete" : res == VK_TIMEOUT ? "TIMEOUT, destroyed anyway" : "error");
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

uint32_t set_dynamic_count_locked(VkDescriptorSet set) {
  auto l = tracker().set_layouts.find(set);
  if (l != tracker().set_layouts.end()) {
    auto d = tracker().layout_dynamic.find(l->second);
    if (d != tracker().layout_dynamic.end()) {
      uint32_t n = 0;
      for (auto& [b, c] : d->second) n += c;
      return n;
    }
  }
  auto it = tracker().dynamic_counts.find(set);
  return it == tracker().dynamic_counts.end() ? 0 : it->second;
}

uint32_t set_dynamic_index_locked(VkDescriptorSet set, uint32_t binding, uint32_t element) {
  auto l = tracker().set_layouts.find(set);
  if (l == tracker().set_layouts.end()) return UINT32_MAX;
  auto d = tracker().layout_dynamic.find(l->second);
  if (d == tracker().layout_dynamic.end()) return UINT32_MAX;
  uint32_t index = 0;
  for (auto& [b, c] : d->second) {
    if (b == binding) return element < c ? index + element : UINT32_MAX;
    if (b > binding) break;
    index += c;
  }
  return UINT32_MAX;
}

// ---- hooks ----

static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* ci,
                                                                const VkAllocationCallbacks* alloc,
                                                                VkDescriptorSetLayout* out) {
  VkResult r = device_of(device)->vk.CreateDescriptorSetLayout(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    std::vector<std::pair<uint32_t, uint32_t>> dyn;
    for (uint32_t i = 0; i < ci->bindingCount; ++i)
      if (is_dynamic_type(ci->pBindings[i].descriptorType))
        dyn.emplace_back(ci->pBindings[i].binding, ci->pBindings[i].descriptorCount);
    std::sort(dyn.begin(), dyn.end());
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().layout_dynamic[*out] = std::move(dyn);
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* ai,
                                                             VkDescriptorSet* out) {
  VkResult r = device_of(device)->vk.AllocateDescriptorSets(device, ai, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    for (uint32_t i = 0; i < ai->descriptorSetCount; ++i) tracker().set_layouts[out[i]] = ai->pSetLayouts[i];
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* ci,
                                                         const VkAllocationCallbacks* alloc, VkShaderModule* out) {
  diag(DIAG_SHADER_MODULES);
  // Track D: vertex shaders get transform feedback decorations on Position
  // (geo_capture=1). Hashes and dumps stay those of the game's own SPIR-V.
  std::vector<uint32_t> patched;
  VkShaderModuleCreateInfo pci;
  const bool geo = geo_patch_shader(ci, patched);
  if (geo) {
    pci = *ci;
    pci.pCode = patched.data();
    pci.codeSize = patched.size() * 4;
  }
  VkResult r = device_of(device)->vk.CreateShaderModule(device, geo ? &pci : ci, alloc, out);
  bool used_patched = geo && r == VK_SUCCESS;
  if (r != VK_SUCCESS && geo) {
    log("[geo] patched vertex shader rejected (%d); using the original", r);
    r = device_of(device)->vk.CreateShaderModule(device, ci, alloc, out);
  } else if (r == VK_SUCCESS) {
    geo_note_module(*out, geo);
  }
  if (r == VK_SUCCESS) {
    uint64_t h = hash64(ci->pCode, ci->codeSize);
    shader_dump(h, ci->pCode, ci->codeSize);
    if (aftermath_enabled()) {
      // The driver sees the patched module when the capture patched it.
      char what[48];
      snprintf(what, sizeof what, "game %016llx%s", (unsigned long long)h, used_patched ? " (XFB-patched)" : "");
      const std::string dumped = root_dir() + "\\captures\\shaders\\" + [&] {
        char b[24];
        snprintf(b, sizeof b, "%016llx.spv", (unsigned long long)h);
        return std::string(b);
      }();
      if (used_patched) aftermath_note_spirv(what, pci.pCode, pci.codeSize, nullptr);
      else aftermath_note_spirv(what, ci->pCode, ci->codeSize, cfg_int("dump_shaders", 0) ? dumped.c_str() : nullptr);
    }
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().modules[*out] = h;
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyShaderModule(VkDevice device, VkShaderModule m, const VkAllocationCallbacks* a) {
  device_of(device)->vk.DestroyShaderModule(device, m, a);
  geo_forget_module(m);
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
  geo_note_pipelines(count, cis, out);
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
  geo_forget_pipeline(p);
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
  // Same for R8 render targets: the sun shadow mask, refined by contact shadows.
  static const bool force_storage_r8 = cfg_int("force_storage_r8", 1) != 0;
  if (force_storage_r8 && ci->format == VK_FORMAT_R8_UNORM && (ci->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
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
  life_before_destroy((uint64_t)img, "image");   // first: the hooks below release our views of it
  sl_note_image_destroyed(img);
  ngx_on_destroy_image(img);  // our views of it go first
  rrpost_forget_image(img);
  device_of(device)->vk.DestroyImage(device, img, a);
  ngx_forget_image(img);
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
  life_before_destroy((uint64_t)v, "image view");
  sl_note_view_destroyed(v);
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
  VkResult r;
  if (!sl_create_swapchain(device, ci, alloc, out, &r)) r = device_of(device)->vk.CreateSwapchainKHR(device, ci, alloc, out);
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
  VkResult r;
  if (!sl_get_swapchain_images(device, sc, count, images, &r))
    r = device_of(device)->vk.GetSwapchainImagesKHR(device, sc, count, images);
  if (images && (r == VK_SUCCESS || r == VK_INCOMPLETE)) {
    VkSwapchainCreateInfoKHR sci{};
    {
      std::lock_guard<std::mutex> lock(g_sc_mu);
      auto it = g_swapchains.find(sc);
      if (it != g_swapchains.end()) sci = it->second;
    }
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    for (uint32_t i = 0; i < *count; ++i) {
      auto known = tracker().images.find(images[i]);
      if (known != tracker().images.end()) {
        // Interposed swapchain images may already have passed CreateImage.
        // They still are presentation images, not ordinary offscreen targets.
        known->second.swapchain = true;
        continue;
      }
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

static VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR sc, const VkAllocationCallbacks* a) {
  if (!sl_destroy_swapchain(device, sc, a)) device_of(device)->vk.DestroySwapchainKHR(device, sc, a);
  std::lock_guard<std::mutex> lock(g_sc_mu);
  g_swapchains.erase(sc);
}

// ---- host-visible memory, so uniform buffers can be read on the CPU ----

static VKAPI_ATTR VkResult VKAPI_CALL BindBufferMemory(VkDevice device, VkBuffer b, VkDeviceMemory m, VkDeviceSize o) {
  VkResult r = device_of(device)->vk.BindBufferMemory(device, b, m, o);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().buffer_mem[b] = {m, o};
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL BindBufferMemory2(VkDevice device, uint32_t n, const VkBindBufferMemoryInfo* infos) {
  VkResult r = device_of(device)->vk.BindBufferMemory2(device, n, infos);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    for (uint32_t i = 0; i < n; ++i) tracker().buffer_mem[infos[i].buffer] = {infos[i].memory, infos[i].memoryOffset};
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice device, VkBuffer b, const VkAllocationCallbacks* a) {
  life_before_destroy((uint64_t)b, "buffer");
  // Our caches keyed by the buffer go first: the handle may be reused at once.
  bvh_forget_buffer(b);
  geo_forget_buffer(b);
  device_of(device)->vk.DestroyBuffer(device, b, a);
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().buffer_mem.erase(b);
}

static VKAPI_ATTR VkResult VKAPI_CALL MapMemory(VkDevice device, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize size,
                                                VkMemoryMapFlags f, void** data) {
  VkResult r = device_of(device)->vk.MapMemory(device, m, o, size, f, data);
  if (r == VK_SUCCESS && data && *data) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().mapped[m] = static_cast<uint8_t*>(*data) - o;
  }
  return r;
}

static VKAPI_ATTR void VKAPI_CALL UnmapMemory(VkDevice device, VkDeviceMemory m) {
  {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().mapped.erase(m);
  }
  device_of(device)->vk.UnmapMemory(device, m);
}

static VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice device, VkDeviceMemory m, const VkAllocationCallbacks* a) {
  {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().mapped.erase(m);
  }
  device_of(device)->vk.FreeMemory(device, m, a);
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
  HOOK(CreateDescriptorSetLayout);
  HOOK(AllocateDescriptorSets);
  HOOK(CreateDescriptorUpdateTemplate);
  HOOK(DestroyDescriptorUpdateTemplate);
  HOOK(UpdateDescriptorSetWithTemplate);
  m["vkCreateSwapchainKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&CreateSwapchainKHR);
  m["vkGetSwapchainImagesKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&GetSwapchainImagesKHR);
  m["vkDestroySwapchainKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&DestroySwapchainKHR);
  HOOK(BindBufferMemory);
  HOOK(BindBufferMemory2);
  HOOK(DestroyBuffer);
  HOOK(MapMemory);
  HOOK(UnmapMemory);
  HOOK(FreeMemory);
#undef HOOK
}

}  // namespace bg3
