// Hot-reloadable core. Rebuild this DLL while the game runs and the host swaps
// it in at the next present. Everything that must outlive a reload goes through
// host->persist; every GPU object goes back through host->defer_destroy.
//
// What it does today:
//  - a pass census: every distinct pass signature (first fragment shader +
//    attachment formats + size) with how often it ran, logged on request;
//  - the pattern for hot-reloaded compute work: a pipeline rebuilt whenever
//    its HLSL source recompiles.
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <tuple>

#include "core.h"

const Bg3HostApi* host = nullptr;

// ---- pass census ----

struct PassKey {
  uint64_t fs;
  uint32_t w, h, colors;
  VkFormat c0, depth;
  bool operator<(const PassKey& o) const {
    return std::tie(fs, w, h, colors, c0, depth) < std::tie(o.fs, o.w, o.h, o.colors, o.c0, o.depth);
  }
};
struct PassStat {
  uint64_t count = 0;
  uint32_t draws = 0;
  uint64_t first_seen = 0;
};
static std::mutex g_census_mu;
static std::map<PassKey, PassStat> g_census;

static void census_dump() {
  std::lock_guard<std::mutex> lock(g_census_mu);
  host->log("pass census: %zu distinct signatures", g_census.size());
  for (auto& [k, s] : g_census)
    host->log("  fs %016llx  %4ux%-4u  colors %u (fmt %d) depth fmt %d  draws %u  runs %llu", (unsigned long long)k.fs,
              k.w, k.h, k.colors, k.c0, k.depth, s.draws, (unsigned long long)s.count);
}

// ---- hot-reloaded compute pipeline (template for GI work) ----

struct Compute {
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  uint64_t generation = 0;
};
static Compute g_probe;

static void compute_release(Compute& c) {
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)c.pipeline);
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)c.layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)c.set_layout);
  c = Compute{};
}

// Rebuilds the pipeline when its source compiled to a new generation.
static bool compute_refresh(Compute& c, const char* file) {
  Bg3ShaderBlob blob{};
  if (!host->get_shader(file, "main", "cs_6_6", &blob)) return false;
  if (c.pipeline && blob.generation == c.generation) return true;

  const VkDeviceFuncs& vk = *host->vk;
  VkDevice dev = host->device;
  if (!c.set_layout) {
    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sci.bindingCount = 1;
    sci.pBindings = &b;
    vk.CreateDescriptorSetLayout(dev, &sci, nullptr, &c.set_layout);
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &c.set_layout;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pc;
    vk.CreatePipelineLayout(dev, &lci, nullptr, &c.layout);
  }
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  // A real module: a null module with VkShaderModuleCreateInfo chained needs maintenance5.
  VkShaderModule mod = VK_NULL_HANDLE;
  if (vk.CreateShaderModule(dev, &mci, nullptr, &mod) != VK_SUCCESS) return c.pipeline != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main",
               nullptr};
  pci.layout = c.layout;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = vk.CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &p);
  vk.DestroyShaderModule(dev, mod, nullptr);
  if (r != VK_SUCCESS) {
    host->log("%s: pipeline creation failed (%d), keeping the previous one", file, r);
    return c.pipeline != VK_NULL_HANDLE;
  }
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)c.pipeline);
  c.pipeline = p;
  c.generation = blob.generation;
  host->log("%s: pipeline generation %llu ready", file, (unsigned long long)blob.generation);
  return true;
}

// ---- callbacks ----

static void on_frame(uint64_t frame) {
  compute_refresh(g_probe, "probe.hlsl");
  int* census_req = static_cast<int*>(host->persist("core.census_request", sizeof(int)));
  int want = host->cfg_int("census_dump", 0);
  if (want != *census_req) {
    *census_req = want;
    if (want) census_dump();
  }
}

static void on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  gi_on_pass_end(cmd, p);
  water_on_pass_end(cmd, p);
  PassKey k{p->first_fs_hash, p->width, p->height, p->color_count, p->color_count ? p->color_formats[0] : VK_FORMAT_UNDEFINED,
            p->depth_format};
  std::lock_guard<std::mutex> lock(g_census_mu);
  PassStat& s = g_census[k];
  if (!s.count) s.first_seen = host->frame_index();
  ++s.count;
  s.draws = p->draw_count;
}

static void on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  core_water_pass_take(cmd);   // a stale note (a pass end not seen) never names this pass
  gi_on_pass_begin(cmd, p);
}
static void on_dispatch(VkCommandBuffer cmd, const Bg3DispatchInfo* d) { gi_on_dispatch(cmd, d); }
static void on_cmd_end(VkCommandBuffer cmd) { gi_on_cmd_end(cmd); }
static void on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* d) { gi_on_dispatch_pre(cmd, d); }

static void unload() {
  gi_release();
  em_release();
  water_release();
  compute_release(g_probe);
  host->log("core unloading");
}

static const Bg3CoreApi g_api = {BG3RTX_ABI_VERSION, &unload, &on_frame, &on_pass_end, &on_pass_begin, &on_dispatch, &on_cmd_end, &on_dispatch_pre,
                                  &core_em_record, &core_em_calib_update, &core_em_calib_apply};

extern "C" __declspec(dllexport) const Bg3CoreApi* bg3rtx_core_load(const Bg3HostApi* h) {
  host = h;
  uint32_t* loads = static_cast<uint32_t*>(host->persist("core.loads", sizeof(uint32_t)));
  ++*loads;
  host->log("core built %s %s loaded (load #%u)", __DATE__, __TIME__, *loads);
  return &g_api;
}
