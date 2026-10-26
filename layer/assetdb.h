// Offline asset database in the host layer (docs/ASSET_DB.md): the loader behind
// Bg3HostApi::assetdb, level detection and the runtime join check.
#pragma once
#include <atomic>
#include <cstdint>

#include "../common/assetdb_api.h"
#include "host.h"

namespace bg3 {

// The table handed to the core (host_api.h, ABI 6).
const Bg3AssetDbApi* assetdb_api();

// Join check (assetdb_join=1): reads the lighting pass's light list at one tiled-lighting
// dispatch every assetdb_join_interval presents and matches it against the database.
extern std::atomic<bool> g_assetdb_armed;     // a capture is wanted at the next lighting dispatch
extern std::atomic<bool> g_assetdb_counting;  // draws of this frame are being counted
void assetdb_dispatch_pre(VkCommandBuffer cmd);   // cmd.cpp, before each game dispatch (when armed)
void assetdb_count_draw(uint64_t vs, uint64_t fs);  // cmd.cpp, per draw (when counting)
inline void assetdb_on_dispatch_pre(VkCommandBuffer cmd) {
  if (g_assetdb_armed.load(std::memory_order_relaxed)) assetdb_dispatch_pre(cmd);
}
inline void assetdb_note_draw(uint64_t vs, uint64_t fs) {
  if (g_assetdb_counting.load(std::memory_order_relaxed)) assetdb_count_draw(vs, fs);
}
void assetdb_on_present(uint64_t frame);          // layer.cpp, main device present
// Makes `name` (a level, or a sub-level of one) current; false when it is not in the
// database. For tests and tools; the layer itself detects the level.
bool assetdb_select_level(const char* name, const char* source);
void assetdb_device_destroyed();

}  // namespace bg3
