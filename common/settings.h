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
    "RTX 4080/4090, 5080. Everything ray traced at the developer defaults (today's bg3rtx.ini).",
    "RTX 5090 at 4K-class output. High plus 2 GI rays, shadowed lights, rougher reflections and a 58 fps "
    "rendered cap (116 displayed with frame generation 2x).",
};

// ---- hardware capabilities a choice needs ----

enum Cap : unsigned {
  kCapRayQuery = 1u << 0,   // VK_KHR_ray_query + VK_KHR_acceleration_structure
  kCapDlss = 1u << 1,       // NVIDIA RTX (DLSS SR/RR): Ray Reconstruction
  kCapFg = 1u << 2,         // RTX 40 and later: DLSS-G 2x
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
  KV kv[5];        // keys this choice writes (key == nullptr ends the list)
  Need need[2];    // dependencies on other knobs (knob == nullptr ends the list)
};

enum class Kind { Choice, Slider };

struct Slider {
  int min, max, step;
  bool has_off;        // value 0 means "Off" (outside min..max)
  float scale;         // key value = slider value * scale
  const char* unit;
  const char* keys[3]; // all set to the same value
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
};

// clang-format off
inline constexpr Knob kKnobs[] = {
  {"rr", "Ray Reconstruction",
   "DLSS Ray Reconstruction replaces the game's DLSS upscaler and denoises the ray-traced effects. "
   "Needs an NVIDIA RTX GPU and the game's upscaler set to DLSS. Off: DLSS Super Resolution, and the "
   "effects switch to their non-denoised paths. About +1.8 ms on an RTX 5090.",
   Kind::Choice, Apply::Hitch, "brief hitch (history reset)", false,
   {{"off", "Off", 0, {{"rr", "0"}}, {}},
    {"on", "On", kCapDlss, {{"rr", "1"}}, {}}},
   {}, {0, 1, 1, 1}},

  {"rt_geometry", "Ray-traced geometry",
   "Builds the game's geometry into a ray-tracing scene every frame. Every ray-traced effect below needs it. "
   "Static meshes are built once and cached, so the update costs about 0.5 ms on an RTX 5090, panning or not "
   "(it was 1.4-2.9 ms while the camera moved). Opaque leaves alpha-tested casters (grates, foliage, chains) out. "
   "Off frees that cost and the geometry capture.",
   Kind::Choice, Apply::Partial, "Opaque <-> Full: restart", false,
   {{"off", "Off", 0, {{"rt_build", "0"}, {"rt_alpha", "0"}}, {}},
    {"opaque", "Opaque only", kCapRayQuery, {{"rt_build", "auto"}, {"rt_alpha", "0"}}, {}},
    {"full", "Full (with alpha-tested)", kCapRayQuery, {{"rt_build", "auto"}, {"rt_alpha", "1"}}, {}}},
   {}, {0, 1, 2, 2}},

  {"sun_shadows", "Sun shadows",
   "Ray-traced sun shadows replace the game's shadow-map detail; screen-space keeps the game's sun shadows "
   "with contact shadows. About 0.1 ms.",
   Kind::Choice, Apply::Live, "", false,
   {{"ss", "Screen-space", 0, {{"rt_shadows", "0"}, {"contact_shadows", "1"}}, {}},
    {"rt", "Ray-traced", kCapRayQuery, {{"rt_shadows", "1"}, {"contact_shadows", "1"}}, {{"rt_geometry", 1}}}},
   {}, {0, 1, 1, 1}},

  {"gi", "Global illumination",
   "One bounce of indirect light and ambient occlusion. Screen-space traces the depth buffer; ray-traced "
   "traces the scene. RT High uses 2 rays per pixel (less noise in dim interiors, about +0.2 ms).",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"gi", "0"}}, {}},
    {"ss", "Screen-space", 0, {{"gi", "1"}, {"gi_rt", "0"}, {"gi_rays", "2"}, {"gi_steps", "12"}}, {}},
    {"rt", "Ray-traced", kCapRayQuery, {{"gi", "1"}, {"gi_rt", "1"}, {"gi_rr_rays", "1"}}, {{"rt_geometry", 1}}},
    {"rt_high", "Ray-traced High", kCapRayQuery, {{"gi", "1"}, {"gi_rt", "1"}, {"gi_rr_rays", "2"}}, {{"rt_geometry", 1}}}},
   {}, {1, 2, 2, 3}},

  {"local_shadows", "Local light shadows",
   "Ray-traced shadows for torches, lamps and spells, so lights without a shadow map stop leaking through "
   "walls. Fast samples 1 light per pixel (noisier, needs Ray Reconstruction). Full also traces the lights "
   "that have a game shadow map, keeping the game's map only where a caster may be off screen. Full+ "
   "replaces those maps everywhere (off-screen casters can be lost). About 0.2-0.4 ms.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"rt_local_shadows", "0"}}, {}},
    {"fast", "Fast", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "1"}, {"ls_shadowed", "0"}}, {{"rt_geometry", 1}}},
    {"full", "Full", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "0"}, {"ls_shadowed", "3"}}, {{"rt_geometry", 1}}},
    {"full_plus", "Full + shadowed lights", kCapRayQuery, {{"rt_local_shadows", "1"}, {"ls_samples", "0"}, {"ls_shadowed", "1"}}, {{"rt_geometry", 1}}}},
   {}, {0, 1, 2, 2}},

  {"reflections", "Reflections",
   "Ray-traced reflections replace the game's reflection probes where the reflected point is on screen. "
   "Higher settings reach rougher surfaces. Without Ray Reconstruction only mirror-like surfaces are traced. "
   "About 0.15-0.45 ms.",
   Kind::Choice, Apply::Live, "", false,
   {{"off", "Off", 0, {{"rt_reflections", "0"}}, {}},
    {"glossy", "Glossy", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.4"}}, {{"rt_geometry", 1}, {"gi", 1}}},
    {"standard", "Standard", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.6"}}, {{"rt_geometry", 1}, {"gi", 1}}},
    {"rough", "Rough", kCapRayQuery, {{"rt_reflections", "1"}, {"rf_max_rough", "0.8"}}, {{"rt_geometry", 1}, {"gi", 1}}}},
   {}, {0, 1, 2, 3}},

  {"frame_generation", "Frame generation",
   "DLSS Frame Generation: 2x on RTX 40 and later, 3x/4x on RTX 50. Needs Reflex.",
   Kind::Choice, Apply::Partial, "Off <-> on: restart", false,
   {{"off", "Off", 0, {{"fg", "0"}}, {}},
    {"2x", "2x", kCapFg, {{"fg", "2"}}, {}},
    {"3x", "3x", kCapMfg, {{"fg", "3"}}, {}},
    {"4x", "4x", kCapMfg, {{"fg", "4"}}, {}}},
   {}, {1, 1, 1, 1}},

  {"rt_intensity", "RT lighting intensity",
   "Scales the ray-traced bounce light, local-light shadows and reflections together. 100% is physically "
   "matched to the game; lower blends towards the original look.",
   Kind::Slider, Apply::Live, "", false,
   {}, {50, 100, 5, false, 0.01f, "%", {"gi_strength", "ls_strength", "rf_strength"}}, {100, 100, 100, 100}},

  {"fps_cap", "Frame cap (rendered fps)",
   "Caps the rendered frame rate (with frame generation 2x the display shows twice as many). Through the "
   "Reflex limiter when frame generation is on, a sleep limiter otherwise.",
   Kind::Slider, Apply::Live, "", false,
   {}, {30, 240, 1, true, 1.0f, "fps", {"fps_cap"}}, {0, 0, 0, 58}},

  {"reflex", "NVIDIA Reflex",
   "Low-latency mode. Frame generation needs it on.",
   Kind::Choice, Apply::Live, "", true,
   {{"off", "Off", 0, {{"reflex", "0"}}, {}},
    {"on", "On", 0, {{"reflex", "1"}}, {}},
    {"boost", "On + Boost", 0, {{"reflex", "2"}}, {}}},
   {}, {1, 1, 1, 1}},

  // Experimental: features being tested, off in every preset (docs/PROGRESS_CODEX.md says
  // what to look at). Live: toggling them in game takes effect at once.
  {"exp_roof_shadows", "Roof sun shadows",
   "Captures the roofs the camera cuts away (from the game's sun shadow map) into the ray-traced scene, "
   "so ray-traced sun shadows also work under them. Look for: light through cut roofs (bad), sharper roof "
   "shadow edges (good). About +1.7 ms.",
   Kind::Choice, Apply::Live, "experimental", true,
   {{"off", "Off", 0, {{"rt_cascade_casters", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"rt_cascade_casters", "1"}}, {{"rt_geometry", 1}, {"sun_shadows", 1}}}},
   {}, {0, 0, 0, 0}, true},

  {"exp_box_lights", "Box light shadows",
   "Ray-traced shadows for the game's box (projector) lights, 37% of its placed lights. Look at lit "
   "interiors: shadows of furniture and characters from these lights. About +0.25 ms.",
   Kind::Choice, Apply::Live, "experimental", true,
   {{"off", "Off", 0, {{"ls_box", "0"}}, {}},
    {"on", "On", kCapRayQuery, {{"ls_box", "1"}}, {{"rt_geometry", 1}, {"local_shadows", 1}}}},
   {}, {0, 0, 0, 0}, true},
};
// clang-format on
inline constexpr int kKnobCount = int(sizeof kKnobs / sizeof kKnobs[0]);

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
// 209 keys of bg3rtx.ini, in file order.
inline constexpr KV kDefaults[] = {
    {"dev", "1"},
    {"ab_hotkey", "1"},
    {"core", "1"},
    {"dump_shaders", "1"},
    {"shader_debug", "0"},
    {"census_dump", "0"},
    {"rr", "1"},
    {"rr_preset", "6"},
    {"rr_post", "1"},
    {"rr_cbt", "1"},
    {"rr_post_quantise", "1"},
    {"rr_pre_exposure", "0"},
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
    {"geo_xfb_always", "0"},
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
    {"rt_fast_trace", "0"},
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
    {"rt_sun_top_pad", "2.0"},
    {"rt_sun_top", "0"},
    {"rt_sun_margin", "0.0"},
    {"rt_sun_ramp", "0.75"},
    {"rt_sun_near", "0"},
    {"rt_cascade_casters", "0"},
    {"rt_cascade_index", "2"},
    {"rt_cascade_mb", "160"},
    {"rt_cascade_roofs_trust", "0"},
    {"rt_cascade_origin", "1"},
    {"rt_cascade_alpha", "2"},
    {"rt_cascade_alpha_dedupe", "1"},
    {"rt_cascade_refit", "1"},
    {"rt_cascade_rebuild_frames", "60"},
    {"rt_ao", "0"},
    {"rt_ao_rays", "2"},
    {"rt_ao_radius", "1.0"},
    {"rt_debug", "0"},
    {"rt_stats", "0"},
    {"pan_capture", "0"},
    {"pan_prepare", "0"},
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
    {"rt_em_lava_heat", "0.5"},
    {"rt_em_log", "0"},
    {"rt_em_dbg_fam_hi", "0"},
    {"rt_em_dbg_fam_lo", "0"},
    {"rt_em_tol", "0.01"},
    {"rt_em_filter", "0"},
    {"rt_em_aniso", "16"},
    {"rt_em_fx_thr", "0.05"},
    {"rt_em_backdrop", "0"},
    {"rt_em_decal", "1"},
    {"rt_alpha", "1"},
    {"rt_prepass_by_ordinal", "0"},
    {"rt_alpha_test", "1"},
    {"rt_alpha_cut", "1"},
    {"rt_alpha_stochastic", "1"},
    {"rt_alpha_unknown", "1"},
    {"rt_alpha_auto", "1"},
    {"rt_alpha_max_pct", "60"},
    {"rt_strips", "1"},
    {"gi_rt", "1"},
    {"gi_rt_radius", "1.0"},
    {"gi_rt_depth_k", "1.0"},
    {"gi_rt_fallback", "0"},
    {"gi_bounce_spec", "0"},
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
    {"ls_box_unshadowed", "0"},
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
    {"rf_so", "0"},
    {"rf_models", "3"},
    {"rf_debug", "0"},
    {"rf_stats", "0"},
    {"rf_validate", "0"},
    {"rr_spec_hit_distance", "1"},
    {"fg", "2"},
    {"reflex", "1"},
    {"fps_cap", "0"},
    {"se_unhook", "1"},
    {"rt_scene_camera_match", "0"},
    {"rt_scene_rebase", "0"},
    {"rt_order_trace", "0"},
    {"rt_scene_wait", "1"},
    {"cpu_timing", "0"},
    {"rt_stability_log", "0"},
    {"rt_capture_state_log", "0"},
    {"rt_mesh_draw_log", "0"},
    {"rt_mesh_capture", "0"},
    {"rt_capture_depth_write", "0"},
    {"rt_capture_discard", "1"},
    {"rt_dyn_hold", "4"},
    {"rt_dyn_hold_max_prims", "8192"},
    {"rt_scene_wait_ms", "12"},
    {"rt_primary_check", "0"},
    {"rt_primary_epoch", "0"},
    {"rt_primary_identity", "0"},
    {"crash_dump", "0"},
    {"fg_current_destroy_only", "1"},
    {"fg_tag_hudless", "1"},
    {"fg_tag_ui", "1"},
    {"fg_ui_pq", "0"},
};
// 24 keys of data/release.ini, in file order.
inline constexpr KV kReleaseDefaults[] = {
    {"dev", "0"},
    {"dump_shaders", "0"},
    {"shader_debug", "0"},
    {"census_dump", "0"},
    {"geo_capture", "0"},
    {"geo_timing", "0"},
    {"geo_xfb_always", "0"},
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
  static const char* const kRestart[] = {"rt", "rt_alpha", "geo_capture", "ngx_hook", "rt_xfb_mb", "rt_alpha_mb",
                                         "sharc_device"};
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

inline const Knob* find_knob(const std::string& id) {
  for (const Knob& k : kKnobs)
    if (id == k.id) return &k;
  return nullptr;
}

inline int knob_index(const std::string& id) {
  for (int i = 0; i < kKnobCount; ++i)
    if (id == kKnobs[i].id) return i;
  return -1;
}

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
  bool consumer = map_int(m, "rt_shadows", 0) || map_int(m, "rt_ao", 0) || map_int(m, "gi_rt", 0) ||
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
    if (values[i] < 0 || values[i] >= n.min_choice) continue;
    if (n.min_choice == 1) return std::string("needs ") + kKnobs[i].label + " on";
    return std::string("needs ") + kKnobs[i].label + " " + kKnobs[i].choices[n.min_choice].label;
  }
  return "";
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

// Values of preset `p` limited to what the hardware supports.
inline std::vector<int> preset_values_for(int p, unsigned caps) {
  std::vector<int> v = preset_values(p);
  for (int i = 0; i < kKnobCount; ++i) v[i] = clamp_choice(kKnobs[i], v[i], caps);
  return v;
}

struct Gpu {
  unsigned vendor = 0;      // PCI vendor: 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
  unsigned device = 0;      // PCI device id
  std::string name;
  bool ray_query = false;   // VK_KHR_ray_query + VK_KHR_acceleration_structure
  bool discrete = false;
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
  static const char* const kCapNames[] = {"ray_query", "dlss", "fg", "mfg"};
  for (int i = 0; i < kKnobCount; ++i) {
    const Knob& k = kKnobs[i];
    o += "    {\"id\": " + json_str(k.id) + ", \"label\": " + json_str(k.label) + ",\n     \"description\": " +
         json_str(k.desc) + ",\n     \"apply\": " + json_str(apply_name(k.apply)) + ", \"apply_note\": " +
         json_str(k.apply_note) + ", \"advanced\": " + (k.advanced ? "true" : "false") + ",\n";
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
        for (int b = 0; b < 4; ++b)
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
        o += std::string("}}") + (c + 1 < n ? "," : "") + "\n";
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
