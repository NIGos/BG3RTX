// Hair cost gate: the game's alpha-tested hair cards vs a linear-swept-sphere (LSS) groom
// (docs/RTX_HAIR_GROOM_EXPERIMENT.md). Standalone Vulkan device, no window, no game.
//   hair_lss_bench <hairbench.bin> <spv dir> [repeats] [rays]
// hairbench.bin: python -m tools.hair_groom.bench_data. SPIR-V: tools/hair_lss_bench.hlsl, entry
// points trace_opaque / trace_alpha / deform / update (DXC cs_6_6, Vulkan 1.3), one .spv each.
// Per scene (cards with the 3-sample candidate test, cards opaque, groom per segment length):
// BLAS build / refit ms and size, ray-query ms of shadow, AO and GI batches (same rays, origins
// on the cards), hit share, candidates per ray. Per frame: card deform, groom point update and
// refit ms. Exits 77 when VK_NV_ray_tracing_linear_swept_spheres is missing. JSON on stdout.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
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
static VkPhysicalDevice phys;
static VkPhysicalDeviceMemoryProperties memprops;
static VkQueue queue;
static VkCommandPool pool;
static VkQueryPool qp;
static float ts_period;
static VkDeviceSize scratch_align;
static PFN_vkCreateAccelerationStructureKHR pCreateAS;
static PFN_vkDestroyAccelerationStructureKHR pDestroyAS;
static PFN_vkGetAccelerationStructureBuildSizesKHR pSizes;
static PFN_vkCmdBuildAccelerationStructuresKHR pBuild;
static PFN_vkGetAccelerationStructureDeviceAddressKHR pASAddr;

struct Buf { VkBuffer b; VkDeviceMemory m; VkDeviceAddress addr; void* map; VkDeviceSize size; };

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags f) {
  for (uint32_t i = 0; i < memprops.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & f) == f) return i;
  std::fprintf(stderr, "no memory type\n");
  std::exit(1);
}

static Buf make(VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
  Buf b{};
  b.size = std::max<VkDeviceSize>(size, 16);
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = b.size;
  bci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  CHECK(vkCreateBuffer(dev, &bci, nullptr, &b.b));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, b.b, &req);
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &fi};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mem_type(req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                          : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  CHECK(vkAllocateMemory(dev, &mai, nullptr, &b.m));
  CHECK(vkBindBufferMemory(dev, b.b, b.m, 0));
  VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  ai.buffer = b.b;
  b.addr = vkGetBufferDeviceAddress(dev, &ai);
  if (host) CHECK(vkMapMemory(dev, b.m, 0, VK_WHOLE_SIZE, 0, &b.map));
  return b;
}

static void drop(Buf& b) {
  if (b.b) vkDestroyBuffer(dev, b.b, nullptr);
  if (b.m) vkFreeMemory(dev, b.m, nullptr);
  b = {};
}

// Records `fn` between two timestamps, submits, waits; returns GPU ms.
template <class F>
static double timed(F fn) {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  CHECK(vkAllocateCommandBuffers(dev, &ai, &cmd));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  CHECK(vkBeginCommandBuffer(cmd, &bi));
  vkCmdResetQueryPool(cmd, qp, 0, 2);
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 0);
  fn(cmd);
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1);
  CHECK(vkEndCommandBuffer(cmd));
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
  CHECK(vkQueueWaitIdle(queue));
  uint64_t t[2];
  CHECK(vkGetQueryPoolResults(dev, qp, 0, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
  vkFreeCommandBuffers(dev, pool, 1, &cmd);
  return double(t[1] - t[0]) * ts_period * 1e-6;
}

static double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v.empty() ? 0 : v[v.size() / 2];
}

static void barrier(VkCommandBuffer cmd) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                     VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_READ_BIT;
  const VkPipelineStageFlags st = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
  vkCmdPipelineBarrier(cmd, st, st, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

static Buf upload(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
  Buf staging = make(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.map, data, size_t(size));
  Buf b = make(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  timed([&](VkCommandBuffer cmd) {
    VkBufferCopy c{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.b, b.b, 1, &c);
  });
  drop(staging);
  return b;
}

// A bottom- or top-level acceleration structure with its own storage and scratch.
struct AS {
  VkAccelerationStructureKHR as = VK_NULL_HANDLE;
  Buf storage, scratch;
  VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  VkAccelerationStructureBuildRangeInfoKHR range{};
  VkDeviceSize size = 0;
  void create(VkAccelerationStructureTypeKHR type, VkBuildAccelerationStructureFlagsKHR flags, uint32_t prims) {
    info.type = type;
    info.flags = flags;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1;
    info.pGeometries = &geom;
    range.primitiveCount = prims;
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    pSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &prims, &sz);
    size = sz.accelerationStructureSize;
    storage = make(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    scratch = make(std::max(sz.buildScratchSize, sz.updateScratchSize) + scratch_align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = storage.b;
    ci.size = sz.accelerationStructureSize;
    ci.type = type;
    CHECK(pCreateAS(dev, &ci, nullptr, &as));
    info.dstAccelerationStructure = as;
    info.scratchData.deviceAddress = (scratch.addr + scratch_align - 1) & ~(scratch_align - 1);
  }
  void record(VkCommandBuffer cmd, bool update) {
    info.pGeometries = &geom;
    info.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.srcAccelerationStructure = update ? as : VK_NULL_HANDLE;
    const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
    pBuild(cmd, 1, &info, &pr);
  }
  VkDeviceAddress address() const {
    VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    ai.accelerationStructure = as;
    return pASAddr(dev, &ai);
  }
  void destroy() {
    if (as) pDestroyAS(dev, as, nullptr);
    drop(storage);
    drop(scratch);
    as = VK_NULL_HANDLE;
  }
};

static std::vector<uint32_t> read_spv(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (b.size() < 20 || b.size() % 4) { std::fprintf(stderr, "bad spv %s\n", path.c_str()); std::exit(1); }
  std::vector<uint32_t> w(b.size() / 4);
  std::memcpy(w.data(), b.data(), b.size());
  return w;
}

struct Push { uint32_t count, kind, seed, triangles; float time, light[3]; };

struct Groom { uint32_t step, points, segments, strands; Buf pos, radii, starts, tri, bary; };

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: hair_lss_bench <hairbench.bin> <spv dir> [repeats] [rays]\n");
    return 2;
  }
  const int repeats = argc > 3 ? std::max(1, atoi(argv[3])) : 9;
  const uint32_t rays = argc > 4 ? uint32_t(std::max(64, atoi(argv[4]))) : 2560u * 1440u;
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<char> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  uint32_t hdr[8];
  if (file.size() < 32) { std::fprintf(stderr, "short file\n"); return 1; }
  std::memcpy(hdr, file.data(), 32);
  if (std::memcmp(hdr, "HLSB", 4) || hdr[1] != 1) { std::fprintf(stderr, "not a hairbench.bin v1\n"); return 1; }
  const uint32_t nv = hdr[2], nt = hdr[3], mw = hdr[4], mh = hdr[5], ng = hdr[6];
  size_t at = 32;
  auto take = [&](size_t bytes) {
    if (at + bytes > file.size()) { std::fprintf(stderr, "truncated\n"); std::exit(1); }
    const char* p = file.data() + at;
    at += bytes;
    return p;
  };
  const char* card_pos = take(size_t(nv) * 12);
  const char* card_uv = take(size_t(nv) * 8);
  const char* card_tri = take(size_t(nt) * 12);
  const char* mask = take(size_t(mw) * mh * 4);

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
  phys = pds[0];
  for (auto p : pds) {
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(p, &pp);
    if (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys = p; break; }
  }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phys, &props);
  ts_period = props.limits.timestampPeriod;
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
    if (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT && fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { fam = i; break; }
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
#define LOAD(var, name) var = reinterpret_cast<decltype(var)>(vkGetDeviceProcAddr(dev, name))
  LOAD(pCreateAS, "vkCreateAccelerationStructureKHR");
  LOAD(pDestroyAS, "vkDestroyAccelerationStructureKHR");
  LOAD(pSizes, "vkGetAccelerationStructureBuildSizesKHR");
  LOAD(pBuild, "vkCmdBuildAccelerationStructuresKHR");
  LOAD(pASAddr, "vkGetAccelerationStructureDeviceAddressKHR");
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = fam;
  CHECK(vkCreateCommandPool(dev, &pci, nullptr, &pool));
  VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qpi.queryCount = 2;
  CHECK(vkCreateQueryPool(dev, &qpi, nullptr, &qp));
  VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asp};
  vkGetPhysicalDeviceProperties2(phys, &p2);
  scratch_align = asp.minAccelerationStructureScratchOffsetAlignment;

  // ---- inputs ----
  const VkBufferUsageFlags as_in = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  Buf rest = upload(card_pos, VkDeviceSize(nv) * 12, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  Buf cpos = upload(card_pos, VkDeviceSize(nv) * 12, as_in);
  Buf ctri = upload(card_tri, VkDeviceSize(nt) * 12, as_in);
  Buf cuv = upload(card_uv, VkDeviceSize(nv) * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  Buf counters = make(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  Buf readback = make(16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  std::vector<Groom> grooms(ng);
  for (auto& g : grooms) {
    uint32_t gh[4];
    std::memcpy(gh, take(16), 16);
    g.step = gh[0]; g.points = gh[1]; g.segments = gh[2]; g.strands = gh[3];
    g.pos = upload(take(size_t(g.points) * 12), VkDeviceSize(g.points) * 12, as_in);
    g.radii = upload(take(size_t(g.points) * 4), VkDeviceSize(g.points) * 4, as_in);
    g.starts = upload(take(size_t(g.segments) * 4), VkDeviceSize(g.segments) * 4, as_in);
    g.tri = upload(take(size_t(g.points) * 4), VkDeviceSize(g.points) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    g.bary = upload(take(size_t(g.points) * 12), VkDeviceSize(g.points) * 12, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  }

  // Mask texture with a full mip chain (the candidate test reads mips 0..2).
  uint32_t mips = 1;
  while ((std::max(mw, mh) >> mips) > 0) ++mips;
  VkImageCreateInfo imci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imci.imageType = VK_IMAGE_TYPE_2D;
  imci.format = VK_FORMAT_R8G8B8A8_UNORM;
  imci.extent = {mw, mh, 1};
  imci.mipLevels = mips;
  imci.arrayLayers = 1;
  imci.samples = VK_SAMPLE_COUNT_1_BIT;
  imci.tiling = VK_IMAGE_TILING_OPTIMAL;
  imci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImage image;
  CHECK(vkCreateImage(dev, &imci, nullptr, &image));
  VkMemoryRequirements ireq;
  vkGetImageMemoryRequirements(dev, image, &ireq);
  VkMemoryAllocateInfo imai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  imai.allocationSize = ireq.size;
  imai.memoryTypeIndex = mem_type(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory imem;
  CHECK(vkAllocateMemory(dev, &imai, nullptr, &imem));
  CHECK(vkBindImageMemory(dev, image, imem, 0));
  {
    Buf staging = make(VkDeviceSize(mw) * mh * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.map, mask, size_t(mw) * mh * 4);
    timed([&](VkCommandBuffer cmd) {
      auto layout = [&](uint32_t mip, VkImageLayout from, VkImageLayout to) {
        VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        ib.oldLayout = from;
        ib.newLayout = to;
        ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = image;
        ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
      };
      for (uint32_t m = 0; m < mips; ++m) layout(m, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkBufferImageCopy c{};
      c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      c.imageExtent = {mw, mh, 1};
      vkCmdCopyBufferToImage(cmd, staging.b, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
      for (uint32_t m = 1; m < mips; ++m) {
        layout(m - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkImageBlit bl{};
        bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1};
        bl.srcOffsets[1] = {int32_t(std::max(1u, mw >> (m - 1))), int32_t(std::max(1u, mh >> (m - 1))), 1};
        bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
        bl.dstOffsets[1] = {int32_t(std::max(1u, mw >> m)), int32_t(std::max(1u, mh >> m)), 1};
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
        layout(m - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      layout(mips - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    drop(staging);
  }
  VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  ivci.image = image;
  ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
  ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
  VkImageView view;
  CHECK(vkCreateImageView(dev, &ivci, nullptr, &view));
  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
  sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.maxLod = float(mips);
  VkSampler sampler;
  CHECK(vkCreateSampler(dev, &sci, nullptr, &sampler));

  // ---- pipelines: one layout, one descriptor set re-pointed per scene ----
  const VkDescriptorType types[11] = {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
  VkDescriptorSetLayoutBinding binds[11];
  for (uint32_t i = 0; i < 11; ++i) binds[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dslci.bindingCount = 11;
  dslci.pBindings = binds;
  VkDescriptorSetLayout dsl;
  CHECK(vkCreateDescriptorSetLayout(dev, &dslci, nullptr, &dsl));
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &dsl;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
  VkPipelineLayout layout;
  CHECK(vkCreatePipelineLayout(dev, &plci, nullptr, &layout));
  auto pipeline = [&](const char* entry) {
    auto code = read_spv(std::string(argv[2]) + "/" + entry + ".spv");
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = code.size() * 4;
    smci.pCode = code.data();
    VkShaderModule sm;
    CHECK(vkCreateShaderModule(dev, &smci, nullptr, &sm));
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, entry};
    cpci.layout = layout;
    VkPipeline p;
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, nullptr, &p));
    vkDestroyShaderModule(dev, sm, nullptr);
    return p;
  };
  VkPipeline p_opaque = pipeline("trace_opaque"), p_alpha = pipeline("trace_alpha"), p_deform = pipeline("deform"),
             p_update = pipeline("update");
  VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8},
                                  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
  VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 4;
  dpci.pPoolSizes = sizes;
  VkDescriptorPool dpool;
  CHECK(vkCreateDescriptorPool(dev, &dpci, nullptr, &dpool));
  VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = dpool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &dsl;
  VkDescriptorSet set;
  CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
  auto bind = [&](VkAccelerationStructureKHR tlas, const Groom* g) {
    VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    wa.accelerationStructureCount = 1;
    wa.pAccelerationStructures = &tlas;
    const Buf* bufs[11] = {nullptr, &cpos, &ctri, &cuv, nullptr, nullptr, &counters, &rest,
                           g ? &g->pos : &rest, g ? &g->tri : &rest, g ? &g->bary : &rest};
    VkDescriptorBufferInfo bi[11];
    VkDescriptorImageInfo ii{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[11];
    for (uint32_t i = 0; i < 11; ++i) {
      w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, i, 0, 1, types[i]};
      if (i == 0) w[i].pNext = &wa;
      else if (i == 4 || i == 5) w[i].pImageInfo = &ii;
      else {
        bi[i] = {bufs[i]->b, 0, VK_WHOLE_SIZE};
        w[i].pBufferInfo = &bi[i];
      }
    }
    vkUpdateDescriptorSets(dev, 11, w, 0, nullptr);
  };
  auto dispatch = [&](VkCommandBuffer cmd, VkPipeline p, const Push& push) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    vkCmdDispatch(cmd, (push.count + 63) / 64, 1, 1);
  };

  // ---- BLAS per scene ----
  auto card_blas = [&](bool opaque) {
    AS* a = new AS;
    a->geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    a->geom.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    auto& t = a->geom.geometry.triangles;
    t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = cpos.addr;
    t.vertexStride = 12;
    t.maxVertex = nv - 1;
    t.indexType = VK_INDEX_TYPE_UINT32;
    t.indexData.deviceAddress = ctri.addr;
    a->create(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
              VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR, nt);
    return a;
  };
  std::vector<VkAccelerationStructureGeometryLinearSweptSpheresDataNV> lss_data(ng);
  auto groom_blas = [&](size_t k, VkBuildAccelerationStructureFlagsKHR flags) {
    const Groom& g = grooms[k];
    auto& d = lss_data[k];
    d = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV};
    d.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    d.vertexData.deviceAddress = g.pos.addr;
    d.vertexStride = 12;
    d.radiusFormat = VK_FORMAT_R32_SFLOAT;
    d.radiusData.deviceAddress = g.radii.addr;
    d.radiusStride = 4;
    d.indexType = VK_INDEX_TYPE_UINT32;
    d.indexData.deviceAddress = g.starts.addr;
    d.indexStride = 4;
    d.indexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
    d.endCapsMode = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;
    AS* a = new AS;
    a->geom.pNext = &d;
    a->geom.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
    a->geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    a->create(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, flags, g.segments);
    return a;
  };
  Buf instbuf = make(sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
  auto make_tlas = [&](const AS& blas) {
    VkAccelerationStructureInstanceKHR in{};
    in.transform.matrix[0][0] = in.transform.matrix[1][1] = in.transform.matrix[2][2] = 1.0f;
    in.mask = 0xFF;
    in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    in.accelerationStructureReference = blas.address();
    std::memcpy(instbuf.map, &in, sizeof in);
    AS* t = new AS;
    t->geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    t->geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    t->geom.geometry.instances.data.deviceAddress = instbuf.addr;
    t->create(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, 1);
    timed([&](VkCommandBuffer cmd) { t->record(cmd, false); });
    return t;
  };

  const float light[3] = {0.24f, 0.39f, -0.89f};
  const char* kinds[3] = {"shadow", "ao", "gi"};
  std::string out = "{\n";
  char line[1024];
  snprintf(line, sizeof line, "\"gpu\":\"%s\",\"driver\":%u,\"rays_per_batch\":%u,\"repeats\":%d,\"card_triangles\":%u,\"card_vertices\":%u,\n",
           props.deviceName, props.driverVersion, rays, repeats, nt, nv);
  out += line;

  // Ray batches against one BLAS; returns the JSON fragment of the three kinds.
  auto trace_all = [&](AS* tlas, VkPipeline p, const Groom* g) {
    timed([&](VkCommandBuffer cmd) { tlas->record(cmd, false); });
    bind(tlas->as, g);
    std::string s;
    for (uint32_t kind = 0; kind < 3; ++kind) {
      Push push{rays, kind, 12345u, nt, 0, {light[0], light[1], light[2]}};
      std::vector<double> ms;
      for (int r = 0; r < repeats; ++r) ms.push_back(timed([&](VkCommandBuffer cmd) { dispatch(cmd, p, push); }));
      timed([&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, counters.b, 0, 16, 0);
        barrier(cmd);
        dispatch(cmd, p, push);
        barrier(cmd);
        VkBufferCopy c{0, 0, 16};
        vkCmdCopyBuffer(cmd, counters.b, readback.b, 1, &c);
      });
      uint32_t cnt[4];
      std::memcpy(cnt, readback.map, 16);
      snprintf(line, sizeof line, "%s\"%s\":{\"ms\":%.3f,\"min_ms\":%.3f,\"hit_share\":%.4f,\"candidates_per_ray\":%.2f}", kind ? "," : "",
               kinds[kind], median(ms), *std::min_element(ms.begin(), ms.end()), double(cnt[0]) / rays, double(cnt[1]) / rays);
      s += line;
    }
    return s;
  };
  auto build_times = [&](AS& a, bool refit, std::vector<double>& build, std::vector<double>& upd) {
    for (int r = 0; r < repeats; ++r) {
      build.push_back(timed([&](VkCommandBuffer cmd) { a.record(cmd, false); }));
      if (refit) upd.push_back(timed([&](VkCommandBuffer cmd) { a.record(cmd, true); }));
    }
  };

  // ---- cards ----
  for (int opaque = 0; opaque < 2; ++opaque) {
    AS* a = card_blas(opaque != 0);
    std::vector<double> build, refit, deform;
    build_times(*a, true, build, refit);
    AS* tlas = make_tlas(*a);
    bind(tlas->as, nullptr);
    for (int r = 0; r < repeats; ++r)
      deform.push_back(timed([&](VkCommandBuffer cmd) { dispatch(cmd, p_deform, Push{nv, 0, 0, nt, 0.1f * r, {0, 0, 0}}); }));
    // Per frame for the cards: deform + refit, as one submission.
    std::vector<double> frame;
    for (int r = 0; r < repeats; ++r)
      frame.push_back(timed([&](VkCommandBuffer cmd) {
        dispatch(cmd, p_deform, Push{nv, 0, 0, nt, 0.1f * r, {0, 0, 0}});
        barrier(cmd);
        a->record(cmd, true);
      }));
    timed([&](VkCommandBuffer cmd) {
      VkBufferCopy c{0, 0, VkDeviceSize(nv) * 12};
      vkCmdCopyBuffer(cmd, rest.b, cpos.b, 1, &c);
      barrier(cmd);
      a->record(cmd, false);
    });
    std::string tr = trace_all(tlas, opaque ? p_opaque : p_alpha, nullptr);
    tlas->destroy();
    delete tlas;
    snprintf(line, sizeof line,
             "\"cards_%s\":{\"primitives\":%u,\"blas_mb\":%.2f,\"build_ms\":%.3f,\"refit_ms\":%.3f,\"deform_ms\":%.3f,"
             "\"frame_update_ms\":%.3f,",
             opaque ? "opaque" : "alpha_test", nt, a->size / 1048576.0, median(build), median(refit), median(deform), median(frame));
    out += line + tr + "},\n";
    a->destroy();
    delete a;
  }

  // ---- grooms ----
  for (size_t k = 0; k < grooms.size(); ++k) {
    Groom& g = grooms[k];
    AS* fast_build = groom_blas(k, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR);
    std::vector<double> fb_build, none;
    build_times(*fast_build, false, fb_build, none);
    const double fb_mb = fast_build->size / 1048576.0;
    fast_build->destroy();
    delete fast_build;
    AS* a = groom_blas(k, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
    std::vector<double> build, refit, update, frame;
    build_times(*a, true, build, refit);
    AS* tlas = make_tlas(*a);
    bind(tlas->as, &g);
    for (int r = 0; r < repeats; ++r)
      update.push_back(timed([&](VkCommandBuffer cmd) { dispatch(cmd, p_update, Push{g.points, 0, 0, nt, 0, {0, 0, 0}}); }));
    // Per frame for the groom: deform the cards (stand-in for the captured vertices), move the
    // points with the embedding, refit.
    for (int r = 0; r < repeats; ++r)
      frame.push_back(timed([&](VkCommandBuffer cmd) {
        dispatch(cmd, p_deform, Push{nv, 0, 0, nt, 0.1f * r, {0, 0, 0}});
        barrier(cmd);
        dispatch(cmd, p_update, Push{g.points, 0, 0, nt, 0, {0, 0, 0}});
        barrier(cmd);
        a->record(cmd, true);
      }));
    // Trace from a freshly built BLAS of the rest pose, so all scenes trace the same geometry.
    timed([&](VkCommandBuffer cmd) {
      VkBufferCopy c{0, 0, VkDeviceSize(nv) * 12};
      vkCmdCopyBuffer(cmd, rest.b, cpos.b, 1, &c);
      barrier(cmd);
      dispatch(cmd, p_update, Push{g.points, 0, 0, nt, 0, {0, 0, 0}});
      barrier(cmd);
      a->record(cmd, false);
    });
    std::string tr = trace_all(tlas, p_opaque, &g);
    tlas->destroy();
    delete tlas;
    snprintf(line, sizeof line,
             "\"groom_step%u\":{\"strands\":%u,\"points\":%u,\"primitives\":%u,\"blas_mb\":%.2f,\"fast_build_blas_mb\":%.2f,"
             "\"build_ms\":%.3f,\"fast_build_ms\":%.3f,\"refit_ms\":%.3f,\"point_update_ms\":%.3f,\"frame_update_ms\":%.3f,",
             g.step, g.strands, g.points, g.segments, a->size / 1048576.0, fb_mb, median(build), median(fb_build), median(refit),
             median(update), median(frame));
    out += line + tr + (k + 1 < grooms.size() ? "},\n" : "}\n");
    a->destroy();
    delete a;
  }
  out += "}\n";
  std::fputs(out.c_str(), stdout);
  vkDeviceWaitIdle(dev);
  return 0;
}
