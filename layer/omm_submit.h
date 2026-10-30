#pragma once
#include <vulkan/vulkan.h>
#include <span>
#include <cstdint>
#include <vector>
namespace bg3 {struct DeviceData;}
namespace bg3::omm {
bool submit_device_created(DeviceData*);
void submit_device_destroyed(DeviceData*);
bool tracked_recording(VkCommandBuffer);
bool command_queue_family(VkCommandBuffer,uint32_t& family);
bool mark_content_dispatch(VkCommandBuffer);
bool register_ticket_use(VkCommandBuffer,uint64_t ticket);
bool register_record_ticket(VkCommandBuffer,uint64_t ticket); // Backend pre-record path, no callback.
bool recorded_reference(uint64_t ticket);
void pool_created(VkCommandPool,uint32_t family);
void pool_discard(VkCommandPool,bool destroyed);
void command_allocated(VkCommandPool,VkCommandBuffer);
void command_discard(VkCommandBuffer,bool freed=false);
void command_begin(VkCommandBuffer,bool hostBarrierAllowed=true);
// Explicit marker for a feedback dispatch without a ticket; normal RT queries
// already carry ticket references. Propagates through recorded secondary edges.
bool mark_feedback_use(VkCommandBuffer);
// Once before native EndCommandBuffer: compute writes -> host reads. Caller
// still waits the actual retired slot fence before reading/resetting byte144.
void feedback_before_end(VkCommandBuffer,bool outsideRenderPass);
void command_end(VkCommandBuffer,bool success);
void command_execute(VkCommandBuffer,std::span<const VkCommandBuffer>);
struct PreparedSubmit {std::vector<uint64_t> tickets;bool valid=true;};
// Capture/pin before the driver call: a completed app fence can permit CB reset
// on another thread before that call returns. Both functions require queue lock.
PreparedSubmit before_submit(DeviceData*,std::span<const VkCommandBuffer>);
// Success signals a private timeline via the real queue function. Failure only
// unpins; it never claims GPU completion or emits a signal.
void after_submit(DeviceData*,VkQueue,const PreparedSubmit&,bool success);
}
