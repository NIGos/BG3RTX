// Native Vulkan NR after RR. Host-private API: no Bg3HostApi ABI change.
#pragma once
#include "host.h"
#include "../common/dlss5_nr_frame.h"
#include <nvsdk_ngx_vk.h>
namespace bg3 {
struct Dlss5Ngx {
  NVSDK_NGX_Result (*create)(VkDevice,VkCommandBuffer,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
  NVSDK_NGX_Result (*evaluate)(VkCommandBuffer,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
  NVSDK_NGX_Result (*release)(NVSDK_NGX_Handle*);
  NVSDK_NGX_Result (*allocate)(NVSDK_NGX_Parameter**);
  NVSDK_NGX_Result (*destroy)(NVSDK_NGX_Parameter*);
  NVSDK_NGX_Result (*capabilities)(NVSDK_NGX_Parameter**);
};
struct Dlss5Frame {
  RrPostImage color{};
  Bg3Binding depth{}, motion{};
  dlss5_frame::Inputs guides{};
  bool rr_success{}, use_post{}, debug{}, depth_inverted{};
};
void dlss5_set_ngx(const Dlss5Ngx&);
void dlss5_evaluate(DeviceData*,VkCommandBuffer,const Dlss5Frame&);
void dlss5_on_present();
void dlss5_note_submit(VkQueue,const std::vector<VkCommandBuffer>&,bool);
void dlss5_device_destroy(DeviceData*);
VkResult dlss5_create_device(InstanceData*,PFN_vkCreateDevice,VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
} // namespace bg3
