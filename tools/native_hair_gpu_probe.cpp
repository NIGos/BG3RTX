// Windowless synthetic Vulkan fixture. Same private-loader/validation pattern as
// omm_ext_smoke; no game, layer hooks, user files, presentation or frame generation.
// Runs the real hair_bounds shader and all four real rt_common query functions.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "../layer/host.h"
#include "../common/native_scalp_coverage.h"
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
 check(argc==4,"usage: native_hair_gpu_probe bounds.spv query.spv query_rebase.spv");
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
 float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
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
 VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};imageInfo.imageType=VK_IMAGE_TYPE_2D;imageInfo.format=VK_FORMAT_R8G8B8A8_UNORM;imageInfo.extent={1,1,1};imageInfo.mipLevels=imageInfo.arrayLayers=1;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;imageInfo.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;VkImage image;vkcheck(vkCreateImage(dev,&imageInfo,nullptr,&image),"image");
 VkMemoryRequirements imageReq;vkGetImageMemoryRequirements(dev,image,&imageReq);VkMemoryAllocateInfo imageAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};imageAlloc.allocationSize=imageReq.size;imageAlloc.memoryTypeIndex=memory_type(imageReq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);VkDeviceMemory imageMemory;vkcheck(vkAllocateMemory(dev,&imageAlloc,nullptr,&imageMemory),"image memory");vkcheck(vkBindImageMemory(dev,image,imageMemory,0),"image bind");
 VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=imageInfo.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkImageView view,arrayView;vkcheck(vkCreateImageView(dev,&vi,nullptr,&view),"2D view");vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;vkcheck(vkCreateImageView(dev,&vi,nullptr,&arrayView),"array view");
 VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;si.maxLod=16;VkSampler sampler;vkcheck(vkCreateSampler(dev,&si,nullptr,&sampler),"sampler");
 VkDescriptorSetLayoutBinding set0Bindings[]={{18,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{21,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{48,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
 VkDescriptorSetLayoutBinding set1Bindings[10];for(uint32_t b=0;b<10;++b)set1Bindings[b]={b,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[2]={2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4096,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[3]={3,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,16,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[4]={4,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};set1Bindings[5]={5,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
 VkDescriptorSetLayout layouts[2];VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=4;dl.pBindings=set0Bindings;vkcheck(vkCreateDescriptorSetLayout(dev,&dl,nullptr,layouts),"set0 layout");dl.bindingCount=10;dl.pBindings=set1Bindings;vkcheck(vkCreateDescriptorSetLayout(dev,&dl,nullptr,layouts+1),"set1 layout");
 VkDescriptorPoolSize poolSizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4112},{VK_DESCRIPTOR_TYPE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=2;dp.poolSizeCount=5;dp.pPoolSizes=poolSizes;VkDescriptorPool descriptorPool;vkcheck(vkCreateDescriptorPool(dev,&dp,nullptr,&descriptorPool),"descriptor pool");VkDescriptorSet sets[2];VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=descriptorPool;da.descriptorSetCount=2;da.pSetLayouts=layouts;vkcheck(vkAllocateDescriptorSets(dev,&da,sets),"sets");
 auto writeBuffer=[&](VkDescriptorSet set,uint32_t b,const Buffer& buffer,VkDescriptorType type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){VkDescriptorBufferInfo bi{buffer.b,0,buffer.size};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=b;w.descriptorCount=1;w.descriptorType=type;w.pBufferInfo=&bi;vkUpdateDescriptorSets(dev,1,&w,0,nullptr);};
 writeBuffer(sets[0],20,stats);writeBuffer(sets[0],21,result);writeBuffer(sets[0],48,nativeCamera,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);writeBuffer(sets[1],0,records);writeBuffer(sets[1],1,source);writeBuffer(sets[1],6,source);writeBuffer(sets[1],7,info);writeBuffer(sets[1],8,pool);writeBuffer(sets[1],9,stamp);
 VkWriteDescriptorSetAccelerationStructureKHR aw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};aw.accelerationStructureCount=1;aw.pAccelerationStructures=&tlas;VkWriteDescriptorSet writeAS{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,&aw};writeAS.dstSet=sets[0];writeAS.dstBinding=18;writeAS.descriptorCount=1;writeAS.descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;vkUpdateDescriptorSets(dev,1,&writeAS,0,nullptr);
 std::vector<VkDescriptorImageInfo> textures(4096,{VK_NULL_HANDLE,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}),caches(16,{VK_NULL_HANDLE,arrayView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});VkDescriptorImageInfo samp{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
 for(uint32_t b=2;b<=5;++b){VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=sets[1];w.dstBinding=b;w.descriptorType=b<4?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER;w.descriptorCount=b==2?4096:b==3?16:1;w.pImageInfo=b==2?textures.data():b==3?caches.data():&samp;vkUpdateDescriptorSets(dev,1,&w,0,nullptr);}
 VkPushConstantRange queryPush{VK_SHADER_STAGE_COMPUTE_BIT,0,32};VkPipelineLayoutCreateInfo ql{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ql.setLayoutCount=2;ql.pSetLayouts=layouts;ql.pushConstantRangeCount=1;ql.pPushConstantRanges=&queryPush;VkPipelineLayout queryLayout;vkcheck(vkCreatePipelineLayout(dev,&ql,nullptr,&queryLayout),"query layout");VkPipeline query[2]={pipeline(argv[2],queryLayout),pipeline(argv[3],queryLayout)};
 VkDescriptorSetLayoutBinding boundsBindings[3];for(uint32_t b=0;b<3;++b)boundsBindings[b]={b,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};dl.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;dl.bindingCount=3;dl.pBindings=boundsBindings;VkDescriptorSetLayout boundsSet;vkcheck(vkCreateDescriptorSetLayout(dev,&dl,nullptr,&boundsSet),"bounds push layout");VkPushConstantRange boundsPush{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(BoundsPush)};ql.setLayoutCount=1;ql.pSetLayouts=&boundsSet;ql.pPushConstantRanges=&boundsPush;VkPipelineLayout boundsLayout;vkcheck(vkCreatePipelineLayout(dev,&ql,nullptr,&boundsLayout),"bounds layout");VkPipeline boundsPipeline=pipeline(argv[1],boundsLayout);
 VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;cp.queueFamilyIndex=family;VkCommandPool commandPool;vkcheck(vkCreateCommandPool(dev,&cp,nullptr,&commandPool),"command pool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=commandPool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer cmd;vkcheck(vkAllocateCommandBuffers(dev,&ca,&cmd),"command");
 bool first=true,lastScalp=false;unsigned cases=0;
 auto run=[&](const char* label,float alpha,float phase,float vertexPhase,bool held,bool inactive,bool invalid,bool rebase,bool scalp=false,float viewportY=0,float viewportHeight=16){
  const float cx=rebase?2.0f:0.0f,nearPlane=0.1f,farPlane=100.0f,A=nearPlane/(nearPlane-farPlane),B=-nearPlane*farPlane/(nearPlane-farPlane);
  Vertex vertices[9]{};for(unsigned n=0;n<9;++n){auto& v=vertices[n];v.position[3]=n>=6&&!inactive?1.0f:0.0f;v.uv[0]=0.5f;v.uv[1]=0.5f;v.n[2]=v.t[0]=v.b[1]=1;v.native[0]=v.native[1]=v.native[2]=std::numeric_limits<float>::quiet_NaN();v.phase[0]=vertexPhase;}
  const float positions[9]={-1,-1,3,1,-1,3,0,1,3};for(unsigned k=0;k<3;++k){auto& v=vertices[6+k];for(unsigned j=0;j<3;++j){v.position[j]=positions[k*3+j]+(j==0?cx:0);if(!inactive)v.native[j]=v.position[j];}}
  std::memcpy(source.map,vertices,sizeof(vertices));std::memcpy(static_cast<uint8_t*>(pool.map)+256,vertices,sizeof(vertices));
  if(scalp) {float compact[9][6]{};for(unsigned k=0;k<9;++k){for(unsigned j=0;j<4;++j)compact[k][j]=vertices[k].position[j];compact[k][4]=compact[k][5]=0.5f;}std::memcpy(source.map,compact,sizeof(compact));std::memcpy(static_cast<uint8_t*>(pool.map)+256,compact,sizeof(compact));}
  bg3::AlphaRec rec{};rec.tris=3;rec.kind=4;rec.tex=1;rec.cache=2;rec.channel=1;rec.cut=rec.cut_first=0.3333f;rec.scale[0]=phase;rec.scale[1]=phase<0?0.0f:1.0f;rec.p0[2]=1;rec.p1[0]=rec.p1[1]=20;rec.p1[2]=0.5f;rec.p1[3]=1;rec.t[1]=1;
  const float matrix[16]={1,0,0,0,0,1,0,0,0,0,A,1,-cx,0,B,0};const float viewport[4]={0,viewportY,16,viewportHeight},nf[2]={nearPlane,farPlane};auto* rb=reinterpret_cast<uint8_t*>(&rec);std::memcpy(rb+96,matrix,64);std::memcpy(rb+160,viewport,16);std::memcpy(rb+176,nf,8);const float cert=invalid?0.0f:1.0f;std::memcpy(rb+188,&cert,4);
  if(scalp) {rec.kind=3;rec.cache=0;rec.p0[0]=0;rec.p0[1]=vertexPhase;rec.p0[2]=phase!=-1?1.0f:0.0f;rec.scale[0]=phase;rec.scale[1]=phase!=-1?1.0f:0.0f;}
  const uint32_t stride=scalp?24u:88u;
  const uint32_t header[4]={1,stride,1,0};std::memcpy(records.map,header,16);std::memcpy(static_cast<uint8_t*>(records.map)+16,&rec,sizeof(rec));
  const uint32_t meta[4]={held?256u:0u,stride|(1u<<10)|(scalp?0u:(1u<<12))|(held?(1u<<8):0u),16,3};std::memcpy(info.map,meta,16);
  std::memset(stamp.map,0,512);static_cast<uint32_t*>(stamp.map)[0]=static_cast<uint32_t*>(stamp.map)[1]=static_cast<uint32_t*>(stamp.map)[2]=1;std::memset(nativeCamera.map,0,256);if(rebase){float sourceOrigin=100,currentOrigin=102;std::memcpy(static_cast<uint8_t*>(stamp.map)+464,&sourceOrigin,4);std::memcpy(static_cast<uint8_t*>(nativeCamera.map)+208,&currentOrigin,4);}
  std::memset(result.map,0xcd,256);std::memset(stats.map,0,65536);
  vkcheck(vkResetCommandBuffer(cmd,0),"reset command");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vkBeginCommandBuffer(cmd,&begin),"begin");
  barrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ib.srcAccessMask=first?0:VK_ACCESS_SHADER_READ_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.oldLayout=first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.srcQueueFamilyIndex=ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ib.image=image;ib.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdPipelineBarrier(cmd,first?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&ib);
  VkClearColorValue clear{{0,0,0,alpha}};vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&ib.subresourceRange);ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&ib);
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
  auto* w=static_cast<uint32_t*>(result.map);const bool active=!inactive&&!invalid;const bool opaque=alpha>=0.3333f;
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
  }
  std::printf("case=%s held=%u inactive=%u invalid=%u rebase=%u vis=%u physical=%.9g primary=%.9g bounds_invalid=%u\n",label,unsigned(held),unsigned(inactive),unsigned(invalid),unsigned(rebase),w[0],as_float(w[1]),as_float(w[6]),w[18]);++cases;(void)active;
 };
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
 check(validationErrors.load()==0,"zero Vulkan validation errors");std::printf("PASS cases=%u validation_errors=0 production_bounds=1 production_four_queries=1\n",cases);
 vkDeviceWaitIdle(dev);vkDestroyCommandPool(dev,commandPool,nullptr);vkDestroyPipeline(dev,boundsPipeline,nullptr);vkDestroyPipeline(dev,query[0],nullptr);vkDestroyPipeline(dev,query[1],nullptr);vkDestroyPipelineLayout(dev,boundsLayout,nullptr);vkDestroyPipelineLayout(dev,queryLayout,nullptr);vkDestroyDescriptorPool(dev,descriptorPool,nullptr);vkDestroyDescriptorSetLayout(dev,boundsSet,nullptr);vkDestroyDescriptorSetLayout(dev,layouts[0],nullptr);vkDestroyDescriptorSetLayout(dev,layouts[1],nullptr);vkDestroySampler(dev,sampler,nullptr);vkDestroyImageView(dev,view,nullptr);vkDestroyImageView(dev,arrayView,nullptr);vkDestroyImage(dev,image,nullptr);vkFreeMemory(dev,imageMemory,nullptr);destroyAS(dev,tlas,nullptr);destroyAS(dev,blas,nullptr);
 }
 vkDestroyDevice(dev,nullptr);destroyDebug(instance,messenger,nullptr);vkDestroyInstance(instance,nullptr);check(validationErrors.load()==0,"zero destruction validation errors");return 0;
}
