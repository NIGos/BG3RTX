// Track D cost probe: builds ray tracing acceleration structures from a
// captured frame (captures/<ts>/geo/scene.bin, written by layer/geocapture.cpp)
// on its own Vulkan device and times them with GPU timestamps.
//   geo_blas_bench <scene.bin> [repeats]
// Variants:
//   1. one BLAS over the whole triangle soup, PREFER_FAST_BUILD
//   2. one BLAS, PREFER_FAST_TRACE | ALLOW_UPDATE, then an in-place refit
//   3. one BLAS per captured draw, all in one build call (FAST_TRACE), plus a
//      TLAS with one instance per draw: the layout a per-object refit scheme uses
// Prints JSON on stdout.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
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

struct DrawRec { uint32_t first, count; uint64_t vs, fs; uint32_t pipe, kind, n, inst, skip, pad; };

static VkDevice dev;
static VkPhysicalDevice phys;
static VkPhysicalDeviceMemoryProperties memprops;
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
  b.size = size;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
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
  vkDestroyBuffer(dev, b.b, nullptr);
  vkFreeMemory(dev, b.m, nullptr);
  b = {};
}

static VkQueue queue;
static VkCommandPool pool;
static VkQueryPool qp;
static float ts_period;

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
  return v[v.size() / 2];
}

static void as_barrier(VkCommandBuffer cmd) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: geo_blas_bench <scene.bin> [repeats]\n");
    return 2;
  }
  int repeats = argc > 2 ? std::max(1, atoi(argv[2])) : 5;
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<char> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (file.size() < 320) { std::fprintf(stderr, "short file\n"); return 1; }
  uint32_t hdr[8];
  std::memcpy(hdr, file.data(), 32);
  if (hdr[0] != 0x47334742u || !(hdr[6] & 1)) { std::fprintf(stderr, "not a scene.bin with world positions\n"); return 1; }
  const uint32_t nvert = hdr[2], ndraw = hdr[3];
  const size_t draws_at = 320, clip_at = draws_at + size_t(ndraw) * sizeof(DrawRec), world_at = clip_at + size_t(nvert) * 16;
  if (file.size() < world_at + size_t(nvert) * 12) { std::fprintf(stderr, "truncated\n"); return 1; }
  std::vector<DrawRec> draws(ndraw);
  std::memcpy(draws.data(), file.data() + draws_at, size_t(ndraw) * sizeof(DrawRec));
  const uint32_t ntri = nvert / 3;

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
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
  std::vector<VkQueueFamilyProperties> fams(n);
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, fams.data());
  uint32_t fam = 0;
  for (uint32_t i = 0; i < n; ++i)
    if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { fam = i; break; }
  float prio = 1;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = fam;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR asf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  asf.accelerationStructure = VK_TRUE;
  VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &asf};
  v12.bufferDeviceAddress = VK_TRUE;
  const char* exts[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &v12};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 2;
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
  const VkDeviceSize scratch_align = asp.minAccelerationStructureScratchOffsetAlignment;

  // ---- vertex upload (world positions, float3) ----
  const VkBufferUsageFlags in_usage =
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  Buf staging = make(VkDeviceSize(nvert) * 12, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.map, file.data() + world_at, size_t(nvert) * 12);
  Buf verts = make(VkDeviceSize(nvert) * 12, in_usage, false);
  double upload_ms = timed([&](VkCommandBuffer cmd) {
    VkBufferCopy c{0, 0, VkDeviceSize(nvert) * 12};
    vkCmdCopyBuffer(cmd, staging.b, verts.b, 1, &c);
  });

  auto tri_geom = [&](uint32_t first_vertex, uint32_t vcount) {
    VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& t = g.geometry.triangles;
    t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = verts.addr + VkDeviceAddress(first_vertex) * 12;
    t.vertexStride = 12;
    t.maxVertex = vcount ? vcount - 1 : 0;
    t.indexType = VK_INDEX_TYPE_NONE_KHR;
    return g;
  };

  std::string out = "{\n";
  char line[512];
  snprintf(line, sizeof line, "\"gpu\":\"%s\",\"triangles\":%u,\"draws\":%u,\"upload_ms\":%.3f,\n", props.deviceName, ntri,
           ndraw, upload_ms);
  out += line;

  // ---- 1 + 2: monolithic BLAS ----
  for (int variant = 0; variant < 2; ++variant) {
    VkAccelerationStructureGeometryKHR g = tri_geom(0, nvert);
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = variant == 0 ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
                            : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                  VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.geometryCount = 1;
    bi.pGeometries = &g;
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    pSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &ntri, &sz);
    Buf asbuf = make(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    Buf scratch = make(std::max(sz.buildScratchSize, sz.updateScratchSize) + scratch_align,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = asbuf.b;
    ci.size = sz.accelerationStructureSize;
    ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VkAccelerationStructureKHR as;
    CHECK(pCreateAS(dev, &ci, nullptr, &as));
    bi.dstAccelerationStructure = as;
    bi.scratchData.deviceAddress = (scratch.addr + scratch_align - 1) & ~(scratch_align - 1);
    VkAccelerationStructureBuildRangeInfoKHR range{ntri, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
    std::vector<double> build, refit;
    for (int r = 0; r < repeats; ++r) {
      bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.srcAccelerationStructure = VK_NULL_HANDLE;
      build.push_back(timed([&](VkCommandBuffer cmd) { pBuild(cmd, 1, &bi, &pr); }));
      if (variant == 1) {
        bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        bi.srcAccelerationStructure = as;
        refit.push_back(timed([&](VkCommandBuffer cmd) { pBuild(cmd, 1, &bi, &pr); }));
      }
    }
    snprintf(line, sizeof line,
             "\"monolithic_%s\":{\"build_ms\":%.3f,\"refit_ms\":%s,\"as_mb\":%.1f,\"scratch_mb\":%.1f,\"update_scratch_mb\":%.1f},\n",
             variant == 0 ? "fast_build" : "fast_trace_updatable", median(build),
             refit.empty() ? "null" : std::to_string(median(refit)).c_str(), sz.accelerationStructureSize / 1048576.0,
             sz.buildScratchSize / 1048576.0, sz.updateScratchSize / 1048576.0);
    out += line;
    pDestroyAS(dev, as, nullptr);
    drop(asbuf);
    drop(scratch);
  }

  // ---- 3: one BLAS per draw + TLAS ----
  {
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < ndraw; ++i)
      if (draws[i].count >= 3) ids.push_back(i);
    const size_t m = ids.size();
    std::vector<VkAccelerationStructureGeometryKHR> geoms(m);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos(m);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(m);
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> pranges(m);
    std::vector<VkAccelerationStructureKHR> blas(m);
    std::vector<VkDeviceSize> as_off(m), scr_off(m);
    VkDeviceSize as_total = 0, scr_total = 0;
    for (size_t k = 0; k < m; ++k) {
      const DrawRec& d = draws[ids[k]];
      geoms[k] = tri_geom(d.first, d.count);
      auto& bi = infos[k];
      bi = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
      bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.geometryCount = 1;
      bi.pGeometries = &geoms[k];
      uint32_t prims = d.count / 3;
      ranges[k] = {prims, 0, 0, 0};
      pranges[k] = &ranges[k];
      VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
      pSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
      as_off[k] = as_total;
      as_total += (sz.accelerationStructureSize + 255) & ~VkDeviceSize(255);
      scr_off[k] = scr_total;
      scr_total += (std::max(sz.buildScratchSize, sz.updateScratchSize) + scratch_align - 1) & ~(scratch_align - 1);
      ranges[k].primitiveCount = prims;
      bi.scratchData.deviceAddress = 0;
      // sizes are kept in as_off/scr_off deltas
      ranges[k].firstVertex = 0;
      (void)sz;
    }
    Buf asbuf = make(as_total + 256, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    Buf scratch = make(scr_total + scratch_align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    VkDeviceAddress scr_base = (scratch.addr + scratch_align - 1) & ~(scratch_align - 1);
    for (size_t k = 0; k < m; ++k) {
      VkDeviceSize size = (k + 1 < m ? as_off[k + 1] : as_total) - as_off[k];
      VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
      ci.buffer = asbuf.b;
      ci.offset = as_off[k];
      ci.size = size;
      ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      CHECK(pCreateAS(dev, &ci, nullptr, &blas[k]));
      infos[k].dstAccelerationStructure = blas[k];
      infos[k].scratchData.deviceAddress = scr_base + scr_off[k];
    }
    // TLAS over all per-draw BLASes, identity transforms.
    Buf instbuf = make(sizeof(VkAccelerationStructureInstanceKHR) * std::max<size_t>(m, 1),
                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
    auto* inst_ptr = static_cast<VkAccelerationStructureInstanceKHR*>(instbuf.map);
    for (size_t k = 0; k < m; ++k) {
      VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
      ai.accelerationStructure = blas[k];
      VkAccelerationStructureInstanceKHR in{};
      in.transform.matrix[0][0] = in.transform.matrix[1][1] = in.transform.matrix[2][2] = 1.0f;
      in.instanceCustomIndex = uint32_t(k);
      in.mask = 0xFF;
      in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
      in.accelerationStructureReference = pASAddr(dev, &ai);
      inst_ptr[k] = in;
    }
    VkAccelerationStructureGeometryKHR tg{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tg.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tg.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tg.geometry.instances.data.deviceAddress = instbuf.addr;
    VkAccelerationStructureBuildGeometryInfoKHR tbi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tbi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tbi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    tbi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tbi.geometryCount = 1;
    tbi.pGeometries = &tg;
    uint32_t ninst = uint32_t(m);
    VkAccelerationStructureBuildSizesInfoKHR tsz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    pSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tbi, &ninst, &tsz);
    Buf tbuf = make(tsz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    Buf tscr = make(tsz.buildScratchSize + scratch_align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    VkAccelerationStructureCreateInfoKHR tci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    tci.buffer = tbuf.b;
    tci.size = tsz.accelerationStructureSize;
    tci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VkAccelerationStructureKHR tlas;
    CHECK(pCreateAS(dev, &tci, nullptr, &tlas));
    tbi.dstAccelerationStructure = tlas;
    tbi.scratchData.deviceAddress = (tscr.addr + scratch_align - 1) & ~(scratch_align - 1);
    VkAccelerationStructureBuildRangeInfoKHR trange{ninst, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ptr = &trange;

    std::vector<double> build, refit, tl;
    for (int r = 0; r < repeats; ++r) {
      for (auto& bi : infos) { bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; bi.srcAccelerationStructure = VK_NULL_HANDLE; }
      build.push_back(timed([&](VkCommandBuffer cmd) { pBuild(cmd, uint32_t(m), infos.data(), pranges.data()); }));
      for (size_t k = 0; k < m; ++k) { infos[k].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR; infos[k].srcAccelerationStructure = blas[k]; }
      refit.push_back(timed([&](VkCommandBuffer cmd) { pBuild(cmd, uint32_t(m), infos.data(), pranges.data()); }));
      tl.push_back(timed([&](VkCommandBuffer cmd) {
        as_barrier(cmd);
        pBuild(cmd, 1, &tbi, &ptr);
      }));
    }
    snprintf(line, sizeof line,
             "\"per_draw\":{\"blas_count\":%zu,\"build_ms\":%.3f,\"refit_ms\":%.3f,\"tlas_build_ms\":%.3f,\"blas_mb\":%.1f,"
             "\"scratch_mb\":%.1f,\"tlas_mb\":%.2f}\n",
             m, median(build), median(refit), median(tl), as_total / 1048576.0, scr_total / 1048576.0,
             tsz.accelerationStructureSize / 1048576.0);
    out += line;
    pDestroyAS(dev, tlas, nullptr);
    for (auto a : blas) pDestroyAS(dev, a, nullptr);
    drop(tbuf);
    drop(tscr);
    drop(instbuf);
    drop(asbuf);
    drop(scratch);
  }
  out += "}\n";
  std::fputs(out.c_str(), stdout);
  drop(verts);
  drop(staging);
  vkDestroyQueryPool(dev, qp, nullptr);
  vkDestroyCommandPool(dev, pool, nullptr);
  vkDestroyDevice(dev, nullptr);
  vkDestroyInstance(inst, nullptr);
  return 0;
}
