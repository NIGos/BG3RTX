#pragma once
#include "../common/omm_backend_plan.h"
#include <vulkan/vulkan.h>
#include <memory>
namespace bg3 { struct DeviceData; struct InstanceData; }
namespace bg3::omm {
// Chained by rtscene only after the ordinary AS/RQ prerequisites pass.
const void* device_features(InstanceData*, VkPhysicalDevice, const void* next,
                            std::vector<const char*>& extensions);
void device_created(DeviceData*, bool rtEnabled);
bool device_requested();
void disable_device_request(); // Optional-device retry; ordinary RT is retained.
void device_destroyed(DeviceData*);
bool available();
bool available_for(DeviceData*);
bool has_resources();
void retain_until_idle(); // Lost completion/provenance: collection AND discard stop.
void reserve_submissions(std::span<const uint64_t>);
void release_submissions(std::span<const uint64_t>);
using Ticket = uint64_t;
struct Input {
  std::span<const Triangle> descriptors;
  VkDeviceAddress data{}, triangles{};
  VkDeviceSize dataBytes{}, triangleBytes{};
  VkBufferUsageFlags dataUsage{}, triangleUsage{};
  // Optional owner of baked data, descriptors and their source/pipeline state.
  // Retained until ticket destruction AFTER micromap/storage destruction.
  std::shared_ptr<void> input_owner;
};
// Inputs must be exact uploads of descriptors, immutable through build completion,
// with four-state alpha semantics already certified by the provider. Backend
// validation checks layout/ranges, not the truth of known alpha states.
// Single graphics queue only; caller serials must name actual queue submissions.
Ticket record(VkCommandBuffer, const Input&);
// One-to-one triangle order, indexType NONE; attachment storage returned by value
// points to ticket-owned histogram. Never overwrite caller's unrelated pNext.
bool attachment(Ticket, uint32_t primitiveCount, VkAccelerationStructureTrianglesOpacityMicromapEXT&);
struct AttachedBlas {
  VkAccelerationStructureKHR handle{};
  VkDeviceAddress address{};
  VkDeviceSize storage_bytes{},scratch_bytes{};
};
// One immutable, nonindexed material BLAS per ticket, owned alongside its
// micromap. Retirement destroys BLAS first, then micromap, then producer inputs.
// Every TLAS build/traversal must still register_ticket_use(cmd,ticket).
bool record_blas(VkCommandBuffer,Ticket,VkDeviceAddress vertices,VkDeviceSize vertexBytes,
                 uint32_t stride,uint32_t triangles,AttachedBlas*);
bool mark_submitted(Ticket, uint64_t actualSubmissionSerial);
bool retire(Ticket);
void collect(uint64_t actualCompletedSerial);
bool discard_unsubmitted(Ticket); // Only after the recorded command buffer is discarded/reset.
}
