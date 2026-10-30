#pragma once
#include <atomic>
#include <cstdint>
// Sticky: held scenes and hot-reloaded cores retain the expanded alpha layout.
// A later device keeps that layout too, with baseline/dummy bindings if needed.
namespace bg3::omm {
inline constexpr char scene_capability_key[]="bg3rtx.omm.scene.capability.v1";
inline bool scene_capability(uint32_t* word){return word && (std::atomic_ref<uint32_t>(*word).load(std::memory_order_acquire)&1u);}
inline void publish_scene_capability(uint32_t* word){if(word)std::atomic_ref<uint32_t>(*word).fetch_or(1u,std::memory_order_release);}
inline bool scene_consumer(uint32_t* word){return word && (std::atomic_ref<uint32_t>(*word).load(std::memory_order_acquire)&2u);}
inline void publish_scene_consumer(uint32_t* word){if(word)std::atomic_ref<uint32_t>(*word).fetch_or(2u,std::memory_order_release);}
inline void release_scene_consumer(uint32_t* word){if(word)std::atomic_ref<uint32_t>(*word).fetch_and(~2u,std::memory_order_release);}
}
