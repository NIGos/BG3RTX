// BG3RTX.exe: settings and launcher for players (docs/USER_SETTINGS.md).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "../../common/hotkeys.h"
#include "../../common/settings.h"
#include "compatibility.h"
#include "diagnostics.h"

namespace launcher {

// In-game keyboard shortcuts: every binding of common/hotkeys.h, so a new id there reaches the launcher unchanged.
inline constexpr size_t kShortcutCount = std::size(bg3::hotkeys::ids);
inline std::array<std::string, kShortcutCount> default_shortcuts() {
  std::array<std::string, kShortcutCount> out;
  for (size_t i = 0; i < kShortcutCount; ++i) out[i] = bg3::hotkeys::defaults[i];
  return out;
}

// ---- text helpers ----
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
bool read_file(const std::wstring& path, std::string& out);
// Atomic: a temporary file next to it, then MoveFileEx over the old one.
bool write_file_atomic(const std::wstring& path, const std::string& text, std::wstring* err);
bool file_exists(const std::wstring& path);

// ---- paths ----
std::wstring exe_dir();    // bin/ (where the layer and its manifest are)
std::wstring root_dir();   // parent of bin/: bg3rtx.ini, bg3rtx_user.ini
std::wstring user_ini_path();
std::wstring base_ini_path();

// "<...>\Baldurs Gate 3" (the folder with bin\bg3.exe), or "".
std::wstring detect_game_dir();
std::wstring saved_game_dir();               // bg3rtx_launcher.ini
bool save_game_dir(const std::wstring& dir, std::wstring* err = nullptr);
bool valid_game_dir(const std::wstring& dir);
// Walks up from any path inside an install to the folder with bin\bg3.exe.
std::wstring game_dir_from_path(const std::wstring& path);
bool game_running();

// ---- launching (what scripts/launch-bg3.ps1 does, without PowerShell) ----
struct LaunchPlan {
  std::wstring exe, cwd;
  std::vector<std::wstring> args;
  std::vector<std::pair<std::wstring, std::wstring>> env_set;
  std::vector<std::wstring> env_unset;
};
// `exe_override` empty: <game_dir>\bin\bg3.exe (Vulkan). clean: also disable the
// Steam, Fossilize, EOS and GOG Vulkan layers.
LaunchPlan make_plan(const std::wstring& game_dir, const std::wstring& exe_override,
                     const std::vector<std::wstring>& args, bool clean, int fg);
std::wstring command_line(const LaunchPlan& p);
std::wstring describe(const LaunchPlan& p);
bool compose_environment(const LaunchPlan& p, const wchar_t* inherited, std::wstring& out);
bool launch(const LaunchPlan& p, DWORD* pid, std::wstring* err);

// ---- GPU probe (Vulkan, own process, no layers) ----
std::vector<bg3::settings::Gpu> probe_gpus(std::wstring* err);
bool local_grooms_available(const std::wstring& database_directory);
int best_gpu(const std::vector<bg3::settings::Gpu>& gpus);   // -1 if none
std::wstring caps_text(unsigned caps);

// ---- settings state shared by the UI and the command line ----
struct State {
  std::string base_text, user_text;   // file contents
  bg3::settings::Map base, user;
  std::vector<int> loaded;            // knob values when loaded (for restart markers)
  std::vector<int> values;            // knob values being edited (-1: hand-edited, kept)
  int preset = bg3::settings::kHigh;  // base preset written as preset=
  bg3::settings::Gpu gpu;
  bool have_gpu = false;
  bool settings_readable = true;
  unsigned caps = ~0u;
  int suggested = bg3::settings::kHigh;
  std::string dlss5_runtime_path, loaded_dlss5_runtime_path, dlss5_runtime_status;
  // One entry per bg3::hotkeys::ids (all ray tracing, DLSS 5, and any binding added there), in that order.
  std::array<std::string, kShortcutCount> shortcuts = default_shortcuts();
  std::array<std::string, kShortcutCount> loaded_shortcuts = default_shortcuts();
};
void load_state(State& s);
bg3::settings::KVList forced_keys(const State& s);
bool save_state(State& s, std::wstring* err);
int effective_fg(const State& s);

// ---- game upscaler guard (game_dlss_guard, docs/TRACK_A_RR.md "Game upscaler guard") ----
// BG3 rewrites graphicSettings.lsx from defaults (no DLSS) after a startup where it judged
// DLSS unavailable; every later launch then never creates DLSS, so Ray Reconstruction never
// runs. Pure text rewrite: ResolutionUpscaleType=3 (DLSS); DLSS2Mode=2 (Quality) only when
// absent, so the player's own DLSS mode is kept. `changes` lists what was done.
std::string guard_upscaler_text(const std::string& lsx, std::vector<std::string>* changes);
// With game_dlss_guard=1 and rr=1: applies it to the player's graphicSettings.lsx (backup
// .lsx.bg3rtx-bak first). Returns a one-line note, empty when nothing changed.
std::wstring guard_game_upscaler(const State& s);

}  // namespace launcher
