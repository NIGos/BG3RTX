// Production function bodies are inserted by core_resource_init_test.py.
#include "common/host_api.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <source_location>
#include <string>
#include <unordered_map>
static int calls, fail_at, checks;
static uint64_t serial = 100;
static std::set<uint64_t> live, retired;
static void check(bool ok, std::source_location at = std::source_location::current()) { ++checks; if (!ok) { std::fprintf(stderr, "check %d failed at line %u\n", checks, at.line()); std::exit(1); } }
template<class T> static VkResult create(T* out) {
  if (++calls == fail_at) return VK_ERROR_OUT_OF_HOST_MEMORY;
  *out = (T)++serial; live.insert(serial); return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL set_layout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout* out) { return create(out); }
static VKAPI_ATTR VkResult VKAPI_CALL pipe_layout(VkDevice, const VkPipelineLayoutCreateInfo* info, const VkAllocationCallbacks*, VkPipelineLayout* out) {
  for (uint32_t i = 0; i < info->setLayoutCount; ++i) check(info->pSetLayouts[i] != VK_NULL_HANDLE);
  return create(out);
}
static VKAPI_ATTR VkResult VKAPI_CALL sampler(VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* out) { return create(out); }
static VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* out) { return create(out); }
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* out) {
  if (++calls == fail_at) return VK_ERROR_OUT_OF_POOL_MEMORY;
  for (uint32_t i = 0; i < info->descriptorSetCount; ++i) out[i] = (VkDescriptorSet)++serial;
  return VK_SUCCESS;
}
template<class T> static void destroy(T handle) { check(live.erase(uint64_t(handle)) == 1); }
static VKAPI_ATTR void VKAPI_CALL destroy_set(VkDevice, VkDescriptorSetLayout h, const VkAllocationCallbacks*) { destroy(h); }
static VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice, VkDescriptorPool h, const VkAllocationCallbacks*) { destroy(h); }
static void defer(Bg3DeferKind, uint64_t handle) { if (handle) { check(retired.insert(handle).second); live.erase(handle); } }
static bool alpha(VkDescriptorSetLayout* layout, VkDescriptorSet*) { *layout = (VkDescriptorSetLayout)1; return true; }
static void log(const char*, ...) {}
static VkDeviceFuncs vk{};
static Bg3HostApi api{};
const Bg3HostApi* host = &api;
namespace gi {
struct Push { uint32_t words[4]; };
constexpr uint32_t kBindings = 2, kRfBindings = 5, kLsBindings = 4, kNumExtras = 1, kSunRing = 4, kSunBindings = 9;
struct ExtraBinding { uint32_t binding; VkDescriptorType type; uint32_t layouts; };
const ExtraBinding kExtras[] = {{6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}};
uint32_t layout_mask(uint32_t n) { return n == kLsBindings ? 4u : 1u; }
bool ls_uses(uint32_t) { return true; }
VkDescriptorType binding_type(uint32_t) { return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; }
VkDescriptorType sun_binding_type(uint32_t) { return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; }
bool g_hair_origin_cap = false;
}
namespace bg3::hair_origin { bool binding_enabled(bool, uint32_t, uint32_t) { return true; } }
namespace gi {
VkSampler g_linear_clamp = VK_NULL_HANDLE;
VkDescriptorSetLayout g_sun_dsl = VK_NULL_HANDLE, g_ls_set_layout = VK_NULL_HANDLE;
VkDescriptorPool g_sun_pool = VK_NULL_HANDLE;
VkDescriptorSet g_sun_sets[kSunRing] = {};
VkPipelineLayout g_ls_layout = VK_NULL_HANDLE;
struct Pipe {
  VkPipeline pipeline = VK_NULL_HANDLE;
  struct Variant { VkPipeline pipeline = VK_NULL_HANDLE; };
  std::unordered_map<std::string, Variant> variants;
};
Pipe g_rf, g_rf_sharc;
VkPipelineLayout g_rf_layout = VK_NULL_HANDLE, g_rf_sharc_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout g_rf_set_layout = VK_NULL_HANDLE, g_rf_game_layout = VK_NULL_HANDLE;
void defer_layout_destroy(uint64_t h) { defer(BG3_DEFER_PIPELINE_LAYOUT, h); }
void rf_gi_drop() {}
// INSERT make_layout
// INSERT ensure_ls_layout
// INSERT ensure_rf_layout
}
namespace em {
struct Push { uint32_t words[4]; };
VkDescriptorSetLayout g_push = VK_NULL_HANDLE, g_empty = VK_NULL_HANDLE, g_em_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE, g_flush = VK_NULL_HANDLE;
uint64_t g_gen = 0, g_flush_gen = 0;
bool make_pipe(VkPipeline&, uint64_t&, const char*) { return true; }
// INSERT ensure
}
int main() {
  vk.CreateDescriptorSetLayout = set_layout; vk.CreatePipelineLayout = pipe_layout;
  vk.CreateSampler = sampler; vk.CreateDescriptorPool = pool; vk.AllocateDescriptorSets = allocate;
  vk.DestroyDescriptorSetLayout = destroy_set; vk.DestroyDescriptorPool = destroy_pool;
  api.vk = &vk; api.rt_alpha = alpha; api.defer_destroy = defer; api.log = log;
  for (int failure = 1; failure <= 2; ++failure) {
    live.clear(); calls = 0; fail_at = failure;
    VkDescriptorSetLayout set = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE;
    check(!gi::make_layout(2, &set, &layout)); check(!set && !layout && live.empty());
    fail_at = 0; check(gi::make_layout(2, &set, &layout)); check(set && layout && live.size() == 2);
  }
  for (int failure = 1; failure <= 6; ++failure) {
    live.clear(); calls = 0; fail_at = failure;
    gi::g_linear_clamp = VK_NULL_HANDLE; gi::g_sun_dsl = VK_NULL_HANDLE; gi::g_sun_pool = VK_NULL_HANDLE;
    gi::g_ls_layout = VK_NULL_HANDLE; gi::g_ls_set_layout = VK_NULL_HANDLE;
    std::fill_n(gi::g_sun_sets, gi::kSunRing, VkDescriptorSet{});
    check(!gi::ensure_ls_layout());
    check(bool(gi::g_sun_dsl) == (failure > 4) && bool(gi::g_sun_pool) == (failure > 4));
    for (auto set : gi::g_sun_sets) check(bool(set) == (failure > 4));
    check(live.size() == (failure > 4 ? 3u : size_t(failure > 1))); // sampler and complete sun tuple may survive
    fail_at = 0; check(gi::ensure_ls_layout());
    check(gi::g_sun_dsl && gi::g_sun_pool && gi::g_ls_layout);
    for (auto set : gi::g_sun_sets) check(set != VK_NULL_HANDLE);
  }
  for (int failure = 1; failure <= 3; ++failure) {
    live.clear(); retired.clear(); calls = 0; fail_at = failure;
    em::g_push = em::g_empty = em::g_em_layout = VK_NULL_HANDLE; em::g_layout = VK_NULL_HANDLE;
    check(!em::ensure((VkDescriptorSetLayout)1)); check(!em::g_push && !em::g_empty && !em::g_layout && live.empty());
    fail_at = 0; check(em::ensure((VkDescriptorSetLayout)1)); check(live.size() == 3);
    auto old = em::g_layout;
    calls = 0; fail_at = failure;
    check(!em::ensure((VkDescriptorSetLayout)2)); check(em::g_layout == old && live.size() == 3 && retired.empty());
    fail_at = 0; check(em::ensure((VkDescriptorSetLayout)2)); check(live.size() == 3 && retired.size() == 3);
  }
  live.clear(); retired.clear(); fail_at = 0;
  check(gi::ensure_rf_layout((VkDescriptorSetLayout)1));
  for (auto p : {&gi::g_rf, &gi::g_rf_sharc}) {
    for (int i = 0; i < 3; ++i) p->variants[std::to_string(i)].pipeline = (VkPipeline)++serial;
    p->pipeline = p->variants["0"].pipeline;
  }
  auto old = gi::g_rf_layout;
  calls = 0; fail_at = 2;
  check(!gi::ensure_rf_layout((VkDescriptorSetLayout)2)); check(gi::g_rf_layout == old && retired.empty());
  fail_at = 0; check(gi::ensure_rf_layout((VkDescriptorSetLayout)2));
  check(retired.size() == 8); // six variants and two layouts, each once
  check(gi::g_rf.variants.empty() && gi::g_rf_sharc.variants.empty());
  std::printf("%d resource initialization checks passed\n", checks);
}
