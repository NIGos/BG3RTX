#pragma once
#include <atomic>
#include <cstdint>
namespace bg3::native_tileset {
// Pak COLOR rows reuse the audited native Tileset capture and consumer layout.
inline constexpr bool requested(bool native,bool paks){return native || paks;}
// Startup layout capability is sticky through hot reload and key-off held scenes.
inline constexpr char capability_key[]="bg3rtx.native_tileset.capability.v1";
inline void publish(uint32_t* word){if(word)std::atomic_ref<uint32_t>(*word).fetch_or(1u,std::memory_order_release);}
inline bool enabled(uint32_t* word){return word&&(std::atomic_ref<uint32_t>(*word).load(std::memory_order_acquire)&1u);}
}
