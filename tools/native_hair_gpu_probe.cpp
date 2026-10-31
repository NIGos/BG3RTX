// Windowless synthetic Vulkan fixture. Same private-loader/validation pattern as
// omm_ext_smoke; no game, layer hooks, user files, presentation or frame generation.
// Runs the real hair_bounds shader and all four real rt_common query functions.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "../layer/host.h"
#include "../common/native_scalp_coverage.h"
#include "../common/native_hit_records.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>
#pragma comment(lib,"vulkan-1.lib")
static VkDevice dev;static VkPhysicalDeviceMemoryProperties memory;
static std::atomic<unsigned> validationErrors{};
static void check(bool yes,const char* what){if(!yes){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
static void vkcheck(VkResult r,const char* what){if(r!=VK_SUCCESS){std::fprintf(stderr,"FAIL %s result=%d\n",what,r);std::exit(1);}}
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* info,void*){
 if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){if(validationErrors.fetch_add(1)<16)std::fprintf(stderr,"VALIDATION %s\n",info->pMessage);}return VK_FALSE;
}
static uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;check(false,"memory type");return 0;}
struct Buffer{
 VkBuffer b{};VkDeviceMemory m{};VkDeviceAddress address{};void* map{};VkDeviceSize size{};
 ~Buffer(){if(map)vkUnmapMemory(dev,m);if(b)vkDestroyBuffer(dev,b,nullptr);if(m)vkFreeMemory(dev,m,nullptr);}
};
static void make_buffer(Buffer& out,VkDeviceSize bytes,VkBufferUsageFlags usage,bool host=true){
 out.size=bytes;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;vkcheck(vkCreateBuffer(dev,&ci,nullptr,&out.b),"buffer");
 VkMemoryRequirements req;vkGetBufferMemoryRequirements(dev,out.b,&req);VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
 VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&flags};ai.allocationSize=req.size;ai.memoryTypeIndex=memory_type(req.memoryTypeBits,host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
 vkcheck(vkAllocateMemory(dev,&ai,nullptr,&out.m),"buffer memory");vkcheck(vkBindBufferMemory(dev,out.b,out.m,0),"bind buffer");
 VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address.buffer=out.b;out.address=vkGetBufferDeviceAddress(dev,&address);
 if(host){vkcheck(vkMapMemory(dev,out.m,0,bytes,0,&out.map),"map");std::memset(out.map,0,size_t(bytes));}
}
static std::vector<uint32_t> read_shader(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);check(bool(f),path);auto bytes=f.tellg();check(bytes>0&&uint64_t(bytes)%4==0,"shader size");std::vector<uint32_t> code(size_t(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(code.data()),bytes);check(bool(f),"shader read");return code;}
static VkPipeline pipeline(const char* path,VkPipelineLayout layout){auto code=read_shader(path);VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=code.size()*4;ci.pCode=code.data();VkShaderModule m;vkcheck(vkCreateShaderModule(dev,&ci,nullptr,&m),"shader module");
 VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,m,"main"};p.layout=layout;VkPipeline result;vkcheck(vkCreateComputePipelines(dev,VK_NULL_HANDLE,1,&p,nullptr,&result),"compute pipeline");vkDestroyShaderModule(dev,m,nullptr);return result;}
static void barrier(VkCommandBuffer c,VkPipelineStageFlags src,VkAccessFlags access,VkPipelineStageFlags dst,VkAccessFlags read){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=access;b.dstAccessMask=read;vkCmdPipelineBarrier(c,src,dst,0,1,&b,0,nullptr,0,nullptr);}
struct Vertex{float position[4],uv[2],n[3],t[3],b[3],native[3],phase[4];};static_assert(sizeof(Vertex)==88);
struct BoundsPush{uint32_t tris,step,soup,bounds;float vp[16],depth[4];uint32_t stride,camera,bounded,pad;};static_assert(sizeof(BoundsPush)==112);
static float as_float(uint32_t x){float f;std::memcpy(&f,&x,4);return f;}
static bool gpu_near(float x,float y){const float e=std::numeric_limits<float>::epsilon(),g=128*e/(1-128*e);return std::isfinite(x)&&std::abs(x-y)<=g*(std::abs(y)+1);}

int main(int argc,char** argv){
 const bool material=argc==7 && !std::strcmp(argv[4],"--material");
 const bool tileset=material || (argc==5 && !std::strcmp(argv[4],"--tileset"));
 check(argc==4 || tileset,"usage: native_hair_gpu_probe bounds.spv query.spv query_rebase.spv [--tileset | --material gather.spv join.spv]");
 SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE","~implicit~");SetEnvironmentVariableA("VK_INSTANCE_LAYERS",nullptr);
 uint32_t nl=0;vkcheck(vkEnumerateInstanceLayerProperties(&nl,nullptr),"layers");std::vector<VkLayerProperties> layers(nl);vkcheck(vkEnumerateInstanceLayerProperties(&nl,layers.data()),"layers list");bool haveValidation=false;for(auto& x:layers)haveValidation|=!std::strcmp(x.layerName,"VK_LAYER_KHRONOS_validation");
 check(haveValidation,"installed Khronos validation required (not a silent pass)");
 const char* layer="VK_LAYER_KHRONOS_validation";const char* debugExt=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
 VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;debug.pfnUserCallback=validation;
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Native Hair isolated console fixture";app.apiVersion=VK_API_VERSION_1_3;
 VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,&debug};ici.pApplicationInfo=&app;ici.enabledLayerCount=1;ici.ppEnabledLayerNames=&layer;ici.enabledExtensionCount=1;ici.ppEnabledExtensionNames=&debugExt;
 VkInstance instance;vkcheck(vkCreateInstance(&ici,nullptr,&instance),"instance");
 auto createDebug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));auto destroyDebug=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
 VkDebugUtilsMessengerEXT messenger;vkcheck(createDebug(instance,&debug,nullptr,&messenger),"debug messenger");
 uint32_t np=0;vkcheck(vkEnumeratePhysicalDevices(instance,&np,nullptr),"physical count");check(np>0,"physical device");std::vector<VkPhysicalDevice> physical(np);vkcheck(vkEnumeratePhysicalDevices(instance,&np,physical.data()),"physical list");VkPhysicalDevice gpu=physical[0];
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
 std::printf("GPU=%s driver=%u implicit_layers=disabled validation=1 real_presentation=none\n",props.deviceName,props.driverVersion);
 VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,&f12};VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&rq};VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&as};vkGetPhysicalDeviceFeatures2(gpu,&features);
 if(!as.accelerationStructure||!rq.rayQuery||!f12.bufferDeviceAddress||!f12.shaderSampledImageArrayNonUniformIndexing){std::puts("UNSUPPORTED (not passed)");destroyDebug(instance,messenger,nullptr);vkDestroyInstance(instance,nullptr);return 77;}
 uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,nullptr);std::vector<VkQueueFamilyProperties> families(nq);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families.data());uint32_t family=UINT32_MAX;for(uint32_t i=0;i<nq;++i)if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT){family=i;break;}check(family!=UINT32_MAX,"compute queue");
 const char* extensions[]={VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
 VkPhysicalDeviceVulkan12Features use12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};use12.bufferDeviceAddress=VK_TRUE;use12.shaderSampledImageArrayNonUniformIndexing=VK_TRUE;use12.scalarBlockLayout=f12.scalarBlockLayout;
 VkPhysicalDeviceRayQueryFeaturesKHR useRQ{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,&use12};useRQ.rayQuery=VK_TRUE;VkPhysicalDeviceAccelerationStructureFeaturesKHR useAS{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&useRQ};useAS.accelerationStructure=VK_TRUE;
 float priority=tileset?0.0f:1.0f;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
 VkPhysicalDeviceFeatures useCore{};useCore.shaderSampledImageArrayDynamicIndexing=features.features.shaderSampledImageArrayDynamicIndexing;
 VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&useAS};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=4;dci.ppEnabledExtensionNames=extensions;dci.pEnabledFeatures=&useCore;
 vkcheck(vkCreateDevice(gpu,&dci,nullptr,&dev),"device");VkQueue queue;vkGetDeviceQueue(dev,family,0,&queue);
#define PROC(type,name) auto name=reinterpret_cast<PFN_##type>(vkGetDeviceProcAddr(dev,#type));check(name!=nullptr,#type)
 PROC(vkCreateAccelerationStructureKHR,createAS);PROC(vkDestroyAccelerationStructureKHR,destroyAS);PROC(vkGetAccelerationStructureBuildSizesKHR,sizesAS);PROC(vkCmdBuildAccelerationStructuresKHR,buildAS);PROC(vkGetAccelerationStructureDeviceAddressKHR,addressAS);PROC(vkCmdPushDescriptorSetKHR,pushDescriptors);
#undef PROC
 VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&asProps};vkGetPhysicalDeviceProperties2(gpu,&properties);check(asProps.minAccelerationStructureScratchOffsetAlignment>0,"scratch alignment");
 {
 Buffer source,pool,records,info,stamp,nativeCamera,stats,result,instances,blasStore,tlasStore,scratch;
 const auto inputUsage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
 make_buffer(source,4096,inputUsage);make_buffer(pool,4096,inputUsage);make_buffer(records,4096,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);make_buffer(info,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
 make_buffer(stamp,512,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);make_buffer(nativeCamera,256,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
 make_buffer(stats,65536,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);make_buffer(result,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);make_buffer(instances,256,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
 VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_AABBS_KHR;geometry.flags=VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;geometry.geometry.aabbs={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,nullptr,{pool.address},24};
 const auto buildFlags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR|VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
 VkAccelerationStructureBuildGeometryInfoKHR bottom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};bottom.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;bottom.flags=buildFlags;bottom.geometryCount=1;bottom.pGeometries=&geometry;
 uint32_t primitives=3;VkAccelerationStructureBuildSizesInfoKHR bs{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};sizesAS(dev,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&bottom,&primitives,&bs);
 VkAccelerationStructureGeometryKHR scalpGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};scalpGeometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;
 scalpGeometry.geometry.triangles={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};scalpGeometry.geometry.triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;scalpGeometry.geometry.triangles.vertexData.deviceAddress=source.address;scalpGeometry.geometry.triangles.vertexStride=24;scalpGeometry.geometry.triangles.maxVertex=8;scalpGeometry.geometry.triangles.indexType=VK_INDEX_TYPE_NONE_KHR;
 auto scalpBuild=bottom;scalpBuild.pGeometries=&scalpGeometry;VkAccelerationStructureBuildSizesInfoKHR scalpSizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};sizesAS(dev,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&scalpBuild,&primitives,&scalpSizes);
 bs.accelerationStructureSize=(std::max)(bs.accelerationStructureSize,scalpSizes.accelerationStructureSize);bs.buildScratchSize=(std::max)(bs.buildScratchSize,scalpSizes.buildScratchSize);bs.updateScratchSize=(std::max)(bs.updateScratchSize,scalpSizes.updateScratchSize);
 make_buffer(blasStore,bs.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,false);VkAccelerationStructureCreateInfoKHR ac{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};ac.buffer=blasStore.b;ac.size=bs.accelerationStructureSize;ac.type=bottom.type;VkAccelerationStructureKHR blas;vkcheck(createAS(dev,&ac,nullptr,&blas),"BLAS");bottom.dstAccelerationStructure=blas;
 VkAccelerationStructureDeviceAddressInfoKHR ba{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};ba.accelerationStructure=blas;VkAccelerationStructureInstanceKHR object{};object.transform.matrix[0][0]=object.transform.matrix[1][1]=object.transform.matrix[2][2]=1;object.mask=255;object.accelerationStructureReference=addressAS(dev,&ba);std::memcpy(instances.map,&object,sizeof(object));
 VkAccelerationStructureGeometryKHR topGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};topGeometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;topGeometry.geometry.instances={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,nullptr,VK_FALSE,{instances.address}};
 VkAccelerationStructureBuildGeometryInfoKHR top{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};top.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;top.flags=buildFlags;top.geometryCount=1;top.pGeometries=&topGeometry;uint32_t one=1;VkAccelerationStructureBuildSizesInfoKHR ts{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};sizesAS(dev,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&top,&one,&ts);
 make_buffer(tlasStore,ts.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,false);ac.buffer=tlasStore.b;ac.size=ts.accelerationStructureSize;ac.type=top.type;VkAccelerationStructureKHR tlas;vkcheck(createAS(dev,&ac,nullptr,&tlas),"TLAS");top.dstAccelerationStructure=tlas;
 VkDeviceSize alignment=asProps.minAccelerationStructureScratchOffsetAlignment;make_buffer(scratch,(std::max)({bs.buildScratchSize,bs.updateScratchSize,ts.buildScratchSize,ts.updateScratchSize})+alignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);bottom.scratchData.deviceAddress=top.scratchData.deviceAddress=(scratch.address+alignment-1)/alignment*alignment;
 VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};imageInfo.imageType=VK_IMAGE_TYPE_2D;imageInfo.format=tileset?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;imageInfo.extent={1,1,1};imageInfo.mipLevels=1;imageInfo.arrayLayers=material?10u:1u;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;imageInfo.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;VkImage image;vkcheck(vkCreateImage(dev,&imageInfo,nullptr,&image),"image");
 VkMemoryRequirements imageReq;vkGetImageMemoryRequirements(dev,image,&imageReq);VkMemoryAllocateInfo imageAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};imageAlloc.allocationSize=imageReq.size;imageAlloc.memoryTypeIndex=memory_type(imageReq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);VkDeviceMemory imageMemory;vkcheck(vkAllocateMemory(dev,&imageAlloc,nullptr,&imageMemory),"image memory");vkcheck(vkBindImageMemory(dev,image,imageMemory,0),"image bind");
 VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=imageInfo.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkImageView view,arrayView;std::vector<VkImageView> materialViews;vkcheck(vkCreateImageView(dev,&vi,nullptr,&view),"2D view");vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;vkcheck(vkCreateImageView(dev,&vi,nullptr,&arrayView),"array view");
 if(material){vi.viewType=VK_IMAGE_VIEW_TYPE_2D;for(uint32_t i=1;i<=9;++i){vi.subresourceRange.baseArrayLayer=i;VkImageView v;vkcheck(vkCreateImageView(dev,&vi,nullptr,&v),"material native 2D view");materialViews.push_back(v);}}
 VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;si.maxLod=16;VkSampler sampler;vkcheck(vkCreateSampler(dev,&si,nullptr,&sampler),"sampler");
 VkDescriptorSetLayoutBinding set0Bindings[]={{18,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{21,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{48,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
 VkSampler nativeSampler{};if(tileset){auto native=si;native.addressModeU=native.addressModeV=native.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;native.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;vkcheck(vkCreateSampler(dev,&native,nullptr,&nativeSampler),"native sampler19");}
 VkDescriptorSetLayoutBinding set1Bindings[15];for(uint32_t b=0;b<10;++b)set1Bindings[b]={b,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[2]={2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4096,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[3]={3,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,16,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[4]={4,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[5]={5,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
 if(tileset)set1Bindings[10]={19,VK_DESCRIPTOR_TYPE_SAMPLER,16,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
 if(material){set1Bindings[11]={12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};set1Bindings[12]={13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};set1Bindings[13]={14,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,64,VK_SHADER_STAGE_COMPUTE_BIT};set1Bindings[14]={15,VK_DESCRIPTOR_TYPE_SAMPLER,16,VK_SHADER_STAGE_COMPUTE_BIT};}
 VkDescriptorSetLayout layouts[2];VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=4;dl.pBindings=set0Bindings;vkcheck(vkCreateDescriptorSetLayout(dev,&dl,nullptr,layouts),"set0 layout");dl.bindingCount=material?15u:tileset?11u:10u;dl.pBindings=set1Bindings;vkcheck(vkCreateDescriptorSetLayout(dev,&dl,nullptr,layouts+1),"set1 layout");
 VkDescriptorPoolSize poolSizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,material?10u:8u},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,material?4176u:4112u},{VK_DESCRIPTOR_TYPE_SAMPLER,material?34u:tileset?18u:2u},{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=2;dp.poolSizeCount=5;dp.pPoolSizes=poolSizes;VkDescriptorPool descriptorPool;vkcheck(vkCreateDescriptorPool(dev,&dp,nullptr,&descriptorPool),"descriptor pool");VkDescriptorSet sets[2];VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=descriptorPool;da.descriptorSetCount=2;da.pSetLayouts=layouts;vkcheck(vkAllocateDescriptorSets(dev,&da,sets),"sets");
 auto writeBuffer=[&](VkDescriptorSet set,uint32_t b,const Buffer& buffer,VkDescriptorType type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){VkDescriptorBufferInfo bi{buffer.b,0,buffer.size};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=b;w.descriptorCount=1;w.descriptorType=type;w.pBufferInfo=&bi;vkUpdateDescriptorSets(dev,1,&w,0,nullptr);};
 writeBuffer(sets[0],20,stats);writeBuffer(sets[0],21,result);writeBuffer(sets[0],48,nativeCamera,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);writeBuffer(sets[1],0,records);writeBuffer(sets[1],1,source);writeBuffer(sets[1],6,source);writeBuffer(sets[1],7,info);writeBuffer(sets[1],8,pool);writeBuffer(sets[1],9,stamp);
 VkWriteDescriptorSetAccelerationStructureKHR aw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};aw.accelerationStructureCount=1;aw.pAccelerationStructures=&tlas;VkWriteDescriptorSet writeAS{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,&aw};writeAS.dstSet=sets[0];writeAS.dstBinding=18;writeAS.descriptorCount=1;writeAS.descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;vkUpdateDescriptorSets(dev,1,&writeAS,0,nullptr);
 std::vector<VkDescriptorImageInfo> textures(4096,{VK_NULL_HANDLE,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}),caches(16,{VK_NULL_HANDLE,arrayView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});VkDescriptorImageInfo samp{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
 if(material)for(unsigned i=0;i<9;++i)textures[i+2]={VK_NULL_HANDLE,materialViews[i],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
 for(uint32_t b=2;b<=5;++b){VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=sets[1];w.dstBinding=b;w.descriptorType=b<4?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER;w.descriptorCount=b==2?4096:b==3?16:1;w.pImageInfo=b==2?textures.data():b==3?caches.data():&samp;vkUpdateDescriptorSets(dev,1,&w,0,nullptr);}
 if(tileset){std::vector<VkDescriptorImageInfo> native(16,{nativeSampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=sets[1];w.dstBinding=19;w.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;w.descriptorCount=16;w.pImageInfo=native.data();vkUpdateDescriptorSets(dev,1,&w,0,nullptr);}
 VkPushConstantRange queryPush{VK_SHADER_STAGE_COMPUTE_BIT,0,32};VkPipelineLayoutCreateInfo ql{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ql.setLayoutCount=2;ql.pSetLayouts=layouts;ql.pushConstantRangeCount=1;ql.pPushConstantRanges=&queryPush;VkPipelineLayout queryLayout;vkcheck(vkCreatePipelineLayout(dev,&ql,nullptr,&queryLayout),"query layout");VkPipeline query[2]={pipeline(argv[2],queryLayout),pipeline(argv[3],queryLayout)};
 VkDescriptorSetLayoutBinding boundsBindings[3];for(uint32_t b=0;b<3;++b)boundsBindings[b]={b,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};dl.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;dl.bindingCount=3;dl.pBindings=boundsBindings;VkDescriptorSetLayout boundsSet;vkcheck(vkCreateDescriptorSetLayout(dev,&dl,nullptr,&boundsSet),"bounds push layout");VkPushConstantRange boundsPush{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(BoundsPush)};ql.setLayoutCount=1;ql.pSetLayouts=&boundsSet;ql.pPushConstantRanges=&boundsPush;VkPipelineLayout boundsLayout;vkcheck(vkCreatePipelineLayout(dev,&ql,nullptr,&boundsLayout),"bounds layout");VkPipeline boundsPipeline=pipeline(argv[1],boundsLayout);
 VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;cp.queueFamilyIndex=family;VkCommandPool commandPool;vkcheck(vkCreateCommandPool(dev,&cp,nullptr,&commandPool),"command pool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=commandPool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer cmd;vkcheck(vkAllocateCommandBuffers(dev,&ca,&cmd),"command");
 bool first=true,lastScalp=false;unsigned cases=0,pubesFmaDifferences=0;
 auto run=[&](const char* label,float alpha,float phase,float vertexPhase,bool held,bool inactive,bool invalid,bool rebase,bool scalp=false,float viewportY=0,float viewportHeight=16,bool pubes=false,bool suppressRoot=false){
  const float cx=rebase?2.0f:0.0f,nearPlane=0.1f,farPlane=100.0f,A=nearPlane/(nearPlane-farPlane),B=-nearPlane*farPlane/(nearPlane-farPlane);
  Vertex vertices[9]{};for(unsigned n=0;n<9;++n){auto& v=vertices[n];v.position[3]=n>=6&&!inactive?1.0f:0.0f;v.uv[0]=0.5f;v.uv[1]=0.5f;v.n[2]=v.t[0]=v.b[1]=1;v.native[0]=v.native[1]=v.native[2]=std::numeric_limits<float>::quiet_NaN();v.phase[0]=vertexPhase;}
  const float positions[9]={-1,-1,3,1,-1,3,0,1,3};for(unsigned k=0;k<3;++k){auto& v=vertices[6+k];for(unsigned j=0;j<3;++j){v.position[j]=positions[k*3+j]+(j==0?cx:0);if(!inactive)v.native[j]=v.position[j];}}
  std::memcpy(source.map,vertices,sizeof(vertices));std::memcpy(static_cast<uint8_t*>(pool.map)+256,vertices,sizeof(vertices));
  if(scalp) {float compact[9][6]{};for(unsigned k=0;k<9;++k){for(unsigned j=0;j<4;++j)compact[k][j]=vertices[k].position[j];compact[k][4]=compact[k][5]=0.5f;}std::memcpy(source.map,compact,sizeof(compact));std::memcpy(static_cast<uint8_t*>(pool.map)+256,compact,sizeof(compact));}
  bg3::AlphaRec rec{};rec.tris=3;rec.kind=4;rec.tex=1;rec.cache=2;rec.channel=1;rec.cut=rec.cut_first=0.3333f;rec.scale[0]=phase;rec.scale[1]=phase<0?0.0f:1.0f;rec.p0[2]=1;rec.p1[0]=rec.p1[1]=20;rec.p1[2]=0.5f;rec.p1[3]=1;rec.t[1]=1;
  const float matrix[16]={1,0,0,0,0,1,0,0,0,0,A,1,-cx,0,B,0};const float viewport[4]={0,viewportY,16,viewportHeight},nf[2]={nearPlane,farPlane};auto* rb=reinterpret_cast<uint8_t*>(&rec);std::memcpy(rb+96,matrix,64);std::memcpy(rb+160,viewport,16);std::memcpy(rb+176,nf,8);const float cert=invalid?0.0f:1.0f;std::memcpy(rb+188,&cert,4);
  if(scalp) {rec.kind=3;rec.cache=0;rec.p0[0]=0;rec.p0[1]=vertexPhase;rec.p0[2]=phase!=-1?1.0f:0.0f;rec.scale[0]=phase;rec.scale[1]=phase!=-1?1.0f:0.0f;}
  if(pubes){const uint32_t subtype=1;std::memcpy(rb+196,&subtype,4);}
  if(suppressRoot)rec.t[0]=1;
  const uint32_t stride=scalp?24u:88u;
  const uint32_t header[4]={1,stride,1,0};std::memcpy(records.map,header,16);std::memcpy(static_cast<uint8_t*>(records.map)+16,&rec,sizeof(rec));
  const uint32_t meta[4]={held?256u:0u,stride|(1u<<10)|(scalp?0u:(1u<<12))|(held?(1u<<8):0u),16,3};std::memcpy(info.map,meta,16);
  std::memset(stamp.map,0,512);static_cast<uint32_t*>(stamp.map)[0]=static_cast<uint32_t*>(stamp.map)[1]=static_cast<uint32_t*>(stamp.map)[2]=1;std::memset(nativeCamera.map,0,256);if(rebase){float sourceOrigin=100,currentOrigin=102;std::memcpy(static_cast<uint8_t*>(stamp.map)+464,&sourceOrigin,4);std::memcpy(static_cast<uint8_t*>(nativeCamera.map)+208,&currentOrigin,4);}
  std::memset(result.map,0xcd,256);std::memset(stats.map,0,65536);
  vkcheck(vkResetCommandBuffer(cmd,0),"reset command");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vkBeginCommandBuffer(cmd,&begin),"begin");
  barrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ib.srcAccessMask=first?0:VK_ACCESS_SHADER_READ_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.oldLayout=first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.srcQueueFamilyIndex=ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ib.image=image;ib.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdPipelineBarrier(cmd,first?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&ib);
  VkClearColorValue clear{{0,0,suppressRoot?1.0f:0.0f,alpha}};vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&ib.subresourceRange);ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&ib);
  vkCmdFillBuffer(cmd,stamp.b,12,4,0);barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  VkDescriptorBufferInfo buffers[3]={{held?pool.b:source.b,0,4096},{pool.b,0,4096},{stamp.b,0,512}};VkWriteDescriptorSet writes[3];for(unsigned b=0;b<3;++b){writes[b]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[b].dstBinding=b;writes[b].descriptorCount=1;writes[b].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[b].pBufferInfo=buffers+b;}
  BoundsPush push{};push.tris=3;push.step=128;push.soup=held?256:0;std::memcpy(push.vp,matrix,64);push.depth[0]=0.2f;push.depth[1]=nearPlane;push.depth[2]=farPlane;push.stride=88;push.camera=1;push.bounded=!invalid;
  if(!scalp) {  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,boundsPipeline);pushDescriptors(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,boundsLayout,0,3,writes);vkCmdPushConstants(cmd,boundsLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);vkCmdDispatch(cmd,1,1,1);}
  barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_SHADER_READ_BIT);
  scalpGeometry.geometry.triangles.vertexData.deviceAddress=held?pool.address+256:source.address;
  bottom.pGeometries=scalp?&scalpGeometry:&geometry;
  const bool rebuild=first||scalp!=lastScalp;lastScalp=scalp;
  bottom.mode=top.mode=rebuild?VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR:VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;bottom.srcAccelerationStructure=rebuild?VK_NULL_HANDLE:blas;top.srcAccelerationStructure=rebuild?VK_NULL_HANDLE:tlas;VkAccelerationStructureBuildRangeInfoKHR br{3,0,0,0},tr{1,0,0,0};const auto* bp=&br;const auto* tp=&tr;buildAS(cmd,1,&bottom,&bp);
  barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);buildAS(cmd,1,&top,&tp);
  barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_SHADER_READ_BIT);
  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,query[rebase?1:0]);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,queryLayout,0,2,sets,0,nullptr);uint32_t pc[8]={0,1,1,0,0,0,0,0};vkCmdPushConstants(cmd,queryLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,pc);vkCmdDispatch(cmd,1,1,1);
  barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);vkcheck(vkEndCommandBuffer(cmd),"end");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;vkcheck(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"submit");vkcheck(vkQueueWaitIdle(queue),"wait");first=false;
  auto* w=static_cast<uint32_t*>(result.map);const bool active=!inactive&&!invalid;const bool opaque=alpha>=0.3333f && !suppressRoot;
  check(w[17]==1,"real TLAS stamp active");
  if(invalid){check(w[0]==2&&w[2]&&w[4]&&w[7]&&w[9]&&w[11]&&w[13]&&w[18]>0,"invalid bounds => all four UNKNOWN");}
  else if(inactive){check(w[0]==0&&!w[2]&&!w[4]&&!w[7]&&!w[9]&&w[18]==0&&as_float(w[1])<0&&as_float(w[6])<0,"proven inactive MISS, not UNKNOWN");}
  else if(scalp) {
   using namespace bg3::native_scalp;
   const float filtered=std::round(alpha*255.0f)/255.0f;
   const bool physical=evaluate(0x03a2d9f645625c44ull,{0,vertexPhase,true},{0,filtered,true}).survives;
   const bool primary=evaluate(0x9e77575a9f377ec5ull,{0,vertexPhase,true},{0,filtered,true},{phase,phase!=-1?1.0f:0.0f,8.5f,std::floor(viewportY+viewportHeight*0.5f)+0.5f,true,true}).survives;
   check(w[0]==unsigned(physical)&&!w[2]&&!w[4]&&!w[13],"Scalp native physical hard branch");
   check(physical?gpu_near(as_float(w[1]),3)&&gpu_near(as_float(w[3]),3):as_float(w[1])<0&&as_float(w[3])<0,"Scalp physical distance");
   check(!w[7]&&!w[9]&&!w[11]&&!w[15],"Scalp camera queries known");
   check(primary?gpu_near(as_float(w[6]),3)&&gpu_near(as_float(w[8]),3):as_float(w[6])<0&&as_float(w[8])<0,"Scalp native camera dither reference");
   check(as_float(w[10])<0,"Scalp has no fragment depth displacement");
   check(w[18]==0,"Scalp does not invalidate Hair bounds");
  }
  else{
   check(w[18]==0,"valid conservative bounds");check(w[0]==uint32_t(opaque),"physical visibility");check(!w[2]&&!w[4]&&!w[13],"physical queries known");
   check(opaque?gpu_near(as_float(w[1]),3)&&gpu_near(as_float(w[3]),3)&&gpu_near(as_float(w[12]),3):as_float(w[1])<0&&as_float(w[3])<0&&as_float(w[12])<0,"physical plane distances");
   const bool primarySolid=opaque&&(phase<0 || (vertexPhase==0?phase==0:phase!=0));
   check(!w[7]&&!w[9]&&!w[11],"primary queries known");
   check(primarySolid?gpu_near(as_float(w[6]),3.2f)&&gpu_near(as_float(w[8]),3.2f)&&gpu_near(as_float(w[10]),0.1f):as_float(w[6])<0&&as_float(w[8])<0&&as_float(w[10])<0,"native primary/dither/window distances");
   check(w[15]!=0,"off-axis primary request UNKNOWN");check(w[16]==0,"grazing ray-view coverage clear");
   if(opaque)check(w[5]==2&&gpu_near(as_float(w[22]),4),"actual original native vertex/index/normal");
   // Independent normalization can differ by FP32 roundoff. Reuse the derived
   // gamma128 envelope; this is not a native sampler/precision certificate.
   if(pubes){check(gpu_near(as_float(w[24]),as_float(w[25]))&&w[26]==1,"Pubes nontrivial-angle native reference within FP32 envelope");pubesFmaDifferences+=w[25]!=w[27];}
  }
  std::printf("case=%s held=%u inactive=%u invalid=%u rebase=%u vis=%u physical=%.9g primary=%.9g bounds_invalid=%u\n",label,unsigned(held),unsigned(inactive),unsigned(invalid),unsigned(rebase),w[0],as_float(w[1]),as_float(w[6]),w[18]);++cases;(void)active;
 };
 if(material){
 constexpr uint32_t control=16u+bg3::native_hit::nh_material_rows*256u;
 Buffer hitRows,hitBasis,deferredBasis,snapshotRows,channels,parameters;
 make_buffer(hitRows,control+16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
 make_buffer(hitBasis,4096,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);make_buffer(deferredBasis,4096,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
 make_buffer(snapshotRows,512,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);make_buffer(channels,16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);make_buffer(parameters,16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
 writeBuffer(sets[1],12,hitRows);writeBuffer(sets[1],13,hitBasis);
 std::vector<VkDescriptorImageInfo> materialCaches(64,{VK_NULL_HANDLE,arrayView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}),materialSamplers(16,{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});
 materialSamplers[1].sampler=nativeSampler;
 VkWriteDescriptorSet materialWrites[2]{};for(unsigned i=0;i<2;++i){auto& w=materialWrites[i];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=sets[1];w.dstBinding=14+i;w.descriptorType=i?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w.descriptorCount=i?16:64;w.pImageInfo=i?materialSamplers.data():materialCaches.data();}vkUpdateDescriptorSets(dev,2,materialWrites,0,nullptr);
 VkDescriptorSetLayout privateSets[2]{};VkPipelineLayout privateLayouts[2]{};VkPipeline privatePipelines[2]{};
 for(unsigned mode=0;mode<2;++mode){
  VkDescriptorSetLayoutBinding bindings[5]{};for(unsigned i=0;i<(mode?5u:4u);++i)bindings[i]={i,!mode&&i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;ci.bindingCount=mode?5:4;ci.pBindings=bindings;vkcheck(vkCreateDescriptorSetLayout(dev,&ci,nullptr,privateSets+mode),"material production layout");
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,mode?32u:16u};VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=privateSets+mode;li.pushConstantRangeCount=1;li.pPushConstantRanges=&range;vkcheck(vkCreatePipelineLayout(dev,&li,nullptr,privateLayouts+mode),"material production pipeline layout");privatePipelines[mode]=pipeline(argv[5+mode],privateLayouts[mode]);
 }
 const float samples[9][4]={{.25f,.5f,.5f,1},{.25f,.5f,1,.5f},{.25f,0,.5f,.5f},
  {.2f,.3f,.4f,1},{.5f,.6f,.7f,1},{.8f,.9f,1,1},{.1f,.2f,.3f,0},{.4f,.5f,.6f,0},{.7f,.8f,.9f,0}};
 auto materialCase=[&](const char* label,unsigned branch,unsigned reject=0,bool outside=false,bool border=false,bool rebase=false,bool both=false,bool varying=false){
  constexpr uint32_t stride=80,rawBytes=9*stride;
  std::memset(source.map,0,4096);std::memset(deferredBasis.map,0,4096);std::memset(hitBasis.map,0,4096);std::memset(snapshotRows.map,0,512);std::memset(hitRows.map,0,size_t(hitRows.size));std::memset(result.map,0xcd,256);
  const float pos[9]={-1,-1,3,1,-1,3,0,1,3},uv[6]={0,0,1,0,.5f,1};
  for(unsigned v=0;v<9;++v){float vertex[20]{};vertex[3]=1;if(v>=6){std::memcpy(vertex,pos+(v-6)*3,12);vertex[4]=uv[(v-6)*2]+(outside?2:0);vertex[5]=uv[(v-6)*2+1]+(outside?2:0);}
   vertex[6]=(branch==1||both)?1.0f:0.0f;vertex[7]=branch==0?1.0f:0.0f;vertex[9]=1;vertex[12]=vertex[14]=vertex[16]=1; // N.z, B.y, T.x
   std::memcpy(static_cast<uint8_t*>(source.map)+v*stride,vertex,stride);std::memcpy(static_cast<uint8_t*>(deferredBasis.map)+v*stride,vertex,stride);
  }
  if(varying)for(unsigned v=6;v<9;++v)for(auto* data:{static_cast<uint8_t*>(source.map),static_cast<uint8_t*>(deferredBasis.map)}){auto* vertex=reinterpret_cast<float*>(data+v*stride);vertex[6]=1;vertex[7]=v==8?1.0f:0.0f;}
  if(reject>=1 && reject<=3){unsigned component=reject==1?0:reject==2?4:6;reinterpret_cast<float*>(static_cast<uint8_t*>(deferredBasis.map)+6*stride)[component]+=.125f;}
  bg3::AlphaRec rec{};rec.tris=3;rec.kind=5;rec.tex=1;rec.cache=1;rec.scale[0]=rec.scale[1]=1;
  const uint32_t alphaHeader[4]={1,stride,1,0},meta[4]={0,stride|(1u<<10),16,3};std::memcpy(records.map,alphaHeader,16);std::memcpy(static_cast<uint8_t*>(records.map)+16,&rec,sizeof(rec));std::memcpy(info.map,meta,16);
  std::memset(stamp.map,0,512);auto* stampWords=static_cast<uint32_t*>(stamp.map);stampWords[0]=stampWords[1]=stampWords[2]=1;std::memset(nativeCamera.map,0,256);
  auto* words=static_cast<uint32_t*>(hitRows.map);words[0]=0;words[1]=1;words[2]=words[3]=1;words[control/4]=reject==4?0:1;
  auto* row=words+4;row[1]=3;row[2]=0;row[3]=rawBytes;row[4]=stride;row[6]=border?1:0;row[11]=2;for(unsigned i=0;i<9;++i)row[20+i]=i+2;
  static_cast<uint32_t*>(channels.map)[0]=5;float params[4]={1,2,3,.65f};if(reject==5)params[3]=std::numeric_limits<float>::quiet_NaN();std::memcpy(parameters.map,params,16);
  vkcheck(vkResetCommandBuffer(cmd,0),"material reset");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vkBeginCommandBuffer(cmd,&begin),"material begin");
  barrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ib.srcAccessMask=first?0:VK_ACCESS_SHADER_READ_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.oldLayout=first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.srcQueueFamilyIndex=ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ib.image=image;ib.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,10};vkCmdPipelineBarrier(cmd,first?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&ib);
  for(unsigned i=0;i<10;++i){VkClearColorValue clear{{1,1,1,1}};if(i)std::memcpy(clear.float32,samples[i-1],16);VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,i,1};vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);}
  ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&ib);
  scalpGeometry.geometry.triangles.vertexStride=stride;scalpGeometry.geometry.triangles.vertexData.deviceAddress=source.address;bottom.pGeometries=&scalpGeometry;bottom.mode=top.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;bottom.srcAccelerationStructure=top.srcAccelerationStructure=VK_NULL_HANDLE;
  VkAccelerationStructureBuildRangeInfoKHR br{3,0,0,0},tr{1,0,0,0};const auto* bp=&br;const auto* tp=&tr;buildAS(cmd,1,&bottom,&bp);barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);buildAS(cmd,1,&top,&tp);
  barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_SHADER_READ_BIT);
  VkDescriptorBufferInfo gatherInfo[4]={{channels.b,0,16},{parameters.b,0,16},{snapshotRows.b,0,512},{snapshotRows.b,0,512}};
  VkWriteDescriptorSet writes[5]{};for(unsigned i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=gatherInfo+i;}
  uint32_t gatherPush[4]={16,0,1,0};vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,privatePipelines[0]);pushDescriptors(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,privateLayouts[0],0,4,writes);vkCmdPushConstants(cmd,privateLayouts[0],VK_SHADER_STAGE_COMPUTE_BIT,0,16,gatherPush);vkCmdDispatch(cmd,1,1,1);
  barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  VkDescriptorBufferInfo joinInfo[5]={{source.b,0,4096},{deferredBasis.b,0,4096},{snapshotRows.b,0,512},{hitRows.b,0,hitRows.size},{hitBasis.b,0,4096}};
  for(unsigned i=0;i<5;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=joinInfo+i;}
  uint32_t joinPush[8]={16,16,0,0,rawBytes,control,0,reject==6?1u:0u};vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,privatePipelines[1]);pushDescriptors(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,privateLayouts[1],0,5,writes);vkCmdPushConstants(cmd,privateLayouts[1],VK_SHADER_STAGE_COMPUTE_BIT,0,32,joinPush);vkCmdDispatch(cmd,(rawBytes/4+63)/64,1,1);
  barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,query[rebase?1:0]);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,queryLayout,0,2,sets,0,nullptr);uint32_t pc[8]={0,0,1,0,0,0,0,0};vkCmdPushConstants(cmd,queryLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,pc);vkCmdDispatch(cmd,1,1,1);
  barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);vkcheck(vkEndCommandBuffer(cmd),"material end");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;vkcheck(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"material submit");vkcheck(vkQueueWaitIdle(queue),"material wait");first=false;
  auto* r=static_cast<uint32_t*>(result.map);const unsigned expected=reject>=1&&reject<=3?4:reject==4?1:reject>=5?3:0;check(r[0]==expected,"material rejection/status");check(gpu_near(as_float(r[1]),3),"material actual triangle hit");check(r[13]==unsigned(reject!=4&&reject!=5&&reject!=6),"production join valid publication");
  if(!expected){check(r[2]==5&&r[3]==2,"material native channel/family");for(unsigned i=0;i<3;++i)check(gpu_near(as_float(r[4+i]),border?1:samples[3+branch][i]),"selected native albedo");check(gpu_near(as_float(r[7]),border?1:samples[6+branch][1]),"selected native roughness");check(gpu_near(as_float(r[8]),border?1:samples[6+branch][0]),"selected native metalness");check(gpu_near(as_float(r[9]),.65f),"native reflectance");
   float normal[3]{};normal[branch==0?0:branch==1?2:1]=1;if(border){normal[0]=normal[2]=1/std::sqrt(3.0f);normal[1]=-normal[0];}for(unsigned i=0;i<3;++i)check(gpu_near(as_float(r[10+i]),normal[i]),"selected native frame normal");for(unsigned i=0;i<4;++i)check(gpu_near(as_float(r[14+i]),params[i]),"execution-time gathered scalar");}
  std::printf("material=%s status=%u family=%u channel=%u albedo=%.7g/%.7g/%.7g rough=%.7g metal=%.7g reflect=%.7g normal=%.7g/%.7g/%.7g joined=%u\n",label,r[0],r[3],r[2],as_float(r[4]),as_float(r[5]),as_float(r[6]),as_float(r[7]),as_float(r[8]),as_float(r[9]),as_float(r[10]),as_float(r[11]),as_float(r[12]),r[13]);++cases;
 };
 materialCase("green",0);materialCase("red",1);materialCase("base",2);materialCase("green_priority",0,0,false,false,false,true);
 materialCase("interpolated_green",0,0,false,false,false,false,true);
 materialCase("repeat_outside",0,0,true);materialCase("native_border",0,0,true,true);materialCase("rebase_zero_origin",1,0,false,false,true);
 materialCase("clip_mismatch",0,1);materialCase("uv_mismatch",0,2);materialCase("color_mismatch",0,3);materialCase("publication_zero",0,4);materialCase("invalid_scalar",0,5);materialCase("snapshot_identity",0,6);
 for(unsigned i=0;i<2;++i){vkDestroyPipeline(dev,privatePipelines[i],nullptr);vkDestroyPipelineLayout(dev,privateLayouts[i],nullptr);vkDestroyDescriptorSetLayout(dev,privateSets[i],nullptr);}

 }else if(!tileset){
 run("opaque",1,-1,0,false,false,false,false);run("transparent",0,-1,0,false,false,false,false);
 for(unsigned p=0;p<4;++p)run("dither",128.0f/255.0f,float(p),0,false,false,false,false);
 for(unsigned p=0;p<4;++p)run("native_vertex_phase",128.0f/255.0f,float(p),0.5f,false,false,false,false);
 run("held",1,-1,0,true,false,false,false);run("original_native_rebase",1,-1,0,false,false,false,true);
 run("inactive_native_nan",1,-1,0,false,true,false,false);run("invalid_bounds",1,-1,0,false,false,true,false);
 run("held_invalid_recertified",1,-1,0,true,false,true,false);run("recovered",1,-1,0,false,false,false,false);
 run("scalp_opaque",1,-1,0,false,false,false,false,true);run("scalp_clear",0,-1,0,false,false,false,false,true);
 for(unsigned p=0;p<4;++p)run("scalp_camera_dither",128.0f/255.0f,float(p),0,false,false,false,false,true);
 run("scalp_root_suppression",1,0,1,false,false,false,false,true);
 run("scalp_negative_phase",128.0f/255.0f,-3,0,false,false,false,false,true);
 run("scalp_negative_viewport",128.0f/255.0f,2,0,false,false,false,false,true,7,-16);
 run("scalp_held",128.0f/255.0f,0,0,true,false,false,false,true);run("scalp_rebase",128.0f/255.0f,0,0,false,false,false,true,true);
 run("pubes_opaque",1,-1,0,false,false,false,false,false,0,16,true);
 run("pubes_clear",0,-1,0,false,false,false,false,false,0,16,true);
 for(unsigned p=0;p<4;++p)run("pubes_dither",128.0f/255.0f,float(p),0,false,false,false,false,false,0,16,true);
 run("pubes_vertex_phase",128.0f/255.0f,0,0.5f,false,false,false,false,false,0,16,true);
 run("pubes_held",1,0,0,true,false,false,false,false,0,16,true);
 run("pubes_rebase",1,0,0,false,false,false,true,false,0,16,true);
 run("pubes_root",1,0,0,false,false,false,false,false,0,16,true,true);
 check(pubesFmaDifferences>0,"Pubes reference distinguishes native FMA from Hair grouping");
 std::printf("PUBES native_reference_fma_distinguishing_cases=%u\n",pubesFmaDifferences);
 }else{
 auto tile=[&](const char* label,uint32_t stride,float alpha,float green,float status,bool geometryBad=false,bool incomplete=false,bool held=false,bool rebase=false,bool border=false){
  std::memset(source.map,0,4096);std::memset(pool.map,0,4096);
  const float pos[9]={-1,-1,3,1,-1,3,0,1,3};
  for(unsigned v=0;v<9;++v){auto* dst=static_cast<uint8_t*>(source.map)+v*stride;
   float data[10]={0,0,0,1,border?2.5f:.5f,border?2.5f:.5f,green<0?1.0f:0.0f,(std::max)(green,0.0f),0,1};
   if(v>=6)std::memcpy(data,pos+(v-6)*3,12);
   std::memcpy(dst,data,40);
  }
  std::memcpy(static_cast<uint8_t*>(pool.map)+256,source.map,9*stride);
  bg3::AlphaRec rec{};rec.tris=3;rec.kind=5;rec.tex=1;rec.cache=1;rec.scale[0]=rec.scale[1]=1;rec.p0[1]=status;
  const uint32_t header[4]={1,stride,1,incomplete?1u:0u};std::memcpy(records.map,header,16);std::memcpy(static_cast<uint8_t*>(records.map)+16,&rec,sizeof(rec));
  const uint32_t meta[4]={geometryBad?4096u:held?256u:0u,stride|(1u<<10)|(held?(1u<<8):0u),16,3};std::memcpy(info.map,meta,16);
  std::memset(stamp.map,0,512);static_cast<uint32_t*>(stamp.map)[0]=static_cast<uint32_t*>(stamp.map)[1]=static_cast<uint32_t*>(stamp.map)[2]=1;
  std::memset(nativeCamera.map,0,256);std::memset(stats.map,0,65536);std::memset(result.map,0xcd,256);
  vkcheck(vkResetCommandBuffer(cmd,0),"tileset reset");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vkBeginCommandBuffer(cmd,&begin),"tileset begin");
  barrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ib.srcAccessMask=first?0:VK_ACCESS_SHADER_READ_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.oldLayout=first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.srcQueueFamilyIndex=ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ib.image=image;ib.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  vkCmdPipelineBarrier(cmd,first?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&ib);
  VkClearColorValue clear{{0,0,0,alpha}};vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&ib.subresourceRange);
  ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&ib);
  scalpGeometry.geometry.triangles.vertexStride=stride;scalpGeometry.geometry.triangles.vertexData.deviceAddress=source.address;
  bottom.pGeometries=&scalpGeometry;bottom.mode=top.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;bottom.srcAccelerationStructure=top.srcAccelerationStructure=VK_NULL_HANDLE;
  VkAccelerationStructureBuildRangeInfoKHR br{3,0,0,0},tr{1,0,0,0};const auto* bp=&br;const auto* tp=&tr;buildAS(cmd,1,&bottom,&bp);
  barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);buildAS(cmd,1,&top,&tp);
  barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_SHADER_READ_BIT);
  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,query[rebase?1:0]);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,queryLayout,0,2,sets,0,nullptr);uint32_t pc[8]={0,1,1,0,0,0,0,0};vkCmdPushConstants(cmd,queryLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,pc);vkCmdDispatch(cmd,1,1,1);
  barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);vkcheck(vkEndCommandBuffer(cmd),"tileset end");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;vkcheck(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"tileset submit");vkcheck(vkQueueWaitIdle(queue),"tileset wait");first=false;
  const bool unknown=status!=0 || geometryBad || incomplete,solid=green<=0 || border || alpha>=.5f;
  auto* w=static_cast<uint32_t*>(result.map);check(w[7]==1,"tileset real TLAS stamp");
  check(w[0]==(unknown?2u:solid?1u:0u),"tileset visibility truth");
  for(unsigned i=0;i<3;++i){check(bool(w[2+i*2])==unknown,"tileset closest/hit/debug UNKNOWN truth");float distance=as_float(w[1+i*2]);if(!unknown)check(solid?gpu_near(distance,3):distance<0,"tileset closest/hit/debug distance truth");}
  if(border)check(as_float(w[8])==alpha && as_float(w[9])==1,"native sampler19 differs from legacy repeat");
  std::printf("tileset=%s stride=%u alpha=%.9g green=%.9g held=%u rebase=%u visibility=%u aborted=%u/%u/%u distance=%.9g\n",label,stride,alpha,green,unsigned(held),unsigned(rebase),w[0],w[2],w[4],w[6],as_float(w[1]));++cases;
 };
 for(uint32_t stride:{40u,64u,88u}){
  tile("green_below",stride,std::nextafter(.5f,0.0f),1,0);
  tile("green_exact",stride,.5f,1,0);tile("green_above",stride,std::nextafter(.5f,1.0f),1,0);
  tile("red_literal",stride,0,-1,0);tile("base_literal",stride,0,0,0);
  tile("see_through",stride,1,1,3);tile("missing_recipe",stride,1,1,1);
  tile("invalid_geometry",stride,1,1,0,true);tile("header12_incomplete",stride,1,1,0,false,true);
  tile("native_sampler_border",stride,0,1,0,false,false,false,false,true);
 }
 tile("held_pool",88,.5f,1,0,false,false,true);tile("rebase",40,.5f,1,0,false,false,false,true);
 }
 check(validationErrors.load()==0,"zero Vulkan validation errors");std::printf("PASS cases=%u validation_errors=0 production_bounds=%u production_four_queries=%u native_tileset=%u production_material=%u production_scalar_gather_join=%u\n",cases,unsigned(!tileset),unsigned(!material),unsigned(tileset),unsigned(material),unsigned(material));
 vkDeviceWaitIdle(dev);vkDestroyCommandPool(dev,commandPool,nullptr);vkDestroyPipeline(dev,boundsPipeline,nullptr);vkDestroyPipeline(dev,query[0],nullptr);vkDestroyPipeline(dev,query[1],nullptr);vkDestroyPipelineLayout(dev,boundsLayout,nullptr);vkDestroyPipelineLayout(dev,queryLayout,nullptr);vkDestroyDescriptorPool(dev,descriptorPool,nullptr);vkDestroyDescriptorSetLayout(dev,boundsSet,nullptr);vkDestroyDescriptorSetLayout(dev,layouts[0],nullptr);vkDestroyDescriptorSetLayout(dev,layouts[1],nullptr);for(auto v:materialViews)vkDestroyImageView(dev,v,nullptr);if(nativeSampler)vkDestroySampler(dev,nativeSampler,nullptr);vkDestroySampler(dev,sampler,nullptr);vkDestroyImageView(dev,view,nullptr);vkDestroyImageView(dev,arrayView,nullptr);vkDestroyImage(dev,image,nullptr);vkFreeMemory(dev,imageMemory,nullptr);destroyAS(dev,tlas,nullptr);destroyAS(dev,blas,nullptr);
 }
 vkDestroyDevice(dev,nullptr);destroyDebug(instance,messenger,nullptr);vkDestroyInstance(instance,nullptr);check(validationErrors.load()==0,"zero destruction validation errors");return 0;
}
