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
#include "../common/native_hit_records.h"
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
  bool validation = false;
  std::string name;
  uint32_t max_image_2d = 0, max_layers = 0;
  uint64_t device_bytes = 0;   // device-local memory allocated (never freed before exit)
};
Gpu g;
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
  const char* extensions[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  VkPhysicalDeviceVulkan12Features use12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  use12.bufferDeviceAddress = VK_TRUE; use12.runtimeDescriptorArray = VK_TRUE; use12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  use12.hostQueryReset = VK_TRUE;
  VkPhysicalDeviceRayQueryFeaturesKHR useRQ{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &use12}; useRQ.rayQuery = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR useAS{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &useRQ};
  useAS.accelerationStructure = VK_TRUE;
  VkPhysicalDeviceFeatures core{}; core.shaderStorageImageExtendedFormats = VK_TRUE; core.samplerAnisotropy = features.features.samplerAnisotropy;
  core.shaderSampledImageArrayDynamicIndexing = features.features.shaderSampledImageArrayDynamicIndexing;
  core.shaderInt64 = features.features.shaderInt64; core.shaderInt16 = features.features.shaderInt16;
  float priority = 0.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = g.family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &useAS};
  dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 3; dci.ppEnabledExtensionNames = extensions; dci.pEnabledFeatures = &core;
  vkcheck(vkCreateDevice(g.physical, &dci, nullptr, &g.dev), "device");
  vkGetDeviceQueue(g.dev, g.family, 0, &g.queue);
#define PROC(type, field) g.field = reinterpret_cast<PFN_##type>(vkGetDeviceProcAddr(g.dev, #type)); check(g.field != nullptr, #type)
  PROC(vkCreateAccelerationStructureKHR, createAS); PROC(vkDestroyAccelerationStructureKHR, destroyAS);
  PROC(vkGetAccelerationStructureBuildSizesKHR, sizesAS); PROC(vkCmdBuildAccelerationStructuresKHR, buildAS);
  PROC(vkGetAccelerationStructureDeviceAddressKHR, addressAS);
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
Img make_image(uint32_t format, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, bool storage, bool array, bool cube = false) {
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
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  check(bool(f), "read " + p.string());
  const uint64_t bytes = rp::image_bytes(im.format, im.w, im.h, im.layers, im.mips);
  check(uint64_t(f.tellg()) == bytes, "image data size: " + p.string());
  fill_image_pieces(im, [&](uint64_t offset, uint64_t n, uint8_t* dst) {
    f.seekg(std::streamoff(offset));
    f.read(reinterpret_cast<char*>(dst), std::streamsize(n));
    check(bool(f), "read " + p.string());
  }, stage_max);
}
// Every mip and layer back, packed (the self-test's round trip).
std::vector<uint8_t> read_image_all(const Img& im) {
  const uint64_t bytes = rp::image_bytes(im.format, im.w, im.h, im.layers, im.mips);
  Buf staging = make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  submit([&](VkCommandBuffer c) {
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout = layout_of(im); ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = im.image; ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, im.mips, 0, im.layers};
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    VkDeviceSize at = 0;
    for (uint32_t m = 0; m < im.mips; ++m) {
      const uint32_t mw = std::max(im.w >> m, 1u), mh = std::max(im.h >> m, 1u);
      VkBufferImageCopy r{}; r.bufferOffset = at; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, im.layers}; r.imageExtent = {mw, mh, 1};
      vkCmdCopyImageToBuffer(c, im.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.b, 1, &r);
      at += rp::image_bytes(im.format, mw, mh, im.layers, 1);
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
  std::vector<uint8_t> out(static_cast<size_t>(f.tellg()));
  f.seekg(0); f.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
  check(bool(f) || out.empty(), "read " + p.string());
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

// ---------------------------------------------------------------- shader
std::string source_root() { return BG3RTX_SOURCE_DIR; }
std::vector<uint32_t> compile(const fs::path& hlsl, const std::vector<std::string>& defines, const fs::path& out_spv) {
  char sdk[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableA("VULKAN_SDK", sdk, MAX_PATH);
  const fs::path dxc = n && n < MAX_PATH ? fs::path(sdk) / "Bin" / "dxc.exe" : fs::path(BG3RTX_DXC);
  const fs::path shaders = fs::path(source_root()) / "shaders";
  // The layer's flags (layer/shaders.cpp): vulkan1.3, HLSL 2021, cs_6_6, BG3RTX=1, the SHaRC
  // headers when present (external/ of this checkout), the file's "// dxc:" line, -O3.
  std::string cmd = "\"" + dxc.string() + "\" \"" + hlsl.string() + "\" -spirv -fspv-target-env=vulkan1.3 -HV 2021 -T cs_6_6 -E main -I \"" +
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

// ---------------------------------------------------------------- statistics (core/gi.cpp wording, 1 frame)
constexpr uint32_t kCounters = 512;   // >= core/gi.cpp kStatsCounters (417)
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
                  "fallback0 ambient occlusion unchanged; supported zero: no positive light term %llu, all occluded %llu",
                  n, u(t[282]), u(t[283]), u(t[284]), u(t[285]), double(t[286]) / 8192.0, u(t[134]), u(t[135]));
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
struct RunResult { std::vector<ImageResult> outputs; Stats stats; double gpu_ms; };
struct Replay { std::vector<RunResult> runs; std::vector<std::string> warnings; };

ImageResult summarize(const std::string& name, uint32_t format, uint32_t w, uint32_t h, std::vector<uint8_t> raw) {
  ImageResult r{name, format, w, h, std::move(raw), 0, {0, 0, 0, 0}, 0};
  r.hash = fnv1a(r.raw);
  const uint64_t px = uint64_t(w) * h;
  uint32_t comps = format == VK_FORMAT_R16G16B16A16_SFLOAT || format == VK_FORMAT_R32G32B32A32_SFLOAT ? 4 : 1;
  for (uint64_t i = 0; i < px; ++i)
    for (uint32_t k = 0; k < comps; ++k) {
      float v = format == VK_FORMAT_R16G16B16A16_SFLOAT ? half_to_float(reinterpret_cast<const uint16_t*>(r.raw.data())[i * 4 + k])
                                                       : reinterpret_cast<const float*>(r.raw.data())[i * comps + k];
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
};

// --sun-trust <file> (offline gi_hit_sun / gi_far, tools/frame_replay.md): the D-LS sun set's b8, the trust
// header + roof volumes image (core/sun_trust.h layout, written by tools/pt_roofs.py --trust), which the
// gi_hit_direct contract does not carry. Bound as set 3 b8 (R32_UINT, one texel per 4 bytes) and Push::ls_flags
// bit 25 (a trust slot this frame) is set, as core/gi.cpp does when the slot exists.
fs::path g_sun_trust;

Replay replay(const fs::path& pack, const std::vector<uint32_t>* spirv_in, const fs::path& hlsl_override,
              const std::vector<std::string>& extra_defines, const fs::path& out, uint32_t repeat, bool quiet,
              const ReplayHooks* hooks = nullptr) {
  Replay result;
  rp::Manifest m; std::string error;
  { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
  rp::Pass pass = *rp::find_pass(m.pass);
  static std::vector<rp::Slot> trust_slots;
  if (!g_sun_trust.empty()) {
    trust_slots.assign(pass.slots, pass.slots + pass.slot_count);
    trust_slots.push_back({3, 8, rp::kStorage, 1, VK_FORMAT_R32_UINT, "sunTrust"});
    pass.slots = trust_slots.data(); pass.slot_count = uint32_t(trust_slots.size());
  }
  fs::create_directories(out);
  // Shader: the production file from this checkout (or an override), the pack's variant defines.
  std::vector<std::string> defines = extra_defines;
  for (size_t a = 0; a < m.defines.size();) { size_t e = m.defines.find(';', a); defines.push_back(m.defines.substr(a, e - a)); if (e == std::string::npos) break; a = e + 1; }
  std::vector<uint32_t> spirv = spirv_in ? *spirv_in
      : compile(hlsl_override.empty() ? fs::path(source_root()) / "shaders" / pass.shader : hlsl_override, defines, out / "pass.spv");
  auto file = [&](const std::string& name) { return read_file(pack / name); };

  // Buffers: the declared range, filled from the file and the ranges.
  std::map<uint64_t, Buf> buffers;   // key set << 32 | binding
  auto key = [](uint32_t s, uint32_t b) { return (uint64_t(s) << 32) | b; };
  const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
  for (const auto& b : m.buffers) {
    const rp::Slot* slot = rp::find_slot(pass, b.set, b.binding);
    check(slot && (slot->kind == rp::kUbo || slot->kind == rp::kSsbo), "buffer slot " + std::to_string(b.set) + "/" + std::to_string(b.binding));
    Buf buf = make_buffer(b.bytes, slot->kind == rp::kUbo ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : ssbo, false);
    buf.size = b.bytes;   // the descriptor range is the captured one, not the 16-byte minimum
    if (!b.file.empty()) { auto d = file(b.file); check(d.size() <= b.bytes, "buffer file larger than its range"); upload(buf, 0, d.data(), d.size()); }
    buffers[key(b.set, b.binding)] = buf;
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
  Buf stats = make_buffer(kCounters * 64 * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
  // Images (inputs), outputs, dummies.
  std::map<uint64_t, std::map<uint32_t, Img>> images;   // slot -> element -> image
  // The writer stores each VkImage once and names its file from every descriptor element that binds
  // it (the 4 x 4 dummies fill thousands of slots, a VT cache is shared by gAlphaCache and gHitCache):
  // one image per file, view type and shape.
  std::map<std::string, Img> by_file;
  uint32_t decoded = 0, decoded_bc = 0;
  for (const auto& i : m.images) {
    const rp::Slot* slot = rp::find_slot(pass, i.set, i.binding);
    check(slot && (slot->kind == rp::kSampled2D || slot->kind == rp::kSampled2DArray || slot->kind == rp::kSampledCube || slot->kind == rp::kStorage) &&
              i.element < slot->count,
          "image slot " + std::to_string(i.set) + "/" + std::to_string(i.binding));
    if (slot->kind == rp::kSampledCube) check(i.layers == 6 && i.w == i.h, std::string("cube image of ") + slot->name + " (6 square layers)");
    if (slot->kind == rp::kStorage) check(i.format == slot->storage_format, std::string("storage format of ") + slot->name);
    const bool array = slot->kind == rp::kSampled2DArray, storage = slot->kind == rp::kStorage, cube = slot->kind == rp::kSampledCube;
    const std::string k = i.file + "|" + std::to_string(i.format) + "|" + std::to_string(i.w) + "x" + std::to_string(i.h) + "x" +
                          std::to_string(i.layers) + "x" + std::to_string(i.mips) + (array ? "|a" : "") + (storage ? "|s" : "") + (cube ? "|c" : "");
    auto known = by_file.find(k);
    if (known == by_file.end()) {
      Img im = make_image(i.format, i.w, i.h, i.layers, i.mips, storage, array, cube);
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
  struct Out { Img img; std::string name, reference; };
  std::vector<Out> outputs;
  for (const auto& o : m.outputs) {
    const rp::Slot* slot = rp::find_slot(pass, o.set, o.binding);
    check(slot && slot->kind == rp::kStorage && slot->storage_format == o.format, "output slot");
    Img im = make_image(o.format, o.w, o.h, 1, 1, true, false);
    fill_image(im, {});
    images[key(o.set, o.binding)][0] = im;
    outputs.push_back({im, slot->name, o.reference});
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
    VkSampler x; vkcheck(vkCreateSampler(g.dev, &si, nullptr, &x), "pack sampler");
    samplers[key(s.set, s.binding)][s.element] = x;
  }

  // ---- TLAS: one BLAS per distinct instance key, its triangles from the instance's InstInfo ----
  std::vector<VkAccelerationStructureInstanceKHR> instances(m.instance_count);
  if (m.instance_count) { auto d = file(m.instances); check(d.size() == instances.size() * sizeof(instances[0]), "instances size"); std::memcpy(instances.data(), d.data(), d.size()); }
  std::vector<uint8_t> info;
  { auto it = buffers.find(key(1, 7)); if (it != buffers.end()) info = download(it->second, it->second.size); }
  struct Blas { VkAccelerationStructureKHR as{}; Buf store; VkDeviceAddress address = 0; };
  std::map<uint64_t, Blas> blas;
  uint32_t skipped = 0, built = 0;
  Buf scratch;
  const VkDeviceSize align = std::max<VkDeviceSize>(g.as.minAccelerationStructureScratchOffsetAlignment, 1);
  auto build_blas = [&](VkDeviceAddress vertices, uint32_t tris, uint32_t stride, bool opaque) {
    VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geo.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;   // layer/bvhsplit.cpp tri_geometry
    auto& t = geo.geometry.triangles;
    t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR; t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = vertices; t.vertexStride = stride; t.maxVertex = tris * 3 - 1; t.indexType = VK_INDEX_TYPE_NONE_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR; bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
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
    if (!usable) { ++skipped; in.mask = 0; in.accelerationStructureReference = empty.address; continue; }
    auto it = blas.find(k);
    if (it == blas.end()) { it = blas.emplace(k, build_blas(src->second.address + base, tris, stride, !(meta & rp::kInfoAlpha))).first; ++built; }
    in.accelerationStructureReference = it->second.address;
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

  // ---- descriptor sets: every slot of the pass contract, pack resources or dummies ----
  std::vector<VkDescriptorSetLayout> setLayouts(pass.set_count);
  std::vector<VkDescriptorPoolSize> poolSizes;
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
      list.push_back({slot.binding, vk_type(slot.kind), slot.count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
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
  VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, pass.push_bytes};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = pass.set_count; pl.pSetLayouts = setLayouts.data(); pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pushRange;
  VkPipelineLayout layout; vkcheck(vkCreatePipelineLayout(g.dev, &pl, nullptr, &layout), "pipeline layout");
  VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = spirv.size() * 4; smi.pCode = spirv.data();
  VkShaderModule module; vkcheck(vkCreateShaderModule(g.dev, &smi, nullptr, &module), "shader module");
  VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main"}; cpi.layout = layout;
  VkPipeline pipeline; vkcheck(vkCreateComputePipelines(g.dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline), "compute pipeline");
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
  for (uint32_t run = 0; run < repeat; ++run) {
    std::memset(stats.map, 0, size_t(stats.size));
    push = push_pack;
    if (hooks && hooks->patch) hooks->patch(run, push);
    if (!g_sun_trust.empty()) {   // Push::ls_flags (offset 188, static_assert below) bit 25: a sun trust slot exists
      uint32_t lf; std::memcpy(&lf, push.data() + 188, 4); lf |= 1u << 25; std::memcpy(push.data() + 188, &lf, 4);
    }
    submit([&](VkCommandBuffer c) {
      barrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
      for (auto& o : outputs) clear_storage(c, o.img);
      vkCmdResetQueryPool(c, queries, 0, 2);
      barrier(c, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
              VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_UNIFORM_READ_BIT);
      vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, pass.set_count, sets.data(), 0, nullptr);
      vkCmdPushConstants(c, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pass.push_bytes, push.data());
      vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0);
      vkCmdDispatch(c, m.groups[0], m.groups[1], m.groups[2]);
      vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries, 1);
      barrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    });
    uint64_t ticks[2] = {};
    vkcheck(vkGetQueryPoolResults(g.dev, queries, 0, 2, sizeof ticks, ticks, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "timestamps");
    RunResult rr;
    rr.gpu_ms = double(ticks[1] - ticks[0]) * g.tick_ns * 1e-6;
    std::vector<uint8_t> raw(static_cast<size_t>(stats.size)); std::memcpy(raw.data(), stats.map, raw.size());
    rr.stats = fold(raw);
    for (auto& o : outputs) rr.outputs.push_back(summarize(o.name, o.img.format, o.img.w, o.img.h, read_image(o.img)));
    if (!quiet || run == 0) {
      std::printf("[replay] %s run %u: dispatch %u x %u x %u, GPU %.3f ms\n", m.pass.c_str(), run, m.groups[0], m.groups[1], m.groups[2], rr.gpu_ms);
      for (auto& l : gi_hit_direct_lines(rr.stats)) std::printf("  %s\n", l.c_str());
      for (auto& r : rr.outputs)
        std::printf("  output %s %ux%u: mean %.7g %.7g %.7g %.7g, non-finite %llu, fnv1a %016llx\n", r.name.c_str(), r.w, r.h, r.mean[0], r.mean[1],
                    r.mean[2], r.mean[3], (unsigned long long)r.nonfinite, (unsigned long long)r.hash);
    }
    if (hooks && hooks->each) hooks->each(run, rr);
    if (hooks && run + 1 < repeat) continue;   // many runs: keep only the last (outputs below)
    result.runs.push_back(std::move(rr));
  }
  // Outputs (the last run), and the live reference comparison when the pack has one.
  for (size_t i = 0; i < outputs.size() && !hooks; ++i) {
    const auto& r = result.runs.back().outputs[i];
    write_vec(out / (r.name + ".raw"), r.raw);
    write_pfm(out / (r.name + ".pfm"), r);
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

// box (the PT furnace, tools/pt_reference.hlsl): the camera inside a closed cube |x|, |y|, |z| <= 2 of
// 12 inward triangles without a material record (instance 0 only); every pixel sees the face y = 2.
// lighting (the native ambient checks, tools/native_ambient.hlsl): also the lighting inputs of sets
// 2-3 in closed form (kSynthAmbient): sky irradiance cube (0.25, 0.5, 1), specular cube 2, DFG LUT
// (1, 0, 1), AO 0.5, albedo (0.8, 0.6, 0.4), roughness 0.5, one sphere probe of radius 100 around
// the camera with a diffuse cube 3 (weight 1) listed in the froxel, m27 = m28 = 1, m29 = 0; the
// atmosphere sun path (colour 1, intensity 1) through a transmittance LUT (0.7, 0.6, 0.5).
struct SynthAmbient { float irr[3] = {0.25f, 0.5f, 1.0f}, spec = 2, probe = 3, ao = 0.5f, albedo[3] = {0.8f, 0.6f, 0.4f}, lut[3] = {0.7f, 0.6f, 0.5f}; };
constexpr SynthAmbient kSynthAmbient{};
void synthesize(const fs::path& dir, bool dark, bool box = false, bool lighting = false) {
  fs::create_directories(dir);
  constexpr uint32_t W = 128, H = 128, frame = 3, stamp = 7;
  constexpr float kFront = 4.3f;   // F: 20 cm in front of K, far beyond the on-screen depth tolerance (~2 cm footprint)
  constexpr float A = 0.5f;
  constexpr uint32_t stride = 64, record = bg3::native_hit::nh_alpha_rows;
  // Soup: 64-byte vertices (position float4 at 0, UV at 16), non-indexed. A quad in the plane y
  // over [x0,x1] x [z0,z1], wound so that cross(p1 - p0, p2 - p0) points along `ny`.
  std::vector<uint8_t> soup, basis;
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
    auto face = [&](V3 a, V3 b, V3 c, V3 d, V3 n) {
      V3 q[4] = {a, b, c, d};
      int order[6] = {0, 1, 2, 0, 2, 3};
      if (dot3(cross(sub(q[1], q[0]), sub(q[2], q[0])), n) < 0) { order[1] = 2; order[2] = 1; order[4] = 3; order[5] = 2; }
      for (int k : order) vertex(soup, q[k], 0, 0, {1, 0, 0}, n, {0, 0, 1});
    };
    const float L = kBox;
    face({-L, L, -L}, {L, L, -L}, {L, L, L}, {-L, L, L}, {0, -1, 0});      // y = +L (the screen)
    face({-L, -L, -L}, {L, -L, -L}, {L, -L, L}, {-L, -L, L}, {0, 1, 0});   // y = -L
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
  const uint32_t info[8] = {0, stride, 0, box ? 12u : 4u, kBase, stride | bg3::native_hit::nh_opaque_meta, record, 2};
  const uint32_t instances_used = box ? 1 : 2;
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
               t[3] = {0, 0, 0}, cloud = 0, atmo = 0; } S;
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
  line("8 no validation errors", g_validation_errors.load() == 0, std::to_string(g_validation_errors.load()) + " errors");
  std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
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
  bool finite = a.outputs[0].nonfinite == 0 && a.outputs[1].nonfinite == 0;
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
    const Case cases[] = {{138, 37, 29, 3, 3, 160}, {9, 13, 9, 3, 3, 40}, {97, 13, 9, 2, 2, 300}, {146, 64, 64, 2, 4, 1u << 20}};
    uint32_t bad = 0; size_t pieces = 0;
    uint64_t seed = 0xD1B54A32D192ED03ull;
    for (const Case& c : cases) {
      std::vector<uint8_t> src(size_t(rp::image_bytes(c.f, c.w, c.h, c.layers, c.mips)));
      for (auto& b : src) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; b = uint8_t(seed); }
      if (c.f == 97) for (size_t i = 0; i + 1 < src.size(); i += 2) src[i + 1] &= 0x3B;   // finite halves
      pieces += rp::transfer_pieces(c.f, c.w, c.h, c.layers, c.mips, c.stage).size();
      Img a = make_image(c.f, c.w, c.h, c.layers, c.mips, false, true);
      fill_image(a, src, c.stage);
      bad += read_image_all(a) != src;
      const fs::path p = dir / ("stage-" + std::to_string(c.f) + ".bin");
      write_vec(p, src);
      Img b = make_image(c.f, c.w, c.h, c.layers, c.mips, false, true);
      fill_image_file(b, p, c.stage);
      bad += read_image_all(b) != src;
    }
    line("12 staged image upload: pieces read back bit-equal", bad == 0, std::to_string(bad) + " of 8 differ, " + std::to_string(pieces) + " pieces");
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
  fs::path pack, out, hlsl, spv, synth, test;
  std::vector<std::string> defines;
  uint32_t repeat = 1;
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
    else if (args[i] == "--repeat") repeat = uint32_t(std::max(1, std::atoi(next().c_str())));
    else if (args[i] == "--no-validation") validation = false;
    else if (args[i] == "--synthesize") synth = next();
    else if (args[i] == "--dark") dark = true;
    else if (args[i] == "--self-test") test = next();
    else if (args[i] == "--load-test") return load_test();
    else if (args[i] == "--pt-self-test") pt_test = next();
    else if (args[i] == "--pt-reference") pt = true;
    else if (args[i] == "--pt-iters") pt_iters = uint32_t(std::max(1, std::atoi(next().c_str())));
    else if (args[i] == "--pt-cost") pt_cost_only = true;
    else if (args[i] == "--pt-skip-cost") pt_skip_cost = true;
    else if (args[i] == "--native-ambient") native = true;
    else if (args[i] == "--sun-trust") g_sun_trust = next();
    else die("unknown argument " + args[i] + " (see tools/frame_replay.md)");
  }
  if (!test.empty()) return self_test(test);
  if (!pt_test.empty()) return pt_self_test(pt_test);
  if (!synth.empty()) { synthesize(synth, dark); std::printf("synthetic pack written to %s\n", synth.string().c_str()); return 0; }
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
  return 0;
}
