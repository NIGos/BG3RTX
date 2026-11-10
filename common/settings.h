// BG3RTX settings model: the single source of truth for the user-facing
// settings (docs/SETTINGS_DESIGN.md section 6), the presets (section 5.2) and
// the layered ini files:
//
//   bg3rtx.ini        developer / shipped defaults (every key)
//   bg3rtx_user.ini   written by the launcher (and later the overlay)
//   %BG3RTX_INI%      per-run overrides (scripts/auto-run.ps1, tests)
//
// Each file is a layer. A layer that contains `preset=<id>` first expands that
// preset's keys, then applies its own keys, so individual keys override the
// preset and a later layer overrides everything before it.
//
// Header-only and standard C++ only: included by the layer (layer/util.cpp),
// the launcher (tools/launcher), the unit tests (tests/settings_test.cpp) and,
// in phase 2, the in-game overlay. data/settings_schema.json is schema_json()
// written out for non-C++ readers; a unit test keeps the two identical.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bg3 {
namespace settings {

using Map = std::unordered_map<std::string, std::string>;
using KVList = std::vector<std::pair<std::string, std::string>>;

// ---- presets ----

enum Preset : int { kLow, kMedium, kHigh, kUltra, kPresetCount };
inline constexpr const char* kPresetId[kPresetCount] = {"low", "medium", "high", "ultra"};
inline constexpr const char* kPresetLabel[kPresetCount] = {"Low", "Medium", "High", "Ultra"};
inline constexpr const char* kPresetDesc[kPresetCount] = {
    "Lightest. Lighting and sun shadows use visible surfaces only; ray tracing and DLSS Ray Reconstruction are off.",
    "Ray-traced lighting and sun shadows on solid surfaces, Fast local light shadows and Glossy reflections.",
    "Adds foliage to ray tracing, Full local light shadows and Standard reflections.",
    "Adds hidden-roof, interior light and hair shadows, two-ray indirect lighting, Rough reflections, water lighting, "
    "Far off-screen bounce light and depth of field smoothing. Limits rendering to 58 fps.",
};

// ---- hardware capabilities a choice needs ----

enum Cap : unsigned {
  kCapRayQuery = 1u << 0,   // VK_KHR_ray_query + VK_KHR_acceleration_structure
  kCapDlss = 1u << 1,       // NVIDIA RTX (DLSS SR/RR): Ray Reconstruction
  kCapFg = 1u << 2,         // RTX 40 and later: DLSS-G 2x
  kCapOpacityMicromap = 1u << 4, // EXT opacity micromap + core BDA/timeline/sync2
  kCapLinearSweptSpheres = 1u << 5,
  kCapShaderInt64 = 1u << 6,
  kCapFragmentStores = 1u << 7,
  kCapHairGrooms = 1u << 8, // Verified local groom files, supplied by the launcher.
  kCapMfg = 1u << 3,        // RTX 50 and later: DLSS-G 3x/4x
};

// How a change reaches the running game (section 7.4).
enum class Apply { Unspecified, Live, Hitch, Restart };

inline constexpr const char* apply_label(Apply mode, bool release = true) {
  if (!release) {
    switch (mode) {
      case Apply::Live: return "Live";
      case Apply::Hitch: return "Applies after a short hitch";
      case Apply::Restart: return "Restart to apply";
      default: return "";
    }
  }
  switch (mode) {
    case Apply::Live: return "Applies after saving";
    case Apply::Hitch: return "Applies after saving; may briefly pause";
    case Apply::Restart: return "Restart the game to apply";
    default: return ""; // settings_test rejects missing metadata, including developer choices.
  }
}

struct KV {
  const char* key;
  const char* value;
};

// Another knob's choice index must be at least `min_choice`.
struct Need {
  const char* knob;
  int min_choice;
};

struct Choice {
  const char* id;
  const char* label;
  unsigned caps;   // Cap bits
  KV kv[24];        // keys this choice writes (key == nullptr ends the list)
  Need need[3];    // dependencies on other knobs (knob == nullptr ends the list)
  // Migration of a file whose keys match no choice exactly (written before the knob became one
  // level choice, or hand-edited): the highest choice one of whose `mark` keys has this choice's
  // value is selected, and saving writes all of its keys (GUI_AUDIT_20261101.md).
  const char* mark[8] = {};
  bool dev = false;  // shown only with dev=1 (always a knob's last choices)
  bool experimental = false;
  bool awaiting_qualification = false;
  Apply apply = Apply::Unspecified; // Explicit for every choice; never infer from empty UI text.
};

enum class Kind { Choice, Slider };

struct Slider {
  int min, max, step;
  bool has_off;        // value 0 means "Off" (outside min..max)
  float scale;         // key value = slider value * scale
  const char* unit;
  const char* keys[3]; // all set to the same value
};

// A raw key a choice at least `min_choice` needs (startup prerequisites without a GUI entry).
struct KeyNeed {const char* key; const char* value; const char* label; int min_choice = 1;};

// Additive UI contract. Reference costs are not predictions for another GPU.
struct Cost {
  const char* status = "Unknown";
  const char* gpu = "Unknown";
  const char* vram = "Unknown";
  const char* provenance = "No qualified measurement available.";
};

struct Knob {
  const char* id;
  const char* label;
  const char* desc;
  Kind kind;
  Apply apply;
  const char* apply_note;   // shown next to the control
  bool advanced;
  Choice choices[5];        // Kind::Choice (id == nullptr ends the list)
  Slider slider;            // Kind::Slider
  int preset[kPresetCount]; // choice index or slider value per preset
  bool experimental = false; // shown under "Experimental": off in every preset, being tested
  KeyNeed required_on[4] = {}; // Raw startup prerequisites; checked, never silently written.
  bool dev_only = false;     // developer tools: the GUI shows them only with dev=1
  const char* category = "Advanced";
  Cost cost = {};
  const char* details = "";
};

// Apply audit: core/gi.cpp reads lighting/strength each frame; refresh() builds variants on demand.
// layer/ngx.cpp switches RR with a history reset. streamline.cpp reflex_apply() and framecap.cpp
// read latency/cap live. geocapture.cpp init fixes rt_alpha/hair/tileset strides; hit_material.cpp
// device_created fixes material/wind capture. water.cpp patches graphics pipelines at creation.
// OMM device setup requires restart.
// Mixed controls declare their most demanding transition, with exceptions in apply_note/choices.
// cfg_hot consumers: alphatest.cpp (VT masks), geocapture.cpp (spill/hooks), hit_material.cpp
// (CPU lists), tracker.cpp/layer.cpp (submit batching). Their prerequisites are separate controls.
// clang-format off
inline constexpr Knob kKnobs[] = {
  {"rr", "DLSS Ray Reconstruction",
   "Reduces noise in ray-traced lighting.",
   Kind::Choice, Apply::Hitch, "May briefly pause when changed", false,
   {{"off", "Off", 0, {{"rr", "0"}}, {}, {}, false, false, false, Apply::Hitch},
    {"on", "On", kCapDlss, {{"rr", "1"}}, {}, {}, false, false, false, Apply::Hitch}},
   {}, {0, 1, 1, 1}, false, {}, false, "Lighting", {}, "Needs an NVIDIA RTX GPU. Select DLSS as the upscaler in the game's graphics options."},

  {"rt_geometry", "Ray-tracing detail",
   "Chooses what ray-traced lighting and shadows include: solid surfaces only, or also foliage, grates and chains.",
   Kind::Choice, Apply::Restart, "Restart the game after changing this setting", false,
   {{"off", "Off", 0, {{"rt_build", "0"}, {"rt_alpha", "0"}}, {}, {}, false, false, false, Apply::Restart},
    {"opaque", "Solid surfaces", kCapRayQuery, {{"rt_build", "auto"}, {"rt_alpha", "0"}}, {}, {}, false, false, false, Apply::Restart},
    {"full", "Include foliage", kCapRayQuery, {{"rt_build", "auto"}, {"rt_alpha", "1"}}, {}, {}, false, false, false, Apply::Restart}},
   {}, {0, 1, 2, 2}, false, {}, false, "Lighting", {}, "Off turns off all ray-traced lighting, shadows, reflections and water."},

  {"sun_shadows", "Sun shadows",
   "Ray-traced sun shadows. Include hidden roofs also keeps shadows from roofs and canopies the game hides near the camera.",
   Kind::Choice, Apply::Live, "", false,
   {{"ss", "Visible surfaces", 0, {{"rt_shadows", "0"}, {"contact_shadows", "1"}, {"rt_cascade_casters", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"rt", "Ray-traced", kCapRayQuery, {{"rt_shadows", "1"}, {"contact_shadows", "1"}, {"rt_cascade_casters", "0"}}, {{"rt_geometry", 1}}, {"rt_shadows"}, false, false, false, Apply::Live},
    {"hidden_roofs", "Include hidden roofs", kCapRayQuery, {{"rt_shadows", "1"}, {"contact_shadows", "1"}, {"rt_cascade_casters", "1"}}, {{"rt_geometry", 1}}, {"rt_cascade_casters"}, false, false, false, Apply::Live}},
   {}, {0, 1, 1, 2}, false, {}, false, "Lighting", {}, "Include hidden roofs costs about 0.3–1.3 ms more on an RTX 5090, depending on the scene."},

  {"gi", "Indirect lighting",
   "Adds bounced light and soft shading where surfaces meet. The highest level traces two rays per pixel for less noise.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"gi", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"ss", "Visible surfaces", 0, {{"gi", "1"}, {"gi_rt", "0"}, {"gi_rays", "2"}, {"gi_steps", "12"}}, {}, {}, false, false, false, Apply::Live},
    {"rt", "Ray-traced", kCapRayQuery, {{"gi", "1"}, {"gi_rt", "1"}, {"gi_rr_rays", "1"}}, {{"rt_geometry", 1}}, {}, false, false, false, Apply::Live},
    {"rt_high", "Ray-traced high", kCapRayQuery, {{"gi", "1"}, {"gi_rt", "1"}, {"gi_rr_rays", "2"}}, {{"rt_geometry", 1}}, {}, false, false, false, Apply::Live}},
   {}, {1, 2, 2, 3}, false, {}, false, "Lighting", {}},

  {"local_shadows", "Local light shadows",
   "Adds shadows from torches, lamps and spells.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"rt_local_shadows", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"fast", "Fast", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "1"}, {"ls_shadowed", "0"}}, {{"rt_geometry", 1}, {"rr", 1}}, {}, false, false, false, Apply::Live},
    {"full", "Full", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "0"}, {"ls_shadowed", "3"}}, {{"rt_geometry", 1}}, {}, false, false, false, Apply::Live},
    {"full_plus", "Full + shadowed lights", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "0"}, {"ls_shadowed", "1"}}, {{"rt_geometry", 1}}, {}, true, false, false, Apply::Live}},
   {}, {0, 1, 2, 2}, false, {}, false, "Lighting", {}, "Fast needs DLSS Ray Reconstruction. Full keeps the game's own shadows where they are needed."},

  {"reflections", "Reflections",
   "Adds reflections to smooth surfaces and, at higher levels, rougher surfaces.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"rt_reflections", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"glossy", "Glossy", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.4"}}, {{"rt_geometry", 1}, {"gi", 1}}, {}, false, false, false, Apply::Live},
    {"standard", "Standard", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.6"}}, {{"rt_geometry", 1}, {"gi", 1}}, {}, false, false, false, Apply::Live},
    {"rough", "Rough", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.8"}}, {{"rt_geometry", 1}, {"gi", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 1, 2, 3}, false, {}, false, "Surfaces", {}, "Needs Ray-tracing detail and Indirect lighting. Without DLSS Ray Reconstruction, only mirror-like reflections are traced."},

  {"frame_generation", "Frame Generation",
   "Adds generated frames between rendered frames for smoother motion.",
   Kind::Choice, Apply::Restart, "Restart when turning Frame Generation on or off; multiplier changes apply after saving", false,
   {{"off", "Off", 0, {{"fg", "0"}}, {}, {}, false, false, false, Apply::Restart},
    {"2x", "2x", kCapFg, {{"fg", "2"}}, {{"reflex", 1}}, {}, false, false, false, Apply::Restart},
    {"3x", "3x", kCapMfg, {{"fg", "3"}}, {{"reflex", 1}}, {}, false, false, false, Apply::Restart},
    {"4x", "4x", kCapMfg, {{"fg", "4"}}, {{"reflex", 1}}, {}, false, false, false, Apply::Restart}},
   {}, {1, 1, 1, 1}, false, {}, false, "Performance", {}, "Needs NVIDIA Reflex On or On + Boost. Turning it on or off needs a game restart; switching between 2x, 3x and 4x applies after saving. Generated frames make motion smoother but do not raise the rendered frame rate."},

  {"rt_intensity", "Mod lighting strength",
   "Blends indirect lighting, local shadows and reflections toward the game's original appearance at lower values.",
   Kind::Slider, Apply::Live, "", false,
   {}, {50, 100, 5, false, 0.01f, "%", {"gi_strength", "ls_strength", "rf_strength"}}, {100, 100, 100, 100}, false, {}, false, "Lighting", {}},

  {"fps_cap", "Rendered frame limit",
   "Limits the frame rate before Frame Generation adds frames.",
   Kind::Slider, Apply::Live, "", false,
   {}, {30, 240, 1, true, 1.0f, "fps", {"fps_cap"}}, {0, 0, 0, 58}, false, {}, false, "Performance", {}, "Ultra limits rendering to 58 fps. A limit is a ceiling, not a target."},

  {"reflex", "NVIDIA Reflex",
   "Reduces the delay between your input and the image on screen.",
   Kind::Choice, Apply::Live, "", true,
   {{"off", "Off", 0, {{"reflex", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", 0, {{"reflex", "1"}}, {}, {}, false, false, false, Apply::Live},
    {"boost", "On + Boost", 0, {{"reflex", "2"}}, {}, {}, false, false, false, Apply::Live}},
   {}, {1, 1, 1, 1}, false, {}, false, "Performance", {}, "Frame Generation needs On or On + Boost."},

  // M5 water chain (docs/PLAN_M5_WATER.md "Promotion": m5_promotion passed at S8, Flymm and camp, 11-01): Advanced,
  // before the Experimental section; "+ Refraction" in Ultra, Off elsewhere. The compose levels patch the
  // audited water FS at pipeline creation (restart to turn on, key_change_needs_restart) and need 64-bit
  // shader integers at device creation (sharc_device=1).
  {"exp_water", "Water lighting",
   "Adds water reflections, shadows and, at the highest level, a refracted view of the riverbed or pool floor. About 0.5–0.7 ms on an RTX 5090 (estimate).",
   Kind::Choice, Apply::Restart, "Restart to enable; disabling applies after saving.", true,
   {{"off", "Off", 0, {{"rt_water_geometry", "0"}, {"rt_water_reflections", "0"}, {"rt_water_compose", "0"},
      {"rt_water_shadows", "0"}, {"rt_water_ls_shadows", "0"}, {"rt_water_game_shadow", "0"}, {"rt_water_refraction", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"reflections", "Reflections", kCapRayQuery, {{"rt_water_geometry", "1"}, {"rt_water_reflections", "1"}, {"rt_water_compose", "1"},
      {"rt_water_shadows", "0"}, {"rt_water_ls_shadows", "0"}, {"rt_water_game_shadow", "0"}, {"rt_water_refraction", "0"}},
     {{"rt_geometry", 1}}, {"rt_water_compose", "rt_water_reflections", "rt_water_geometry"}, false, false, false, Apply::Restart},
    {"shadows", "Reflections and shadows", kCapRayQuery, {{"rt_water_geometry", "1"}, {"rt_water_reflections", "1"}, {"rt_water_compose", "1"},
      {"rt_water_shadows", "1"}, {"rt_water_ls_shadows", "1"}, {"rt_water_game_shadow", "1"}, {"rt_water_refraction", "0"}},
     {{"rt_geometry", 1}}, {"rt_water_shadows", "rt_water_ls_shadows", "rt_water_game_shadow"}, false, false, false, Apply::Restart},
    {"refraction", "Include refraction", kCapRayQuery, {{"rt_water_geometry", "1"}, {"rt_water_reflections", "1"}, {"rt_water_compose", "1"},
      {"rt_water_shadows", "1"}, {"rt_water_ls_shadows", "1"}, {"rt_water_game_shadow", "1"}, {"rt_water_refraction", "1"}},
     {{"rt_geometry", 1}}, {"rt_water_refraction"}, false, false, false, Apply::Restart}},
   {}, {0, 0, 0, 3}, false, {{"rt","1","Ray-tracing support"},{"sharc_device","1","Ray-tracing support"}}, false, "Surfaces", {"Estimate", "0.5–0.7 ms", "Up to 520 MB", "Estimated water cost: 0.5–0.7 ms and up to 520 MB on RTX 5090 at Water Queens House."}, "Needs Ray-tracing detail. Turning water on or raising its level needs a game restart; lowering it or turning it off applies after saving. Uses up to about 520 MB of video memory."},

  {"dlss5_nr", "DLSS 5 Neural Rendering (experimental)",
   "(experimental) Neural relighting after Ray Reconstruction and before post effects and frame generation. Requires your own DLSS 5 runtime.",
   Kind::Choice, Apply::Restart, "Restart the game after changing DLSS 5 or its runtime file", false,
   {{"off", "Off", 0, {{"dlss5_nr", "0"}}, {}, {}, false, false, false, Apply::Restart},
    {"on", "On", kCapDlss, {{"dlss5_nr", "1"}}, {{"rr", 1}}, {}, false, false, true, Apply::Restart}},
   {}, {0, 0, 0, 0}, true, {{"rr_post", "1", "Ray Reconstruction before post effects"}}, false, "DLSS 5", {},
   "Experimental and off in every preset. Requires Ray Reconstruction before post effects and a compatible user-provided nvngx_dlssnr.dll. The launcher prepares the runtime before Play; custom or unsigned runtimes are identified in the log and may be rejected by NVIDIA (0xbad0000b). Restart after changing this option or the runtime file."},

  {"water_surface_fresnel", "Water surface reflections",
   "Experimental: works out the reflection at the water surface separately from the water's depth colour. Mostly visible at the edges of shallow pools; open water barely changes.",
   Kind::Choice, Apply::Restart, "on: restart; off: live", true,
   {{"off", "Off", 0, {{"rt_water_surface_fresnel", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_water_surface_fresnel", "1"}}, {{"exp_water", 1}},
     {"rt_water_surface_fresnel"}, false, true, true, Apply::Restart}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "Needs Water lighting. Turning it on needs a game restart; turning it off applies after saving. Compare the reflections on shallow pools."},

  {"fg_destroy_wait_exact", "Frame generation pacing test",
   "Experimental: may reduce frame generation stutter when the game replaces temporary images. Cost and smoothness are unqualified.",
   Kind::Choice, Apply::Live, "Applies after saving.", false,
   {{"off", "Standard", 0, {{"fg_destroy_wait_exact", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "Deferred", kCapFg, {{"fg_destroy_wait_exact", "1"}}, {{"frame_generation", 1}}, {}, false, true, true, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "Compare smoothness while turning in the same scene. Experimental; disabled in every preset."},

  {"fg_present_metering", "Multi-frame presentation test",
   "Experimental: enables an optional driver presentation capability for frame generation. Smoothness and cost are unqualified.",
   Kind::Choice, Apply::Restart, "Restart to apply.", false,
   {{"off", "Standard", 0, {{"fg_present_metering", "0"}}, {}, {}, false, false, false, Apply::Restart},
    {"on", "Driver metering", kCapMfg, {{"fg_present_metering", "1"}}, {{"frame_generation", 1}}, {}, false, true, true, Apply::Restart}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "Requires Frame generation and a game restart. Compare 3x smoothness in the same scene. Disabled in every preset."},

  // om3 qualified the fine-LOD policy; om4 qualified the dynamic micromap bake (2026-11-03).
  // Keep the legacy ids for saved GUI selections. Hardware and local masks remain prerequisites.
  {"exp_omm", "Opacity micromaps",
   "Accelerates cutout shadows with locally prepared foliage masks on compatible hardware. About 1 ms faster at camp on RTX 5090; requires a restart.",
   Kind::Choice, Apply::Restart, "restart required", true,
   {{"off", "Off", 0, {{"rt_omm", "0"}, {"rt_omm_dyn", "0"}, {"rt_alpha_fine_lod", "0"}, {"rt_omm_reference", "0"}}, {}, {}, false, false, false, Apply::Restart},
    {"on", "On", kCapRayQuery | kCapOpacityMicromap, {{"rt_omm", "1"}, {"rt_omm_dyn", "1"}, {"rt_alpha_fine_lod", "1"}, {"rt_omm_reference", "0"}},
     {{"rt_geometry", 2}, {"exp_vt_alpha_masks", 1}}, {}, false, false, false, Apply::Restart}},
   {}, {0, 0, 1, 1}, false, {{"rt","1","RT device setup"},{"rt_bvh_split","1","Static geometry cache"}}, true, "Advanced", {"Measured", "About -1 ms", "Up to 256 MB micromap cache", "RTX 5090; camp om4; 2026-11-03 08:10; RT passes 8.65-8.75 ms vs reference 9.37-10.08 ms; 1.17G rays compared, 0 not compared, 0 violations. Reference only."}},

  {"exp_vt_alpha_masks", "Offline cutout masks",
   "Uses foliage masks prepared locally from your game installation. Required for accelerated cutout shadows; unavailable masks keep the original path.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_vt_alpha_masks", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_vt_alpha_masks", "1"}}, {{"rt_geometry", 2}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 1, 1}, false, {{"rt","1","RT device setup"},{"rt_alpha","1","Cutout capture"}}, true, "Advanced", {"Measured", "Not measured", "80.9 MB camp masks", "RTX 5090; va1 camp 2026-11-02; energy +0.24% within +/-1.95% repeat band; RT passes unchanged. Masks generated on the player's PC. Reference only."}},

  // GUI audit A: one chain, legacy partial files migrate via Choice::mark.
  {"exp_hidden_surface", "Off-screen bounce light",
   "Adds bounced light and glow from surfaces outside the camera's view. Far carries sunlight bounce up to 6 m. About 0.6 ms for Near and 3–4 ms for Far on an RTX 5090.",
   Kind::Choice, Apply::Restart, "Restart to enable or disable; changing Near/Far may briefly pause.", true,
   {{"off", "Off", 0, {{"rt_hit_material", "0"}, {"rt_hit_material_instanced", "0"}, {"rt_hit_material_wind_rgb", "0"},
      {"gi_hit_direct", "0"}, {"gi_hit_sun", "0"}, {"rf_hit_shade", "0"}, {"rf_hit_gi", "0"},
      {"rt_hit_emission", "0"}, {"rt_emissive", "0"}, {"gi_far", "0"}, {"gi_far_m", "6"}}, {}, {}, false, false, false, Apply::Restart},
    {"near", "Near", kCapRayQuery, {{"rt_hit_material", "1"}, {"rt_hit_material_instanced", "1"}, {"rt_hit_material_wind_rgb", "1"},
      {"gi_hit_direct", "1"}, {"gi_hit_sun", "1"}, {"rf_hit_shade", "0"}, {"rf_hit_gi", "0"},
      {"rt_hit_emission", "1"}, {"rt_emissive", "1"}, {"gi_far", "0"}, {"gi_far_m", "6"}},
     {{"rt_geometry", 2}, {"gi", 2}}, {"rt_hit_material", "rt_hit_material_instanced", "rt_hit_material_wind_rgb", "gi_hit_direct", "gi_hit_sun", "rt_hit_emission"}, false, false, false, Apply::Restart},
    {"far", "Far (6 m)", kCapRayQuery, {{"rt_hit_material", "1"}, {"rt_hit_material_instanced", "1"}, {"rt_hit_material_wind_rgb", "1"},
      {"gi_hit_direct", "1"}, {"gi_hit_sun", "1"}, {"rf_hit_shade", "0"}, {"rf_hit_gi", "0"},
      {"rt_hit_emission", "1"}, {"rt_emissive", "1"}, {"gi_far", "1"}, {"gi_far_m", "6"}},
     {{"rt_geometry", 2}, {"gi", 2}}, {"gi_far"}, false, false, false, Apply::Restart},
    {"reflections", "Far + reflections", kCapRayQuery, {{"rt_hit_material", "1"}, {"rt_hit_material_instanced", "1"}, {"rt_hit_material_wind_rgb", "1"},
      {"gi_hit_direct", "1"}, {"gi_hit_sun", "1"}, {"rf_hit_shade", "1"}, {"rf_hit_gi", "1"},
      {"rt_hit_emission", "1"}, {"rt_emissive", "1"}, {"gi_far", "1"}, {"gi_far_m", "6"}},
     {{"rt_geometry", 2}, {"gi", 2}, {"reflections", 1}}, {"rf_hit_shade", "rf_hit_gi"}, true, true, false, Apply::Restart}},
   {}, {0, 0, 0, 2}, false,
   {{"rt","1","Ray-tracing support"},{"rt_alpha","1","Foliage capture"},{"rt_bvh_split","1","Hidden-surface tracking"},
    {"gi_rt_fallback","0","Hidden-surface lighting mode",1}}, false, "Lighting", {"Estimate", "Near about 0.6 ms; Far about 4 ms", "Unknown", "Estimated cost on RTX 5090: about 0.6 ms for Near or 4 ms for Far."}, "Needs Ray-tracing detail: Include foliage and Indirect lighting: Ray-traced. Turning it on or off needs a game restart; switching between Near and Far may briefly pause the game."},

  // gi_rt_pipeline (docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"): the hit-GI trace as a ray tracing pipeline.
  // Qualified, on in every preset (user decisions 2026-11-08; live inn and camp about -2 ms, energy unchanged); a no-op when the
  // hit-GI trace does not run (core/gi.cpp hit_gi_dispatch is its only reader). The id keeps its
  // exp_ prefix for saved selections. The reorder option below stays experimental and developer-only.
  {"exp_gi_rt_pipeline", "Faster bounce light tracing",
   "Traces the off-screen bounce light with the GPU's ray tracing pipeline instead of a compute pass: the same lighting, about 2 ms faster on an RTX 5090.",
   Kind::Choice, Apply::Live, "May briefly pause the first time while the pipeline builds", false,
   {{"off", "Off", 0, {{"gi_rt_pipeline", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"gi_rt_pipeline", "1"}}, {}, {}, false, false, false, Apply::Live}},
   {}, {1, 1, 1, 1}, false, {{"rt","1","Ray-tracing support"}}, false, "Performance", {"Measured", "About -2 ms on the bounce light trace", "Under 1 MB", "RTX 5090, Ultra, live 2026-11-08: GI trace -2.0 ms at the inn (12.0 to 9.95 ms) and -2.1 ms at the camp; scene energy unchanged within noise."}, "Applies to Off-screen bounce light: without it this option does nothing. The first time, the pipeline builds for up to a minute while the usual trace keeps running. Without the GPU's ray tracing pipeline and reordering features the usual trace runs."},

  // Shader execution reordering on the raygen trace: on by default (user decision 2026-11-08, "Yes, turn it on"). Live: camp
  // -1.5 / -1.6 ms in two runs, inn -0.8 ms in one clean window; the committed two-scene rule was not met (inn unproven).
  {"exp_gi_rt_pipeline_reorder", "Bounce trace reordering",
   "Sorts the bounce rays by what they hit before shading them (shader execution reordering): about 1.5 ms faster at the camp and 0.8 ms at the inn on top of the faster bounce light tracing, same lighting.",
   Kind::Choice, Apply::Hitch, "May briefly pause when changed", false,
   {{"off", "Off", 0, {{"gi_rt_pipeline_reorder", "0"}}, {}, {}, false, false, false, Apply::Hitch},
    {"kind", "By hit kind", kCapRayQuery, {{"gi_rt_pipeline_reorder", "1"}}, {{"exp_gi_rt_pipeline", 1}}, {}, false, false, false, Apply::Hitch},
    {"object", "By hit kind and object", kCapRayQuery, {{"gi_rt_pipeline_reorder", "2"}}, {{"exp_gi_rt_pipeline", 1}}, {}, false, false, false, Apply::Hitch}},
   {}, {1, 1, 1, 1}, false, {{"rt","1","Ray-tracing support"}}, false, "Performance", {"Measured", "About -1.5 ms at the camp, -0.8 ms at the inn", "Unknown", "RTX 5090, Ultra, live 2026-11-08 on top of the ray tracing pipeline trace: camp -1.5 / -1.6 ms (two runs), inn -0.8 ms (one clean window); scene energy unchanged. Shipped on the user's decision; the inn result is one window."}, "Needs Faster bounce light tracing. By hit kind is the default; by hit kind and object is the alternative. Without the GPU's reordering support the rays are traced unsorted."},
  // Box shadows qualified for Ultra (user decision 2026-11-03). Experimental features
  // stay off in every preset; hair origins and spill are developer-only until qualified.
  {"box_lights", "Interior light shadows",
   "Adds shadows from rectangular interior lights that are meant to cast them. About 0.13 ms on an RTX 5090 at Grymforge.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"ls_box", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"all", "On", kCapRayQuery, {{"ls_box", "2"}, {"ls_box_honour_shadow", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 1}}, {}, false, false, false, Apply::Live},
    {"on", "Shadowed", kCapRayQuery, {{"ls_box", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 1}}, {}, true, false, false, Apply::Live}},
   {}, {0, 0, 0, 1}, false, {}, false, "Lighting", {"Measured", "About 0.13 ms", "Unknown", ""}, "Needs Local light shadows. Lights the game marks as not casting shadows stay unshadowed."},

  {"rr_post", "RR order",
   "Experimental: changes how fine hair edges appear through depth of field.",
   Kind::Choice, Apply::Live, "live", false,
   {{"before", "Before post (default)", 0, {{"rr_post", "1"}}, {}, {}, false, false, false, Apply::Live},
    {"after", "After post", kCapDlss, {{"rr_post", "0"}}, {{"rr", 1}}, {}, false, true, true, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "Moves RR before/after game post-processing. Dialogue3: thin hair against depth of field; compare dotted shimmer. Cost: unmeasured."},

  {"rr_post_temporal", "Depth of field and bloom smoothing",
   "Blends background blur and bloom across frames to smooth fine edges. About 0.15 ms on an RTX 5090 in dialogue.",
   Kind::Choice, Apply::Live, "Applies after saving.", false,
   {{"off", "Off", 0, {{"rr_post_temporal", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapDlss, {{"rr_post_temporal", "1"}}, {{"rr", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 1}, false, {{"rr_post", "1", "Ray Reconstruction before post effects"}}, false, "Surfaces", {"Measured", "About 0.15 ms", "250 MiB at 2560 × 1067", "250 MiB at 2560 × 1067 on RTX 5090 in dialogue."}, "Needs DLSS Ray Reconstruction. The effect is subtle; it is most visible in dialogue close-ups, on hair and fine edges against a blurred background."},

  {"exp_water_compose_shift", "Water camera correction",
   "Keeps water reflections on while the camera turns (traces the reflection scene in its own camera space).",
   Kind::Choice, Apply::Live, "live", false,
   {{"off", "Off", 0, {{"rt_water_compose_shift", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_water_compose_shift", "1"}}, {{"exp_water", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 1}, false, {}, true, "Surfaces", {"Measured", "+0.00 ms (compose 0.12 ms on and off) S8", "Unknown", "RTX 5090; wf9 2026-11-03: turning screen hits 7% -> 17%, hidden 25% -> 15.5%, static delta 0 m, energy within noise"}},

  {"exp_native_coverage", "Hair shadows",
   "Adds shadows and contact shading from the game's hair, including on faces and necks. About 2–6 ms in ray tracing and 3–9 ms in indirect lighting on an RTX 5090, depending on the scene.",
   Kind::Choice, Apply::Restart, "Restart to enable; disabling applies after saving.", false,
   {{"off", "Off", 0, {{"rt_native_hair", "0"}, {"rt_native_scalp", "0"}, {"rt_native_tileset", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"hair", "On", kCapRayQuery, {{"rt_native_hair", "1"}, {"rt_native_scalp", "1"}, {"rt_native_tileset", "0"}},
     {{"rt_geometry", 2}}, {"rt_native_hair", "rt_native_scalp"}, false, false, false, Apply::Restart},
    {"tilesets", "+ Tilesets", kCapRayQuery, {{"rt_native_hair", "1"}, {"rt_native_scalp", "1"}, {"rt_native_tileset", "1"}},
     {{"rt_geometry", 2}}, {"rt_native_tileset"}, true, false, false, Apply::Restart}},
   {}, {0, 0, 0, 1}, false, {{"rt","1","Ray-tracing support"},{"rt_alpha","1","Foliage capture"},{"rt_bvh_split","1","Scene cache"}}, false, "Lighting", {"Measured", "RT 2.3–6.3 ms; indirect-light tracing 3.2–8.8 ms", "Unknown", "Recorded RTX 5090 increases: 2.3–6.3 ms in the ray-tracing pass and 3.2–8.8 ms in indirect-light tracing, depending on scene."}, "Needs Ray-tracing detail: Include foliage. Turning it on needs a game restart, and the first start may pause while shaders compile. Visible hair stays the game's own hair."},

  {"exp_hair_groom", "RTX Hair in ray-traced lighting",
   "Experimental: uses individual strands for hair shadows and reflected light.",
   Kind::Choice, Apply::Live, "live; supported LSS hardware required; styles get strands once captured and converted", false,
   {{"off", "Off", 0, {{"rt_hair_groom", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery | kCapLinearSweptSpheres, {{"rt_hair_groom", "1"}}, {{"rt_geometry", 2}, {"exp_native_coverage", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, false, {{"rt","1","RT device setup"}}, true, "Advanced", {"Measured", "RT -1.1-1.5 ms; GI -1.6-1.8 ms; refit +0.57 ms", "Unknown", "RTX 5090; Dialogue1 versus native cards. Reference only; qualification incomplete."}, "Uses strands in RT shadows/AO/GI/reflections. Inspect hair shadows on faces and necks in Dialogue1."},

  {"exp_hair_groom_primary", "RTX Hair",
   "Beta: shows hair as individual ray-traced strands instead of flat cards. A style gets strands after the game has shown it once and the launcher has converted it. Turns on and off while playing.",
   Kind::Choice, Apply::Live, "Turns on and off while playing (also with its keyboard shortcut). If the game started with RTX Hair off, physical hair ray origins wait for a restart.", false,
   {{"off", "Off", 0, {{"rt_hair_groom_primary", "0"}, {"rt_hair_capture", "0"}}, {}, {}, false, false, false, Apply::Live},
    // Review 2026-11-08 (docs/RTX_HAIR_PRIMARY.md 5j): On is the one coherent strand path, the qualified bundle: header in
    // G-buffer order decided at submit (order, current), per-frame trace tags, LOD keep, coverage hold and the
    // world-space tangent. The individual keys stay as developer diagnostics in bg3rtx.ini only (order 0 and current 0
    // are known-broken off-paths). The mark migrates a file with only rt_hair_groom_primary=1 to the whole bundle.
    // RTX Hair single option (user approval 2026-11-08): On also writes every key of the user's tested set (strands in
    // RT, shadow/AO transmittance, no card shadows, physical fibre shading with GI/specular/mapped lights, RR hair
    // guides, physical hair ray origins). Tips, sub-pixel coverage, card normal and footprint bounds stay off.
    // 5m (docs/RTX_HAIR_PRIMARY.md, common/hair_live.h): live. The startup parts are prepared on every capable device
    // (strand header and pipeline variants with the world-space tangent compiled in, occlusionQueryPrecise, BC3
    // transfer-source usage for the capture). Only rt_hair_physical_origin stays a startup reservation (43.7 MB at
    // 2560 x 1067 and the GI's origin shader variants): 0 -> 1 needs a restart (the restart banner says so); its
    // other transitions and every other key are live. rt_hair=0 (hotkey_rtx_hair) makes this whole list read 0.
    // 5u (user decision 2026-11-10, live hs1 095322 and 103633): the union coverage count, the confirmed coverage
    // failure, the stable character slots and the warm-up after a live switch are part of On. Their own Advanced
    // knobs stay for A/B. rt_hair_coverage_min_pixels stays out (its prediction failed in 103633).
    // 5u (live hs1 133243): the nearest-strand trace (rt_hair_groom_nearest) too. It is compiled into the strand
    // pipelines at their creation, so a game started with RTX Hair off needs a restart for it (the old trace until then).
    {"on", "On", kCapRayQuery | kCapLinearSweptSpheres | kCapShaderInt64 | kCapFragmentStores,
     {{"rt_hair_groom_primary", "1"}, {"rt_hair_groom_primary_order", "1"}, {"rt_hair_groom_primary_current", "1"},
      {"rt_hair_primary_frame_tag", "1"}, {"rt_hair_groom_lod_keep", "1"}, {"rt_hair_primary_hold", "1"},
      {"rt_hair_tangent_world", "1"}, {"rt_hair_groom", "1"}, {"rt_hair_groom_shadow_t", "1"},
      {"rt_hair_groom_ao_t", "1"}, {"rt_hair_groom_cascade_drop", "1"}, {"rt_hair_fibre", "1"},
      {"rt_hair_fibre_gi", "1"}, {"rt_hair_fibre_spec", "1"}, {"rt_hair_fibre_mapped", "1"},
      {"rr_hair_guides", "1"}, {"rt_hair_physical_origin", "1"}, {"rt_hair_capture", "1"},
      {"rt_hair_coverage_union", "1"}, {"rt_hair_coverage_confirm", "1"}, {"rt_hair_slot_stable", "1"},
      {"rt_hair_live_warmup", "1"}, {"rt_hair_groom_nearest", "1"}},
     {{"exp_hair_groom", 1}}, {"rt_hair_groom_primary"}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, false, {{"rt","1","RT device setup"},{"sharc_device","1","64-bit shader integers at startup"}}, false, "Lighting", {}, "Requires linear swept spheres, shaderInt64 and fragment stores. Hair styles get strands after the game has shown them once and the launcher has converted them. Turns on all of RTX Hair: strands on screen and in ray-traced lighting (frame record decided at submit, per-frame trace tags, LOD keep, coverage hold, world-space tangent), strand shadow and AO transmittance, no card shadows, physical fibre shading (also indirect, specular and shadow-mapped lights), RR hair guides and physical hair ray origins. Cost remains unqualified. It turns on and off while playing, also with its keyboard shortcut (Ctrl+Shift+F7 by default); if the game started with RTX Hair off, the physical hair ray origins need a game restart."},

  {"exp_hair_fibre", "Physical strand shading",
   "Experimental: gives on-screen hair brighter, warmer shading.",
   Kind::Choice, Apply::Live, "live; needs Strand hair on screen", false,
   {{"off", "Off", 0, {{"rt_hair_fibre", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_fibre", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"}}, true, "Advanced", {}, "Uses a fibre BSDF with multiple scattering instead of the game hair shading. Cost and appearance remain unqualified."},

  {"exp_hair_fibre_gi", "Strand indirect scattering",
   "Uses the fibre volume's multiple-scattering reflectance for indirect light on visible strands. Experimental; cost and look unqualified.",
   Kind::Choice, Apply::Live, "live; needs Strand hair on screen and GI", false,
   {{"off", "Off", 0, {{"rt_hair_fibre_gi", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_fibre_gi", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"}}, true, "Developer", {}},

  {"exp_hair_fibre_spec", "Strand specular reflections",
   "Experimental: reflections and environment light on visible strands through the fibre's surface reflection instead of the game's hair term. Appearance and cost are unqualified.",
   Kind::Choice, Apply::Live, "live; needs Strand hair on screen and RT reflections", true,
   {{"off", "Off", 0, {{"rt_hair_fibre_spec", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_fibre_spec", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"}}, true, "Advanced", {}},

  {"exp_hair_fibre_mapped", "Strand shading for shadow-mapped lights",
   "Experimental: physical strand shading also for local lights that have a game shadow map, with ray-traced shadows through the strands instead of the card shadow map. Appearance and cost are unqualified.",
   Kind::Choice, Apply::Live, "live; needs Physical strand shading", true,
   {{"off", "Off", 0, {{"rt_hair_fibre_mapped", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_fibre_mapped", "1"}}, {{"exp_hair_fibre", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"}}, true, "Advanced", {}},

  {"exp_hair_groom_crowd","Strand hair crowd budget",
   "Experimental: gives nearby characters finer strands and reduces distant hair detail.",
   Kind::Choice, Apply::Live, "live", false,
   {{"off", "Off", 0, {{"rt_hair_groom_crowd_ksegments", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"large", "3.5 million segments", kCapRayQuery, {{"rt_hair_groom_crowd_ksegments", "3500"}}, {{"exp_hair_groom", 1}}, {}, false, false, false, Apply::Live},
    {"small", "1.8 million segments", kCapRayQuery, {{"rt_hair_groom_crowd_ksegments", "1800"}}, {{"exp_hair_groom", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"}}, true, "Advanced", {}, "Budgets total groom segments, not characters. Approximately 4 or 2 close characters respectively; actual counts depend on groom size. Refit and memory scale with the budget."},

  {"exp_hair_groom_shadow_t", "Strand shadow transmittance",
   "Experimental: shadow rays through strand hair pass with the fibre's forward-scattered light per crossing instead of being blocked. Appearance and cost are unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_groom_shadow_t", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_groom_shadow_t", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Developer", {}},

  {"exp_hair_groom_ao_t", "Strand AO transmittance",
   "Experimental: ambient occlusion and indirect-light rays through strand hair pass with the fibre's forward-scattered light per crossing, as the strand shadows do, instead of stopping at the first strand. Appearance and cost are unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_groom_ao_t", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_groom_ao_t", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}},

  {"exp_hair_primary_passes", "Strand passes that swap",
   "Diagnostic for on-screen strands: which passes may replace the cards (G-buffer, motion vectors, depth prepass). Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"all", "All", 0, {{"rt_hair_primary_passes", "15"}}, {}, {}, false, false, false, Apply::Live},
    {"gbuffer", "G-buffer and probe", kCapRayQuery, {{"rt_hair_primary_passes", "9"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live},
    {"gbuffer_velocity", "G-buffer, motion vectors and probe", kCapRayQuery, {{"rt_hair_primary_passes", "11"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live},
    {"no_probe", "Swaps without the probe", kCapRayQuery, {{"rt_hair_primary_passes", "7"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live},
    {"none", "No strand pass", kCapRayQuery, {{"rt_hair_primary_passes", "0"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Developer", {}},

  {"exp_hair_groom_tips", "Dithered strand tips",
   "Experimental: fades on-screen strand tips with the game mask alpha and temporal dither. Appearance and cost are unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_groom_tips", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_groom_tips", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Developer", {}},

  {"exp_hair_groom_coverage", "Strand sub-pixel coverage",
   "Experimental: two camera rays per on-screen strand pixel instead of one, so distant sub-pixel strands alias less. Appearance and cost are unqualified.",
   Kind::Choice, Apply::Restart, "restart to enable; switches off live", true,
   {{"off", "Off", 0, {{"rt_hair_groom_coverage", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_groom_coverage", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Restart}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "M3.2: compiled into the strand pipelines at their creation. Doubles the strand trace on strand candidate pixels. Compare thin strands against the sky or a bright wall in Test Dialogue1 while turning."},

  {"exp_hair_groom_nearest", "Strand hair: nearest-strand trace",
   "Experimental: each strand pixel finds the nearest strand of its own hair style in one pass, past any number of strands of a hair style still drawn as cards, instead of giving up after three (holes in other characters' hair while a new character's hair is being prepared). Unqualified.",
   Kind::Choice, Apply::Restart, "restart to enable; switches off live", true,
   {{"off", "Off", 0, {{"rt_hair_groom_nearest", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_groom_nearest", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Restart}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5u 2026-11-10: compiled into the strand pipelines at their creation. In S8 with the camera still, the hair of the others keeps its strands when a character appears; the frame watchdog line stays at 0 trips."},

  {"exp_hair_macro_normal", "Strand hair: card normal",
   "Experimental: on-screen strands give the game's hair lighting the hair card's own normal (the hair volume's surface) instead of a normal facing the camera. Appearance is unqualified.",
   Kind::Choice, Apply::Restart, "restart to enable; switches live after", true,
   {{"off", "Off", 0, {{"rt_hair_macro_normal", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_macro_normal", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Restart}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5g: compiled into the strand pipelines at their creation; no extra cost. Compare the hair's shading and strand definition in Test Dialogue1 (on/off/on)."},

  {"exp_hair_groom_cascade_drop", "Strand hair: no card shadows",
   "Experimental: hair whose strands are in the ray-traced scene stops casting its card silhouette into the game's sun shadow maps. Where the game's shadow is still used, that hair casts no shadow. Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_groom_cascade_drop", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_groom_cascade_drop", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "M2.2: removes ribbon and double shadows under roofs. Compare the neck and shoulders under hair in Test Dialogue1, then the ground in Campsite."},

  {"exp_hair_header_cam_gpu", "Strand hair: GPU build camera",
   "Experimental: the strands' camera origin is copied on the GPU from the same camera data the strand points were built with, instead of a CPU read taken earlier. Removes strands flickering to cards while the camera turns. Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_header_cam_gpu", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_header_cam_gpu", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5i item 11: turn the camera in Test Dialogue1 or S8 while watching the hair: with On the strands should stay strands instead of flashing to cards."},

  {"exp_hair_watchdog_draw", "Strand hair: per-draw watchdog",
   "Experimental: when a hair style's strands lose coverage, only that style falls back to its cards, and only after two frames in a row, instead of every style for one bad frame. Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_watchdog_draw", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_watchdog_draw", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5i item 11: turn the camera in S8 or Test Dialogue1 with several characters in view: with On a hair style that flashes to cards no longer takes the others with it."},

  {"exp_hair_shared_key", "Strand hair: shared style key",
   "Experimental: when two characters wear the same hair style, their hair turns to strands only when both have their strands ready, instead of one character's hair losing its cards with nothing drawn in their place. Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_shared_key", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_shared_key", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5i 2026-11-09: two characters with the same hair style in view (S8), turn the camera: with On their hair should no longer flash to cards."},

  {"exp_hair_row_stable", "Strand hair: stable strand rows",
   "Experimental: each character's strands keep their place in the strand tables while the camera moves or a new character appears, instead of being reordered every frame (holes in every character's hair for a few frames). Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_row_stable", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_row_stable", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5w 2026-11-10: several characters in view (S8), zoom with the wheel: with On their hair should no longer show holes at the start of the zoom."},

  {"exp_hair_slot_stable", "Strand hair: stable character slots",
   "Experimental: when two characters wear the same hair style, each keeps its own strands when the game changes the order it draws them in, instead of the two swapping for a frame (a one-frame hole in the hair). Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_slot_stable", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_slot_stable", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5s 2026-11-10: two characters with the same hair style in view (S8), camera still: with On the one-frame holes in their hair should stop."},

  {"exp_hair_watchdog_one_key", "Strand hair: one-style watchdog",
   "Experimental: when one hair style loses its strands in a frame while the others keep theirs, only that style falls back to its cards instead of every style. Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_watchdog_one_key", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_watchdog_one_key", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5i 2026-11-09: turn the camera in S8 with several characters in view: with On one style that flashes to cards no longer takes the others with it."},

  {"exp_hair_swap_latch", "Strand hair: no strand retries",
   "Experimental: a hair style whose strands failed the coverage check keeps its cards until its size on screen changes, instead of trying the strands again a moment later (hair that flickers between strands and cards with a still camera). Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_swap_latch", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_swap_latch", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5t 2026-11-10: still camera, characters at a distance whose hair flickers between strands and cards: with On each hair settles on strands or on cards and stays there until the camera or the character moves."},

  {"exp_hair_coverage_union", "Strand hair: coverage check on every strand pixel",
   "Experimental: the check that decides between strands and cards counts every strand pixel, as its limit was derived, instead of only the strand pixels that land on an opaque card pixel (which drops for small, distant characters and sends their hair back to cards). Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_coverage_union", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_coverage_union", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5u 2026-11-10: still camera, characters at a distance whose hair flickers between strands and cards: with On distant hair should stay on strands; the log line '[hair primary] coverage by footprint' shows the judged ratio flat over the card-pixel bins."},

  {"exp_hair_coverage_confirm", "Strand hair: confirm a failed coverage check",
   "Experimental: hair already shown as strands goes back to cards only when its coverage check fails twice in a row, not on one bad frame (one-frame card flashes several times a second with a still camera). Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_coverage_confirm", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_coverage_confirm", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5u 2026-11-10: S8, still camera: with On the one-frame card flashes stop; the '[hair primary] switches' line shows card runs of 1 frame gone."},

  {"exp_hair_coverage_min_pixels", "Strand hair: no coverage check on tiny hair",
   "Experimental: hair drawn on fewer than 64 pixels is not measured (its count says nothing), so tiny distant hair stays on cards steadily instead of trying strands every half second. Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_coverage_min_pixels", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_coverage_min_pixels", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5u audit 2026-11-10: S8, still camera, very distant characters: with On their hair stays cards without switching; the switches line shows no 'coverage stale' switches."},

  {"exp_hair_live_warmup", "Strand hair: warm-up after turning RTX Hair on",
   "Experimental: after RTX Hair is turned on while playing, the hair keeps its cards until every hair style is rebuilt and settled, as after a level load, instead of showing strands while they are still being rebuilt. Unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rt_hair_live_warmup", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_live_warmup", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5u audit 2026-11-10: turn RTX Hair off and on (Ctrl+Shift+F7): with On the hair returns as strands once, without a burst of card flashes a second later."},

  {"exp_rr_hair_guides", "RR hair guides",
   "Experimental: Ray Reconstruction treats hair (cards and strands) as a dielectric fibre instead of reading its backlit value as metalness. Appearance is unqualified.",
   Kind::Choice, Apply::Live, "live", true,
   {{"off", "Off", 0, {{"rr_hair_guides", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapDlss, {{"rr_hair_guides", "1"}}, {{"rr", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "M3.3: hair diffuse guide = base colour, specular guide from the fibre F0 0.0465, roughness >= 0.09. Compare hair highlights and hair noise in Test Dialogue1."},

  {"exp_hair_strand_aa", "Strand hair edge resolve",
   "Experimental: how on-screen strand edges enter Ray Reconstruction. The game's hair dither resolve no longer mixes strands by the hair cards' dither pattern; with analytic coverage, thin strands at the edges are blended with their background by their exact pixel coverage instead of flickering. Appearance and cost are unqualified.",
   Kind::Choice, Apply::Live, "live; needs RTX Hair and RR before post", true,
   {{"off", "Off", 0, {{"rr_hair_strand_aa", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"untag", "Strands unresolved", kCapDlss, {{"rr_hair_strand_aa", "1"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live},
    {"coverage", "Analytic coverage", kCapDlss, {{"rr_hair_strand_aa", "2"}}, {{"exp_hair_groom_primary", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {}, true, "Advanced", {}, "5n: compare strand edges against the background in Test Dialogue1 (hair silhouette, flyaway strands): dithering at the edges should be gone with Analytic coverage."},
  {"exp_hair_pool_trim", "Retire unused hair snapshots",
   "Reclaims unused hair capture memory.",
   Kind::Choice, Apply::Live, "live", false,
   {{"off", "Off", 0, {{"rt_hair_pool_trim", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_pool_trim", "1"}}, {{"exp_native_coverage", 1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 1}, false, {}, true, "Performance", {"Measured", "Not measured", "Mesh pool -26-50%", "RTX 5090; hair_qual 2026-11-03"}, "Reclaims and packs unused native hair snapshots. Mesh pool reduction of 26-50% in the reference scenes; no numeric GPU timing available."},

  {"exp_hair_physical_origin", "Physical hair ray origins (advanced)",
   "Adjusts where light rays start on hair to improve secondary lighting. Quality and cost remain unqualified.",
   Kind::Choice, Apply::Restart, "Enable at startup to reserve ray-origin resources; then live A/B is available.", true,
   {{"off", "Off", 0, {{"rt_hair_physical_origin", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"on", "On", kCapRayQuery, {{"rt_hair_physical_origin", "1"}}, {{"rt_geometry", 2}, {"exp_native_coverage",1}}, {}, false, false, false, Apply::Restart}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_native_hair","1","Native hair capture startup"},{"rt_origin","1","Primary origin matching"}}, true, "Developer", {}},

  {"detailed_logging", "Detailed logging",
   "Writes extra performance and diagnostic details to the log for bug reports. Leave it off for normal play.",
   Kind::Choice, Apply::Restart, "Restart the game to apply", false,
   {{"off", "Off", 0, {{"detailed_logging", "0"}}, {}, {}, false, false, false, Apply::Restart},
    {"on", "On", 0, {{"detailed_logging", "1"}}, {}, {}, false, false, false, Apply::Restart}},
   {}, {0, 0, 0, 0}, false, {}, false, "Support", {}},

  // Developer only (dev=1): debug references and options parked by user decision. Their keys keep working.
  {"exp_hit_reference", "Experimental lighting reference",
   "Shows a limited reference using point and spot lights on supported opaque surfaces. "
   "Two surfaces adds one indirect bounce; incomplete paths retain direct-only lighting and are counted. "
   "Requires Hidden-surface shading at startup. No sunlight, sky, emission or full-scene quality claim. "
   "This replaces the display for inspection; it is not a gameplay quality preset.",
   Kind::Choice, Apply::Live, "live; native materials need startup", false,
   {{"off", "Off", 0, {{"rt_hit_reference", "0"}}, {}, {}, false, false, false, Apply::Live},
    {"direct", "Direct only", kCapRayQuery, {{"rt_hit_reference", "1"}}, {{"rt_geometry", 2}, {"exp_hidden_surface",1}}, {}, false, false, false, Apply::Live},
    {"two", "Two surfaces", kCapRayQuery, {{"rt_hit_reference", "2"}}, {{"rt_geometry", 2}, {"exp_hidden_surface",1}}, {}, false, false, false, Apply::Live}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_hit_material","1","Native material startup"}}, true, "Developer", {}},

  {"exp_ls_exact_k", "Local light sampling (exact-k)",
   "Cost option for interiors with many lights: the strongest 1 or 2 lights of each pixel keep their own "
   "shadow ray, and one ray is shared by the rest, picked in proportion to their light and weighted so the "
   "mean brightness is unchanged. Needs Local light shadows Full. Qualified k1 is automatic in Ultra; developer override only.",
   Kind::Choice, Apply::Hitch, "First use may compile a core pipeline; no game restart.", false,
   {{"off", "Off", 0, {{"ls_k", "0"}, {"ls_rest_samples", "1"}}, {}, {}, false, false, false, Apply::Live},
    {"k1", "1 exact light", kCapRayQuery, {{"ls_k", "1"}, {"ls_rest_samples", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 2}}, {}, false, false, false, Apply::Hitch},
    {"k2", "2 exact lights", kCapRayQuery, {{"ls_k", "2"}, {"ls_rest_samples", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 2}}, {}, false, false, false, Apply::Hitch}},
   {}, {0, 0, 0, 1}, false, {}, true, "Developer", {"Measured", "0.717 -> 0.555 ms (-23%) at S7 inn", "Unknown", "RTX 5090; k1 scene energy -0.38/-0.34/+0.12%, within OFF spread 0.89%. Ultra approved 2026-11-04; lower presets unqualified."}},

};
// clang-format on
inline constexpr int kKnobCount = int(sizeof kKnobs / sizeof kKnobs[0]);

// Knob ids before the 2026-11-01 GUI rationalization (docs/GUI_AUDIT_20261101.md) and the knob and
// choice their "On" became. find_knob() resolves them; their keys are read back by the new knob
// (Choice::mark), so a bg3rtx_user.ini written before still loads as the matching level.
struct KnobAlias {
  const char* old_id;
  const char* knob;
  int on_choice;
};
inline constexpr KnobAlias kKnobAliases[] = {
    {"exp_roof_shadows", "sun_shadows", 2},
    {"roof_shadows", "sun_shadows", 2},
    {"exp_box_lights", "box_lights", 2},
    {"exp_hit_emission", "exp_hidden_surface", 1},
    {"exp_gi_far", "exp_hidden_surface", 2},
    {"exp_hit_material", "exp_hidden_surface", 1},
    {"exp_hit_material_instanced", "exp_hidden_surface", 1},
    {"exp_hit_material_recycle", "exp_hidden_surface", 1},
    {"exp_hit_material_wind_rgb", "exp_hidden_surface", 1},
    {"exp_gi_hit_direct", "exp_hidden_surface", 1},
    {"exp_gi_hit_sun", "exp_hidden_surface", 1},
    {"exp_rf_hit_shade", "exp_hidden_surface", 3},
    {"exp_rf_hit_gi", "exp_hidden_surface", 3},
    {"exp_water_geometry", "exp_water", 1},
    {"exp_water_reflections", "exp_water", 1},
    {"exp_water_compose", "exp_water", 1},
    {"exp_water_shadows", "exp_water", 2},
    {"exp_water_ls_shadows", "exp_water", 2},
    {"exp_water_game_shadow", "exp_water", 2},
    {"exp_water_refraction", "exp_water", 3},
    {"exp_native_scalp", "exp_native_coverage", 1},
    {"exp_native_hair", "exp_native_coverage", 1},
    {"exp_native_tileset", "exp_native_coverage", 2},
};

// Fixes that passed their gates and are on by default without a GUI entry (keys kept for A/B):
// the hit-material provider's buffer reuse, unlocked captures and lean per-draw hooks (scene_wait sw3
// 2026-11-01 22:13: 0 scene-wait timeouts in every pan and rotation, hooks ~2 ms instead of 5-6);
// fg_letterbox, a no-op unless the swapchain is larger than the game's output (common/fg_extent.h;
// smoke_ultra su1 2026-11-02 01:08 with it on: every-window gates ok, FG 2.00).
// VRAM guard, cache compaction and elastic captures: vg3 S8 real-budget OFF/ON/OFF passed.
inline constexpr const char* kAutomaticKeys[] = {"rt_hit_material_recycle", "rt_hit_material_capture_unlocked",
                                                 "rt_hit_material_lean_hooks", "fg_letterbox",
                                                 "rt_vram_guard", "rt_vram_compact", "rt_capture_elastic"};

// Internal keys a preset sets besides its knobs (never shown in a UI).
// Ultra: rare forced BVH rebuilds (SETTINGS_DESIGN 5.4 #4). With the BVH split
// (rt_bvh_split=1, TRACK_D_RT.md 11) rt_rebuild_frames only paces the per-draw
// BLAS of the captured (skinned, wind, per-draw matrix) draws, staggered per draw;
// static meshes are built once for trace speed and never refit.
inline constexpr KV kPresetExtra[kPresetCount][5] = {
  {{"rt_scene_compact_holds", "1"}, {"rt_compact_hit_state", "1"}, {"gi_far_roof_early", "1"}, {"rf_compact_probe_state", "1"}},
  {{"rt_scene_compact_holds", "1"}, {"rt_compact_hit_state", "1"}, {"gi_far_roof_early", "1"}, {"rf_compact_probe_state", "1"}},
  {{"rt_scene_compact_holds", "1"}, {"rt_compact_hit_state", "1"}, {"gi_far_roof_early", "1"}, {"rf_compact_probe_state", "1"}},
  {{"rt_rebuild_frames", "600"}, {"rt_scene_compact_holds", "1"}, {"rt_compact_hit_state", "1"}, {"gi_far_roof_early", "1"}, {"rf_compact_probe_state", "1"}}};

// ---- defaults: one source (docs/SETTINGS_DESIGN.md "Defaults: one source") ----
// kDefaults is every key bg3rtx.ini sets, with its value; kReleaseDefaults the developer
// keys data/release.ini switches off. tests/settings_test.cpp asserts that both files and
// both tables agree exactly. The layer's cfg_int/cfg_float (layer/util.cpp) use
// code_default() for a key no ini layer has: kDefaults with the release profile on top, so
// a missing developer key is off. A call site's fallback literal is used only for keys in
// neither table, and the layer logs once when a literal disagrees with the table.
// clang-format off
// BEGIN GENERATED DEFAULTS (scripts/gen_settings_defaults.py; edit the ini files, then run it)
// The keys of bg3rtx.ini, in file order.
inline constexpr KV kDefaults[] = {
    {"dev", "1"},
    {"detailed_logging", "0"},
    {"ab_hotkey", "1"},
    {"ab_off", "0"},
    {"experimental_options", "0"},
    {"dlss5_nr", "0"},
    {"dlss5_nr_runtime", ""},
    {"dlss5_nr_bypass", "0"},
    {"hotkey_rt_toggle", "Ctrl+Shift+F9"},
    {"hotkey_dlss5", "Ctrl+Shift+F8"},
    {"hotkey_rtx_hair", "Ctrl+Shift+F7"},
    {"game_dlss_guard", "1"},
    {"core", "1"},
    {"dump_shaders", "1"},
    {"shader_debug", "0"},
    {"census_dump", "0"},
    {"rr", "1"},
    {"rr_preset", "6"},
    {"rr_post", "1"},
    {"rr_post_temporal", "0"},
    {"rr_cbt", "1"},
    {"rr_post_quantise", "1"},
    {"rr_post_check", "0"},
    {"rr_test_fail", "0"},
    {"rr_show", "0"},
    {"rr_guides_v2", "1"},
    {"rr_nov", "0.5"},
    {"rr_hair_guides", "0"},
    {"rr_hair_strand_aa", "0"},
    {"rr_hair_strand_snap_site", "0"},
    {"rr_guides_at", "1"},
    {"rr_dump_gbuffer", "0"},
    {"dump_raw", "0"},
    {"dump_preview_f32", "0"},
    {"ngx_hook", "1"},
    {"gi", "1"},
    {"gi_rays", "4"},
    {"gi_rr_rays", "1"},
    {"gi_steps", "16"},
    {"gi_radius", "0.15"},
    {"gi_thickness", "0.03"},
    {"gi_strength", "1.0"},
    {"gi_ao", "1.0"},
    {"gi_history", "0.1"},
    {"gi_debug", "0"},
    {"geo_capture", "0"},
    {"geo_timing", "0"},
    {"rt", "1"},
    {"rt_build", "auto"},
    {"rt_refit", "1"},
    {"rt_rebuild_frames", "60"},
    {"rt_bvh_split", "1"},
    {"rt_tlas_refit", "1"},
    {"rt_mesh_compact", "1"},
    {"rt_mesh_new_tris", "4000000"},
    {"rt_mesh_budget_mb", "2048"},
    {"rt_mesh_budget_own", "0"},
    {"rt_mesh_audits", "2"},
    {"rt_mesh_audit_frames", "600"},
    {"rt_mesh_content_id", "0"},
    {"rt_vram_guard", "1"},
    {"rt_vram_guard_budget_mb", "0"},
    {"rt_vram_compact", "1"},
    {"rt_capture_elastic", "1"},
    {"rt_capture_rise_reserve", "1"},
    {"rt_as_chunk_reuse_frames", "120"},
    {"rt_as_cap_occupied", "0"},
    {"rt_shadows", "1"},
    {"contact_shadows", "1"},
    {"rt_strength", "1.0"},
    {"rt_origin", "1"},
    {"rt_bias", "0.002"},
    {"rt_backlit", "1"},
    {"rt_tmax", "300"},
    {"rt_sun_radius", "0.00459"},
    {"rt_sun_replace", "1"},
    {"rt_sun_roofs", "1"},
    {"rt_sun_roof_mode", "0"},
    {"rt_sun_roof_pad", "3.0"},
    {"rt_sun_roof_up", "50.0"},
    {"rt_sun_roof_radius", "0"},
    {"rt_sun_offscreen", "1"},
    {"rt_sun_rise", "6.0"},
    {"rt_sun_margin", "0.0"},
    {"rt_sun_ramp", "0.75"},
    {"rt_sun_near", "0"},
    {"rt_cascade_casters", "0"},
    {"rt_cascade_index", "2"},
    {"rt_cascade_mb", "160"},
    {"rt_cascade_origin", "1"},
    {"rt_cascade_alpha", "2"},
    {"rt_cascade_alpha_dedupe", "1"},
    {"rt_cascade_refit", "1"},
    {"rt_cascade_rebuild_frames", "60"},
    {"rt_debug", "0"},
    {"rt_stats", "0"},
    {"rt_occ_stats", "0"},
    {"rt_occ_stats_frames", "60"},
    {"ls_box_cascade_old", "0"},
    {"pan_capture", "0"},
    {"pan_water", "0"},
    {"pan_edge", "0"},
    {"pan_post", "0"},
    {"fg_snap_full", "0"},
    {"fg_snap", "0"},
    {"fg_snap_any", "0"},
    {"fg_log_presents", "6"},
    {"fg_log_camera", "0"},
    {"sl_log_barriers", "0"},
    {"sl_verbose", "0"},
    {"sl_dev", "0"},
    {"log_presents", "0"},
    {"log_procs", "0"},
    {"fg_only_interpolated", "0"},
    {"pan_water_frames", "8"},
    {"pan_delay_ms", "0"},
    {"pan_prepare", "0"},
    {"pan_inputs", "0"},
    {"replay_pack", "0"},
    {"replay_pack_pass", "0"},
    {"replay_pack_cap_mb", "2048"},
    {"fg_input_copy", "1"},
    {"sl_init_after_create", "1"},
    {"qa_zero", "0"},
    {"qa_exposure_hold", "0"},
    {"rt_max_candidates", "0"},
    {"rt_emissive", "0"},
    {"rt_em_overlap_check", "0"},
    {"rt_em_max_lights", "65536"},
    {"rt_em_xfb_mb", "32"},
    {"rt_em_xfb_max_mb", "512"},
    {"rt_em_calib", "1"},
    {"rt_em_check_frames", "16"},
    {"rt_em_check_slices", "16"},
    {"rt_em_lava_heat", "0.5"},
    {"rt_em_log", "0"},
    {"rt_em_dbg_fam_hi", "0"},
    {"rt_em_dbg_fam_lo", "0"},
    {"rt_em_tol", "0.01"},
    {"rt_em_fx_thr", "0.05"},
    {"rt_em_backdrop", "0"},
    {"rt_em_decal", "1"},
    {"rt_em_fx_pulse", "0"},
    {"rt_hit_emission", "0"},
    {"rt_water_geometry", "0"},
    {"rt_water_xfb_mb", "32"},
    {"rt_water_xfb_max_mb", "256"},
    {"rt_water_reflections", "0"},
    {"rt_water_compose_shift", "0"},
    {"rt_water_surface_fresnel", "0"},
    {"rt_water_compose", "0"},
    {"rt_water_shadows", "0"},
    {"rt_water_ls_shadows", "0"},
    {"rt_water_refraction", "0"},
    {"rt_water_game_shadow", "0"},
    {"rt_water_compose_mb", "160"},
    {"rt_alpha", "1"},
    {"rt_alpha_test", "1"},
    {"rt_alpha_cut", "1"},
    {"rt_alpha_stochastic", "1"},
    {"rt_native_scalp", "0"},
    {"rt_native_tileset", "0"},
    {"rt_hit_material", "0"},
    {"rt_hit_material_instanced", "0"},
    {"rt_hit_material_recycle", "1"},
    {"rt_hit_material_capture_unlocked", "1"},
    {"rt_hit_material_lean_hooks", "1"},
    {"perf_hitmat_lists", "0"},
    {"perf_hook_state", "0"},
    {"cpu_opt1", "1"},
    {"cpu_opt2", "1"},
    {"cpu_opt3", "1"},
    {"rt_hit_material_wind_rgb", "0"},
    {"rt_hit_material_mask", "0"},
    {"rt_hit_material_pool_alpha", "1"},
    {"rt_hit_reference", "0"},
    {"rt_omm", "1"},
    {"rt_omm_reference", "0"},
    {"rt_omm_dyn", "1"},
    {"rt_omm_dyn_bake_tris", "131072"},
    {"rt_omm_dyn_mb", "256"},
    {"rt_omm_dyn_retry_frames", "64"},
    {"rt_omm_dyn_evict_frames", "0"},
    {"rt_native_hair", "0"},
    {"rt_hair_pool_trim", "0"},
    {"rt_scene_compact_holds", "1"},
    {"rt_hair_groom", "0"},
    {"rt_hair_capture", "0"},
    {"rt_hair_groom_primary", "0"},
    {"rt_hair", "1"},
    {"rt_hair_groom_shadow_t", "0"},
    {"rt_hair_groom_ao_t", "0"},
    {"rt_hair_groom_primary_order", "0"},
    {"rt_hair_groom_primary_current", "0"},
    {"rt_hair_primary_passes", "15"},
    {"rt_hair_primary_hold", "0"},
    {"rt_hair_coverage_footprint", "0"},
    {"rt_hair_groom_tips", "0"},
    {"rt_hair_groom_coverage", "0"},
    {"rt_hair_groom_nearest", "0"},
    {"rt_hair_pixel_lod", "0"},
    {"rt_hair_pixel_lod_k", "1.0"},
    {"rt_hair_primary_frame_tag", "0"},
    {"rt_hair_header_cam_gpu", "0"},
    {"rt_hair_mv_card", "0"},
    {"rt_hair_watchdog_draw", "0"},
    {"rt_hair_shared_key", "0"},
    {"rt_hair_slot_stable", "0"},
    {"rt_hair_row_stable", "0"},
    {"rt_hair_watchdog_one_key", "0"},
    {"rt_hair_swap_latch", "0"},
    {"rt_hair_coverage_union", "0"},
    {"rt_hair_coverage_confirm", "0"},
    {"rt_hair_coverage_min_pixels", "0"},
    {"rt_hair_live_warmup", "0"},
    {"rt_hair_debug_late_arrival", "0"},
    {"rt_hair_groom_lod_keep", "0"},
    {"rt_hair_warmup", "1"},
    {"rt_hair_macro_normal", "0"},
    {"rt_hair_tangent_world", "0"},
    {"rt_hair_groom_cascade_drop", "0"},
    {"rt_hair_fibre", "0"},
    {"rt_hair_fibre_gi", "0"},
    {"rt_hair_fibre_spec", "0"},
    {"rt_hair_fibre_mapped", "0"},
    {"rt_hair_groom_crowd_ksegments", "0"},
    {"rt_hair_physical_origin", "0"},
    {"rt_hair_receiver_guard", "0"},
    {"rt_alpha_unknown", "1"},
    {"rt_alpha_auto", "1"},
    {"rt_vt_alpha_masks", "1"},
    {"rt_alpha_fine_lod", "1"},
    {"rt_vt_alpha_miss_log", "0"},
    {"rt_alpha_max_pct", "60"},
    {"rt_alpha_floor_yield", "0"},
    {"rt_capture_wide_split", "1"},
    {"rt_strips", "1"},
    {"gi_rt", "1"},
    {"gi_rt_radius", "1.0"},
    {"rt_compact_hit_state", "1"},
    {"rf_compact_probe_state", "1"},
    {"gi_rt_depth_k", "1.0"},
    {"gi_rt_fallback", "0"},
    {"gi_hit_direct", "0"},
    {"gi_hit_sun", "0"},
    {"gi_far", "0"},
    {"gi_far_m", "6"},
    {"gi_rt_pipeline", "1"},
    {"gi_rt_pipeline_reorder", "1"},
    {"gi_far_roof_early", "1"},
    {"sharc", "0"},
    {"sharc_log2", "22"},
    {"sharc_inject_stride", "2"},
    {"sharc_frames", "16"},
    {"sharc_stale", "128"},
    {"sharc_scene_scale", "50"},
    {"sharc_log_base", "2"},
    {"sharc_level_bias", "0"},
    {"sharc_query", "7"},
    {"sharc_gi_radius", "4"},
    {"sharc_rf_rough", "0.2"},
    {"sharc_rf_cone", "1"},
    {"sharc_near", "1"},
    {"sharc_em", "1"},
    {"sharc_reset_jump", "50"},
    {"sharc_reject_occluded", "1"},
    {"sharc_leak_check", "0"},
    {"sharc_fine_check", "0"},
    {"sharc_fine_scale", "4"},
    {"sharc_debug", "0"},
    {"sharc_stats", "0"},
    {"sharc_stats_frames", "300"},
    {"sharc_device", "1"},
    {"rt_local_shadows", "1"},
    {"ls_shadowed", "3"},
    {"ls_source_radius", "0.05"},
    {"ls_soft", "-1"},
    {"ls_clear", "0.2"},
    {"ls_min_radius", "0"},
    {"ls_samples", "0"},
    {"ls_k", "0"},
    {"ls_rest_samples", "1"},
    {"ls_strength", "1.0"},
    {"ls_debug", "0"},
    {"ls_stats", "0"},
    {"ls_box", "0"},
    {"ls_box_honour_shadow", "1"},
    {"ls_native_direct_validate", "0"},
    {"ls_lights", "1"},
    {"ls_owners", "31"},
    {"ls_noshadow", "1"},
    {"ls_giveback_cap", "1"},
    {"ls_lights_radius", "80"},
    {"ls_dyn_speed", "8"},
    {"ls_impl", "1"},
    {"ls_check", "0"},
    {"se_bridge", "1"},
    {"se_stale_periods", "4"},
    {"se_stale_ms", "0"},
    {"mat_classes", "1"},
    {"ao_direct", "1"},
    {"rt_reflections", "1"},
    {"rf_max_rough", "0.6"},
    {"rf_max_rough_norr", "0.3"},
    {"rf_mirror_rough", "0.1"},
    {"rf_rr", "-1"},
    {"rf_tmax", "100"},
    {"rf_depth_k", "1.0"},
    {"rf_edge", "0.05"},
    {"rf_strength", "1.0"},
    {"rf_models", "3"},
    {"rf_debug", "0"},
    {"rf_stats", "0"},
    {"rf_validate", "0"},
    {"rf_hit_shade", "0"},
    {"rf_hit_gi", "0"},
    {"hit_ls_policy", "1"},
    {"hit_ao_rays", "2"},
    {"rr_spec_hit_distance", "1"},
    {"fg", "2"},
    {"reflex", "1"},
    {"fps_cap", "0"},
    {"se_unhook", "1"},
    {"rt_order_trace", "0"},
    {"rt_scene_wait", "1"},
    {"rt_scene_handout_layout", "0"},
    {"rt_scene_handout", "0"},
    {"cpu_timing", "0"},
    {"perf_submit_batch", "0"},
    {"rt_alpha_stream", "1"},
    {"rt_stability_log", "0"},
    {"rt_capture_state_log", "0"},
    {"rt_mesh_draw_log", "0"},
    {"rt_mesh_capture", "0"},
    {"rt_capture_discard", "1"},
    {"rt_dyn_hold", "4"},
    {"rt_dyn_hold_max_prims", "8192"},
    {"rt_scene_wait_ms", "12"},
    {"rt_scene_wait_log_frames", "600"},
    {"rt_primary_check", "0"},
    {"rt_primary_epoch", "0"},
    {"rt_primary_identity", "0"},
    {"rt_primary_fade_class", "0"},
    {"crash_dump", "0"},
    {"fg_current_destroy_only", "1"},
    {"fg_destroy_wait_exact", "0"},
    {"fg_present_metering", "0"},
    {"fg_tag_hudless", "1"},
    {"fg_tag_ui", "1"},
    {"fg_letterbox", "1"},
    {"fg_ui_pq", "0"},
    {"rt_stats_period_frames", "0"},
    {"rt_stats_reset", "0"},
};
// The keys of data/release.ini, in file order.
inline constexpr KV kReleaseDefaults[] = {
    {"dev", "0"},
    {"detailed_logging", "0"},
    {"dump_shaders", "0"},
    {"shader_debug", "0"},
    {"census_dump", "0"},
    {"geo_capture", "0"},
    {"geo_timing", "0"},
    {"rr_show", "0"},
    {"rr_dump_gbuffer", "0"},
    {"gi_debug", "0"},
    {"rt_debug", "0"},
    {"rt_stats", "0"},
    {"ls_debug", "0"},
    {"ls_stats", "0"},
    {"ls_native_direct_validate", "0"},
    {"rf_debug", "0"},
    {"rf_stats", "0"},
    {"rf_validate", "0"},
    {"rr_post_check", "0"},
    {"dump_raw", "0"},
    {"rr_test_fail", "0"},
    {"rt_em_overlap_check", "0"},
    {"rt_primary_check", "0"},
    {"rt_primary_epoch", "0"},
    {"rt_primary_identity", "0"},
    {"rt_primary_fade_class", "0"},
    {"cpu_timing", "0"},
    {"crash_dump", "0"},
    {"pan_capture", "0"},
    {"pan_prepare", "0"},
    {"pan_inputs", "0"},
    {"replay_pack", "0"},
    {"rt_order_trace", "0"},
    {"rt_stability_log", "0"},
    {"rt_capture_state_log", "0"},
    {"rt_mesh_draw_log", "0"},
    {"rt_mesh_capture", "0"},
    {"sharc_debug", "0"},
    {"sharc_stats", "0"},
    {"sharc_leak_check", "0"},
    {"sharc_fine_check", "0"},
    {"ls_check", "0"},
    {"rt_em_log", "0"},
    {"rt_hair_debug_late_arrival", "0"},
    {"rt_hair_pixel_lod", "0"},
    {"rt_hair_mv_card", "0"},
    {"rt_hit_reference", "0"},
    {"fg_snap", "0"},
    {"fg_snap_any", "0"},
    {"fg_snap_full", "0"},
    {"fg_log_presents", "0"},
    {"fg_log_camera", "0"},
    {"sl_log_barriers", "0"},
    {"sl_verbose", "0"},
    {"sl_dev", "0"},
    {"log_presents", "0"},
    {"log_procs", "0"},
    {"fg_only_interpolated", "0"},
    {"pan_edge", "0"},
    {"rt_stats_period_frames", "0"},
    {"rt_stats_reset", "0"},
};
// END GENERATED DEFAULTS
// clang-format on

// The value a key has when no ini layer sets it, or nullptr for a key in neither table.
inline const char* code_default(const std::string& key) {
  for (const KV& kv : kReleaseDefaults)
    if (key == kv.key) return kv.value;
  for (const KV& kv : kDefaults)
    if (key == kv.key) return kv.value;
  return nullptr;
}

// Every key with its code_default(): the layer the ini files go on top of.
inline Map code_defaults() {
  Map m;
  for (const KV& kv : kDefaults) m[kv.key] = kv.value;
  for (const KV& kv : kReleaseDefaults) m[kv.key] = kv.value;
  return m;
}

// Keys read only at instance/device creation (or by static initialisers).
inline bool key_change_needs_restart(const std::string& key, const std::string& from, const std::string& to) {
  if (from == to) return false;
  if (key == "dlss5_nr" || key == "dlss5_nr_runtime") return true;
  // Vulkan extension/feature enablement is immutable for the device lifetime.
  if (key == "fg_present_metering") return true;
  // core/gi.cpp initialize_hair_origin persists the startup reservation across core reloads.
  if (key == "rt_hair_physical_origin") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // rt_hair_groom_primary is live since 5m (docs/RTX_HAIR_PRIMARY.md): its startup parts are prepared on every capable device.
  if (key == "fg") return (std::atoi(from.c_str()) >= 2) != (std::atoi(to.c_str()) >= 2);
  // M5.3: the water FS variants exist only if the key was on when the pipelines were created.
  if (key == "rt_water_compose") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.4: likewise (the sun words of the record are patched in at creation).
  if (key == "rt_water_shadows") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.4b: likewise (the local-light words of the record are patched in at creation).
  if (key == "rt_water_ls_shadows") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.5: likewise (the refraction words of the record are patched in at creation).
  if (key == "rt_water_refraction") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.6: the interface/column FS patch is provisioned at pipeline creation.
  if (key == "rt_water_surface_fresnel") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.4c: likewise (the game-shadow words of the record are patched in at creation).
  if (key == "rt_water_game_shadow") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // The instanced foliage rows patch the G-buffer VS at startup; off is live (layer/hit_material.cpp).
  if (key == "rt_hit_material_instanced") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // Native hair capture and optional LSS support are prepared at startup for live toggles.
  static const char* const kRestart[] = {"rt", "rt_alpha", "geo_capture", "ngx_hook", "rt_xfb_mb", "rt_alpha_mb",
                                         "sharc_device", "rt_bvh_split", "rt_native_tileset", "rt_omm", "rt_hit_material",
                                         "rt_hit_material_wind_rgb"};
  for (const char* k : kRestart)
    if (key == k) return from != to;
  return false;
}

// Native tileset capture is allocated at startup. Saving its next-launch value
// must not change shaders or scene publication in the running process.
inline void retain_startup_tilesets(Map& candidate,const Map& startup) {
  auto i=startup.find("rt_native_tileset");
  candidate["rt_native_tileset"]=i==startup.end()?"0":i->second;
}

// Every choice is explicit. A custom value uses the control's most demanding mode.
inline Apply knob_apply(const Knob& k,int value) {
  if(k.kind==Kind::Choice && value>=0 && value<5 && k.choices[value].id)return k.choices[value].apply;
  return k.apply;
}

// Frame generation only needs restart when crossing Off/on.
inline Apply transition_apply(const Knob& k, int from, int to) {
  if (std::string(k.id)=="frame_generation" && from>=0 && to>=0)
    return (from==0)!=(to==0) ? Apply::Restart : Apply::Live;
  return knob_apply(k,to);
}

// ---- small helpers ----

inline std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline std::string trim(const std::string& v) {
  size_t a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

// A current knob id, or the knob an old id (kKnobAliases) became.
inline std::string resolve_knob_id(const std::string& id) {
  for (const KnobAlias& a : kKnobAliases)
    if (id == a.old_id) return a.knob;
  return id;
}

inline const Knob* find_knob(const std::string& raw_id) {
  const std::string id = resolve_knob_id(raw_id);
  for (const Knob& k : kKnobs)
    if (id == k.id) return &k;
  return nullptr;
}

inline int knob_index(const std::string& raw_id) {
  const std::string id = resolve_knob_id(raw_id);
  for (int i = 0; i < kKnobCount; ++i)
    if (id == kKnobs[i].id) return i;
  return -1;
}

// dev=1 in the merged settings (the developer bg3rtx.ini; data/release.ini sets 0).
inline bool dev_mode(const std::string& dev_value) { return std::atoi(dev_value.c_str()) == 1; }

// Whether the GUI shows a knob (developer-only ones need dev=1).
inline bool knob_visible(const Knob& k, bool dev) { return dev || !k.dev_only; }

// Unqualified developer knobs a release player may opt into with experimental_options=1
// (launcher: "Show experimental options", Experimental page). Rejected candidates
// (compact hit state, alpha metadata cache, empty water tiles), perf_* keys, diagnostics and
// knobs owned by an automatic release policy are not listed and stay developer only.
// Release 2026-11: Water surface reflections, FG pacing tests and the strand edge resolve. The other strand knobs
// (exp_hair_*) stay developer only: release resets their keys, except the ones the release control RTX Hair
// (exp_hair_groom_primary, 2026-11-08) writes while it is On (enforce_release_policy).
inline constexpr const char* kOptInKnobs[] = {
  "water_surface_fresnel", "fg_destroy_wait_exact", "fg_present_metering",
  // 5n: lets players judge the strand edge resolve live themselves (RTX Hair dithering).
  "exp_hair_strand_aa"};
inline bool opt_in_knob(const Knob& k) {
  for (const char* id : kOptInKnobs) if (std::string(id) == k.id) return true;
  return false;
}
// Release visibility with the experimental opt-in. dev=1 already shows everything.
inline bool knob_visible(const Knob& k, bool dev, bool opt_in) {
  return knob_visible(k, dev) || (opt_in && opt_in_knob(k));
}

inline int choice_count(const Knob& k) {
  int n = 0;
  while (n < 5 && k.choices[n].id) ++n;
  return n;
}

// -1 for "custom", empty or unknown ids.
inline int preset_from_id(const std::string& id) {
  std::string s = lower(trim(id));
  for (int p = 0; p < kPresetCount; ++p)
    if (s == kPresetId[p]) return p;
  return -1;
}

inline std::string slider_value_string(const Knob& k, int v) {
  char buf[32];
  if (k.slider.scale == 1.0f)
    snprintf(buf, sizeof buf, "%d", v);
  else
    snprintf(buf, sizeof buf, "%.2f", double(v) * double(k.slider.scale));
  return buf;
}

// The keys knob `k` writes for `value` (choice index or slider value).
inline void knob_kvs(const Knob& k, int value, KVList& out) {
  if (k.kind == Kind::Choice) {
    if (value < 0 || value >= choice_count(k)) return;
    for (const KV& kv : k.choices[value].kv)
      if (kv.key) out.emplace_back(kv.key, kv.value);
    return;
  }
  for (const char* key : k.slider.keys)
    if (key) out.emplace_back(key, slider_value_string(k, value));
}

// Every key a knob may write.
inline std::vector<std::string> knob_keys(const Knob& k) {
  std::vector<std::string> keys;
  auto add = [&](const char* key) {
    if (key && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
  };
  if (k.kind == Kind::Choice) {
    for (int c = 0; c < choice_count(k); ++c)
      for (const KV& kv : k.choices[c].kv) add(kv.key);
  } else {
    for (const char* key : k.slider.keys) add(key);
  }
  return keys;
}

inline bool is_knob_key(const std::string& key) {
  for (const Knob& k : kKnobs)
    for (const std::string& kk : knob_keys(k))
      if (kk == key) return true;
  return false;
}

inline KVList expand_preset(int p) {
  KVList out;
  if (p < 0 || p >= kPresetCount) return out;
  for (const Knob& k : kKnobs) knob_kvs(k, k.preset[p], out);
  for (const KV& kv : kPresetExtra[p])
    if (kv.key) out.emplace_back(kv.key, kv.value);
  return out;
}

inline std::vector<int> preset_values(int p) {
  std::vector<int> v(kKnobCount, 0);
  for (int i = 0; i < kKnobCount; ++i) v[i] = kKnobs[i].preset[p < 0 ? kHigh : p];
  return v;
}

// ---- ini parsing and layering ----

// key=value lines; '#' starts a comment outside a double-quoted value.
// Backslashes stay literal so quoted Windows runtime paths round-trip unchanged.
inline Map parse_ini(const std::string& text) {
  Map out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    std::string s = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? text.size() : nl + 1;
    size_t quoted_eq = s.find('=');
    if (quoted_eq != std::string::npos) {
      std::string key = trim(s.substr(0, quoted_eq));
      std::string value = trim(s.substr(quoted_eq + 1));
      size_t end = value.size() > 1 && value.front() == '"' ? value.find('"', 1) : std::string::npos;
      if (!key.empty() && key.find('#') == std::string::npos && end != std::string::npos) {
        std::string rest = trim(value.substr(end + 1));
        if (rest.empty() || rest.front() == '#') { out[key] = value.substr(1, end - 1); continue; }
      }
    }
    size_t hash = s.find('#');
    if (hash != std::string::npos) s.resize(hash);
    size_t eq = s.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(s.substr(0, eq));
    if (!key.empty()) out[key] = trim(s.substr(eq + 1));
  }
  return out;
}

// Applies one layer onto `merged`: its preset's keys first, then its own keys.
// Returns a note for the log ("" or "unknown preset 'x'").
inline void enforce_release_policy(Map& m);
inline void complete_marked_choices(Map& merged, const Map& layer);

inline std::string apply_layer(Map& merged, const Map& layer) {
  std::string note;
  auto it = layer.find("preset");
  if (it != layer.end()) {
    int p = preset_from_id(it->second);
    if (p >= 0) {
      for (auto& kv : expand_preset(p)) merged[kv.first] = kv.second;
    } else if (!it->second.empty() && lower(it->second) != "custom") {
      note = "unknown preset '" + it->second + "' ignored";
    }
  }
  for (auto& kv : layer) merged[kv.first] = kv.second;
  complete_marked_choices(merged, layer);
  enforce_release_policy(merged);
  return note;
}

inline int map_int(const Map& m, const char* key, int fallback) {
  auto it = m.find(key);
  return it == m.end() ? fallback : std::atoi(it->second.c_str());
}

// Derived values. rt_build=auto builds the ray-tracing scene only while some
// ray-traced effect (or an RT debug view) consumes it, the same consumers the
// core checks before it traces (core/gi.cpp). Returns true if anything changed.
inline bool resolve_derived(Map& m) {
  auto it = m.find("rt_build");
  if (it == m.end() || lower(it->second) != "auto") return false;
  bool consumer = map_int(m, "rt_shadows", 0) || map_int(m, "gi_rt", 0) ||
                  map_int(m, "rt_local_shadows", 0) || map_int(m, "rt_reflections", 0) || map_int(m, "rt_debug", 0);
  it->second = consumer ? "1" : "0";
  return true;
}

// The full merge the layer does: every layer in order, then derived values.
inline Map merge_layers(const std::vector<const Map*>& layers, std::vector<std::string>* notes = nullptr) {
  Map merged;
  for (const Map* l : layers) {
    if (!l) continue;
    std::string n = apply_layer(merged, *l);
    if (!n.empty() && notes) notes->push_back(n);
  }
  resolve_derived(merged);
  return merged;
}

// ---- reading the current choice back from a merged map ----

inline std::string lookup(const Map& m, const std::string& key) {
  auto it = m.find(key);
  if (it != m.end()) return it->second;
  const char* d = code_default(key);
  return d ? d : "";
}

// `want` is the choice's value; "auto" also matches an explicit "1".
inline bool same_value(const std::string& a, const std::string& want) {
  const std::string& b = want;
  if (lower(a) == lower(b)) return true;
  if (lower(b) == "auto" && trim(a) == "1") return true;
  char* ea = nullptr;
  char* eb = nullptr;
  double x = std::strtod(a.c_str(), &ea), y = std::strtod(b.c_str(), &eb);
  if (ea == a.c_str() || eb == b.c_str() || *ea || *eb) return false;
  return std::fabs(x - y) < 1e-4;
}

// Choice index / slider value the merged (pre-resolve) map selects, or -1
// when the keys match no choice (hand-edited: shown as "custom").
inline int knob_value(const Knob& k, const Map& m) {
  if (k.kind == Kind::Choice) {
    if (std::string(k.id) == "exp_hidden_surface" && lookup(m, "rt_hit_emission") == "2") return -1;
    for (int c = 0; c < choice_count(k); ++c) {
      bool all = true;
      for (const KV& kv : k.choices[c].kv)
        if (kv.key && !same_value(lookup(m, kv.key), kv.value)) all = false;
      if (all) return c;
    }
    // Migration: the highest choice one of whose mark keys holds that choice's value.
    for (int c = choice_count(k) - 1; c > 0; --c)
      for (const char* mark : k.choices[c].mark) {
        if (!mark) continue;
        for (const KV& kv : k.choices[c].kv)
          if (kv.key && std::string(kv.key) == mark && same_value(lookup(m, mark), kv.value)) return c;
      }
    return -1;
  }
  std::string first = lookup(m, k.slider.keys[0]);
  for (const char* key : k.slider.keys)
    if (key && !same_value(lookup(m, key), first)) return -1;
  int v = int(std::lround(std::atof(first.c_str()) / k.slider.scale));
  if (k.slider.has_off && v == 0) return 0;
  return v < k.slider.min || v > k.slider.max ? -1 : v;
}

// Neutralize stale raw overrides as well as hidden GUI selections in release.
// A file written before a release control's choice gained keys (RTX Hair On, 2026-11-10: four keys more) still
// selects that choice through its migration mark, but lacks the new keys, so they would load as their code defaults
// until the launcher saves again. The keys of such a choice that this file does not set (its preset writes every choice key, so it cannot tell)
// take the choice's values at load. A key the file sets keeps the file's value. Only for a mark set in this same
// file, only without dev=1, and never for developer-only knobs or choices.
inline void complete_marked_choices(Map& merged, const Map& layer) {
  if (dev_mode(lookup(merged, "dev"))) return;
  for (const auto& k : kKnobs) {
    if (k.dev_only || k.kind != Kind::Choice) continue;
    bool exact = false;
    for (int c = 0; c < choice_count(k) && !exact; ++c) {
      exact = true;
      for (const KV& kv : k.choices[c].kv)
        if (kv.key && !same_value(lookup(merged, kv.key), kv.value)) exact = false;
    }
    if (exact) continue;
    const int c = knob_value(k, merged);   // the choice the mark selects
    if (c <= 0 || k.choices[c].dev) continue;
    bool marked_here = false;
    for (const char* mark : k.choices[c].mark)
      if (mark && layer.count(mark))
        for (const KV& kv : k.choices[c].kv)
          if (kv.key && std::string(kv.key) == mark && same_value(lookup(layer, mark), kv.value)) marked_here = true;
    if (!marked_here) continue;
    for (const KV& kv : k.choices[c].kv)
      if (kv.key && !layer.count(kv.key)) merged[kv.key] = kv.value;
  }
}

inline void enforce_release_policy(Map& m) {
  if (dev_mode(lookup(m, "dev"))) return;
  auto write = [&](const char* id, int value) {
    KVList pairs; knob_kvs(*find_knob(id), value, pairs);
    for (const auto& kv : pairs) m[kv.first] = kv.second;
  };
  // experimental_options=1: the player opted into the unqualified kOptInKnobs; 0 restores them.
  const bool opt_in = map_int(m, "experimental_options", 0) == 1;
  const bool omm_enabled = map_int(m, "rt_omm", 1) != 0;
  // The selected choice of a release control wins over the reset below for the keys that choice writes: RTX Hair On
  // (exp_hair_groom_primary) writes keys the developer-only exp_hair_* knobs also own, and 0.9.5-beta shipped with the
  // toggle inert because their reset undid it. Only that choice overlaps today (settings_test pins the list).
  KVList keep;
  for (const auto& k : kKnobs) {
    const int c = k.dev_only || k.kind != Kind::Choice ? -1 : knob_value(k, m);
    if (c < 0 || k.choices[c].dev) continue;
    for (const KV& kv : k.choices[c].kv) if (kv.key) keep.emplace_back(kv.key, lookup(m, kv.key));
  }
  for (const auto& k : kKnobs) if (k.dev_only && !(opt_in && opt_in_knob(k))) write(k.id, 0);
  for (const auto& kv : keep) m[kv.first] = kv.second;
  // Player files can override the packaged defaults. Keep diagnostic pins
  // effective after layering too. geo_capture is an RT input, not a capture
  // dump; Detailed logging permits support statistics, not reference passes.
  const bool detailed=map_int(m,"detailed_logging",0)!=0;
  for (const KV& pin:kReleaseDefaults) {
    const std::string key=pin.key;
    if (key=="dev" || key=="detailed_logging" || key=="geo_capture" || std::string(pin.value)!="0") continue;
    const bool support_stats=key=="rt_stats" || key=="ls_stats" || key=="rf_stats" || key=="sharc_stats" ||
      key=="cpu_timing" || key=="geo_timing" || key=="rt_stats_period_frames" || key=="rt_stats_reset";
    if (!detailed || !support_stats) m[key]="0";
  }
  m["rr_post"] = "1";
  m["rt_scene_compact_holds"] = "1";
  m["rt_compact_hit_state"] = "1";
  m["gi_far_roof_early"] = "1";
  m["rf_compact_probe_state"] = "1";
  for (const char* key : {"perf_submit_batch", "perf_hitmat_lists", "perf_hook_state",
       "sharc",
       "rt_hair_receiver_guard"}) m[key] = "0";
  m.erase("rt_water_rf_edge"); m.erase("rt_water_rr_hitdist");
  int bounce = knob_value(*find_knob("exp_hidden_surface"), m);
  if (bounce < 0) bounce = map_int(m, "gi_far", 0) ? 2 :
      (map_int(m, "rt_hit_material", 0) || map_int(m, "rt_hit_emission", 0) ? 1 : 0);
  write("exp_hidden_surface", std::min(bounce, 2));
  if (map_int(m, "ls_box", 0)) write("box_lights", 1);
  // A partial override need not match any choice (e.g. Fast + ls_shadowed=1).
  // Normalize the raw hidden mode without changing whether local shadows are enabled.
  if (map_int(m, "ls_shadowed", 0) == 1)
    m["ls_shadowed"] = map_int(m, "ls_samples", 0) == 0 ? "3" : "0";
  if (map_int(m, "rt_native_tileset", 0)) write("exp_native_coverage", 1);
  m["rt_hair_pool_trim"] = map_int(m, "rt_native_hair", 0) ? "1" : "0";
  // OMM and offline masks are qualified for Full geometry in High and Ultra.
  bool qualified_full = (lookup(m, "preset") == "high" || lookup(m, "preset") == "ultra") && map_int(m, "rt_alpha", 0) &&
      lookup(m, "rt_build") != "0";
  write("exp_vt_alpha_masks", qualified_full ? 1 : 0);
  write("exp_omm", qualified_full && omm_enabled ? 1 : 0);
  // Approved k1 optimisation, no release control. Lower presets lack qualification.
  write("exp_ls_exact_k", lookup(m, "preset") == "ultra" && lookup(m, "rt_build") != "0" &&
      knob_value(*find_knob("local_shadows"), m) == 2 ? 1 : 0);
  // Qualified camera correction follows water.
  m["rt_water_compose_shift"] = map_int(m, "rt_water_compose", 0) ? "1" : "0";
}

inline std::vector<int> knob_values(const Map& m) {
  std::vector<int> v(kKnobCount);
  for (int i = 0; i < kKnobCount; ++i) v[i] = knob_value(kKnobs[i], m);
  return v;
}

// The preset whose knob values all equal `values`, else -1 (custom).
inline int matching_preset(const std::vector<int>& values) {
  for (int p = 0; p < kPresetCount; ++p) {
    bool all = true;
    for (int i = 0; i < kKnobCount; ++i)
      if (std::string(kKnobs[i].category) != "Support" && values[i] != kKnobs[i].preset[p]) all = false;
    if (all) return p;
  }
  return -1;
}

// ---- dependencies and capabilities ----

// Text naming the first unmet dependency of choice `c` given all knob values, or "".
inline std::string unmet_need(const Knob& k, int c, const std::vector<int>& values) {
  if (k.kind != Kind::Choice || c < 0 || c >= choice_count(k)) return "";
  for (const Need& n : k.choices[c].need) {
    if (!n.knob) continue;
    int i = knob_index(n.knob);
    if (i < 0) continue;
    if (values[i] >= n.min_choice) continue;
    if (knob_visible(k, false)) return std::string("Requires ") + kKnobs[i].label + ": " + kKnobs[i].choices[n.min_choice].label;
    if (n.min_choice == 1) return std::string("needs ") + kKnobs[i].label + " on";
    return std::string("needs ") + kKnobs[i].label + " " + kKnobs[i].choices[n.min_choice].label;
  }
  return "";
}

inline std::string unmet_key_need(const Knob& k,int c,const Map& effective) {
  if(c<=0)return {};
  for(const auto& need:k.required_on)
    if(need.key && c>=need.min_choice && !same_value(lookup(effective,need.key),need.value))return std::string(knob_visible(k,false)?"Requires ":"needs ")+need.label;
  return {};
}

// Lowers every choice whose knob dependencies are unmet to the highest choice whose are met
// (repeated, so a chain such as far-field -> hidden-surface -> geometry settles). Returns the
// knob indices it changed. Hand-edited (-1) values are left alone.
inline std::vector<int> enforce_needs(std::vector<int>& values) {
  std::vector<int> changed;
  for (bool again = true; again;) {
    again = false;
    for (int i = 0; i < kKnobCount; ++i) {
      const Knob& k = kKnobs[i];
      if (k.kind != Kind::Choice || values[i] <= 0) continue;
      int c = values[i];
      while (c > 0 && !unmet_need(k, c, values).empty()) --c;
      if (c != values[i]) {
        values[i] = c;
        again = true;
        if (std::find(changed.begin(), changed.end(), i) == changed.end()) changed.push_back(i);
      }
    }
  }
  return changed;
}

inline bool choice_supported(const Knob& k, int c, unsigned caps) {
  if (k.kind != Kind::Choice || c < 0 || c >= choice_count(k)) return true;
  return (k.choices[c].caps & ~caps) == 0;
}

// The highest supported choice not above `c` (choices are ordered by cost).
inline int clamp_choice(const Knob& k, int c, unsigned caps) {
  if (k.kind != Kind::Choice) return c;
  while (c > 0 && !choice_supported(k, c, caps)) --c;
  return c;
}

// Choice visibility never depends on the stored value. Hidden legacy values remain custom.
inline const char* choice_maturity(const Knob& k, int c) {
  if (k.kind != Kind::Choice || c < 0 || c >= choice_count(k)) return "custom";
  const Choice& ch = k.choices[c];
  if (k.dev_only || ch.dev) return "developer";
  if (ch.awaiting_qualification) return "awaiting_qualification";
  if (ch.experimental || (k.experimental && c > 0)) return "experimental";
  return "qualified";
}

inline bool choice_visible(const Knob& k, int c, bool dev, bool opt_in = false) {
  return knob_visible(k, dev, opt_in) && c >= 0 && c < choice_count(k) && (dev || !k.choices[c].dev);
}

inline bool choice_requires_experimental(const Knob& k, int c) {
  return std::string(k.id)!="dlss5_nr" && k.kind==Kind::Choice && c>=0 && c<choice_count(k) &&
    (k.choices[c].awaiting_qualification || k.choices[c].experimental || (k.experimental && c>0));
}

inline bool choice_available(const Knob& k, int c, bool dev, bool experimental, unsigned caps,
                             bool opt_in = false) {
  return choice_visible(k, c, dev, opt_in) && choice_supported(k, c, caps) &&
         (experimental || !choice_requires_experimental(k,c));
}

// Selecting choice `c` of knob `i` raises every knob it needs (recursively) to the needed choice, so one option
// turns on what it requires. All or nothing: false (values unchanged) when a need cannot be met (custom value,
// hidden, experimental without the opt-in, or unsupported hardware); the card then shows the unmet need as before.
// `raised` receives the knobs it changed. Lowering when a prerequisite is turned off stays with enforce_needs.
inline bool raise_needs(std::vector<int>& values, int i, int c, bool dev, bool experimental, unsigned caps,
                        bool opt_in, std::vector<int>* raised = nullptr) {
  std::vector<int> v = values, changed;
  std::vector<std::pair<int, int>> todo{{i, c}};
  while (!todo.empty()) {
    auto [ki, kc] = todo.back();
    todo.pop_back();
    const Knob& k = kKnobs[ki];
    if (k.kind != Kind::Choice || kc <= 0 || kc >= choice_count(k)) continue;
    for (const Need& n : k.choices[kc].need) {
      if (!n.knob) continue;
      const int j = knob_index(n.knob);
      if (j < 0 || v[j] >= n.min_choice) continue;
      // A developer-only need of a visible knob is raised with it when it is supported and qualified (RTX Hair, 0.9.5:
      // its strands-in-RT part, exp_hair_groom, is not a release card of its own). Experimental or unsupported needs
      // still raise nothing.
      const bool hidden_part = !knob_visible(kKnobs[j], dev, opt_in) && knob_visible(kKnobs[ki], dev, opt_in) &&
                               choice_supported(kKnobs[j], n.min_choice, caps) && !choice_requires_experimental(kKnobs[j], n.min_choice);
      // (A hidden knob holds -1, "preserve", in release: as its raised part it takes the needed choice.)
      if ((v[j] < 0 && !hidden_part) || !(choice_available(kKnobs[j], n.min_choice, dev, experimental, caps, opt_in) || hidden_part))
        return false;
      v[j] = n.min_choice;
      if (std::find(changed.begin(), changed.end(), j) == changed.end()) changed.push_back(j);
      if (changed.size() > size_t(kKnobCount)) return false;
      todo.emplace_back(j, n.min_choice);
    }
  }
  values = std::move(v);
  if (raised) *raised = std::move(changed);
  return true;
}

// Pass these values to render_user_ini to preserve hidden/custom keys during an unrelated save.
inline std::vector<int> presentation_values(const Map& effective, bool dev, bool opt_in = false) {
  auto values = knob_values(effective);
  for (int i = 0; i < kKnobCount; ++i)
    if (!knob_visible(kKnobs[i], dev, opt_in) ||
        (kKnobs[i].kind == Kind::Choice && !choice_visible(kKnobs[i], values[i], dev, opt_in))) values[i] = -1;
  return values;
}

struct DependencyPreview {
  std::vector<int> values;
  std::vector<int> changed; // Every adjusted card, including transitive dependencies.
  KVList required_keys;    // Explicit startup adjustments; never silently applied.
  std::vector<std::string> blocked;
};

// Pure preview: the caller presents all adjustments before accepting either policy.
// enable_required=false lowers dependent choices; true enables their prerequisites.
inline DependencyPreview preview_dependencies(const std::vector<int>& edited, const Map& effective,
                                               unsigned caps, bool enable_required,
                                               bool dev = false, bool experimental = false,
                                               bool opt_in = false) {
  DependencyPreview out{edited, {}, {}, {}};
  if (edited.size() != kKnobCount) { out.blocked.push_back("Settings could not be read. Reopen the launcher."); return out; }
  if (!enable_required) enforce_needs(out.values);
  else {
    for (int pass = 0; pass < kKnobCount; ++pass) {
      bool changed = false;
      for (int i = 0; i < kKnobCount; ++i) {
        const Knob& k = kKnobs[i];
        int c = out.values[i];
        if (k.kind != Kind::Choice || c <= 0 || c >= choice_count(k)) continue;
        for (const Need& n : k.choices[c].need) {
          if (!n.knob) continue;
          int j = knob_index(n.knob);
          if (j < 0 || out.values[j] >= n.min_choice) continue;
          if (out.values[j] < 0 || !choice_available(kKnobs[j], n.min_choice, dev, experimental, caps, opt_in)) {
            const std::string message = std::string(knob_visible(k,false)?"Check ":"Review required setting: ") + kKnobs[j].label;
            if (std::find(out.blocked.begin(), out.blocked.end(), message) == out.blocked.end())
              out.blocked.push_back(message);
            continue;
          }
          out.values[j] = n.min_choice;
          changed = true;
        }
      }
      if (!changed) break;
    }
  }
  Map selected = effective;
  for (int i = 0; i < kKnobCount; ++i) {
    KVList pairs;
    knob_kvs(kKnobs[i], out.values[i], pairs);
    for (const auto& kv : pairs) selected[kv.first] = kv.second;
  }
  for (int i = 0; i < kKnobCount; ++i) {
    const Knob& k = kKnobs[i];
    int c = out.values[i];
    if (c != edited[i]) out.changed.push_back(i);
    if (k.kind != Kind::Choice || c <= 0 || c >= choice_count(k)) continue;
    if (!choice_supported(k, c, caps)) out.blocked.push_back(knob_visible(k,false)?std::string(k.label)+" is unavailable on this GPU":std::string("Unsupported: ")+k.label);
    for (const KeyNeed& need : k.required_on) {
      if (!need.key || c < need.min_choice || same_value(lookup(selected, need.key), need.value)) continue;
      auto found = std::find_if(out.required_keys.begin(), out.required_keys.end(),
                                [&](const auto& kv) { return kv.first == need.key; });
      if (found == out.required_keys.end()) out.required_keys.emplace_back(need.key, need.value);
    }
  }
  return out;
}

// Values of preset `p` limited to what the hardware supports.
inline std::vector<int> preset_values_for(int p, unsigned caps) {
  std::vector<int> v = preset_values(p);
  for (int i = 0; i < kKnobCount; ++i) v[i] = clamp_choice(kKnobs[i], v[i], caps);
  enforce_needs(v);
  return v;
}

struct Gpu {
  unsigned vendor = 0;      // PCI vendor: 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
  unsigned device = 0;      // PCI device id
  std::string name;
  bool ray_query = false;   // VK_KHR_ray_query + VK_KHR_acceleration_structure
  bool discrete = false;
  bool opacity_micromap = false; // Required EXT/core features were queried, not inferred from GPU name.
  bool linear_swept_spheres = false;
  bool shader_int64 = false;
  bool fragment_stores = false;
  bool hair_grooms = false;
};

// NVIDIA RTX generation as the marketing series: 20, 30, 40, 50 (0 unknown / not RTX).
inline int nv_series(const Gpu& g) {
  if (g.vendor != 0x10DE || !g.ray_query) return 0;
  // "GeForce RTX 4070 Ti", "RTX 5090 Laptop GPU": the first two digits.
  size_t at = g.name.find("RTX ");
  auto digit = [&](size_t i) { return i < g.name.size() && std::isdigit(static_cast<unsigned char>(g.name[i])); };
  if (at != std::string::npos && digit(at + 4) && digit(at + 5) && digit(at + 6) && digit(at + 7)) {
    int s = (g.name[at + 4] - '0') * 10;
    if (s >= 20) return s;
  }
  // Workstation names (RTX A6000, RTX PRO): by PCI device id range.
  if (g.device >= 0x2900) return 50;   // Blackwell
  if (g.device >= 0x2600) return 40;   // Ada
  if (g.device >= 0x2200) return 30;   // Ampere
  if (g.device >= 0x1E00) return 20;   // Turing
  return 0;
}

inline unsigned gpu_caps(const Gpu& g) {
  unsigned c = 0;
  if (g.ray_query) c |= kCapRayQuery;
  if (g.ray_query && g.opacity_micromap) c |= kCapOpacityMicromap;
  if (g.linear_swept_spheres) c |= kCapLinearSweptSpheres;
  if (g.shader_int64) c |= kCapShaderInt64;
  if (g.fragment_stores) c |= kCapFragmentStores;
  if (g.hair_grooms) c |= kCapHairGrooms;
  int s = nv_series(g);
  if (s >= 20) c |= kCapDlss;
  if (s >= 40) c |= kCapFg;
  if (s >= 50) c |= kCapMfg;
  return c;
}

// Tier within a series from the model number ("4070 Ti" -> 70 + 5).
inline int nv_tier(const Gpu& g) {
  size_t at = g.name.find("RTX ");
  if (at == std::string::npos || at + 8 > g.name.size()) return 0;
  int t = std::atoi(g.name.substr(at + 6, 2).c_str());
  if (g.name.find(" Ti", at) != std::string::npos || g.name.find("SUPER", at) != std::string::npos) t += 5;
  return t;
}

// Suggested preset (section 5.1), then limited by gpu_caps().
inline int suggest_preset(const Gpu& g) {
  if (!g.ray_query || g.vendor != 0x10DE) return kLow;   // non-NVIDIA RT is untested: Low
  int s = nv_series(g), t = nv_tier(g);
  bool laptop = g.name.find("Laptop") != std::string::npos;
  int p = kLow;
  if (s >= 50) p = t >= 90 ? kUltra : t >= 75 ? kHigh : t >= 65 ? kMedium : kLow;
  else if (s == 40) p = t >= 80 ? kHigh : t >= 65 ? kMedium : kLow;
  else if (s == 30) p = t >= 70 ? kMedium : kLow;
  if (laptop && p > kLow) --p;
  return p;
}

// ---- writing bg3rtx_user.ini ----

// Renders the user file: `preset=<base>` plus every knob whose value differs
// from that preset, plus `forced` keys (hardware limits such as rt=0). Lines of
// `existing` with keys the tools do not manage are kept, so hand edits survive;
// so are the keys of knobs whose value is -1 (hand-edited, matching no choice).
inline std::string render_user_ini(const std::string& existing, int base_preset, const std::vector<int>& values,
                                   const KVList& forced = {}) {
  std::vector<std::string> managed = {"preset"};
  for (int i = 0; i < kKnobCount; ++i)
    if (values[i] >= 0)
      for (const std::string& key : knob_keys(kKnobs[i])) managed.push_back(key);
  for (auto& kv : forced) managed.push_back(kv.first);

  std::string kept;
  size_t pos = 0;
  while (pos < existing.size()) {
    size_t nl = existing.find('\n', pos);
    std::string line = existing.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? existing.size() : nl + 1;
    std::string s = line;
    size_t hash = s.find('#');
    if (hash != std::string::npos) s.resize(hash);
    size_t eq = s.find('=');
    if (eq == std::string::npos) continue;   // comments and blank lines are regenerated
    std::string key = trim(s.substr(0, eq));
    if (std::find(managed.begin(), managed.end(), key) != managed.end()) continue;
    kept += trim(line) + "\n";
  }

  std::string out =
      "# BG3RTX user settings, written by BG3RTX.exe. Layered over bg3rtx.ini:\n"
      "# the preset's keys apply first, then every key below. Re-read while the game runs.\n"
      "# See docs/USER_SETTINGS.md.\n";
  int p = base_preset < 0 || base_preset >= kPresetCount ? kHigh : base_preset;
  out += std::string("preset=") + kPresetId[p] + "\n";
  std::string overrides;
  for (int i = 0; i < kKnobCount; ++i) {
    const Knob& k = kKnobs[i];
    if (values[i] < 0 || values[i] == k.preset[p]) continue;
    KVList kvs;
    knob_kvs(k, values[i], kvs);
    // Keys the preset's choice writes but this one does not (gi=0 vs the RT keys)
    // keep the preset's values: harmless, the choice's own keys decide.
    overrides += std::string("# ") + k.label + "\n";
    for (auto& kv : kvs) overrides += kv.first + "=" + kv.second + "\n";
  }
  if (!overrides.empty()) out += "\n# Changed from the preset\n" + overrides;
  if (!forced.empty()) {
    out += "\n# Hardware limits (set by the launcher)\n";
    for (auto& kv : forced) out += kv.first + "=" +
      (kv.second.find('#') == std::string::npos ? kv.second : "\"" + kv.second + "\"") + "\n";
  }
  if (!kept.empty()) out += "\n# Other keys (kept as written)\n" + kept;
  return out;
}

// ---- JSON export (data/settings_schema.json) ----

inline std::string json_str(const char* s) {
  std::string o = "\"";
  for (const char* p = s ? s : ""; *p; ++p) {
    if (*p == '"' || *p == '\\') o += '\\';
    o += *p;
  }
  return o + "\"";
}

inline const char* apply_name(Apply a) {
  switch (a) {
    case Apply::Live: return "live";
    case Apply::Hitch: return "hitch";
    case Apply::Restart: return "restart";
    default: return "unspecified";
  }
}

inline std::string schema_json() {
  std::string o = "{\n  \"generated_from\": \"common/settings.h\",\n  \"presets\": [\n";
  for (int p = 0; p < kPresetCount; ++p) {
    o += "    {\"id\": " + json_str(kPresetId[p]) + ", \"label\": " + json_str(kPresetLabel[p]) +
         ", \"description\": " + json_str(kPresetDesc[p]) + ", \"extra\": {";
    bool first = true;
    for (const KV& kv : kPresetExtra[p])
      if (kv.key) {
        o += std::string(first ? "" : ", ") + json_str(kv.key) + ": " + json_str(kv.value);
        first = false;
      }
    o += std::string("}}") + (p + 1 < kPresetCount ? "," : "") + "\n";
  }
  o += "  ],\n  \"knobs\": [\n";
  static const char* const kCapNames[] = {"ray_query", "dlss", "fg", "mfg", "opacity_micromap", "linear_swept_spheres", "shader_int64", "fragment_stores", "hair_grooms"};
  for (int i = 0; i < kKnobCount; ++i) {
    const Knob& k = kKnobs[i];
    o += "    {\"id\": " + json_str(k.id) + ", \"label\": " + json_str(k.label) + ",\n     \"description\": " +
         json_str(k.desc) + ",\n     \"apply\": " + json_str(apply_name(k.apply)) + ", \"apply_note\": " +
         json_str(k.apply_note) + ", \"advanced\": " + (k.advanced ? "true" : "false") + ", \"experimental\": " + (k.experimental ? "true" : "false") + ", \"developer\": " + (k.dev_only ? "true" : "false") + ",\n";
    o += "     \"category\": " + json_str(k.category) + ", \"summary\": " + json_str(k.desc) + ",\n";
    o += "     \"details\": " + json_str(k.details) + ",\n";
    o += "     \"cost\": {\"status\": " + json_str(k.cost.status) + ", \"gpu\": " + json_str(k.cost.gpu) +
         ", \"vram\": " + json_str(k.cost.vram) + ", \"provenance\": " + json_str(k.cost.provenance) + "},\n";
    o += "     \"required_on\": {";
    bool firstNeed=true;
    for(const auto& need:k.required_on)if(need.key){o+=std::string(firstNeed?"":", ")+json_str(need.key)+": "+json_str(need.value);firstNeed=false;}
    o += "},\n     \"required_from_choice\": {";
    firstNeed=true;
    for(const auto& need:k.required_on)if(need.key){o+=std::string(firstNeed?"":", ")+json_str(need.key)+": "+std::to_string(need.min_choice);firstNeed=false;}
    o += "},\n";
    o += "     \"preset_values\": {";
    for (int p = 0; p < kPresetCount; ++p)
      o += json_str(kPresetId[p]) + ": " + std::to_string(k.preset[p]) + (p + 1 < kPresetCount ? ", " : "");
    o += "},\n";
    if (k.kind == Kind::Slider) {
      o += "     \"kind\": \"slider\", \"min\": " + std::to_string(k.slider.min) + ", \"max\": " +
           std::to_string(k.slider.max) + ", \"step\": " + std::to_string(k.slider.step) + ", \"off_value\": " +
           (k.slider.has_off ? "0" : "null") + ", \"unit\": " + json_str(k.slider.unit) + ", \"keys\": [";
      bool first = true;
      for (const char* key : k.slider.keys)
        if (key) {
          o += std::string(first ? "" : ", ") + json_str(key);
          first = false;
        }
      char sc[32];
      snprintf(sc, sizeof sc, "%g", double(k.slider.scale));
      o += std::string("], \"scale\": ") + sc + "}";
    } else {
      o += "     \"kind\": \"choice\", \"choices\": [\n";
      int n = choice_count(k);
      for (int c = 0; c < n; ++c) {
        const Choice& ch = k.choices[c];
        o += "       {\"id\": " + json_str(ch.id) + ", \"label\": " + json_str(ch.label) + ", \"apply\": " + json_str(apply_name(ch.apply)) + ", \"caps\": [";
        bool first = true;
        for (int b = 0; b < 9; ++b)
          if (ch.caps & (1u << b)) {
            o += std::string(first ? "" : ", ") + json_str(kCapNames[b]);
            first = false;
          }
        o += "], \"keys\": {";
        first = true;
        for (const KV& kv : ch.kv)
          if (kv.key) {
            o += std::string(first ? "" : ", ") + json_str(kv.key) + ": " + json_str(kv.value);
            first = false;
          }
        o += "}, \"needs\": {";
        first = true;
        for (const Need& nd : ch.need)
          if (nd.knob) {
            o += std::string(first ? "" : ", ") + json_str(nd.knob) + ": " + std::to_string(nd.min_choice);
            first = false;
          }
        o += "}, \"migrate_from\": [";
        first = true;
        for (const char* mk : ch.mark)
          if (mk) {
            o += std::string(first ? "" : ", ") + json_str(mk);
            first = false;
          }
        o += std::string("], \"developer\": ") + (ch.dev ? "true" : "false");
        o += ", \"maturity\": " + json_str(choice_maturity(k, c));
        o += std::string("}") + (c + 1 < n ? "," : "") + "\n";
      }
      o += "     ]}";
    }
    o += std::string(i + 1 < kKnobCount ? "," : "") + "\n";
  }
  o += "  ]\n}\n";
  return o;
}

}  // namespace settings
}  // namespace bg3
