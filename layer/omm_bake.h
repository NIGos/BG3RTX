#pragma once
#include "omm_backend.h"
#include "../common/omm_plain_bake.h"
#include <memory>
namespace bg3::omm {
struct PlainVertices {
  VkBuffer buffer{};VkDeviceAddress address{};VkDeviceSize bytes{};
  std::shared_ptr<void> owner;
};
// Allocation ONLY: no copy, barrier or other GPU command is recorded. Uses the
// same actual-allocation 128 MiB producer budget. Pass owner to the native factory.
bool allocate_plain_vertices(VkCommandBuffer,VkDeviceSize,PlainVertices*);
struct PlainInput {
  VkBuffer vertices{};
  VkDeviceSize vertex_bytes{};
  VkImageView mask{};
  VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  PlainBakePush push{};
  // Keeps the immutable sampled snapshot/source vertices alive. Their contents
  // and the push's full mip/UV/numeric domain must be certified by the caller.
  std::shared_ptr<void> sampled_owner;
  // Optional same-command post-VS snapshot. Destination vertices must be an
  // owned, exact source_bytes range at offset0; sampled_owner retains it. The
  // source needs TRANSFER_SRC and lifetime through this copy, not ticket lifetime.
  VkBuffer copy_source{};
  VkDeviceSize copy_source_bytes{},copy_offset{};
};
struct PlainOutput {
  Ticket ticket{};
  VkBuffer data{}, audit{};
  VkDeviceSize data_bytes{}, audit_bytes{};
  VkDeviceSize data_offset{}; // immutable content-stamp header precedes packed states
  uint64_t image_id{}, view_id{};
  uint32_t source_epoch_offset{};
};
// Producer -> EXT build. Does not bypass required material/content proofs.
// Changes compute bindings; the enclosing injection must restore_bindings()
// before returning to the game, as for existing BVH extraction work.
// A zero sampling_certificate produces UNKNOWN only; that is backend testing,
// never a successful quality/acceleration gate.
// Optional diagnostic buffers live exactly as long as the returned ticket.
Ticket record_plain(VkCommandBuffer,const PlainInput&,PlainOutput* = nullptr);
// Diagnostics ([omm] line): producer input bytes; cumulative baked microtriangle states
// (total, known opaque, known transparent, unknown by value) as last copied back.
uint64_t bake_input_bytes();
bool bake_state_totals(uint64_t out[4]);
void bake_device_destroyed(DeviceData*);
}
