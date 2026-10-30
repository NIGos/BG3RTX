#include "host.h"
#include "omm_backend.h"
#include "omm_submit.h"
#include "omm_content.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <mutex>

namespace bg3::omm {
namespace {
DeviceData* device{};
bool requested{}, enabled{};
bool creationSuppressed{};
bool retained{};
VkPhysicalDeviceOpacityMicromapFeaturesEXT feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT};
VkPhysicalDeviceSynchronization2Features sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
VkPhysicalDeviceOpacityMicromapPropertiesEXT properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT};
VkDeviceSize scratchAlignment{256};
uint64_t maxBlasPrimitives{};
PFN_vkCreateMicromapEXT createMicromap{};
PFN_vkDestroyMicromapEXT destroyMicromap{};
PFN_vkGetMicromapBuildSizesEXT getSizes{};
PFN_vkCmdBuildMicromapsEXT buildMicromaps{};
std::mutex mutex;
struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; VkDeviceSize bytes{}; VkDeviceAddress address{}; };
struct Resource {
  std::shared_ptr<void> input_owner;
  Buffer storage, scratch;
  Buffer blas_storage, blas_scratch;
  VkAccelerationStructureKHR blas{};
  VkMicromapEXT micromap{};
  std::vector<VkMicromapUsageEXT> usage;
  uint32_t triangles{};
  Lifetime life;
};
std::map<Ticket, Resource> resources;
Ticket nextTicket{1};
VkDeviceSize bytesLive{};
constexpr VkDeviceSize budget = 256ull * 1024 * 1024;
const VkBaseInStructure* find(const void* next, VkStructureType type) {
  for (auto* p = static_cast<const VkBaseInStructure*>(next); p; p = p->pNext)
    if (p->sType == type) return p;
  return nullptr;
}
void destroy(Buffer& b) {
  if (b.buffer) device->vk.DestroyBuffer(device->device,b.buffer,nullptr);
  if (b.memory) device->vk.FreeMemory(device->device,b.memory,nullptr);
  b={};
}
void destroy(Resource& r) {
  if(r.blas)device->vk.DestroyAccelerationStructureKHR(device->device,r.blas,nullptr);
  bytesLive-=r.blas_storage.bytes+r.blas_scratch.bytes;
  destroy(r.blas_storage);destroy(r.blas_scratch);r.blas=VK_NULL_HANDLE;
  // Micromap must die BEFORE its storage. Caller has proven all uses completed.
  if (r.micromap) destroyMicromap(device->device,r.micromap,nullptr);
  bytesLive -= r.storage.bytes + r.scratch.bytes;
  destroy(r.storage); destroy(r.scratch); r.micromap=VK_NULL_HANDLE;
  r.input_owner.reset();
}
bool allocate(VkDeviceSize bytes, VkBufferUsageFlags usage, Buffer& out) {
  if (!bytes) return false;
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size=bytes; ci.usage=usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  if (device->vk.CreateBuffer(device->device,&ci,nullptr,&out.buffer)!=VK_SUCCESS) return false;
  VkMemoryRequirements req{}; device->vk.GetBufferMemoryRequirements(device->device,out.buffer,&req);
  VkPhysicalDeviceMemoryProperties memory{};
  device->inst->GetPhysicalDeviceMemoryProperties(device->physical,&memory);
  uint32_t type=UINT32_MAX;
  for (uint32_t i=0;i<memory.memoryTypeCount;++i)
    if ((req.memoryTypeBits & (1u<<i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {type=i;break;}
  if (type==UINT32_MAX) {destroy(out);return false;}
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&flags};
  ai.allocationSize=req.size; ai.memoryTypeIndex=type;
  if (device->vk.AllocateMemory(device->device,&ai,nullptr,&out.memory)!=VK_SUCCESS ||
      device->vk.BindBufferMemory(device->device,out.buffer,out.memory,0)!=VK_SUCCESS) {destroy(out);return false;}
  VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; address.buffer=out.buffer;
  out.address=device->vk.GetBufferDeviceAddress(device->device,&address);
  if (!out.address) {destroy(out);return false;}
  out.bytes=req.size; return true;
}
}

const void* device_features(InstanceData* id, VkPhysicalDevice physical, const void* next,
                            std::vector<const char*>& extensions) {
  requested=false;
  if (creationSuppressed || !cfg_int("rt_omm",0) || !id || !id->GetPhysicalDeviceFeatures2) return next;
  const auto* bda=reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(
      find(next,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES));
  const auto* v12=reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(
      find(next,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES));
  // Our allocations use the core DEVICE_ADDRESS memory flag. The diagnostic
  // EXT-only BDA path does not certify this allocation prerequisite.
  if (!(bda && bda->bufferDeviceAddress) && !(v12 && v12->bufferDeviceAddress)) {
    log("[omm] enabled core bufferDeviceAddress required; ordinary RT retained");return next;
  }
  VkPhysicalDeviceProperties props{}; id->GetPhysicalDeviceProperties(physical,&props);
  // Initial backend deliberately requires core 1.3 synchronization2. Keep the
  // ordinary RT 1.2 path intact rather than adding extension dependency chains.
  if (id->api_version < VK_API_VERSION_1_3 || props.apiVersion < VK_API_VERSION_1_3) {
    log("[omm] EXT backend requires Vulkan 1.3; ordinary RT retained"); return next;
  }
  uint32_t count{}; id->EnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr);
  std::vector<VkExtensionProperties> ext(count);
  if (id->EnumerateDeviceExtensionProperties(physical,nullptr,&count,ext.data())!=VK_SUCCESS) return next;
  bool has=false; for (const auto& e:ext) has |= !strcmp(e.extensionName,VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
  if (!has) {log("[omm] VK_EXT_opacity_micromap unavailable; ordinary RT retained");return next;}
  VkPhysicalDeviceTimelineSemaphoreFeatures t{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceSynchronization2Features s{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,&t};
  VkPhysicalDeviceOpacityMicromapFeaturesEXT f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT,&s};
  VkPhysicalDeviceFeatures2 all{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&f};
  id->GetPhysicalDeviceFeatures2(physical,&all);
  if (!f.micromap || !s.synchronization2 || !t.timelineSemaphore) {log("[omm] micromap/synchronization2/timeline unsupported; ordinary RT retained");return next;}
  auto* existing= reinterpret_cast<const VkPhysicalDeviceOpacityMicromapFeaturesEXT*>(find(next,f.sType));
  auto* existingSync= reinterpret_cast<const VkPhysicalDeviceSynchronization2Features*>(find(next,s.sType));
  auto* v13= reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(find(next,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES));
  auto* existingTimeline=reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(find(next,t.sType));
  if ((existing && !existing->micromap) || (existingSync && !existingSync->synchronization2) || (v13 && !v13->synchronization2) ||
      (v12 && !v12->timelineSemaphore) || (existingTimeline && !existingTimeline->timelineSemaphore)) {
    log("[omm] application explicitly disabled needed feature; ordinary RT retained");return next;
  }
  if (!existingTimeline && !v12) {timelineFeature={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};timelineFeature.timelineSemaphore=VK_TRUE;timelineFeature.pNext=const_cast<void*>(next);next=&timelineFeature;}
  if (!existingSync && !v13) {sync={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};sync.synchronization2=VK_TRUE;sync.pNext=const_cast<void*>(next);next=&sync;}
  if (!existing) {feature={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT};feature.micromap=VK_TRUE;feature.pNext=const_cast<void*>(next);next=&feature;}
  bool listed=false;for (auto e:extensions) listed |= !strcmp(e,VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
  if (!listed) extensions.push_back(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
  requested=true;
  log("[omm] requesting EXT micromap + synchronization2; four-state only, classification gate still required");
  return next;
}
bool device_requested() {return requested;}
void disable_device_request() {requested=false;creationSuppressed=true;}
void device_created(DeviceData* dd, bool rtEnabled) {
  std::lock_guard lock(mutex);
  if (device) return;
  device=dd; enabled=false;retained=false;
  if (!requested || !rtEnabled) return;
#define LOAD(name, member) member=reinterpret_cast<PFN_vk##name>(dd->gdpa(dd->device,"vk" #name))
  LOAD(CreateMicromapEXT,createMicromap); LOAD(DestroyMicromapEXT,destroyMicromap);
  LOAD(GetMicromapBuildSizesEXT,getSizes); LOAD(CmdBuildMicromapsEXT,buildMicromaps);
#undef LOAD
  auto properties2=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(dd->inst->gipa(dd->inst->instance,"vkGetPhysicalDeviceProperties2"));
  if (!createMicromap || !destroyMicromap || !getSizes || !buildMicromaps || !properties2 ||
      !dd->vk.GetBufferDeviceAddress || !dd->vk.CmdPipelineBarrier2) {
    log("[omm] missing EXT build/address/synchronization entry points; backend unavailable");return;
  }
  VkPhysicalDeviceAccelerationStructurePropertiesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  properties={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT,&as};
  VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&properties};properties2(dd->physical,&p);
  scratchAlignment=std::max<VkDeviceSize>(as.minAccelerationStructureScratchOffsetAlignment,1);
  maxBlasPrimitives=as.maxPrimitiveCount;
  enabled=submit_device_created(dd);
  if(!enabled){log("[omm] private completion timeline unavailable; backend disabled");return;}
  if(!content_device_created(dd))log("[omm content] unavailable; native asset content proof must decline");
  log("[omm] EXT backend available max_four_state_level=%u; no known-state quality gate passed",properties.maxOpacity4StateSubdivisionLevel);
}
bool available() {std::lock_guard lock(mutex);return enabled && !retained && cfg_int("rt_omm",0);}
bool available_for(DeviceData* expected) {std::lock_guard lock(mutex);return expected && expected==device && enabled && !retained && cfg_int("rt_omm",0);}
bool has_resources() {std::lock_guard lock(mutex);return !resources.empty();}
void retain_until_idle() {std::lock_guard lock(mutex);if(!retained)log("[omm] resource provenance/completion unavailable; retaining until device idle");retained=true;}
void reserve_submissions(std::span<const uint64_t> tickets) {
  std::lock_guard lock(mutex);
  for(auto ticket:tickets) {auto i=resources.find(ticket);if(i==resources.end()){retained=true;continue;}
    if(!i->second.life.reserve())retained=true;
  }
}
void release_submissions(std::span<const uint64_t> tickets) {
  std::lock_guard lock(mutex);
  for(auto ticket:tickets){auto i=resources.find(ticket);if(i!=resources.end() && !i->second.life.unreserve())retained=true;}
}
bool register_ticket_use(VkCommandBuffer cmd,uint64_t ticket) {
  std::lock_guard lock(mutex);auto i=resources.find(ticket);
  // Expected cache-eviction decline, not a loss of completion provenance.
  if(i!=resources.end() && i->second.life.owner_released)return false;
  if(device_of(cmd)!=device || i==resources.end() || i->second.life.retiring || !register_record_ticket(cmd,ticket)) {
    if(!retained)log("[omm] rejected ticket use without live ticket/tracked command recording; retaining until idle");
    retained=true;return false;
  }
  return true;
}

Ticket record(VkCommandBuffer cmd,const Input& input) {
  std::lock_guard lock(mutex);
  if (!enabled || retained || !cfg_int("rt_omm",0) || !cmd || device_of(cmd)!=device || !tracked_recording(cmd)) return 0;
  Plan plan;
  if (!make_plan(input.descriptors,input.dataBytes,properties.maxOpacity4StateSubdivisionLevel,plan) ||
      !input_ranges(plan,input.data,input.dataBytes,input.triangles,input.triangleBytes) ||
      (input.dataUsage & (VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) !=
        (VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ||
      (input.triangleUsage & (VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) !=
        (VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) return 0;
  Resource resource;resource.triangles=plan.triangles;resource.input_owner=input.input_owner;
  for (auto u:plan.usage) resource.usage.push_back({u.count,u.subdivisionLevel,u.format});
  VkMicromapBuildInfoEXT build{VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT};
  build.type=VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT; build.flags=VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
  build.mode=VK_BUILD_MICROMAP_MODE_BUILD_EXT; build.usageCountsCount=uint32_t(resource.usage.size());build.pUsageCounts=resource.usage.data();
  build.data.deviceAddress=input.data;build.triangleArray.deviceAddress=input.triangles;build.triangleArrayStride=sizeof(Triangle);
  VkMicromapBuildSizesInfoEXT size{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
  getSizes(device->device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&size);
  if (!size.micromapSize || size.buildScratchSize>UINT64_MAX-(scratchAlignment-1)) return 0;
  const VkDeviceSize scratchBytes=std::max<VkDeviceSize>(size.buildScratchSize,1)+scratchAlignment-1;
  if (size.micromapSize>budget || scratchBytes>budget-size.micromapSize || bytesLive>budget-size.micromapSize-scratchBytes) return 0;
  if (!allocate(size.micromapSize,VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,resource.storage)) return 0;
  if (!allocate(scratchBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,resource.scratch)) {destroy(resource.storage);return 0;}
  if(resource.storage.bytes>budget || resource.scratch.bytes>budget-resource.storage.bytes ||
     bytesLive>budget-resource.storage.bytes-resource.scratch.bytes) {
    destroy(resource.storage);destroy(resource.scratch);return 0;
  }
  VkMicromapCreateInfoEXT ci{VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT};
  ci.buffer=resource.storage.buffer;ci.size=size.micromapSize;ci.type=VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
  if (createMicromap(device->device,&ci,nullptr,&resource.micromap)!=VK_SUCCESS) {destroy(resource.storage);destroy(resource.scratch);return 0;}
  bytesLive+=resource.storage.bytes+resource.scratch.bytes;
  const Ticket ticket=nextTicket++;
  if(!register_record_ticket(cmd,ticket)){destroy(resource);return 0;}
  build.dstMicromap=resource.micromap;
  const auto remainder=resource.scratch.address%scratchAlignment;
  build.scratchData.deviceAddress=resource.scratch.address+(remainder?scratchAlignment-remainder:0);
  // Same-queue producer (compute or transfer) -> micromap input read. HOST writes
  // still require flushing by the input owner before queue submission.
  VkMemoryBarrier2 before{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  before.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;before.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;
  before.dstStageMask=VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
  before.dstAccessMask=VK_ACCESS_2_MICROMAP_READ_BIT_EXT | VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dep.memoryBarrierCount=1;dep.pMemoryBarriers=&before;
  device->vk.CmdPipelineBarrier2(cmd,&dep);buildMicromaps(cmd,1,&build);
  VkMemoryBarrier2 after{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  after.srcStageMask=VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;after.srcAccessMask=VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
  after.dstStageMask=VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;after.dstAccessMask=VK_ACCESS_2_MICROMAP_READ_BIT_EXT;
  dep.pMemoryBarriers=&after;device->vk.CmdPipelineBarrier2(cmd,&dep);
  resources.emplace(ticket,std::move(resource));
  log("[omm] recorded EXT build ticket=%llu triangles=%u storage=%llu scratch=%llu",ticket,plan.triangles,size.micromapSize,scratchBytes);
  return ticket;
}
bool attachment(Ticket t,uint32_t primitiveCount,VkAccelerationStructureTrianglesOpacityMicromapEXT& out) {
  std::lock_guard lock(mutex);auto it=resources.find(t);
  if (it==resources.end() || !it->second.life.accepts_new_use() || primitiveCount!=it->second.triangles) return false;
  auto& r=it->second;
  out={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT};
  out.indexType=VK_INDEX_TYPE_NONE_KHR;out.micromap=r.micromap;
  out.usageCountsCount=uint32_t(r.usage.size());out.pUsageCounts=r.usage.data();
  return true;
}
bool record_blas(VkCommandBuffer cmd,Ticket ticket,VkDeviceAddress vertices,VkDeviceSize vertexBytes,
                 uint32_t stride,uint32_t triangles,AttachedBlas* output) {
  if(output)*output={};
  if(!output || !blas_vertex_range(vertices,vertexBytes,stride,triangles))return false;
  std::lock_guard lock(mutex);auto it=resources.find(ticket);
  if(!enabled || retained || !cfg_int("rt_omm",0) || device_of(cmd)!=device || !tracked_recording(cmd) ||
     it==resources.end() || !it->second.life.accepts_new_use() || it->second.triangles!=triangles || it->second.blas ||
     triangles>maxBlasPrimitives)return false;
  auto& r=it->second;
  VkAccelerationStructureTrianglesOpacityMicromapEXT opacity{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT};
  opacity.indexType=VK_INDEX_TYPE_NONE_KHR;opacity.micromap=r.micromap;
  opacity.usageCountsCount=uint32_t(r.usage.size());opacity.pUsageCounts=r.usage.data();
  VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  geometry.geometry.triangles={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,&opacity};
  auto& tri=geometry.geometry.triangles;tri.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;
  tri.vertexData.deviceAddress=vertices;tri.vertexStride=stride;tri.maxVertex=triangles*3u-1u;tri.indexType=VK_INDEX_TYPE_NONE_KHR;
  VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geometry;
  VkAccelerationStructureBuildSizesInfoKHR size{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  device->vk.GetAccelerationStructureBuildSizesKHR(device->device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&triangles,&size);
  if(!size.accelerationStructureSize || size.buildScratchSize>UINT64_MAX-(scratchAlignment-1u))return false;
  const VkDeviceSize scratchBytes=std::max<VkDeviceSize>(size.buildScratchSize,1)+scratchAlignment-1;
  Buffer storage{},scratch{};
  if(!allocate(size.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,storage))return false;
  if(!allocate(scratchBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,scratch)){destroy(storage);return false;}
  if(storage.bytes>budget || scratch.bytes>budget-storage.bytes || bytesLive>budget-storage.bytes-scratch.bytes){destroy(storage);destroy(scratch);return false;}
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.type=build.type;ci.buffer=storage.buffer;ci.size=size.accelerationStructureSize;
  VkAccelerationStructureKHR blas{};
  if(device->vk.CreateAccelerationStructureKHR(device->device,&ci,nullptr,&blas)!=VK_SUCCESS){destroy(storage);destroy(scratch);return false;}
  VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};ai.accelerationStructure=blas;
  const auto address=device->vk.GetAccelerationStructureDeviceAddressKHR(device->device,&ai);
  if(!address || !register_record_ticket(cmd,ticket)){
    device->vk.DestroyAccelerationStructureKHR(device->device,blas,nullptr);destroy(storage);destroy(scratch);return false;
  }
  r.blas=blas;r.blas_storage=storage;r.blas_scratch=scratch;bytesLive+=storage.bytes+scratch.bytes;
  build.dstAccelerationStructure=blas;
  const auto rem=scratch.address%scratchAlignment;build.scratchData.deviceAddress=scratch.address+(rem?scratchAlignment-rem:0);
  VkMemoryBarrier2 before{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};before.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  before.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;before.dstStageMask=VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
  before.dstAccessMask=VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_2_MICROMAP_READ_BIT_EXT;
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dep.memoryBarrierCount=1;dep.pMemoryBarriers=&before;
  device->vk.CmdPipelineBarrier2(cmd,&dep);
  VkAccelerationStructureBuildRangeInfoKHR range{triangles,0,0,0};const auto* ranges=&range;
  device->vk.CmdBuildAccelerationStructuresKHR(cmd,1,&build,&ranges);
  before.srcStageMask=VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;before.srcAccessMask=VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  before.dstStageMask=VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  before.dstAccessMask=VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;device->vk.CmdPipelineBarrier2(cmd,&dep);
  *output={blas,address,storage.bytes,scratch.bytes};return true;
}
bool mark_submitted(Ticket t,uint64_t serial) {std::lock_guard lock(mutex);auto i=resources.find(t);return i!=resources.end() && i->second.life.use(serial);}
void release_ticket(Ticket t) {
  std::lock_guard lock(mutex);auto i=resources.find(t);if(i==resources.end())return;
  i->second.life.release_owner();
  // Pending work with no surviving recording/reservation never reached GPU.
  if(!retained && i->second.life.releasable(0) && !recorded_reference(t)){
    destroy(i->second);resources.erase(i);
  }
}
bool retire(Ticket t) {std::lock_guard lock(mutex);auto i=resources.find(t);return i!=resources.end() && i->second.life.retire();}
void collect(uint64_t completed) {
  std::lock_guard lock(mutex);
  if(retained)return;
  for (auto i=resources.begin();i!=resources.end();) if (i->second.life.releasable(completed) && !recorded_reference(i->first)) {destroy(i->second);i=resources.erase(i);} else ++i;
}
bool discard_unsubmitted(Ticket t) {
  std::lock_guard lock(mutex);auto i=resources.find(t);
  if(retained || i==resources.end() || !i->second.life.discardable() || recorded_reference(t)) return false;
  destroy(i->second);resources.erase(i);return true;
}
void device_destroyed(DeviceData* dd) {
  std::lock_guard lock(mutex);if(dd!=device)return;
  if (!resources.empty()) {
    // vkDestroyDevice requires completed work. Wait explicitly as a last-resort
    // teardown guard; ordinary per-scene retirement never waits for device idle.
    const auto status=device->vk.DeviceWaitIdle(device->device);
    if(status!=VK_SUCCESS) log("[omm] device idle at teardown returned %d",status);
    for(auto& [ticket,r]:resources)destroy(r);
    resources.clear();
  }
  device=nullptr;enabled=false;requested=false;bytesLive=0;retained=false;creationSuppressed=false;
}
static_assert(sizeof(Triangle)==sizeof(VkMicromapTriangleEXT));
static_assert(offsetof(Triangle,dataOffset)==offsetof(VkMicromapTriangleEXT,dataOffset));
static_assert(offsetof(Triangle,subdivisionLevel)==offsetof(VkMicromapTriangleEXT,subdivisionLevel));
static_assert(offsetof(Triangle,format)==offsetof(VkMicromapTriangleEXT,format));
}
