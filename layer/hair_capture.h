#pragma once
// rt_hair_capture (docs/RTX_HAIR_ROADMAP.md 2c): once per hair style drawn without a groom, the draw's index range, its
// soup corners (position at 0, UV0 at +16) and its mask's mip 0 (raw BC3 blocks) are copied by the GPU into one
// host-visible staging buffer in the BVH build's recording; when the GPU is done, a worker thread rebuilds the
// vertices, applies the full-mask rule (common/hair_capture.h) and writes <assetdb dir>\hair_capture\<index id>.hcap,
// which bin\bg3rtx_hairconv converts. Off (the default): no work at all. All calls but the worker's run under
// bvhsplit's lock.
#include <vulkan/vulkan.h>
#include <cstdint>

namespace bg3 { struct DeviceData; }
namespace bg3::hair_capture {

// Once per capture (bvh_begin_capture): reads rt_hair_capture.
void begin_capture();
bool enabled();
// Once per bvh_build, before any want(): hands GPU-done readbacks to the worker, drops lost ones, frees the staging
// the worker has read, logs the summary every 300 frames when anything happened.
void poll(DeviceData* dd, uint64_t now);
// A staging slot is free and nothing was staged this frame (the cheap gate before the mask is looked up).
bool ready();

struct Draw {
  uint32_t count = 0;                  // indices (= soup corners)
  VkBuffer ib = VK_NULL_HANDLE;
  VkDeviceSize ib_offset = 0;          // byte offset of the draw's first index
  uint32_t index_bytes = 0;            // 2 or 4
  VkBuffer soup = VK_NULL_HANDLE;
  VkDeviceSize soup_off = 0, soup_size = 0;
  uint32_t stride = 0;
  VkImageView mask = VK_NULL_HANDLE;   // the CHAR_Hair mask as bound (alpha record kind 4, tex)
  VkImageLayout mask_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  uint32_t mask_id = 0;                // tracker image id when snapshotted
  uint64_t material = 0;
};
// No mask view for a hair draw (counted once per index count).
void no_mask(uint32_t count);
// Decides and stages the capture of this draw (true: record() records its copies in this build).
bool want(DeviceData* dd, const Draw& d, uint64_t now);
// The staged capture's copies (no-op when nothing was staged). `rec`: the build record (bvh_rec_register).
void record(DeviceData* dd, VkCommandBuffer cmd, uint64_t now, uint64_t rec);
// Joins the worker and frees every staging buffer (the device is idle).
void device_destroyed(DeviceData* dd);

}  // namespace bg3::hair_capture
