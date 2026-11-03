// Primary strand hair GPU check (docs/RTX_HAIR_PRIMARY.md 1b): an LSS groom on a standalone device (no window, no
// game), pixel-centre camera rays through tools/hair_primary_gpu_check.hlsl, per pixel the words the G-buffer variant
// derives from the hit. tests/hair_primary_gpu_test.py writes the input from tools/hair_groom/primary.py and compares.
//   hair_primary_gpu_check <input.bin> <check.spv> <output.bin>
// input.bin: "HPGC", version 1, points, segments, width, height, 2 x 0, then the 128-byte push block, float3 points,
// float radii, uint segment starts. output.bin: 8 uint32 per pixel. Exits 77 without linear swept spheres.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "vulkan-1.lib")

#define CHECK(x)                                                              \
  do {                                                                        \
    VkResult r_ = (x);                                                        \
    if (r_ != VK_SUCCESS) {                                                   \
      std::fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__);    \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

static VkDevice dev;
static VkPhysicalDeviceMemoryProperties memprops;
static VkQueue queue;
static VkCommandPool cpool;
struct Buf { VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; VkDeviceAddress addr = 0; void* map = nullptr; };

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags f) {
  for (uint32_t i = 0; i < memprops.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & f) == f) return i;
  std::fprintf(stderr, "no memory type\n");
  std::exit(1);
}

// Host-visible everywhere: a check, not a benchmark.
static Buf make(VkDeviceSize size, VkBufferUsageFlags usage, const void* data = nullptr) {
  Buf b;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = std::max<VkDeviceSize>(size, 16);
  bci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  CHECK(vkCreateBuffer(dev, &bci, nullptr, &b.b));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, b.b, &req);
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &fi};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  CHECK(vkAllocateMemory(dev, &mai, nullptr, &b.m));
  CHECK(vkBindBufferMemory(dev, b.b, b.m, 0));
  VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  ai.buffer = b.b;
  b.addr = vkGetBufferDeviceAddress(dev, &ai);
  CHECK(vkMapMemory(dev, b.m, 0, VK_WHOLE_SIZE, 0, &b.map));
  if (data) std::memcpy(b.map, data, size_t(size));
  return b;
}

template <class F>
static void submit(F fn) {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cpool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  CHECK(vkAllocateCommandBuffers(dev, &ai, &cmd));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  CHECK(vkBeginCommandBuffer(cmd, &bi));
  fn(cmd);
  CHECK(vkEndCommandBuffer(cmd));
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
  CHECK(vkQueueWaitIdle(queue));
  vkFreeCommandBuffers(dev, cpool, 1, &cmd);
}

static std::vector<char> read_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: hair_primary_gpu_check <input.bin> <check.spv> <output.bin>\n");
    return 2;
  }
  const std::vector<char> in = read_file(argv[1]);
  uint32_t hdr[8];
  if (in.size() < 32 + 128) { std::fprintf(stderr, "short input\n"); return 1; }
  std::memcpy(hdr, in.data(), 32);
  if (std::memcmp(hdr, "HPGC", 4) || hdr[1] != 1) { std::fprintf(stderr, "not an HPGC v1 input\n"); return 1; }
  const uint32_t np = hdr[2], ns = hdr[3], width = hdr[4], height = hdr[5];
  const size_t need = 32 + 128 + size_t(np) * 16 + size_t(ns) * 4;
  if (in.size() != need || !np || !ns || !width || !height) { std::fprintf(stderr, "bad input size\n"); return 1; }
  const char* push = in.data() + 32;
  const char* points = push + 128;
  const char* radii = points + size_t(np) * 12;
  const char* starts = radii + size_t(np) * 4;
  const std::vector<char> spv = read_file(argv[2]);
  if (spv.size() < 20 || spv.size() % 4) { std::fprintf(stderr, "bad spv\n"); return 1; }

  // ---- device ----
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  VkInstance inst;
  CHECK(vkCreateInstance(&ici, nullptr, &inst));
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(inst, &n, nullptr);
  std::vector<VkPhysicalDevice> pds(n);
  vkEnumeratePhysicalDevices(inst, &n, pds.data());
  if (pds.empty()) { std::printf("{\"skipped\":\"no Vulkan device\"}\n"); return 77; }
  VkPhysicalDevice phys = pds[0];
  for (auto p : pds) {
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(p, &pp);
    if (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys = p; break; }
  }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phys, &props);
  vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
  VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV lss_q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &lss_q};
  vkGetPhysicalDeviceFeatures2(phys, &f2);
  if (!lss_q.linearSweptSpheres) {
    std::printf("{\"gpu\":\"%s\",\"skipped\":\"linearSweptSpheres not supported\"}\n", props.deviceName);
    return 77;
  }
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
  std::vector<VkQueueFamilyProperties> fams(n);
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, fams.data());
  uint32_t fam = 0;
  for (uint32_t i = 0; i < n; ++i)
    if (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { fam = i; break; }
  float prio = 1;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = fam;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV lss{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV};
  lss.linearSweptSpheres = VK_TRUE;
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &lss};
  rq.rayQuery = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR asf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
  asf.accelerationStructure = VK_TRUE;
  VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &asf};
  v12.bufferDeviceAddress = VK_TRUE;
  const char* exts[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                        VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                        VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME};
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &v12};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 5;
  dci.ppEnabledExtensionNames = exts;
  CHECK(vkCreateDevice(phys, &dci, nullptr, &dev));
  vkGetDeviceQueue(dev, fam, 0, &queue);
  auto pCreateAS = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR"));
  auto pSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR"));
  auto pBuild = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR"));
  auto pAddr = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureDeviceAddressKHR"));
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = fam;
  CHECK(vkCreateCommandPool(dev, &pci, nullptr, &cpool));
  VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asp};
  vkGetPhysicalDeviceProperties2(phys, &p2);
  const VkDeviceSize al = asp.minAccelerationStructureScratchOffsetAlignment;

  // ---- groom BLAS (the layer's geometry: bvhsplit groom_geometry) and a one-instance TLAS ----
  const VkBufferUsageFlags as_in = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  Buf pos = make(VkDeviceSize(np) * 12, as_in, points), rad = make(VkDeviceSize(np) * 4, as_in, radii);
  Buf idx = make(VkDeviceSize(ns) * 4, as_in, starts);
  auto build = [&](VkAccelerationStructureTypeKHR type, const VkAccelerationStructureGeometryKHR& g, uint32_t prims, Buf& storage) {
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = type;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.geometryCount = 1;
    bi.pGeometries = &g;
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    pSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
    storage = make(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
    Buf scratch = make(sz.buildScratchSize + al, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = storage.b;
    ci.size = sz.accelerationStructureSize;
    ci.type = type;
    VkAccelerationStructureKHR as;
    CHECK(pCreateAS(dev, &ci, nullptr, &as));
    bi.dstAccelerationStructure = as;
    bi.scratchData.deviceAddress = (scratch.addr + al - 1) / al * al;
    VkAccelerationStructureBuildRangeInfoKHR range{prims, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
    submit([&](VkCommandBuffer cmd) { pBuild(cmd, 1, &bi, &pr); });
    return as;
  };
  VkAccelerationStructureGeometryLinearSweptSpheresDataNV d{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV};
  d.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  d.vertexData.deviceAddress = pos.addr;
  d.vertexStride = 12;
  d.radiusFormat = VK_FORMAT_R32_SFLOAT;
  d.radiusData.deviceAddress = rad.addr;
  d.radiusStride = 4;
  d.indexType = VK_INDEX_TYPE_UINT32;
  d.indexData.deviceAddress = idx.addr;
  d.indexStride = 4;
  d.indexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
  d.endCapsMode = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.pNext = &d;
  g.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
  g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  Buf blas_buf, tlas_buf;
  VkAccelerationStructureKHR blas = build(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, g, ns, blas_buf);
  VkAccelerationStructureDeviceAddressInfoKHR bai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  bai.accelerationStructure = blas;
  VkAccelerationStructureInstanceKHR ins{};
  ins.transform.matrix[0][0] = ins.transform.matrix[1][1] = ins.transform.matrix[2][2] = 1.0f;
  ins.mask = 0xFF;
  ins.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
  ins.accelerationStructureReference = pAddr(dev, &bai);
  Buf ibuf = make(sizeof ins, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, &ins);
  VkAccelerationStructureGeometryKHR tg{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  tg.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  tg.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  tg.geometry.instances.data.deviceAddress = ibuf.addr;
  VkAccelerationStructureKHR tlas = build(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tg, 1, tlas_buf);

  // ---- the check pass ----
  Buf out = make(VkDeviceSize(width) * height * 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  const VkDescriptorType types[4] = {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
  VkDescriptorSetLayoutBinding binds[4];
  for (uint32_t i = 0; i < 4; ++i) binds[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dslci.bindingCount = 4;
  dslci.pBindings = binds;
  VkDescriptorSetLayout dsl;
  CHECK(vkCreateDescriptorSetLayout(dev, &dslci, nullptr, &dsl));
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 128};
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &dsl;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
  VkPipelineLayout layout;
  CHECK(vkCreatePipelineLayout(dev, &plci, nullptr, &layout));
  VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  smci.codeSize = spv.size();
  smci.pCode = reinterpret_cast<const uint32_t*>(spv.data());
  VkShaderModule sm;
  CHECK(vkCreateShaderModule(dev, &smci, nullptr, &sm));
  VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, "main"};
  cpci.layout = layout;
  VkPipeline pipe;
  CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipe));
  VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}};
  VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 2;
  dpci.pPoolSizes = sizes;
  VkDescriptorPool dpool;
  CHECK(vkCreateDescriptorPool(dev, &dpci, nullptr, &dpool));
  VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = dpool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &dsl;
  VkDescriptorSet set;
  CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
  VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  wa.accelerationStructureCount = 1;
  wa.pAccelerationStructures = &tlas;
  const Buf* bufs[4] = {nullptr, &pos, &idx, &out};
  VkDescriptorBufferInfo bi[4];
  VkWriteDescriptorSet w[4];
  for (uint32_t i = 0; i < 4; ++i) {
    w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, i, 0, 1, types[i]};
    if (i == 0) {
      w[i].pNext = &wa;
    } else {
      bi[i] = {bufs[i]->b, 0, VK_WHOLE_SIZE};
      w[i].pBufferInfo = &bi[i];
    }
  }
  vkUpdateDescriptorSets(dev, 4, w, 0, nullptr);
  submit([&](VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, push);
    vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
  });
  std::ofstream o(argv[3], std::ios::binary);
  o.write(static_cast<const char*>(out.map), std::streamsize(size_t(width) * height * 32));
  if (!o) { std::fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
  std::printf("{\"gpu\":\"%s\",\"points\":%u,\"segments\":%u,\"width\":%u,\"height\":%u}\n", props.deviceName, np, ns,
              width, height);
  return 0;   // the process exit releases the device objects
}
