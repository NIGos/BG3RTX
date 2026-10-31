#include <windows.h>
#include "../common/host_api.h"
#include <type_traits>
#include <cstdio>
static_assert(BG3RTX_ABI_VERSION==20);
using MaterialUse=bool(*)(VkCommandBuffer,VkAccelerationStructureKHR,VkDescriptorSet,uint64_t);
static_assert(std::is_same_v<decltype(Bg3HostApi::rt_hit_material_use),MaterialUse>);
static_assert(offsetof(Bg3HostApi,rt_hit_material_use)==offsetof(Bg3HostApi,rt_omm_scene_reference)+sizeof(Bg3HostApi::rt_omm_scene_reference));
static bool use(VkCommandBuffer cmd,VkAccelerationStructureKHR scene,VkDescriptorSet alpha,uint64_t generation){return cmd && scene && alpha && generation==42;}
int main(){
 Bg3HostApi api{};if(api.rt_hit_material_use)return 1;
 api.rt_hit_material_use=&use;
 if(api.rt_hit_material_use(VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,42))return 2;
 puts("PASS ABI20 appended typed material-use callback; no Vulkan calls");
}
