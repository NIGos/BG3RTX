#pragma once
// GI shader variants as compile-time defines (replaces the per-flag wrapper trees that were
// shaders/{native_tileset,hair_origin,omm,sun_current,native_scalp}/.../gi/{cascade/}).
// A request is "<file>?NAME=VALUE;NAME=VALUE": Bg3HostApi::get_shader (ABI 23) passes each
// pair to DXC as -D after BG3RTX/SHaRC, the cache key is the whole string, and hot reload
// recompiles it when anything under shaders/ changes. shaders/variants.txt lists every
// request the core can make (tests/shader_variant_test.cpp keeps it equal to these rules;
// tools/compile_shaders.py compiles all of them).
#include <cstring>
#include <string>
#include "native_hair_origin.h"

namespace bg3::shader_variant {

// Variant bits of core/gi.cpp's Pipe::variants (1 was rt_scene_rebase). kSunCurrent selects
// a different Pipe (the current-caster sun), not a slot.
enum : unsigned { kCascade = 2, kScalp = 4, kOmm = 8, kHairOrigin = 16, kTileset = 32, kSunCurrent = 64, kHitInstanced = 128,
                  kGiFar = 256, kRfHitGi = 512, kHitWindRgb = 1024, kHitIpGrass = 2048, kOccStats = 4096 };
// rt_occ_stats (common/occ_stats.h): RT_OCC_STATS=1 on the every-mode D-LS box pass only (its occluder
// counters; core/gi.cpp g_ls_full_box_occ, its own Pipe like kSunCurrent). The sun/AO part is its own
// file (gi/rt_occ_stats.hlsl), so no other request changes.
inline bool occ_stats_entry(const char* file) { return std::strcmp(file, "gi/rt_local_shadows_full_box.hlsl") == 0; }
// rt_hit_material_ip_grass at startup (with kHitInstanced only): RT_HIT_IP_GRASS=1 compiles the fourth instanced family's
// 3-bit family field and clip counters (rtStats 435..442); without it the instanced variants are unchanged.
// rt_hit_material_wind_rgb at startup (with kHitInstanced only): RT_HIT_WIND_RGB=1 compiles the third
// instanced family's clip counters (rtStats 427..434); without it the instanced variants are unchanged.

// rt_hit_material_instanced at startup (common/native_hit_instanced.h): the files that include
// rt/native_hit_material.hlsli get RT_HIT_INSTANCED=1 (RtHit.dir, the two-sided N column of row word
// 220). Without it they compile as before.
inline bool hit_material_entry(const char* file) {
  static const char* const names[] = {
      "gi/gi_trace_rt_hit_direct.hlsl", "gi/gi_trace_rt_hit_direct_em.hlsl", "gi/gi_trace_rt_hit_sun.hlsl",
      "gi/gi_trace_rt_hit_sun_em.hlsl", "gi/gi_trace_rt_sharc_hit_direct.hlsl", "gi/gi_trace_rt_sharc_hit_direct_em.hlsl",
      "gi/gi_trace_rt_sharc_hit_sun.hlsl", "gi/gi_trace_rt_sharc_hit_sun_em.hlsl", "gi/rt_hit_direct.hlsl",
      "gi/rt_hit_material.hlsl", "gi/rt_hit_two_bounce.hlsl", "gi/rt_reflections_hit_shade.hlsl",
      "gi/rt_reflections_hit_shade_em.hlsl", "gi/rt_reflections_sharc_hit_shade.hlsl",
      "gi/rt_reflections_sharc_hit_shade_em.hlsl"};
  for (const char* n : names)
    if (std::strcmp(n, file) == 0) return true;
  return false;
}

// gi_far (docs/TRACK_C_GI.md "Far-field sun bounce"): the gi_hit_sun files get RT_GI_FAR=1 (long GI rays,
// shaders/gi/gi_far.hlsli). The far field is sun bounce, so the direct-only files never get it.
inline bool gi_far_entry(const char* file) {
  static const char* const names[] = {"gi/gi_trace_rt_hit_sun.hlsl", "gi/gi_trace_rt_sharc_hit_sun.hlsl",
                                      "gi/gi_trace_rt_hit_sun_em.hlsl", "gi/gi_trace_rt_sharc_hit_sun_em.hlsl"};
  for (const char* n : names)
    if (std::strcmp(n, file) == 0) return true;
  return false;
}

// rf_hit_gi (docs/TRACK_D_REFLECTIONS.md 14 "Indirect diffuse at hits"): the rf_hit_shade files get
// RF_HIT_GI=1 (the screen's indirect diffuse and ls_box=2 box visibility at hits). Like kSunCurrent it
// selects other Pipes (core/gi.cpp g_rf_shade_gi), not a slot.
inline bool rf_hit_gi_entry(const char* file) {
  static const char* const names[] = {"gi/rt_reflections_hit_shade.hlsl", "gi/rt_reflections_sharc_hit_shade.hlsl",
                                      "gi/rt_reflections_hit_shade_em.hlsl", "gi/rt_reflections_sharc_hit_shade_em.hlsl"};
  for (const char* n : names)
    if (std::strcmp(n, file) == 0) return true;
  return false;
}

// rt_cascade_casters (docs/TRACK_D_RT.md 6.7 item 2): the RT passes that trace the cascade's
// instance masks. rt_sun_resolve traces nothing.
inline bool cascade_entry(const char* file) {
  static const char* const names[] = {
      "gi/gi_trace_rt.hlsl", "gi/gi_trace_rt_sharc.hlsl", "gi/gi_trace_rt_hit_direct.hlsl",
      "gi/gi_trace_rt_sharc_hit_direct.hlsl", "gi/gi_trace_rt_hit_sun.hlsl", "gi/gi_trace_rt_sharc_hit_sun.hlsl",
      // rt_hit_emission (the same passes plus the D-EM set): as their base files.
      "gi/gi_trace_rt_hit_direct_em.hlsl", "gi/gi_trace_rt_sharc_hit_direct_em.hlsl", "gi/gi_trace_rt_hit_sun_em.hlsl",
      "gi/gi_trace_rt_sharc_hit_sun_em.hlsl", "gi/rt_reflections_hit_shade_em.hlsl", "gi/rt_reflections_sharc_hit_shade_em.hlsl",
      "gi/pan_primary.hlsl", "gi/pan_primary_epoch.hlsl",
      "gi/pan_primary_identity.hlsl", "gi/pan_primary_fade.hlsl", "gi/pan_local.hlsl", "gi/rt_shadow.hlsl", "gi/rt_debug.hlsl",
      "gi/rt_hit_material.hlsl", "gi/rt_hit_direct.hlsl", "gi/rt_origin.hlsl", "gi/rt_local_shadows.hlsl",
      "gi/rt_local_shadows_full.hlsl", "gi/rt_local_shadows_ref.hlsl", "gi/rt_local_shadows_check.hlsl",
      "gi/rt_local_shadows_full_check.hlsl", "gi/rt_local_shadows_k.hlsl", "gi/rt_local_shadows_full_k.hlsl",
      "gi/rt_reflections.hlsl", "gi/rt_reflections_sharc.hlsl",
      "gi/rt_reflections_hit_shade.hlsl", "gi/rt_reflections_sharc_hit_shade.hlsl",
      "gi/sharc_debug.hlsl", "gi/sharc_inject.hlsl", "gi/sharc_inject_lock.hlsl", "gi/rt_occ_stats.hlsl"};
  for (const char* n : names)
    if (std::strcmp(n, file) == 0) return true;
  return false;
}

// Fixed requests outside gi/ (core/water.cpp): M5.4's compose variant with the 40-byte records and
// the sun ray (shaders/rt/water_compose.hlsl WATER_SHADOWS; the plain file stays M5.3's), and M5.4b's
// with the 96-byte records and the local-light rays (WATER_LS on top of WATER_SHADOWS), and M5.5's with
// the 104-byte records and the refraction ray (WATER_REFR on top of both); M5.4c's WATER_GSH on top of each
// (the records + 8 bytes: the game's sun shadow replaced on the FS that sample it).
inline constexpr const char* kFixedRequests[] = {"rt/water_compose.hlsl?WATER_SHADOWS=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_GSH=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_GSH=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1",
                                                 // rt_water_rr_hitdist (core/water.cpp): the reflection writes RR's hit distance
                                                 "rt/water_reflect.hlsl?WATER_RR_HIT=1",
                                                 // rt_water_rf_edge (core/water.cpp water_rf_edge_request): each compose
                                                 // request above with the screen-edge fade of the traced term
                                                 "rt/water_compose.hlsl?WATER_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_GSH=1;WATER_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_GSH=1;WATER_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1;WATER_RF_EDGE=1"};

// rt_water_rf_edge: the compose request `req` with the screen-edge fade (WATER_RF_EDGE) when `on`.
inline std::string water_rf_edge_request(const char* req, bool on) {
  std::string out = req;
  if (on) out += out.find('?') == std::string::npos ? "?WATER_RF_EDGE=1" : ";WATER_RF_EDGE=1";
  return out;
}

// rt_cascade_casters for the water passes (core/water.cpp): the TLAS holds the cascade's casters
// (mask 0x02), so the reflection, refraction and local-light rays trace 0x01 (RT_CASCADE_MASKS) and the
// compose's sun ray (WATER_SHADOWS) sees them too (RT_CASCADE_SUN_HIT: g_rt_cascade_ray around it),
// as gi_hit_sun's sun ray at a GI hit. Off: the request unchanged.
inline constexpr const char* kWaterBases[] = {"rt/water_reflect.hlsl", "rt/water_compose.hlsl"};
inline std::string water_request(const char* req, bool cascade) {
  std::string out = req;
  if (!cascade) return out;
  out += out.find('?') == std::string::npos ? "?RT_CASCADE_MASKS=1" : ";RT_CASCADE_MASKS=1";
  if (out.find("WATER_SHADOWS=1") != std::string::npos) out += ";RT_CASCADE_SUN_HIT=1";
  return out;
}

// The current-caster sun pipe (core/gi.cpp g_rt_shadow_current) runs this file only.
inline bool sun_current_entry(const char* file) { return std::strcmp(file, "gi/rt_shadow.hlsl") == 0; }

// The flags that may apply to `file` (scalp/OMM/tileset: every gi/ entry).
inline unsigned allowed(const char* file) {
  if (std::strncmp(file, "gi/", 3) != 0) return 0;
  unsigned f = kScalp | kOmm | kTileset;
  if (cascade_entry(file)) f |= kCascade;
  if (hair_origin::alternate_entry(file)) f |= kHairOrigin;
  if (sun_current_entry(file)) f |= kSunCurrent;
  if (hit_material_entry(file)) f |= kHitInstanced | kHitWindRgb | kHitIpGrass;
  if (gi_far_entry(file)) f |= kGiFar;
  if (rf_hit_gi_entry(file)) f |= kRfHitGi;
  if (occ_stats_entry(file)) f |= kOccStats;
  return f;
}

// The keys under which the water passes (core/water.cpp) have no variant and skip: each changes the
// alpha set or the scene's primitives that the gi/ passes follow with refresh()'s flags. Counted as
// "variant" in the [water] reflections line and logged "[water] reflections skipped: no variant for
// <keys>" (the convention for every such skip; smoke_ultra forbids "skipped: no variant for").
// `defines`: what a water variant would add. tests/variant_matrix_test.cpp builds those requests
// (tools/shader_identity.py compiled all 24 on 2026-11-01), so routing them is the fix. A key added
// here without updating that test's expected skips fails it.
struct NoVariant { const char* key; const char* defines; };
inline constexpr NoVariant kWaterNoVariant[] = {
    {"rt_omm", "RT_OMM_SCENE=1"},
    {"rt_native_tileset", "RT_NATIVE_TILESET=1"},
    {"rt_native_scalp", "RT_NATIVE_SCALP=1;RT_NATIVE_HAIR=1"},
    {"rt_native_hair", "RT_NATIVE_SCALP=1;RT_NATIVE_HAIR=1"},
    {"rt_hair_physical_origin", "RT_HAIR_PHYSICAL_ORIGIN=1;RT_NATIVE_SCALP=1;RT_NATIVE_HAIR=1"}};

// The request string for `file` with `flags` (bits outside allowed(file) are ignored; the
// Hair origin needs the scalp). Defines are in the old wrapper nesting order, outer first,
// each name once: tileset, Hair origin, OMM, current sun, scalp, instanced, cascade, gi_far, rf_hit_gi, then rt_occ_stats.
inline std::string request(const char* file, unsigned flags) {
  flags &= allowed(file);
  if (!(flags & kScalp)) flags &= ~unsigned(kHairOrigin);
  if (!(flags & kHitInstanced)) flags &= ~unsigned(kHitWindRgb | kHitIpGrass);
  std::string out = file;
  char sep = '?';
  auto add = [&](const char* def) {
    std::string name(def, std::strchr(def, '=') - def);
    for (size_t i = out.find('?'); i != std::string::npos; i = out.find(';', i + 1)) {
      size_t a = i + 1, b = out.find('=', a);
      if (out.compare(a, b - a, name) == 0) return;
    }
    out += sep;
    out += def;
    sep = ';';
  };
  if (flags & kTileset) add("RT_NATIVE_TILESET=1");
  if (flags & kHairOrigin) add("RT_HAIR_PHYSICAL_ORIGIN=1");
  if (flags & kOmm) add("RT_OMM_SCENE=1");
  if (flags & kSunCurrent) {
    add("RT_SUN_CURRENT=1");
    add("RT_CASCADE_SUN=1");
    add("RT_CASCADE_MASKS=1");
    add("RT_CASCADE_TMIN=0.0");
    add("RT_CASCADE_TMIN_REL=0.0");
  }
  if (flags & kScalp) {
    add("RT_NATIVE_SCALP=1");
    add("RT_NATIVE_HAIR=1");
  }
  if (flags & kHitInstanced) add("RT_HIT_INSTANCED=1");
  if (flags & kHitWindRgb) add("RT_HIT_WIND_RGB=1");
  if (flags & kHitIpGrass) add("RT_HIT_IP_GRASS=1");
  if (flags & kCascade) {
    add("RT_CASCADE_MASKS=1");
    if (std::strcmp(file, "gi/rt_shadow.hlsl") == 0) add("RT_CASCADE_SUN=1");  // the sun ray 0xFF
  }
  if (flags & kGiFar) add("RT_GI_FAR=1");
  if (flags & kRfHitGi) add("RF_HIT_GI=1");
  if (flags & kOccStats) add("RT_OCC_STATS=1");
  return out;
}

}  // namespace bg3::shader_variant
