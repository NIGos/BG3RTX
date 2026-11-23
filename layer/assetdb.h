// Offline asset database in the host layer (docs/ASSET_DB.md): the loader behind
// Bg3HostApi::assetdb, level detection and the runtime join check.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../common/assetdb_api.h"
#include "host.h"

namespace bg3 {

// The table handed to the core (host_api.h, ABI 6).
const Bg3AssetDbApi* assetdb_api();
// The database file the layer loads: %BG3RTX_ASSETDB%, else this checkout's data/assetdb, else
// (a git worktree without one) the main checkout's, resolved through the git common dir.
std::string assetdb_file();

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

// The level vote (docs/ASSET_DB.md "Level detection", step 2): each voter (a froxel-listed runtime light, or a
// projector's near-face anchor, absolute position) votes once for every database level that has a light within
// tol of it (plus the light's authored move_amount). change: the rule says switch from `current` to `best`
// (at least 3 votes and more than the current level's; with no level yet, the only level voted for).
struct LevelVote {
  int best = -1;
  uint32_t best_votes = 0, cur_votes = 0;
  uint32_t levels = 0;   // distinct levels voted for
  bool change = false;
  // clear: fail closed. No voter matches `current`, no switch qualifies, and either at least 3 voters were seen or this
  // is the first vote after a level load (after_load).
  bool clear = false;
};
LevelVote assetdb_vote(const std::array<float, 3>* voters, size_t n, int current, float tol, bool after_load = false);
// assetdb_level_vote=1 (default 0): the level vote runs without the join diagnostics (layer/assetdb.cpp
// vote_check, every assetdb_join_interval presents). Turning both keys off forgets a level the vote set.
// Forget the current level (as if none had been detected): the clear rules and tests.
void assetdb_clear_level(const char* why);
// Makes `name` (a level, or a sub-level of one) current; false when it is not in the
// database. For tests and tools; the layer itself detects the level.
bool assetdb_select_level(const char* name, const char* source);
void assetdb_device_destroyed();

}  // namespace bg3
