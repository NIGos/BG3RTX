// Compiles a compute shader's SPIR-V on this machine's GPU and prints the driver's
// pipeline statistics (VK_KHR_pipeline_executable_properties: on NVIDIA the register
// count, shared memory and binary size), for occupancy work on the core's passes.
//
//   py -3 tools/pipeline_stats.py shaders/gi/rt_local_shadows.hlsl [...]   (compiles, reflects, runs this)
//   bin\pipeline_stats.exe <file.spv> <layout.txt>
//
// layout.txt, one descriptor per line: "<set> <binding> <VkDescriptorType number> <count>".
// The push constant range is 256 bytes (gi_common.hlsli Push). Own Vulkan 1.3 device,
// every feature the GPU supports enabled.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "vulkan-1.lib")

#define CHECK(x)                                                         \
  do {                                                                   \
    VkResult r_ = (x);                                                   \
    if (r_ != VK_SUCCESS) {                                              \
      std::fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); \
      return 1;                                                          \
    }                                                                    \
  } while (0)

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: pipeline_stats <file.spv> <layout.txt>\n");
    return 2;
  }
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<char> spv((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (spv.empty() || spv.size() % 4) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  std::map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> sets;
  {
    std::ifstream lf(argv[2]);
    std::string line;
    while (std::getline(lf, line)) {
      std::istringstream is(line);
      uint32_t set, binding, type, count;
      if (!(is >> set >> binding >> type >> count)) continue;
      sets[set].push_back({binding, VkDescriptorType(type), count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    }
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  VkInstance inst;
  CHECK(vkCreateInstance(&ici, nullptr, &inst));
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(inst, &n, nullptr);
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(inst, &n, devs.data());
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  for (VkPhysicalDevice d : devs) {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(d, &p);
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || !phys) phys = d;
  }
  if (!phys) return 1;
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phys, &props);

  // Every supported feature, so whatever the shader declares is allowed.
  VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR exe{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &exe};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
  VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &as};
  VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13};
  VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &f12};
  VkPhysicalDeviceFeatures2 feats{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f11};
  vkGetPhysicalDeviceFeatures2(phys, &feats);
  if (!exe.pipelineExecutableInfo) {
    std::fprintf(stderr, "VK_KHR_pipeline_executable_properties not supported\n");
    return 1;
  }
  const char* exts[] = {VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &feats};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = uint32_t(sizeof exts / sizeof exts[0]);
  dci.ppEnabledExtensionNames = exts;
  VkDevice dev;
  CHECK(vkCreateDevice(phys, &dci, nullptr, &dev));
  auto getProps = (PFN_vkGetPipelineExecutablePropertiesKHR)vkGetDeviceProcAddr(dev, "vkGetPipelineExecutablePropertiesKHR");
  auto getStats = (PFN_vkGetPipelineExecutableStatisticsKHR)vkGetDeviceProcAddr(dev, "vkGetPipelineExecutableStatisticsKHR");

  uint32_t max_set = sets.empty() ? 0 : sets.rbegin()->first;
  std::vector<VkDescriptorSetLayout> dsl(max_set + 1);
  for (uint32_t s = 0; s <= max_set; ++s) {
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    auto it = sets.find(s);
    if (it != sets.end()) {
      li.bindingCount = uint32_t(it->second.size());
      li.pBindings = it->second.data();
    }
    CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &dsl[s]));
  }
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 256};
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pli.setLayoutCount = uint32_t(dsl.size());
  pli.pSetLayouts = dsl.data();
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  VkPipelineLayout layout;
  CHECK(vkCreatePipelineLayout(dev, &pli, nullptr, &layout));
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = spv.size();
  mci.pCode = reinterpret_cast<const uint32_t*>(spv.data());
  VkShaderModule mod;
  CHECK(vkCreateShaderModule(dev, &mci, nullptr, &mod));
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main",
               nullptr};
  pci.layout = layout;
  VkPipeline pipe;
  CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe));

  VkPipelineInfoKHR pinfo{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
  pinfo.pipeline = pipe;
  uint32_t ne = 0;
  CHECK(getProps(dev, &pinfo, &ne, nullptr));
  std::vector<VkPipelineExecutablePropertiesKHR> ep(ne, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
  CHECK(getProps(dev, &pinfo, &ne, ep.data()));
  std::printf("%s (%s)\n", argv[1], props.deviceName);
  for (uint32_t e = 0; e < ne; ++e) {
    std::printf("  executable %u: %s, subgroup size %u\n", e, ep[e].name, ep[e].subgroupSize);
    VkPipelineExecutableInfoKHR xi{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
    xi.pipeline = pipe;
    xi.executableIndex = e;
    uint32_t ns = 0;
    CHECK(getStats(dev, &xi, &ns, nullptr));
    std::vector<VkPipelineExecutableStatisticKHR> st(ns, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
    CHECK(getStats(dev, &xi, &ns, st.data()));
    for (const auto& s : st) {
      std::printf("    %-28s ", s.name);
      switch (s.format) {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: std::printf("%s\n", s.value.b32 ? "true" : "false"); break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: std::printf("%lld\n", (long long)s.value.i64); break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: std::printf("%llu\n", (unsigned long long)s.value.u64); break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: std::printf("%g\n", s.value.f64); break;
        default: std::printf("?\n");
      }
    }
  }
  vkDestroyPipeline(dev, pipe, nullptr);
  vkDestroyShaderModule(dev, mod, nullptr);
  vkDestroyPipelineLayout(dev, layout, nullptr);
  for (VkDescriptorSetLayout l : dsl) vkDestroyDescriptorSetLayout(dev, l, nullptr);
  vkDestroyDevice(dev, nullptr);
  vkDestroyInstance(inst, nullptr);
  return 0;
}
