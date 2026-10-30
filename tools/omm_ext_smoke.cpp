// Synthetic console-only Vulkan smoke test. Never opens windows or launches BG3.
// Links the production EXT backend/submission code with these minimal host seams.
#include "../layer/host.h"
#include "../layer/omm_backend.h"
#include "../layer/omm_submit.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>
#include <atomic>
namespace bg3 {
static DeviceData* smokeDevice;
void log(const char* fmt,...){va_list ap;va_start(ap,fmt);std::vprintf(fmt,ap);std::puts("");va_end(ap);}
int cfg_int(const char* key,int fallback){return !std::strcmp(key,"rt_omm")?1:fallback;}
DeviceData* device_of(const void*){return smokeDevice;}
}
using namespace bg3;
static std::atomic<unsigned> validationErrors{};
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*) {
  if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++validationErrors;std::fprintf(stderr,"VALIDATION %s\n",data->pMessage);}
  return VK_FALSE;
}
static void check(bool yes,const char* why){if(!yes){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static void vkcheck(VkResult result,const char* why){if(result!=VK_SUCCESS){std::fprintf(stderr,"FAIL %s VkResult=%d\n",why,result);std::exit(1);}}
struct Buffer {
  DeviceData* dd{};VkBuffer handle{};VkDeviceMemory memory{};VkDeviceAddress address{};void* mapped{};
  ~Buffer(){if(!dd)return;if(mapped)dd->vk.UnmapMemory(dd->device,memory);if(handle)dd->vk.DestroyBuffer(dd->device,handle,nullptr);if(memory)dd->vk.FreeMemory(dd->device,memory,nullptr);}
};
static void buffer(DeviceData& dd,Buffer& b,VkDeviceSize size,VkBufferUsageFlags usage,bool host=true){
  b.dd=&dd;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;ci.usage=usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  vkcheck(dd.vk.CreateBuffer(dd.device,&ci,nullptr,&b.handle),"create buffer");
  VkMemoryRequirements req{};dd.vk.GetBufferMemoryRequirements(dd.device,b.handle,&req);
  VkPhysicalDeviceMemoryProperties memory{};dd.inst->GetPhysicalDeviceMemoryProperties(dd.physical,&memory);
  const auto required=host?(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT):VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  uint32_t type=UINT32_MAX;for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&required)==required){type=i;break;}
  check(type!=UINT32_MAX,"memory type");
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&flags};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
  vkcheck(dd.vk.AllocateMemory(dd.device,&ai,nullptr,&b.memory),"allocate memory");vkcheck(dd.vk.BindBufferMemory(dd.device,b.handle,b.memory,0),"bind buffer");
  VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address.buffer=b.handle;b.address=dd.vk.GetBufferDeviceAddress(dd.device,&address);
  check(b.address && !(b.address%256),"256-aligned device address");if(host)vkcheck(dd.vk.MapMemory(dd.device,b.memory,0,size,0,&b.mapped),"map buffer");
}
static VkAccelerationStructureKHR acceleration(DeviceData& dd,Buffer& storage,VkAccelerationStructureTypeKHR type,VkDeviceSize bytes){
  buffer(dd,storage,bytes,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,false);
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};ci.buffer=storage.handle;ci.size=bytes;ci.type=type;
  VkAccelerationStructureKHR result{};vkcheck(dd.vk.CreateAccelerationStructureKHR(dd.device,&ci,nullptr,&result),"create AS");return result;
}
static void barrier(DeviceData& dd,VkCommandBuffer cmd,VkPipelineStageFlags2 source,VkAccessFlags2 access,VkPipelineStageFlags2 target,VkAccessFlags2 read){
  VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};b.srcStageMask=source;b.srcAccessMask=access;b.dstStageMask=target;b.dstAccessMask=read;
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dep.memoryBarrierCount=1;dep.pMemoryBarriers=&b;dd.vk.CmdPipelineBarrier2(cmd,&dep);
}
int main(int argc,char** argv){
  check(argc==2 || argc==3,"usage: omm_ext_smoke.exe ray_query.spv [endpoints.spv]");
  // Process-local loader settings only. The parent also sets these before launch.
  SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE","~implicit~");SetEnvironmentVariableA("VK_INSTANCE_LAYERS",nullptr);
  HMODULE loader=LoadLibraryA("vulkan-1.dll");check(loader!=nullptr,"existing Vulkan loader");
  auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));check(gipa!=nullptr,"get instance proc addr");
  auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE,"vkCreateInstance"));
  auto enumerateLayers=reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(gipa(VK_NULL_HANDLE,"vkEnumerateInstanceLayerProperties"));
  uint32_t layerCount{};vkcheck(enumerateLayers(&layerCount,nullptr),"layer count");std::vector<VkLayerProperties> layers(layerCount);vkcheck(enumerateLayers(&layerCount,layers.data()),"layer list");
  bool validationAvailable=false;for(const auto& layer:layers)validationAvailable|=!strcmp(layer.layerName,"VK_LAYER_KHRONOS_validation");
  auto enumerateExtensions=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(gipa(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties"));
  uint32_t extensionCount{};vkcheck(enumerateExtensions(nullptr,&extensionCount,nullptr),"instance extension count");std::vector<VkExtensionProperties> instanceExtensions(extensionCount);vkcheck(enumerateExtensions(nullptr,&extensionCount,instanceExtensions.data()),"instance extensions");
  bool debugAvailable=false;for(const auto& extension:instanceExtensions)debugAvailable|=!strcmp(extension.extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  const char* validationLayer="VK_LAYER_KHRONOS_validation";const char* debugExtension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;debug.pfnUserCallback=validation;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="OMM synthetic console smoke";app.apiVersion=VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;
  if(validationAvailable){ici.enabledLayerCount=1;ici.ppEnabledLayerNames=&validationLayer;}
  if(validationAvailable && debugAvailable){ici.enabledExtensionCount=1;ici.ppEnabledExtensionNames=&debugExtension;ici.pNext=&debug;}
  InstanceData instance;vkcheck(createInstance(&ici,nullptr,&instance.instance),"create instance");instance.gipa=gipa;instance.api_version=app.apiVersion;
  VkDebugUtilsMessengerEXT messenger{};
  if(validationAvailable && debugAvailable){auto createDebug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(gipa(instance.instance,"vkCreateDebugUtilsMessengerEXT"));vkcheck(createDebug(instance.instance,&debug,nullptr,&messenger),"debug messenger");}
  std::printf("implicit_layers=disabled explicit_validation=%d debug_error_counting=%d\n",validationAvailable,messenger!=VK_NULL_HANDLE);
#define LOAD_I(n) instance.n=reinterpret_cast<PFN_vk##n>(gipa(instance.instance,"vk" #n))
  LOAD_I(GetPhysicalDeviceFeatures2);LOAD_I(GetPhysicalDeviceProperties);LOAD_I(GetPhysicalDeviceMemoryProperties);LOAD_I(EnumerateDeviceExtensionProperties);LOAD_I(DestroyInstance);
#undef LOAD_I
  auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance.instance,"vkEnumeratePhysicalDevices"));
  uint32_t count{};vkcheck(enumerate(instance.instance,&count,nullptr),"enumerate device count");check(count!=0,"physical device present");
  std::vector<VkPhysicalDevice> physical(count);vkcheck(enumerate(instance.instance,&count,physical.data()),"enumerate devices");
  DeviceData dd;dd.inst=&instance;dd.physical=physical[0];
  VkPhysicalDeviceProperties props{};instance.GetPhysicalDeviceProperties(dd.physical,&props);
  std::printf("GPU=%s vendor=%04x device=%04x driver_raw=%u API=%u.%u.%u\n",props.deviceName,props.vendorID,props.deviceID,props.driverVersion,VK_API_VERSION_MAJOR(props.apiVersion),VK_API_VERSION_MINOR(props.apiVersion),VK_API_VERSION_PATCH(props.apiVersion));
  std::printf("subTexelPrecisionBits=%u mipmapPrecisionBits=%u\n",props.limits.subTexelPrecisionBits,props.limits.mipmapPrecisionBits);
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceSynchronization2Features sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,&timeline};
  VkPhysicalDeviceOpacityMicromapFeaturesEXT mm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT,&sync};
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,&mm};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&rq};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,&as};
  VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&bda};instance.GetPhysicalDeviceFeatures2(dd.physical,&f);
  uint32_t ne{};vkcheck(instance.EnumerateDeviceExtensionProperties(dd.physical,nullptr,&ne,nullptr),"extension count");std::vector<VkExtensionProperties> extensions(ne);
  vkcheck(instance.EnumerateDeviceExtensionProperties(dd.physical,nullptr,&ne,extensions.data()),"extension list");
  bool ext=false,push=false;for(const auto& e:extensions){ext|=!strcmp(e.extensionName,VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);push|=!strcmp(e.extensionName,VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);}
  std::printf("EXT=%d AS=%u rayQuery=%u BDA=%u sync2=%u timeline=%u pushDescriptor=%d\n",ext,as.accelerationStructure,rq.rayQuery,bda.bufferDeviceAddress,sync.synchronization2,timeline.timelineSemaphore,push);
  if(!ext||!as.accelerationStructure||!rq.rayQuery||!bda.bufferDeviceAddress||!sync.synchronization2||!timeline.timelineSemaphore||!push){std::puts("UNSUPPORTED synthetic OMM test NOT PASSED");instance.DestroyInstance(instance.instance,nullptr);return 77;}
  auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
  uint32_t nf{};families(dd.physical,&nf,nullptr);std::vector<VkQueueFamilyProperties> qs(nf);families(dd.physical,&nf,qs.data());
  for(uint32_t i=0;i<nf;++i)if((qs[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){dd.graphics_family=i;break;}
  check(dd.graphics_family!=UINT32_MAX,"compute-capable graphics queue");float priority=1;
  VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=dd.graphics_family;queue.queueCount=1;queue.pQueuePriorities=&priority;
  // Explicit prerequisites; production negotiation adds only its EXT/sync/timeline.
  rq={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};rq.rayQuery=VK_TRUE;
  as={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&rq};as.accelerationStructure=VK_TRUE;
  bda={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,&as};bda.bufferDeviceAddress=VK_TRUE;
  std::vector<const char*> enabled{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
  const void* chain=omm::device_features(&instance,dd.physical,&bda,enabled);check(omm::device_requested(),"production EXT request enabled");
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,chain};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&queue;dci.enabledExtensionCount=uint32_t(enabled.size());dci.ppEnabledExtensionNames=enabled.data();
  auto createDevice=reinterpret_cast<PFN_vkCreateDevice>(gipa(instance.instance,"vkCreateDevice"));vkcheck(createDevice(dd.physical,&dci,nullptr,&dd.device),"create device");
  dd.gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance.instance,"vkGetDeviceProcAddr"));
#define LOAD_D(n) dd.vk.n=reinterpret_cast<PFN_vk##n>(dd.gdpa(dd.device,"vk" #n));if(!dd.vk.n)dd.vk.n=reinterpret_cast<PFN_vk##n>(dd.gdpa(dd.device,"vk" #n "KHR"));
  BG3RTX_DEVICE_FUNCS(LOAD_D)
#undef LOAD_D
  dd.vk.GetDeviceQueue(dd.device,dd.graphics_family,0,&dd.graphics_queue);smokeDevice=&dd;
  omm::device_created(&dd,true);check(omm::available(),"production EXT backend available");
  VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  VkPhysicalDeviceProperties2 allProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&asProperties};
  auto properties2=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance.instance,"vkGetPhysicalDeviceProperties2"));properties2(dd.physical,&allProperties);
  const VkDeviceSize scratchAlignment=asProperties.minAccelerationStructureScratchOffsetAlignment;
  check(scratchAlignment!=0,"reported AS scratch alignment");
  const auto alignedScratch=[&](VkDeviceAddress address){return address+(scratchAlignment-address%scratchAlignment)%scratchAlignment;};
  // A scope guarantees all helper-owned buffers die before VkDevice destruction.
  {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pci.queueFamilyIndex=dd.graphics_family;
    VkCommandPool pool{};vkcheck(dd.vk.CreateCommandPool(dd.device,&pci,nullptr,&pool),"command pool");omm::pool_created(pool,dd.graphics_family);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;
    VkCommandBuffer cmd{};vkcheck(dd.vk.AllocateCommandBuffers(dd.device,&cai,&cmd),"allocate command");omm::command_allocated(pool,cmd);
    struct Inputs {Buffer data,descriptors;};auto inputs=std::make_shared<Inputs>();
    constexpr auto inputUsage=VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    buffer(dd,inputs->data,256,inputUsage);buffer(dd,inputs->descriptors,256,inputUsage);
    const uint32_t states[]={1,0,3};std::memcpy(inputs->data.mapped,states,sizeof(states));
    const omm::Triangle descriptors[]={{0,0,2},{4,0,2},{8,0,2}};std::memcpy(inputs->descriptors.mapped,descriptors,sizeof(descriptors));
    Buffer vertices,instances,result,blasStorage,tlasStorage,blasScratch,tlasScratch,texels;
    buffer(dd,vertices,256,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    float v[27]{};for(unsigned p=0;p<3;++p){float x=float(p)*4;const float tri[]={x,0,0,x+1,0,0,x,1,0};std::memcpy(v+p*9,tri,sizeof(tri));}std::memcpy(vertices.mapped,v,sizeof(v));
    buffer(dd,instances,256,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    buffer(dd,result,65536,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);std::memset(result.mapped,0xcd,65536);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(dd.vk.BeginCommandBuffer(cmd,&begin),"begin command");omm::command_begin(cmd);
    omm::Input input{};input.descriptors=descriptors;input.data=inputs->data.address;input.triangles=inputs->descriptors.address;input.dataBytes=input.triangleBytes=256;input.dataUsage=input.triangleUsage=inputUsage;input.input_owner=inputs;
    const auto ticket=omm::record(cmd,input);check(ticket!=0,"production micromap record");
    VkAccelerationStructureTrianglesOpacityMicromapEXT attachment{};check(omm::attachment(ticket,3,attachment),"production matching attachment");
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,&attachment};
    geometry.geometry.triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;geometry.geometry.triangles.vertexData.deviceAddress=vertices.address;geometry.geometry.triangles.vertexStride=12;geometry.geometry.triangles.maxVertex=8;geometry.geometry.triangles.indexType=VK_INDEX_TYPE_NONE_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geometry;
    uint32_t primitiveCount=3;VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};dd.vk.GetAccelerationStructureBuildSizesKHR(dd.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&primitiveCount,&sizes);
    auto blas=acceleration(dd,blasStorage,build.type,sizes.accelerationStructureSize);buffer(dd,blasScratch,sizes.buildScratchSize+scratchAlignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);
    build.dstAccelerationStructure=blas;build.scratchData.deviceAddress=alignedScratch(blasScratch.address);
    VkAccelerationStructureBuildRangeInfoKHR range{3,0,0,0};const auto* rangePointer=&range;dd.vk.CmdBuildAccelerationStructuresKHR(cmd,1,&build,&rangePointer);
    barrier(dd,cmd,VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    VkAccelerationStructureDeviceAddressInfoKHR address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};address.accelerationStructure=blas;
    VkAccelerationStructureInstanceKHR inst{};inst.transform.matrix[0][0]=inst.transform.matrix[1][1]=inst.transform.matrix[2][2]=1;inst.mask=255;inst.accelerationStructureReference=dd.vk.GetAccelerationStructureDeviceAddressKHR(dd.device,&address);std::memcpy(instances.mapped,&inst,sizeof(inst));
    VkAccelerationStructureGeometryKHR top{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};top.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;top.geometry.instances={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};top.geometry.instances.data.deviceAddress=instances.address;
    build.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;build.pGeometries=&top;build.dstAccelerationStructure=VK_NULL_HANDLE;primitiveCount=1;sizes={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};dd.vk.GetAccelerationStructureBuildSizesKHR(dd.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&primitiveCount,&sizes);
    auto tlas=acceleration(dd,tlasStorage,build.type,sizes.accelerationStructureSize);buffer(dd,tlasScratch,sizes.buildScratchSize+scratchAlignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);build.dstAccelerationStructure=tlas;build.scratchData.deviceAddress=alignedScratch(tlasScratch.address);range.primitiveCount=1;dd.vk.CmdBuildAccelerationStructuresKHR(cmd,1,&build,&rangePointer);
    barrier(dd,cmd,VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    VkImage endpointImage{};VkDeviceMemory endpointMemory{};VkImageView endpointView{};VkSampler endpointSampler{};
    if(argc==3){
      buffer(dd,texels,256,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);std::memset(texels.mapped,0,84);std::memset(static_cast<char*>(texels.mapped)+84,255,84);
      VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_UNORM;image.extent={4,4,1};image.mipLevels=3;image.arrayLayers=2;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
      vkcheck(dd.vk.CreateImage(dd.device,&image,nullptr,&endpointImage),"endpoint image");
      VkMemoryRequirements req{};dd.vk.GetImageMemoryRequirements(dd.device,endpointImage,&req);VkPhysicalDeviceMemoryProperties memory{};instance.GetPhysicalDeviceMemoryProperties(dd.physical,&memory);
      uint32_t type=UINT32_MAX;for(uint32_t j=0;j<memory.memoryTypeCount;++j)if((req.memoryTypeBits&(1u<<j)) && (memory.memoryTypes[j].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=j;break;}check(type!=UINT32_MAX,"endpoint memory type");
      VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;vkcheck(dd.vk.AllocateMemory(dd.device,&alloc,nullptr,&endpointMemory),"endpoint memory");vkcheck(dd.vk.BindImageMemory(dd.device,endpointImage,endpointMemory,0),"endpoint image bind");
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=endpointImage;view.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,2};vkcheck(dd.vk.CreateImageView(dd.device,&view,nullptr,&endpointView),"endpoint image view");
      VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;sampler.maxLod=2;vkcheck(dd.vk.CreateSampler(dd.device,&sampler,nullptr,&endpointSampler),"endpoint linear sampler");
      VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};imageBarrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;imageBarrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;imageBarrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;imageBarrier.srcQueueFamilyIndex=imageBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;imageBarrier.image=endpointImage;imageBarrier.subresourceRange=view.subresourceRange;
      dd.vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&imageBarrier);
      VkBufferImageCopy copies[6]{};uint32_t offset=0;
      for(uint32_t layer=0;layer<2;++layer)for(uint32_t mip=0;mip<3;++mip){auto& copy=copies[layer*3+mip];copy.bufferOffset=offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,layer,1};copy.imageExtent={4u>>mip,4u>>mip,1};offset+=(4u>>mip)*(4u>>mip)*4;}
      dd.vk.CmdCopyBufferToImage(cmd,texels.handle,endpointImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,6,copies);
      imageBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;imageBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;imageBarrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;imageBarrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;dd.vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&imageBarrier);
    }
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate);check(bool(file),"shader file");auto bytes=file.tellg();check(bytes>0 && uint64_t(bytes)%4==0,"shader byte count");std::vector<uint32_t> code(size_t(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smci.codeSize=code.size()*4;smci.pCode=code.data();VkShaderModule shader{};vkcheck(dd.vk.CreateShaderModule(dd.device,&smci,nullptr,&shader),"shader module");
    VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dlci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;dlci.bindingCount=4;dlci.pBindings=bindings;VkDescriptorSetLayout layout{};vkcheck(dd.vk.CreateDescriptorSetLayout(dd.device,&dlci,nullptr,&layout),"descriptor layout");
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};plci.setLayoutCount=1;plci.pSetLayouts=&layout;VkPipelineLayout pipelineLayout{};vkcheck(dd.vk.CreatePipelineLayout(dd.device,&plci,nullptr,&pipelineLayout),"pipeline layout");
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cpci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};cpci.layout=pipelineLayout;VkPipeline pipeline{};vkcheck(dd.vk.CreateComputePipelines(dd.device,VK_NULL_HANDLE,1,&cpci,nullptr,&pipeline),"compute pipeline");
    VkWriteDescriptorSetAccelerationStructureKHR asWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};asWrite.accelerationStructureCount=1;asWrite.pAccelerationStructures=&tlas;
    VkDescriptorBufferInfo output{result.handle,0,65536};VkWriteDescriptorSet writes[2]{};writes[0]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,&asWrite};writes[0].dstBinding=0;writes[0].descriptorCount=1;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;writes[1]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[1].dstBinding=1;writes[1].descriptorCount=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&output;
    check(omm::register_ticket_use(cmd,ticket),"register actual TLAS traversal use");dd.vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);dd.vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,2,writes);dd.vk.CmdDispatch(cmd,1,1,1);
    VkPipeline endpointPipeline{};VkShaderModule endpointShader{};
    if(argc==3){
      std::ifstream endpointFile(argv[2],std::ios::binary|std::ios::ate);check(bool(endpointFile),"endpoint shader file");auto length=endpointFile.tellg();check(length>0 && uint64_t(length)%4==0,"endpoint shader size");std::vector<uint32_t> endpointCode(size_t(length)/4);endpointFile.seekg(0);endpointFile.read(reinterpret_cast<char*>(endpointCode.data()),length);
      smci.codeSize=endpointCode.size()*4;smci.pCode=endpointCode.data();vkcheck(dd.vk.CreateShaderModule(dd.device,&smci,nullptr,&endpointShader),"endpoint shader");cpci.stage.module=endpointShader;cpci.stage.pName="endpoints";vkcheck(dd.vk.CreateComputePipelines(dd.device,VK_NULL_HANDLE,1,&cpci,nullptr,&endpointPipeline),"endpoint pipeline");
      VkDescriptorImageInfo image{VK_NULL_HANDLE,endpointView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},sampler{endpointSampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};VkWriteDescriptorSet descriptors[2]{};
      for(uint32_t j=0;j<2;++j){descriptors[j]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};descriptors[j].dstBinding=2+j;descriptors[j].descriptorCount=1;descriptors[j].descriptorType=j?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;descriptors[j].pImageInfo=j?&sampler:&image;}
      dd.vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,endpointPipeline);dd.vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,2,descriptors);dd.vk.CmdDispatch(cmd,34,1,1);
    }
    barrier(dd,cmd,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_2_HOST_BIT,VK_ACCESS_2_HOST_READ_BIT);
    vkcheck(dd.vk.EndCommandBuffer(cmd),"end command");omm::command_end(cmd,true);
    const VkCommandBuffer commands[]={cmd};const auto prepared=omm::before_submit(&dd,commands);VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=commands;
    const auto sent=dd.vk.QueueSubmit(dd.graphics_queue,1,&submit,VK_NULL_HANDLE);omm::after_submit(&dd,dd.graphics_queue,prepared,sent==VK_SUCCESS);vkcheck(sent,"synthetic submit");vkcheck(dd.vk.QueueWaitIdle(dd.graphics_queue),"synthetic queue idle");
    auto* answers=static_cast<uint32_t*>(result.mapped);unsigned contradictions=0,unknownCandidates=0;
    for(unsigned i=0;i<6;++i){auto* a=answers+i*4;std::printf("ray=%u hit=%u candidates=%u native_ge=%u primitive=%u\n",i,a[0],a[1],a[2],a[3]);contradictions+=a[0]!=a[2];unknownCandidates+=i>=4?a[1]:0;check(a[1]==(i>=4?1u:0u),"known automatic vs unknown alpha candidate path");}
    check(!contradictions && unknownCandidates==2,"zero known/native contradictions and actual unknown fallback");
    if(argc==3){unsigned failures=0;for(unsigned i=0;i<2176;++i){auto* a=answers+(6+i)*4;const uint32_t layer=i/(64*17);failures+=a[0]!=(layer?0x3f800000u:0u) || a[1]!=layer || a[2]!=i%17 || a[3]!=(i/17)%64;}
      std::printf("UNORM8_ENDPOINTS samples=2176 UVs=64 LODs=17 mips=3 layers=2 filter=LINEAR mipmap=LINEAR repeat exact_bit_failures=%u\n",failures);check(!failures,"exact constant zero/one endpoint samples");}
    vkcheck(dd.vk.ResetCommandBuffer(cmd,0),"reset completed command");omm::command_discard(cmd);check(omm::retire(ticket),"retire ticket");
    input.input_owner.reset();inputs.reset();
    // An actual empty app submit polls the private timeline through production
    // code. It does not manufacture a completed serial from frame age or idle.
    const auto empty=omm::before_submit(&dd,{});vkcheck(dd.vk.QueueSubmit(dd.graphics_queue,0,nullptr,VK_NULL_HANDLE),"empty app submit");omm::after_submit(&dd,dd.graphics_queue,empty,true);check(!omm::has_resources(),"actual timeline retirement collected ticket");
    dd.vk.DestroyPipeline(dd.device,pipeline,nullptr);dd.vk.DestroyPipelineLayout(dd.device,pipelineLayout,nullptr);dd.vk.DestroyDescriptorSetLayout(dd.device,layout,nullptr);dd.vk.DestroyShaderModule(dd.device,shader,nullptr);
    if(endpointPipeline)dd.vk.DestroyPipeline(dd.device,endpointPipeline,nullptr);if(endpointShader)dd.vk.DestroyShaderModule(dd.device,endpointShader,nullptr);
    if(endpointSampler)dd.vk.DestroySampler(dd.device,endpointSampler,nullptr);if(endpointView)dd.vk.DestroyImageView(dd.device,endpointView,nullptr);if(endpointImage)dd.vk.DestroyImage(dd.device,endpointImage,nullptr);if(endpointMemory)dd.vk.FreeMemory(dd.device,endpointMemory,nullptr);
    dd.vk.DestroyAccelerationStructureKHR(dd.device,tlas,nullptr);dd.vk.DestroyAccelerationStructureKHR(dd.device,blas,nullptr);dd.vk.DestroyCommandPool(dd.device,pool,nullptr);omm::pool_discard(pool,true);
    std::printf("PASS synthetic EXT: primitives=3 rays=6 contradictions=%u unknown_candidates=%u actual_timeline_retirement=1; not a BG3/FG/sampler proof\n",contradictions,unknownCandidates);
  }
  omm::device_destroyed(&dd);omm::submit_device_destroyed(&dd);dd.vk.DestroyDevice(dd.device,nullptr);
  if(messenger){auto destroyDebug=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(gipa(instance.instance,"vkDestroyDebugUtilsMessengerEXT"));destroyDebug(instance.instance,messenger,nullptr);}
  instance.DestroyInstance(instance.instance,nullptr);
  std::printf("validation_errors=%u\n",validationErrors.load());check(validationErrors==0,"zero validation errors");
}
