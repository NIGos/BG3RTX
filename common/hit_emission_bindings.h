#pragma once
// rt_hit_emission (docs/PLAN_M4_REFLECTIONS_GI.md M4.4): the host's D-EM emissive scene set as the hit
// consumers bind it (Bg3HostApi::rt_emissive; its layout is shaders/rt/em_common.hlsli, written by
// layer/emissive.cpp). GI hit variants: set 2 (native_hit_gi_bindings.h kSetEmissive); rf_hit_shade:
// set 5 (native_hit_rf_bindings.h kSetEmissive). tests/native_hit_gi_bindings_test.cpp and
// tests/native_hit_rf_bindings_test.cpp (--em) check every compiled *_em module against this table.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
namespace bg3::hit_emission {
struct Binding {uint32_t binding;VkDescriptorType type;uint32_t count;};
// 0 emissive TLAS, 1 InstInfo, 2 pool (unused), 3 soup, 4 records, 5 light list, 6 textures (256),
// 7 sampler, 8 debug statistics, 9 header, 10 the luminance scales, 11 VT tile caches (16).
inline constexpr std::array<Binding,12> emissive={{
 {0,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {6,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,256},{7,VK_DESCRIPTOR_TYPE_SAMPLER,1},
 {8,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{9,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {10,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{11,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,16}
}};
inline constexpr const Binding* find(uint32_t slot){for(const auto& b:emissive)if(b.binding==slot)return &b;return nullptr;}
// What a hit lookup must read (em_lookup + em_radiance): the TLAS, InstInfo, the soup, the records and the header.
inline constexpr std::array<uint32_t,5> required={0u,1u,3u,4u,9u};
}
