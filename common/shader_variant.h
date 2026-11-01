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
enum : unsigned { kCascade = 2, kScalp = 4, kOmm = 8, kHairOrigin = 16, kTileset = 32, kSunCurrent = 64, kHitInstanced = 128 };

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
      "gi/sharc_debug.hlsl", "gi/sharc_inject.hlsl", "gi/sharc_inject_lock.hlsl"};
  for (const char* n : names)
    if (std::strcmp(n, file) == 0) return true;
  return false;
}

// Fixed requests outside gi/ (core/water.cpp): M5.4's compose variant with the 40-byte records and
// the sun ray (shaders/rt/water_compose.hlsl WATER_SHADOWS; the plain file stays M5.3's), and M5.4b's
// with the 96-byte records and the local-light rays (WATER_LS on top of WATER_SHADOWS), and M5.5's with
// the 104-byte records and the refraction ray (WATER_REFR on top of both).
inline constexpr const char* kFixedRequests[] = {"rt/water_compose.hlsl?WATER_SHADOWS=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1"};

// The current-caster sun pipe (core/gi.cpp g_rt_shadow_current) runs this file only.
inline bool sun_current_entry(const char* file) { return std::strcmp(file, "gi/rt_shadow.hlsl") == 0; }

// The flags that may apply to `file` (scalp/OMM/tileset: every gi/ entry).
inline unsigned allowed(const char* file) {
  if (std::strncmp(file, "gi/", 3) != 0) return 0;
  unsigned f = kScalp | kOmm | kTileset;
  if (cascade_entry(file)) f |= kCascade;
  if (hair_origin::alternate_entry(file)) f |= kHairOrigin;
  if (sun_current_entry(file)) f |= kSunCurrent;
  if (hit_material_entry(file)) f |= kHitInstanced;
  return f;
}

// The request string for `file` with `flags` (bits outside allowed(file) are ignored; the
// Hair origin needs the scalp). Defines are in the old wrapper nesting order, outer first,
// each name once: tileset, Hair origin, OMM, current sun, scalp, cascade.
inline std::string request(const char* file, unsigned flags) {
  flags &= allowed(file);
  if (!(flags & kScalp)) flags &= ~unsigned(kHairOrigin);
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
  if (flags & kCascade) {
    add("RT_CASCADE_MASKS=1");
    if (std::strcmp(file, "gi/rt_shadow.hlsl") == 0) add("RT_CASCADE_SUN=1");  // the sun ray 0xFF
  }
  return out;
}

}  // namespace bg3::shader_variant
