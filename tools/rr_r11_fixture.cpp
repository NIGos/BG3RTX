// Windowless R11G11B10 store/load oracle. Input: float words; output: six
// uint words per input (emulated RGB, actual image-load RGB). No game hooks.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <cwctype>

#define VKCHECK(x) do { auto r = (x); if (r != VK_SUCCESS) { std::fprintf(stderr, "%s: %d\n", #x, r); return 1; } } while (0)
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory;
static unsigned validation_errors;
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* info, void*) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ++validation_errors; std::fprintf(stderr, "VALIDATION %s\n", info->pMessage);
  }
  return VK_FALSE;
}
static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
  std::fprintf(stderr, "No suitable memory\n"); std::exit(1);
}
struct Buffer { VkBuffer b{}; VkDeviceMemory m{}; void* map{}; };
static Buffer buffer(VkDeviceSize size) {
  Buffer b;
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = size; ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  if (vkCreateBuffer(device, &ci, nullptr, &b.b)) std::exit(1);
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, b.b, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (vkAllocateMemory(device, &ai, nullptr, &b.m) || vkBindBufferMemory(device, b.b, b.m, 0) ||
      vkMapMemory(device, b.m, 0, size, 0, &b.map)) std::exit(1);
  return b;
}
static std::vector<uint32_t> read(const char* path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  auto size = f.tellg();
  if (!f || size <= 0 || size % 4) { std::fprintf(stderr, "Invalid input %s\n", path); std::exit(1); }
  std::vector<uint32_t> words(size_t(size) / 4); f.seekg(0);
  f.read(reinterpret_cast<char*>(words.data()), size);
  if (!f) std::exit(1);
  return words;
}
int main(int argc, char** argv) {
  // Fail before creating a Vulkan instance if any of the user's games is open.
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 1;
  PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot, &entry)) { CloseHandle(snapshot); return 1; }
  do {
    std::wstring name(entry.szExeFile);
    for (auto& c : name) c = wchar_t(towlower(c));
    if (name.find(L"bg3") != name.npos || name.find(L"stalker2") != name.npos || name.find(L"cyberpunk") != name.npos) {
      std::puts("SKIP game running"); CloseHandle(snapshot); return 77;
    }
  } while (Process32NextW(snapshot, &entry));
  CloseHandle(snapshot);
  if (argc != 5) return 1;
  // Same isolation as frame_replay/native_hit_gpu_fixture: only the explicit
  // validation layer, without installed game/ReShade implicit layers.
  SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", "~implicit~");
  SetEnvironmentVariableA("VK_INSTANCE_LAYERS", nullptr);
  SetEnvironmentVariableA("VK_LOADER_LAYERS_ENABLE", nullptr);
  auto input = read(argv[1]), store = read(argv[2]), load = read(argv[3]);
  uint32_t count = uint32_t(input.size()), height = (count + 255) / 256;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
  uint32_t layer_count = 0; VKCHECK(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
  std::vector<VkLayerProperties> layers(layer_count); VKCHECK(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));
  const char* layer = "VK_LAYER_KHRONOS_validation";
  bool validated = false;
  for (const auto& available : layers) if (std::strcmp(available.layerName, layer) == 0) validated = true;
  if (!validated) { std::puts("SKIP no validation layer"); return 77; }
  const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = &layer;
  ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = &extension;
  VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  debug.pfnUserCallback = validation; ici.pNext = &debug;
  VkInstance instance; VKCHECK(vkCreateInstance(&ici, nullptr, &instance));
  VkDebugUtilsMessengerEXT messenger;
  auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  if (!create_debug) return 1;
  VKCHECK(create_debug(instance, &debug, nullptr, &messenger));
  uint32_t n = 0; VKCHECK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
  std::vector<VkPhysicalDevice> devices(n); VKCHECK(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
  VkPhysicalDevice physical{}; uint32_t family = 0;
  for (auto candidate : devices) {
    VkPhysicalDeviceFeatures features; vkGetPhysicalDeviceFeatures(candidate, &features);
    VkFormatProperties format; vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_B10G11R11_UFLOAT_PACK32, &format);
    if (!features.shaderStorageImageExtendedFormats || !(format.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) continue;
    uint32_t qn; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn); vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qn, qs.data());
    for (uint32_t q = 0; q < qn; ++q) if (qs[q].queueFlags & VK_QUEUE_COMPUTE_BIT) { physical = candidate; family = q; break; }
    if (physical) break;
  }
  if (!physical) { std::puts("SKIP no R11 storage device"); vkDestroyInstance(instance, nullptr); return 77; }
  VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
  std::printf("GPU %s; %u boundary values\n", properties.deviceName, count);
  if (height > properties.limits.maxImageDimension2D) return 1;
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  float priority = 1;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
  VkPhysicalDeviceFeatures enabled{}; enabled.shaderStorageImageExtendedFormats = VK_TRUE;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &enabled;
  VKCHECK(vkCreateDevice(physical, &di, nullptr, &device));
  VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
  auto src = buffer(count * 4ull), dst = buffer(count * 24ull);
  std::memcpy(src.map, input.data(), count * 4ull); std::memset(dst.map, 0xcd, count * 24ull);
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32; ii.extent = {256, height, 1}; ii.mipLevels = ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = VK_IMAGE_USAGE_STORAGE_BIT;
  VkImage image; VKCHECK(vkCreateImage(device, &ii, nullptr, &image));
  VkMemoryRequirements req; vkGetImageMemoryRequirements(device, image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory image_memory; VKCHECK(vkAllocateMemory(device, &ai, nullptr, &image_memory)); VKCHECK(vkBindImageMemory(device, image, image_memory, 0));
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ii.format;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView view; VKCHECK(vkCreateImageView(device, &vi, nullptr, &view));
  VkDescriptorSetLayoutBinding bindings[] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT}, {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT}};
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount = 3; li.pBindings = bindings;
  VkDescriptorSetLayout dsl; VKCHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &dsl));
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
  VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pi.setLayoutCount = 1; pi.pSetLayouts = &dsl; pi.pushConstantRangeCount = 1; pi.pPushConstantRanges = &push;
  VkPipelineLayout layout; VKCHECK(vkCreatePipelineLayout(device, &pi, nullptr, &layout));
  VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
  VkDescriptorPoolCreateInfo pooli{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pooli.maxSets = 1; pooli.poolSizeCount = 2; pooli.pPoolSizes = sizes;
  VkDescriptorPool pool; VKCHECK(vkCreateDescriptorPool(device, &pooli, nullptr, &pool));
  VkDescriptorSetAllocateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; si.descriptorPool = pool; si.descriptorSetCount = 1; si.pSetLayouts = &dsl;
  VkDescriptorSet set; VKCHECK(vkAllocateDescriptorSets(device, &si, &set));
  VkDescriptorBufferInfo bis[] = {{src.b, 0, VK_WHOLE_SIZE}, {dst.b, 0, VK_WHOLE_SIZE}};
  VkDescriptorImageInfo im{}; im.imageView = view; im.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet writes[3]{};
  for (uint32_t i = 0; i < 3; ++i) {
    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
    writes[i].descriptorType = bindings[i].descriptorType;
    if (i < 2) writes[i].pBufferInfo = &bis[i]; else writes[i].pImageInfo = &im;
  }
  vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
  VkShaderModule modules[2]; VkPipeline pipelines[2];
  for (int i = 0; i < 2; ++i) {
    auto& code = i == 0 ? store : load;
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; sm.codeSize = code.size() * 4; sm.pCode = code.data();
    VKCHECK(vkCreateShaderModule(device, &sm, nullptr, &modules[i]));
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; cp.layout = layout;
    cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, modules[i], i == 0 ? "store" : "load"};
    VKCHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &pipelines[i]));
  }
  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.queueFamilyIndex = family;
  VkCommandPool command_pool; VKCHECK(vkCreateCommandPool(device, &cpi, nullptr, &command_pool));
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = command_pool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
  VkCommandBuffer cmd; VKCHECK(vkAllocateCommandBuffers(device, &ca, &cmd));
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; VKCHECK(vkBeginCommandBuffer(cmd, &begin));
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib.image = image; ib.subresourceRange = vi.subresourceRange; ib.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
  vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &count);
  for (int i = 0; i < 2; ++i) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[i]); vkCmdDispatch(cmd, (count + 63) / 64, 1, 1);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = i == 0 ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, i == 0 ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  }
  VKCHECK(vkEndCommandBuffer(cmd));
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
  VKCHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); VKCHECK(vkQueueWaitIdle(queue));
  std::ofstream out(argv[4], std::ios::binary); out.write(static_cast<char*>(dst.map), count * 24ull); out.close(); bool ok = bool(out);
  vkDestroyCommandPool(device, command_pool, nullptr);
  for (int i = 0; i < 2; ++i) { vkDestroyPipeline(device, pipelines[i], nullptr); vkDestroyShaderModule(device, modules[i], nullptr); }
  vkDestroyDescriptorPool(device, pool, nullptr); vkDestroyPipelineLayout(device, layout, nullptr); vkDestroyDescriptorSetLayout(device, dsl, nullptr);
  vkDestroyImageView(device, view, nullptr); vkDestroyImage(device, image, nullptr); vkFreeMemory(device, image_memory, nullptr);
  for (auto b : {src, dst}) { vkUnmapMemory(device, b.m); vkDestroyBuffer(device, b.b, nullptr); vkFreeMemory(device, b.m, nullptr); }
  vkDestroyDevice(device, nullptr);
  auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  destroy_debug(instance, messenger, nullptr); vkDestroyInstance(instance, nullptr);
  std::printf("Validation errors: %u\n", validation_errors);
  return ok && !validation_errors ? 0 : 1;
}
