// Track D phase 2: hardware ray tracing on the game's own device
// (docs/TRACK_D_RT.md).
//
// Device: with rt=1 the layer asks for instance API 1.3 (layer.cpp) and adds
// VK_KHR_acceleration_structure, VK_KHR_ray_query and
// VK_KHR_deferred_host_operations with their features to a copy of the game's
// device create info. Buffer device addresses come from the game's own
// VK_EXT_buffer_device_address (enabling VK_KHR_buffer_device_address as well
// is forbidden, VUID-03328; with API 1.2 the AS extensions no longer depend on
// the KHR extension, and vkGetBufferDeviceAddress accepts the EXT feature,
// VUID-vkGetBufferDeviceAddress-bufferDeviceAddress-03324). If the game does not
// enable the EXT extension, the core 1.2 bufferDeviceAddress feature is used
// instead and our allocations carry VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT.
//
// Per frame (geocapture.cpp captures the G-buffer pass into a device-local XFB
// buffer, every draw at a CPU-known offset, so the soup is contiguous and its
// size is known while recording):
//   1. compute: clip-space gl_Position -> camera-relative world, in place, by
//      solving the x, y, w rows of the camera UBO's ViewProj (_m2); vertices
//      whose z does not match (the sky dome writes z = 0) or that are not
//      finite (the game parks culled vertices at NaN) collapse their triangle
//      to a point (zero area: never hit, and still "active", so refits stay valid);
//   2. one BLAS over the soup: full build, or a refit when the draw sequence is
//      identical to the last build (rt_refit, rt_rebuild_frames);
//   3. a one-instance TLAS (identity transform: the space is already camera-relative).
// The core traces against the TLAS in the tiled lighting (rt_shadow.hlsl).
#include "host.h"

#include <intrin.h>

#include <algorithm>
#include <cstring>

namespace bg3 {

namespace {

std::atomic<bool> g_rt{false};
DeviceData* g_dd = nullptr;
bool g_bda_core = false;          // core bufferDeviceAddress feature (allocation flag needed)
PFN_vkGetBufferDeviceAddress p_bda = nullptr;
VkDeviceSize g_scratch_align = 256;
float g_ts_period = 1.0f;

// Storage for the modified device create info.
std::vector<const char*> g_ext_names;
VkPhysicalDeviceAccelerationStructureFeaturesKHR g_f_as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
VkPhysicalDeviceRayQueryFeaturesKHR g_f_rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
VkPhysicalDeviceBufferDeviceAddressFeatures g_f_bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
VkPhysicalDeviceBufferDeviceAddressFeaturesEXT g_f_bda_ext{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};

struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  VkDeviceAddress addr = 0;
};

std::mutex g_mu;   // everything below
Buf g_blas_buf, g_tlas_buf, g_scratch, g_inst;
VkAccelerationStructureKHR g_blas = VK_NULL_HANDLE, g_tlas = VK_NULL_HANDLE;
VkDeviceAddress g_blas_addr = 0;
uint32_t g_blas_capacity = 0;     // primitives the BLAS storage was sized for
VkBuildAccelerationStructureFlagsKHR g_blas_flags = 0;
bool g_blas_valid = false;        // built at least once with the current storage
uint32_t g_built_prims = 0;
uint64_t g_built_sig = 0;
uint32_t g_since_build = 0;
uint64_t g_last_frame = UINT64_MAX;   // frame the scene was last recorded in
std::atomic<uint64_t> g_tlas_frame{0};
bool g_logged_first = false;
bool g_failed = false;

// XFB -> world conversion (shaders/rt/xfb_to_world.hlsl), push descriptors.
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_pl = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE;
uint64_t g_pipe_gen = 0;

// GPU timing: 4 timestamps per build (start, converted, BLAS, TLAS) in a ring.
constexpr uint32_t kRing = 16;
VkQueryPool g_qp = VK_NULL_HANDLE;
struct Slot { uint64_t frame = 0; bool refit = false; uint32_t prims = 0; };
Slot g_slots[kRing];
uint32_t g_next_slot = 0;
struct Acc { std::vector<float> conv, blas, tlas; };
Acc g_acc[2];                      // [0] rebuild, [1] refit
uint32_t g_n_rebuild = 0, g_n_refit = 0, g_n_frames = 0;
uint64_t g_prims_sum = 0;

const VkBaseInStructure* find_in(const void* chain, VkStructureType t) {
  for (auto p = static_cast<const VkBaseInStructure*>(chain); p; p = p->pNext)
    if (p->sType == t) return p;
  return nullptr;
}

bool make_buf(Buf& out, VkDeviceSize size, VkBufferUsageFlags usage) {
  DeviceData* dd = g_dd;
  out = Buf{};
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &out.b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, out.b, &req);
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  if (g_bda_core) mai.pNext = &fi;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out = Buf{};
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
  out.size = size;
  VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  ai.buffer = out.b;
  out.addr = p_bda(dd->device, &ai);
  return true;
}

void free_buf(Buf& b) {
  defer_destroy(BG3_DEFER_BUFFER, uint64_t(b.b));
  defer_destroy(BG3_DEFER_MEMORY, uint64_t(b.m));
  b = Buf{};
}

VkDeviceAddress buffer_address(VkBuffer b) {
  VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  ai.buffer = b;
  return p_bda(g_dd->device, &ai);
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  g_dd->vk.CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

constexpr VkPipelineStageFlags kAS = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kXFB = VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
constexpr VkPipelineStageFlags kXfer = VK_PIPELINE_STAGE_TRANSFER_BIT;

bool ensure_pipeline() {
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/xfb_to_world.hlsl", "main", "cs_6_6", &blob)) return g_pipe != VK_NULL_HANDLE;
  if (g_pipe && blob.generation == g_pipe_gen) return true;
  DeviceData* dd = g_dd;
  if (!g_dsl) {
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount = 2;
    dci.pBindings = b;
    if (dd->vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pci.setLayoutCount = 1;
    pci.pSetLayouts = &g_dsl;
    pci.pushConstantRangeCount = 1;
    pci.pPushConstantRanges = &pc;
    if (dd->vk.CreatePipelineLayout(dd->device, &pci, nullptr, &g_pl) != VK_SUCCESS) return false;
  }
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return g_pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  ci.layout = g_pl;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) {
    log("[rt] xfb_to_world pipeline creation failed (%d)", r);
    return g_pipe != VK_NULL_HANDLE;
  }
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  g_pipe = p;
  g_pipe_gen = blob.generation;
  log("[rt] xfb_to_world pipeline generation %llu", (unsigned long long)blob.generation);
  return true;
}

VkAccelerationStructureGeometryKHR blas_geometry(VkDeviceAddress verts, uint32_t prims) {
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  auto& t = g.geometry.triangles;
  t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  t.vertexData.deviceAddress = verts;
  t.vertexStride = 16;
  t.maxVertex = prims * 3 ? prims * 3 - 1 : 0;
  t.indexType = VK_INDEX_TYPE_NONE_KHR;
  return g;
}

VkAccelerationStructureGeometryKHR tlas_geometry() {
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  g.geometry.instances.data.deviceAddress = g_inst.addr;
  return g;
}

bool grow_scratch(VkDeviceSize need) {
  need += g_scratch_align;
  if (g_scratch.b && g_scratch.size >= need) return true;
  if (g_scratch.b) free_buf(g_scratch);
  return make_buf(g_scratch, need + need / 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

VkDeviceAddress scratch_addr() { return (g_scratch.addr + g_scratch_align - 1) & ~(g_scratch_align - 1); }

// BLAS storage for at least `prims` primitives with `flags`.
bool ensure_blas(uint32_t prims, VkBuildAccelerationStructureFlagsKHR flags, VkDeviceAddress verts) {
  if (g_blas && prims <= g_blas_capacity && flags == g_blas_flags) return true;
  DeviceData* dd = g_dd;
  uint32_t cap = (std::max)(prims + prims / 4, 65536u);
  VkAccelerationStructureGeometryKHR g = blas_geometry(verts, cap);
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bi.flags = flags;
  bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bi.geometryCount = 1;
  bi.pGeometries = &g;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &cap, &sz);
  if (g_blas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_blas));
  g_blas = VK_NULL_HANDLE;
  g_blas_valid = false;
  if (g_blas_buf.b) free_buf(g_blas_buf);
  if (!make_buf(g_blas_buf, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) ||
      !grow_scratch((std::max)(sz.buildScratchSize, sz.updateScratchSize))) {
    log("[rt] could not allocate the BLAS (%u primitives, %llu MB)", cap,
        (unsigned long long)(sz.accelerationStructureSize >> 20));
    return false;
  }
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.buffer = g_blas_buf.b;
  ci.size = sz.accelerationStructureSize;
  ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  if (dd->vk.CreateAccelerationStructureKHR(dd->device, &ci, nullptr, &g_blas) != VK_SUCCESS) {
    g_blas = VK_NULL_HANDLE;
    return false;
  }
  VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  ai.accelerationStructure = g_blas;
  g_blas_addr = dd->vk.GetAccelerationStructureDeviceAddressKHR(dd->device, &ai);
  g_blas_capacity = cap;
  g_blas_flags = flags;
  log("[rt] BLAS storage for %u primitives: %llu MB, scratch build %llu MB / update %llu MB (flags 0x%x)", cap,
      (unsigned long long)(sz.accelerationStructureSize >> 20), (unsigned long long)(sz.buildScratchSize >> 20),
      (unsigned long long)(sz.updateScratchSize >> 20), flags);
  return true;
}

bool ensure_tlas() {
  if (g_tlas) return true;
  DeviceData* dd = g_dd;
  if (!make_buf(g_inst, sizeof(VkAccelerationStructureInstanceKHR),
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
    return false;
  VkAccelerationStructureGeometryKHR g = tlas_geometry();
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bi.geometryCount = 1;
  bi.pGeometries = &g;
  uint32_t one = 1;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &one, &sz);
  if (!make_buf(g_tlas_buf, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) ||
      !grow_scratch(sz.buildScratchSize))
    return false;
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.buffer = g_tlas_buf.b;
  ci.size = sz.accelerationStructureSize;
  ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  if (dd->vk.CreateAccelerationStructureKHR(dd->device, &ci, nullptr, &g_tlas) != VK_SUCCESS) {
    g_tlas = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

void release_all() {
  if (g_blas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_blas));
  if (g_tlas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_tlas));
  g_blas = g_tlas = VK_NULL_HANDLE;
  for (Buf* b : {&g_blas_buf, &g_tlas_buf, &g_scratch, &g_inst})
    if (b->b) free_buf(*b);
  g_blas_capacity = 0;
  g_blas_valid = false;
}

void timestamp(VkCommandBuffer cmd, int slot, uint32_t i) {
  if (slot < 0) return;
  g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, uint32_t(slot) * 4 + i);
}

}  // namespace

// ================= host-facing entry points =================

uint32_t rt_instance_api(uint32_t requested) {
  if (!cfg_int("rt", 0)) return requested;
  // 1.2 is what the AS/ray query dependencies need; 1.3 also makes our own
  // shaders (DXC -fspv-target-env=vulkan1.3, SPIR-V 1.6) valid for the device,
  // and is what Streamline asks for anyway (fg>=2).
  return (std::max)(requested, uint32_t(VK_API_VERSION_1_3));
}

const VkDeviceCreateInfo* rt_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                VkDeviceCreateInfo* scratch) {
  if (!cfg_int("rt", 0) || !id) return ci;
  VkPhysicalDeviceProperties props{};
  id->GetPhysicalDeviceProperties(phys, &props);
  if (id->api_version < VK_API_VERSION_1_2 || props.apiVersion < VK_API_VERSION_1_2) {
    log("[rt] needs Vulkan 1.2 (instance %u.%u, device %u.%u): ray tracing off", VK_API_VERSION_MAJOR(id->api_version),
        VK_API_VERSION_MINOR(id->api_version), VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));
    return ci;
  }
  // Supported?
  uint32_t n = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> avail(n);
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data());
  auto available = [&](const char* e) {
    for (auto& a : avail)
      if (!strcmp(a.extensionName, e)) return true;
    return false;
  };
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, &as};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &bda};
  if (id->GetPhysicalDeviceFeatures2) id->GetPhysicalDeviceFeatures2(phys, &f2);
  const char* need[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  for (const char* e : need)
    if (!available(e)) {
      log("[rt] %s not available: ray tracing off", e);
      return ci;
    }
  if (!as.accelerationStructure || !rq.rayQuery || !bda.bufferDeviceAddress) {
    log("[rt] features missing (accelerationStructure %d, rayQuery %d, bufferDeviceAddress %d): ray tracing off",
        as.accelerationStructure, rq.rayQuery, bda.bufferDeviceAddress);
    return ci;
  }

  // What the game already asks for.
  std::string chain;
  for (auto p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext) chain += " " + std::to_string(p->sType);
  log("[rt] game device create chain:%s", chain.c_str());
  g_ext_names.assign(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
  auto listed = [&](const char* e) {
    for (auto x : g_ext_names)
      if (!strcmp(x, e)) return true;
    return false;
  };
  if (find_in(ci->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR) ||
      find_in(ci->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR)) {
    log("[rt] the application already chains ray tracing features; leaving the device alone");
    return ci;
  }
  const void* next = ci->pNext;
  if (listed(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
    // Keep the game's EXT extension (KHR + EXT is VUID-03328). Its feature
    // struct must say bufferDeviceAddress = TRUE; add one if it has none.
    auto* e = reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT*>(
        find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT));
    if (e && !e->bufferDeviceAddress) {
      log("[rt] the game enables VK_EXT_buffer_device_address with bufferDeviceAddress off: ray tracing off");
      return ci;
    }
    if (!e) {
      g_f_bda_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
      g_f_bda_ext.bufferDeviceAddress = VK_TRUE;
      g_f_bda_ext.pNext = const_cast<void*>(next);
      next = &g_f_bda_ext;
      log("[rt] + VkPhysicalDeviceBufferDeviceAddressFeaturesEXT");
    }
    // With API 1.2 the core feature can be enabled next to the EXT extension
    // (only VkPhysicalDeviceVulkan12Features::bufferDeviceAddress is excluded,
    // VUID-04748). Something in the process (NGX's own allocations) already
    // passes VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, which needs the core
    // feature (VUID-VkMemoryAllocateInfo-flags-03331) once the device is 1.2.
    g_bda_core = false;
    if (cfg_int("rt_bda_core", 1) && !find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) &&
        !find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)) {
      g_f_bda = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      g_f_bda.bufferDeviceAddress = VK_TRUE;
      g_f_bda.pNext = const_cast<void*>(next);
      next = &g_f_bda;
      g_bda_core = true;
    }
    log("[rt] buffer device addresses through the game's VK_EXT_buffer_device_address%s",
        g_bda_core ? " plus the core 1.2 bufferDeviceAddress feature" : "");
  } else {
    auto* v12 = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(
        find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES));
    auto* core = reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(
        find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES));
    if ((v12 && !v12->bufferDeviceAddress) || (core && !core->bufferDeviceAddress)) {
      log("[rt] the game chains bufferDeviceAddress = FALSE: ray tracing off");
      return ci;
    }
    if (!v12 && !core) {
      g_f_bda = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      g_f_bda.bufferDeviceAddress = VK_TRUE;
      g_f_bda.pNext = const_cast<void*>(next);
      next = &g_f_bda;
    }
    g_bda_core = true;
    log("[rt] buffer device addresses through the core 1.2 feature");
  }
  for (const char* e : need)
    if (!listed(e)) g_ext_names.push_back(e);
  g_f_rq = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  g_f_rq.rayQuery = VK_TRUE;
  g_f_rq.pNext = const_cast<void*>(next);
  g_f_as = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  g_f_as.accelerationStructure = VK_TRUE;
  g_f_as.pNext = &g_f_rq;
  *scratch = *ci;
  scratch->pNext = &g_f_as;
  scratch->enabledExtensionCount = uint32_t(g_ext_names.size());
  scratch->ppEnabledExtensionNames = g_ext_names.data();
  log("[rt] + VK_KHR_acceleration_structure, VK_KHR_ray_query, VK_KHR_deferred_host_operations");
  return scratch;
}

void rt_device_created(DeviceData* dd, bool enabled) {
  if (g_dd) return;
  g_dd = dd;
  if (!enabled) {
    if (cfg_int("rt", 0)) log("[rt] ray tracing is NOT enabled on the device");
    return;
  }
  p_bda = dd->vk.GetBufferDeviceAddress;
  if (!p_bda) p_bda = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(dd->gdpa(dd->device, "vkGetBufferDeviceAddressEXT"));
  bool fns = p_bda && dd->vk.CreateAccelerationStructureKHR && dd->vk.DestroyAccelerationStructureKHR &&
             dd->vk.GetAccelerationStructureBuildSizesKHR && dd->vk.GetAccelerationStructureDeviceAddressKHR &&
             dd->vk.CmdBuildAccelerationStructuresKHR && dd->vk.CmdUpdateBuffer && dd->vk.CmdPushDescriptorSet;
  if (!fns) {
    log("[rt] ray tracing entry points missing; off");
    return;
  }
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) {
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asp};
    auto props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        dd->inst->gipa(dd->inst->instance, "vkGetPhysicalDeviceProperties2"));
    if (props2) {
      props2(dd->physical, &p2);
      g_scratch_align = (std::max)(VkDeviceSize(asp.minAccelerationStructureScratchOffsetAlignment), VkDeviceSize(16));
    }
    VkPhysicalDeviceProperties p;
    dd->inst->GetPhysicalDeviceProperties(dd->physical, &p);
    g_ts_period = p.limits.timestampPeriod;
  }
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kRing * 4;
  if (dd->vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
  g_rt = true;
  log("[rt] acceleration structures + ray query enabled on the game's device (scratch alignment %llu)",
      (unsigned long long)g_scratch_align);
}

void rt_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    release_all();
    defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
    defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_pl));
    defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_dsl));
    defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(g_qp));
    g_pipe = VK_NULL_HANDLE;
    g_pl = VK_NULL_HANDLE;
    g_dsl = VK_NULL_HANDLE;
    g_qp = VK_NULL_HANDLE;
  }
  g_rt = false;
  g_dd = nullptr;
}

bool rt_enabled() { return g_rt.load(); }

bool rt_capture_wanted() {
  if (!g_rt || g_failed || !cfg_int("rt_build", 1)) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  return g_last_frame != frame_index();
}

VkBufferUsageFlags rt_xfb_usage() {
  return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
}

bool rt_alloc_device_address() { return g_bda_core; }

void rt_before_capture(VkCommandBuffer cmd) {
  if (!g_rt) return;
  // Last frame's BLAS build read the XFB buffer this pass is about to overwrite.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.dstAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
  g_dd->vk.CmdPipelineBarrier(cmd, kAS | kCS, kXFB, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void rt_after_capture(VkCommandBuffer cmd, VkBuffer xfb, VkDeviceSize bytes, VkBuffer ubo, VkDeviceSize ubo_offset,
                      uint64_t signature, uint32_t draws) {
  if (!g_rt) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_last_frame == frame_index()) return;
  uint32_t prims = uint32_t(bytes / 48);
  if (!prims || !ubo || !xfb) {
    static int warned = 0;
    if (warned++ < 5) log("[rt] frame %llu: nothing to build (%u triangles, camera UBO %s)",
                          (unsigned long long)frame_index(), prims, ubo ? "found" : "missing");
    return;
  }
  if (!ensure_pipeline()) return;
  const bool want_refit = cfg_int("rt_refit", 1) != 0;
  VkBuildAccelerationStructureFlagsKHR flags =
      (cfg_int("rt_fast_trace", 0) ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                   : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR) |
      (want_refit ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
  VkDeviceAddress verts = buffer_address(xfb);
  if (!ensure_tlas() || !ensure_blas(prims, flags, verts)) {
    g_failed = true;
    log("[rt] acceleration structure allocation failed; scene building stopped");
    release_all();
    return;
  }
  const DeviceData* dd = g_dd;
  int rebuild_frames = (std::max)(cfg_int("rt_rebuild_frames", 60), 1);
  bool refit = want_refit && g_blas_valid && prims == g_built_prims && signature == g_built_sig &&
               int(g_since_build) < rebuild_frames;

  int slot = -1;
  if (g_qp) {
    uint32_t s = g_next_slot % kRing;
    if (!g_slots[s].frame) {
      slot = int(s);
      g_next_slot = s + 1;
      g_slots[s] = {frame_index() + 1, refit, prims};
      dd->vk.CmdResetQueryPool(cmd, g_qp, s * 4, 4);
    }
  }
  timestamp(cmd, slot, 0);
  // The XFB writes are done; last frame's ray queries and builds are done with
  // the AS memory, scratch and instance buffer.
  barrier(cmd, kXFB | kAS | kCS, kCS | kAS | kXfer);
  VkAccelerationStructureInstanceKHR inst{};
  inst.transform.matrix[0][0] = inst.transform.matrix[1][1] = inst.transform.matrix[2][2] = 1.0f;
  inst.mask = 0xFF;
  inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
  inst.accelerationStructureReference = g_blas_addr;
  dd->vk.CmdUpdateBuffer(cmd, g_inst.b, 0, sizeof inst, &inst);

  // 1. clip -> camera-relative world, in place.
  {
    VkDescriptorBufferInfo in{ubo, ubo_offset, 224}, out{xfb, 0, VkDeviceSize(prims) * 48};
    VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &in;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &out;
    uint32_t groups = (std::min)((prims + 255) / 256, 65535u);
    uint32_t pc[4] = {prims, groups * 256, 0, 0};
    dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
    dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 2, w);
    dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    dd->vk.CmdDispatch(cmd, groups, 1, 1);
  }
  barrier(cmd, kCS | kXfer, kAS);
  timestamp(cmd, slot, 1);

  // 2. BLAS
  {
    VkAccelerationStructureGeometryKHR g = blas_geometry(verts, prims);
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = g_blas_flags;
    bi.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.srcAccelerationStructure = refit ? g_blas : VK_NULL_HANDLE;
    bi.dstAccelerationStructure = g_blas;
    bi.geometryCount = 1;
    bi.pGeometries = &g;
    bi.scratchData.deviceAddress = scratch_addr();
    VkAccelerationStructureBuildRangeInfoKHR range{prims, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &bi, &ranges);
  }
  // The TLAS build reads the BLAS and reuses the scratch memory.
  barrier(cmd, kAS, kAS);
  timestamp(cmd, slot, 2);

  // 3. TLAS (one instance, identity: the soup is already camera-relative)
  {
    VkAccelerationStructureGeometryKHR g = tlas_geometry();
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.dstAccelerationStructure = g_tlas;
    bi.geometryCount = 1;
    bi.pGeometries = &g;
    bi.scratchData.deviceAddress = scratch_addr();
    VkAccelerationStructureBuildRangeInfoKHR range{1, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &bi, &ranges);
  }
  // Ray queries in later command buffers of this frame (submission order).
  barrier(cmd, kAS, kCS | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  timestamp(cmd, slot, 3);
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

  if (refit) {
    ++g_since_build;
    ++g_n_refit;
  } else {
    g_since_build = 0;
    ++g_n_rebuild;
  }
  ++g_n_frames;
  g_prims_sum += prims;
  g_blas_valid = true;
  g_built_prims = prims;
  g_built_sig = signature;
  g_last_frame = frame_index();
  g_tlas_frame = frame_index();
  if (!g_logged_first) {
    g_logged_first = true;
    log("[rt] first scene: %u draws, %u triangles, camera UBO %p+%llu", draws, prims, (void*)ubo,
        (unsigned long long)ubo_offset);
  }
}

bool rt_scene(VkAccelerationStructureKHR* tlas, uint64_t* built_frame) {
  if (!g_rt) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_tlas || !g_blas_valid) return false;
  *tlas = g_tlas;
  *built_frame = g_tlas_frame;
  return true;
}

void rt_on_present() {
  if (!g_rt || !g_qp) return;
  std::lock_guard<std::mutex> lock(g_mu);
  // Compile (and recompile after edits) on the present thread, not while
  // the game records.
  if (!g_failed && cfg_int("rt_build", 1)) ensure_pipeline();
  uint64_t now = frame_index();
  for (uint32_t s = 0; s < kRing; ++s) {
    Slot& sl = g_slots[s];
    if (!sl.frame || sl.frame + 4 > now) continue;
    uint64_t r[8] = {};
    VkResult res = g_dd->vk.GetQueryPoolResults(g_dd->device, g_qp, s * 4, 4, sizeof r, r, 16,
                                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    bool done = res == VK_SUCCESS && r[1] && r[3] && r[5] && r[7];
    if (!done && sl.frame + 60 > now) continue;
    bool refit = sl.refit;
    sl = Slot{};
    if (!done || r[6] < r[0]) continue;
    auto ms = [&](uint64_t a, uint64_t b) { return float(double(b - a) * g_ts_period * 1e-6); };
    Acc& a = g_acc[refit ? 1 : 0];
    a.conv.push_back(ms(r[0], r[2]));
    a.blas.push_back(ms(r[2], r[4]));
    a.tlas.push_back(ms(r[4], r[6]));
  }
  int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
  if (g_n_frames < uint32_t(every)) return;
  auto med = [](std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };
  for (int k = 0; k < 2; ++k) {
    Acc& a = g_acc[k];
    if (a.conv.empty()) continue;
    size_t n = a.conv.size();
    float c = med(a.conv), b = med(a.blas), t = med(a.tlas);
    log("[rt] scene build GPU (%s, %zu samples, median): convert %.3f ms, BLAS %.3f ms, TLAS %.3f ms, total %.3f ms",
        k ? "BLAS refit" : "BLAS rebuild", n, c, b, t, c + b + t);
    a = Acc{};
  }
  log("[rt] last %u frames: %u rebuilds, %u refits, %.2fM triangles average, BLAS capacity %u", g_n_frames, g_n_rebuild,
      g_n_refit, double(g_prims_sum) / g_n_frames * 1e-6, g_blas_capacity);
  g_n_frames = g_n_rebuild = g_n_refit = 0;
  g_prims_sum = 0;
}

// Diagnostic: who allocates memory with VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
// (the reason the core bufferDeviceAddress feature is enabled, see above).
static VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* ai,
                                                     const VkAllocationCallbacks* alloc, VkDeviceMemory* out) {
  static std::atomic<int> logged{0};
  if (g_rt && logged < 4)
    for (auto p = static_cast<const VkBaseInStructure*>(ai->pNext); p; p = p->pNext)
      if (p->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO &&
          (reinterpret_cast<const VkMemoryAllocateFlagsInfo*>(p)->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT) &&
          logged++ < 4) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(_ReturnAddress()), &mod))
          GetModuleFileNameA(mod, name, MAX_PATH);
        log("[rt] DEVICE_ADDRESS allocation of %llu KB, caller %s", (unsigned long long)(ai->allocationSize >> 10), name);
      }
  return device_of(device)->vk.AllocateMemory(device, ai, alloc, out);
}

void register_rt_hooks(std::unordered_map<std::string, PFN_vkVoidFunction>& m) {
  m["vkAllocateMemory"] = reinterpret_cast<PFN_vkVoidFunction>(&AllocateMemory);
}

}  // namespace bg3
