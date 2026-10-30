#pragma once
#include <atomic>
#include <cstdint>

// Existing host persist() storage. Sticky for the process: retired/held scenes
// may retain kind3 after the ini is switched off or the core is reloaded.
// persist returns zeroed, operator-new-aligned storage; NEVER clear this word.
namespace bg3::native_scalp {
inline constexpr char capability_key[] = "bg3rtx.native_scalp.capability.v1";
inline void publish_capability(uint32_t* word) {
  if (word) std::atomic_ref<uint32_t>(*word).fetch_or(1u, std::memory_order_release);
}
inline bool has_capability(uint32_t* word) {
  return word && (std::atomic_ref<uint32_t>(*word).load(std::memory_order_acquire) & 1u);
}
}
