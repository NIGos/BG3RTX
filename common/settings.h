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
inline constexpr const char* kPresetLabel[kPresetCount] = {"Low", "Medium", "High", "Ultra (RTX 5090)"};
inline constexpr const char* kPresetDesc[kPresetCount] = {
    "RTX 2060-3060 / 4060, AMD and Intel with RT. No hardware scene build, no Ray Reconstruction: "
    "screen-space GI and contact shadows.",
    "RTX 3070/3080, 4060 Ti/4070. Opaque ray-traced geometry, RT GI, fast local-light shadows, glossy reflections.",
    "Estimated for RTX 4080/4090 and 5080. Ray-traced lighting and full geometry; hardware suitability is not a measured frame-rate promise.",
    "RTX 5090 at 4K-class output. High plus 2 GI rays, shadowed lights, rougher reflections and a 58 fps "
    "rendered cap (estimated 116 displayed with frame generation 2x). Includes water refraction, hidden-surface bounce and far-field sunlight bounce. Reference coverage: RTX 5090 scenes.",
};

// ---- hardware capabilities a choice needs ----

enum Cap : unsigned {
  kCapRayQuery = 1u << 0,   // VK_KHR_ray_query + VK_KHR_acceleration_structure
  kCapDlss = 1u << 1,       // NVIDIA RTX (DLSS SR/RR): Ray Reconstruction
  kCapFg = 1u << 2,         // RTX 40 and later: DLSS-G 2x
  kCapOpacityMicromap = 1u << 4, // EXT opacity micromap + core BDA/timeline/sync2
  kCapMfg = 1u << 3,        // RTX 50 and later: DLSS-G 3x/4x
};

// How a change reaches the running game (section 7.4).
enum class Apply { Live, Hitch, Restart, Partial };

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
  KV kv[8];        // keys this choice writes (key == nullptr ends the list)
  Need need[3];    // dependencies on other knobs (knob == nullptr ends the list)
  // Migration of a file whose keys match no choice exactly (written before the knob became one
  // level choice, or hand-edited): the highest choice one of whose `mark` keys has this choice's
  // value is selected, and saving writes all of its keys (GUI_AUDIT_20261101.md).
  const char* mark[4] = {};
  bool dev = false;  // shown only with dev=1 (always a knob's last choices)
  bool experimental = false;
  bool awaiting_qualification = false;
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
  Choice choices[4];        // Kind::Choice (id == nullptr ends the list)
  Slider slider;            // Kind::Slider
  int preset[kPresetCount]; // choice index or slider value per preset
  bool experimental = false; // shown under "Experimental": off in every preset, being tested
  KeyNeed required_on[4] = {}; // Raw startup prerequisites; checked, never silently written.
  bool dev_only = false;     // developer tools: the GUI shows them only with dev=1
  const char* category = "Advanced";
  Cost cost = {};
};

// clang-format off
inline constexpr Knob kKnobs[] = {
  {"rr", "Ray Reconstruction",
   "Reconstructs ray-traced lighting with DLSS Ray Reconstruction. Requires a compatible NVIDIA RTX GPU and DLSS enabled in the game.",
   Kind::Choice, Apply::Hitch, "brief hitch (history reset)", false,
   {{"off", "Off", 0, {{"rr", "0"}}, {}},
    {"on", "On", kCapDlss, {{"rr", "1"}}, {}}},
   {}, {0, 1, 1, 1}, false, {}, false, "Lighting", {}},

  {"rt_geometry", "Ray-traced geometry",
   "Includes scene geometry in ray-traced lighting. Full also includes cutout foliage, grates and chains.",
   Kind::Choice, Apply::Partial, "Opaque <-> Full: restart", false,
   {{"off", "Off", 0, {{"rt_build", "0"}, {"rt_alpha", "0"}}, {}},
    {"opaque", "Opaque only", kCapRayQuery, {{"rt_build", "auto"}, {"rt_alpha", "0"}}, {}},
    {"full", "Full (with alpha-tested)", kCapRayQuery, {{"rt_build", "auto"}, {"rt_alpha", "1"}}, {}}},
   {}, {0, 1, 2, 2}, false, {}, false, "Lighting", {}},

  {"sun_shadows", "Sun shadows",
   "Adds contact detail to sunlight shadows. Ray-traced shadows can include objects outside the camera view.",
   Kind::Choice, Apply::Live, "", false,
   {{"ss", "Screen-space", 0, {{"rt_shadows", "0"}, {"contact_shadows", "1"}}, {}},
    {"rt", "Ray-traced", kCapRayQuery, {{"rt_shadows", "1"}, {"contact_shadows", "1"}}, {{"rt_geometry", 1}}}},
   {}, {0, 1, 1, 1}, false, {}, false, "Lighting", {}},

  {"roof_shadows", "Roof sun shadows",
   "Keeps sunlight shadows under roofs hidden by the camera.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"rt_cascade_casters", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"rt_cascade_casters", "1"}}, {{"rt_geometry", 1}, {"sun_shadows", 1}}}},
   {}, {0, 0, 0, 1}, false, {}, false, "Lighting", {}},

  {"gi", "Global illumination",
   "Adds indirect light and contact shading. Ray-traced High uses more samples to reduce noise.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"gi", "0"}}, {}},
    {"ss", "Screen-space", 0, {{"gi", "1"}, {"gi_rt", "0"}, {"gi_rays", "2"}, {"gi_steps", "12"}}, {}},
    {"rt", "Ray-traced", kCapRayQuery, {{"gi", "1"}, {"gi_rt", "1"}, {"gi_rr_rays", "1"}}, {{"rt_geometry", 1}}},
    {"rt_high", "Ray-traced High", kCapRayQuery, {{"gi", "1"}, {"gi_rt", "1"}, {"gi_rr_rays", "2"}}, {{"rt_geometry", 1}}}},
   {}, {1, 2, 2, 3}, false, {}, false, "Lighting", {}},

  {"local_shadows", "Local light shadows",
   "Adds shadows from torches, lamps and spells. Fast requires Ray Reconstruction; Full keeps native shadows where needed.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"rt_local_shadows", "0"}}, {}},
    {"fast", "Fast", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "1"}, {"ls_shadowed", "0"}}, {{"rt_geometry", 1}, {"rr", 1}}},
    {"full", "Full", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "0"}, {"ls_shadowed", "3"}}, {{"rt_geometry", 1}}},
    {"full_plus", "Full + shadowed lights", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "0"}, {"ls_shadowed", "1"}}, {{"rt_geometry", 1}}}},
   {}, {0, 1, 2, 2}, false, {}, false, "Lighting", {}},

  {"box_lights", "Box light shadows",
   "Adds shadows to broad area lights in interiors. Awaiting qualification; off in every preset.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"ls_box", "0"}}, {}},
    {"on", "Shadowed", kCapRayQuery, {{"ls_box", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 1}}, {}, false, false, true},
    {"all", "All", kCapRayQuery, {{"ls_box", "2"}}, {{"rt_geometry", 1}, {"local_shadows", 1}}, {}, false, false, true}},
   {}, {0, 0, 0, 0}, false, {}, false, "Lighting", {}},

  {"reflections", "Reflections",
   "Adds reflections on smooth surfaces. Higher levels include rougher surfaces; without Ray Reconstruction only mirror-like surfaces are traced.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"rt_reflections", "0"}}, {}},
    {"glossy", "Glossy", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.4"}}, {{"rt_geometry", 1}, {"gi", 1}}},
    {"standard", "Standard", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.6"}}, {{"rt_geometry", 1}, {"gi", 1}}},
    {"rough", "Rough", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.8"}}, {{"rt_geometry", 1}, {"gi", 1}}}},
   {}, {0, 1, 2, 3}, false, {}, false, "Surfaces", {}},

  {"frame_generation", "Frame generation",
   "Adds generated display frames between rendered frames. Requires NVIDIA Reflex; display-frame estimates are not measured frame rates.",
   Kind::Choice, Apply::Partial, "Off <-> on: restart", false,
   {{"off", "Off", 0, {{"fg", "0"}}, {}},
    {"2x", "2x", kCapFg, {{"fg", "2"}}, {{"reflex", 1}}},
    {"3x", "3x", kCapMfg, {{"fg", "3"}}, {{"reflex", 1}}},
    {"4x", "4x", kCapMfg, {{"fg", "4"}}, {{"reflex", 1}}}},
   {}, {1, 1, 1, 1}, false, {}, false, "Performance", {}},

  {"rt_intensity", "RT lighting intensity",
   "Adjusts the strength of indirect light, local shadows and reflections. Lower values blend toward the original appearance.",
   Kind::Slider, Apply::Live, "", false,
   {}, {50, 100, 5, false, 0.01f, "%", {"gi_strength", "ls_strength", "rf_strength"}}, {100, 100, 100, 100}, false, {}, false, "Advanced", {}},

  {"fps_cap", "Frame cap (rendered fps)",
   "Limits rendered frames per second. Frame generation can increase displayed frames without increasing rendered frames.",
   Kind::Slider, Apply::Live, "", false,
   {}, {30, 240, 1, true, 1.0f, "fps", {"fps_cap"}}, {0, 0, 0, 58}, false, {}, false, "Performance", {}},

  {"reflex", "NVIDIA Reflex",
   "Reduces rendering latency. Frame generation requires On or On + Boost.",
   Kind::Choice, Apply::Live, "", true,
   {{"off", "Off", 0, {{"reflex", "0"}}, {}},
    {"on", "On", 0, {{"reflex", "1"}}, {}},
    {"boost", "On + Boost", 0, {{"reflex", "2"}}, {}}},
   {}, {1, 1, 1, 1}, false, {}, false, "Performance", {}},

  // M5 water chain (docs/PLAN_M5_WATER.md "Promotion": m5_promotion passed at S8, Flymm and camp, 11-01): Advanced,
  // before the Experimental section; "+ Refraction" in Ultra, Off elsewhere. The compose levels patch the
  // audited water FS at pipeline creation (restart to turn on, key_change_needs_restart) and need 64-bit
  // shader integers at device creation (sharc_device=1).
  {"exp_water", "RT water",
   "Adds reflections, shadows and a refracted view of the water bed. Enabling water lighting requires a restart.",
   Kind::Choice, Apply::Partial, "on: restart; off: live", true,
   {{"off", "Off", 0, {{"rt_water_geometry", "0"}, {"rt_water_reflections", "0"}, {"rt_water_compose", "0"},
      {"rt_water_shadows", "0"}, {"rt_water_ls_shadows", "0"}, {"rt_water_game_shadow", "0"}, {"rt_water_refraction", "0"}}, {}},
    {"reflections", "Reflections", kCapRayQuery, {{"rt_water_geometry", "1"}, {"rt_water_reflections", "1"}, {"rt_water_compose", "1"},
      {"rt_water_shadows", "0"}, {"rt_water_ls_shadows", "0"}, {"rt_water_game_shadow", "0"}, {"rt_water_refraction", "0"}},
     {{"rt_geometry", 1}}, {"rt_water_compose", "rt_water_reflections", "rt_water_geometry"}},
    {"shadows", "Reflections + shadows", kCapRayQuery, {{"rt_water_geometry", "1"}, {"rt_water_reflections", "1"}, {"rt_water_compose", "1"},
      {"rt_water_shadows", "1"}, {"rt_water_ls_shadows", "1"}, {"rt_water_game_shadow", "1"}, {"rt_water_refraction", "0"}},
     {{"rt_geometry", 1}}, {"rt_water_shadows", "rt_water_ls_shadows", "rt_water_game_shadow"}},
    {"refraction", "Reflections + shadows + refraction", kCapRayQuery, {{"rt_water_geometry", "1"}, {"rt_water_reflections", "1"}, {"rt_water_compose", "1"},
      {"rt_water_shadows", "1"}, {"rt_water_ls_shadows", "1"}, {"rt_water_game_shadow", "1"}, {"rt_water_refraction", "1"}},
     {{"rt_geometry", 1}}, {"rt_water_refraction"}}},
   {}, {0, 0, 0, 3}, false, {{"rt","1","RT device setup"},{"sharc_device","1","64-bit shader integers at startup"}}, false, "Surfaces", {"Estimate", "0.5-0.7 ms", "Up to 520 MB", "RTX 5090; Water Queens House; 4K-class output; 2026-11-02 progress; ABI 26; all water levels; total water chain. Reference only."}},

  // M4 hidden-surface bundle (docs/PLAN_M4_REFLECTIONS_GI.md "Promotion", m4_promotion without rf_hit_shade): Advanced,
  // before the Experimental section; "+ Bounce" and Emission "Hidden hits" in Ultra, Off elsewhere.
  {"exp_hidden_surface", "Hidden-surface shading",
   "Improves light bouncing from surfaces behind the camera. The reflections level remains experimental.",
   Kind::Choice, Apply::Partial, "first enable: restart; levels live", true,
   {{"off", "Off", 0, {{"rt_hit_material", "0"}, {"rt_hit_material_instanced", "0"},
      {"rt_hit_material_wind_rgb", "0"}, {"gi_hit_direct", "0"}, {"gi_hit_sun", "0"}, {"rf_hit_shade", "0"}, {"rf_hit_gi", "0"}}, {}},
    {"materials", "Materials", kCapRayQuery, {{"rt_hit_material", "1"}, {"rt_hit_material_instanced", "1"},
      {"rt_hit_material_wind_rgb", "1"}, {"gi_hit_direct", "0"}, {"gi_hit_sun", "0"}, {"rf_hit_shade", "0"}, {"rf_hit_gi", "0"}},
     {{"rt_geometry", 2}}, {"rt_hit_material", "rt_hit_material_instanced", "rt_hit_material_wind_rgb"}},
    {"bounce", "+ Bounce", kCapRayQuery, {{"rt_hit_material", "1"}, {"rt_hit_material_instanced", "1"},
      {"rt_hit_material_wind_rgb", "1"}, {"gi_hit_direct", "1"}, {"gi_hit_sun", "1"}, {"rf_hit_shade", "0"}, {"rf_hit_gi", "0"}},
     {{"rt_geometry", 2}, {"gi", 2}}, {"gi_hit_direct", "gi_hit_sun"}},
    {"reflections", "+ Reflections", kCapRayQuery, {{"rt_hit_material", "1"}, {"rt_hit_material_instanced", "1"},
      {"rt_hit_material_wind_rgb", "1"}, {"gi_hit_direct", "1"}, {"gi_hit_sun", "1"}, {"rf_hit_shade", "1"}, {"rf_hit_gi", "1"}},
     {{"rt_geometry", 2}, {"gi", 2}, {"reflections", 1}}, {"rf_hit_shade", "rf_hit_gi"}, false, true}},
   {}, {0, 0, 0, 2}, false,
   {{"rt","1","RT device setup"},{"rt_alpha","1","Native prepass capture"},{"rt_bvh_split","1","Split hit identity"},
    {"gi_rt_fallback","0","GI fallback 0 (zero-bounce hidden hits)",2}}, false, "Advanced", {}},

  {"exp_hit_emission", "Emission at ray hits",
   "Adds emitted light when lighting rays hit glowing hidden surfaces, such as lava or braziers.",
   Kind::Choice, Apply::Live, "live; first enable compiles shaders", true,
   {{"off", "Off", 0, {{"rt_hit_emission", "0"}, {"rt_emissive", "0"}}, {}},
    {"hidden", "Hidden hits", kCapRayQuery, {{"rt_hit_emission", "1"}, {"rt_emissive", "1"}}, {{"exp_hidden_surface", 1}}, {"rt_hit_emission"}},
    {"all", "All hits (developer)", kCapRayQuery, {{"rt_hit_emission", "2"}, {"rt_emissive", "1"}}, {{"exp_hidden_surface", 1}}, {"rt_hit_emission"}, true}},
   {}, {0, 0, 0, 1}, false, {{"rt","1","RT device setup"}}, false, "Advanced", {}},

  {"exp_gi_far", "Far-field sun bounce",
   "Includes more distant surfaces in sunlight bounce. Requires hidden-surface bounce lighting.",
   Kind::Choice, Apply::Live, "live; first enable compiles shaders", true,
   {{"off", "Off", 0, {{"gi_far", "0"}}, {}},
    {"m6", "6 m", kCapRayQuery, {{"gi_far", "1"}, {"gi_far_m", "6"}}, {{"exp_hidden_surface", 2}}, {"gi_far"}}},
   {}, {0, 0, 0, 1}, false, {}, false, "Advanced", {"Estimate", "+3.2-3.7 ms", "Unknown", "RTX 5090; Water Queens House and camp; 4K-class output; 2026-11-02 progress; ABI 26; 6 m; incremental GI trace cost. Reference only."}},

  // Experimental: features being tested, off in every preset (docs/GUI_AUDIT_20261101.md: 28 entries
  // became these 8, 4 since RT water, the M4 bundle and gi_far left). Each level writes every key it covers, so a file reads back as one level.
  {"exp_native_coverage", "Native coverage",
   "Improves the shape of hair, scalp and cutout surfaces in ray-traced lighting. Quality and cost remain unqualified.",
   Kind::Choice, Apply::Restart, "restart required", false,
   {{"off", "Off", 0, {{"rt_native_hair", "0"}, {"rt_native_scalp", "0"}, {"rt_native_tileset", "0"}}, {}},
    {"hair", "Hair + scalp", kCapRayQuery, {{"rt_native_hair", "1"}, {"rt_native_scalp", "1"}, {"rt_native_tileset", "0"}},
     {{"rt_geometry", 2}}, {"rt_native_hair", "rt_native_scalp"}},
    {"tilesets", "+ Tilesets", kCapRayQuery, {{"rt_native_hair", "1"}, {"rt_native_scalp", "1"}, {"rt_native_tileset", "1"}},
     {{"rt_geometry", 2}}, {"rt_native_tileset"}}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"}}, false, "Advanced", {}},

  {"exp_cascade_static_only", "Static roof casters only",
   "Excludes animated and unclassified shadow-map copies from roof sun shadows. "
   "May omit some roofs; dialogue motion and roof coverage are unqualified.",
   Kind::Choice, Apply::Live, "live; allow two frames for capture replacement", true,
   {{"off", "Off", 0, {{"rt_cascade_static_only", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"rt_cascade_static_only", "1"}}, {{"roof_shadows", 1}}}},
   {}, {0, 0, 0, 0}, true, {}, false, "Advanced", {}},

  {"exp_hair_physical_origin", "Physical hair ray origins (advanced)",
   "Adjusts where light rays start on hair to improve secondary lighting. Quality and cost remain unqualified.",
   Kind::Choice, Apply::Partial, "Enable at startup; then live A/B", true,
   {{"off", "Off", 0, {{"rt_hair_physical_origin", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"rt_hair_physical_origin", "1"}}, {{"rt_geometry", 2}, {"exp_native_coverage",1}}}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_native_hair","1","Native hair capture startup"},{"rt_origin","1","Primary origin matching"}}, false, "Advanced", {}},

  {"exp_omm", "Opacity micromaps",
   "Uses supported hardware to accelerate cutout shadows. Requires compatible hardware; quality and cost remain unqualified.",
   Kind::Choice, Apply::Restart, "restart required", false,
   {{"off", "Off", 0, {{"rt_omm", "0"}}, {}},
    {"on", "On", kCapRayQuery | kCapOpacityMicromap, {{"rt_omm", "1"}}, {{"rt_geometry", 2}}}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_bvh_split","1","Static geometry cache"}}, false, "Advanced", {}},

  {"exp_vt_alpha_masks", "Offline cutout masks",
   "Uses locally prepared foliage masks to improve cutout shadows. Coverage and brightness remain unqualified.",
   Kind::Choice, Apply::Live, "live", false,
   {{"off", "Off", 0, {{"rt_vt_alpha_masks", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"rt_vt_alpha_masks", "1"}}, {{"rt_geometry", 2}}}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_alpha","1","Cutout capture"}}, false, "Advanced", {}},

  {"exp_alpha_spill", "Preserve cutout capture on overflow",
   "Preserves more cutout surfaces when scene capture fills. Quality and cost remain unqualified.",
   Kind::Choice, Apply::Live, "live", false,
   {{"off", "Off", 0, {{"rt_alpha_spill", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"rt_alpha_spill", "1"}}, {{"rt_geometry", 2}}}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_alpha","1","Cutout capture"},{"rt_bvh_split","1","Direct split records"}}, false, "Advanced", {}},

  {"perf_hitmat_lists", "Hit material CPU batching",
   "Tests recording-owned candidate lists, short query snapshots and batched material row uploads. "
   "Preserves material validation and shading. Requires Hidden-surface shading at startup.",
   Kind::Choice, Apply::Live, "live; native materials need startup", false,
   {{"off", "Off", 0, {{"perf_hitmat_lists", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"perf_hitmat_lists", "1"}}, {{"exp_hidden_surface", 1}}}},
   {}, {0, 0, 0, 0}, true, {{"rt_hit_material","1","Native material startup"}}, true},
  {"perf_hook_state", "Reuse capture hook state",
   "Reuses immutable pipeline eligibility and gathers repeated instance matrices once per submit. "
   "Experimental CPU optimization; geometry and material coverage stay the same.",
   Kind::Choice, Apply::Live, "live", false,
   {{"off", "Off", 0, {{"perf_hook_state", "0"}}, {}},
    {"on", "On", 0, {{"perf_hook_state", "1"}}, {{"rt_geometry", 1}}}},
   {}, {0, 0, 0, 0}, true, {}, true},

  {"perf_submit_batch", "Batch descriptor and submit bookkeeping",
   "Reuses CPU scratch and batches compatible descriptor updates and private completion signals. Unqualified CPU experiment.",
   Kind::Choice, Apply::Live, "live", false,
   {{"off", "Off", 0, {{"perf_submit_batch", "0"}}, {}},
    {"on", "On", 0, {{"perf_submit_batch", "1"}}, {}}},
   {}, {0, 0, 0, 0}, false, {}, true},

  // Developer only (dev=1): debug references and options parked by user decision. Their keys keep working.
  {"exp_hit_reference", "Experimental lighting reference",
   "Shows a limited reference using point and spot lights on supported opaque surfaces. "
   "Two surfaces adds one indirect bounce; incomplete paths retain direct-only lighting and are counted. "
   "Requires Hidden-surface shading at startup. No sunlight, sky, emission or full-scene quality claim. "
   "This replaces the display for inspection; it is not a gameplay quality preset.",
   Kind::Choice, Apply::Live, "live; native materials need startup", false,
   {{"off", "Off", 0, {{"rt_hit_reference", "0"}}, {}},
    {"direct", "Direct only", kCapRayQuery, {{"rt_hit_reference", "1"}}, {{"rt_geometry", 2}, {"exp_hidden_surface",1}}},
    {"two", "Two surfaces", kCapRayQuery, {{"rt_hit_reference", "2"}}, {{"rt_geometry", 2}, {"exp_hidden_surface",1}}}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_hit_material","1","Native material startup"}}, true, "Developer", {}},

  {"exp_ls_exact_k", "Local light sampling (exact-k)",
   "Cost option for interiors with many lights: the strongest 1 or 2 lights of each pixel keep their own "
   "shadow ray, and one ray is shared by the rest, picked in proportion to their light and weighted so the "
   "mean brightness is unchanged. Needs Local light shadows Full. Off by user decision (developer only).",
   Kind::Choice, Apply::Live, "live; first enable compiles shaders", false,
   {{"off", "Off", 0, {{"ls_k", "0"}, {"ls_rest_samples", "1"}}, {}},
    {"k1", "1 exact light", kCapRayQuery, {{"ls_k", "1"}, {"ls_rest_samples", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 2}}},
    {"k2", "2 exact lights", kCapRayQuery, {{"ls_k", "2"}, {"ls_rest_samples", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 2}}}},
   {}, {0, 0, 0, 0}, true, {}, true, "Developer", {}},

  {"exp_current_sun", "Current cascade sun shadows",
   "Uses the current four native sun-caster domains with finite per-layer rays and native "
   "receiver selection. Needs ray-traced geometry, Roof sun shadows, RT device setup and the "
   "static BVH split. Gives one window of samples, then falls back while the scene is unready "
   "(DIALOGUE_SHADOWS.md): developer only until fixed. Restart required.",
   Kind::Choice, Apply::Restart, "restart required", false,
   {{"off", "Off", 0, {{"rt_sun_cascade_current", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"rt_sun_cascade_current", "1"}}, {{"rt_geometry", 1}, {"roof_shadows", 1}}}},
   {}, {0, 0, 0, 0}, true, {{"rt","1","RT device setup"},{"rt_bvh_split","1","Static geometry cache"},{"rt_cascade_casters","1","Roof sun shadows"}}, true, "Developer", {}},
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
    {"exp_roof_shadows", "roof_shadows", 1},
    {"exp_box_lights", "box_lights", 1},
    {"exp_hit_material", "exp_hidden_surface", 1},
    {"exp_hit_material_instanced", "exp_hidden_surface", 1},
    {"exp_hit_material_recycle", "exp_hidden_surface", 1},
    {"exp_hit_material_wind_rgb", "exp_hidden_surface", 1},
    {"exp_gi_hit_direct", "exp_hidden_surface", 2},
    {"exp_gi_hit_sun", "exp_hidden_surface", 2},
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
inline constexpr KV kPresetExtra[kPresetCount][2] = {{}, {}, {}, {{"rt_rebuild_frames", "600"}}};

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
    {"ab_hotkey", "1"},
    {"ab_off", "0"},
    {"core", "1"},
    {"dump_shaders", "1"},
    {"shader_debug", "0"},
    {"census_dump", "0"},
    {"rr", "1"},
    {"rr_preset", "6"},
    {"rr_post", "1"},
    {"rr_cbt", "1"},
    {"rr_post_quantise", "1"},
    {"rr_post_check", "0"},
    {"rr_test_fail", "0"},
    {"rr_show", "0"},
    {"rr_guides_v2", "1"},
    {"rr_nov", "0.5"},
    {"rr_guides_at", "1"},
    {"rr_dump_gbuffer", "0"},
    {"dump_raw", "0"},
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
    {"rt_mesh_audits", "2"},
    {"rt_mesh_audit_frames", "600"},
    {"rt_vram_guard", "1"},
    {"rt_vram_guard_budget_mb", "0"},
    {"rt_vram_compact", "1"},
    {"rt_capture_elastic", "1"},
    {"rt_shadows", "1"},
    {"contact_shadows", "1"},
    {"rt_strength", "1.0"},
    {"rt_origin", "1"},
    {"rt_bias", "0.002"},
    {"rt_backlit", "1"},
    {"rt_tmax", "300"},
    {"rt_sun_radius", "0.00459"},
    {"rt_sun_cascade_current", "0"},
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
    {"rt_cascade_static_only", "0"},
    {"rt_cascade_index", "2"},
    {"rt_cascade_mb", "160"},
    {"rt_cascade_origin", "1"},
    {"rt_cascade_alpha", "2"},
    {"rt_cascade_alpha_dedupe", "1"},
    {"rt_cascade_refit", "1"},
    {"rt_cascade_rebuild_frames", "60"},
    {"rt_debug", "0"},
    {"rt_stats", "0"},
    {"pan_capture", "0"},
    {"pan_prepare", "0"},
    {"pan_inputs", "0"},
    {"replay_pack", "0"},
    {"replay_pack_cap_mb", "2048"},
    {"fg_input_copy", "1"},
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
    {"rt_water_rr_hitdist", "0"},
    {"rt_water_rf_edge", "0"},
    {"rt_water_rr_mv", "0"},
    {"rt_water_fg_mv", "0"},
    {"rt_water_glint_aa", "0"},
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
    {"rt_hit_material_wind_rgb", "0"},
    {"rt_hit_material_ip_grass", "0"},
    {"rt_hit_material_mask", "0"},
    {"rt_hit_material_pool_alpha", "1"},
    {"rt_hit_material_generated", "0"},
    {"rt_hit_reference", "0"},
    {"rt_omm", "0"},
    {"rt_omm_reference", "0"},
    {"rt_native_hair", "0"},
    {"rt_hair_physical_origin", "0"},
    {"rt_hair_receiver_guard", "0"},
    {"rt_alpha_unknown", "1"},
    {"rt_alpha_auto", "1"},
    {"rt_alpha_spill", "0"},
    {"rt_vt_alpha_masks", "0"},
    {"rt_alpha_fine_lod", "0"},
    {"rt_vt_alpha_miss_log", "0"},
    {"rt_alpha_max_pct", "60"},
    {"rt_strips", "1"},
    {"gi_rt", "1"},
    {"gi_rt_radius", "1.0"},
    {"gi_rt_depth_k", "1.0"},
    {"gi_rt_fallback", "0"},
    {"gi_hit_direct", "0"},
    {"gi_hit_sun", "0"},
    {"gi_far", "0"},
    {"gi_far_m", "6"},
    {"gi_temporal_mv_sample", "0"},
    {"gi_prev_radiance_guard", "0"},
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
    {"fg_tag_hudless", "1"},
    {"fg_tag_ui", "1"},
    {"fg_letterbox", "1"},
    {"fg_ui_pq", "0"},
};
// The keys of data/release.ini, in file order.
inline constexpr KV kReleaseDefaults[] = {
    {"dev", "0"},
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
    {"ls_box", "0"},
    {"ls_native_direct_validate", "0"},
    {"rf_debug", "0"},
    {"rf_stats", "0"},
    {"rf_validate", "0"},
    {"rr_post_check", "0"},
    {"dump_raw", "0"},
    {"rr_test_fail", "0"},
    {"rt_emissive", "0"},
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
    {"rt_hit_reference", "0"},
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
  if (key == "fg") return (std::atoi(from.c_str()) >= 2) != (std::atoi(to.c_str()) >= 2);
  // M5.3: the water FS variants exist only if the key was on when the pipelines were created.
  if (key == "rt_water_compose") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.4: likewise (the sun words of the record are patched in at creation).
  if (key == "rt_water_shadows") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.4b: likewise (the local-light words of the record are patched in at creation).
  if (key == "rt_water_ls_shadows") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.5: likewise (the refraction words of the record are patched in at creation).
  if (key == "rt_water_refraction") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // M5.4c: likewise (the game-shadow words of the record are patched in at creation).
  if (key == "rt_water_game_shadow") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // The instanced foliage rows patch the G-buffer VS at startup; off is live (layer/hit_material.cpp).
  if (key == "rt_hit_material_instanced") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  // Likewise the generated rows (common/native_hit_generated.h: their G-buffer VSs are patched at startup).
  if (key == "rt_hit_material_generated") return std::atoi(from.c_str()) == 0 && std::atoi(to.c_str()) != 0;
  static const char* const kRestart[] = {"rt", "rt_alpha", "geo_capture", "ngx_hook", "rt_xfb_mb", "rt_alpha_mb",
                                         "sharc_device", "rt_native_hair", "rt_native_tileset", "rt_omm", "rt_sun_cascade_current", "rt_hit_material",
                                         "rt_hit_material_wind_rgb"};
  for (const char* k : kRestart)
    if (key == k) return from != to;
  return false;
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

inline int choice_count(const Knob& k) {
  int n = 0;
  while (n < 4 && k.choices[n].id) ++n;
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

// key=value lines, '#' starts a comment (same rules the layer always had).
inline Map parse_ini(const std::string& text) {
  Map out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    std::string s = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? text.size() : nl + 1;
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
      if (values[i] != kKnobs[i].preset[p]) all = false;
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
    if (n.min_choice == 1) return std::string("needs ") + kKnobs[i].label + " on";
    return std::string("needs ") + kKnobs[i].label + " " + kKnobs[i].choices[n.min_choice].label;
  }
  return "";
}

inline std::string unmet_key_need(const Knob& k,int c,const Map& effective) {
  if(c<=0)return {};
  for(const auto& need:k.required_on)
    if(need.key && c>=need.min_choice && !same_value(lookup(effective,need.key),need.value))return std::string("needs ")+need.label;
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

inline bool choice_visible(const Knob& k, int c, bool dev) {
  return knob_visible(k, dev) && c >= 0 && c < choice_count(k) && (dev || !k.choices[c].dev);
}

inline bool choice_available(const Knob& k, int c, bool dev, bool experimental, unsigned caps) {
  return choice_visible(k, c, dev) && choice_supported(k, c, caps) &&
         (experimental || std::string(choice_maturity(k, c)) != "experimental");
}

// Pass these values to render_user_ini to preserve hidden/custom keys during an unrelated save.
inline std::vector<int> presentation_values(const Map& effective, bool dev) {
  auto values = knob_values(effective);
  for (int i = 0; i < kKnobCount; ++i)
    if (!knob_visible(kKnobs[i], dev) ||
        (kKnobs[i].kind == Kind::Choice && !choice_visible(kKnobs[i], values[i], dev))) values[i] = -1;
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
                                               bool dev = false, bool experimental = false) {
  DependencyPreview out{edited, {}, {}, {}};
  if (edited.size() != kKnobCount) { out.blocked.push_back("Invalid settings selection"); return out; }
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
          if (out.values[j] < 0 || !choice_available(kKnobs[j], n.min_choice, dev, experimental, caps)) {
            const std::string message = std::string("Review required setting: ") + kKnobs[j].label;
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
    if (!choice_supported(k, c, caps)) out.blocked.push_back(std::string("Unsupported: ") + k.label);
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
    for (auto& kv : forced) out += kv.first + "=" + kv.second + "\n";
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
    default: return "partial";
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
  static const char* const kCapNames[] = {"ray_query", "dlss", "fg", "mfg", "opacity_micromap"};
  for (int i = 0; i < kKnobCount; ++i) {
    const Knob& k = kKnobs[i];
    o += "    {\"id\": " + json_str(k.id) + ", \"label\": " + json_str(k.label) + ",\n     \"description\": " +
         json_str(k.desc) + ",\n     \"apply\": " + json_str(apply_name(k.apply)) + ", \"apply_note\": " +
         json_str(k.apply_note) + ", \"advanced\": " + (k.advanced ? "true" : "false") + ", \"experimental\": " + (k.experimental ? "true" : "false") + ", \"developer\": " + (k.dev_only ? "true" : "false") + ",\n";
    o += "     \"category\": " + json_str(k.category) + ", \"summary\": " + json_str(k.desc) + ",\n";
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
        o += "       {\"id\": " + json_str(ch.id) + ", \"label\": " + json_str(ch.label) + ", \"caps\": [";
        bool first = true;
        for (int b = 0; b < 5; ++b)
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
