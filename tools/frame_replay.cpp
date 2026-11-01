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
  vkcheck(vkBindBufferMemory(g.dev, out.b, out.m, 0), "bind buffer");
  VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; address.buffer = out.b;
  out.address = vkGetBufferDeviceAddress(g.dev, &address);
  if (host) { vkcheck(vkMapMemory(g.dev, out.m, 0, out.size, 0, &out.map), "map"); std::memset(out.map, 0, size_t(out.size)); }
  return out;
}
void upload(const Buf& dst, VkDeviceSize offset, const void* data, size_t n) {
  if (!n) return;
  check(offset + n <= dst.size, "upload inside the buffer");
  Buf staging = make_buffer(n, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.map, data, n);
  submit([&](VkCommandBuffer c) { VkBufferCopy r{0, offset, n}; vkCmdCopyBuffer(c, staging.b, dst.b, 1, &r); });
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
Img make_image(uint32_t format, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, bool storage, bool array) {
  Img im; im.format = format; im.w = w; im.h = h; im.layers = layers; im.mips = mips; im.storage = storage;
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VkFormat(format); ii.extent = {w, h, 1}; ii.mipLevels = mips; ii.arrayLayers = layers;
  ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT);
  vkcheck(vkCreateImage(g.dev, &ii, nullptr, &im.image), "image");
  VkMemoryRequirements req; vkGetImageMemoryRequirements(g.dev, im.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkcheck(vkAllocateMemory(g.dev, &ai, nullptr, &im.mem), "image memory"); vkcheck(vkBindImageMemory(g.dev, im.image, im.mem, 0), "image bind");
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = im.image; vi.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D; vi.format = VkFormat(format);
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, layers};
  vkcheck(vkCreateImageView(g.dev, &vi, nullptr, &im.view), "image view");
  return im;
}
VkImageLayout layout_of(const Img& im) { return im.storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
// Fills every mip and layer from `data` (mip-major, packed), or zero when `data` is empty.
void fill_image(const Img& im, const std::vector<uint8_t>& data) {
  const uint64_t bytes = rp::image_bytes(im.format, im.w, im.h, im.layers, im.mips);
  check(data.empty() || data.size() == bytes, "image data size");
  Buf staging = make_buffer(std::max<uint64_t>(bytes, 16), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  if (!data.empty()) std::memcpy(staging.map, data.data(), data.size());
  submit([&](VkCommandBuffer c) {
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib.image = im.image;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, im.mips, 0, im.layers};
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize at = 0;
    for (uint32_t m = 0; m < im.mips; ++m) {
      const uint32_t mw = std::max(im.w >> m, 1u), mh = std::max(im.h >> m, 1u);
      VkBufferImageCopy r{}; r.bufferOffset = at; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, im.layers}; r.imageExtent = {mw, mh, 1};
      regions.push_back(r);
      at += rp::image_bytes(im.format, mw, mh, im.layers, 1);
    }
    vkCmdCopyBufferToImage(c, staging.b, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(regions.size()), regions.data());
    ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib.newLayout = layout_of(im);
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
  });
  vkDestroyBuffer(g.dev, staging.b, nullptr); vkFreeMemory(g.dev, staging.m, nullptr);
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
constexpr uint32_t kCounters = 512;   // >= core/gi.cpp kStatsCounters (331)
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

Replay replay(const fs::path& pack, const std::vector<uint32_t>* spirv_in, const fs::path& hlsl_override,
              const std::vector<std::string>& extra_defines, const fs::path& out, uint32_t repeat, bool quiet) {
  Replay result;
  rp::Manifest m; std::string error;
  { auto text = read_file(pack / "manifest.txt"); check(rp::parse(std::string(text.begin(), text.end()), m, error), error); }
  const rp::Pass& pass = *rp::find_pass(m.pass);
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
    check(slot && (slot->kind == rp::kSampled2D || slot->kind == rp::kSampled2DArray || slot->kind == rp::kStorage) && i.element < slot->count,
          "image slot " + std::to_string(i.set) + "/" + std::to_string(i.binding));
    if (slot->kind == rp::kStorage) check(i.format == slot->storage_format, std::string("storage format of ") + slot->name);
    const bool array = slot->kind == rp::kSampled2DArray, storage = slot->kind == rp::kStorage;
    const std::string k = i.file + "|" + std::to_string(i.format) + "|" + std::to_string(i.w) + "x" + std::to_string(i.h) + "x" +
                          std::to_string(i.layers) + "x" + std::to_string(i.mips) + (array ? "|a" : "") + (storage ? "|s" : "");
    auto known = by_file.find(k);
    if (known == by_file.end()) {
      Img im = make_image(i.format, i.w, i.h, i.layers, i.mips, storage, array);
      fill_image(im, file(i.file));
      known = by_file.emplace(k, im).first;
      if (i.source_format) { ++decoded; decoded_bc += rp::block_compressed(i.source_format); }
    }
    images[key(i.set, i.binding)][i.element] = known->second;
  }
  if (decoded)
    result.warnings.push_back(std::to_string(decoded) + " of " + std::to_string(by_file.size()) + " images were read by the writer's shader and stored decoded (" +
                              std::to_string(decoded_bc) + " from BCn; rp::shader_read_format)");
  if (!m.omitted.empty()) result.warnings.push_back(std::to_string(m.omitted.size()) + " image elements omitted by the writer (bound NaN)");
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
      case rp::kSampled2D: case rp::kSampled2DArray: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
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
  std::vector<std::string> missing;
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
        missing.push_back(slot.name);
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
        const Img& d = slot.kind == rp::kSampled2DArray ? (isNan ? nanArr : zeroArr) : (isNan ? nan2d : zero2d);
        ii.push_back({VK_NULL_HANDLE, d.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
      }
      if (!any && slot.count == 1) missing.push_back(slot.name);
      w.pImageInfo = ii.data();
    }
    vkUpdateDescriptorSets(g.dev, 1, &w, 0, nullptr);
  }
  if (!missing.empty()) {
    std::string s = "not in the pack (bound zero dummies):";
    for (auto& x : missing) s += " " + x;
    result.warnings.push_back(s);
  }

  // ---- pipeline, push constants, dispatch ----
  auto push = file(m.push);
  check(push.size() == pass.push_bytes, "push constant bytes");
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
  for (uint32_t run = 0; run < repeat; ++run) {
    std::memset(stats.map, 0, size_t(stats.size));
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
    result.runs.push_back(std::move(rr));
  }
  // Outputs (the last run), and the live reference comparison when the pack has one.
  for (size_t i = 0; i < outputs.size(); ++i) {
    const auto& r = result.runs.back().outputs[i];
    write_vec(out / (r.name + ".raw"), r.raw);
    write_pfm(out / (r.name + ".pfm"), r);
    if (!outputs[i].reference.empty() && r.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
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

void synthesize(const fs::path& dir, bool dark) {
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
  quad(5.0f, -10, 10, -10, 10, -1, false);        // B (instance 0, tris 0-1)
  quad(kFront, -0.6f, 0.6f, -0.6f, 0.6f, -1, false);  // F (instance 0, tris 2-3)
  const uint32_t kBase = uint32_t(soup.size());
  quad(4.5f, -0.6f, 0.6f, -0.6f, 0.6f, +1, true);   // K (instance 1, tris 0-1, with material)
  const uint32_t info[8] = {0, stride, 0, 4, kBase, stride | bg3::native_hit::nh_opaque_meta, record, 2};
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
  std::vector<float> cam2(56); cam2[12 * 4 + 1] = 1;
  std::vector<uint32_t> lflags(8); { const float m0[2] = {1, 0.5f}; std::memcpy(lflags.data(), m0, 8); lflags[4] = lflags[5] = 1; }
  const float scale[4] = {1, 0, 0, 0};
  const uint32_t clusters[4] = {0, 1, 0, 0};
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
      depth[y * W + x] = slab ? kFront : 5.0f;
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
  write_file(dir / "instances.bin", inst, sizeof inst);
  // The manifest through the writer the live capture uses (rp::serialize); the soup as two sparse
  // slices of one file, as the live writer stores the InstInfo spans (B + F, then K).
  rp::Manifest m;
  m.version = rp::kVersion; m.pass = "gi_hit_direct"; m.frame = frame;
  m.notes.push_back(" synthetic: back plane, slab front/back, one point light" + std::string(dark ? " of colour 0 (dark)" : " (lit)"));
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
  m.ranges.push_back({1, 6, kBase, "soup.bin", kBase, soup.size() - kBase});
  buf(1, 7, 32, "instinfo.bin");
  buf(1, 9, 512, "stamp.bin"); buf(1, 12, recordsBytes, "records.bin");
  buf(1, 13, basis.size(), "basis.bin");
  img(1, 2, 1, rp::kRgba32Sfloat, 1, 1, "page.rgba32f");
  img(1, 14, 1, rp::kRgba32Sfloat, 1, 1, "albedo.rgba32f"); img(1, 14, 2, rp::kRgba32Sfloat, 1, 1, "nrm.rgba32f"); img(1, 14, 3, rp::kRgba32Sfloat, 1, 1, "params.rgba32f");
  m.samplers.push_back({1, 15, 0, 1, 1, 1, 2, 2, 2, 0, 1000});
  m.instances = "instances.bin"; m.instance_count = 2;
  m.outputs.push_back({0, 8, rp::kRgba16Sfloat, W / 2, H / 2, ""}); m.outputs.push_back({0, 13, rp::kR32Sfloat, W / 2, H / 2, ""});
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
  bool validation = true, dark = false;
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
    else die("unknown argument " + args[i] + " (see tools/frame_replay.md)");
  }
  if (!test.empty()) return self_test(test);
  if (!synth.empty()) { synthesize(synth, dark); std::printf("synthetic pack written to %s\n", synth.string().c_str()); return 0; }
  check(!pack.empty(), "usage: frame_replay --pack <dir> [--out <dir>] [--hlsl <file> | --spv <file>] [--define N=V] [--repeat N]");
  if (int r = init_gpu(validation, false)) return r;
  if (out.empty()) out = pack / "replay";
  std::vector<uint32_t> code;
  if (!spv.empty()) { auto b = read_file(spv); code.resize(b.size() / 4); std::memcpy(code.data(), b.data(), code.size() * 4); }
  replay(pack, spv.empty() ? nullptr : &code, hlsl, defines, out, repeat, false);
  if (g_validation_errors.load()) { std::printf("FAIL %u validation errors\n", g_validation_errors.load()); return 1; }
  std::printf("written to %s\n", out.string().c_str());
  return 0;
}
