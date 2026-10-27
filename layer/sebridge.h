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

// ---- what the bridge's Lua does (mods/BG3RTX_Bridge/ScriptExtender/Lua/BootstrapClient.lua) ----
// The join tolerances below are derived from these; keep them equal to the Lua.
constexpr double kSeExportPeriodS = 0.25;   // PERIOD_US 250000: the earliest tick after 250 ms
constexpr double kSePollS = 0.1;            // this reader's poll of the files (sebridge.cpp reader)
constexpr float kSeRange = 60.0f;           // RANGE: lights within 60 m of the controlled character
constexpr float kSeCell = 16.0f;            // CELL: placed lights are searched through a 16 m grid,
constexpr int kSeCellsY = 2;                //   cells cy-2..cy+2 vertically (Lua tick(), "+-32 m")
constexpr float kSePosQuant = 0.5e-4f;      // positions "%.4f": at most half the last digit off
constexpr float kSeRadiusQuant = 0.5e-3f;   // radius "%.3f" (r.s1)
constexpr float kSeHostQuant = 0.5e-3f;     // host position "%.3f" (the header)
// The upper bound of the speed of a moving light's carrier (m/s), for ls_dyn_speed: 8 m/s is
// the MovementSpeedSprint and MovementSpeedDash most root templates that set them use
// (Shared/RootTemplates/_merged.lsf: Sprint 8.0 in 13 templates, Dash 8.0 in 11; party
// members sprint to catch up with the leader). Humanoids set no LocomotionParams (engine
// default, run 3.75 where stated), so the "[se] motion" log line measures it in game.
constexpr float kSeMoveSpeedMax = 8.0f;

// Float ulp at magnitude m: for m in [2^(e-1), 2^e), 2^(e-24).
float se_ulp(float m);
// Position tolerance of the join for a light that stays put (docs/SE_BRIDGE.md "Join
// tolerances"): per coordinate 4 ulp at the largest coordinate the comparison involves
// (|p| + `reach`, the camera being within `reach` of the light) + the export's %.4f
// rounding, times sqrt(3) for the distance, + `move_amount` (the light's authored jitter).
// The 4 ulp: the game's camera-relative position and our + camera (2 roundings), the
// bridge's float parse (1), and one for the engine's transform composition: a placed
// light's array position was within 2.9 ulp (distance) of its database record in the 93
// joins of captures/assetdb_join_20261026-*.csv (|p| up to 1570 m). Database positions
// reach 8497 m (data/assetdb, 19572 lights): 6.8 mm there, 0.5 mm at Campsite (|p| ~630 m).
float se_static_tol(const float p[3], float reach, float move_amount);
// Radius tolerance for a light that stays put: the export's %.3f rounding + 4 relative ulp
// (the game's 1/r storage 0.5, the shader's 1/inv_r division 2.5 by the Vulkan precision
// rules of OpFDiv, the bridge's parse 0.5, rounded up). The database's range matched
// 1/inv_r to 1e-6 (relative) in the same captures.
float se_static_radius_tol(float r);
// Radius tolerance for lights that move (item, character, visual lights): not derived (a
// VFX light's radius may be animated, and no capture holds both values of a moving light);
// the old 2% + 1 cm, and the "[se] join" line logs the largest disagreement it accepted.
float se_moving_radius_tol(float r);
// True when the bridge's export would hold a placed light at `p` if that light were loaded:
// within kSeRange of the export's host and inside the Lua's vertical cell range, with the
// host's %.3f rounding as margin. A database light there that the bridge did not report is
// not loaded now.
bool se_covers(const SeSnapshot& s, const float p[3]);
// The bridge's export interval: the longest of the last 16 (file write times), at least
// kSeExportPeriodS. kSeExportPeriodS until two exports were read.
double se_period_s();
// The staleness limit (ms): se_stale_ms when > 0, else se_stale_periods x se_period_s() + the
// reader's poll.
double se_stale_ms();

// Parsers (tests use them directly). False when the text is not a complete export.
bool se_parse_lights(const std::string& text, SeSnapshot& out);
bool se_parse_world(const std::string& text, SeSnapshot& out);

// The latest export when it is fresh (written in this game session and younger than
// se_stale_ms()), else nullptr. Starts the reader thread on first use
// (se_bridge=0 disables it).
std::shared_ptr<const SeSnapshot> se_snapshot();
// The latest export whatever its age (for logging), and the number parsed so far.
std::shared_ptr<const SeSnapshot> se_latest();
uint32_t se_exports_read();
double se_age_s(const SeSnapshot& s);
double se_now_s();   // the clock of SeSnapshot::time (seconds since 1601)
void se_stop();
const char* se_intern(const std::string& s);

// The output folder: %LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\Script Extender\bg3rtx
// (%BG3RTX_SE_DIR% overrides it).
std::string se_dir();

}  // namespace bg3
