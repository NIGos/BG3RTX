#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <span>
#include <vector>
namespace bg3::native_hit {
// Exact current-recording marker. False on a fresh default-off device.
bool recording_needs_submit_promotion(VkCommandBuffer);
// Capture/pin owner IDs BEFORE the real submit, since a legal completed/reset
// command may have a different recording by the time that call returns.
struct PreparedSubmission {std::vector<uint64_t> owners;};
PreparedSubmission before_submit(std::span<const VkCommandBuffer>);
// Only actual successful submits stamp completion-fence frame ownership.
// Failure unpins and leaves the original recording pending/retryable.
void after_submit(const PreparedSubmission&,VkQueue,bool success);
}
