#pragma once
#include "../common/omm_backend_plan.h"
#include <vulkan/vulkan.h>
namespace bg3 { struct DeviceData; struct InstanceData; }
namespace bg3::omm {
// Chained by rtscene only after the ordinary AS/RQ prerequisites pass.
const void* device_features(InstanceData*, VkPhysicalDevice, const void* next,
                            std::vector<const char*>& extensions);
void device_created(DeviceData*, bool rtEnabled);
void device_destroyed(DeviceData*);
bool available();
using Ticket = uint64_t;
struct Input {
  std::span<const Triangle> descriptors;
  VkDeviceAddress data{}, triangles{};
  VkDeviceSize dataBytes{}, triangleBytes{};
  VkBufferUsageFlags dataUsage{}, triangleUsage{};
};
// Inputs must be exact uploads of descriptors, immutable through build completion,
// with four-state alpha semantics already certified by the provider. Backend
// validation checks layout/ranges, not the truth of known alpha states.
// Single graphics queue only; caller serials must name actual queue submissions.
Ticket record(VkCommandBuffer, const Input&);
// One-to-one triangle order, indexType NONE; attachment storage returned by value
// points to ticket-owned histogram. Never overwrite caller's unrelated pNext.
bool attachment(Ticket, uint32_t primitiveCount, VkAccelerationStructureTrianglesOpacityMicromapEXT&);
bool mark_submitted(Ticket, uint64_t actualSubmissionSerial);
bool retire(Ticket);
void collect(uint64_t actualCompletedSerial);
bool discard_unsubmitted(Ticket); // Only after the recorded command buffer is discarded/reset.
}
