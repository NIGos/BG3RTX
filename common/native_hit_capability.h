#pragma once
#include <atomic>
#include <cstdint>
namespace bg3::native_hit {
inline constexpr const char* capability_key="native-hit-material-v1";
inline void capability(uint32_t* word,bool value){if(word)std::atomic_ref<uint32_t>(*word).store(value?1u:0u,std::memory_order_release);}
inline bool capable(uint32_t* word){return word && std::atomic_ref<uint32_t>(*word).load(std::memory_order_acquire)==1;}
// rt_hit_material_instanced at startup (layer/hit_material.cpp): the core then requests the hit-material
// consumers with RT_HIT_INSTANCED=1 (common/shader_variant.h kHitInstanced); the live key only gates the rows.
inline constexpr const char* instanced_capability_key="native-hit-instanced-v1";
// rt_hit_material_wind_rgb at startup: the instanced consumers also get RT_HIT_WIND_RGB=1 (kHitWindRgb).
inline constexpr const char* wind_rgb_capability_key="native-hit-wind-rgb-v1";
// rt_hit_material_ip_grass at startup: the instanced consumers also get RT_HIT_IP_GRASS=1 (kHitIpGrass).
inline constexpr const char* ip_grass_capability_key="native-hit-ip-grass-v1";
// rt_hit_material_generated at startup with two-sided generated pool rows: RT_HIT_POOL_2S=1 (kHitPool2s).
inline constexpr const char* pool_2s_capability_key="native-hit-pool-2s-v1";
}
