#pragma once
#include <vulkan/vulkan.h>
#include <array>
namespace bg3::native_hit_gi {
struct Binding {uint32_t binding;VkDescriptorType type;uint32_t count;};
// Exact union of production GI/native-direct inputs, including rebase/SHaRC
// camera48 and physical Hair50. Never apply the D-LS contraction to GI.
inline constexpr std::array<Binding,20> bindings={{
 {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
 {2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{5,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {8,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{11,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {13,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{17,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {18,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {21,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{22,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {23,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{24,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {25,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{26,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
 {29,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{40,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {48,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{50,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}
}};
inline constexpr uint32_t push_bytes=256;
inline constexpr bool enabled(const Binding& b,bool hair){return b.binding!=50u || hair;}
inline constexpr const Binding* find(uint32_t slot,bool hair){for(const auto& b:bindings)if(b.binding==slot && enabled(b,hair))return &b;return nullptr;}
inline constexpr uint32_t descriptor_count(bool hair){uint32_t count=0;for(const auto& b:bindings)if(enabled(b,hair))count+=b.count;return count;}
static_assert(descriptor_count(true)<=32);
}
