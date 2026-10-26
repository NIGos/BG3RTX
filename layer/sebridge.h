// Script Extender bridge reader (docs/SE_BRIDGE.md): the files the BG3RTX_Bridge Lua mod
// writes with Ext.IO.SaveFile, read off the render thread and published as immutable
// snapshots. layer/assetdb.cpp merges them with the database (Bg3AssetDbApi v2).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bg3 {

struct SeLight {
  uint64_t id;               // entity handle (Ext.Utils.HandleToInteger)
  float pos[3], radius, color[3], intensity, kelvin;
  float flicker_speed, flicker_amount, move_speed, move_amount;
  uint32_t flags;            // Bg3RtLightFlags (low bits, as the Lua writes them)
  uint8_t owner, type, channel;
  const char* name;          // interned
};

struct SeTrigger {
  char kind;                 // 'A' atmosphere, 'L' lighting
  const char* name;          // interned
  const char* current;       // resource UUID, "" when none (interned)
};

struct SeSnapshot {
  // lights.txt
  uint32_t seq = 0;
  double time = 0;           // file write time (seconds since 1601, FILETIME / 1e7)
  float host[3] = {};
  const char* level = "";
  uint32_t all = 0, out = 0;
  float scan_us = 0, avg_us = 0, max_us = 0, classify_us = 0;
  std::vector<SeLight> lights;
  // world.txt
  const char* region = "";
  std::vector<SeTrigger> triggers;
};

// Parsers (tests use them directly). False when the text is not a complete export.
bool se_parse_lights(const std::string& text, SeSnapshot& out);
bool se_parse_world(const std::string& text, SeSnapshot& out);

// The latest export when it is fresh (written in this game session and younger than
// se_stale_ms, default 3000), else nullptr. Starts the reader thread on first use
// (se_bridge=0 disables it).
std::shared_ptr<const SeSnapshot> se_snapshot();
// The latest export whatever its age (for logging), and the number parsed so far.
std::shared_ptr<const SeSnapshot> se_latest();
uint32_t se_exports_read();
double se_age_s(const SeSnapshot& s);
void se_stop();
const char* se_intern(const std::string& s);

// The output folder: %LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\Script Extender\bg3rtx
// (%BG3RTX_SE_DIR% overrides it).
std::string se_dir();

}  // namespace bg3
