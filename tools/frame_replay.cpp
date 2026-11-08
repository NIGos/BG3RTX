// Offline frame replay harness (tools/frame_replay.md): loads a replay pack (common/replay_pack.h),
// rebuilds the frame's BLAS/TLAS from the pack's InstInfo + soup/pool buffers + instance array,
// binds every descriptor the pass reads, compiles the REAL production shader with the layer's DXC
// flags (layer/shaders.cpp) and dispatches it once or more, windowless. Writes the output images
// (raw + PFM), the pass's rtStats lines in the live log's wording (core/gi.cpp) and the GPU time.
//
//   frame_replay --pack <dir> [--out <dir>] [--hlsl <file> | --spv <file>] [--define N=V]... [--repeat N]
//                [--no-validation]
//   frame_replay --synthesize <dir> [--dark]     write the synthetic pack (the CTest's)
//   frame_replay --self-test <dir>               CTest frame_replay_test: synthesize lit + dark packs,
//                                                replay each twice from disk, check determinism and terms
//   frame_replay --pack <dir> --pt-reference [--pt-iters N] | --pt-cost
//                                                offline path-traced reference (tools/pt_reference.hlsl)
//   frame_replay --pt-self-test <dir>            CTest pt_reference_test: furnace + slab checks
// Edit shaders/..., rerun: the shader is recompiled from the checkout on every run (seconds).
// Exit 77 (CTest SKIP_RETURN_CODE): no Vulkan device with ray queries, or (self-test) no validation layer.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../common/replay_pack.h"
#include "../common/replay_codec.h"
#include "replay_ab.h"
#include "../common/native_hit_records.h"
#include "../common/native_hit_gi_bindings.h"
#include "../common/native_hit_rf_bindings.h"
#pragma comment(lib, "vulkan-1.lib")

namespace fs = std::filesystem;
namespace rp = bg3::replay;
static_assert(rp::kR32Sfloat == VK_FORMAT_R32_SFLOAT && rp::kR16G16Sfloat == VK_FORMAT_R16G16_SFLOAT &&
              rp::kR32G32Sfloat == VK_FORMAT_R32G32_SFLOAT && rp::kRgba16Sfloat == VK_FORMAT_R16G16B16A16_SFLOAT &&
              rp::kRgba32Sfloat == VK_FORMAT_R32G32B32A32_SFLOAT && rp::kRgba32Uint == VK_FORMAT_R32G32B32A32_UINT &&
              rp::kRgba8Unorm == VK_FORMAT_R8G8B8A8_UNORM && VK_FORMAT_R16_SFLOAT == 76 && VK_FORMAT_R16G16_UNORM == 77 &&
              VK_FORMAT_B10G11R11_UFLOAT_PACK32 == 122 && VK_FORMAT_BC1_RGB_UNORM_BLOCK == 131 && VK_FORMAT_BC4_UNORM_BLOCK == 139 &&
              VK_FORMAT_BC7_SRGB_BLOCK == 146 && VK_FORMAT_R16G16B16A16_UNORM == 91, "replay_pack.h VkFormat numbers");

namespace {
// ---------------------------------------------------------------- Vulkan context
struct Gpu {
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice dev{};
  VkQueue queue{};
  uint32_t family = 0;
  VkPhysicalDeviceMemoryProperties memory{};
  VkPhysicalDeviceAccelerationStructurePropertiesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  float tick_ns = 1;
  VkCommandPool pool{};
  VkCommandBuffer cmd{};
  PFN_vkCreateAccelerationStructureKHR createAS{};
  PFN_vkDestroyAccelerationStructureKHR destroyAS{};
  PFN_vkGetAccelerationStructureBuildSizesKHR sizesAS{};
  PFN_vkCmdBuildAccelerationStructuresKHR buildAS{};
  PFN_vkGetAccelerationStructureDeviceAddressKHR addressAS{};
  bool validation = false, pipeline_stats = false;
  PFN_vkGetPipelineExecutablePropertiesKHR executableProperties{};
  PFN_vkGetPipelineExecutableStatisticsKHR executableStatistics{};
  // --ab-rgen (SER prototype): pipeline B is a ray tracing pipeline with one raygen group.
  bool rt_pipeline = false;
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
  PFN_vkCreateRayTracingPipelinesKHR createRT{};
  PFN_vkGetRayTracingShaderGroupHandlesKHR groupHandles{};
  PFN_vkCmdTraceRaysKHR traceRays{};
  std::string name;
  uint32_t max_image_2d = 0, max_layers = 0;
  uint64_t device_bytes = 0;   // device-local memory allocated (never freed before exit)
};
Gpu g;
bool g_pipeline_stats = false;
fs::path g_reference_out;
std::map<uint32_t, uint32_t> g_specs;
std::map<uint32_t, std::pair<uint32_t, uint32_t>> g_ab_specs;
fs::path g_ab_spv;
fs::path g_ab_rgen;   // --ab-rgen <hlsl>: B is this file as a lib_6_6 raygen (tools/gi_trace_rgen.hlsl) over A's grid
uint32_t g_synth_size = 128;   // --synth-size N: --synthesize at N x N (a timing fixture; the CTests keep 128)
std::map<uint32_t, uint32_t> g_rgen_specs;   // --rgen-spec ID=V: B's specialization only (1050: the reorder hint)
bool g_ab_require_equal = false;
bool g_ab_numeric = false;
bool g_require_live_equal = false;
uint32_t g_ab_warmup = 0; // untimed GPU dispatches immediately before each A/B sample
bool g_ab_blas = false, g_blas_control = false;
// Explicit class policies: legacy preserves the historical replay; layer matches Ultra.
std::string g_blas_policy = "legacy";
uint32_t blas_class(uint32_t meta, bool cascade) {
  return cascade ? 2u : (!(meta & rp::kInfoPool) || (meta & (1u << 31)) ? 1u : 0u);
}
bool blas_class_test() {
  bool okay = blas_class(64, false) == 1 && blas_class(64 | rp::kInfoAlpha, false) == 1 &&
    blas_class(64 | rp::kInfoPool, false) == 0 &&
    blas_class(64 | rp::kInfoPool | (1u << 31), false) == 1 &&
    blas_class(64, true) == 2;
  for (uint32_t slot = 1; slot <= 4096; ++slot)
    okay &= blas_class(64 | rp::kInfoPool | rp::kInfoAlpha | (slot << 16), false) == 0;
  std::printf("%s BLAS classes: soup, alpha, pool, held, all 4096 pool alpha slots, cascade\n",okay?"PASS":"FAIL");
  return okay;
}
bool ab_mode() { return g_ab_blas || !g_ab_specs.empty() || !g_ab_spv.empty() || !g_ab_rgen.empty(); }
bool g_reference_mismatch = false;
std::atomic<unsigned> g_validation_errors{};
[[noreturn]] void die(const std::string& what) { std::fprintf(stderr, "FAIL %s\n", what.c_str()); std::exit(1); }
void check(bool yes, const std::string& what) { if (!yes) die(what); }
void vkcheck(VkResult r, const char* what) { if (r != VK_SUCCESS) die(std::string(what) + " result=" + std::to_string(int(r))); }
VKAPI_ATTR VkBool32 VKAPI_CALL on_validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* info, void*) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    if (g_validation_errors.fetch_add(1) < 16) std::fprintf(stderr, "VALIDATION %s\n", info->pMessage);
  return VK_FALSE;
}

// 0 ok, 77 skip (printed).
int init_gpu(bool want_validation, bool require_validation) {
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  // Private loader: never the game's implicit layers (VkLayer_bg3rtx), at most explicit validation.
  SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", "~implicit~");
  SetEnvironmentVariableA("VK_INSTANCE_LAYERS", nullptr);
  SetEnvironmentVariableA("VK_LOADER_LAYERS_ENABLE", nullptr);
  SetEnvironmentVariableA("VK_ADD_LAYER_PATH", nullptr);
  uint32_t nl = 0;
  if (vkEnumerateInstanceLayerProperties(&nl, nullptr) != VK_SUCCESS) { std::puts("SKIP no Vulkan loader"); return 77; }
  std::vector<VkLayerProperties> layers(nl);
  vkEnumerateInstanceLayerProperties(&nl, layers.data());
  bool have = false;
  for (auto& x : layers) have |= !std::strcmp(x.layerName, "VK_LAYER_KHRONOS_validation");
  if (require_validation && !have) { std::puts("SKIP Khronos validation layer not installed (never a silent pass)"); return 77; }
  g.validation = want_validation && have;
  const char* layer = "VK_LAYER_KHRONOS_validation";
  const char* debugExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
  debug.pfnUserCallback = on_validation;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "bg3rtx frame replay"; app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, g.validation ? &debug : nullptr};
  ici.pApplicationInfo = &app;
  if (g.validation) { ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = &layer; ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = &debugExt; }
  if (vkCreateInstance(&ici, nullptr, &g.instance) != VK_SUCCESS) { std::puts("SKIP no Vulkan 1.3 instance"); return 77; }
  if (g.validation) {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(g.instance, "vkCreateDebugUtilsMessengerEXT"));
    VkDebugUtilsMessengerEXT messenger;
    vkcheck(create(g.instance, &debug, nullptr, &messenger), "debug messenger");   // lives until exit
  }
  uint32_t np = 0;
  vkEnumeratePhysicalDevices(g.instance, &np, nullptr);
  std::vector<VkPhysicalDevice> physical(np);
  if (np) vkEnumeratePhysicalDevices(g.instance, &np, physical.data());
  VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &f12};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR asf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &asf};
  for (auto p : physical) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(p, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) continue;
    vkGetPhysicalDeviceFeatures2(p, &features);
    if (asf.accelerationStructure && rq.rayQuery && f12.bufferDeviceAddress && f12.runtimeDescriptorArray &&
        f12.shaderSampledImageArrayNonUniformIndexing && features.features.shaderStorageImageExtendedFormats) { g.physical = p; break; }
  }
  if (!g.physical) { std::puts("SKIP no Vulkan 1.3 device with ray queries (not passed)"); return 77; }
  VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &g.as};
  vkGetPhysicalDeviceProperties2(g.physical, &props2);
  g.name = props2.properties.deviceName;
  g.tick_ns = props2.properties.limits.timestampPeriod;
  g.max_image_2d = props2.properties.limits.maxImageDimension2D;
  g.max_layers = props2.properties.limits.maxImageArrayLayers;
  vkGetPhysicalDeviceMemoryProperties(g.physical, &g.memory);
  uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(g.physical, &nq, nullptr);
  std::vector<VkQueueFamilyProperties> families(nq); vkGetPhysicalDeviceQueueFamilyProperties(g.physical, &nq, families.data());
  g.family = UINT32_MAX;
  for (uint32_t i = 0; i < nq; ++i) if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && families[i].timestampValidBits) { g.family = i; break; }
  check(g.family != UINT32_MAX, "compute queue with timestamps");
  std::vector<const char*> extensions = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR executable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
  if (g_pipeline_stats) {
    uint32_t count = 0;
    vkcheck(vkEnumerateDeviceExtensionProperties(g.physical, nullptr, &count, nullptr), "device extension count");
    std::vector<VkExtensionProperties> available(count);
    vkcheck(vkEnumerateDeviceExtensionProperties(g.physical, nullptr, &count, available.data()), "device extensions");
    for (const auto& x : available) if (!std::strcmp(x.extensionName, VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME)) {
      VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &executable};
      vkGetPhysicalDeviceFeatures2(g.physical, &query);
      g.pipeline_stats = executable.pipelineExecutableInfo != VK_FALSE;
      break;
    }
    if (g.pipeline_stats) extensions.push_back(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
    else std::puts("[pipeline-stats] unavailable: VK_KHR_pipeline_executable_properties feature not supported");
  }
  VkPhysicalDeviceVulkan12Features use12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  use12.bufferDeviceAddress = VK_TRUE; use12.runtimeDescriptorArray = VK_TRUE; use12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  use12.hostQueryReset = VK_TRUE;
  if (g.pipeline_stats) use12.pNext = &executable;
  VkPhysicalDeviceRayQueryFeaturesKHR useRQ{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &use12}; useRQ.rayQuery = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR useAS{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &useRQ};
  useAS.accelerationStructure = VK_TRUE;
  // --ab-rgen: the ray tracing pipeline and SER (EXT) features, only in that mode (ordinary replay is unchanged).
  VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT useSER{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR useRT{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &useSER};
  if (!g_ab_rgen.empty()) {
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT haveSER{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR haveRT{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &haveSER};
    VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &haveRT};
    vkGetPhysicalDeviceFeatures2(g.physical, &query);
    if (!haveRT.rayTracingPipeline || !haveSER.rayTracingInvocationReorder) {
      std::puts("SKIP --ab-rgen: no rayTracingPipeline + rayTracingInvocationReorder (EXT) on this device");
      return 77;
    }
    extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    extensions.push_back(VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
    useRT.rayTracingPipeline = VK_TRUE;
    useSER.rayTracingInvocationReorder = VK_TRUE;
    useSER.pNext = useAS.pNext;
    useAS.pNext = &useRT;
    g.rt_pipeline = true;
  }
  VkPhysicalDeviceFeatures core{}; core.shaderStorageImageExtendedFormats = VK_TRUE; core.samplerAnisotropy = features.features.samplerAnisotropy;
  core.shaderSampledImageArrayDynamicIndexing = features.features.shaderSampledImageArrayDynamicIndexing;
  core.textureCompressionBC = features.features.textureCompressionBC;
  core.shaderInt64 = features.features.shaderInt64; core.shaderInt16 = features.features.shaderInt16;
  float priority = 0.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = g.family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &useAS};
  dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = uint32_t(extensions.size()); dci.ppEnabledExtensionNames = extensions.data(); dci.pEnabledFeatures = &core;
  vkcheck(vkCreateDevice(g.physical, &dci, nullptr, &g.dev), "device");
  vkGetDeviceQueue(g.dev, g.family, 0, &g.queue);
#define PROC(type, field) g.field = reinterpret_cast<PFN_##type>(vkGetDeviceProcAddr(g.dev, #type)); check(g.field != nullptr, #type)
  PROC(vkCreateAccelerationStructureKHR, createAS); PROC(vkDestroyAccelerationStructureKHR, destroyAS);
  PROC(vkGetAccelerationStructureBuildSizesKHR, sizesAS); PROC(vkCmdBuildAccelerationStructuresKHR, buildAS);
  PROC(vkGetAccelerationStructureDeviceAddressKHR, addressAS);
  if (g.rt_pipeline) {
    PROC(vkCreateRayTracingPipelinesKHR, createRT);
    PROC(vkGetRayTracingShaderGroupHandlesKHR, groupHandles);
    PROC(vkCmdTraceRaysKHR, traceRays);
    VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT ser{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_EXT};
    g.rtp.pNext = &ser;
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &g.rtp};
    vkGetPhysicalDeviceProperties2(g.physical, &p2);
    g.rtp.pNext = nullptr;
    std::printf("[rgen] ray tracing pipeline + SER (EXT): handle %u B, base alignment %u, reordering hint %s\n",
                g.rtp.shaderGroupHandleSize, g.rtp.shaderGroupBaseAlignment,
                ser.rayTracingInvocationReorderReorderingHint == VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_EXT ? "REORDER" : "NONE");
  }
  if (g.pipeline_stats) {
    PROC(vkGetPipelineExecutablePropertiesKHR, executableProperties);
    PROC(vkGetPipelineExecutableStatisticsKHR, executableStatistics);
  }
#undef PROC
  VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex = g.family;
  vkcheck(vkCreateCommandPool(g.dev, &cp, nullptr, &g.pool), "command pool");
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = g.pool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
  vkcheck(vkAllocateCommandBuffers(g.dev, &ca, &g.cmd), "command buffer");
  std::printf("GPU=%s implicit_layers=disabled validation=%d window=none\n", g.name.c_str(), g.validation ? 1 : 0);
  return 0;
}
void submit(const std::function<void(VkCommandBuffer)>& record) {
  vkcheck(vkResetCommandBuffer(g.cmd, 0), "reset");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkcheck(vkBeginCommandBuffer(g.cmd, &begin), "begin"); record(g.cmd); vkcheck(vkEndCommandBuffer(g.cmd), "end");
  VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO}; s.commandBufferCount = 1; s.pCommandBuffers = &g.cmd;
  vkcheck(vkQueueSubmit(g.queue, 1, &s, VK_NULL_HANDLE), "submit"); vkcheck(vkQueueWaitIdle(g.queue), "wait");
}
void barrier(VkCommandBuffer c, VkPipelineStageFlags src, VkAccessFlags access, VkPipelineStageFlags dst, VkAccessFlags read) {
  VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask = access; b.dstAccessMask = read;
  vkCmdPipelineBarrier(c, src, dst, 0, 1, &b, 0, nullptr, 0, nullptr);
}
uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (uint32_t i = 0; i < g.memory.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (g.memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
  die("memory type");
}

// ---------------------------------------------------------------- resources (freed at exit)
struct Buf { VkBuffer b{}; VkDeviceMemory m{}; VkDeviceAddress address{}; void* map{}; VkDeviceSize size{}; };
Buf make_buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, bool host) {
  Buf out; out.size = std::max<VkDeviceSize>(bytes, 16);
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = out.size; ci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  vkcheck(vkCreateBuffer(g.dev, &ci, nullptr, &out.b), "buffer");
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(g.dev, out.b, &req);
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkcheck(vkAllocateMemory(g.dev, &ai, nullptr, &out.m), "buffer memory");
  if (!host) g.device_bytes += req.size;
  vkcheck(vkBindBufferMemory(g.dev, out.b, out.m, 0), "bind buffer");
  VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; address.buffer = out.b;
  out.address = vkGetBufferDeviceAddress(g.dev, &address);
  if (host) { vkcheck(vkMapMemory(g.dev, out.m, 0, out.size, 0, &out.map), "map"); std::memset(out.map, 0, size_t(out.size)); }
  return out;
}
// Host staging of one upload step (buffers and images): a multi-GB pack image or buffer goes up in
// pieces of at most this, through one reused staging buffer (tools/frame_replay.md "Size").
constexpr uint64_t kStageBytes = 256ull << 20;
void upload(const Buf& dst, VkDeviceSize offset, const void* data, size_t n) {
  if (!n) return;
  check(offset + n <= dst.size, "upload inside the buffer");
  Buf staging = make_buffer(std::min<uint64_t>(n, kStageBytes), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  for (size_t at = 0; at < n;) {
    const size_t k = size_t(std::min<uint64_t>(n - at, kStageBytes));
    std::memcpy(staging.map, static_cast<const uint8_t*>(data) + at, k);
    submit([&](VkCommandBuffer c) { VkBufferCopy r{0, offset + at, k}; vkCmdCopyBuffer(c, staging.b, dst.b, 1, &r); });
    at += k;
  }
  vkDestroyBuffer(g.dev, staging.b, nullptr); vkFreeMemory(g.dev, staging.m, nullptr);
}
std::vector<uint8_t> download(const Buf& src, VkDeviceSize bytes) {
  Buf staging = make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  submit([&](VkCommandBuffer c) {
    barrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferCopy r{0, 0, bytes}; vkCmdCopyBuffer(c, src.b, staging.b, 1, &r);
    barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  });
  std::vector<uint8_t> out(static_cast<size_t>(bytes));
  std::memcpy(out.data(), staging.map, out.size());
  vkDestroyBuffer(g.dev, staging.b, nullptr); vkFreeMemory(g.dev, staging.m, nullptr);
  return out;
}
struct Img { VkImage image{}; VkDeviceMemory mem{}; VkImageView view{}; uint32_t format = 0, w = 1, h = 1, layers = 1, mips = 1; bool storage = false; };
Img make_image(uint32_t format, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, bool storage, bool array, bool cube = false, const std::array<uint32_t,4>& components = {}) {
  Img im; im.format = format; im.w = w; im.h = h; im.layers = layers; im.mips = mips; im.storage = storage;
  check(w <= g.max_image_2d && h <= g.max_image_2d && layers <= g.max_layers,
        "image " + std::to_string(w) + "x" + std::to_string(h) + "x" + std::to_string(layers) + " over the device limits " +
            std::to_string(g.max_image_2d) + " / " + std::to_string(g.max_layers) + " layers");
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VkFormat(format); ii.extent = {w, h, 1}; ii.mipLevels = mips; ii.arrayLayers = layers;
  if (cube) { check(layers == 6 && w == h, "a cube image has 6 square layers"); ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; }
  ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT);
  vkcheck(vkCreateImage(g.dev, &ii, nullptr, &im.image), "image");
  VkMemoryRequirements req; vkGetImageMemoryRequirements(g.dev, im.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkcheck(vkAllocateMemory(g.dev, &ai, nullptr, &im.mem), "image memory"); g.device_bytes += req.size; vkcheck(vkBindImageMemory(g.dev, im.image, im.mem, 0), "image bind");
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = im.image; vi.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D; vi.format = VkFormat(format);
  vi.components = {VkComponentSwizzle(components[0]),VkComponentSwizzle(components[1]),VkComponentSwizzle(components[2]),VkComponentSwizzle(components[3])};
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, layers};
  vkcheck(vkCreateImageView(g.dev, &vi, nullptr, &im.view), "image view");
  return im;
}
VkImageLayout layout_of(const Img& im) { return im.storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
// Fills every mip and layer (mip-major, packed: the pack's file layout) in pieces of at most
// `stage_max` bytes (rp::transfer_pieces, as the live writer copied them: whole mips, layer groups,
// else block rows), several pieces per submit through one reused host staging buffer. `source`
// writes `bytes` of the packed image at `offset` to `dst`; null = zero. A 2 GB VT cache never needs
// one 2 GB host allocation (nor the file in memory).
using ImageSource = std::function<void(uint64_t offset, uint64_t bytes, uint8_t* dst)>;
void fill_image_pieces(const Img& im, const ImageSource& source, uint64_t stage_max = kStageBytes) {
  const auto pieces = rp::transfer_pieces(im.format, im.w, im.h, im.layers, im.mips, stage_max);
  check(!pieces.empty(), "image pieces (format " + std::to_string(im.format) + ", a block row over the staging size)");
  uint64_t largest = 0, total = 0;
  for (const auto& p : pieces) { largest = std::max(largest, p.bytes); total += (p.bytes + 15) & ~15ull; }
  Buf staging = make_buffer(std::max<uint64_t>(std::max(largest, std::min(total, stage_max)), 16), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  uint8_t* map = static_cast<uint8_t*>(staging.map);
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib.image = im.image;
  ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, im.mips, 0, im.layers};
  for (size_t k = 0; k < pieces.size();) {
    std::vector<VkBufferImageCopy> regions;
    const bool first = k == 0;
    for (uint64_t at = 0; k < pieces.size(); ++k) {
      const auto& p = pieces[k];
      at = (at + 15) & ~15ull;   // a multiple of every texel / block size
      if (!regions.empty() && at + p.bytes > staging.size) break;
      if (source) source(p.file_offset, p.bytes, map + at);
      else std::memset(map + at, 0, size_t(p.bytes));
      VkBufferImageCopy r{}; r.bufferOffset = at; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, p.mip, p.layer, p.layers};
      r.imageOffset = {0, int32_t(p.row), 0}; r.imageExtent = {std::max(im.w >> p.mip, 1u), p.rows, 1};
      regions.push_back(r);
      at += p.bytes;
    }
    const bool last = k == pieces.size();
    submit([&](VkCommandBuffer c) {
      if (first) {
        ib.srcAccessMask = 0; ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
      }
      vkCmdCopyBufferToImage(c, staging.b, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(regions.size()), regions.data());
      if (last) {
        ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib.newLayout = layout_of(im);
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
      }
    });
  }
  vkDestroyBuffer(g.dev, staging.b, nullptr); vkFreeMemory(g.dev, staging.m, nullptr);
}
// From `data` (packed), or zero when `data` is empty.
void fill_image(const Img& im, const std::vector<uint8_t>& data, uint64_t stage_max = kStageBytes) {
  check(data.empty() || data.size() == rp::image_bytes(im.format, im.w, im.h, im.layers, im.mips), "image data size");
  if (data.empty()) { fill_image_pieces(im, nullptr, stage_max); return; }
  fill_image_pieces(im, [&](uint64_t offset, uint64_t bytes, uint8_t* dst) { std::memcpy(dst, data.data() + offset, size_t(bytes)); }, stage_max);
}
// Streamed from a pack file, piece by piece.
void fill_image_file(const Img& im, const fs::path& p, uint64_t stage_max = kStageBytes) {
  std::ifstream f(p,std::ios::binary);
  check(bool(f), "read " + p.string());
  bg3::replay::codec::Reader reader(f);
  check(reader.good(), "image codec: " + reader.error());
  const uint64_t bytes = rp::image_bytes(im.format, im.w, im.h, im.layers, im.mips);
  check(reader.size() == bytes, "image data size: " + p.string());
  fill_image_pieces(im, [&](uint64_t offset,uint64_t n,uint8_t* dst) {
    check(reader.read(offset,size_t(n),dst),"image codec read: " + reader.error());
  },stage_max);
}
// Every mip and layer back, packed (the self-test's round trip).
std::vector<uint8_t> read_image_all(const Img& im, uint32_t base_mip = 0, uint32_t base_layer = 0, uint32_t mips = 0, uint32_t layers = 0) {
  if (!mips) mips=im.mips-base_mip;
  if (!layers) layers=im.layers-base_layer;
  const uint32_t w=std::max(im.w>>base_mip,1u), h=std::max(im.h>>base_mip,1u);
  const uint64_t bytes = rp::image_bytes(im.format,w,h,layers,mips);
  Buf staging = make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  submit([&](VkCommandBuffer c) {
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout = layout_of(im); ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = im.image; ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,base_mip,mips,base_layer,layers};
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    // Same block-row plan as live capture; exercise nonzero descriptor ranges.
    for (const auto& p : rp::transfer_pieces(im.format,w,h,layers,mips,std::max(uint64_t(160),rp::image_bytes(im.format,w,1,1,1)))) {
      VkBufferImageCopy r{}; r.bufferOffset=p.file_offset;
      r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,base_mip+p.mip,base_layer+p.layer,p.layers};
      r.imageOffset={0,int32_t(p.row),0}; r.imageExtent={std::max(w>>p.mip,1u),p.rows,1};
      vkCmdCopyImageToBuffer(c,im.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging.b,1,&r);
    }
    ib.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    std::swap(ib.oldLayout, ib.newLayout);
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  });
  std::vector<uint8_t> out(static_cast<size_t>(bytes));
  std::memcpy(out.data(), staging.map, out.size());
  vkDestroyBuffer(g.dev, staging.b, nullptr); vkFreeMemory(g.dev, staging.m, nullptr);
  return out;
}
std::vector<uint8_t> read_image(const Img& im) {
  const uint64_t bytes = rp::image_bytes(im.format, im.w, im.h, 1, 1);
  Buf staging = make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  submit([&](VkCommandBuffer c) {
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout = ib.newLayout = VK_IMAGE_LAYOUT_GENERAL; ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = im.image; ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {im.w, im.h, 1};
    vkCmdCopyImageToBuffer(c, im.image, VK_IMAGE_LAYOUT_GENERAL, staging.b, 1, &r);
    barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  });
  std::vector<uint8_t> out(static_cast<size_t>(bytes));
  std::memcpy(out.data(), staging.map, out.size());
  vkDestroyBuffer(g.dev, staging.b, nullptr); vkFreeMemory(g.dev, staging.m, nullptr);
  return out;
}
void clear_storage(VkCommandBuffer c, const Img& im) {
  VkClearColorValue zero{};
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(c, im.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
}

// ---------------------------------------------------------------- files
std::vector<uint8_t> read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  check(bool(f), "read " + p.string());
  bg3::replay::codec::Reader reader(f);
  check(reader.good(),"codec: " + reader.error());
  std::vector<uint8_t> out(static_cast<size_t>(reader.size()));
  check(reader.read(0,out.size(),out.data()),"codec read: " + reader.error());
  return out;
}
void write_file(const fs::path& p, const void* data, size_t n) {
  std::ofstream f(p, std::ios::binary);
  f.write(static_cast<const char*>(data), std::streamsize(n));
  check(bool(f), "write " + p.string());
}
template <class T> void write_vec(const fs::path& p, const std::vector<T>& v) { write_file(p, v.data(), v.size() * sizeof(T)); }
float half_to_float(uint16_t h) {
  const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
  float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(float(m | 1024), int(e) - 25);
  return s ? -v : v;
}
uint64_t fnv1a(const std::vector<uint8_t>& d) { uint64_t h = 1469598103934665603ull; for (uint8_t b : d) { h ^= b; h *= 1099511628211ull; } return h; }

// Read a previous replay's raw file; preserve every bit, including signed zero and NaN payloads.
bool reference_equal(const fs::path& file, const std::vector<uint8_t>& raw, size_t component, const std::string& label, bool print = true) {
  const auto reference = read_file(file);
  if (reference.size() != raw.size() || raw.empty() || raw.size() % component != 0) {
    if (print) std::printf("  vs replay %s: bit-equal FAILED (size mismatch)\n", label.c_str());
    return false;
  }
  uint64_t equal = 0, n = raw.size() / component;
  for (size_t j = 0; j < raw.size(); j += component) equal += std::memcmp(raw.data() + j, reference.data() + j, component) == 0;
  if (print) std::printf("  vs replay %s: bit-equal %.4f%% of components (%llu/%llu)\n", label.c_str(), 100.0 * equal / double(n),
                         (unsigned long long)equal, (unsigned long long)n);
  return equal == n;
}

// ---------------------------------------------------------------- shader
std::string source_root() { return BG3RTX_SOURCE_DIR; }
std::vector<uint32_t> compile(const fs::path& hlsl, const std::vector<std::string>& defines, const fs::path& out_spv,
                              const char* profile = "cs_6_6") {
  char sdk[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableA("VULKAN_SDK", sdk, MAX_PATH);
  const fs::path dxc = n && n < MAX_PATH ? fs::path(sdk) / "Bin" / "dxc.exe" : fs::path(BG3RTX_DXC);
  const fs::path shaders = fs::path(source_root()) / "shaders";
  // The layer's flags (layer/shaders.cpp): vulkan1.3, HLSL 2021, cs_6_6, BG3RTX=1, the SHaRC
  // headers when present (external/ of this checkout), the file's "// dxc:" line, -O3.
  std::string cmd = "\"" + dxc.string() + "\" \"" + hlsl.string() + "\" -spirv -fspv-target-env=vulkan1.3 -HV 2021 -T " + std::string(profile) +
                    (std::strncmp(profile, "lib_", 4) ? " -E main" : "") + " -I \"" +
                    shaders.string() + "\" -D BG3RTX=1";
  const fs::path sharc = fs::path(source_root()) / "external" / "sharc-1.8.3" / "include";
  if (fs::exists(sharc / "SharcCommon.h")) cmd += " -I \"" + sharc.string() + "\" -D BG3RTX_SHARC_SDK=1";
  for (const auto& d : defines) cmd += " -D " + d;
  { std::ifstream f(hlsl); std::string first; std::getline(f, first);
    if (first.rfind("// dxc:", 0) == 0) cmd += " " + first.substr(7); }
  cmd += " -O3 -Fo \"" + out_spv.string() + "\"";
  STARTUPINFOA si{sizeof si}; PROCESS_INFORMATION pi{};
  std::vector<char> line(cmd.begin(), cmd.end()); line.push_back(0);
  check(CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0, "start " + dxc.string());
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  check(code == 0, "dxc " + hlsl.string());
  auto bytes = read_file(out_spv);
  check(bytes.size() % 4 == 0 && !bytes.empty(), "SPIR-V size");
  std::vector<uint32_t> code32(bytes.size() / 4); std::memcpy(code32.data(), bytes.data(), bytes.size());
  return code32;
}

// Pipeline creation is shared by replay and the pack-free GI compiler. Statistics describe
// the specialized driver executable, rather than the unspecialized SPIR-V module.
VkPipeline make_pipeline(const std::vector<uint32_t>& spirv, VkPipelineLayout layout) {
  VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = spirv.size() * 4; smi.pCode = spirv.data();
  VkShaderModule module; vkcheck(vkCreateShaderModule(g.dev, &smi, nullptr, &module), "shader module");
  std::vector<VkSpecializationMapEntry> entries;
  std::vector<uint32_t> values;
  for (const auto& [id, value] : g_specs) {
    entries.push_back({id, uint32_t(values.size() * sizeof(uint32_t)), sizeof(uint32_t)}); values.push_back(value);
    std::printf("[pipeline] spec %u=%u\n", id, value);
  }
  VkSpecializationInfo specs{uint32_t(entries.size()), entries.data(), values.size() * sizeof(uint32_t), values.data()};
  VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main"};
  if (!entries.empty()) cpi.stage.pSpecializationInfo = &specs;
  cpi.layout = layout;
  if (g.pipeline_stats) cpi.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
  VkPipeline pipeline; vkcheck(vkCreateComputePipelines(g.dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline), "compute pipeline");
  vkDestroyShaderModule(g.dev, module, nullptr);
  if (!g.pipeline_stats) return pipeline;
  VkPipelineInfoKHR info{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR}; info.pipeline = pipeline;
  uint32_t count = 0;
  vkcheck(g.executableProperties(g.dev, &info, &count, nullptr), "pipeline executable count");
  std::vector<VkPipelineExecutablePropertiesKHR> properties;
  VkResult status;
  do {
    properties.assign(count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
    status = g.executableProperties(g.dev, &info, &count, properties.data());
    if (status == VK_INCOMPLETE) vkcheck(g.executableProperties(g.dev, &info, &count, nullptr), "pipeline executable recount");
  } while (status == VK_INCOMPLETE);
  vkcheck(status, "pipeline executables");
  for (uint32_t i = 0; i < count; ++i) {
    const auto& p = properties[i];
    std::printf("[pipeline-stats] executable %u: %s; %s; stages=0x%x subgroup=%u\n", i, p.name, p.description, p.stages, p.subgroupSize);
    VkPipelineExecutableInfoKHR ei{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR}; ei.pipeline = pipeline; ei.executableIndex = i;
    uint32_t n = 0;
    vkcheck(g.executableStatistics(g.dev, &ei, &n, nullptr), "pipeline statistic count");
    std::vector<VkPipelineExecutableStatisticKHR> stats;
    do {
      stats.assign(n, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
      status = g.executableStatistics(g.dev, &ei, &n, stats.data());
      if (status == VK_INCOMPLETE) vkcheck(g.executableStatistics(g.dev, &ei, &n, nullptr), "pipeline statistic recount");
    } while (status == VK_INCOMPLETE);
    vkcheck(status, "pipeline statistics");
    for (uint32_t j = 0; j < n; ++j) {
      const auto& st = stats[j];
      std::printf("[pipeline-stats] %u %s = ", i, st.name);
      switch (st.format) {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: std::printf("%s", st.value.b32 ? "true" : "false"); break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: std::printf("%lld", (long long)st.value.i64); break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: std::printf("%llu", (unsigned long long)st.value.u64); break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: std::printf("%.17g", st.value.f64); break;
        default: die("unknown pipeline statistic format");
      }
      std::printf("; %s\n", st.description);
    }
  }
  std::fflush(stdout);
  return pipeline;
}

// --ab-rgen: a ray tracing pipeline of one raygen group (ray queries only: no miss, hit or callable records) and its
// shader binding table. Specialization: the common values plus --rgen-spec.
struct RtPipe { VkPipeline pipeline{}; VkStridedDeviceAddressRegionKHR raygen{}; };
RtPipe make_rt_pipeline(const std::vector<uint32_t>& spirv, VkPipelineLayout layout) {
  check(g.rt_pipeline, "--ab-rgen needs the ray tracing pipeline device");
  VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = spirv.size() * 4; smi.pCode = spirv.data();
  VkShaderModule module; vkcheck(vkCreateShaderModule(g.dev, &smi, nullptr, &module), "raygen module");
  std::map<uint32_t, uint32_t> specs = g_specs;
  for (const auto& [id, value] : g_rgen_specs) specs[id] = value;
  std::vector<VkSpecializationMapEntry> entries;
  std::vector<uint32_t> values;
  for (const auto& [id, value] : specs) {
    entries.push_back({id, uint32_t(values.size() * sizeof(uint32_t)), sizeof(uint32_t)}); values.push_back(value);
    std::printf("[rgen] spec %u=%u\n", id, value);
  }
  VkSpecializationInfo si{uint32_t(entries.size()), entries.data(), values.size() * sizeof(uint32_t), values.data()};
  VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_RAYGEN_BIT_KHR, module, "main"};
  if (!entries.empty()) stage.pSpecializationInfo = &si;
  VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
  group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  group.generalShader = 0; group.closestHitShader = group.anyHitShader = group.intersectionShader = VK_SHADER_UNUSED_KHR;
  VkRayTracingPipelineCreateInfoKHR ci{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
  ci.stageCount = 1; ci.pStages = &stage; ci.groupCount = 1; ci.pGroups = &group;
  ci.maxPipelineRayRecursionDepth = 1; ci.layout = layout;
  RtPipe r;
  vkcheck(g.createRT(g.dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &ci, nullptr, &r.pipeline), "ray tracing pipeline");
  vkDestroyShaderModule(g.dev, module, nullptr);
  const uint32_t hs = g.rtp.shaderGroupHandleSize, base = g.rtp.shaderGroupBaseAlignment;
  std::vector<uint8_t> handle(hs);
  vkcheck(g.groupHandles(g.dev, r.pipeline, 0, 1, hs, handle.data()), "raygen handle");
  Buf sbt = make_buffer(VkDeviceSize(base) * 2, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, true);
  const VkDeviceAddress aligned = (sbt.address + base - 1) / base * base;
  std::memcpy(static_cast<uint8_t*>(sbt.map) + (aligned - sbt.address), handle.data(), hs);
  const VkDeviceSize stride = (hs + g.rtp.shaderGroupHandleAlignment - 1) / g.rtp.shaderGroupHandleAlignment * g.rtp.shaderGroupHandleAlignment;
  r.raygen = {aligned, stride, stride};   // raygen: size == stride
  return r;
}

// Pack-free compiler for explicit production contracts; this does not dispatch.
// GI: material 1, emission 2, sun 3. RF: material 1, game 2, sun 3, empty 4,
// emission 5, hit-GI counters 6. LS box: material 1, sun 2, game 3, counters 4.
void compile_production_pipeline(const std::vector<uint32_t>& spirv, const std::string& pass) {
  const bool rf = pass == "reflections", ls = pass == "local-shadows";
  std::vector<VkDescriptorSetLayout> sets(rf ? 7 : ls ? 5 : 4);
  for (uint32_t set = 0; set < sets.size(); ++set) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    auto add = [&](uint32_t binding, VkDescriptorType type, uint32_t count = 1) {
      bindings.push_back({binding, type, count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    };
    auto table = [&](const auto& bindings) {
      // The game contract marks its bindless b45 with count 0; use the replay's
      // 4096-element capacity (no resources are allocated in compile-only mode).
      for (const auto& b : bindings) add(b.binding, b.type, b.count ? b.count : 4096);
    };
    if (set == 0) {
      if (rf) {
        for (const auto& b : bg3::native_hit_rf::push)
          if (bg3::native_hit_rf::push_enabled(b, false)) add(b.binding, b.type, b.count);
      } else if (ls) {
        // core/gi.cpp make_layout(kLsBindings), ls_uses and kExtras (mask 4).
        for (const auto& b : bg3::native_hit_rf::push)
          if (b.binding < 30 && (b.binding <= 7 || b.binding == 12 || b.binding == 16 || b.binding == 18 || b.binding >= 20))
            add(b.binding, b.type, b.count);
        add(40, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); add(41, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        add(42, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); add(43, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        add(44, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); add(45, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(46, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); add(50, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      } else {
        table(bg3::native_hit_gi::bindings);
        if (pass == "origin") add(4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      }
    }
    if (set == 1) {
      for (const auto& slot : rp::kGiHitDirect) if (slot.set == set) {
        const auto type = slot.kind == rp::kSsbo ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER :
            slot.kind == rp::kSampler ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        add(slot.binding, type, slot.count);
      }
      add(10, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR); add(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // OMM contract
      add(19, VK_DESCRIPTOR_TYPE_SAMPLER, 16); // native tileset
    }
    if (rf || ls) {
      if (set == (rf ? 2u : 3u)) table(bg3::native_hit_rf::game);
      if (set == (rf ? 3u : 2u)) table(bg3::native_hit_rf::sun);
      if (rf && set == 5) table(bg3::hit_emission::emissive);
      if (set == (rf ? 6u : 4u)) add(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      if (ls && set == 4) add(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // occluder stats
    } else {
      if (set == 2) table(bg3::hit_emission::emissive);
      if (set == 3) {
        table(bg3::native_hit_gi::sun);
        add(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      }
    }
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = uint32_t(bindings.size()); ci.pBindings = bindings.data();
    vkcheck(vkCreateDescriptorSetLayout(g.dev, &ci, nullptr, &sets[set]), "compile-only production set layout");
  }
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 256};
  VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  ci.setLayoutCount = uint32_t(sets.size()); ci.pSetLayouts = sets.data(); ci.pushConstantRangeCount = 1; ci.pPushConstantRanges = &range;
  VkPipelineLayout layout; vkcheck(vkCreatePipelineLayout(g.dev, &ci, nullptr, &layout), "compile-only production layout");
  VkPipeline pipeline = make_pipeline(spirv, layout);
  vkDestroyPipeline(g.dev, pipeline, nullptr); vkDestroyPipelineLayout(g.dev, layout, nullptr);
  for (auto set : sets) vkDestroyDescriptorSetLayout(g.dev, set, nullptr);
  std::printf("[pipeline] compile-only %s pipeline built; no resources loaded or dispatch submitted\n", pass.c_str());
}

// ---------------------------------------------------------------- statistics (core/gi.cpp wording, 1 frame)
constexpr uint32_t kCounters = 512;   // >= core/gi.cpp kStatsCounters (448)
struct Stats { uint64_t t[kCounters] = {}; };
Stats fold(const std::vector<uint8_t>& raw) {
  Stats s; const auto* c = reinterpret_cast<const uint32_t*>(raw.data());
  for (uint32_t i = 0; i < kCounters; ++i) for (uint32_t b = 0; b < 64; ++b) s.t[i] += c[i * 64 + b];
  return s;
}
std::vector<std::string> gi_hit_direct_lines(const Stats& s) {
  const uint64_t* t = s.t; const unsigned n = 1; std::vector<std::string> out; char line[1024];
  auto u = [](uint64_t v) { return (unsigned long long)v; };
  if (t[8]) {
    double r = double(t[8]), h = double(t[9] ? t[9] : 1);
    std::snprintf(line, sizeof line, "rt stats (%u frames): hybrid GI %.2fM rays/frame, %.1f%% hit; of the hits %.1f%% shaded from the screen, "
                  "%.1f%% on screen but hidden, %.1f%% off screen", n, r / n * 1e-6, 100.0 * t[9] / r, 100.0 * t[10] / h, 100.0 * t[11] / h, 100.0 * t[12] / h);
    out.push_back(line);
  }
  if (t[282]) {
    std::snprintf(line, sizeof line, "GI hit direct (%u frames): candidates %llu qualified %llu direct supported %llu nonzero %llu; added weighted luminance sum %.7f; "
                  "fallback0 ambient occlusion unchanged; supported zero: no positive light term %llu, all occluded %llu; "
                  "non-finite hit terms (fallback0) %llu, bounce over the half-float max (stored 65504) %llu px",
                  n, u(t[282]), u(t[283]), u(t[284]), u(t[285]), double(t[286]) / 8192.0, u(t[134]), u(t[135]), u(t[417]), u(t[418]));
    out.push_back(line);
  }
  const unsigned b = 160, x = 172;
  if (t[b + 6] | t[b + 9] | t[b + 10] | t[b + 11] | t[b + 5] | t[x + 2]) {
    std::snprintf(line, sizeof line, "%s lights (%u frames): light rays traced %llu, lit %llu, occluded %llu (first hit within 1 cm %llu, on the hit's own triangle %llu, "
                  "within 1 m of the light %llu), unknown %llu; rays to lights of radius >= 10 m %llu; "
                  "froxel lists no point/spot %llu; lights evaluated %llu, zero before the cosine (channel/range/cone) %llu, N.L <= 0 %llu; "
                  "projector entries %llu, evaluated %llu, skipped %llu; unsupported: source (frustum/froxel slice/index) %llu, projector %llu, other %llu",
                  "GI hit direct", n, u(t[b]), u(t[b + 1]), u(t[b] - t[b + 1] - t[b + 4]), u(t[b + 2]), u(t[b + 3]), u(t[x]), u(t[b + 4]), u(t[x + 1]),
                  u(t[b + 5]), u(t[b + 6]), u(t[b + 7]), u(t[b + 8]), u(t[x + 2]), u(t[x + 3]), u(t[x + 2] - t[x + 3]), u(t[b + 9]), u(t[b + 10]), u(t[b + 11]));
    out.push_back(line);
  }
  const unsigned p = 312;
  if (t[p] | t[p + 1] | t[p + 2]) {
    std::snprintf(line, sizeof line, "%s policy (%u frames): lights at the game's visibility (unshadowed, as D-LS leaves them) %llu, "
                  "traced with a per-light radius %llu; AO estimates %llu, mean AO %.4f", "GI hit direct", n,
                  u(t[p]), u(t[p + 1]), u(t[p + 2]), t[p + 2] ? double(t[p + 3]) / 8192.0 / double(t[p + 2]) : 0.0);
    out.push_back(line);
  }
  const unsigned gs = 320;   // gi_hit_sun (core/gi.cpp kGiSunFirst, the same wording)
  if (t[gs] | t[gs + 7]) {
    std::snprintf(line, sizeof line, "GI hit sun (%u frames): evaluated %llu, zero sun colour x clouds x b13 %llu, N.L <= 0 %llu, traced lit %llu, traced occluded %llu, "
                  "roof-unsupported (no cut-away roofs in the TLAS) %llu, unknown visibility %llu, other unsupported %llu; added weighted luminance sum %.7f",
                  n, u(t[gs]), u(t[gs + 1]), u(t[gs + 2]), u(t[gs + 3]), u(t[gs + 4]), u(t[gs + 5]), u(t[gs + 6]), u(t[gs + 7]), double(t[gs + 8]) / 8192.0);
    out.push_back(line);
  }
  const unsigned f = 372;    // gi_far (core/gi.cpp kGiFarFirst, the same wording)
  if (t[f]) {
    const double q = 8192.0 * double(t[f]);
    std::snprintf(line, sizeof line, "GI far (%u frames): rays %llu; beyond the base radius: screen hits %llu, qualified sun-lit %llu (outside the froxels %llu), "
                  "kept as native ambient %llu (of them roof rule %llu); per ray: ambient visibility %.5f (old rule %.5f), bounce %.7f (old rule %.7f), ambient kept at qualified hits %.5f",
                  n, u(t[f]), u(t[f + 1]), u(t[f + 2]), u(t[f + 3]), u(t[f + 4]), u(t[f + 10]), 1.0 - double(t[f + 5]) / q, 1.0 - double(t[f + 6]) / q,
                  double(t[f + 7]) / q, double(t[f + 8]) / q, double(t[f + 9]) / q);
    out.push_back(line);
  }
  uint64_t total = 0; for (unsigned i = 253; i < 263; ++i) total += t[i];
  if (total) {
    std::snprintf(line, sizeof line, "hit material (%u frames): attempted %llu qualified %llu unsupported %llu; reasons base/epoch/row/clip/geometry/sampling/fields/frame/parameters "
                  "%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu; of base, instance without a material record %llu",
                  n, u(total), u(t[253]), u(total - t[253]), u(t[254]), u(t[255]), u(t[256]), u(t[257]), u(t[258]), u(t[259]), u(t[260]), u(t[261]), u(t[262]), u(t[127]));
    out.push_back(line);
  }
  std::snprintf(line, sizeof line, "guard (%u frames): light-list reads out of range %llu, set-1 reads out of range %llu, invalid rays %llu, loop caps %llu; unknown GI queries %llu",
                n, u(t[118]), u(t[119]), u(t[120]), u(t[121]), u(t[128 + 5]));
  out.push_back(line);
  return out;
}

// ---------------------------------------------------------------- replay
struct ImageResult { std::string name; uint32_t format, w, h; std::vector<uint8_t> raw; uint64_t hash; double mean[4]; uint64_t nonfinite; };
struct RunResult { std::vector<ImageResult> outputs; Stats stats; double gpu_ms; std::vector<uint8_t> raw_stats; };
struct Replay { std::vector<RunResult> runs; std::vector<std::string> warnings; };

float image_component(const std::vector<uint8_t>& raw, uint32_t format, size_t p, uint32_t k, uint32_t comps) {
      auto ufloat = [](uint32_t x, uint32_t mantissa) {
        const uint32_t e = x >> mantissa, m = x & ((1u << mantissa)-1);
        return e == 31 ? (m ? std::nanf("") : INFINITY) : std::ldexp(float(e ? (1u << mantissa) + m : m), int(e ? e : 1)-15-int(mantissa));
      };
      const uint32_t packed = format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ? reinterpret_cast<const uint32_t*>(raw.data())[p] : 0;
      return format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ? ufloat((packed >> (k == 0 ? 0 : k == 1 ? 11 : 22)) & (k == 2 ? 1023u : 2047u),k == 2 ? 5 : 6) :
                (format == VK_FORMAT_R16G16B16A16_SFLOAT || format == VK_FORMAT_R16_SFLOAT) ? half_to_float(reinterpret_cast<const uint16_t*>(raw.data())[p * comps + k])
                                                       : reinterpret_cast<const float*>(raw.data())[p * comps + k];
}

ImageResult summarize(const std::string& name, uint32_t format, uint32_t w, uint32_t h, std::vector<uint8_t> raw) {
  ImageResult r{name, format, w, h, std::move(raw), 0, {0, 0, 0, 0}, 0};
  r.hash = fnv1a(r.raw);
  const uint64_t px = uint64_t(w) * h;
  uint32_t comps = format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ? 3 : format == VK_FORMAT_R16G16B16A16_SFLOAT || format == VK_FORMAT_R32G32B32A32_SFLOAT ? 4 : 1;
  for (uint64_t i = 0; i < px; ++i)
    for (uint32_t k = 0; k < comps; ++k) {
      float v = image_component(r.raw, format, i, k, comps);
      if (!std::isfinite(v)) { ++r.nonfinite; continue; }
      r.mean[k] += v / double(px);
    }
  return r;
}
void write_pfm(const fs::path& p, const ImageResult& r) {
  if (r.format != VK_FORMAT_R16G16B16A16_SFLOAT) return;
  std::string header = "PF\n" + std::to_string(r.w) + " " + std::to_string(r.h) + "\n-1.0\n";
  std::vector<float> rgb(size_t(r.w) * r.h * 3);
  const auto* h = reinterpret_cast<const uint16_t*>(r.raw.data());
  for (uint32_t y = 0; y < r.h; ++y)
    for (uint32_t x = 0; x < r.w; ++x)
      for (int k = 0; k < 3; ++k) rgb[(size_t(r.h - 1 - y) * r.w + x) * 3 + k] = half_to_float(h[(size_t(y) * r.w + x) * 4 + k]);
  std::ofstream f(p, std::ios::binary); f << header; f.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size() * 4));
}

// Optional per-run control (--pt-reference): patch the pushed constants before run r, receive each
// run's result instead of keeping it (only the last run stays in Replay::runs).
struct ReplayHooks {
  std::function<void(uint32_t run, std::vector<uint8_t>& push)> patch;
  std::function<void(uint32_t run, const RunResult&)> each;
  std::function<std::string(uint32_t runs_done)> note;   // progress detail (the PT: iterations done)
};

// Progress of a many-dispatch replay (--repeat, the PT reference, the native ambient): at most every
// 30 s, "[replay] progress: <done>/<total> dispatches (pct), <rate>/s, elapsed, ETA", flushed so a
// captured console (post_replay.ps1, CTest) shows it while the run lasts. Silent below 30 s.
struct ReplayProgress {
  uint64_t t0 = GetTickCount64(), last = t0;
  bool printed = false;
  void tick(uint32_t done, uint32_t total, const std::string& note) {
    const uint64_t now = GetTickCount64();
    if (now - last < 30000 && !(printed && done == total)) return;
    last = now; printed = true;
    const double el = (now - t0) / 1000.0, rate = el > 0 ? done / el : 0;
    std::printf("[replay] progress: %u/%u dispatches (%.0f%%)%s%s, %.2f/s, elapsed %.0f s, ETA %.0f s\n", done, total, 100.0 * done / std::max(total, 1u),
                note.empty() ? "" : ", ", note.c_str(), rate, el, rate > 0 ? (total - done) / rate : 0.0);
    std::fflush(stdout);
  }
};

// --sun-trust <file> (offline gi_hit_sun / gi_far, tools/frame_replay.md): the D-LS sun set's b8, the trust
// header + roof volumes image (core/sun_trust.h layout, written by tools/pt_roofs.py --trust), which the
// gi_hit_direct contract does not carry. Bound as set 3 b8 (R32_UINT, one texel per 4 bytes) and Push::ls_flags
// bit 25 (a trust slot this frame) is set, as core/gi.cpp does when the slot exists.
fs::path g_sun_trust;
// --no-casters: leave a pack's rt_cascade_casters instance out of the TLAS (mask 0, as packs before the
// "casters" directive), for an A/B against the replay with the cut-away roofs.
bool g_no_casters = false;
bool g_origin = false;
// --push FIELD=VALUE (repeatable): overwrite one Push field of the pack's push before the dispatch, as the live
// core sets it per pass (e.g. radius=6: core/gi.cpp hit_gi_dispatch's gi_far ray length; rt_stats=0 / 1).
// Fields: radius (float, offset 24), rays (uint, offset 32), rt_stats (uint, offset 124), gi_rt_radius (float, offset 112).
std::vector<std::pair<std::string, std::string>> g_push_set;
void apply_push_set(std::vector<uint8_t>& push) {
  for (const auto& [k, v] : g_push_set) {
    const bool f = k == "radius" || k == "gi_rt_radius";
    const size_t off = k == "radius" ? 24 : k == "rays" ? 32 : k == "gi_rt_radius" ? 112 : k == "rt_stats" ? 124 : 0;
    check(off && off + 4 <= push.size(), "--push: unknown field " + k + " (radius, rays, gi_rt_radius, rt_stats)");
    if (f) { const float x = std::stof(v); std::memcpy(push.data() + off, &x, 4); }
    else { const uint32_t x = uint32_t(std::stoul(v)); std::memcpy(push.data() + off, &x, 4); }
  }
}

void origin_manifest(rp::Manifest& m, const std::vector<uint8_t>& push) {
  for (const auto& image : m.images) if (image.set == 2 && image.binding == 3 && image.element == 0) {
    auto material = image; material.set = 0; material.binding = 4;
    m.images.push_back(material); break;
  }
  check(push.size() == 256, "origin push bytes");
  uint32_t full[2]; std::memcpy(full, push.data(), sizeof(full));
  check(full[0] && full[1] && full[0] <= UINT32_MAX - 7 && full[1] <= UINT32_MAX - 7, "origin full resolution");
  for (uint32_t binding : {0u, 1u}) {
    bool found = false;
    for (const auto& buffer : m.buffers) found |= buffer.set == 0 && buffer.binding == binding && !buffer.file.empty();
    check(found, "origin requires captured camera buffers");
  }
  std::string reference;
  for (const auto& image : m.images)
    if (image.set == 0 && image.binding == 40 && image.element == 0) reference = image.file;
  for (uint32_t binding : {2u, 5u}) {
    bool found = false;
    for (const auto& image : m.images)
      found |= image.set == 0 && image.binding == binding && image.element == 0 &&
               image.w == full[0] && image.h == full[1];
    check(found, "origin requires full-resolution depth and normal inputs");
  }
  m.images.erase(std::remove_if(m.images.begin(), m.images.end(), [](const auto& image) {
    return image.set == 0 && image.binding == 40;
  }), m.images.end());
  m.outputs = {{0, 40, rp::kRgba16Sfloat, full[0], full[1], reference}};
  m.groups[0] = (full[0] + 7) / 8; m.groups[1] = (full[1] + 7) / 8; m.groups[2] = 1;
  m.pass = "rt_origin";
}

bool origin_self_test() {
  for (uint32_t w : {1u, 7u, 8u, 9u, 2560u}) for (uint32_t h : {1u, 7u, 8u, 9u, 1067u}) {
    rp::Manifest m; m.pass = "gi_hit_direct";
    m.buffers = {{0,0,448,"cam"}, {0,1,176,"vp"}};
    m.images = {{0,2,0,83,w,h,1,1,"normal"}, {0,5,0,100,w,h,1,1,"depth"},
                {0,40,0,97,w,h,1,1,"origin"}, {2,3,0,37,w,h,1,1,"material"}};
    m.outputs = {{0,8,97,w,h,"old-output"}};
    const auto before = m;
    std::vector<uint8_t> push(256); std::memcpy(push.data(), &w, 4); std::memcpy(push.data()+4, &h, 4);
    origin_manifest(m, push);
    check(m.pass == "rt_origin" && m.groups[0] == (w+7)/8 && m.groups[1] == (h+7)/8 && m.groups[2] == 1, "origin adapter dimensions");
    check(m.outputs.size() == 1 && m.outputs[0].binding == 40 && m.outputs[0].format == 97 &&
          m.outputs[0].w == w && m.outputs[0].h == h && m.outputs[0].reference == "origin", "origin adapter output");
    check(m.images.size() == before.images.size() && before.images[2].binding == 40 && before.pass == "gi_hit_direct", "origin adapter source unchanged");
    bool material = false, depth = false, normal = false;
    for (const auto& i : m.images) {
      check(i.set != 0 || i.binding != 40, "origin input must not alias output");
      material |= i.set == 0 && i.binding == 4 && i.file == "material";
      depth |= i.set == 0 && i.binding == 5 && i.file == "depth";
      normal |= i.set == 0 && i.binding == 2 && i.file == "normal";
    }
    check(material && depth && normal, "origin adapter preserves input files");
  }
  std::puts("PASS origin adapter: 25 edge/resolution cases, output/reference, no input/output alias, preserved inputs");
  return true;
}

bool ls_masks_casters(const rp::Manifest& m) {
  if ((m.pass != "local_shadows" && m.pass != "reflections") ||
      (";" + m.defines + ";").find(";RT_SPEC_CASCADE=1;") == std::string::npos) return false;
  uint32_t value = 0;
  for (const auto& spec : m.specs) if (spec.id == 1000u) value = spec.value;
  if (g_specs.count(1000u)) value = g_specs.at(1000u);
  if (g_ab_specs.count(1000u)) {
    const auto values = g_ab_specs.at(1000u);
    return values.first != 0u && values.second != 0u;
  }
  return value != 0u;
}

Replay replay(const fs::path& pack, const std::vector<uint32_t>* spirv_in, const fs::path& hlsl_override,
              const std::vector<std::string>& extra_defines, const fs::path& out, uint32_t repeat, bool quiet,
              const ReplayHooks* hooks = nullptr) {
  Replay result;
  rp::Manifest m; std::string error;
  { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
  rp::Pass pass = *rp::find_pass(m.pass);
  std::vector<rp::Slot> origin_slots;
  // Reuse the captured GI inputs and AS, but publish the primary-origin image.
  // Keep the pack immutable. Its original origin input is a live reference only:
  // reconstructed BLAS ordering need not reproduce the live AS bit for bit.
  if (g_origin) {
    check(m.pass == "gi_hit_direct", "--origin requires a gi_hit_direct pack");
    origin_slots.assign(pass.slots, pass.slots + pass.slot_count);
    origin_slots.push_back({0, 4, rp::kSampled2D, 1, 0, "gMaterial"});
    origin_slots.push_back({0, 50, rp::kStorage, 1, rp::kRgba32Sfloat, "rtHairOrigin"});
    origin_slots.push_back({1, 19, rp::kSampler, 16, 0, "gNativeTilesetSampler"});
    pass.slots = origin_slots.data(); pass.slot_count = uint32_t(origin_slots.size());
    // Physical/native hair AS replay is unsupported; stock hair uses alpha cards.
    for (uint32_t id : {1008u, 1009u, 1010u, 1020u, 1026u}) {
      check(!g_specs.count(id) || g_specs.at(id) == 0, "origin supports stock hair only");
      check(!g_ab_specs.count(id) || (g_ab_specs.at(id).first == 0 && g_ab_specs.at(id).second == 0), "origin supports stock hair only");
    }
    origin_manifest(m, read_file(pack / m.push));
    pass.shader = "gi/rt_origin.hlsl";
  }
  static std::vector<rp::Slot> trust_slots;
  if (!g_sun_trust.empty()) {
    trust_slots.assign(pass.slots, pass.slots + pass.slot_count);
    if (!g_sun_trust.empty() && !rp::find_slot(pass, 3, 8)) trust_slots.push_back({3, 8, rp::kStorage, 1, VK_FORMAT_R32_UINT, "sunTrust"});
    pass.slots = trust_slots.data(); pass.slot_count = uint32_t(trust_slots.size());
  }
  fs::create_directories(out);
  check(g_reference_out.empty() || fs::weakly_canonical(out) != fs::weakly_canonical(g_reference_out), "--reference-out must differ from --out");
  // Shader: the production file from this checkout (or an override), the pack's variant defines.
  std::vector<std::string> defines = extra_defines;
  for (size_t a = 0; a < m.defines.size();) { size_t e = m.defines.find(';', a); defines.push_back(m.defines.substr(a, e - a)); if (e == std::string::npos) break; a = e + 1; }
  std::vector<uint32_t> spirv = spirv_in ? *spirv_in
      : compile(hlsl_override.empty() ? fs::path(source_root()) / "shaders" / pass.shader : hlsl_override, defines, out / "pass.spv");
  auto file = [&](const std::string& name) { return read_file(pack / name); };

  // rt_cascade_casters (the "casters" directive): the cascade instance's triangles go into the soup (b6)
  // past its captured range and its InstInfo (b7) entry points at them (rp::caster_patch), so the TLAS
  // build below and rt_hit_normal see real triangles. The live entry had none (GI rays never read it).
  std::vector<uint8_t> casters, info_patched;
  uint64_t casters_base = 0;
  // Production LS/RF excludes mask 0x02. Keep its captured soup descriptor range:
  // appending unused casters can cross 2 GiB and make NVIDIA GetDimensions return 0.
  // Respect both pipelines' effective overrides before eliding any geometry.
  const bool masked_casters = ls_masks_casters(m);
  if (!m.casters.file.empty() && masked_casters)
    result.warnings.push_back("casters: excluded by both replay pipelines' 0x01 ray mask; captured soup range preserved");
  if (!m.casters.file.empty() && !g_no_casters && !masked_casters) {
    casters = file(m.casters.file);
    rp::Buffer* soup = nullptr; const rp::Buffer* info_b = nullptr;
    for (auto& b : m.buffers) { if (b.set == 1 && b.binding == 6) soup = &b; if (b.set == 1 && b.binding == 7) info_b = &b; }
    uint64_t end = 0;
    if (rp::casters_valid(m.casters, m.instance_count, casters.size()) && soup && info_b && !info_b->file.empty()) {
      info_patched = file(info_b->file);
      if (info_patched.size() >= size_t(m.instance_count) * 16)
        end = rp::caster_patch(reinterpret_cast<uint32_t*>(info_patched.data()), m.instance_count, m.casters, soup->bytes);
    }
    if (end) {
      casters_base = end - casters.size();
      soup->bytes = end;
      result.warnings.push_back("casters: instance " + std::to_string(m.casters.instance) + ", " + std::to_string(m.casters.tris) +
                                " triangles of stride " + std::to_string(m.casters.stride) + " in the TLAS (mask and flags as live), appended to the soup at " +
                                std::to_string(casters_base));
    } else {
      casters.clear(); info_patched.clear();
      result.warnings.push_back("casters: the pack's directive does not match its InstInfo / file (left out, mask 0)");
    }
  }

  // Buffers: the declared range, filled from the file and the ranges.
  std::map<uint64_t, Buf> buffers;   // key set << 32 | binding
  auto key = [](uint32_t s, uint32_t b) { return (uint64_t(s) << 32) | b; };
  const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
  for (const auto& b : m.buffers) {
    const rp::Slot* slot = rp::find_slot(pass, b.set, b.binding);
    check(slot && (slot->kind == rp::kUbo || slot->kind == rp::kSsbo), "buffer slot " + std::to_string(b.set) + "/" + std::to_string(b.binding));
    Buf buf = make_buffer(b.bytes, slot->kind == rp::kUbo ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : ssbo, false);
    buf.size = b.bytes;   // the descriptor range is the captured one, not the 16-byte minimum
    if (!b.file.empty()) {
      auto d = b.set == 1 && b.binding == 7 && !info_patched.empty() ? info_patched : file(b.file);
      check(d.size() <= b.bytes, "buffer file larger than its range"); upload(buf, 0, d.data(), d.size());
    }
    buffers[key(b.set, b.binding)] = buf;
  }
  if (!casters.empty()) upload(buffers[key(1, 6)], casters_base, casters.data(), casters.size());
  std::map<uint64_t,std::vector<uint8_t>> mutable_counters;
  if (m.pass == "local_shadows") for (const auto& b:m.buffers) if (b.set==4 && !b.file.empty()) {
    auto initial=file(b.file);initial.resize(size_t(b.bytes),0);mutable_counters[key(b.set,b.binding)]=std::move(initial);
  }
  std::map<std::string, std::vector<uint8_t>> range_files;   // the live writer puts many slices in one file
  for (const auto& r : m.ranges) {
    auto it = buffers.find(key(r.set, r.binding)); check(it != buffers.end(), "range before its buffer");
    auto f = range_files.find(r.file);
    if (f == range_files.end()) f = range_files.emplace(r.file, file(r.file)).first;
    const auto& d = f->second;
    const uint64_t n = r.bytes ? r.bytes : d.size();
    check(r.file_offset + n <= d.size() && r.offset + n <= it->second.size, "range outside its file or buffer: " + r.file);
    upload(it->second, r.offset, d.data() + r.file_offset, size_t(n));
  }
  Buf stats = make_buffer(kCounters * 64 * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m.pass != "local_shadows" && m.pass != "reflections");
  // Images (inputs), outputs, dummies.
  std::map<uint64_t, std::map<uint32_t, Img>> images;   // slot -> element -> image
  // The writer stores each VkImage once and names its file from every descriptor element that binds
  // it (the 4 x 4 dummies fill thousands of slots, a VT cache is shared by gAlphaCache and gHitCache):
  // one image per file, view type and shape.
  std::map<std::string, Img> by_file;
  uint32_t decoded = 0, decoded_bc = 0;
  for (const auto& i : m.images) {
    if (std::any_of(m.outputs.begin(),m.outputs.end(),[&](const auto& o){return o.set==i.set && o.binding==i.binding && o.initial==i.file;})) continue;
    const rp::Slot* slot = rp::find_slot(pass, i.set, i.binding);
    check(slot && (slot->kind == rp::kSampled2D || slot->kind == rp::kSampled2DArray || slot->kind == rp::kSampledCube || slot->kind == rp::kStorage) &&
              i.element < slot->count,
          "image slot " + std::to_string(i.set) + "/" + std::to_string(i.binding));
    if (slot->kind == rp::kSampledCube) check(i.layers == 6 && i.w == i.h, std::string("cube image of ") + slot->name + " (6 square layers)");
    if (slot->kind == rp::kStorage) check(i.format == slot->storage_format, std::string("storage format of ") + slot->name);
    const bool array = slot->kind == rp::kSampled2DArray, storage = slot->kind == rp::kStorage, cube = slot->kind == rp::kSampledCube;
    std::string k = i.file + "|" + std::to_string(i.format) + "|" + std::to_string(i.w) + "x" + std::to_string(i.h) + "x" +
                          std::to_string(i.layers) + "x" + std::to_string(i.mips) + (array ? "|a" : "") + (storage ? "|s" : "") + (cube ? "|c" : "");
    for (uint32_t c : i.components) k += "|" + std::to_string(c);
    auto known = by_file.find(k);
    if (known == by_file.end()) {
      Img im = make_image(i.format, i.w, i.h, i.layers, i.mips, storage, array, cube, i.components);
      fill_image_file(im, pack / i.file);   // streamed: a VT cache is ~2 GB
      known = by_file.emplace(k, im).first;
      if (i.source_format) { ++decoded; decoded_bc += rp::block_compressed(i.source_format); }
    }
    images[key(i.set, i.binding)][i.element] = known->second;
  }
  if (decoded)
    result.warnings.push_back(std::to_string(decoded) + " of " + std::to_string(by_file.size()) + " images were read by the writer's shader and stored decoded (" +
                              std::to_string(decoded_bc) + " from BCn; rp::shader_read_format)");
  if (!m.omitted.empty()) result.warnings.push_back(std::to_string(m.omitted.size()) + " image elements omitted by the writer (bound NaN)");
  if (!g_sun_trust.empty()) {
    auto d = read_file(g_sun_trust);
    check(d.size() >= 192 && d.size() % 4 == 0, "--sun-trust file: the 192-byte header + 48-byte volumes (tools/pt_roofs.py --trust)");
    Img im = make_image(VK_FORMAT_R32_UINT, uint32_t(d.size() / 4), 1, 1, 1, true, false);
    fill_image(im, d);
    images[key(3, 8)][0] = im;
    uint32_t n = 0, flags = 0; std::memcpy(&n, d.data(), 4); std::memcpy(&flags, d.data() + 4, 4);
    result.warnings.push_back("sun trust from " + g_sun_trust.filename().string() + ": " + std::to_string(n) + " roof volumes, flags " + std::to_string(flags) +
                              "; Push::ls_flags bit 25 set");
  }
  struct Out { Img img; std::string name, reference; Buf initial; };
  std::vector<Out> outputs;
  for (const auto& o : m.outputs) {
    const rp::Slot* slot = rp::find_slot(pass, o.set, o.binding);
    check(slot && slot->kind == rp::kStorage && slot->storage_format == o.format, "output slot");
    Img im = make_image(o.format, o.w, o.h, 1, 1, true, false);
    fill_image(im, {});
    images[key(o.set, o.binding)][0] = im;
    Buf initial;
    if (!o.initial.empty()) {
      const auto bytes = file(o.initial);
      check(bytes.size() == rp::image_bytes(o.format,o.w,o.h,1,1), "output initial image size");
      initial = make_buffer(bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
      upload(initial,0,bytes.data(),bytes.size());
    }
    check((m.pass != "local_shadows" && m.pass != "reflections") || initial.b, "two-phase output requires exact initial bytes");
    outputs.push_back({im, slot->name, o.reference, initial});
  }
  const float nan = std::nanf("");
  const float nan4[4] = {nan, nan, nan, nan};
  std::vector<uint8_t> nan_bytes(16); std::memcpy(nan_bytes.data(), nan4, 16);
  Img zero2d = make_image(VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 1, 1, false, false); fill_image(zero2d, {});
  Img zeroArr = make_image(VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 1, 1, false, true); fill_image(zeroArr, {});
  Img nan2d = make_image(VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 1, 1, false, false); fill_image(nan2d, nan_bytes);
  Img nanArr = make_image(VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 1, 1, false, true); fill_image(nanArr, nan_bytes);
  std::vector<uint8_t> nan6; for (int f = 0; f < 6; ++f) nan6.insert(nan6.end(), nan_bytes.begin(), nan_bytes.end());
  Img zeroCube = make_image(VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 6, 1, false, false, true); fill_image(zeroCube, {});
  Img nanCube = make_image(VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 6, 1, false, false, true); fill_image(nanCube, nan6);
  std::map<uint32_t, Img> storage_dummy;   // per format
  std::map<uint64_t, std::map<uint32_t, bool>> omitted;
  for (const auto& o : m.omitted) omitted[key(o.set, o.binding)][o.element] = true;
  // Samplers.
  VkSamplerCreateInfo sd{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sd.magFilter = sd.minFilter = VK_FILTER_LINEAR; sd.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sd.addressModeU = sd.addressModeV = sd.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sd.maxLod = VK_LOD_CLAMP_NONE;
  VkSampler default_sampler; vkcheck(vkCreateSampler(g.dev, &sd, nullptr, &default_sampler), "sampler");
  std::map<uint64_t, std::map<uint32_t, VkSampler>> samplers;
  for (const auto& s : m.samplers) {
    VkSamplerCreateInfo si = sd;
    si.magFilter = VkFilter(s.mag); si.minFilter = VkFilter(s.min); si.mipmapMode = VkSamplerMipmapMode(s.mip);
    si.addressModeU = VkSamplerAddressMode(s.au); si.addressModeV = VkSamplerAddressMode(s.av); si.addressModeW = VkSamplerAddressMode(s.aw);
    si.anisotropyEnable = s.aniso > 1 ? VK_TRUE : VK_FALSE; si.maxAnisotropy = std::max(s.aniso, 1.0f); si.maxLod = s.max_lod;
    if (s.state_version) {
      check(s.flags == 0, "sampler extension flags are not supported by replay");
      si.flags = s.flags; si.mipLodBias = s.lod_bias; si.minLod = s.min_lod;
      si.compareEnable = s.compare_enable; si.compareOp = VkCompareOp(s.compare_op);
      si.borderColor = VkBorderColor(s.border); si.unnormalizedCoordinates = s.unnormalized;
      si.anisotropyEnable = s.aniso_enable; si.maxAnisotropy = s.aniso;
    } else {
      result.warnings.push_back("legacy sampler s" + std::to_string(s.set) + "b" + std::to_string(s.binding) +
        "[" + std::to_string(s.element) + "]: bias/minLod/compare/border/coordinate state was not captured; exactness is unproven");
    }
    VkSampler x; vkcheck(vkCreateSampler(g.dev, &si, nullptr, &x), "pack sampler");
    samplers[key(s.set, s.binding)][s.element] = x;
  }

  // ---- TLAS: one BLAS per distinct instance key, its triangles from the instance's InstInfo ----
  std::vector<VkAccelerationStructureInstanceKHR> instances(m.instance_count);
  if (m.instance_count) { auto d = file(m.instances); check(d.size() == instances.size() * sizeof(instances[0]), "instances size"); std::memcpy(instances.data(), d.data(), d.size()); }
  std::vector<uint8_t> info;
  { auto it = buffers.find(key(1, 7)); if (it != buffers.end()) info = download(it->second, it->second.size); }
  struct Blas { VkAccelerationStructureKHR as{}; Buf store; VkDeviceAddress address = 0;
    VkAccelerationStructureGeometryKHR geo{}; VkBuildAccelerationStructureFlagsKHR flags{};
    uint32_t tris = 0, cls = 0; VkDeviceSize scratch_bytes = 0;
  };
  std::map<uint64_t, Blas> blas, blas_b;
  auto instances_b = instances;
  uint32_t class_count[3]{}, class_held = 0;
  uint64_t class_tris[3]{};
  VkQueryPoolCreateInfo bqi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; bqi.queryType = VK_QUERY_TYPE_TIMESTAMP; bqi.queryCount = 2;
  VkQueryPool bqueries; vkcheck(vkCreateQueryPool(g.dev, &bqi, nullptr, &bqueries), "BLAS query pool");
  uint32_t skipped = 0, built = 0;
  Buf scratch;
  const VkDeviceSize align = std::max<VkDeviceSize>(g.as.minAccelerationStructureScratchOffsetAlignment, 1);
  auto build_blas = [&](VkDeviceAddress vertices, uint32_t tris, uint32_t stride, bool opaque, uint32_t cls = 0, bool candidate = false) {
    VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geo.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;   // layer/bvhsplit.cpp tri_geometry
    auto& t = geo.geometry.triangles;
    t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR; t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = vertices; t.vertexStride = stride; t.maxVertex = tris * 3 - 1; t.indexType = VK_INDEX_TYPE_NONE_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (g_blas_policy != "legacy" || g_ab_blas) {
      bi.flags = cls == 0 ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR
        : ((candidate || g_blas_policy == "trace") && !g_blas_control ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
           : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR) | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; bi.geometryCount = 1; bi.pGeometries = &geo;
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    g.sizesAS(g.dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &tris, &sz);
    Blas out; out.store = make_buffer(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    VkAccelerationStructureCreateInfoKHR ac{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ac.buffer = out.store.b; ac.size = sz.accelerationStructureSize; ac.type = bi.type;
    vkcheck(g.createAS(g.dev, &ac, nullptr, &out.as), "BLAS");
    if (scratch.size < sz.buildScratchSize + align) scratch = make_buffer(sz.buildScratchSize + align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    bi.dstAccelerationStructure = out.as; bi.scratchData.deviceAddress = (scratch.address + align - 1) / align * align;
    VkAccelerationStructureBuildRangeInfoKHR range{tris, 0, 0, 0}; const auto* rangePtr = &range;
    submit([&](VkCommandBuffer c) {
      barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_SHADER_READ_BIT);
      g.buildAS(c, 1, &bi, &rangePtr);
    });
    VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR}; ai.accelerationStructure = out.as;
    out.address = g.addressAS(g.dev, &ai);
    out.geo = geo; out.flags = bi.flags; out.tris = tris; out.cls = cls;
    out.scratch_bytes = std::max(sz.buildScratchSize, sz.updateScratchSize);
    return out;
  };
  Buf degenerate = make_buffer(36, ssbo, false);   // one zero-area triangle: the BLAS of an instance left out
  Blas empty = build_blas(degenerate.address, 1, 12, true);
  for (uint32_t i = 0; i < m.instance_count; ++i) {
    auto& in = instances[i];
    const uint64_t k = in.accelerationStructureReference;
    uint32_t base = 0, meta = 0, tris = 0;
    if (info.size() >= (i + 1) * 16ull) { const auto* v = reinterpret_cast<const uint32_t*>(info.data()) + i * 4; base = v[0]; meta = v[1]; tris = v[3]; }
    const uint32_t stride = meta & 255u;
    const bool pool = (meta & rp::kInfoPool) != 0;
    auto src = buffers.find(key(1, pool ? 8 : 6));
    const bool usable = tris && stride >= 12 && !(meta & (rp::kInfoLegacy | rp::kInfoHair)) && src != buffers.end() &&
                        uint64_t(base) + uint64_t(tris) * 3 * stride <= src->second.size;
    if (!usable) {
      if (!(masked_casters && i == m.casters.instance)) ++skipped;
      in.mask = 0; in.accelerationStructureReference = empty.address; instances_b[i] = in; continue;
    }
    auto it = blas.find(k);
    // The cascade instance's BLAS is not opaque, as live (layer/bvhsplit.cpp: the sun's variant tests its candidates).
    const bool opaque = !(meta & rp::kInfoAlpha) && !(!casters.empty() && i == m.casters.instance);
    const bool cascade = !casters.empty() && i == m.casters.instance;
    // Static pool alpha row indices are <= 4096, so cannot overlap kInfoHeld (bit 31).
    const bool held = pool && (meta & (1u << 31));
    const uint32_t cls = blas_class(meta, cascade);
    if (it == blas.end()) {
      it = blas.emplace(k, build_blas(src->second.address + base, tris, stride, opaque, cls)).first; ++built;
      ++class_count[cls]; class_tris[cls] += tris; class_held += held;
      if (g_ab_blas && cls) blas_b.emplace(k, build_blas(src->second.address + base, tris, stride, opaque, cls, true));
    }
    check(it->second.cls == cls, "shared BLAS key has conflicting classes");
    in.accelerationStructureReference = it->second.address;
    instances_b[i] = in;
    if (g_ab_blas && cls) instances_b[i].accelerationStructureReference = blas_b.at(k).address;
  }
  if (skipped) result.warnings.push_back(std::to_string(skipped) + " instances left out of the TLAS (legacy soup, hair AABBs, no triangles, or outside the captured buffers): mask 0");
  Buf instBuf = make_buffer(std::max<size_t>(instances.size(), 1) * sizeof(VkAccelerationStructureInstanceKHR),
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, false);
  upload(instBuf, 0, instances.data(), instances.size() * sizeof(instances[0]));
  VkAccelerationStructureGeometryKHR top{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  top.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  top.geometry.instances = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, nullptr, VK_FALSE, {instBuf.address}};
  VkAccelerationStructureBuildGeometryInfoKHR tb{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  tb.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; tb.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  tb.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; tb.geometryCount = 1; tb.pGeometries = &top;
  const uint32_t count = m.instance_count;
  VkAccelerationStructureBuildSizesInfoKHR ts{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  g.sizesAS(g.dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tb, &count, &ts);
  Buf tlasStore = make_buffer(ts.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
  VkAccelerationStructureCreateInfoKHR tc{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  tc.buffer = tlasStore.b; tc.size = ts.accelerationStructureSize; tc.type = tb.type;
  VkAccelerationStructureKHR tlas; vkcheck(g.createAS(g.dev, &tc, nullptr, &tlas), "TLAS");
  if (scratch.size < ts.buildScratchSize + align) scratch = make_buffer(ts.buildScratchSize + align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
  tb.dstAccelerationStructure = tlas; tb.scratchData.deviceAddress = (scratch.address + align - 1) / align * align;
  { VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0}; const auto* rangePtr = &range;
    submit([&](VkCommandBuffer c) {
      barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
              VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
      g.buildAS(c, 1, &tb, &rangePtr);
    }); }

  VkAccelerationStructureKHR tlas_b = tlas;
  Buf tlasStoreB;
  if (g_ab_blas) {
    tlasStoreB = make_buffer(ts.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    tc.buffer = tlasStoreB.b; vkcheck(g.createAS(g.dev, &tc, nullptr, &tlas_b), "candidate TLAS");
    upload(instBuf, 0, instances_b.data(), instances_b.size() * sizeof(instances_b[0]));
    tb.dstAccelerationStructure = tlas_b;
    VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0}; const auto* rp = &range;
    submit([&](VkCommandBuffer c) {
      barrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
              VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
      g.buildAS(c, 1, &tb, &rp);
    });
    std::printf("[replay] BLAS classes unique/tris: static %u/%llu dynamic %u/%llu cascade %u/%llu; held dynamic %u; layer A vs trace B\n",
      class_count[0], (unsigned long long)class_tris[0], class_count[1], (unsigned long long)class_tris[1],
      class_count[2], (unsigned long long)class_tris[2], class_held);
    std::fflush(stdout);
  }

  // ---- descriptor sets: every slot of the pass contract, pack resources or dummies ----
  std::vector<VkDescriptorSetLayout> setLayouts(pass.set_count);
  std::vector<VkDescriptorPoolSize> poolSizes;
  // --ab-rgen: the same sets and push constants are visible to the raygen stage too (B binds them at the ray tracing
  // bind point); the stage masks of the dispatch's barriers and end timestamp then cover ray tracing shaders.
  const bool rgen = !g_ab_rgen.empty();
  const VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | (rgen ? VK_SHADER_STAGE_RAYGEN_BIT_KHR : 0u);
  const VkPipelineStageFlags kShaderStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (rgen ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR : 0u);
  auto vk_type = [](rp::Kind k) {
    switch (k) {
      case rp::kUbo: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      case rp::kSsbo: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      case rp::kSampled2D: case rp::kSampled2DArray: case rp::kSampledCube: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      case rp::kStorage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      case rp::kSampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
      default: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
  };
  for (uint32_t s = 0; s < pass.set_count; ++s) {
    std::vector<VkDescriptorSetLayoutBinding> list;
    for (uint32_t i = 0; i < pass.slot_count; ++i) if (pass.slots[i].set == s) {
      const rp::Slot& slot = pass.slots[i];
      list.push_back({slot.binding, vk_type(slot.kind), slot.count, kStages, nullptr});
      poolSizes.push_back({vk_type(slot.kind), slot.count});
    }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = uint32_t(list.size()); dl.pBindings = list.data();
    vkcheck(vkCreateDescriptorSetLayout(g.dev, &dl, nullptr, &setLayouts[s]), "set layout");
  }
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets = pass.set_count; dp.poolSizeCount = uint32_t(poolSizes.size()); dp.pPoolSizes = poolSizes.data();
  VkDescriptorPool descriptorPool; vkcheck(vkCreateDescriptorPool(g.dev, &dp, nullptr, &descriptorPool), "descriptor pool");
  std::vector<VkDescriptorSet> sets(pass.set_count);
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = descriptorPool;
  da.descriptorSetCount = pass.set_count; da.pSetLayouts = setLayouts.data();
  vkcheck(vkAllocateDescriptorSets(g.dev, &da, sets.data()), "descriptor sets");
  std::vector<std::string> missing, missing_lighting;   // lighting: sets 2-3 (older packs have none)
  std::vector<Buf> dummies;
  for (uint32_t i = 0; i < pass.slot_count; ++i) {
    const rp::Slot& slot = pass.slots[i];
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = sets[slot.set]; w.dstBinding = slot.binding;
    w.descriptorCount = slot.count; w.descriptorType = vk_type(slot.kind);
    VkDescriptorBufferInfo bi{};
    std::vector<VkDescriptorImageInfo> ii;
    VkWriteDescriptorSetAccelerationStructureKHR aw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    const uint64_t k = key(slot.set, slot.binding);
    if (slot.kind == rp::kUbo || slot.kind == rp::kSsbo) {
      auto it = buffers.find(k);
      if (slot.set == 0 && slot.binding == 20) bi = {stats.b, 0, VK_WHOLE_SIZE};
      else if (it != buffers.end()) bi = {it->second.b, 0, it->second.size};
      else {
        (slot.set < 2 ? missing : missing_lighting).push_back(slot.name);
        dummies.push_back(make_buffer(256, slot.kind == rp::kUbo ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false));
        bi = {dummies.back().b, 0, 256};
      }
      w.pBufferInfo = &bi;
    } else if (slot.kind == rp::kTlas) {
      aw.accelerationStructureCount = 1; aw.pAccelerationStructures = &tlas; w.pNext = &aw;
    } else if (slot.kind == rp::kSampler) {
      auto it = samplers.find(k);
      for (uint32_t e = 0; e < slot.count; ++e) {
        VkSampler s = default_sampler;
        if (it != samplers.end() && it->second.count(e)) s = it->second.at(e);
        ii.push_back({s, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
      }
      w.pImageInfo = ii.data();
    } else {
      auto it = images.find(k);
      auto om = omitted.find(k);
      bool any = it != images.end();
      for (uint32_t e = 0; e < slot.count; ++e) {
        if (any && it->second.count(e)) { const Img& im = it->second.at(e); ii.push_back({VK_NULL_HANDLE, im.view, layout_of(im)}); continue; }
        if (slot.kind == rp::kStorage) {
          auto d = storage_dummy.find(slot.storage_format);
          if (d == storage_dummy.end()) { Img im = make_image(slot.storage_format, 1, 2, 1, 1, true, false); fill_image(im, {}); d = storage_dummy.emplace(slot.storage_format, im).first; }
          ii.push_back({VK_NULL_HANDLE, d->second.view, VK_IMAGE_LAYOUT_GENERAL});
          continue;
        }
        const bool isNan = om != omitted.end() && om->second.count(e);
        const Img& d = slot.kind == rp::kSampledCube ? (isNan ? nanCube : zeroCube) : slot.kind == rp::kSampled2DArray ? (isNan ? nanArr : zeroArr)
                                                                                                   : (isNan ? nan2d : zero2d);
        ii.push_back({VK_NULL_HANDLE, d.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
      }
      if (!any && slot.count == 1) (slot.set < 2 ? missing : missing_lighting).push_back(slot.name);
      w.pImageInfo = ii.data();
    }
    vkUpdateDescriptorSets(g.dev, 1, &w, 0, nullptr);
  }
  if (!missing.empty()) {
    std::string s = "not in the pack (bound zero dummies):";
    for (auto& x : missing) s += " " + x;
    result.warnings.push_back(s);
  }
  if (!missing_lighting.empty()) {
    std::string s = std::to_string(missing_lighting.size()) + " lighting inputs (sets 2-3) not in the pack (bound zero dummies):";
    for (auto& x : missing_lighting) s += " " + x;
    result.warnings.push_back(s);
  }

  // ---- pipeline, push constants, dispatch ----
  const auto push_pack = file(m.push);
  check(push_pack.size() == pass.push_bytes, "push constant bytes");
  std::vector<uint8_t> push = push_pack;
  VkPushConstantRange pushRange{kStages, 0, pass.push_bytes};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = pass.set_count; pl.pSetLayouts = setLayouts.data(); pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pushRange;
  VkPipelineLayout layout; vkcheck(vkCreatePipelineLayout(g.dev, &pl, nullptr, &layout), "pipeline layout");
  const auto user_specs = g_specs;
  for (const auto& spec : m.specs) if (!g_specs.count(spec.id)) g_specs[spec.id] = spec.value;
  const auto common_specs = g_specs;
  for (const auto& [id, values] : g_ab_specs) g_specs[id] = values.first;
  VkPipeline pipeline = make_pipeline(spirv, layout), pipeline_b = pipeline;
  if (ab_mode()) {
    check(!hooks, "paired A/B is not supported with PT/native hooks");
    g_specs = common_specs;
    for (const auto& [id, values] : g_ab_specs) g_specs[id] = values.second;
    auto code_b = spirv;
    if (!g_ab_spv.empty()) {
      auto bytes = read_file(g_ab_spv);
      check(!bytes.empty() && bytes.size() % 4 == 0, "--ab-spv SPIR-V size");
      code_b.resize(bytes.size() / 4); std::memcpy(code_b.data(), bytes.data(), bytes.size());
    }
    if (!rgen) pipeline_b = make_pipeline(code_b, layout);
  }
  RtPipe rt_b;
  if (rgen) {
    check(m.pass != "local_shadows" && m.pass != "reflections", "--ab-rgen: GI packs only (tools/gi_trace_rgen.hlsl)");
    g_specs = common_specs;
    rt_b = make_rt_pipeline(compile(g_ab_rgen, defines, out / "rgen.spv", "lib_6_6"), layout);
    pipeline_b = rt_b.pipeline;
    g_specs = user_specs;
  }
  g_specs = user_specs;
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2;
  VkQueryPool queries; vkcheck(vkCreateQueryPool(g.dev, &qi, nullptr, &queries), "query pool");
  if (!quiet) {   // what the pack costs the GPU (a 2 GB VT cache included; host staging is kStageBytes)
    uint64_t heap = 0;
    for (uint32_t h = 0; h < g.memory.memoryHeapCount; ++h)
      if (g.memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap = std::max<uint64_t>(heap, g.memory.memoryHeaps[h].size);
    uint64_t largest = 0;
    for (const auto& [k, im] : by_file) largest = std::max(largest, rp::image_bytes(im.format, im.w, im.h, im.layers, im.mips));
    std::printf("[replay] loaded %zu images (largest %.1f MB), device-local %.1f MB of a %.1f MB heap, staging pieces of %.0f MB\n", by_file.size(),
                largest / 1048576.0, g.device_bytes / 1048576.0, heap / 1048576.0, kStageBytes / 1048576.0);
  }
  std::vector<uint8_t> initial_stats(size_t(stats.size),0);
  if (!m.counter_initial.empty()) {
    const auto bytes = file(m.counter_initial);
    check(bytes.size() <= initial_stats.size() && bytes.size() % 4 == 0, "initial counter size");
    std::copy(bytes.begin(),bytes.end(),initial_stats.begin());
  }
  // Keep restores on the GPU so the measured dispatch follows sustained work,
  // rather than another CPU readback/upload gap that lets clocks fall to idle.
  Buf warm_stats;
  std::map<uint64_t, Buf> warm_counters;
  if (g_ab_warmup) {
    warm_stats = make_buffer(stats.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
    upload(warm_stats,0,initial_stats.data(),initial_stats.size());
    for (const auto& [binding,initial] : mutable_counters) {
      auto source = make_buffer(initial.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
      upload(source,0,initial.data(),initial.size()); warm_counters[binding] = source;
    }
  }
  ReplayProgress progress;
  // Decode captured references once. A/B retains only its last RunResult, so
  // strict validation must inspect each readback before that result is dropped.
  std::vector<std::vector<uint8_t>> live_output_references;
  std::vector<uint8_t> live_counter_reference;
  bool live_equal = true;
  uint32_t live_checked = 0;
  if (!hooks && g_require_live_equal) {
    live_equal = !outputs.empty() && !m.counters.empty();
    for (const auto& output : outputs) {
      live_output_references.push_back(output.reference.empty() ? std::vector<uint8_t>{} : file(output.reference));
      live_equal &= !live_output_references.back().empty();
    }
    if (!m.counters.empty()) live_counter_reference = file(m.counters);
    live_equal &= !live_counter_reference.empty() && live_counter_reference.size() <= stats.size &&
                  live_counter_reference.size() % 4 == 0;
    if (!live_counter_reference.empty() && live_counter_reference.size() <= stats.size)
      live_counter_reference.resize(size_t(stats.size), 0);
  }
  std::vector<double> timings;
  const uint32_t dispatches = ab_mode() ? 4 + repeat * 4 : repeat;
  std::ofstream ab_csv, numeric_csv;
  std::vector<double> ab_deltas;
  RunResult pair_reference;
  double quartet_ms[4]{};
  bool pair_outputs_equal = true, pair_counters_equal = true;
  bool all_outputs_equal = true, all_counters_equal = true;
  uint32_t first_failed_pair = UINT32_MAX;
  bool explained_outputs = false, explained_counters = false, saved_failed_pair = false;
  if (ab_mode()) {
    if (g_ab_numeric) {
      numeric_csv.open(out / "ab_numeric.csv");
      numeric_csv << "pair,output,channel,pixels,different_pixels,max_abs,mean_diff,mean_abs_diff,nonfinite\n";
      numeric_csv.precision(17);
    }
    ab_csv.open(out / "ab.csv");
    ab_csv << "pair,A_ms,B_ms,delta_ms,outputs_equal,counters_equal\n";
    std::puts("[replay] paired A/B: 4 warmup dispatches excluded; each pair ABBA/BAAB, delta B-A (linear drift cancels)");
    if (g_ab_warmup) std::printf("[replay] per-sample GPU warmup: %u untimed dispatches with complete input restoration, then one timed dispatch\n",g_ab_warmup);
  }
  for (uint32_t run = 0; run < dispatches; ++run) {
    if (run) progress.tick(run, dispatches, hooks && hooks->note ? hooks->note(run) : std::string());
    const bool warmup = ab_mode() && run < 4;
    const uint32_t pair = ab_mode() && !warmup ? (run - 4) / 4 : 0;
    const uint32_t phase = ab_mode() ? run % 4 : 0;
    const bool use_b = ab_mode() && (warmup ? (run & 1) != 0 : ((phase == 1 || phase == 2) != ((pair & 1) != 0)));
    if (g_ab_blas) {
      const auto selected = use_b ? tlas_b : tlas;
      VkWriteDescriptorSetAccelerationStructureKHR aw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
      aw.accelerationStructureCount = 1; aw.pAccelerationStructures = &selected;
      for (uint32_t i = 0; i < pass.slot_count; ++i) if (pass.slots[i].kind == rp::kTlas) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &aw};
        w.dstSet = sets[pass.slots[i].set]; w.dstBinding = pass.slots[i].binding;
        w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(g.dev, 1, &w, 0, nullptr);
      }
    }
    if (stats.map) std::memcpy(stats.map, initial_stats.data(), initial_stats.size());
    else upload(stats,0,initial_stats.data(),initial_stats.size());
    for (const auto& [binding,initial]:mutable_counters) upload(buffers.at(binding),0,initial.data(),initial.size());
    push = push_pack;
    if (hooks && hooks->patch) hooks->patch(run, push);
    apply_push_set(push);
    if (!g_sun_trust.empty()) {   // Push::ls_flags (offset 188, static_assert below) bit 25: a sun trust slot exists
      uint32_t lf; std::memcpy(&lf, push.data() + 188, 4); lf |= 1u << 25; std::memcpy(push.data() + 188, &lf, 4);
    }
    submit([&](VkCommandBuffer c) {
      if (g_ab_warmup)
        barrier(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
      const bool traced = rgen && use_b;   // B of --ab-rgen: vkCmdTraceRaysKHR over the same thread grid
      const VkPipelineBindPoint bind = traced ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR : VK_PIPELINE_BIND_POINT_COMPUTE;
      vkCmdBindPipeline(c, bind, use_b ? pipeline_b : pipeline);
      vkCmdBindDescriptorSets(c, bind, layout, 0, pass.set_count, sets.data(), 0, nullptr);
      vkCmdPushConstants(c, layout, kStages, 0, pass.push_bytes, push.data());
      const VkStridedDeviceAddressRegionKHR none{};
      auto run_pass = [&] {
        if (traced) g.traceRays(c, &rt_b.raygen, &none, &none, &none, m.groups[0] * 8, m.groups[1] * 8, m.groups[2]);
        else vkCmdDispatch(c, m.groups[0], m.groups[1], m.groups[2]);
      };
      for (uint32_t pre = 0; pre <= g_ab_warmup; ++pre) {
        barrier(c, kShaderStage | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        if (g_ab_warmup) {
          VkBufferCopy restore{0,0,stats.size}; vkCmdCopyBuffer(c,warm_stats.b,stats.b,1,&restore);
          for (const auto& [binding,source] : warm_counters) {
            restore.size = buffers.at(binding).size;
            vkCmdCopyBuffer(c,source.b,buffers.at(binding).b,1,&restore);
          }
        }
        for (auto& o : outputs) {
          if (o.initial.b) {
            VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.imageExtent = {o.img.w,o.img.h,1};
            vkCmdCopyBufferToImage(c,o.initial.b,o.img.image,VK_IMAGE_LAYOUT_GENERAL,1,&region);
          } else clear_storage(c, o.img);
        }
        if (pre == g_ab_warmup) vkCmdResetQueryPool(c, queries, 0, 2);
        barrier(c, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kShaderStage,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_UNIFORM_READ_BIT);
        if (pre < g_ab_warmup) {
          run_pass();
          continue;
        }
        // Wait for preceding output clears/uploads; time only this dispatch.
        vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 0);
        run_pass();
        vkCmdWriteTimestamp(c, traced ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries, 1);
      }
      barrier(c, kShaderStage, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    });
    uint64_t ticks[2] = {};
    vkcheck(vkGetQueryPoolResults(g.dev, queries, 0, 2, sizeof ticks, ticks, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "timestamps");
    RunResult rr;
    rr.gpu_ms = double(ticks[1] - ticks[0]) * g.tick_ns * 1e-6;
    if (!warmup) timings.push_back(rr.gpu_ms);
    std::vector<uint8_t> raw(static_cast<size_t>(stats.size));
    if (stats.map) std::memcpy(raw.data(), stats.map, raw.size()); else raw = download(stats,stats.size);
    rr.stats = fold(raw);
    rr.raw_stats = std::move(raw);
    if (m.pass == "local_shadows") for (uint32_t binding : {0u,1u}) {
      const auto it = buffers.find(key(4,binding));
      if (it != buffers.end()) {
        const auto extra = download(it->second,it->second.size);
        rr.raw_stats.insert(rr.raw_stats.end(),extra.begin(),extra.end());
      }
    }
    for (auto& o : outputs) rr.outputs.push_back(summarize(o.name, o.img.format, o.img.w, o.img.h, read_image(o.img)));
    if (!hooks && g_require_live_equal) {
      live_equal &= rr.outputs.size() == live_output_references.size();
      for (size_t i = 0; i < rr.outputs.size() && i < live_output_references.size(); ++i)
        live_equal &= rr.outputs[i].raw == live_output_references[i];
      // LS appends extra diagnostic buffers to raw_stats. The captured primary
      // reference names precisely the original rtStats range (zero-padded above).
      live_equal &= !live_counter_reference.empty() && rr.raw_stats.size() >= live_counter_reference.size() &&
                    std::equal(live_counter_reference.begin(), live_counter_reference.end(), rr.raw_stats.begin());
      ++live_checked;
    }
    if ((!quiet && !ab_mode()) || run == 0) {
      std::printf("[replay] %s run %u: dispatch %u x %u x %u, GPU %.3f ms\n", m.pass.c_str(), run, m.groups[0], m.groups[1], m.groups[2], rr.gpu_ms);
      for (auto& l : gi_hit_direct_lines(rr.stats)) std::printf("  %s\n", l.c_str());
      for (auto& r : rr.outputs)
        std::printf("  output %s %ux%u: mean %.7g %.7g %.7g %.7g, non-finite %llu, fnv1a %016llx\n", r.name.c_str(), r.w, r.h, r.mean[0], r.mean[1],
                    r.mean[2], r.mean[3], (unsigned long long)r.nonfinite, (unsigned long long)r.hash);
    }
    if (ab_mode() && !warmup) {
      // One balanced, adjacent cross-state comparison per quartet: phase 0/1.
      // BAAB reverses operand order so signed output differences remain B-A.
      if (g_ab_numeric && phase == 1) {
        check(rr.outputs.size() == pair_reference.outputs.size(), "numeric output count");
        for (size_t i = 0; i < rr.outputs.size(); ++i) {
          const auto& a = use_b ? pair_reference.outputs[i] : rr.outputs[i];
          const auto& b = use_b ? rr.outputs[i] : pair_reference.outputs[i];
          const unsigned channels = a.format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ? 3 :
            a.format == VK_FORMAT_R16G16B16A16_SFLOAT || a.format == VK_FORMAT_R32G32B32A32_SFLOAT ? 4 : 1;
          check(a.format == b.format && a.w == b.w && a.h == b.h, "numeric image shape");
          const size_t pixel_bytes = size_t(rp::image_bytes(a.format,a.w,a.h,1,1)) / (size_t(a.w)*a.h);
          const auto diff = bg3::replay::numeric_difference(a.raw,b.raw,pixel_bytes,channels,
            [&](const std::vector<uint8_t>& raw, size_t p, unsigned k) { return image_component(raw,a.format,p,k,channels); });
          for (unsigned k = 0; k < channels; ++k)
            numeric_csv << pair + 1 << ',' << a.name << ',' << k << ',' << diff.pixels << ',' << diff.different_pixels << ','
              << diff.max_abs[k] << ',' << diff.mean[k] << ',' << diff.mean_abs[k] << ',' << diff.nonfinite << '\n';
        }
        numeric_csv.flush();
      }
      quartet_ms[phase] = rr.gpu_ms;
      if (phase == 0) {
        pair_reference = rr;
        pair_outputs_equal = pair_counters_equal = true;
      } else {
        pair_counters_equal &= pair_reference.raw_stats == rr.raw_stats;
        pair_outputs_equal &= pair_reference.outputs.size() == rr.outputs.size();
        for (size_t i = 0; i < rr.outputs.size() && i < pair_reference.outputs.size(); ++i)
          pair_outputs_equal &= pair_reference.outputs[i].raw == rr.outputs[i].raw;
        if ((!pair_outputs_equal || !pair_counters_equal) && first_failed_pair == UINT32_MAX) first_failed_pair = pair;
        if (pair == first_failed_pair) {
          if (!saved_failed_pair) {
            saved_failed_pair = true;
            const std::string reference_label = (pair & 1) ? "B" : "A";
            const std::string current_label = use_b ? "B" : "A";
            const std::string suffix = current_label == reference_label ? "_repeat" : "";
            for (const auto& output : pair_reference.outputs) write_vec(out / ("ab_" + reference_label + "_" + output.name + ".raw"), output.raw);
            for (const auto& output : rr.outputs) write_vec(out / ("ab_" + current_label + suffix + "_" + output.name + ".raw"), output.raw);
            if (!pair_counters_equal) {
              write_vec(out / ("ab_" + reference_label + "_rtstats.bin"), pair_reference.raw_stats);
              write_vec(out / ("ab_" + current_label + suffix + "_rtstats.bin"), rr.raw_stats);
            }
            std::printf("[replay] saved first failed pair raw outputs as ab_%s_* and ab_%s%s_*\n", reference_label.c_str(), current_label.c_str(), suffix.c_str());
          }
          if (!pair_outputs_equal && !explained_outputs) {
            explained_outputs = true;
            if (rr.outputs.size() != pair_reference.outputs.size())
              std::printf("[replay] A/B mismatch: output count %zu vs %zu\n", pair_reference.outputs.size(), rr.outputs.size());
            for (size_t i = 0; i < rr.outputs.size() && i < pair_reference.outputs.size(); ++i) {
              const auto& a = pair_reference.outputs[i]; const auto& b = rr.outputs[i];
              const auto diff = bg3::replay::byte_difference(a.raw, b.raw);
              if (!diff.count) continue;
              const size_t component_bytes = (a.format == VK_FORMAT_R16G16B16A16_SFLOAT || a.format == VK_FORMAT_R16_SFLOAT) ? 2 : 4;
              const size_t components = a.format == VK_FORMAT_R16G16B16A16_SFLOAT || a.format == VK_FORMAT_R32G32B32A32_SFLOAT ? 4 : 1;
              size_t different_pixels = 0; double max_diff = 0;
              for (size_t p = 0; p < size_t(a.w) * a.h; ++p) {
                const size_t offset = p * components * component_bytes;
                different_pixels += std::memcmp(a.raw.data()+offset,b.raw.data()+offset,components*component_bytes) != 0;
                for (size_t k = 0; k < components; ++k) {
                  const size_t n = p*components+k;
                  const double av = component_bytes == 2 ? half_to_float(reinterpret_cast<const uint16_t*>(a.raw.data())[n]) : reinterpret_cast<const float*>(a.raw.data())[n];
                  const double bv = component_bytes == 2 ? half_to_float(reinterpret_cast<const uint16_t*>(b.raw.data())[n]) : reinterpret_cast<const float*>(b.raw.data())[n];
                  max_diff = std::max(max_diff,std::abs(av-bv));
                }
              }
              std::printf("[replay] A/B numeric %s: %zu differing pixels, max absolute diff %.12g\n",a.name.c_str(),different_pixels,max_diff);
              const size_t component = diff.first / component_bytes, pixel = component / components;
              std::printf("[replay] A/B mismatch %s pair %u phase %u vs phase 0: %zu differing bytes, first byte %zu component %zu pixel (%zu,%zu) channel %zu; sizes %zu/%zu\n",
                          a.name.c_str(), pair + 1, phase, diff.count, diff.first, component, pixel % a.w, pixel / a.w, component % components, a.raw.size(), b.raw.size());
            }
          }
          if (!pair_counters_equal && !explained_counters) {
            explained_counters = true;
            const auto diff = bg3::replay::byte_difference(pair_reference.raw_stats, rr.raw_stats);
            const size_t index = diff.first / sizeof(uint32_t);
            uint32_t a = 0, b = 0;
            if ((index + 1) * 4 <= pair_reference.raw_stats.size()) std::memcpy(&a, pair_reference.raw_stats.data() + index * 4, 4);
            if ((index + 1) * 4 <= rr.raw_stats.size()) std::memcpy(&b, rr.raw_stats.data() + index * 4, 4);
            std::printf("[replay] A/B counter mismatch pair %u phase %u vs phase 0: %zu differing bytes, first counter %zu bin %zu flat index %zu values %u/%u; sizes %zu/%zu\n",
                        pair + 1, phase, diff.count, index / 64, index % 64, index, a, b, pair_reference.raw_stats.size(), rr.raw_stats.size());
          }
        }
      }
      if (phase == 3) {
        const double outside = (quartet_ms[0] + quartet_ms[3]) * 0.5;
        const double inside = (quartet_ms[1] + quartet_ms[2]) * 0.5;
        const double a = (pair & 1) ? inside : outside, b = (pair & 1) ? outside : inside;
        ab_deltas.push_back(b - a);
        all_outputs_equal &= pair_outputs_equal; all_counters_equal &= pair_counters_equal;
        std::printf("[replay] pair %u/%u %s: A %.6f ms B %.6f ms delta %+.6f ms; outputs %s counters %s\n",
                    pair + 1, repeat, (pair & 1) ? "BAAB" : "ABBA", a, b, b - a,
                    pair_outputs_equal ? "bit-equal" : "DIFFER", pair_counters_equal ? "bit-equal" : "DIFFER");
        ab_csv.precision(12);
        ab_csv << pair + 1 << ',' << a << ',' << b << ',' << b - a << ',' << pair_outputs_equal << ',' << pair_counters_equal << '\n';
        ab_csv.flush(); std::fflush(stdout);
      }
    }
    if (hooks && hooks->each) hooks->each(run, rr);
    if ((hooks || ab_mode()) && run + 1 < dispatches) continue;   // many runs: keep only the last (outputs below)
    result.runs.push_back(std::move(rr));
  }
  if (progress.printed) progress.tick(dispatches, dispatches, hooks && hooks->note ? hooks->note(dispatches) : std::string());
  if (g_ab_blas) {
    std::ofstream csv(out / "blas.csv"); csv << "class,mode,pair,A_ms,B_ms,delta_ms\n";
    std::ofstream summary(out / "blas_summary.txt");
    for (uint32_t cls : {1u, 2u}) {
      if (!class_count[cls]) continue;
      struct Batch {
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ptrs;
        Buf scratch;
      } batches[2];
      for (uint32_t side = 0; side < 2; ++side) {
        auto& batch = batches[side]; auto& source = side ? blas_b : blas;
        VkDeviceSize bytes = 0;
        for (auto& [k,b] : source) if (b.cls == cls) bytes += (b.scratch_bytes + align - 1) / align * align;
        batch.scratch = make_buffer(bytes + align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
        VkDeviceAddress addr = (batch.scratch.address + align - 1) / align * align;
        for (auto& [k,b] : source) if (b.cls == cls) {
          VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
          bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR; bi.flags = b.flags;
          bi.dstAccelerationStructure = b.as; bi.geometryCount = 1; bi.pGeometries = &b.geo;
          bi.scratchData.deviceAddress = addr; addr += (b.scratch_bytes + align - 1) / align * align;
          batch.builds.push_back(bi); batch.ranges.push_back({b.tris,0,0,0});
        }
        for (auto& range : batch.ranges) batch.ptrs.push_back(&range);
      }
      for (uint32_t update : {0u, 1u}) {
        for (auto& batch : batches) for (auto& bi : batch.builds) {
          bi.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
          bi.srcAccelerationStructure = update ? bi.dstAccelerationStructure : VK_NULL_HANDLE;
        }
        std::vector<double> deltas, avals, bvals;
        double times[4]{};
        for (uint32_t run = 0; run < 4 + repeat * 4; ++run) {
          const uint32_t pair = run < 4 ? 0 : (run - 4) / 4, phase = run % 4;
          const bool side = run < 4 ? run % 2 : ((phase == 1 || phase == 2) != ((pair & 1) != 0));
          auto& batch = batches[side];
          submit([&](VkCommandBuffer c) {
            barrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
              VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
            vkCmdResetQueryPool(c,bqueries,0,2);
            vkCmdWriteTimestamp(c,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,bqueries,0);
            g.buildAS(c,uint32_t(batch.builds.size()),batch.builds.data(),batch.ptrs.data());
            vkCmdWriteTimestamp(c,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,bqueries,1);
          });
          uint64_t ticks[2]{};
          vkcheck(vkGetQueryPoolResults(g.dev,bqueries,0,2,sizeof ticks,ticks,8,VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),"BLAS timestamps");
          times[phase] = double(ticks[1]-ticks[0])*g.tick_ns*1e-6;
          if (run >= 4 && phase == 3) {
            const double outside = (times[0]+times[3])*.5, inside = (times[1]+times[2])*.5;
            const double a = pair & 1 ? inside : outside, b = pair & 1 ? outside : inside;
            avals.push_back(a); bvals.push_back(b); deltas.push_back(b-a);
            csv.precision(12); csv << cls << ',' << update << ',' << pair+1 << ',' << a << ',' << b << ',' << b-a << '\n'; csv.flush();
            if (pair % 10 == 9) { std::printf("[replay] BLAS %u %s %u/%u pairs\n",cls,update?"refit":"build",pair+1,repeat); std::fflush(stdout); }
          }
        }
        const auto d = rp::ab_summary(deltas), a = rp::ab_summary(avals), b = rp::ab_summary(bvals);
        char line[512]; std::snprintf(line,sizeof line,"class %u %s A %.6f B %.6f delta %+.6f CI [%+.6f, %+.6f] ms",cls,update?"refit":"build",a.median,b.median,d.median,d.low,d.high);
        std::printf("[replay] %s\n",line); summary << line << '\n'; summary.flush();
      }
    }
  }
  if (ab_mode()) {
    const auto summary = bg3::replay::ab_summary(ab_deltas);
    char line[512];
    std::snprintf(line, sizeof line, "paired delta B-A: median %+.6f ms, 95%% median CI [%+.6f, %+.6f] ms (%zu ABBA/BAAB pairs, exact binomial order statistics); outputs %s; counters %s",
                  summary.median, summary.low, summary.high, ab_deltas.size(),
                  all_outputs_equal ? "100% bit-equal" : "DIFFER", all_counters_equal ? "100% bit-equal" : "DIFFER");
    std::printf("[replay] %s\n", line);
    std::ofstream(out / "ab_summary.txt") << line << '\n';
    if (g_ab_require_equal && !(all_outputs_equal && all_counters_equal)) g_reference_mismatch = true;
  }
  std::sort(timings.begin(), timings.end());
  const size_t mid = timings.size() / 2;
  const double median_ms = timings.size() % 2 ? timings[mid] : (timings[mid - 1] + timings[mid]) * 0.5;
  std::printf("[replay] GPU repeats %zu: median %.6f ms, min %.6f ms, max %.6f ms, spread %.6f ms (all dispatches)\n",
              timings.size(), median_ms, timings.front(), timings.back(), timings.back() - timings.front());
  // Outputs (the last run), and the live reference comparison when the pack has one.
  for (size_t i = 0; i < outputs.size() && !hooks; ++i) {
    const auto& r = result.runs.back().outputs[i];
    write_vec(out / (r.name + ".raw"), r.raw);
    write_pfm(out / (r.name + ".pfm"), r);
    if (!g_reference_out.empty()) {
      const size_t component = (r.format == VK_FORMAT_R16G16B16A16_SFLOAT || r.format == VK_FORMAT_R16_SFLOAT) ? 2 : 4;
      g_reference_mismatch |= !reference_equal(g_reference_out / (r.name + ".raw"), r.raw, component, r.name);
    }
    if (!outputs[i].reference.empty() && (m.pass == "local_shadows" || m.pass == "reflections")) {
      const auto live = file(outputs[i].reference);
      const auto diff = rp::byte_difference(live,r.raw);
      std::printf("  vs live %s: %zu differing bytes of %zu (exact packed format %u)\n",r.name.c_str(),diff.count,r.raw.size(),r.format);
      if (diff.count) result.warnings.push_back("live output differs: " + r.name);
    }
    if (!hooks && !outputs[i].reference.empty() && r.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
      auto live = file(outputs[i].reference);
      if (live.size() != r.raw.size()) { result.warnings.push_back("reference " + outputs[i].reference + " has another size"); continue; }
      const auto* a = reinterpret_cast<const uint16_t*>(r.raw.data()); const auto* b = reinterpret_cast<const uint16_t*>(live.data());
      double sum = 0, worst = 0; uint64_t equal = 0, n = live.size() / 2;
      for (uint64_t j = 0; j < n; ++j) { const double d = std::abs(double(half_to_float(a[j])) - half_to_float(b[j])); if (std::isfinite(d)) { sum += d; worst = std::max(worst, d); } equal += a[j] == b[j]; }
      std::printf("  vs live %s: bit-equal %.4f%% of components, mean |diff| %.7g, max %.7g\n", r.name.c_str(), 100.0 * equal / double(n), sum / double(n), worst);
    }
  }
  { std::ofstream f(out / "stats.txt"); for (auto& l : gi_hit_direct_lines(result.runs.back().stats)) f << l << "\n";
    f << "gpu_ms " << result.runs.back().gpu_ms << "\nblas " << built << " skipped_instances " << skipped << "\n"; }
  // The raw counters (kCounters x 64 bins of uint32): per-slot diagnostics such as HM_BLOCKERS
  // (native_hit_material.hlsli, tools/hit_material_blockers.py) read their slots from it.
  if (!hooks) write_vec(out / "rtstats.bin", result.runs.back().raw_stats);
  if (!hooks && !m.counters.empty()) {
    auto live = file(m.counters);
    check(live.size() <= stats.size && live.size() % 4 == 0, "live counter size");
    live.resize(size_t(stats.size),0);
    const auto& raw = result.runs.back().raw_stats;
    const bool same = std::equal(live.begin(),live.end(),raw.begin());
    std::printf("  vs live counters: %s\n",same ? "bit-equal" : "DIFFER");
    if (!same) result.warnings.push_back("live counters differ");
  }
  if (!hooks && g_require_live_equal) {
    live_equal &= live_checked == dispatches;
    std::printf("[replay] required live outputs/counters: %s (%u/%u readback dispatches; complete nonempty references required)\n",
                live_equal ? "100% bit-equal" : "FAIL", live_checked, dispatches);
    g_reference_mismatch |= !live_equal;
  }
  if (!hooks && !g_reference_out.empty()) {
    g_reference_mismatch |= !reference_equal(g_reference_out / "rtstats.bin", result.runs.back().raw_stats, 4, "counters");
  }
  if (!hooks && m.pass == "water_compose_fixture") {
    // Exact integer side effects of the real production compose entry, read only
    // after dispatch completion. This opt-in fixture leaves ordinary replay intact.
    for (const auto& binding : {std::pair<uint32_t,const char*>{61,"waterStats.bin"},
                               {64,"waterCamera.bin"}, {66,"waterRecords.bin"}}) {
      auto it = buffers.find(key(0, binding.first));
      check(it != buffers.end(), "water fixture buffer missing");
      write_vec(out / binding.second, download(it->second, it->second.size));
    }
  }
  for (auto& w : result.warnings) std::printf("  note: %s\n", w.c_str());
  return result;
}

// ---------------------------------------------------------------- synthetic pack
// One frame of gi_hit_direct over a closed-form scene (lighting space = camera-relative, the camera
// at the origin looking along +y; ray(uv) = (2A u - A, 1, 2A v - A), so a point's depth is its y):
//   B  the back plane y = 5 facing the camera (no material record);
//   F  a slab's front face y = 4.3, |x|, |z| <= 0.6, facing the camera: what the depth buffer shows;
//   K  the slab's back face y = 4.5, facing B, with an opaque hit-material row (record 8192):
//      albedo (0.8, 0.6, 0.4), roughness 0.5, metalness 0, reflectance 0.5, through the real
//      8995 sampling recipe (an all-zero page table and 1x1 tile caches);
//   one point light at (0, 4.75, 0), r 3, colour 2 (or 0: --dark), in the one froxel.
// GI rays from B near the slab hit K: on screen, but the depth buffer there is F 20 cm nearer, so
// hidden: rt_hit_material qualifies and native_hit_direct shades it (the term under test).
struct V3 { float x, y, z; };
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
struct Light { float pos[3], inv_r, col[3], cos_outer, dir[3], cos_inner, m3, m4, m5; uint32_t chan, flags; float strength; uint32_t m9[2]; };
static_assert(sizeof(Light) == 80, "bg3_lights.hlsli load_light record");

// gi_common.hlsli Push (core/gi.cpp's mirror; 256 bytes, the offsets spirv-reflect reports).
struct Push {
  uint32_t full[2], half[2];
  uint32_t frame;
  float strength, radius, thickness;
  uint32_t rays, steps, debug;
  float history, ao;
  float cs_length, cs_strength;
  uint32_t cs_steps;
  float mv_scale[2];
  uint32_t has_mv, bounce_spec;
  float rt_bias, rt_tmax;
  uint32_t rt_debug;
  float rt_strength, rt_ao_radius;
  uint32_t rt_ao_rays, ls_kmode, sharc_fx;
  float gi_rt_radius, gi_rt_tol;
  uint32_t gi_rt_fallback, rt_stats;
  float sun_radius;
  uint32_t res_shift, qa_zero, rt_max_cand;
  float ls_source_radius, ls_clear, ls_min_radius;
  uint32_t ls_samples, ls_shadowed, ls_debug, ls_stats, ls_pass, ls_soft;
  float ls_strength;
  uint32_t ls_has_mask, ls_flags;
  float rf_max_rough, rf_mirror_rough, rf_tmax, rf_depth_tol, rf_edge, rf_strength;
  uint32_t rf_debug, rf_sample, rf_so, rf_validate, rf_models, rf_stats;
  uint32_t rt_backlit, mat_classes, ao_direct, rt_origin;
};
static_assert(sizeof(Push) == 256 && offsetof(Push, rt_tmax) == 84 && offsetof(Push, rt_ao_rays) == 100 &&
              offsetof(Push, gi_rt_radius) == 112 && offsetof(Push, sun_radius) == 128 && offsetof(Push, ls_source_radius) == 144 &&
              offsetof(Push, ls_flags) == 188, "Push must match gi_common.hlsli");
static_assert(offsetof(Push, radius) == 24 && offsetof(Push, rays) == 32 && offsetof(Push, rt_stats) == 124, "--push offsets (apply_push_set)");

// box (the PT furnace, tools/pt_reference.hlsl): the camera inside a closed cube |x|, |y|, |z| <= 2 of
// 12 inward triangles without a material record (instance 0 only); every pixel sees the face y = 2.
// lighting (the native ambient checks, tools/native_ambient.hlsl): also the lighting inputs of sets
// 2-3 in closed form (kSynthAmbient): sky irradiance cube (0.25, 0.5, 1), specular cube 2, DFG LUT
// (1, 0, 1), AO 0.5, albedo (0.8, 0.6, 0.4), roughness 0.5, one sphere probe of radius 100 around
// the camera with a diffuse cube 3 (weight 1) listed in the froxel, m27 = m28 = 1, m29 = 0; the
// atmosphere sun path (colour 1, intensity 1) through a transmittance LUT (0.7, 0.6, 0.5).
struct SynthAmbient { float irr[3] = {0.25f, 0.5f, 1.0f}, spec = 2, probe = 3, ao = 0.5f, albedo[3] = {0.8f, 0.6f, 0.4f}, lut[3] = {0.7f, 0.6f, 0.5f}; };
constexpr SynthAmbient kSynthAmbient{};
// casters (with box; pt_reference_test 9-11): the face y = -L is not in instance 0 but in an rt_cascade_casters
// instance 1 as the live writer packs it (InstInfo {0, stride, 0, 0}, mask 0x02, instanceCustomIndex 0xFFFFFF, not
// opaque, the "casters" directive), stored 1 m higher and moved down by the instance's translation; the
// variant defines say RT_CASCADE_MASKS=1 (GI rays trace 0x01).
void synthesize(const fs::path& dir, bool dark, bool box = false, bool lighting = false, bool casters = false) {
  fs::create_directories(dir);
  const uint32_t W = g_synth_size, H = g_synth_size;   // --synth-size (default 128, the CTest's)
  constexpr uint32_t frame = 3, stamp = 7;
  constexpr float kFront = 4.3f;   // F: 20 cm in front of K, far beyond the on-screen depth tolerance (~2 cm footprint)
  constexpr float A = 0.5f;
  constexpr uint32_t stride = 64, record = bg3::native_hit::nh_alpha_rows;
  // Soup: 64-byte vertices (position float4 at 0, UV at 16), non-indexed. A quad in the plane y
  // over [x0,x1] x [z0,z1], wound so that cross(p1 - p0, p2 - p0) points along `ny`.
  std::vector<uint8_t> soup, basis, caster_soup;
  auto vertex = [&](std::vector<uint8_t>& out, V3 p, float u, float v, V3 b0, V3 b1, V3 b2) {
    float rec[16] = {p.x, p.y, p.z, 1, u, v, b0.x, b0.y, b0.z, b1.x, b1.y, b1.z, b2.x, b2.y, b2.z, 0};
    const auto* bytes = reinterpret_cast<const uint8_t*>(rec); out.insert(out.end(), bytes, bytes + sizeof rec);
  };
  auto quad = [&](float y, float x0, float x1, float z0, float z1, float ny, bool with_basis) {
    V3 c[4] = {{x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}};
    float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    int order[6] = {0, 1, 2, 0, 2, 3};
    if (dot3(cross(sub(c[1], c[0]), sub(c[2], c[0])), {0, ny, 0}) < 0) { order[1] = 2; order[2] = 1; order[4] = 3; order[5] = 2; }
    // Basis per vertex: component 0 / 1 / 2 (native_hit_material.hlsli): the frame normal (0, 0, 1)
    // the normal layer encodes picks component 1, the surface normal.
    const V3 b0{1, 0, 0}, b1{0, ny, 0}, b2{0, 0, 1};
    for (int k : order) { vertex(soup, c[k], uv[k][0], uv[k][1], b0, b1, b2); if (with_basis) vertex(basis, c[k], uv[k][0], uv[k][1], b0, b1, b2); }
  };
  constexpr float kBox = 2.0f;
  if (box) {
    // Six inward faces: corners c0..c3, wound so the geometric normal points to the centre.
    auto face = [&](V3 a, V3 b, V3 c, V3 d, V3 n, std::vector<uint8_t>* to = nullptr) {
      V3 q[4] = {a, b, c, d};
      int order[6] = {0, 1, 2, 0, 2, 3};
      if (dot3(cross(sub(q[1], q[0]), sub(q[2], q[0])), n) < 0) { order[1] = 2; order[2] = 1; order[4] = 3; order[5] = 2; }
      for (int k : order) vertex(to ? *to : soup, q[k], 0, 0, {1, 0, 0}, n, {0, 0, 1});
    };
    const float L = kBox;
    face({-L, L, -L}, {L, L, -L}, {L, L, L}, {-L, L, L}, {0, -1, 0});      // y = +L (the screen)
    if (casters) face({-L, 1 - L, -L}, {L, 1 - L, -L}, {L, 1 - L, L}, {-L, 1 - L, L}, {0, 1, 0}, &caster_soup);   // y = -L, 1 m up
    else face({-L, -L, -L}, {L, -L, -L}, {L, -L, L}, {-L, -L, L}, {0, 1, 0});   // y = -L
    face({L, -L, -L}, {L, L, -L}, {L, L, L}, {L, -L, L}, {-1, 0, 0});      // x = +L
    face({-L, -L, -L}, {-L, L, -L}, {-L, L, L}, {-L, -L, L}, {1, 0, 0});   // x = -L
    face({-L, -L, L}, {L, -L, L}, {L, L, L}, {-L, L, L}, {0, 0, -1});      // z = +L
    face({-L, -L, -L}, {L, -L, -L}, {L, L, -L}, {-L, L, -L}, {0, 0, 1});   // z = -L
    const std::vector<uint8_t> keep = soup;
    quad(4.5f, -0.6f, 0.6f, -0.6f, 0.6f, +1, true);   // only for the (unused) basis buffer
    soup = keep;
  } else {
    quad(5.0f, -10, 10, -10, 10, -1, false);        // B (instance 0, tris 0-1)
    quad(kFront, -0.6f, 0.6f, -0.6f, 0.6f, -1, false);  // F (instance 0, tris 2-3)
  }
  const uint32_t kBase = uint32_t(soup.size());
  if (!box) quad(4.5f, -0.6f, 0.6f, -0.6f, 0.6f, +1, true);   // K (instance 1, tris 0-1, with material)
  uint32_t info[8] = {0, stride, 0, box ? 12u : 4u, kBase, stride | bg3::native_hit::nh_opaque_meta, record, 2};
  if (casters) { info[3] = 10; info[4] = 0; info[5] = stride; info[6] = 0; info[7] = 0; }   // the live cascade entry: no vertices
  const uint32_t instances_used = box && !casters ? 1 : 2;
  // Hit-material records (native_hit_material.hlsli rt_hit_material, opaque prepass row).
  const uint32_t recordsBytes = 16 + bg3::native_hit::nh_material_rows * 256 + 16;
  std::vector<uint8_t> records(recordsBytes);
  auto put = [&](uint32_t at, const void* v, size_t n) { std::memcpy(records.data() + at, v, n); };
  { const uint32_t header[4] = {frame, stamp, 0, 1}; put(0, header, 16); }
  const uint32_t row = 16 + record * 256;
  { const uint32_t h[12] = {record + 1, 2, 0, 0, /*h1*/ stride, 1, 0, frame, /*h2: normal, albedo, params caches*/ 2, 1, 3, 0}; put(row, h, sizeof h); }
  { const float p0[4] = {0, 0, 1, 1}, p1[4] = {0, 0, 0, 0}; put(row + 48, p0, 16); put(row + 64, p1, 16); }
  float mt[32] = {};   // matrix m, column c, row r at m * 16 + r * 4 + c (hm_t)
  auto set = [&](uint32_t mtx, uint32_t col, uint32_t r, float v) { mt[mtx * 16 + r * 4 + col] = v; };
  set(0, 2, 3, 2202);                  // the recipe tag
  set(0, 0, 3, 1); set(0, 1, 3, 1);    // page-table dimensions (1 x 1)
  set(0, 2, 0, 1); set(0, 3, 0, 1);    // UV scale of the mip selection
  set(0, 0, 2, 1); set(0, 1, 2, 1);    // tile scale (s168)
  set(0, 0, 1, 1);                     // the cache derivative scale
  for (uint32_t col = 0; col < 3; ++col) { set(1, 0, col, 1); set(1, 1, col, 1); }   // per-layer scale, offset 0
  put(row + 80, mt, sizeof mt);
  { const float reflectance = 0.5f; const uint32_t channel = 0; put(row + 208, &reflectance, 4); put(row + 212, &channel, 4); }
  { const uint32_t ready = 1; put(16 + bg3::native_hit::nh_material_rows * 256, &ready, 4); }
  // Scene stamp: generation 7 expected, ran, and the build's (rt_scene_ok).
  std::vector<uint32_t> stampBuf(128); stampBuf[0] = stampBuf[1] = stampBuf[2] = stamp;
  std::vector<uint32_t> alphaRec(4); alphaRec[1] = stride;
  // Camera (gi_common.hlsli CameraCB, 400 B): view normal -> lighting rows; frustum rays.
  std::vector<float> cam(100);
  const float m6[12] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0};   // (0, 0, -1) view -> (0, -1, 0)
  std::memcpy(cam.data() + 12, m6, sizeof m6);
  const float rays[12] = {-A, 1, -A, 0, A, 1, -A, 0, -A, 1, A, 0}; std::memcpy(cam.data() + 28, rays, sizeof rays);
  const float vp[4] = {0.5f, 0.5f, 0, 0};
  // One froxel listing the light (as tools/native_hit_gpu_fixture.cpp): tile 1, 1 x 1 tiles, slice z 0.
  std::vector<float> lconsts(136); lconsts[30 * 4 + 2] = 1; lconsts[30 * 4 + 3] = 1; { const uint32_t one = 1; std::memcpy(&lconsts[31 * 4], &one, 4); }
  if (lighting) {   // m27, m28 1; m29 0; m30, m31 8; tiles of 128 px: every pixel in the one froxel (bg3_cluster_raw reads pixels)
    lconsts[31 * 4 + 2] = lconsts[31 * 4 + 3] = 1; lconsts[32 * 4 + 1] = lconsts[32 * 4 + 2] = 8;
    const uint32_t tile = W; std::memcpy(&lconsts[31 * 4], &tile, 4);
  }
  if (lighting) {   // the atmosphere sun path (bg3_sun.hlsli bg3_sun_colour): cam.x[i] = float 40 + 4 i
    cam[40 + 5 * 4] = 1; cam[40 + 5 * 4 + 1] = 1;                          // x[5].x atmosphere on, .y intensity (.z clouds A off)
    cam[40 + 13 * 4] = cam[40 + 13 * 4 + 1] = cam[40 + 13 * 4 + 2] = 1;   // x[13] colour, .w 0
    cam[40 + 14 * 4 + 1] = 1;                                              // x[14] sun direction (0, 1, 0)
  }
  std::vector<float> cam2(56); cam2[12 * 4 + 1] = 1;
  std::vector<uint32_t> lflags(8); { const float m0[2] = {1, 0.5f}; std::memcpy(lflags.data(), m0, 8); lflags[4] = lflags[5] = 1; }
  const float scale[4] = {1, 0, 0, 0};
  const uint32_t clusters[4] = {0, lighting ? 1u | (1u << 24) : 1u, 0, 0};   // lighting: the light, then probe 0 (index list entry 1)
  std::vector<uint32_t> index(64);
  Light light{}; light.pos[0] = 0; light.pos[1] = 4.75f; light.pos[2] = 0; light.inv_r = 1.0f / 3; light.m3 = 1; light.m4 = 2; light.m5 = 0;
  light.chan = 1; light.strength = 1; for (float& c : light.col) c = dark ? 0.0f : 2.0f;
  // G-buffer: depth = y of the first surface along each pixel ray; view normal (0, 0, -1) = rg 0.5.
  std::vector<float> depth(W * H), normal(W * H * 2, 0.5f);
  std::vector<uint16_t> zeros16(W * H * 4);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      const float u = (x + 0.5f) / W, v = (y + 0.5f) / H, rx = 2 * A * u - A, rz = 2 * A * v - A;
      const bool slab = std::abs(rx * kFront) <= 0.6f && std::abs(rz * kFront) <= 0.6f;
      depth[y * W + x] = box ? kBox : slab ? kFront : 5.0f;
    }
  // Push (gi_common.hlsli Push; bg3rtx.ini production defaults where the pass reads them).
  Push pc{};
  pc.full[0] = W; pc.full[1] = H; pc.half[0] = W / 2; pc.half[1] = H / 2; pc.frame = frame; pc.strength = 1;
  pc.rays = 8; pc.rt_bias = 0.001f; pc.rt_tmax = 300; pc.rt_ao_rays = 8;
  pc.gi_rt_radius = 1; pc.gi_rt_tol = 1; pc.gi_rt_fallback = 0; pc.rt_stats = 1;
  pc.sun_radius = 0.00459f; pc.res_shift = 1;
  pc.ls_source_radius = 0.05f; pc.ls_clear = 0.2f; pc.ls_min_radius = 0; pc.ls_flags = 1u << 24;   // hit_ls_policy
  pc.ao_direct = 1; pc.rt_origin = 0;
  // Tile caches (1 x 1 x 1 RGBA32F): albedo, normal (wzy = (0.5, 0.5, 0): frame normal (0, 0, 1)), params.
  const float albedo[4] = {0.8f, 0.6f, 0.4f, 1}, nrm[4] = {0, 0, 0.5f, 0.5f}, params[4] = {0, 0.5f, 0, 0}, page[4] = {0, 0, 0, 0};
  auto vec = [](const float* f, size_t n) { return std::vector<float>(f, f + n); };
  write_file(dir / "push.bin", &pc, sizeof pc);
  write_vec(dir / "cam.bin", cam); write_vec(dir / "vp.bin", vec(vp, 4)); write_vec(dir / "lconsts.bin", lconsts); write_vec(dir / "cam2.bin", cam2);
  write_vec(dir / "lflags.bin", lflags); write_vec(dir / "lscale.bin", vec(scale, 4)); write_file(dir / "lclusters.bin", clusters, 16);
  write_vec(dir / "lindex.bin", index); write_file(dir / "llights.bin", &light, sizeof light);
  write_vec(dir / "depth.r32f", depth); write_vec(dir / "normal.rg32f", normal); write_vec(dir / "prev.rgba16f", zeros16);
  write_vec(dir / "soup.bin", soup); write_vec(dir / "basis.bin", basis); write_file(dir / "instinfo.bin", info, sizeof info);
  write_vec(dir / "records.bin", records); write_vec(dir / "stamp.bin", stampBuf); write_vec(dir / "alpharec.bin", alphaRec);
  write_vec(dir / "albedo.rgba32f", vec(albedo, 4)); write_vec(dir / "nrm.rgba32f", vec(nrm, 4)); write_vec(dir / "params.rgba32f", vec(params, 4));
  write_vec(dir / "page.rgba32f", vec(page, 4));
  VkAccelerationStructureInstanceKHR inst[2] = {};
  for (int i = 0; i < 2; ++i) {
    inst[i].transform.matrix[0][0] = inst[i].transform.matrix[1][1] = inst[i].transform.matrix[2][2] = 1;
    inst[i].instanceCustomIndex = uint32_t(i); inst[i].mask = 0xff;
    inst[i].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    inst[i].accelerationStructureReference = 0x1000u + i;   // opaque BLAS keys
  }
  if (casters) {   // layer/bvhsplit.cpp's cascade instance (common/cascade_casters.h kMask, kCustomIndex)
    inst[1].instanceCustomIndex = 0xFFFFFFu; inst[1].mask = 0x02; inst[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    inst[1].transform.matrix[1][3] = -1;
    write_vec(dir / "casters.bin", caster_soup);
  }
  write_file(dir / "instances.bin", inst, sizeof inst[0] * instances_used);
  // The manifest through the writer the live capture uses (rp::serialize); the soup as two sparse
  // slices of one file, as the live writer stores the InstInfo spans (B + F, then K).
  rp::Manifest m;
  m.version = rp::kVersion; m.pass = "gi_hit_direct"; m.frame = frame;
  m.notes.push_back(box ? std::string(" synthetic: closed box (PT furnace), no material record") :
                          " synthetic: back plane, slab front/back, one point light" + std::string(dark ? " of colour 0 (dark)" : " (lit)"));
  m.groups[0] = (W / 2 + 7) / 8; m.groups[1] = (H / 2 + 7) / 8; m.groups[2] = 1;
  m.push = "push.bin";
  auto buf = [&](uint32_t s, uint32_t b, uint64_t bytes, const char* f) { m.buffers.push_back({s, b, bytes, f ? f : ""}); };
  buf(0, 0, 400, "cam.bin"); buf(0, 1, 16, "vp.bin"); buf(0, 21, 32, "lflags.bin"); buf(0, 22, 16, "lscale.bin");
  buf(0, 23, 16, "lclusters.bin"); buf(0, 24, 256, "lindex.bin"); buf(0, 25, 80, "llights.bin"); buf(0, 26, 544, "lconsts.bin");
  buf(0, 29, 224, "cam2.bin");
  auto img = [&](uint32_t s, uint32_t b, uint32_t e, uint32_t f, uint32_t w, uint32_t h, const char* file) { m.images.push_back({s, b, e, f, w, h, 1, 1, file}); };
  img(0, 5, 0, rp::kR32Sfloat, W, H, "depth.r32f"); img(0, 2, 0, rp::kR32G32Sfloat, W, H, "normal.rg32f"); img(0, 11, 0, rp::kRgba16Sfloat, W, H, "prev.rgba16f");
  buf(1, 0, 16, "alpharec.bin"); buf(1, 1, 256, nullptr); buf(1, 8, 256, nullptr);
  buf(1, 6, soup.size(), nullptr);
  m.ranges.push_back({1, 6, 0, "soup.bin", 0, kBase});
  if (!box) m.ranges.push_back({1, 6, kBase, "soup.bin", kBase, soup.size() - kBase});
  buf(1, 7, 32, "instinfo.bin");
  buf(1, 9, 512, "stamp.bin"); buf(1, 12, recordsBytes, "records.bin");
  buf(1, 13, basis.size(), "basis.bin");
  img(1, 2, 1, rp::kRgba32Sfloat, 1, 1, "page.rgba32f");
  img(1, 14, 1, rp::kRgba32Sfloat, 1, 1, "albedo.rgba32f"); img(1, 14, 2, rp::kRgba32Sfloat, 1, 1, "nrm.rgba32f"); img(1, 14, 3, rp::kRgba32Sfloat, 1, 1, "params.rgba32f");
  m.samplers.push_back({1, 15, 0, 1, 1, 1, 2, 2, 2, 0, 1000});
  m.instances = "instances.bin"; m.instance_count = instances_used;
  if (casters) { m.casters = {"casters.bin", 1, uint32_t(caster_soup.size() / (3 * stride)), stride}; m.defines = "RT_CASCADE_MASKS=1"; }
  m.outputs.push_back({0, 8, rp::kRgba16Sfloat, W / 2, H / 2, ""}); m.outputs.push_back({0, 13, rp::kR32Sfloat, W / 2, H / 2, ""});
  if (lighting) {
    const SynthAmbient& q = kSynthAmbient;
    auto full = [&](float r, float g, float b, float a) { std::vector<float> v(size_t(W) * H * 4); for (size_t i = 0; i < v.size(); i += 4) { v[i] = r; v[i + 1] = g; v[i + 2] = b; v[i + 3] = a; } return v; };
    auto cube = [&](float r, float g, float b) { std::vector<float> v; for (int f = 0; f < 6; ++f) v.insert(v.end(), {r, g, b, 1.0f}); return v; };
    write_vec(dir / "albedo_g.rgba32f", full(q.albedo[0], q.albedo[1], q.albedo[2], 1)); write_vec(dir / "material_g.rgba32f", full(0.5f, 0.5f, 0, 0));
    write_vec(dir / "ao_g.rgba32f", full(q.ao, q.ao, q.ao, 1));
    write_vec(dir / "irr.cube", cube(q.irr[0], q.irr[1], q.irr[2])); write_vec(dir / "spec.cube", cube(q.spec, q.spec, q.spec));
    write_vec(dir / "probe_rgb.cube", cube(q.probe, q.probe, q.probe)); write_vec(dir / "probe_w.cube", cube(1, 1, 1));
    const float dfg[4] = {1, 0, 1, 0}, lut[4] = {q.lut[0], q.lut[1], q.lut[2], 1}, atmo[4] = {636000, 696000, 0, 0};
    write_vec(dir / "dfg.rgba32f", vec(dfg, 4)); write_vec(dir / "lut.rgba32f", vec(lut, 4)); write_vec(dir / "atmo.bin", vec(atmo, 4));
    // One sphere probe (f469db78 _57, 144 B): m0 (fade radius 100, radius^2 1e4, -, fade width 1), identity
    // world -> probe rows, m3 0, m4 1, m5 = has diffuse | diffuse pair 1 (elements 0, 1), m6 0.
    std::vector<float> probe(36); probe[0] = 100; probe[1] = 1e4f; probe[3] = 1; probe[4] = 1; probe[9] = 1; probe[14] = 1; probe[33] = 1;
    { const uint32_t m5 = 2u | (1u << 3); std::memcpy(&probe[34], &m5, 4); }
    write_vec(dir / "probes.bin", probe);
    img(2, 2, 0, rp::kRgba32Sfloat, W, H, "albedo_g.rgba32f"); img(2, 3, 0, rp::kRgba32Sfloat, W, H, "material_g.rgba32f");
    img(2, 6, 0, rp::kRgba32Sfloat, W, H, "ao_g.rgba32f"); img(2, 38, 0, rp::kRgba32Sfloat, 1, 1, "dfg.rgba32f");
    auto cimg = [&](uint32_t b, uint32_t e, const char* file) { m.images.push_back({2, b, e, rp::kRgba32Sfloat, 1, 1, 6, 1, file}); };
    cimg(34, 0, "irr.cube"); cimg(36, 0, "irr.cube"); cimg(35, 0, "spec.cube"); cimg(37, 0, "spec.cube");
    cimg(45, 0, "probe_rgb.cube"); cimg(45, 1, "probe_w.cube");
    buf(2, 33, probe.size() * 4, "probes.bin"); buf(3, 0, 16, "atmo.bin");
    img(3, 1, 0, rp::kRgba32Sfloat, 1, 1, "lut.rgba32f");
    m.samplers.push_back({2, 16, 0, 1, 1, 1, 2, 2, 2, 0, 1000}); m.samplers.push_back({2, 32, 0, 1, 1, 1, 2, 2, 2, 0, 1000});
    m.samplers.push_back({3, 6, 0, 1, 1, 1, 2, 2, 2, 0, 1000});
    m.notes.push_back(" synthetic lighting inputs (sets 2-3): kSynthAmbient");
  }
  const std::string mf = rp::serialize(m);
  { rp::Manifest back; std::string error; check(rp::parse(mf, back, error), "synthetic manifest does not parse: " + error); }
  write_file(dir / "manifest.txt", mf.data(), mf.size());
}

// ---------------------------------------------------------------- the writer's image read (self-test 9-11)
// layer/replay.cpp reads game images without TRANSFER_SRC through shaders/replay_image.hlsl; this
// runs the same shader with the same piece plan (rp::shader_read_pieces, 16-byte aligned staging
// slots, packed into the file layout) on images of every mapped format.
struct ReadPipe { VkDescriptorSetLayout dsl{}; VkPipelineLayout pl{}; VkPipeline pipe{}; VkDescriptorPool pool{}; };
ReadPipe make_read_pipe(const std::vector<uint32_t>& code) {
  ReadPipe r;
  VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
  VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dl.bindingCount = 2; dl.pBindings = b;
  vkcheck(vkCreateDescriptorSetLayout(g.dev, &dl, nullptr, &r.dsl), "read set layout");
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = 1; pl.pSetLayouts = &r.dsl; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
  vkcheck(vkCreatePipelineLayout(g.dev, &pl, nullptr, &r.pl), "read pipeline layout");
  VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = code.size() * 4; smi.pCode = code.data();
  VkShaderModule module; vkcheck(vkCreateShaderModule(g.dev, &smi, nullptr, &module), "read shader module");
  VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main"}; cpi.layout = r.pl;
  vkcheck(vkCreateComputePipelines(g.dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &r.pipe), "read pipeline");
  VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  dp.maxSets = 1; dp.poolSizeCount = 2; dp.pPoolSizes = sizes;
  vkcheck(vkCreateDescriptorPool(g.dev, &dp, nullptr, &r.pool), "read pool");
  return r;
}
std::vector<uint8_t> shader_read(const ReadPipe& rpipe, const Img& im, const rp::ShaderRead& r, uint64_t max_piece) {
  const auto pieces = rp::shader_read_pieces(r, im.w, im.h, im.layers, im.mips, max_piece);
  std::vector<uint64_t> slot;
  uint64_t at = 0;
  for (const auto& p : pieces) { at = (at + 15) & ~15ull; slot.push_back(at); at += (p.bytes + 3) & ~3ull; }
  Buf staging = make_buffer(at, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
  std::memset(staging.map, 0xCD, size_t(staging.size));
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = rpipe.pool; da.descriptorSetCount = 1; da.pSetLayouts = &rpipe.dsl;
  VkDescriptorSet set; vkcheck(vkAllocateDescriptorSets(g.dev, &da, &set), "read set");
  VkDescriptorImageInfo ii{VK_NULL_HANDLE, im.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorBufferInfo bi{staging.b, 0, staging.size};
  VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
  w[0].dstSet = set; w[0].dstBinding = 0; w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[0].pImageInfo = &ii;
  w[1].dstSet = set; w[1].dstBinding = 1; w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &bi;
  vkUpdateDescriptorSets(g.dev, 2, w, 0, nullptr);
  const uint64_t per = rp::shader_read_per_thread(r);
  submit([&](VkCommandBuffer c) {
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, rpipe.pipe);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, rpipe.pl, 0, 1, &set, 0, nullptr);
    for (size_t k = 0; k < pieces.size(); ++k) {
      const auto& p = pieces[k];
      const uint32_t push[8] = {uint32_t(slot[k]), uint32_t(p.first), uint32_t(p.count), std::max(im.w >> p.mip, 1u), std::max(im.h >> p.mip, 1u),
                                p.mip, r.encode, r.channels};
      vkCmdPushConstants(c, rpipe.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
      vkCmdDispatch(c, uint32_t(((p.count + per - 1) / per + 255) / 256), 1, 1);
    }
    barrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  });
  vkFreeDescriptorSets(g.dev, rpipe.pool, 1, &set);
  std::vector<uint8_t> out(size_t(rp::image_bytes(r.pack_format, im.w, im.h, im.layers, im.mips)));
  for (size_t k = 0; k < pieces.size(); ++k) std::memcpy(out.data() + pieces[k].file_offset, static_cast<uint8_t*>(staging.map) + slot[k], size_t(pieces[k].bytes));
  vkDestroyBuffer(g.dev, staging.b, nullptr); vkFreeMemory(g.dev, staging.m, nullptr);
  return out;
}
struct ReadCheck { uint32_t formats = 0, skipped = 0, exact_fail = 0, tol_fail = 0, piece_fail = 0; std::string detail; };
ReadCheck check_shader_reads(const fs::path& dir) {
  ReadCheck rc;
  const ReadPipe rpipe = make_read_pipe(compile(fs::path(source_root()) / "shaders" / "replay_image.hlsl", {}, dir / "replay_image.spv"));
  const rp::ShaderRead as_float{rp::kRgba32Sfloat, rp::kEncFloat, 4};
  const uint32_t formats[] = {37, 44, 43, 50, 38, 9, 10, 16, 17, 64, 70, 77, 91, 76, 83, 97, 100, 103, 109, 122, 123,
                              131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146};
  constexpr uint32_t W = 13, H = 9, L = 3, M = 3;   // odd sizes, partial blocks, 3 layers, mips 13x9 / 6x4 / 3x2
  uint64_t seed = 0x9E3779B97F4A7C15ull;
  auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return uint32_t(seed); };
  std::string worst;
  for (uint32_t f : formats) {
    rp::ShaderRead r;
    check(rp::shader_read_format(f, false, r), "shader_read_format " + std::to_string(f));
    VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(g.physical, VkFormat(f), &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) { ++rc.skipped; continue; }
    ++rc.formats;
    // Source texels: random, made finite for float formats and free of SNORM -128 (it reads as -127).
    std::vector<uint8_t> src(size_t(rp::image_bytes(f, W, H, L, M)));
    for (auto& b : src) b = uint8_t(rnd());
    if (f == 10 || f == 17 || f == 38) for (auto& b : src) if (b == 0x80) b = 0x81;
    if (f == 76 || f == 83 || f == 97)   // normal halves (exponent 1..30)
      for (size_t i = 0; i + 1 < src.size(); i += 2) { uint16_t v = uint16_t(rnd()); v = uint16_t((v & 0x83FF) | ((1 + (v >> 10 & 31) % 30) << 10)); std::memcpy(&src[i], &v, 2); }
    if (f == 100 || f == 103 || f == 109)   // normal floats of magnitude 2^-27 .. 2^36
      for (size_t i = 0; i + 3 < src.size(); i += 4) { uint32_t v = rnd(); v = (v & 0x807FFFFFu) | ((100u + (v >> 23 & 255) % 64) << 23); std::memcpy(&src[i], &v, 4); }
    if (f == 122) for (size_t i = 0; i + 3 < src.size(); i += 4) { uint32_t v; std::memcpy(&v, &src[i], 4); v &= ~((1u << 10) | (1u << 21) | (1u << 31)); std::memcpy(&src[i], &v, 4); }
    Img im = make_image(f, W, H, L, M, false, true);
    fill_image(im, src);
    const auto packed = shader_read(rpipe, im, r, 200);   // many pieces per mip
    if (packed != shader_read(rpipe, im, r, 1ull << 30)) ++rc.piece_fail;
    // Exact: uncompressed formats stored in their own layout keep their bits (BGRA8 swizzled to RGBA8).
    if (!rp::block_compressed(f) && r.pack_format == f && packed != src) ++rc.exact_fail;
    if (f == 44 || f == 50) {
      std::vector<uint8_t> sw = src;
      for (size_t i = 0; i + 3 < sw.size(); i += 4) std::swap(sw[i], sw[i + 2]);
      if (packed != sw) ++rc.exact_fail;
    }
    // Tolerance: the stored texels read back (as the replay's image) against the game format's.
    Img back = make_image(r.pack_format, W, H, L, M, false, true);
    fill_image(back, packed);
    const auto a = shader_read(rpipe, im, as_float, 1ull << 30), b = shader_read(rpipe, back, as_float, 1ull << 30);
    const float* fa = reinterpret_cast<const float*>(a.data()), * fb = reinterpret_cast<const float*>(b.data());
    double max_units = 0;   // in steps of the stored encoding
    for (size_t i = 0; i < a.size() / 4; ++i) {
      double x = fa[i], y = fb[i], step;
      switch (r.encode) {
        case rp::kEncUnorm8: step = 1.0 / 255; break;
        case rp::kEncSnorm8: step = 1.0 / 127; break;
        case rp::kEncSrgb8: {   // colour compared in sRGB space; alpha is linear
          auto enc = [](double l) { l = std::clamp(l, 0.0, 1.0); return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055; };
          if (i % 4 != 3) { x = enc(x); y = enc(y); }
          step = 1.0 / 255; break;
        }
        case rp::kEncUnorm16: step = 1.0 / 65535; break;
        case rp::kEncHalf: step = std::max(std::abs(x) * std::ldexp(1.0, -10), std::ldexp(1.0, -24)); break;
        default: step = std::max(std::abs(x) * std::ldexp(1.0, -23), 1e-45); break;
      }
      if (std::isnan(x) && std::isnan(y)) continue;
      max_units = std::max(max_units, std::abs(x - y) / step);
    }
    // At most half a step (rounding), plus the GPU's sRGB conversion error for the sRGB formats.
    const double limit = r.encode == rp::kEncSrgb8 ? 0.51 : 0.5 + 1e-3;
    if (!(max_units <= limit)) ++rc.tol_fail;
    if (rp::block_compressed(f) || max_units > 0) {
      char s[64]; std::snprintf(s, sizeof s, "%s%u %.2f", worst.empty() ? "" : ", ", f, max_units);
      worst += s;
    }
  }
  rc.detail = std::to_string(rc.formats) + " formats (" + std::to_string(rc.skipped) + " unsupported), max |diff| in stored steps: " + worst;
  return rc;
}

// --load-test (manual, not a CTest: 2 GB of device memory): the S8 hit VT cache of 2026-11-01
// (BC3 sRGB 32688 x 26352, 2 layers, 2 mips, 2053.7 MB) created and filled through the staged
// upload; its last mip is read back and compared.
int load_test() {
  if (int r = init_gpu(true, false)) return r;
  const uint32_t f = 138, w = 32688, h = 26352, layers = 2, mips = 2;
  auto pattern = [](uint64_t at) { uint64_t x = (at / 8 + 1) * 0x9E3779B97F4A7C15ull; x ^= x >> 29; return uint8_t(x >> (8 * (at % 8))); };
  const auto t0 = GetTickCount64();
  Img im = make_image(f, w, h, layers, mips, false, true);
  fill_image_pieces(im, [&](uint64_t offset, uint64_t n, uint8_t* dst) { for (uint64_t i = 0; i < n; ++i) dst[i] = pattern(offset + i); });
  const double fill_s = (GetTickCount64() - t0) / 1000.0;
  const uint64_t mip0 = rp::image_bytes(f, w, h, layers, 1), mip1 = rp::image_bytes(f, w >> 1, h >> 1, layers, 1);
  Buf staging = make_buffer(mip1, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  submit([&](VkCommandBuffer c) {
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; ib.oldLayout = layout_of(im);
    ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = im.image; ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, layers};
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, layers}; r.imageExtent = {w >> 1, h >> 1, 1};
    vkCmdCopyImageToBuffer(c, im.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.b, 1, &r);
    barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  });
  uint64_t bad = 0;
  const uint8_t* back = static_cast<const uint8_t*>(staging.map);
  for (uint64_t i = 0; i < mip1; ++i) bad += back[i] != pattern(mip0 + i);
  uint64_t heap = 0;
  for (uint32_t k = 0; k < g.memory.memoryHeapCount; ++k)
    if (g.memory.memoryHeaps[k].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap = std::max<uint64_t>(heap, g.memory.memoryHeaps[k].size);
  std::printf("load test: %ux%u x%u m%u BC3 %.1f MB filled in %zu pieces (%.0f MB staging) in %.1f s; mip 1 read back: %llu bytes differ; "
              "device-local %.1f MB of a %.1f MB heap; validation errors %u\n",
              w, h, layers, mips, rp::image_bytes(f, w, h, layers, mips) / 1048576.0, rp::transfer_pieces(f, w, h, layers, mips, kStageBytes).size(),
              kStageBytes / 1048576.0, fill_s, (unsigned long long)bad, g.device_bytes / 1048576.0, heap / 1048576.0, g_validation_errors.load());
  return bad || g_validation_errors.load() ? 1 : 0;
}

// ---------------------------------------------------------------- PT reference (tools/pt_reference.hlsl)
// Runs the offline path tracer over a pack: `iters` iterations x `modes` dispatches. Iteration i
// pushes Push::ls_kmode = i + 1 (the shader's seed with Push::frame, which stays the pack's: it is the
// hit-material epoch), every mode of one iteration the same seed, so all modes see the same paths;
// Push::debug = mode, rt_stats 0. Sums each mode's giRaw (rgba) and giRawDepth (aux) in double; the
// mean is sum / iters (the shader averages its PT_SPP).
constexpr uint32_t kPtModes = 7, kPtModesEnv = 8;   // 8: + mode 7, the environment at misses (PT_ENV)
struct PtAccum {
  uint32_t w = 0, h = 0, iters = 0, modes = 0;
  std::vector<double> rgba[kPtModesEnv], aux[kPtModesEnv];
  std::vector<double> gpu_ms;
  double mean(uint32_t mode, uint32_t c, size_t i) const { return (c < 4 ? rgba[mode][i * 4 + c] : aux[mode][i]) / std::max(iters, 1u); }
};
// `shader` (in tools/): pt_reference.hlsl, or native_ambient.hlsl (--native-ambient, same mode protocol).
PtAccum pt_run(const fs::path& pack, std::vector<std::string> defines, uint32_t iters, uint32_t modes, const fs::path& out, bool quiet = true,
               const std::string& shader = "pt_reference.hlsl") {
  check(modes <= kPtModesEnv, "at most 8 modes");
  PtAccum acc; acc.iters = iters; acc.modes = modes;
  ReplayHooks hooks;
  hooks.patch = [&](uint32_t run, std::vector<uint8_t>& push) {
    const uint32_t iteration = run / modes + 1, mode = run % modes, zero = 0;
    std::memcpy(push.data() + offsetof(Push, ls_kmode), &iteration, 4);
    std::memcpy(push.data() + offsetof(Push, debug), &mode, 4);
    std::memcpy(push.data() + offsetof(Push, rt_stats), &zero, 4);
  };
  hooks.note = [&](uint32_t done) { return std::to_string(done / modes) + "/" + std::to_string(iters) + " " + shader + " iterations"; };
  hooks.each = [&](uint32_t run, const RunResult& rr) {
    const uint32_t mode = run % modes;
    acc.gpu_ms.push_back(rr.gpu_ms);
    for (const auto& o : rr.outputs) {
      const size_t n = size_t(o.w) * o.h;
      if (o.name == "giRaw") {
        acc.w = o.w; acc.h = o.h;
        auto& v = acc.rgba[mode]; if (v.empty()) v.assign(n * 4, 0.0);
        const auto* hp = reinterpret_cast<const uint16_t*>(o.raw.data());
        for (size_t i = 0; i < n * 4; ++i) { const float x = half_to_float(hp[i]); if (std::isfinite(x)) v[i] += x; }
      } else if (o.name == "giRawDepth") {
        auto& v = acc.aux[mode]; if (v.empty()) v.assign(n, 0.0);
        const auto* fp = reinterpret_cast<const float*>(o.raw.data());
        for (size_t i = 0; i < n; ++i) if (std::isfinite(fp[i])) v[i] += fp[i];
      }
    }
  };
  // One DXC compile per define set (the pack's variant defines appended as replay() does): the
  // self-test runs the same variant on several packs.
  static std::map<std::string, std::vector<uint32_t>> cache;
  rp::Manifest m; std::string error;
  { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
  std::vector<std::string> all = defines;
  for (size_t a = 0; a < m.defines.size();) { size_t e = m.defines.find(';', a); all.push_back(m.defines.substr(a, e - a)); if (e == std::string::npos) break; a = e + 1; }
  std::string key = shader + "|"; for (auto& d : all) key += d + ";";
  auto it = cache.find(key);
  if (it == cache.end()) {
    fs::create_directories(out);
    it = cache.emplace(key, compile(fs::path(source_root()) / "tools" / shader, all, out / (fs::path(shader).stem().string() + ".spv"))).first;
  }
  replay(pack, &it->second, {}, {}, out, iters * modes, quiet, &hooks);
  return acc;
}
double median(std::vector<double> v) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
// Approximate sun transmittance for the atmosphere path (the pack has no LUT): Kasten-Young air
// mass at the sun's elevation (lighting space y up, bg3_sun.hlsli), Rayleigh optical depth at
// 680 / 550 / 440 nm (0.036, 0.097, 0.236) plus 0.1 aerosol. A declared estimate, not the game's LUT.
void sun_transmittance(float mu, float t[3]) {
  if (!(mu > -0.05f)) { t[0] = t[1] = t[2] = 0; return; }
  const double z = std::acos(std::clamp<double>(mu, -1, 1)) * 180.0 / 3.14159265358979;
  const double am = 1.0 / (std::max<double>(mu, 0) + 0.50572 * std::pow(std::max(96.07995 - z, 1e-3), -1.6364));
  const double tau[3] = {0.036 + 0.1, 0.097 + 0.1, 0.236 + 0.1};
  for (int k = 0; k < 3; ++k) t[k] = float(std::exp(-tau[k] * am));
}
void write_bmp(const fs::path& p, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb) {
  const uint32_t row = (w * 3 + 3) & ~3u, size = 54 + row * h;
  std::vector<uint8_t> f(size, 0);
  auto u32 = [&](size_t at, uint32_t v) { std::memcpy(&f[at], &v, 4); };
  f[0] = 'B'; f[1] = 'M'; u32(2, size); u32(10, 54); u32(14, 40); u32(18, w); u32(22, h);
  f[26] = 1; f[28] = 24; u32(34, row * h);
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x)
      for (int k = 0; k < 3; ++k) f[54 + size_t(h - 1 - y) * row + x * 3 + (2 - k)] = rgb[(size_t(y) * w + x) * 3 + k];
  write_file(p, f.data(), f.size());
}
void write_pfm_rgb(const fs::path& p, uint32_t w, uint32_t h, const std::function<float(size_t, int)>& at) {
  std::string header = "PF\n" + std::to_string(w) + " " + std::to_string(h) + "\n-1.0\n";
  std::vector<float> rgb(size_t(w) * h * 3);
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x)
      for (int k = 0; k < 3; ++k) rgb[(size_t(h - 1 - y) * w + x) * 3 + k] = at(size_t(y) * w + x, k);
  std::ofstream f(p, std::ios::binary); f << header; f.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size() * 4));
}

// The comparison against the pack's live giRaw (s0b8.live) over the pixels the live pass shaded
// (live giRawDepth > 0): image means, the energy split by first-hit class / distance / light,
// per-region (16 x 9 tiles) ratios, images. Luminance (Rec. 709) throughout.
void pt_report(const fs::path& pack, const PtAccum& a, const fs::path& out, const std::string& params) {
  rp::Manifest m; std::string error;
  { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
  std::string live_rgba, live_depth;
  for (const auto& o : m.outputs) { if (o.binding == 8) live_rgba = o.reference; if (o.binding == 13) live_depth = o.reference; }
  const size_t n = size_t(a.w) * a.h;
  std::vector<float> lv(n * 4, 0.0f), ld(n, 1.0f);
  const bool have_live = !live_rgba.empty() && fs::exists(pack / live_rgba);
  if (have_live) {
    auto raw = read_file(pack / live_rgba); check(raw.size() == n * 8, "live giRaw size");
    const auto* hp = reinterpret_cast<const uint16_t*>(raw.data());
    for (size_t i = 0; i < n * 4; ++i) lv[i] = half_to_float(hp[i]);
    if (!live_depth.empty() && fs::exists(pack / live_depth)) {
      auto d = read_file(pack / live_depth); check(d.size() == n * 4, "live giRawDepth size"); std::memcpy(ld.data(), d.data(), d.size());
    }
  }
  float radius = 0;
  { auto push = read_file(pack / m.push); if (push.size() >= sizeof(Push)) std::memcpy(&radius, push.data() + offsetof(Push, gi_rt_radius), 4); }
  auto lum = [](double r, double g, double b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
  auto L = [&](uint32_t mode, size_t i) { return lum(a.mean(mode, 0, i), a.mean(mode, 1, i), a.mean(mode, 2, i)); };
  // Image means over the shaded pixels.
  double cnt = 0, s_live = 0, s_live_a = 0;
  double s[kPtModes][5] = {};   // per mode: lum rgb, a, aux, (unused)
  for (size_t i = 0; i < n; ++i) {
    if (!(ld[i] > 0) || !std::isfinite(lv[i * 4])) continue;
    cnt += 1; s_live += lum(lv[i * 4], lv[i * 4 + 1], lv[i * 4 + 2]); s_live_a += lv[i * 4 + 3];
    for (uint32_t k = 0; k < a.modes; ++k) { s[k][0] += L(k, i); s[k][1] += a.mean(k, 3, i); s[k][2] += a.mean(k, 4, i); }
  }
  check(cnt > 0, "no shaded pixels to compare");
  auto M = [&](uint32_t mode, int c) { return mode < a.modes ? s[mode][c] / cnt : 0.0; };
  const double E = M(0, 0), live = s_live / cnt;
  auto pct = [&](double x) { return E > 0 ? 100.0 * x / E : 0.0; };
  std::vector<std::string> lines;
  char b[1024];
  auto add = [&](const char* fmt, auto... v) { std::snprintf(b, sizeof b, fmt, v...); lines.push_back(b); std::printf("  %s\n", b); };
  add("pt reference: %s; %u iterations x %u modes, %ux%u GI pixels, %.0f shaded (live giRawDepth > 0)", params.c_str(), a.iters, a.modes, a.w, a.h, cnt);
  add("dispatch GPU ms: median %.3f, total %.1f s", median(a.gpu_ms), [&] { double t = 0; for (double x : a.gpu_ms) t += x; return t * 1e-3; }());
  add("means (luminance of mean incident radiance, the giRaw.rgb quantity): live giRaw %.6g (live a %.4f); PT total %.6g, PT 1-bounce %.6g", live, s_live_a / cnt, E, M(1, 0));
  double k = 1;   // game / PT on visible first hits (below)
  if (a.modes >= kPtModes) {
    const double vis = M(0, 2) + M(1, 2), hid = M(2, 2) + M(3, 2), off = M(4, 2);
    add("PT energy by first hit: visible on screen %.1f%% (near %.1f, far %.1f), on screen but hidden %.1f%% (near %.1f, far %.1f), off screen %.1f%%",
        pct(vis), pct(M(0, 2)), pct(M(1, 2)), pct(hid), pct(M(2, 2)), pct(M(3, 2)), pct(off));
    add("PT energy by light: sun %.1f%%, local point/spot %.1f%%; multi-bounce (vertex >= 2) %.1f%%", pct(M(5, 2)), pct(M(6, 2)), pct(E - M(1, 0)));
    add("ray fractions: miss (sky/unknown environment, 0 in PT) %.4f, first hit visible %.4f, hidden on screen %.4f, near (t < gi_rt_radius) %.4f, aborted %.5f, "
        "qualified hit material %.5f", M(0, 1), M(1, 1), M(6, 1), M(3, 1), M(4, 1), M(2, 1));
    add("PT energy by distance: first hit beyond gi_rt_radius %.1f%% (visible %.1f, hidden %.1f, off screen part not split); live domain (near x fade) %.1f%%",
        pct(M(1, 2) + M(3, 2)), pct(M(1, 2)), pct(M(3, 2)), pct(M(3, 0)));
    // Diagnostics per channel (mode 5 rgb is not a colour).
    double dr = 0, dg = 0, db = 0;
    for (size_t i = 0; i < n; ++i) if (ld[i] > 0 && std::isfinite(lv[i * 4])) { dr += a.mean(5, 0, i); dg += a.mean(5, 1, i); db += a.mean(5, 2, i); }
    add("vertex diagnostics: no froxel light list (local lights unsupported) %.4f, sun ray unknown %.5f, fallback material %.4f; mean vertices per path %.3f",
        dr / cnt, dg / cnt, db / cnt, M(5, 1));
    const double ptvis = vis, scr = M(2, 0);
    k = ptvis > 0 ? scr / ptvis : 0;
    add("calibration on visible first hits (same paths): screen radiance %.6g vs PT radiance %.6g -> k = %.3f (game / PT: material, ambient, emission the PT lacks)", scr, ptvis, k);
    add("live domain (first hit t < %.3g m, live fade): PT %.6g, PT x k %.6g, of it hidden/off-screen (fallback0 adds 0) %.6g = %.1f%%; screen tier (game radiance x fade) %.6g",
        radius, M(3, 0), M(3, 0) * k, M(6, 0), M(3, 0) > 0 ? 100.0 * M(6, 0) / M(3, 0) : 0.0, M(4, 0));
    add("live vs PT: live / PT-live-domain %.3f, live / (k x PT-live-domain) %.3f, live / screen tier %.3f (live above it: in-game hidden-hit direct %.6g), "
        "live / PT total %.3f, live / (k x PT total) %.3f",
        M(3, 0) > 0 ? live / M(3, 0) : 0.0, M(3, 0) * k > 0 ? live / (M(3, 0) * k) : 0.0, M(4, 0) > 0 ? live / M(4, 0) : 0.0, live - M(4, 0),
        E > 0 ? live / E : 0.0, E * k > 0 ? live / (E * k) : 0.0);
  }
  // Per region: 16 x 9 tiles, mean luminance live vs PT (live domain and total).
  constexpr int TX = 16, TY = 9;
  struct Tile { double n = 0, live = 0, dom = 0, total = 0, scr = 0; };
  std::vector<Tile> tiles(TX * TY);
  for (uint32_t y = 0; y < a.h; ++y)
    for (uint32_t x = 0; x < a.w; ++x) {
      const size_t i = size_t(y) * a.w + x;
      if (!(ld[i] > 0) || !std::isfinite(lv[i * 4])) continue;
      Tile& t = tiles[(y * TY / a.h) * TX + x * TX / a.w];
      t.n += 1; t.live += lum(lv[i * 4], lv[i * 4 + 1], lv[i * 4 + 2]); t.total += L(0, i);
      if (a.modes > 4) { t.dom += L(3, i); t.scr += L(4, i); }
    }
  std::vector<double> r_near, r_total, gap;
  double abs_dom = 0, sum_dom = 0;
  std::ofstream csv(out / "pt_tiles.csv");
  csv << "tx,ty,pixels,live,pt_live_domain,pt_total,screen_tier\n";
  for (int ty = 0; ty < TY; ++ty)
    for (int tx = 0; tx < TX; ++tx) {
      Tile& t = tiles[ty * TX + tx];
      if (t.n < 1) continue;
      t.live /= t.n; t.dom /= t.n; t.total /= t.n; t.scr /= t.n;
      csv << tx << "," << ty << "," << t.n << "," << t.live << "," << t.dom << "," << t.total << "," << t.scr << "\n";
      if (t.dom > 0) r_near.push_back(t.live / (k * t.dom));
      abs_dom += t.n * std::abs(t.live - k * t.dom); sum_dom += t.n * k * t.dom;
      if (t.total > 0) r_total.push_back(t.live / (k * t.total));
      gap.push_back(t.total - t.live);
    }
  auto q = [](std::vector<double> v, double p) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[size_t(p * (v.size() - 1))]; };
  add("per region (16 x 9 tiles, %zu with pixels, PT x k): live / PT-live-domain p10 %.3f median %.3f p90 %.3f, pixel-weighted mean |live - PT| / PT %.3f; "
      "live / PT total p10 %.3f median %.3f p90 %.3f", gap.size(), q(r_near, 0.1), q(r_near, 0.5), q(r_near, 0.9), sum_dom > 0 ? abs_dom / sum_dom : 0.0,
      q(r_total, 0.1), q(r_total, 0.5), q(r_total, 0.9));
  { std::ofstream f(out / "pt_report.txt"); for (auto& l : lines) f << l << "\n"; }
  // Images: PFM (linear) and one BMP strip live | PT live domain | PT total at one exposure.
  write_pfm_rgb(out / "pt_total.pfm", a.w, a.h, [&](size_t i, int k) { return float(a.mean(0, k, i)); });
  if (a.modes > 3) write_pfm_rgb(out / "pt_live_domain.pfm", a.w, a.h, [&](size_t i, int k) { return float(a.mean(3, k, i)); });
  write_pfm_rgb(out / "live_giRaw.pfm", a.w, a.h, [&](size_t i, int k) { return lv[i * 4 + k]; });
  const uint32_t sw = std::max(1u, a.w / 640), W = a.w / sw, H = a.h / sw;
  std::vector<uint8_t> strip(size_t(W) * 3 * H * 3);
  const double exposure = E > 0 ? 0.5 / E : 1.0;
  auto tone = [&](double v) { v = std::max(0.0, v * exposure); v = v / (1 + v); v = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055; return uint8_t(std::clamp(v, 0.0, 1.0) * 255 + 0.5); };
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      const size_t i = size_t(y * sw) * a.w + x * sw;
      for (int k = 0; k < 3; ++k) {
        strip[(size_t(y) * W * 3 + x) * 3 + k] = tone(lv[i * 4 + k]);
        strip[(size_t(y) * W * 3 + W + x) * 3 + k] = tone(a.modes > 3 ? a.mean(3, k, i) : 0.0);
        strip[(size_t(y) * W * 3 + 2 * W + x) * 3 + k] = tone(a.mean(0, k, i));
      }
    }
  write_bmp(out / "pt_compare.bmp", W * 3, H, strip);
}

// Cost of real-time-sized PT kernels on this pack (mode 0 only, rt_stats 0): 1 spp with 1 and 2
// path vertices (RR off), each with sun + local-light NEE shadow rays, rays up to 300 m and 8 m.
// Median of `runs` dispatches.
void pt_cost(const fs::path& pack, const std::vector<std::string>& base, const fs::path& out) {
  const uint32_t runs = 9;
  struct Config { uint32_t bounces; const char* tmax; };
  for (const Config& c : {Config{1, "300.0"}, Config{2, "300.0"}, Config{1, "8.0"}, Config{2, "8.0"}}) {
    std::vector<std::string> d = base;
    d.push_back("PT_SPP=1"); d.push_back("PT_BOUNCES=" + std::to_string(c.bounces)); d.push_back("PT_RR_START=99");
    d.push_back(std::string("PT_TMAX=") + c.tmax);
    PtAccum a = pt_run(pack, d, runs, 1, out);
    const double px = double(a.w) * a.h, ms = median(a.gpu_ms);
    std::printf("  pt cost: 1 spp, %u vertex%s, rays <= %s m (NEE sun + local at each): median %.3f ms at %ux%u (%.3f ms per Mpx); scaled to DLSS 4K "
                "render Quality 2560x1440 %.2f ms, Balanced 2227x1253 %.2f ms, Performance 1920x1080 %.2f ms\n", c.bounces, c.bounces > 1 ? "es" : "",
                c.tmax, ms, a.w, a.h, ms / px * 1e6, ms / px * 2560 * 1440, ms / px * 2227 * 1253, ms / px * 1920 * 1080);
  }
}

// ---------------------------------------------------------------- native ambient (tools/native_ambient.hlsl)
// --native-ambient: the game's diffuse ambient at each GI pixel (E_native, the giRaw.rgb quantity, from
// the pack's lighting inputs) against the PT reference's far field over the same paths and pixels:
// PT total minus its live domain (first hits within gi_rt_radius x the live fade), calibrated by k
// (game / PT on visible first hits, as pt_report), plus the environment the PT's misses see (PT_ENV,
// game units). Pixels: those the live pass shaded (live giRawDepth > 0), else every shaded pixel.
// Luminance (Rec. 709) throughout.
void native_ambient_report(const fs::path& pack, const PtAccum& na, const PtAccum& pt, const fs::path& out, const std::string& params,
                           const float est_t[3]) {
  rp::Manifest m; std::string error;
  { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
  check(na.w == pt.w && na.h == pt.h && na.modes >= 3 && pt.modes >= kPtModes, "native ambient and PT runs over the same pixels");
  const size_t n = size_t(na.w) * na.h;
  std::string live_rgba, live_depth;
  for (const auto& o : m.outputs) { if (o.binding == 8) live_rgba = o.reference; if (o.binding == 13) live_depth = o.reference; }
  std::vector<float> lv(n * 4, 0.0f), ld(n, 0.0f);
  const bool have_live = !live_rgba.empty() && fs::exists(pack / live_rgba) && !live_depth.empty() && fs::exists(pack / live_depth);
  if (have_live) {
    auto raw = read_file(pack / live_rgba); check(raw.size() == n * 8, "live giRaw size");
    const auto* hp = reinterpret_cast<const uint16_t*>(raw.data());
    for (size_t i = 0; i < n * 4; ++i) lv[i] = half_to_float(hp[i]);
    auto d = read_file(pack / live_depth); check(d.size() == n * 4, "live giRawDepth size"); std::memcpy(ld.data(), d.data(), d.size());
  }
  float radius = 0;
  { auto push = read_file(pack / m.push); if (push.size() >= sizeof(Push)) std::memcpy(&radius, push.data() + offsetof(Push, gi_rt_radius), 4); }
  auto lum = [](double r, double g, double b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
  auto L = [&](const PtAccum& a, uint32_t mode, size_t i) { return lum(a.mean(mode, 0, i), a.mean(mode, 1, i), a.mean(mode, 2, i)); };
  const bool env = pt.modes > 7;
  auto use = [&](size_t i) {
    const double E = L(na, 0, i);
    if (!std::isfinite(E) || !std::isfinite(L(pt, 0, i))) return false;
    return have_live ? ld[i] > 0 && std::isfinite(lv[i * 4]) : E > 0;
  };
  // Calibration k over the compared pixels (pt_report: screen radiance / PT radiance on visible first hits).
  double cnt = 0, s_scr = 0, s_vis = 0;
  for (size_t i = 0; i < n; ++i) if (use(i)) { cnt += 1; s_scr += L(pt, 2, i); s_vis += pt.mean(0, 4, i) + pt.mean(1, 4, i); }
  check(cnt > 0, "no shaded pixels to compare");
  const double k = s_vis > 0 ? s_scr / s_vis : 0;
  auto far_rgb = [&](size_t i, int c) { return k * (pt.mean(0, c, i) - pt.mean(3, c, i)) + (env ? pt.mean(7, c, i) : 0.0); };
  struct Sum { double E = 0, Eao = 0, Eaoa = 0, sky = 0, lit = 0, spec = 0, probe = 0, ao = 0, a = 0, pt_far = 0, far_on = 0, off = 0, env = 0, env1 = 0, miss = 0,
               t[3] = {0, 0, 0}, cloud = 0, atmo = 0, tot = 0, vis_far = 0, hid_far = 0, live = 0, sun = 0, local = 0; } S;
  constexpr int TX = 16, TY = 9;
  struct Tile { double n = 0, nat = 0, ref = 0, refs = 0; };
  std::vector<Tile> tiles(TX * TY);
  for (uint32_t y = 0; y < na.h; ++y)
    for (uint32_t x = 0; x < na.w; ++x) {
      const size_t i = size_t(y) * na.w + x;
      if (!use(i)) continue;
      const double E = L(na, 0, i), ao = na.mean(0, 3, i), la = have_live ? lv[i * 4 + 3] : 1.0;
      const double pfar = L(pt, 0, i) - L(pt, 3, i), e = env ? L(pt, 7, i) : 0.0;
      S.E += E; S.Eao += E * ao; S.Eaoa += E * ao * la; S.sky += na.mean(0, 4, i); S.ao += ao; S.a += la;
      S.lit += L(na, 1, i); S.probe += na.mean(1, 3, i); S.spec += na.mean(1, 4, i);
      S.pt_far += pfar; S.far_on += pt.mean(1, 4, i) + pt.mean(3, 4, i); S.off += pt.mean(4, 4, i); S.env += e; S.env1 += env ? pt.mean(7, 4, i) : 0.0;
      S.miss += pt.mean(0, 3, i);
      S.tot += L(pt, 0, i); S.vis_far += pt.mean(1, 4, i); S.hid_far += pt.mean(3, 4, i); S.live += L(pt, 3, i);
      S.sun += pt.mean(5, 4, i); S.local += pt.mean(6, 4, i);
      for (int c = 0; c < 3; ++c) S.t[c] += na.mean(2, c, i);
      S.cloud += na.mean(2, 3, i); S.atmo += na.mean(2, 4, i);
      Tile& t = tiles[(y * TY / na.h) * TX + x * TX / na.w];
      t.n += 1; t.nat += E * ao; t.ref += k * pfar + e; t.refs += k * pfar;
    }
  auto M = [&](double v) { return v / cnt; };
  std::vector<std::string> lines;
  char b[1024];
  auto add = [&](const char* fmt, auto... v) { std::snprintf(b, sizeof b, fmt, v...); lines.push_back(b); std::printf("  %s\n", b); };
  const rp::LightingInputs li = rp::lighting_inputs(m);
  add("native ambient: %s; %ux%u GI pixels, %.0f compared (%s); lighting inputs %u images, %u probe cubes, probe records %s, LUT sun %s, sky cube %s",
      params.c_str(), na.w, na.h, cnt, have_live ? "live giRawDepth > 0" : "no live reference: every shaded pixel", li.images, li.probe_cubes,
      li.probes ? "yes" : "no", li.sun_lut ? "yes" : "no", li.sky ? "yes" : "no");
  add("native ambient means (luminance, the giRaw.rgb quantity, game units): E_native %.6g (sky irradiance only %.6g), x game AO %.6g (AO %.4f), "
      "x game AO x live a %.6g (live a %.4f); froxels listing a local probe %.1f%%; as lit (x diffuse colour x AO) %.6g, specular ambient %.6g",
      M(S.E), M(S.sky), M(S.Eao), M(S.ao), M(S.Eaoa), M(S.a), 100.0 * M(S.probe), M(S.lit), M(S.spec));
  add("PT far field (total minus live domain, first hit beyond gi_rt_radius %.3g m or faded): %.6g PT units (first hits beyond it on screen %.6g, off screen %.6g); "
      "k = %.3f; k x PT far %.6g; PT environment through misses %.6g (first vertex %.6g; first-ray miss fraction %.4f)%s",
      radius, M(S.pt_far), M(S.far_on), M(S.off), k, k * M(S.pt_far), M(S.env), M(S.env1), M(S.miss), env ? "" : " (PT_ENV off: no sky cube in the pack)");
  // The traced bounce's split over the same pixels (pt_report's definitions): first hits beyond gi_rt_radius
  // on screen (visible / hidden), off screen (not split by distance), the live domain; sun / local lights.
  auto share = [&](double x) { return S.tot > 0 ? 100.0 * x / S.tot : 0.0; };
  add("PT split (compared pixels, %% of PT total): far field on screen beyond gi_rt_radius %.1f%% (visible %.1f, hidden %.1f), off screen %.1f%%, "
      "live domain %.1f%%; sun %.1f%%, local point/spot %.1f%%",
      share(S.vis_far + S.hid_far), share(S.vis_far), share(S.hid_far), share(S.off), share(S.live), share(S.sun), share(S.local));
  const double ref = k * M(S.pt_far) + M(S.env), refs = k * M(S.pt_far);
  add("native vs PT far field: E_native x AO / (k x PT far) %.3f, E_native x AO / (k x PT far + PT env) %.3f, E_native / (k x PT far + PT env) %.3f, "
      "E_native x AO x live a / (k x PT far + PT env) %.3f",
      refs > 0 ? M(S.Eao) / refs : 0.0, ref > 0 ? M(S.Eao) / ref : 0.0, ref > 0 ? M(S.E) / ref : 0.0, ref > 0 ? M(S.Eaoa) / ref : 0.0);
  std::vector<double> r_env, r_far;
  double abs_err = 0, sum_ref = 0;
  std::ofstream csv(out / "native_ambient_tiles.csv");
  csv << "tx,ty,pixels,native_x_ao,k_pt_far_plus_env,k_pt_far\n";
  for (int ty = 0; ty < TY; ++ty)
    for (int tx = 0; tx < TX; ++tx) {
      Tile& t = tiles[ty * TX + tx];
      if (t.n < 1) continue;
      t.nat /= t.n; t.ref /= t.n; t.refs /= t.n;
      csv << tx << "," << ty << "," << t.n << "," << t.nat << "," << t.ref << "," << t.refs << "\n";
      if (t.ref > 0) r_env.push_back(t.nat / t.ref);
      if (t.refs > 0) r_far.push_back(t.nat / t.refs);
      abs_err += t.n * std::abs(t.nat - t.ref); sum_ref += t.n * t.ref;
    }
  auto q = [](std::vector<double> v, double p) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[size_t(p * (v.size() - 1))]; };
  add("per tile (16 x 9, %zu with pixels): E_native x AO / (k x PT far + PT env) p10 %.3f median %.3f p90 %.3f, pixel-weighted mean |native - ref| / ref %.3f; "
      "/ (k x PT far) p10 %.3f median %.3f p90 %.3f",
      r_env.size(), q(r_env, 0.1), q(r_env, 0.5), q(r_env, 0.9), sum_ref > 0 ? abs_err / sum_ref : 0.0, q(r_far, 0.1), q(r_far, 0.5), q(r_far, 0.9));
  add("sun transmittance: game LUT %.4f %.4f %.4f (atmosphere path on %.0f%% of pixels), Kasten-Young estimate %.4f %.4f %.4f; cloud shadow mean %.4f",
      M(S.t[0]), M(S.t[1]), M(S.t[2]), 100.0 * M(S.atmo), est_t[0], est_t[1], est_t[2], M(S.cloud));
  { std::ofstream f(out / "native_ambient_report.txt"); for (auto& l : lines) f << l << "\n"; }
  write_pfm_rgb(out / "native_ambient.pfm", na.w, na.h, [&](size_t i, int c) { return float(na.mean(0, c, i) * na.mean(0, 3, i)); });
  write_pfm_rgb(out / "pt_far_field.pfm", na.w, na.h, [&](size_t i, int c) { return float(far_rgb(i, c)); });
  // Per-pixel terms for offline comparisons (the gi_far emulation, docs/TRACK_C_GI.md): E_native (diffuse colour
  // 1), the as-lit ambient (x diffuse colour x AO), the game AO; PT total / live domain / environment (PT units,
  // uncalibrated), the screen radiance at visible first hits and the PT's radiance there (luminance, as k).
  write_pfm_rgb(out / "native_E.pfm", na.w, na.h, [&](size_t i, int c) { return float(na.mean(0, c, i)); });
  write_pfm_rgb(out / "native_lit.pfm", na.w, na.h, [&](size_t i, int c) { return float(na.mean(1, c, i)); });
  write_pfm_rgb(out / "native_ao.pfm", na.w, na.h, [&](size_t i, int) { return float(na.mean(0, 3, i)); });
  write_pfm_rgb(out / "pt_total_rgb.pfm", pt.w, pt.h, [&](size_t i, int c) { return float(pt.mean(0, c, i)); });
  write_pfm_rgb(out / "pt_live_rgb.pfm", pt.w, pt.h, [&](size_t i, int c) { return float(pt.mean(3, c, i)); });
  if (env) write_pfm_rgb(out / "pt_env_rgb.pfm", pt.w, pt.h, [&](size_t i, int c) { return float(pt.mean(7, c, i)); });
  write_pfm_rgb(out / "pt_k_terms.pfm", pt.w, pt.h, [&](size_t i, int c) {
    return float(c == 0 ? L(pt, 2, i) : c == 1 ? pt.mean(0, 4, i) + pt.mean(1, 4, i) : 0.0); });
  const uint32_t sw = std::max(1u, na.w / 640), W = na.w / sw, H = na.h / sw;
  std::vector<uint8_t> strip(size_t(W) * 3 * H * 3);
  const double exposure = M(S.Eao) > 0 ? 0.5 / M(S.Eao) : 1.0;
  auto tone = [&](double v) { v = std::max(0.0, v * exposure); v = v / (1 + v); v = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055; return uint8_t(std::clamp(v, 0.0, 1.0) * 255 + 0.5); };
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      const size_t i = size_t(y * sw) * na.w + x * sw;
      for (int c = 0; c < 3; ++c) {
        strip[(size_t(y) * W * 3 + x) * 3 + c] = tone(na.mean(0, c, i) * na.mean(0, 3, i));
        strip[(size_t(y) * W * 3 + W + x) * 3 + c] = tone(far_rgb(i, c));
        strip[(size_t(y) * W * 3 + 2 * W + x) * 3 + c] = tone(na.mean(0, c, i));
      }
    }
  write_bmp(out / "native_compare.bmp", W * 3, H, strip);
}

// CTest pt_reference_test: the furnace (closed box, every vertex emits Le = 1, Lambertian albedo rho):
// mean incident radiance = sum_{k<B} rho^k exactly for B vertices (each Lambert sample's f cos / pdf
// is rho; every ray hits), Le / (1 - rho) with Russian roulette (unbiased; checked within 4 standard
// errors), a FresnelBlend white furnace bounded by B (energy conservation), closure (miss 0), and on
// the slab pack: native hit material + point light lit, dark control exactly 0, deterministic.
int pt_self_test(const fs::path& dir) {
  if (int r = init_gpu(true, true)) return r;
  int failures = 0;
  auto line = [&](const char* name, bool ok, const std::string& detail) {
    if (!ok) ++failures;
    std::printf("%-62s %s  %s\n", name, ok ? "PASS" : "FAIL", detail.c_str());
  };
  synthesize(dir / "box", true, true);
  synthesize(dir / "casters", true, true, false, true);
  synthesize(dir / "lit", false);
  synthesize(dir / "dark", true);
  struct Stat { double mean = 0, se = 0, miss = 0, lo = 1e30, hi = -1e30; };
  auto stat = [](const PtAccum& a, uint32_t c) {
    Stat s; const size_t n = size_t(a.w) * a.h; double sq = 0;
    for (size_t i = 0; i < n; ++i) { const double v = a.mean(0, c, i); s.mean += v; sq += v * v; s.miss += a.mean(0, 3, i); s.lo = std::min(s.lo, v); s.hi = std::max(s.hi, v); }
    s.mean /= double(n); s.miss /= double(n);
    s.se = std::sqrt(std::max(0.0, sq / double(n) - s.mean * s.mean) / double(n));
    return s;
  };
  auto fmt = [](const Stat& s, double want) { char t[160]; std::snprintf(t, sizeof t, "mean %.6f (expected %.6f, SE %.2g), pixels %.6f..%.6f, miss %.4f", s.mean, want, s.se, s.lo, s.hi, s.miss); return std::string(t); };
  const std::vector<std::string> furnace = {"PT_FURNACE_LE=1.0", "PT_LAMBERT=1", "PT_LIGHTS=0", "PT_SUN=0", "PT_SPP=4"};
  // fp16 output: a value near 2 has a step of 2^-10; the deterministic cases are within 1e-3 relative.
  {
    auto d = furnace; d.push_back("PT_ALBEDO=0.5"); d.push_back("PT_BOUNCES=1"); d.push_back("PT_RR_START=99");
    const Stat s = stat(pt_run(dir / "box", d, 1, 1, dir / "box-out"), 0);
    line("1 furnace B=1: every ray hits an emitter of Le 1", std::abs(s.lo - 1) < 1e-3 && std::abs(s.hi - 1) < 1e-3 && s.miss == 0, fmt(s, 1));
  }
  {
    auto d = furnace; d.push_back("PT_ALBEDO=0.5"); d.push_back("PT_BOUNCES=3"); d.push_back("PT_RR_START=99");
    const Stat s = stat(pt_run(dir / "box", d, 1, 1, dir / "box-out"), 0);
    line("2 furnace B=3 rho 0.5: 1 + rho + rho^2 = 1.75 per pixel", std::abs(s.lo - 1.75) < 2e-3 && std::abs(s.hi - 1.75) < 2e-3, fmt(s, 1.75));
  }
  PtAccum rr;
  {
    auto d = furnace; d.push_back("PT_ALBEDO=0.8"); d.push_back("PT_BOUNCES=96"); d.push_back("PT_RR_START=2");
    rr = pt_run(dir / "box", d, 8, 1, dir / "box-out");
    const Stat s = stat(rr, 0);
    const double want = (1 - std::pow(0.8, 96)) / (1 - 0.8);
    line("3 furnace rho 0.8, Russian roulette: Le / (1 - rho) = 5 (4 SE)", std::abs(s.mean - want) < 4 * s.se + 5e-3 && s.se > 0, fmt(s, want));
  }
  {
    // FresnelBlend (the reference's BSDF), white substrate Rd = 1, F0 0.04, 4 vertices: each
    // vertex's directional albedo <= 1, so 1 <= L <= 4 (energy conservation), and > 1 + 0.5 x 3.
    std::vector<std::string> d = {"PT_FURNACE_LE=1.0", "PT_LIGHTS=0", "PT_SUN=0", "PT_SPP=4", "PT_ALBEDO=1.0", "PT_BOUNCES=4", "PT_RR_START=99"};
    const Stat s = stat(pt_run(dir / "box", d, 8, 1, dir / "box-out"), 0);
    line("4 FresnelBlend white furnace, 4 vertices: 2.5 < L <= 4 (no gain)", s.mean > 2.5 && s.mean <= 4 + 4 * s.se, fmt(s, 4));
  }
  {
    std::vector<std::string> d = {"PT_SPP=4", "PT_BOUNCES=4"};
    const PtAccum lit = pt_run(dir / "lit", d, 2, kPtModes, dir / "lit-out");
    const PtAccum lit2 = pt_run(dir / "lit", d, 2, kPtModes, dir / "lit-out");
    const PtAccum dark = pt_run(dir / "dark", d, 2, kPtModes, dir / "dark-out");
    const Stat sl = stat(lit, 0), sd = stat(dark, 0);
    double qual = 0; const size_t n = size_t(lit.w) * lit.h;
    for (size_t i = 0; i < n; ++i) qual += lit.mean(2, 3, i);
    line("5 slab: native hit material qualifies, point light lights the PT", sl.mean > 0 && qual > 0 && sl.miss > 0, fmt(sl, sl.mean) + ", qualified sum " + std::to_string(qual));
    line("6 slab dark control (light colour 0): PT exactly 0", sd.lo == 0 && sd.hi == 0, fmt(sd, 0));
    bool same = true;
    for (uint32_t k = 0; k < kPtModes; ++k) same &= lit.rgba[k] == lit2.rgba[k] && lit.aux[k] == lit2.aux[k];
    line("7 deterministic: two PT runs bit-identical (all modes)", same, "");
  }
  {
    // rt_cascade_casters (the "casters" directive): the box's floor is only in the cascade instance (mask 0x02,
    // moved 1 m down by its transform). PT_CASTERS=1 traces it: the box is closed again, exactly as 1 and 2
    // (its vertices continue the paths: the replay appended them to the soup). Without PT_CASTERS the pack's
    // variant (RT_CASCADE_MASKS) traces 0x01, as the live GI rays: the floor is open.
    auto d = furnace; d.push_back("PT_ALBEDO=0.5"); d.push_back("PT_BOUNCES=1"); d.push_back("PT_RR_START=99");
    auto c = d; c.push_back("PT_CASTERS=1");
    const Stat s1 = stat(pt_run(dir / "casters", c, 1, 1, dir / "casters-out"), 0);
    line("8 casters: PT_CASTERS closes the box (B=1: every ray hits Le 1)", std::abs(s1.lo - 1) < 1e-3 && std::abs(s1.hi - 1) < 1e-3 && s1.miss == 0, fmt(s1, 1));
    auto c3 = furnace; c3.push_back("PT_ALBEDO=0.5"); c3.push_back("PT_BOUNCES=3"); c3.push_back("PT_RR_START=99"); c3.push_back("PT_CASTERS=1");
    // The near rule off (RT_CASCADE_TMIN 0.5 m skips the floor from wall points within 0.5 m of it: -0.6% here).
    c3.push_back("RT_CASCADE_TMIN=0.0"); c3.push_back("RT_CASCADE_TMIN_REL=0.0");
    const Stat s3 = stat(pt_run(dir / "casters", c3, 1, 1, dir / "casters-out"), 0);
    line("9 casters: paths continue off the caster face (B=3, near rule off: 1.75)", std::abs(s3.lo - 1.75) < 2e-3 && std::abs(s3.hi - 1.75) < 2e-3, fmt(s3, 1.75));
    const Stat s0 = stat(pt_run(dir / "casters", d, 1, 1, dir / "casters-out"), 0);
    line("10 casters: mask 0x01 without PT_CASTERS (the live GI rays): floor open", s0.miss > 0.05 && s0.mean < 1 - s0.miss + 1e-3, fmt(s0, 1 - s0.miss));
  }
  line("11 no validation errors", g_validation_errors.load() == 0, std::to_string(g_validation_errors.load()) + " errors");
  std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}

int ls_self_test(const fs::path& dir) {
  if (int r = init_gpu(true,true)) return r;
  const fs::path pack = dir / "pack";
  synthesize(pack,false);
  rp::Manifest m; std::string error;
  { const auto bytes = read_file(pack / "manifest.txt"); check(rp::parse(std::string(bytes.begin(),bytes.end()),m,error),error); }
  m.pass = "local_shadows";
  m.defines = "RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1";
  m.specs = {{1000,1},{1008,0},{1009,0},{1010,0},{1020,0},{1026,0}};
  check(ls_masks_casters(m), "LS captured cascade mask excludes casters");
  {auto rf=m;rf.pass="reflections";check(ls_masks_casters(rf),"default RF preserves soup range when casters are masked");}
  g_specs[1000u] = 0;
  check(!ls_masks_casters(m), "LS common override retains casters");
  g_ab_specs[1000u] = {1,0};
  check(!ls_masks_casters(m), "LS either paired ray mask retains casters");
  g_ab_specs[1000u] = {1,1};
  check(ls_masks_casters(m), "LS paired overrides supersede common value");
  g_ab_specs.clear(); g_specs.clear();
  auto mask_control = m;
  mask_control.pass = "gi_hit_direct";
  check(!ls_masks_casters(mask_control), "non-LS replay retains casters");
  mask_control = m; mask_control.defines.clear();
  check(!ls_masks_casters(mask_control), "unspecialized LS retains casters");
  mask_control = m; mask_control.specs.clear();
  check(!ls_masks_casters(mask_control), "default cascade specialization retains casters");
  const auto& pass = *rp::find_pass(m.pass);
  m.buffers.erase(std::remove_if(m.buffers.begin(),m.buffers.end(),[&](const auto& b){return !rp::find_slot(pass,b.set,b.binding);}),m.buffers.end());
  m.images.erase(std::remove_if(m.images.begin(),m.images.end(),[&](const auto& i){return !rp::find_slot(pass,i.set,i.binding);}),m.images.end());
  auto push_bytes = read_file(pack / m.push);
  Push push{}; std::memcpy(&push,push_bytes.data(),sizeof push);
  push.ls_strength = 1; push.ls_shadowed = 3; push.ls_clear = 0.2f;
  push.ls_flags = 31u << 8; push.ls_kmode = 1; push.ao_direct = 0; push.rt_stats = 1;
  write_file(pack / m.push,&push,sizeof push);
  auto light_bytes = read_file(pack / "llights.bin");
  Light light{}; std::memcpy(&light,light_bytes.data(),sizeof light);light.pos[1]=3.5f;
  write_file(pack / "llights.bin",&light,sizeof light);
  const uint32_t W = push.full[0], H = push.full[1];
  m.groups[0] = (W+7)/8; m.groups[1] = (H+7)/8;
  auto flags=read_file(pack / "lflags.bin");std::memcpy(flags.data()+16,&W,4);std::memcpy(flags.data()+20,&H,4);write_vec(pack / "lflags.bin",flags);
  const uint32_t cluster[4]={0,1,1,0};write_file(pack / "lclusters.bin",cluster,sizeof cluster);
  auto constants=read_file(pack / "lconsts.bin");const float atlas_constants[4]={0.25f,0.25f,1,0};
  std::memcpy(constants.data()+31*16,&W,4); // one screen-sized froxel for direct LS pixel coordinates
  std::memcpy(constants.data()+33*16,atlas_constants,16);write_vec(pack / "lconsts.bin",constants);
  const uint32_t colour = (15u << 6) | ((15u << 6) << 11) | ((15u << 5) << 22); // RGB 1 exactly
  std::vector<uint32_t> initial(size_t(W)*H,colour);
  write_vec(pack / "colour.bin",initial);
  std::vector<uint16_t> auxiliary(size_t(W)*H*4,0); for(size_t i=0;i<auxiliary.size();i+=4) auxiliary[i]=0x3c00;
  write_vec(pack / "aux.bin",auxiliary);
  m.outputs = {{0,6,122,W,H,"","colour.bin"},{0,7,122,W,H,"","colour.bin"},{0,44,97,W,H,"","aux.bin"}};
  auto image = [&](uint32_t b,uint32_t format,const char* name,const std::vector<uint8_t>& bytes) {
    write_vec(pack / name,bytes); m.images.push_back({0,b,0,format,W,H,1,1,name});
  };
  std::vector<float> albedo(size_t(W)*H*4,0.5f), material(size_t(W)*H*4,0);
  for(size_t i=0;i<material.size();i+=4){material[i]=0.5f;material[i+1]=0.5f;}
  auto raw = [](const auto& v){const auto* b=reinterpret_cast<const uint8_t*>(v.data());return std::vector<uint8_t>(b,b+v.size()*sizeof(v[0]));};
  image(3,109,"ls-albedo.bin",raw(albedo)); image(4,109,"ls-material.bin",raw(material));
  image(12,64,"ls-ao.bin",raw(std::vector<uint32_t>(size_t(W)*H,0xffffffffu)));
  image(16,9,"ls-mask.bin",std::vector<uint8_t>(size_t(W)*H,255));
  float shadow[24]{};shadow[11]=0.5f;shadow[15]=1;shadow[18]=1;shadow[21]=1;shadow[23]=1;
  write_file(pack / "shadow.bin",shadow,sizeof shadow); m.buffers.push_back({0,27,sizeof shadow,"shadow.bin"});
  const std::vector<uint8_t> atlas{255,255,0,0,0,0,0,0}; // one raw BC4 block, depth 1
  write_vec(pack / "atlas.bin",atlas); m.images.push_back({0,28,0,139,4,4,1,1,"atlas.bin"});
  std::vector<uint8_t> projector(128,0);
  auto pf=[&](size_t offset,float value){std::memcpy(projector.data()+offset,&value,4);};
  auto pu=[&](size_t offset,uint32_t value){std::memcpy(projector.data()+offset,&value,4);};
  pf(0,0.5f);pf(4,0.5f);pf(8,0.5f);pu(12,16); // cookie element 7
  pf(16,0);pf(20,1);pf(32,4);pf(36,4);pu(40,1);pf(44,1);
  pf(48,0.05f);pf(60,0.5f);pf(68,0.05f);pf(76,0.25f);pf(88,0.05f);pf(92,0.5f);
  pu(108,1);pf(112,0.6f);pf(116,-0.8f); // oblique light: the visible slab shadows adjacent back-plane pixels
  write_vec(pack / "projector.bin",projector); m.buffers.push_back({3,29,128,"projector.bin"});
  const uint8_t cookie[16]={128,128,0,0,0,0,0,0,192,192,0,0,0,0,0,0}; // raw BC5
  write_file(pack / "cookie.bin",cookie,sizeof cookie);
  m.images.push_back({3,45,7,141,4,4,1,1,"cookie.bin"});
  m.samplers.push_back({3,16,0,1,1,1,2,2,2,0,1000});
  std::vector<uint8_t> counters(kCounters*64*4,0); counters.back()=17;
  write_vec(pack / "initial-counters.bin",counters); m.counter_initial="initial-counters.bin";
  // Exercise codec integration through both ordinary file reads and staged image upload.
  auto encoded_file = [&](const char* name,const std::vector<uint8_t>& bytes) {
    std::ofstream output(pack / name,std::ios::binary);rp::codec::Writer writer(output,bytes.size());
    check(writer.write(bytes.data(),bytes.size()) && writer.finish(),"LS fixture compressed file");
  };
  encoded_file("colour.bin",raw(initial));encoded_file("initial-counters.bin",counters);encoded_file("atlas.bin",atlas);
  encoded_file("cookie.bin",std::vector<uint8_t>(cookie,cookie+sizeof cookie));
  auto save = [&]{ const auto text=rp::serialize(m);write_file(pack / "manifest.txt",text.data(),text.size()); };
  save();
  rp::Manifest roundtrip; check(rp::parse(rp::serialize(m),roundtrip,error),error);
  check(roundtrip.specs.size()==m.specs.size() && roundtrip.outputs[0].initial=="colour.bin" && roundtrip.counter_initial==m.counter_initial,"LS manifest round trip");
  const auto a = replay(pack,nullptr,{}, {},dir / "first",2,true);
  check(a.runs[0].raw_stats == a.runs[1].raw_stats,"LS repeat counters");
  check(a.runs[0].stats.t[118]==0 && a.runs[0].stats.t[119]==0 && a.runs[0].stats.t[120]==0,"LS synthetic input ranges and rays valid");
  bool changed = false;
  for(size_t i=0;i<a.runs[0].outputs.size();++i) {
    const auto& x=a.runs[0].outputs[i];
    check(x.raw==a.runs[1].outputs[i].raw,"LS mutable images restored every repeat");
    check(x.nonfinite==0,"LS finite packed outputs");
    if(i<2) changed |= x.raw != raw(initial);
    m.outputs[i].reference="reference"+std::to_string(i)+".bin";write_vec(pack / m.outputs[i].reference,x.raw);
  }
  check(changed,"LS fixture exercises colour modification");
  const auto spv_bytes=read_file(dir / "first" / "pass.spv");std::vector<uint32_t> code(spv_bytes.size()/4);
  std::memcpy(code.data(),spv_bytes.data(),spv_bytes.size());
  const uint8_t black[16]={};write_file(pack / "cookie.bin",black,sizeof black);
  const auto no_cookie=replay(pack,&code,{}, {},dir / "black-cookie",1,true);
  write_file(pack / "cookie.bin",cookie,sizeof cookie);
  check(no_cookie.runs[0].outputs[0].raw != a.runs[0].outputs[0].raw,"LS reads projector cookie element 7");
  write_vec(pack / "atlas.bin",std::vector<uint8_t>(8,0));
  const auto no_atlas=replay(pack,&code,{}, {},dir / "dark-atlas",1,true);
  write_vec(pack / "atlas.bin",atlas);
  check(no_atlas.runs[0].outputs[0].raw != a.runs[0].outputs[0].raw,"LS reads shadow-view and atlas PCF");
  m.counters="reference-counters.bin";write_vec(pack / m.counters,a.runs[0].raw_stats);save();
  g_ab_specs[2147483647u]={0,1}; g_ab_require_equal=true;
  replay(pack,&code,{}, {},dir / "paired",2,true);
  check(!g_reference_mismatch,"LS paired restoration bit-equal");
  g_ab_warmup = 2; g_reference_out = dir / "paired";
  replay(pack,&code,{}, {},dir / "paired-warmed",2,true);
  check(!g_reference_mismatch,"LS per-sample warmup preserves baseline outputs and counters");
  g_ab_warmup = 0; g_reference_out.clear();
  g_ab_specs.clear();g_ab_require_equal=false;
  uint64_t disk_bytes=0;
  for(const auto& f:fs::directory_iterator(pack)) if(f.is_regular_file()) disk_bytes+=f.file_size();
  check(disk_bytes<=rp::kPackHardCapBytes,"LS synthetic pack fits 2048 MiB disk cap");
  std::printf("LS synthetic pack disk: %llu bytes (cap %llu)\n",(unsigned long long)disk_bytes,(unsigned long long)rp::kPackHardCapBytes);
  std::puts("LS synthetic pack: production Ultra shader, packed colour restoration, auxiliary, raw BC4 atlas/BC5 cookie, specs and counters PASS");
  return g_validation_errors.load() ? 1 : 0;
}

// Default RF production request, with a tilted slab reflecting the visible back
// plane. A nonzero history source makes both packed colour and hit distance observable.
int rf_self_test(const fs::path& dir) {
  if(int r=init_gpu(true,true))return r;
  const auto pack=dir/"pack";synthesize(pack,false,false,true);
  rp::Manifest m;std::string error;
  {const auto b=read_file(pack/"manifest.txt");check(rp::parse(std::string(b.begin(),b.end()),m,error),error);}
  m.pass="reflections";
  m.defines="RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1";
  m.specs={{1000,1},{1008,0},{1009,0},{1010,0},{1020,0},{1026,0}};
  const auto& pass=*rp::find_pass(m.pass);
  // G-buffer albedo/material are pushed at set 0 by production RF.
  for(auto& i:m.images)if(i.set==2 && (i.binding==2 || i.binding==3)){i.set=0;++i.binding;}
  auto valid=[&](const auto& x){return rp::find_slot(pass,x.set,x.binding)!=nullptr;};
  std::erase_if(m.buffers,[&](const auto& b){return !valid(b);});
  std::erase_if(m.images,[&](const auto& i){return !valid(i);});
  std::erase_if(m.samplers,[&](const auto& i){return !valid(i);});
  auto pb=read_file(pack/m.push);Push push{};std::memcpy(&push,pb.data(),sizeof push);
  push.rf_max_rough=0.8f;push.rf_mirror_rough=0.1f;push.rf_tmax=100;
  push.rf_depth_tol=1;push.rf_edge=0.05f;push.rf_strength=1;push.rf_sample=1;
  push.rf_models=3;push.rf_stats=1;push.res_shift=0;
  write_file(pack/m.push,&push,sizeof push);
  const uint32_t W=push.full[0],H=push.full[1];m.groups[0]=(W+7)/8;m.groups[1]=(H+7)/8;
  auto flags=read_file(pack/"lflags.bin");std::memcpy(flags.data()+16,&W,4);std::memcpy(flags.data()+20,&H,4);write_vec(pack/"lflags.bin",flags);
  // Use a specular local probe pair as well as the global raw BC1 cube.
  auto probes=read_file(pack/"probes.bin");uint32_t mask=4,pair=1;
  std::memcpy(probes.data()+136,&mask,4);std::memcpy(probes.data()+140,&pair,4);write_vec(pack/"probes.bin",probes);
  std::vector<uint8_t> cube;
  const uint8_t block[8]={0,248,0,248,0,0,0,0};
  for(int f=0;f<6;++f)cube.insert(cube.end(),block,block+8);
  write_vec(pack/"raw-bc1.cube",cube);
  for(auto& i:m.images)if(i.set==2 && (i.binding==35 || i.binding==37 || (i.binding==45 && i.element==0)))
    {i.format=131;i.w=i.h=4;i.file="raw-bc1.cube";}
  // A sampler state regression that uniform one-mip probes cannot expose:
  // SampleLevel(0) of DFG must clamp to the captured minLod, not invented zero.
  std::vector<float> mip_dfg;
  for(int i=0;i<4;++i)mip_dfg.insert(mip_dfg.end(),{1.0f,0.0f,0.0f,1.0f});
  mip_dfg.insert(mip_dfg.end(),{0.25f,0.125f,0.0f,1.0f});
  write_vec(pack/"mip-dfg.bin",mip_dfg);
  for(auto& i:m.images)if(i.set==2 && i.binding==38)
    {i.format=rp::kRgba32Sfloat;i.w=i.h=2;i.mips=2;i.file="mip-dfg.bin";}
  for(auto& s:m.samplers)if(s.set==2 && s.binding==32)
    {s.state_version=1;s.min_lod=1.0f;s.lod_bias=-0.375f;}
  auto raw=[](const auto& v){const auto* b=reinterpret_cast<const uint8_t*>(v.data());return std::vector<uint8_t>(b,b+v.size()*sizeof(v[0]));};
  auto normal=read_file(pack/"normal.rg32f");auto depth=read_file(pack/"depth.r32f");
  for(size_t i=0;i<size_t(W)*H;++i){float d;std::memcpy(&d,depth.data()+i*4,4);if(d<4.4f){const float rg[2]={(0.5f+1.7777f)/3.5554f,0.5f};std::memcpy(normal.data()+i*8,rg,8);}}
  write_vec(pack/"normal.rg32f",normal);
  // Nonzero previous colour; GGX paths include visible, hidden, offscreen and miss.
  std::vector<uint16_t> history(size_t(W)*H*4,0x4400);write_vec(pack/"prev.rgba16f",history);
  std::vector<uint32_t> colour(size_t(W)*H,(15u<<6)|((15u<<6)<<11)|((15u<<5)<<22));
  std::vector<uint16_t> hit(size_t(W)*H,0x3c00);
  write_vec(pack/"colour.bin",colour);write_vec(pack/"hit.bin",hit);
  m.outputs={{0,6,122,W,H,"","colour.bin"},{0,7,122,W,H,"","colour.bin"},{0,30,76,W,H,"","hit.bin"}};
  std::vector<uint8_t> initial(kCounters*64*4,0);initial.back()=17;
  write_vec(pack/"initial-counters.bin",initial);m.counter_initial="initial-counters.bin";
  auto save=[&]{auto text=rp::serialize(m);write_file(pack/"manifest.txt",text.data(),text.size());};save();
  rp::Manifest back;check(rp::parse(rp::serialize(m),back,error),error);
  check(back.specs.size()==m.specs.size() && back.outputs[2].initial=="hit.bin","RF manifest round trip");
  const auto a=replay(pack,nullptr,{}, {},dir/"first",2,true);
  check(a.runs[0].stats.t[49]>0 && a.runs[0].stats.t[50]>0,"RF eligible rays and screen hits");
  check(a.runs[0].stats.t[118]==0 && a.runs[0].stats.t[119]==0 && a.runs[0].stats.t[120]==0,"RF guards zero");
  check(a.runs[0].raw_stats==a.runs[1].raw_stats,"RF repeat raw counters");
  check(a.runs[0].outputs[1].raw!=raw(colour),"RF modifies packed specular");
  check(a.runs[0].outputs[2].raw!=raw(hit),"RF writes R16 hit distance");
  for(size_t i=0;i<m.outputs.size();++i){const auto& x=a.runs[0].outputs[i];
    check(x.raw==a.runs[1].outputs[i].raw && x.nonfinite==0,"RF repeat identity and finite outputs");
    m.outputs[i].reference="reference"+std::to_string(i)+".bin";write_vec(pack/m.outputs[i].reference,x.raw);}
  auto bytes=read_file(dir/"first"/"pass.spv");std::vector<uint32_t> code(bytes.size()/4);std::memcpy(code.data(),bytes.data(),bytes.size());
  for(auto& s:m.samplers)if(s.set==2 && s.binding==32)s.min_lod=0.0f;
  save();
  const auto zero_lod=replay(pack,&code,{}, {},dir/"zero-min-lod",1,true);
  check(zero_lod.runs[0].outputs[1].raw!=a.runs[0].outputs[1].raw,"RF captured minLod changes nonuniform-mip DFG sampling");
  for(auto& s:m.samplers)if(s.set==2 && s.binding==32)s.min_lod=1.0f;
  save();
  const auto zero_history=std::vector<uint16_t>(history.size(),0);write_vec(pack/"prev.rgba16f",zero_history);
  const auto dark=replay(pack,&code,{}, {},dir/"dark-history",1,true);write_vec(pack/"prev.rgba16f",history);
  check(dark.runs[0].outputs[1].raw!=a.runs[0].outputs[1].raw,"RF reads previous radiance");
  // Change only the probe cube: makes the replacement term's subtraction observable.
  write_vec(pack/"raw-bc1.cube",std::vector<uint8_t>(cube.size(),0));
  const auto black=replay(pack,&code,{}, {},dir/"black-probe",1,true);write_vec(pack/"raw-bc1.cube",cube);
  check(black.runs[0].outputs[1].raw!=a.runs[0].outputs[1].raw,"RF reads raw BCn probe cubes");
  m.counters="reference-counters.bin";write_vec(pack/m.counters,a.runs[0].raw_stats);save();
  // Exercise the same bounded lossless codec the live two-phase writer uses.
  for(const char* name:{"colour.bin","hit.bin","raw-bc1.cube","initial-counters.bin"}){
    const auto data=read_file(pack/name);std::ofstream output(pack/name,std::ios::binary);
    rp::codec::Writer writer(output,data.size());check(writer.write(data.data(),data.size()) && writer.finish(),"RF compressed fixture");}
  g_ab_specs[2147483647u]={0,1};g_ab_require_equal=true;g_ab_warmup=2;g_require_live_equal=true;
  replay(pack,&code,{}, {},dir/"paired",2,true);
  check(!g_reference_mismatch,"RF paired output/counter restoration");
  g_ab_specs.clear();g_ab_require_equal=false;g_ab_warmup=0;
  // A printed difference is not a validation gate unless the process fails.
  auto changed=a.runs[0].outputs[1].raw;changed[0]^=1u;
  write_vec(pack/m.outputs[1].reference,changed);
  replay(pack,&code,{}, {},dir/"bad-live-output",1,true);
  check(g_reference_mismatch,"RF required live gate rejects an output bit mismatch");
  write_vec(pack/m.outputs[1].reference,a.runs[0].outputs[1].raw);g_reference_mismatch=false;
  changed=a.runs[0].raw_stats;changed[0]^=1u;write_vec(pack/m.counters,changed);
  replay(pack,&code,{}, {},dir/"bad-live-counters",1,true);
  check(g_reference_mismatch,"RF required live gate rejects a counter bit mismatch");
  write_vec(pack/m.counters,a.runs[0].raw_stats);g_reference_mismatch=false;
  const auto counter_reference=m.counters,output_reference=m.outputs[1].reference;
  m.counters.clear();m.outputs[1].reference.clear();save();
  replay(pack,&code,{}, {},dir/"missing-live-reference",1,true);
  check(g_reference_mismatch,"RF required live gate rejects missing references");
  m.counters=counter_reference;m.outputs[1].reference=output_reference;save();
  g_require_live_equal=false;g_reference_mismatch=false;
  std::puts("RF synthetic pack: default production shader, packed colours, R16 hit distance, raw BCn probes, repeat and paired equality PASS");
  return g_validation_errors.load()?1:0;
}

int self_test(const fs::path& dir) {
  if (int r = init_gpu(true, true)) return r;
  int failures = 0;
  auto line = [&](const char* name, bool ok, const std::string& detail) {
    if (!ok) ++failures;
    std::printf("%-58s %s  %s\n", name, ok ? "PASS" : "FAIL", detail.c_str());
  };
  synthesize(dir / "lit", false);
  synthesize(dir / "dark", true);
  const fs::path spv = dir / "gi_trace_rt_hit_direct.spv";
  const auto t0 = GetTickCount64();
  auto code = compile(fs::path(source_root()) / "shaders" / "gi" / "gi_trace_rt_hit_direct.hlsl", {}, spv);
  std::printf("compiled the production gi_hit_direct shader in %.2f s\n", (GetTickCount64() - t0) / 1000.0);
  Replay lit = replay(dir / "lit", &code, {}, {}, dir / "lit-out", 2, true);
  Replay dark = replay(dir / "dark", &code, {}, {}, dir / "dark-out", 2, true);
  const auto& a = lit.runs[0], & b = lit.runs[1], & d = dark.runs[0];
  const uint64_t* t = a.stats.t;
  line("1 load -> TLAS -> dispatch: GI rays traced and hit", t[8] > 0 && t[9] > 0, "rays " + std::to_string(t[8]) + ", hits " + std::to_string(t[9]));
  line("2 hidden hits reach rt_hit_material and qualify", t[282] > 0 && t[283] > 0 && t[253] == t[283],
       "candidates " + std::to_string(t[282]) + ", qualified " + std::to_string(t[283]));
  line("3 native_hit_direct supported and nonzero (lit)", t[284] > 0 && t[285] > 0 && t[286] > 0,
       "supported " + std::to_string(t[284]) + ", nonzero " + std::to_string(t[285]) + ", sum x8192 " + std::to_string(t[286]));
  bool same = a.outputs.size() == b.outputs.size();
  for (size_t i = 0; same && i < a.outputs.size(); ++i) same = a.outputs[i].raw == b.outputs[i].raw;
  same = same && std::memcmp(a.stats.t, b.stats.t, sizeof a.stats.t) == 0;
  line("4 deterministic: two dispatches bit-identical (images, counters)", same, "giRaw fnv1a " + [&] { char s[32]; std::snprintf(s, sizeof s, "%016llx", (unsigned long long)a.outputs[0].hash); return std::string(s); }());
  {
    const bool old_mismatch = g_reference_mismatch;
    g_ab_specs[2147483647u] = {0, 1}; // unused ID: identical pipelines are the zero-delta control
    g_ab_require_equal = true;
    replay(dir / "lit", &code, {}, {}, dir / "paired-out", 2, true);
    line("4a paired pipelines: all measured dispatches bit-identical", !g_reference_mismatch, "outputs and raw counter bins");
    g_ab_specs.clear(); g_ab_require_equal = false; g_reference_mismatch = old_mismatch;
  }
  bool finite = a.outputs[0].nonfinite == 0 && a.outputs[1].nonfinite == 0;
  { // Reference-output gate uses the same disk files and helper as --reference-out, no extra dispatch.
    const auto ref = dir / "lit-out";
    bool positive = reference_equal(ref / "rtstats.bin", b.raw_stats, 4, "counters", false);
    for (const auto& output : b.outputs) positive &= reference_equal(ref / (output.name + ".raw"), output.raw,
        output.format == VK_FORMAT_R16G16B16A16_SFLOAT ? 2 : 4, output.name, false);
    auto changed = b.outputs[0].raw; changed[0] ^= 1; // one bit must fail even if the rounded percentage is 100%.
    auto counters = b.raw_stats; counters.back() ^= 1;
    auto truncated = b.outputs[0].raw; truncated.pop_back();
    const bool negative = !reference_equal(ref / "giRaw.raw", d.outputs[0].raw, 2, "dark control", false) &&
        !reference_equal(ref / "giRaw.raw", changed, 2, "one bit", false) &&
        !reference_equal(ref / "rtstats.bin", counters, 4, "counter bit", false) &&
        !reference_equal(ref / "giRaw.raw", truncated, 2, "size", false);
    line("4b replay reference: exact match and negative controls", positive && negative, "outputs, dark control, one output/counter bit and size");
  }
  line("5 outputs finite, bounce positive where lit", finite && a.outputs[0].mean[0] > 0, "mean giRaw.r " + std::to_string(a.outputs[0].mean[0]));
  // Dark: the same rays and hits, the light of colour 0: the bounce is exactly 0, the occlusion identical.
  const auto* la = reinterpret_cast<const uint16_t*>(a.outputs[0].raw.data()), * ld = reinterpret_cast<const uint16_t*>(d.outputs[0].raw.data());
  bool zero = true, occ = d.outputs[0].raw.size() == a.outputs[0].raw.size();
  for (size_t i = 0; occ && i < d.outputs[0].raw.size() / 8; ++i) {
    zero &= ld[i * 4] == 0 && ld[i * 4 + 1] == 0 && ld[i * 4 + 2] == 0;
    occ &= ld[i * 4 + 3] == la[i * 4 + 3];
  }
  line("6 dark control: bounce exactly 0, nonzero 0", zero && d.stats.t[285] == 0 && d.stats.t[282] == t[282], "candidates " + std::to_string(d.stats.t[282]));
  line("7 occlusion (giRaw.a) independent of the light", occ, "lit vs dark alpha bit-equal");
  const ReadCheck rc = check_shader_reads(dir);
  line("9 image read: uncompressed texels keep their bits", rc.formats > 0 && rc.exact_fail == 0, std::to_string(rc.exact_fail) + " formats differ");
  line("10 image read: stored texels within half a step (BCn too)", rc.formats > 0 && rc.tol_fail == 0, rc.detail);
  line("11 image read: piece plan does not change the bytes", rc.piece_fail == 0, std::to_string(rc.piece_fail) + " formats differ");
  {   // 12: the staged upload of large pack images (a 2 GB VT cache): a staging this small forces
      // block rows of one layer, layer groups and whole mips; from memory and streamed from a file,
      // every mip and layer reads back bit-equal.
    struct Case { uint32_t f, w, h, layers, mips; uint64_t stage; };
    const Case cases[] = {{138, 37, 29, 3, 3, 160}, {9, 13, 9, 3, 3, 40}, {97, 13, 9, 2, 2, 300}, {146, 64, 64, 2, 4, 1u << 20}, {139, 13, 9, 3, 3, 40}, {141, 13, 9, 3, 3, 80},
                          {140, 13, 9, 3, 3, 40}, {142, 13, 9, 3, 3, 80}};
    const ReadPipe raw_pipe = make_read_pipe(compile(fs::path(source_root()) / "shaders" / "replay_image.hlsl", {}, dir / "raw_read.spv"));
    const rp::ShaderRead as_float{rp::kRgba32Sfloat,rp::kEncFloat,4};
    uint32_t bad = 0, sample_bad = 0; size_t pieces = 0;
    uint64_t seed = 0xD1B54A32D192ED03ull;
    for (const Case& c : cases) {
      std::vector<uint8_t> src(size_t(rp::image_bytes(c.f, c.w, c.h, c.layers, c.mips)));
      for (auto& b : src) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; b = uint8_t(seed); }
      if (c.f == 97) for (size_t i = 0; i + 1 < src.size(); i += 2) src[i + 1] &= 0x3B;   // finite halves
      pieces += rp::transfer_pieces(c.f, c.w, c.h, c.layers, c.mips, c.stage).size();
      Img a = make_image(c.f, c.w, c.h, c.layers, c.mips, false, true);
      fill_image(a, src, c.stage);
      const std::array<uint32_t,4> components = c.f==139 ? std::array<uint32_t,4>{3,3,3,2} : std::array<uint32_t,4>{};
      auto captured=read_image_all(a);
      bad += captured != src;
      if (rp::block_compressed(c.f)) {
        Img full=make_image(c.f,c.w*2,c.h*2,c.layers+2,c.mips+1,false,true);
        std::vector<uint8_t> all(size_t(rp::image_bytes(c.f,full.w,full.h,full.layers,full.mips)));
        for (size_t j=0;j<all.size();++j) all[j]=uint8_t(j*37+j/19);
        fill_image(full,all);
        captured=read_image_all(full,1,1,c.mips,c.layers);
        // Independently slice mip-major bytes to check the selected block ranges.
        std::vector<uint8_t> expected; uint64_t offset=rp::image_bytes(c.f,full.w,full.h,full.layers,1);
        for(uint32_t mip=0;mip<c.mips;++mip) {
          const uint64_t plane=rp::image_bytes(c.f,std::max(c.w>>mip,1u),std::max(c.h>>mip,1u),1,1);
          expected.insert(expected.end(),all.begin()+size_t(offset+plane),all.begin()+size_t(offset+plane*(1+c.layers)));
          offset+=plane*full.layers;
        }
        bad += captured!=expected;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=full.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;vi.format=VkFormat(c.f);
        vi.components={VkComponentSwizzle(components[0]),VkComponentSwizzle(components[1]),VkComponentSwizzle(components[2]),VkComponentSwizzle(components[3])};
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,c.mips,1,c.layers};
        vkcheck(vkCreateImageView(g.dev,&vi,nullptr,&full.view),"raw BCn subrange view");
        a=full;a.w=c.w;a.h=c.h;a.mips=c.mips;a.layers=c.layers;
      }
      const fs::path p = dir / ("stage-" + std::to_string(c.f) + ".bin");
      { std::ofstream out(p,std::ios::binary); rp::codec::Writer writer(out,captured.size());
        check(writer.write(captured.data(),captured.size()) && writer.finish(),"raw blocks lossless codec"); }
      Img b = make_image(c.f, c.w, c.h, c.layers, c.mips, false, true, false, components);
      fill_image_file(b, p, c.stage);
      bad += read_image_all(b) != captured;
      if (rp::block_compressed(c.f))
        sample_bad += shader_read(raw_pipe,a,as_float,200) != shader_read(raw_pipe,b,as_float,200);
    }
    line("12 staged image upload: pieces read back bit-equal", bad == 0, std::to_string(bad) + " byte/range checks differ, " + std::to_string(pieces) + " pieces");
    line("12b raw BCn sampling: codec round trip identical",sample_bad == 0,std::to_string(sample_bad) + " formats differ");
  }
  {   // 13-16: the lighting inputs (sets 2-3: cubes, LUTs, probe records, samplers) load and replay in closed
      // form (synthesize kSynthAmbient; tools/native_ambient.hlsl, pt_reference PT_ENV / PT_SUN_LUT). The
      // packs above carry none: 1-7 replay with their zero dummies.
    const SynthAmbient& q = kSynthAmbient;
    synthesize(dir / "ambient", true, false, true);
    const PtAccum na = pt_run(dir / "ambient", {}, 1, 3, dir / "ambient-out", true, "native_ambient.hlsl");
    const size_t n = size_t(na.w) * na.h;
    const double k100 = 100.0, E = q.probe * k100, sky = (0.2126 * q.irr[0] + 0.7152 * q.irr[1] + 0.0722 * q.irr[2]) * k100;
    double e_err = 0, sky_err = 0, ao_err = 0, lit_err = 0, probe = 0, t_err = 0, cloud_err = 0, atmo = 0;
    for (size_t i = 0; i < n; ++i) {
      for (int c = 0; c < 3; ++c) {
        e_err = std::max(e_err, std::abs(na.mean(0, c, i) - E));
        lit_err = std::max(lit_err, std::abs(na.mean(1, c, i) - E * q.albedo[c] * q.ao));
        t_err = std::max(t_err, std::abs(na.mean(2, c, i) - q.lut[c]));
      }
      sky_err = std::max(sky_err, std::abs(na.mean(0, 4, i) - sky)); ao_err = std::max(ao_err, std::abs(na.mean(0, 3, i) - q.ao));
      probe += na.mean(1, 3, i); cloud_err = std::max(cloud_err, std::abs(na.mean(2, 3, i) - 1.0)); atmo += na.mean(2, 4, i);
    }
    char d[200];
    std::snprintf(d, sizeof d, "%zu px: max |E - %.0f| %.3g, |sky part - %.3f| %.3g, |AO - %.2f| %.3g", n, E, e_err, sky, sky_err, q.ao, ao_err);
    line("13 lighting inputs load: E_native = local probe over the sky x b13 100, AO", n > 0 && e_err < 0.25 && sky_err < 1e-3 && ao_err < 1e-3, d);
    std::snprintf(d, sizeof d, "max |as lit - E x albedo x AO| %.3g, probe listed at %.0f of %zu px", lit_err, probe, n);
    line("14 native ambient as lit = E_native x diffuse colour x AO", lit_err < 0.1 && probe == double(n), d);
    std::snprintf(d, sizeof d, "max |t - LUT| %.3g, |cloud - 1| %.3g, atmosphere path at %.0f of %zu px", t_err, cloud_err, atmo, n);
    line("15 sun transmittance read from the pack's LUT (set 3)", t_err < 1e-3 && cloud_err < 1e-6 && atmo == double(n), d);
    // PT_ENV: a miss sees the specular cube x m27 x b13 100 (game units), kept out of the traced bounce.
    const PtAccum pe = pt_run(dir / "ambient", {"PT_ENV=1", "PT_SUN_LUT=1", "PT_SUN=0", "PT_LIGHTS=0", "PT_BOUNCES=1"}, 2, kPtModesEnv,
                              dir / "ambient-pt", true);
    double misses = 0, env_err = 0, total = 0;
    for (size_t i = 0; i < size_t(pe.w) * pe.h; ++i) {
      total += std::abs(pe.mean(0, 0, i)) + std::abs(pe.mean(0, 1, i)) + std::abs(pe.mean(0, 2, i));
      const double miss = pe.mean(7, 3, i);
      if (!(miss > 0)) continue;
      misses += 1;
      env_err = std::max(env_err, std::abs(pe.mean(7, 4, i) / miss - q.spec * k100));
    }
    std::snprintf(d, sizeof d, "%.0f px with first-ray misses, max |env / miss - %.0f| %.3g, traced total %.3g", misses, q.spec * k100, env_err, total);
    line("16 PT_ENV: a miss sees the specular cube x b13 100; total untouched", misses > 0 && env_err < 1e-3 && total == 0, d);
  }
  line("8 no validation errors", g_validation_errors.load() == 0, std::to_string(g_validation_errors.load()) + " errors");
  std::printf("%s: %d failure(s); GPU dispatch %.3f ms\n", failures ? "FAIL" : "PASS", failures, a.gpu_ms);
  return failures ? 1 : 0;
}
}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  fs::path pack, out, hlsl, spv, synth, test, ls_test, rf_test;
  std::vector<std::string> defines;
  uint32_t repeat = 1;
  bool repeat_given = false;
  std::string compile_pass = "gi";
  bool synth_box = false, synth_lighting = false, synth_casters = false, compile_only = false;
  bool validation = true, dark = false, pt = false, pt_cost_only = false, pt_skip_cost = false, native = false;
  uint32_t pt_iters = 32;
  fs::path pt_test;
  for (size_t i = 0; i < args.size(); ++i) {
    auto next = [&]() -> std::string { check(i + 1 < args.size(), "missing value after " + args[i]); return args[++i]; };
    if (args[i] == "--pack") pack = next();
    else if (args[i] == "--out") out = next();
    else if (args[i] == "--hlsl") hlsl = next();
    else if (args[i] == "--spv") spv = next();
    else if (args[i] == "--define") defines.push_back(next());
    else if (args[i] == "--repeat") { repeat = uint32_t(std::max(1, std::atoi(next().c_str()))); repeat_given = true; }
    else if (args[i] == "--ab-blas") g_ab_blas = true;
    else if (args[i] == "--blas-control") g_blas_control = true;
    else if (args[i] == "--blas-policy") {
      g_blas_policy = next();
      check(g_blas_policy == "legacy" || g_blas_policy == "layer" || g_blas_policy == "trace", "--blas-policy legacy|layer|trace");
    }
    else if (args[i] == "--ab-spv") g_ab_spv = next();
    else if (args[i] == "--ab-rgen") g_ab_rgen = next();
    else if (args[i] == "--synth-size") {
      g_synth_size = uint32_t(std::stoul(next()));
      check(g_synth_size >= 16 && g_synth_size <= 4096 && g_synth_size % 16 == 0, "--synth-size 16..4096, a multiple of 16");
    }
    else if (args[i] == "--rgen-spec") {
      const std::string kv = next(); const size_t eq = kv.find('=');
      check(eq != std::string::npos, "--rgen-spec ID=UINT");
      g_rgen_specs[uint32_t(std::stoul(kv.substr(0, eq), nullptr, 0))] = uint32_t(std::stoul(kv.substr(eq + 1), nullptr, 0));
    }
    else if (args[i] == "--ab-numeric") g_ab_numeric = true;
    else if (args[i] == "--ab-require-equal") g_ab_require_equal = true;
    else if (args[i] == "--require-live-equal") g_require_live_equal = true;
    else if (args[i] == "--ab-warmup") {
      const auto text = next();
      check(!text.empty() && text.find_first_not_of("0123456789") == std::string::npos && text.size() <= 3,
            "--ab-warmup requires an integer 0..256");
      g_ab_warmup = uint32_t(std::stoul(text));
      check(g_ab_warmup <= 256, "--ab-warmup requires an integer 0..256");
    }
    else if (args[i] == "--origin-self-test") return origin_self_test() ? 0 : 1;
    else if (args[i] == "--ab-self-test") return bg3::replay::ab_self_test() && blas_class_test() ? 0 : 1;
    else if (args[i] == "--no-validation") validation = false;
    else if (args[i] == "--pipeline-stats") g_pipeline_stats = true;
    else if (args[i] == "--compile-only") compile_only = true;
    else if (args[i] == "--compile-pass") compile_pass = next();
    else if (args[i] == "--reference-out") g_reference_out = next();
    else if (args[i] == "--ab-spec") {
      const std::string kv = next(); const size_t eq = kv.find('='), comma = kv.find(',');
      check(eq != std::string::npos && comma != std::string::npos && eq < comma, "--ab-spec ID=A,B");
      auto number = [&](const std::string& text) {
        size_t used = 0; const auto value = std::stoull(text, &used, 0);
        check(used == text.size() && value <= UINT32_MAX && text.find('-') == std::string::npos, "--ab-spec requires uint32 ID=A,B");
        return uint32_t(value);
      };
      const auto id = number(kv.substr(0, eq));
      check(g_ab_specs.count(id) == 0, "duplicate --ab-spec ID");
      g_ab_specs[id] = {number(kv.substr(eq + 1, comma - eq - 1)), number(kv.substr(comma + 1))};
    }
    else if (args[i] == "--spec") {
      const std::string kv = next(); const size_t eq = kv.find('=');
      check(eq != std::string::npos, "--spec ID=UINT");
      size_t a = 0, b = 0;
      const auto id = std::stoull(kv.substr(0, eq), &a, 0), value = std::stoull(kv.substr(eq + 1), &b, 0);
      check(a == eq && b == kv.size() - eq - 1 && id <= UINT32_MAX && value <= UINT32_MAX && kv.find('-') == std::string::npos,
            "--spec requires uint32 ID=UINT");
      g_specs[uint32_t(id)] = uint32_t(value);
    }
    else if (args[i] == "--synthesize") synth = next();
    else if (args[i] == "--dark") dark = true;
    else if (args[i] == "--box") synth_box = true;            // --synthesize: the closed box (PT furnace)
    else if (args[i] == "--lighting") synth_lighting = true;  // --synthesize: with the lighting inputs (kSynthAmbient)
    else if (args[i] == "--casters") synth_casters = true;    // --synthesize: the box's floor as rt_cascade_casters
    else if (args[i] == "--rf-self-test") rf_test = next();
    else if (args[i] == "--ls-self-test") ls_test = next();
    else if (args[i] == "--self-test") test = next();
    else if (args[i] == "--load-test") return load_test();
    else if (args[i] == "--pt-self-test") pt_test = next();
    else if (args[i] == "--pt-reference") pt = true;
    else if (args[i] == "--pt-iters") pt_iters = uint32_t(std::max(1, std::atoi(next().c_str())));
    else if (args[i] == "--pt-cost") pt_cost_only = true;
    else if (args[i] == "--pt-skip-cost") pt_skip_cost = true;
    else if (args[i] == "--native-ambient") native = true;
    else if (args[i] == "--sun-trust") g_sun_trust = next();
    else if (args[i] == "--no-casters") g_no_casters = true;
    else if (args[i] == "--origin") g_origin = true;
    else if (args[i] == "--push") {
      const std::string kv = next(); const size_t eq = kv.find('=');
      check(eq != std::string::npos, "--push FIELD=VALUE");
      g_push_set.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
    }
    else die("unknown argument " + args[i] + " (see tools/frame_replay.md)");
  }
  check(!g_blas_control || g_ab_blas, "--blas-control requires --ab-blas");
  check(!g_ab_blas || g_blas_policy != "trace", "--ab-blas baseline is layer; do not select trace baseline");
  if (g_origin) check(!compile_only && !pt && !native && !pt_cost_only && test.empty() && pt_test.empty() && synth.empty(), "--origin requires ordinary pack replay");
  if (ab_mode()) {
    check(!compile_only && !pt && !native && !pt_cost_only && test.empty() && pt_test.empty() && synth.empty(), "--ab-spec/--ab-spv/--ab-rgen require ordinary pack replay");
    check(g_ab_rgen.empty() || g_ab_spv.empty(), "--ab-rgen and --ab-spv both name pipeline B");
    check(repeat <= (UINT32_MAX - 4) / 4, "paired repeat count too large");
    if (!repeat_given) repeat = 40;
  }
  check(!g_ab_numeric || ab_mode(), "--ab-numeric requires paired A/B mode");
  check(!g_ab_warmup || ab_mode(), "--ab-warmup requires --ab-spec or --ab-spv");
  if (g_require_live_equal)
    check(!compile_only && !pt && !native && !pt_cost_only && !g_origin && !pack.empty(),
          "--require-live-equal requires ordinary pack replay with live outputs and counters");
  if (!rf_test.empty()) return rf_self_test(rf_test);
  if (!ls_test.empty()) return ls_self_test(ls_test);
  if (!test.empty()) return self_test(test);
  if (!pt_test.empty()) return pt_self_test(pt_test);
  if (!synth.empty()) { synthesize(synth, dark, synth_box || synth_casters, synth_lighting, synth_casters); std::printf("synthetic pack written to %s\n", synth.string().c_str()); return 0; }
  if (compile_only) {
    check(pack.empty() && (!hlsl.empty() != !spv.empty()), "--compile-only requires exactly one of --hlsl or --spv, no --pack (production layout)");
    check(compile_pass == "gi" || compile_pass == "origin" || compile_pass == "reflections" || compile_pass == "local-shadows", "--compile-pass gi|origin|reflections|local-shadows");
    if (int r = init_gpu(validation, false)) return r;
    if (out.empty()) out = fs::path(source_root()) / ".scratch" / "pipeline-stats";
    fs::create_directories(out);
    std::vector<uint32_t> code;
    if (!spv.empty()) { auto b = read_file(spv); check(!b.empty() && b.size() % 4 == 0, "SPIR-V size"); code.resize(b.size() / 4); std::memcpy(code.data(), b.data(), b.size()); }
    else code = compile(hlsl, defines, out / "pass.spv");
    compile_production_pipeline(code, compile_pass);
    return g_validation_errors.load() ? 1 : 0;
  }
  check(!pack.empty(), "usage: frame_replay --pack <dir> [--out <dir>] [--hlsl <file> | --spv <file>] [--define N=V] [--repeat N]");
  if (int r = init_gpu(validation, false)) return r;
  if (out.empty()) out = pack / (native ? "native-ambient" : pt || pt_cost_only ? "pt-reference" : "replay");
  // The Kasten-Young estimate of the sun's transmittance at the pack's sun elevation (cam.x[14].y).
  auto estimate_t = [&](const rp::Manifest& m, float t[3]) {
    t[0] = t[1] = t[2] = 1;
    std::vector<uint8_t> cam;
    for (auto& b : m.buffers) if (b.set == 0 && b.binding == 0 && !b.file.empty()) cam = read_file(pack / b.file);
    if (cam.size() >= 400) { float mu; std::memcpy(&mu, cam.data() + 24 * 16 + 4, 4); sun_transmittance(mu, t); }
  };
  // A pack with the cascade casters: the PT traces them (PT_CASTERS, tools/pt_reference.hlsl) unless
  // --no-casters, or the command line chooses PT_CASTERS / PT_ROOFS itself.
  auto casters_define = [&](const rp::Manifest& m, std::vector<std::string>& d, std::string& params) {
    if (m.casters.file.empty() || g_no_casters) return;
    for (auto& x : d) if (x.rfind("PT_CASTERS", 0) == 0 || x.rfind("PT_ROOFS", 0) == 0) return;
    d.push_back("PT_CASTERS=1");
    params += "PT_CASTERS=1 (the pack's cascade casters: " + std::to_string(m.casters.tris) + " triangles) ";
  };
  if (native) {
    // tools/native_ambient.hlsl + tools/pt_reference.hlsl over the pack (tools/frame_replay.md "Native ambient").
    fs::create_directories(out);
    rp::Manifest m; std::string error;
    { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
    const rp::LightingInputs li = rp::lighting_inputs(m);
    check(li.ambient, "the pack has no lighting inputs (set 2 b34 irradiance cube and b38 DFG LUT): capture it with a writer that has "
                      "them (tools/frame_replay.md \"Lighting inputs\")");
    float est[3]; estimate_t(m, est);
    std::vector<std::string> d = defines;
    if (li.sun_lut) d.push_back("PT_SUN_LUT=1");
    else { char x[96]; std::snprintf(x, sizeof x, "PT_SUN_T=float3(%.4f,%.4f,%.4f)", est[0], est[1], est[2]); d.push_back(x); }
    if (li.sky) d.push_back("PT_ENV=1");
    std::string params;
    for (auto& x : d) params += x + " ";
    casters_define(m, d, params);
    params += "PT iterations " + std::to_string(pt_iters);
    const PtAccum na = pt_run(pack, {}, 1, 3, out, true, "native_ambient.hlsl");
    const PtAccum a = pt_run(pack, d, pt_iters, li.sky ? kPtModesEnv : kPtModes, out);
    native_ambient_report(pack, na, a, out, params, est);
    if (g_validation_errors.load()) { std::printf("FAIL %u validation errors\n", g_validation_errors.load()); return 1; }
    std::printf("written to %s\n", out.string().c_str());
    return 0;
  }
  if (pt || pt_cost_only) {
    // tools/pt_reference.hlsl over the pack (tools/frame_replay.md "PT reference"). The sun's
    // transmittance: the pack's LUT when it has the lighting inputs, else estimated, unless --define PT_SUN_T=... is given.
    fs::create_directories(out);
    rp::Manifest m; std::string error;
    { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
    std::string params;
    bool has_t = false;
    for (auto& d : defines) { has_t |= d.rfind("PT_SUN_T=", 0) == 0; params += d + " "; }
    if (!has_t && rp::lighting_inputs(m).sun_lut) {   // a pack with the lighting inputs: the game's LUT and clouds
      defines.push_back("PT_SUN_LUT=1"); params += "PT_SUN_LUT=1 (the pack's transmittance LUT) ";
    } else if (!has_t) {
      float t[3]; estimate_t(m, t);   // cam.x[14].y
      char d[96]; std::snprintf(d, sizeof d, "PT_SUN_T=float3(%.4f,%.4f,%.4f)", t[0], t[1], t[2]);
      defines.push_back(d); params += std::string(d) + " (estimated) ";
    }
    casters_define(m, defines, params);
    if (!pt_skip_cost) pt_cost(pack, defines, out);
    if (!pt_cost_only) {
      const PtAccum a = pt_run(pack, defines, pt_iters, kPtModes, out);
      pt_report(pack, a, out, params.empty() ? "defaults" : params);
    }
    if (g_validation_errors.load()) { std::printf("FAIL %u validation errors\n", g_validation_errors.load()); return 1; }
    std::printf("written to %s\n", out.string().c_str());
    return 0;
  }
  std::vector<uint32_t> code;
  if (!spv.empty()) { auto b = read_file(spv); code.resize(b.size() / 4); std::memcpy(code.data(), b.data(), code.size() * 4); }
  replay(pack, spv.empty() ? nullptr : &code, hlsl, defines, out, repeat, false);
  if (g_validation_errors.load()) { std::printf("FAIL %u validation errors\n", g_validation_errors.load()); return 1; }
  std::printf("written to %s\n", out.string().c_str());
  return g_reference_mismatch ? 1 : 0;
}
