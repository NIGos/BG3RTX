#pragma once
#include <atomic>
#include <cstdint>
namespace bg3::native_hit {
inline constexpr const char* capability_key="native-hit-material-v1";
inline void capability(uint32_t* word,bool value){if(word)std::atomic_ref<uint32_t>(*word).store(value?1u:0u,std::memory_order_release);}
inline bool capable(uint32_t* word){return word && std::atomic_ref<uint32_t>(*word).load(std::memory_order_acquire)==1;}
}
