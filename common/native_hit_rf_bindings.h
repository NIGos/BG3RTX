#pragma once
// rf_hit_shade (gi/rt_reflections_hit_shade.hlsl and its SHaRC twin): the typed descriptor
// contract of the D-RF tier (b) dispatch (core/gi.cpp rf_hit_dispatch; docs/PLAN_M4_REFLECTIONS_GI.md
// M4.2). No new push descriptors: set 0 is D-RF's own push set (already 32 on the RTX 5090), the
// material and the sun come in ordinary sets. tests/native_hit_rf_bindings_test.cpp checks the
// compiled SPIR-V against these tables (tools/compile_shaders.py runs it on every variant).
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include "hit_emission_bindings.h"
namespace bg3::native_hit_rf {
struct Binding {uint32_t binding;VkDescriptorType type;uint32_t count;};   // count 0: runtime array
// 0 D-RF push set; 1 the material AlphaDSL (Bg3HostApi::rt_hit_material_use, the host alpha
// set's layout); 2 the game's lighting set 1 (its own layout); 3 the D-LS sun set (g_sun_dsl,
// write_sun_set); 4 the SHaRC set (SHaRC variant only). maxBoundDescriptorSets: 32 on the RTX 5090
// (the SHaRC D-RF variant already binds 5).
inline constexpr uint32_t kSetPush=0,kSetMaterial=1,kSetGame=2,kSetSun=3,kSetSharc=4;
inline constexpr uint32_t set_count(bool sharc){return sharc?5u:4u;}
// rt_hit_emission (gi/rt_reflections*_hit_shade_em.hlsl): the host's D-EM emissive scene set
// (hit_emission_bindings.h) as set 5, after SHaRC's set 4 (an empty set 4 without SHaRC;
// core/gi.cpp ensure_rf_hit_em_layout).
inline constexpr uint32_t kSetEmissive=5;
// rf_hit_gi (gi/rt_reflections*_hit_shade*.hlsl?RF_HIT_GI=1): its device-local counters as set 6 b0 (core/gi.cpp
// ensure_rg; empty sets 4 / 5 where the variant has no SHaRC / emission; ensure_rf_hit_gi_layout).
inline constexpr uint32_t kSetRfGi=6;
inline constexpr Binding rf_gi_stats{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
// D-RF's push set: core/gi.cpp kRfBindings (0-30, binding_type()) + rtOrigin 40. With the
// S3 stable layout, unused 12 (gAO) leaves and 50 is always present (native_hair_origin.h binding_enabled).
inline constexpr std::array<Binding,33> push={{
 {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
 {2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{3,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {4,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{5,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {6,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{7,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {8,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{9,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {10,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{11,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {12,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{13,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {14,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{15,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {16,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{17,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {18,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{19,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{21,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
 {22,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{23,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {24,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{25,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {26,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{27,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},
 {28,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{29,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
 {30,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{40,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
 {50,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}
}};
// S3: the stable union swaps unused AO12 for Hair50 even with physical origins off.
inline constexpr bool push_enabled(const Binding& b,bool /*hair*/){return b.binding!=12u;}
// The lighting's set 1 (f469db78) as the variant reads it: b6 AO, b16/b32 samplers, b29 box
// (projector) lights (rt/bg3_boxes.hlsli; core/gi.cpp checks it is a bound storage buffer),
// b33 probes, b34/b36 sky irradiance (the diffuse ambient at the hit, verified in the dumped
// f469db78:265-267, 969-994), b35/b37 specular cubes, b38 DFG LUT, b45 bindless probe cubes and
// the box cookies (2D views of the same array, rt/bg3_boxes.hlsli _25).
inline constexpr std::array<Binding,11> game={{
 {6,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{16,VK_DESCRIPTOR_TYPE_SAMPLER,1},{29,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{32,VK_DESCRIPTOR_TYPE_SAMPLER,1},
 {33,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{34,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{35,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {36,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{37,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{38,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {45,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,0}
}};
// The D-LS sun set (core/gi.cpp ensure_ls_layout g_sun_dsl): 0 atmosphere UBO, 1-5 images, 6 sampler,
// 7 the D-LS per-light data lsSlots (RGBA32UI storage; the hit light policy, native_hit_direct.hlsli
// NATIVE_HIT_DLS_POLICY: D-RF's push set has no room; D-LS reads it at push b46 and ignores b7).
// 8 the sun trust image (core/sun_trust.h header and roof volumes; rt/native_hit_sun.hlsli
// NATIVE_HIT_SUN_ROOF: the hit's sun is unsupported under a roof the TLAS may lack).
inline constexpr std::array<Binding,9> sun={{
 {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {3,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{4,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{5,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},
 {6,VK_DESCRIPTOR_TYPE_SAMPLER,1},{7,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{8,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}
}};
template<size_t N> inline constexpr const Binding* find_in(const std::array<Binding,N>& t,uint32_t slot){
 for(const auto& b:t)if(b.binding==slot)return &b;return nullptr;
}
inline constexpr uint32_t push_count(bool hair){uint32_t n=0;for(const auto& b:push)if(push_enabled(b,hair))n+=b.count;return n;}
static_assert(push_count(false)==32 && push_count(true)==32,"D-RF push set stays at the 32 push descriptors");
}
