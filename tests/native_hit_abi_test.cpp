#include <windows.h>
#include "../common/host_api.h"
#include <type_traits>
#include <cstdio>
static_assert(BG3RTX_ABI_VERSION==28);
using MaterialUse=bool(*)(VkCommandBuffer,VkAccelerationStructureKHR,VkDescriptorSet,uint64_t,VkDescriptorSet*);
static_assert(std::is_same_v<decltype(Bg3HostApi::rt_hit_material_use),MaterialUse>);
static_assert(offsetof(Bg3HostApi,rt_hit_material_use)==offsetof(Bg3HostApi,rt_omm_scene_reference)+sizeof(Bg3HostApi::rt_omm_scene_reference));
static bool use(VkCommandBuffer cmd,VkAccelerationStructureKHR scene,VkDescriptorSet alpha,uint64_t generation,VkDescriptorSet* out){if(out)*out={};if(!(cmd && scene && alpha && generation==42 && out))return false;*out=reinterpret_cast<VkDescriptorSet>(uintptr_t(5));return true;}
int main(){
 Bg3HostApi api{};if(api.rt_hit_material_use)return 1;
 api.rt_hit_material_use=&use;
 if(api.rt_hit_material_use(VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,42,nullptr))return 2;
 VkDescriptorSet selected=reinterpret_cast<VkDescriptorSet>(uintptr_t(3)),query{};
 if(!api.rt_hit_material_use(reinterpret_cast<VkCommandBuffer>(uintptr_t(1)),reinterpret_cast<VkAccelerationStructureKHR>(uintptr_t(2)),selected,42,&query) || query==selected || !query)return 3;
 puts("PASS ABI21 appended typed material-use callback; no Vulkan calls");
}
