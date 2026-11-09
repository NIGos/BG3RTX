// Windowless GPU fixture of scene handout increment 1 (CTest scene_handout_gpu_fixture,
// docs/SCENE_HANDOUT_DESIGN.md 3.2 and 5). Uses the layer's own policy (common/scene_handout_layout.h):
//   1. the empty dummy TLAS (EmptyTlas) builds and traces as "no hit";
//   2. a set laid out like the material set 1 (stamp b9, 4096 alpha views b2, immutable sampler b4, OMM reference
//      TLAS b10) with uab_binding_flags is bound into a recorded command buffer, then rewritten (stamp, one view,
//      the TLAS) after the recording ended: the dispatch must see the rewritten descriptors;
//   3. the fallback of increment 3: set P copied into the bound set R before the submit (vkUpdateDescriptorSets
//      copies) must be what the dispatch sees.
// Zero Khronos validation errors are required. Private loader (never the game's implicit layers), no window, no
// game. Exit 77 (CTest SKIP_RETURN_CODE): no validation layer or no device with the update-after-bind features.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#pragma comment(lib, "vulkan-1.lib")
#include "../common/scene_handout_layout.h"

namespace {
VkDevice dev;
VkPhysicalDeviceMemoryProperties memory;
std::atomic<unsigned> validationErrors{};
void check(bool yes, const char* what) {
  if (!yes) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(1); }
}
void vkcheck(VkResult r, const char* what) {
  if (r != VK_SUCCESS) { std::fprintf(stderr, "FAIL %s result=%d\n", what, r); std::exit(1); }
}
VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                          const VkDebugUtilsMessengerCallbackDataEXT* info, void*) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    if (validationErrors.fetch_add(1) < 16) std::fprintf(stderr, "VALIDATION %s\n", info->pMessage);
  return VK_FALSE;
}
uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
  check(false, "memory type");
  return 0;
}
struct Buffer {
  VkBuffer b{}; VkDeviceMemory m{}; VkDeviceAddress address{}; uint32_t* map{};
  void destroy() { if (map) vkUnmapMemory(dev, m); if (b) vkDestroyBuffer(dev, b, nullptr); if (m) vkFreeMemory(dev, m, nullptr); *this = {}; }
};
void make_buffer(Buffer& out, VkDeviceSize bytes, VkBufferUsageFlags usage, bool host) {
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = bytes; ci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  vkcheck(vkCreateBuffer(dev, &ci, nullptr, &out.b), "buffer");
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, out.b, &req);
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkcheck(vkAllocateMemory(dev, &ai, nullptr, &out.m), "buffer memory");
  vkcheck(vkBindBufferMemory(dev, out.b, out.m, 0), "bind buffer");
  VkBufferDeviceAddressInfo bai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; bai.buffer = out.b;
  out.address = vkGetBufferDeviceAddress(dev, &bai);
  if (host) { void* p{}; vkcheck(vkMapMemory(dev, out.m, 0, VK_WHOLE_SIZE, 0, &p), "map"); out.map = static_cast<uint32_t*>(p); std::memset(p, 0, size_t(bytes)); }
}
struct Image { VkImage i{}; VkDeviceMemory m{}; VkImageView v{}; };
}  // namespace

int main(int argc, char** argv) {
  check(argc == 2, "usage: scene_handout_gpu_fixture scene_handout_gpu_fixture.spv");
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", "~implicit~");
  SetEnvironmentVariableA("VK_INSTANCE_LAYERS", nullptr);
  SetEnvironmentVariableA("VK_LOADER_LAYERS_ENABLE", nullptr);
  SetEnvironmentVariableA("VK_ADD_LAYER_PATH", nullptr);
  uint32_t nl = 0;
  if (vkEnumerateInstanceLayerProperties(&nl, nullptr) != VK_SUCCESS) { std::puts("SKIP no Vulkan loader"); return 77; }
  std::vector<VkLayerProperties> layers(nl);
  vkcheck(vkEnumerateInstanceLayerProperties(&nl, layers.data()), "layers");
  bool haveValidation = false;
  for (auto& x : layers) haveValidation |= !std::strcmp(x.layerName, "VK_LAYER_KHRONOS_validation");
  if (!haveValidation) { std::puts("SKIP Khronos validation layer not installed (never a silent pass)"); return 77; }
  const char* layer = "VK_LAYER_KHRONOS_validation";
  const char* debugExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
  debug.pfnUserCallback = validation;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "scene handout fixture"; app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, &debug};
  ici.pApplicationInfo = &app; ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = &layer;
  ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = &debugExt;
  VkInstance instance;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) { std::puts("SKIP no Vulkan 1.3 instance"); return 77; }
  auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  auto destroyDebug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  VkDebugUtilsMessengerEXT messenger;
  vkcheck(createDebug(instance, &debug, nullptr, &messenger), "debug messenger");
  auto skip = [&](const char* why) { std::printf("SKIP %s\n", why); destroyDebug(instance, messenger, nullptr); vkDestroyInstance(instance, nullptr); return 77; };

  // The features the layer asks for (rtscene.cpp handout_features) plus what this fixture needs.
  uint32_t np = 0;
  vkEnumeratePhysicalDevices(instance, &np, nullptr);
  std::vector<VkPhysicalDevice> physical(np);
  if (np) vkEnumeratePhysicalDevices(instance, &np, physical.data());
  VkPhysicalDevice gpu = VK_NULL_HANDLE;
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  VkPhysicalDeviceDescriptorIndexingFeatures di{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &bda};
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &di};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &as};
  for (auto p : physical) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(p, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) continue;
    vkGetPhysicalDeviceFeatures2(p, &features);
    if (as.accelerationStructure && rq.rayQuery && bda.bufferDeviceAddress && bg3::scene_handout::uab_supported(di, as)) { gpu = p; break; }
  }
  if (!gpu) return skip("no Vulkan 1.3 device with ray queries and the update-after-bind features (not passed)");
  VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(gpu, &props);
  vkGetPhysicalDeviceMemoryProperties(gpu, &memory);
  VkPhysicalDeviceDescriptorIndexingProperties dip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
  VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &dip};
  vkGetPhysicalDeviceProperties2(gpu, &p2);
  std::printf("GPU=%s driver=%u implicit_layers=disabled validation=1 window=none uab_sampled_images_per_stage=%u\n",
              props.deviceName, props.driverVersion, dip.maxPerStageDescriptorUpdateAfterBindSampledImages);
  check(dip.maxPerStageDescriptorUpdateAfterBindSampledImages >= 4096 + 16 + 64, "UAB sampled image limit below set 1's arrays");
  uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, nullptr);
  std::vector<VkQueueFamilyProperties> families(nq); vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, families.data());
  uint32_t family = UINT32_MAX;
  for (uint32_t i = 0; i < nq; ++i) if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
  check(family != UINT32_MAX, "compute queue");
  const char* extensions[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  VkPhysicalDeviceBufferDeviceAddressFeatures useBda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  useBda.bufferDeviceAddress = VK_TRUE;
  VkPhysicalDeviceDescriptorIndexingFeatures useDi{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &useBda};
  useDi.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
  useDi.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  VkPhysicalDeviceRayQueryFeaturesKHR useRq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &useDi}; useRq.rayQuery = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR useAs{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &useRq};
  useAs.accelerationStructure = VK_TRUE;
  useAs.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
  float priority = 0.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &useAs};
  dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 3; dci.ppEnabledExtensionNames = extensions;
  vkcheck(vkCreateDevice(gpu, &dci, nullptr, &dev), "device");
  VkQueue queue; vkGetDeviceQueue(dev, family, 0, &queue);
  auto getSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR"));
  auto createAs = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR"));
  auto destroyAs = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(dev, "vkDestroyAccelerationStructureKHR"));
  auto cmdBuildAs = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR"));
  check(getSizes && createAs && destroyAs && cmdBuildAs, "acceleration structure entry points");

  VkCommandPool cpool;
  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpci.queueFamilyIndex = family;
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vkcheck(vkCreateCommandPool(dev, &cpci, nullptr, &cpool), "command pool");
  VkCommandBuffer cmd[3];
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = cpool; cai.commandBufferCount = 3;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  vkcheck(vkAllocateCommandBuffers(dev, &cai, cmd), "command buffers");
  VkFence fence; VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkcheck(vkCreateFence(dev, &fci, nullptr, &fence), "fence");
  auto begin = [&](VkCommandBuffer c) { VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkcheck(vkBeginCommandBuffer(c, &bi), "begin"); };
  auto submit = [&](VkCommandBuffer c) {
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &c;
    vkcheck(vkQueueSubmit(queue, 1, &si, fence), "submit");
    vkcheck(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10'000'000'000ull), "wait (device loss: never rerun, report)");
    vkcheck(vkResetFences(dev, 1, &fence), "reset fence");
  };

  // 1. The dummy TLAS, built exactly as layer/rtscene.cpp ensure_dummy_tlas does.
  Buffer inst, tlasBuf, scratch;
  bg3::scene_handout::EmptyTlas probe;
  uint32_t maxInstances = bg3::scene_handout::EmptyTlas::kMaxInstances;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  getSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &probe.build, &maxInstances, &sz);
  VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  VkPhysicalDeviceProperties2 pp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asp};
  vkGetPhysicalDeviceProperties2(gpu, &pp);
  const VkDeviceSize align = asp.minAccelerationStructureScratchOffsetAlignment ? asp.minAccelerationStructureScratchOffsetAlignment : 256;
  make_buffer(inst, bg3::scene_handout::EmptyTlas::kInstanceBytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, false);
  make_buffer(tlasBuf, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
  make_buffer(scratch, sz.buildScratchSize + align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
  VkAccelerationStructureKHR tlas;
  VkAccelerationStructureCreateInfoKHR asci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  asci.buffer = tlasBuf.b; asci.size = sz.accelerationStructureSize; asci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  vkcheck(createAs(dev, &asci, nullptr, &tlas), "dummy TLAS");

  // Two 1x1 views (values 0.25 and 0.75), cleared and made shader-readable in the same setup command buffer.
  Image img[2];
  const float values[2] = {0.25f, 0.75f};
  for (auto& im : img) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R32_SFLOAT; ii.extent = {1, 1, 1}; ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    vkcheck(vkCreateImage(dev, &ii, nullptr, &im.i), "image");
    VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, im.i, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkcheck(vkAllocateMemory(dev, &ai, nullptr, &im.m), "image memory");
    vkcheck(vkBindImageMemory(dev, im.i, im.m, 0), "bind image");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = im.i; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R32_SFLOAT; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkcheck(vkCreateImageView(dev, &vi, nullptr, &im.v), "image view");
  }
  begin(cmd[0]);
  {
    bg3::scene_handout::EmptyTlas e(inst.address);
    e.build.dstAccelerationStructure = tlas;
    e.build.scratchData.deviceAddress = (scratch.address + align - 1) & ~(align - 1);
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &e.range;
    cmdBuildAs(cmd[0], 1, &e.build, &ranges);
    for (int k = 0; k < 2; ++k) {
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img[k].i; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd[0], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
      VkClearColorValue c{}; c.float32[0] = values[k];
      vkCmdClearColorImage(cmd[0], img[k].i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &b.subresourceRange);
      b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      vkCmdPipelineBarrier(cmd[0], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR; mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd[0], VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  }
  vkcheck(vkEndCommandBuffer(cmd[0]), "end setup");
  submit(cmd[0]);

  // 2. Set 0 (output, ordinary) and the set 1 look-alike with the layer's UAB policy.
  VkSampler sampler;
  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sci.magFilter = sci.minFilter = VK_FILTER_LINEAR; sci.maxLod = 16.0f;
  vkcheck(vkCreateSampler(dev, &sci, nullptr, &sampler), "sampler");
  VkDescriptorSetLayoutBinding outBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo l0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; l0.bindingCount = 1; l0.pBindings = &outBinding;
  VkDescriptorSetLayout dsl0, dsl1;
  vkcheck(vkCreateDescriptorSetLayout(dev, &l0, nullptr, &dsl0), "set 0 layout");
  constexpr uint32_t kTextures = 4096;   // layer/alphatest.cpp kTextures
  const VkDescriptorSetLayoutBinding b1[4] = {{9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kTextures, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sampler},
                                              {10, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
  VkDescriptorBindingFlags f1[4];
  bg3::scene_handout::uab_binding_flags(b1, 4, f1);
  check(f1[0] && f1[1] && !f1[2] && f1[3], "uab_binding_flags: written bindings UAB, immutable sampler not");
  VkDescriptorSetLayoutBindingFlagsCreateInfo bf{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO}; bf.bindingCount = 4; bf.pBindingFlags = f1;
  VkDescriptorSetLayoutCreateInfo l1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &bf};
  l1.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT; l1.bindingCount = 4; l1.pBindings = b1;
  vkcheck(vkCreateDescriptorSetLayout(dev, &l1, nullptr, &dsl1), "set 1 UAB layout");
  VkDescriptorPoolSize s0{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo p0{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; p0.maxSets = 1; p0.poolSizeCount = 1; p0.pPoolSizes = &s0;
  VkDescriptorPoolSize s1[4] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2 * kTextures},
                                {VK_DESCRIPTOR_TYPE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2}};
  VkDescriptorPoolCreateInfo p1{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; p1.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  p1.maxSets = 2; p1.poolSizeCount = 4; p1.pPoolSizes = s1;
  VkDescriptorPool pool0, pool1;
  vkcheck(vkCreateDescriptorPool(dev, &p0, nullptr, &pool0), "pool 0");
  vkcheck(vkCreateDescriptorPool(dev, &p1, nullptr, &pool1), "pool 1 (UAB)");
  VkDescriptorSet set0, setR, setP;
  VkDescriptorSetAllocateInfo a0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a0.descriptorPool = pool0; a0.descriptorSetCount = 1; a0.pSetLayouts = &dsl0;
  vkcheck(vkAllocateDescriptorSets(dev, &a0, &set0), "set 0");
  VkDescriptorSetLayout two[2] = {dsl1, dsl1}; VkDescriptorSet sets[2];
  VkDescriptorSetAllocateInfo a1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a1.descriptorPool = pool1; a1.descriptorSetCount = 2; a1.pSetLayouts = two;
  vkcheck(vkAllocateDescriptorSets(dev, &a1, sets), "sets R and P");
  setR = sets[0]; setP = sets[1];

  Buffer out, stamp[3];
  make_buffer(out, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
  const uint32_t stampWords[3][2] = {{1u, 1u}, {2u, 2u}, {3u, 3u}};   // {value, view index}
  for (int k = 0; k < 3; ++k) { make_buffer(stamp[k], 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true); std::memcpy(stamp[k].map, stampWords[k], 8); }
  VkDescriptorBufferInfo outInfo{out.b, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w0.dstSet = set0; w0.dstBinding = 0; w0.descriptorCount = 1;
  w0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w0.pBufferInfo = &outInfo;
  vkUpdateDescriptorSets(dev, 1, &w0, 0, nullptr);
  // Writes the whole set: the stamp, every view = img[0] except `special` = img[1], the TLAS.
  auto write_set = [&](VkDescriptorSet s, const Buffer& st, uint32_t special) {
    std::vector<VkDescriptorImageInfo> views(kTextures, VkDescriptorImageInfo{VK_NULL_HANDLE, img[0].v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    views[special].imageView = img[1].v;
    VkDescriptorBufferInfo si{st.b, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSetAccelerationStructureKHR asw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asw.accelerationStructureCount = 1; asw.pAccelerationStructures = &tlas;
    VkWriteDescriptorSet w[3]{};
    for (auto& x : w) { x = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; x.dstSet = s; x.descriptorCount = 1; }
    w[0].dstBinding = 9; w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[0].pBufferInfo = &si;
    w[1].dstBinding = 2; w[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[1].descriptorCount = kTextures; w[1].pImageInfo = views.data();
    w[2].dstBinding = 10; w[2].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; w[2].pNext = &asw;
    vkUpdateDescriptorSets(dev, 3, w, 0, nullptr);
  };

  std::ifstream f(argv[1], std::ios::binary);
  check(bool(f), "SPIR-V file");
  std::vector<char> spv((std::istreambuf_iterator<char>(f)), {});
  check(!spv.empty() && spv.size() % 4 == 0, "SPIR-V size");
  VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = spv.size(); smi.pCode = reinterpret_cast<const uint32_t*>(spv.data());
  VkShaderModule module; vkcheck(vkCreateShaderModule(dev, &smi, nullptr, &module), "shader module");
  VkDescriptorSetLayout both[2] = {dsl0, dsl1};
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 2; pli.pSetLayouts = both;
  VkPipelineLayout pl; vkcheck(vkCreatePipelineLayout(dev, &pli, nullptr, &pl), "pipeline layout");
  VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
  cpi.layout = pl;
  VkPipeline pipe; vkcheck(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipe), "pipeline");
  auto record = [&](VkCommandBuffer c, VkDescriptorSet s1) {
    begin(c);
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    VkDescriptorSet bound[2] = {set0, s1};
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 2, bound, 0, nullptr);
    vkCmdDispatch(c, 1, 1, 1);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    vkcheck(vkEndCommandBuffer(c), "end");
  };
  auto expect = [&](const char* what, uint32_t value, float view) {
    float seen; std::memcpy(&seen, out.map + 1, 4);
    std::printf("%s: stamp %u (want %u), view %.2f (want %.2f), empty TLAS no-hit %u, sampler %s\n", what, out.map[0], value,
                double(seen), double(view), out.map[2], out.map[3] == 0xC0FFEEu ? "ok" : "BAD");
    check(out.map[0] == value && seen == view && out.map[2] == 1u && out.map[3] == 0xC0FFEEu, what);
    std::memset(out.map, 0, 16);
  };

  // A: update after bind. R holds stamp 0 (view 1 special) when bound; rewritten to stamp 1 (view 2 special) and
  // the TLAS again after the recording ended, before the submit.
  write_set(setR, stamp[0], 1);
  record(cmd[1], setR);
  write_set(setR, stamp[1], 2);
  submit(cmd[1]);
  expect("update after bind", 2u, values[1]);
  // Again the other way (the readback is not stale: each run sees the descriptors written after its bind).
  record(cmd[1], setR);
  write_set(setR, stamp[0], 1);
  submit(cmd[1]);
  expect("second update after bind", 1u, values[1]);
  // B: the increment-3 fallback, set P copied into the bound R before the submit (every binding but the immutable sampler).
  write_set(setP, stamp[2], 3);
  write_set(setR, stamp[1], 2);
  record(cmd[2], setR);
  VkCopyDescriptorSet copies[3]{};
  const uint32_t copied[3][2] = {{9, 1}, {2, kTextures}, {10, 1}};
  for (int k = 0; k < 3; ++k) {
    copies[k] = {VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET}; copies[k].srcSet = setP; copies[k].dstSet = setR;
    copies[k].srcBinding = copies[k].dstBinding = copied[k][0]; copies[k].descriptorCount = copied[k][1];
  }
  vkUpdateDescriptorSets(dev, 0, nullptr, 3, copies);
  submit(cmd[2]);
  expect("fallback copy P -> R after bind", 3u, values[1]);

  vkDeviceWaitIdle(dev);
  vkDestroyPipeline(dev, pipe, nullptr); vkDestroyPipelineLayout(dev, pl, nullptr); vkDestroyShaderModule(dev, module, nullptr);
  vkDestroyDescriptorPool(dev, pool0, nullptr); vkDestroyDescriptorPool(dev, pool1, nullptr);
  vkDestroyDescriptorSetLayout(dev, dsl0, nullptr); vkDestroyDescriptorSetLayout(dev, dsl1, nullptr);
  vkDestroySampler(dev, sampler, nullptr);
  for (auto& im : img) { vkDestroyImageView(dev, im.v, nullptr); vkDestroyImage(dev, im.i, nullptr); vkFreeMemory(dev, im.m, nullptr); }
  destroyAs(dev, tlas, nullptr);
  for (Buffer* b : {&inst, &tlasBuf, &scratch, &out, &stamp[0], &stamp[1], &stamp[2]}) b->destroy();
  vkDestroyFence(dev, fence, nullptr); vkDestroyCommandPool(dev, cpool, nullptr);
  vkDestroyDevice(dev, nullptr);
  destroyDebug(instance, messenger, nullptr); vkDestroyInstance(instance, nullptr);
  if (validationErrors) { std::fprintf(stderr, "FAIL %u validation errors\n", validationErrors.load()); return 1; }
  std::puts("PASS scene handout layout: UAB set 1 look-alike, update after bind, fallback copy, empty dummy TLAS; 0 validation errors");
  return 0;
}
