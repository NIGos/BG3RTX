#pragma once
#include <array>
// GI shader requests and specialization constants (replaces the per-flag wrapper trees that were
// shaders/{native_tileset,hair_origin,omm,sun_current,native_scalp}/.../gi/{cascade/}).
// A request is "<file>?NAME=VALUE;NAME=VALUE": Bg3HostApi::get_shader (ABI 23) passes each
// pair to DXC as -D after BG3RTX/SHaRC, the cache key is the whole string, and hot reload
// recompiles it when anything under shaders/ changes. shaders/variants.txt lists every
// request the core can make (tests/shader_variant_test.cpp keeps it equal to these rules;
// tools/compile_shaders.py compiles all of them).
#include <cstring>
#include <cstdint>
#include <string>
#include "native_hair_origin.h"

namespace bg3::shader_variant {
// Variant bits of core/gi.cpp's Pipe::variants (1 was rt_scene_rebase). kSunCurrent selects
// a different Pipe (the current-caster sun), not a slot.
enum : unsigned { kCascade = 2, kScalp = 4, kOmm = 8, kHairOrigin = 16, kTileset = 32, kSunCurrent = 64, kHitInstanced = 128,
                  kGiFar = 256, kRfHitGi = 512, kHitWindRgb = 1024, kHitIpGrass = 2048, kHitPool2s = 4096, kOccStats = 1u << 13, kSpecCascade = 1u << 16 };
// kSpecCascade sits at bit 16; generated pool rows use bit 12 and the occ diagnostic uses bit 13.
// rt_spec_cascade (docs/BUILD_AND_PACKAGE_EFFICIENCY.md 1, S1): with kSpecCascade a cascade_entry file's request
// carries RT_SPEC_CASCADE=1 (rt_shadow.hlsl also RT_SPEC_CASCADE_SUN=1) instead of RT_CASCADE_MASKS (and
// RT_CASCADE_SUN), whatever kCascade says: kCascade then only sets specialization constant 1000
// (kSpecCascadeId, spec_cascade_value) at pipeline creation. Not with kSunCurrent (that pipe keeps its defines).
inline constexpr unsigned kSpecCascadeId = 1000;
// Specialization constant ids, one per on/off key (docs/BUILD_AND_PACKAGE_EFFICIENCY.md "Migration plan"): the one
// registry, so branches never pick the same id. Reserved for the steps that are not merged yet; an id is never reused.
enum SpecId : unsigned {
  kSpecIdCascade = kSpecCascadeId,   // GI and water cascade masks, including sun-ray selection
  kSpecIdTemporalMv = 1001,          // G   GI_TEMPORAL_MV_SAMPLE  (branch gi-reproj-guards)
  kSpecIdPrevRadiance = 1002,        // G   GI_PREV_RADIANCE_GUARD (branch gi-reproj-guards)
  kSpecIdHitInstanced = 1003,        // S2  RT_HIT_INSTANCED
  kSpecIdWindRgb = 1004,             // S2  RT_HIT_WIND_RGB
  kSpecIdIpGrass = 1005,             // S2  RT_HIT_IP_GRASS
  kSpecIdPool2s = 1006,              // S2  RT_HIT_POOL_2S (branch hm-recipe-rows)
  kSpecIdGiFar = 1007,               // S2  RT_GI_FAR
  kSpecIdTileset = 1008,             // S3  RT_NATIVE_TILESET
  kSpecIdHairOrigin = 1009,          // S3  RT_HAIR_PHYSICAL_ORIGIN
  kSpecIdScalpHair = 1010,           // S3  RT_NATIVE_SCALP + RT_NATIVE_HAIR (always together)
  kSpecIdOccStats = 1011,            // S3  RT_OCC_STATS (branch worktree-agent-a0c9be1ab336eae1a)
  kSpecIdOmmScene = 1012,            // S4  RT_OMM_SCENE
  kSpecIdRfHitGi = 1013,             // S5  RF_HIT_GI
  kSpecIdWaterRfEdge = 1014,         // S5  WATER_RF_EDGE
  kSpecIdWaterComposeShift = 1015,   // wf9 compose scene camera rebase
  kSpecWaterHitShade = 1016,         // water hidden-hit radiance (layout capability stays a define)
  kSpecIdHairCheap = 1018,           // rt_hair_cheap: physical secondary coverage
  kSpecIdHairGiMean = 1019,          // rt_hair_gi_mean: stored hard-coverage probability
  kSpecIdHairGiCull = 1017,          // rt_hair_gi_cull: GI closest rays only
  // 1021 reserved for water-hit-cost; do not reuse.
  kSpecIdCompactHitState = 1024,     // gpu-opt1: exact hit-state lifetime
  kSpecIdAlphaMetadataCache = 1022,  // gpu-opt1: query-local immutable alpha metadata
  kSpecIdWaterEmptyTiles = 1023,     // gpu-opt1: exact empty compose tiles
  kSpecIdHairGroom = 1020,           // rt_hair_groom: strand groom TLAS in the secondary queries (gi/ files)
};
// rt_hit_material_generated at startup with two-sided generated pool rows (the layer's capability word): RT_HIT_POOL_2S=1
// compiles the pool consumer's N flip for rows with h2.w = 5 (shaders/rt/native_hit_material.hlsli). Only in the Ultra-like
// instanced configuration (wind RGB on; no IP grass, OMM, native tileset, scalp or hair origin) to keep the request set
// small (+15 spec requests): elsewhere the define is dropped and those rows are rejected (fail closed, reason `row`).
// S3: full-box counters always bind set 4 b1; constant 1011 selects the diagnostic.
inline bool hair_gi_cull_entry(const char* file) { return std::strncmp(file, "gi/gi_trace_rt", 14) == 0; }
// Separate installed slots preserve both warmed cull states during same-run A/B.
inline constexpr unsigned kHairGiCullSlot = 256u, kHairCheapSlot = 512u, kHairGiMeanSlot = 1024u, kHairGroomSlot = 2048u, kCompactHitStateSlot = 4096u, kAlphaMetadataCacheSlot = 8192u, kPipelineSlots = 16384u;
inline unsigned hair_gi_cull_slot(const char* file, bool enabled) {
  return hair_gi_cull_entry(file) && enabled ? kHairGiCullSlot : 0u;
}
// rt_hair_groom (constant 1020 on every gi/ file): its own installed slots, so a live A/B toggles between two
// warmed pipelines instead of rebuilding (a pass waiting for its build is skipped).
inline unsigned hair_groom_slot(const char* file, bool enabled) {
  return std::strncmp(file, "gi/", 3) == 0 && enabled ? kHairGroomSlot : 0u;
}
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
// RF_SPEC_HIT_GI=1 (constant 1013: indirect diffuse and ls_box=2 box visibility at hits).
// Both values use the always-present set 6 and core/gi.cpp g_rf_shade_gi; the value joins the pipeline key.
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
      // the box (projector) passes (2026-11-02: without these they traced 0xFF and the F-1/F-2 cascade copy shadowed
      // local and box lights; rt_occ_stats "cascade" kind under local/box)
      "gi/rt_local_shadows_box.hlsl", "gi/rt_local_shadows_full_box.hlsl", "gi/rt_local_shadows_box_k.hlsl",
      "gi/rt_local_shadows_full_box_k.hlsl", "gi/rt_local_shadows_native_full_box.hlsl",
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
inline constexpr const char* kFixedRequests[] = {"rt/water_light_prepare.hlsl",
                                                 // rt_water_rr_hitdist (core/water.cpp): the reflection writes RR's hit distance
                                                 "rt/water_reflect.hlsl?WATER_RR_HIT=1",
                                                 // rt_water_rf_edge (core/water.cpp water_rf_edge_request): each compose
                                                 // record layout with constant 1014 selecting the screen-edge fade
                                                 "rt/water_compose.hlsl?WATER_SPEC_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_SPEC_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_SPEC_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_SPEC_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1",
                                                 "rt/water_reflect.hlsl?WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_reflect.hlsl?WATER_RR_HIT=1;WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_compose.hlsl?WATER_SPEC_RF_EDGE=1;WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_SPEC_RF_EDGE=1;WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_SPEC_RF_EDGE=1;WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_SPEC_RF_EDGE=1;WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1;WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1;WATER_HIT_SHADE_LAYOUT=1",
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1;WATER_HIT_SHADE_LAYOUT=1"};

// rt_water_rf_edge: both values share WATER_SPEC_RF_EDGE; core/water.cpp supplies constant 1014.
inline std::string water_rf_edge_request(const char* req, bool on) {
  std::string out = req;
  (void)on;
  if(out.find("WATER_SPEC_RF_EDGE=1") == std::string::npos)
    out += out.find('?') == std::string::npos ? "?WATER_SPEC_RF_EDGE=1" : ";WATER_SPEC_RF_EDGE=1";
  return out;
}

// Water uses the existing cascade constant 1000 for both states. The sun-hit marker
// describes the pass, never the toggle; only the sun ray sees cascade casters.
// Native hair (`scalp`: the layer's scalp capability, as kScalp for gi/): the scene's kind3 scalp and hair
// records, read through the shared alpha set 1 (RT_SPEC_NATIVE, constants 1008/1010), so the
// water rays see hair as the gi/ passes do. The physical hair origin (RT_HAIR_PHYSICAL_ORIGIN) is a
// launch-point image of the gi/ receivers (binding 50): water rays start on the water, so it adds nothing.
inline constexpr const char* kWaterBases[] = {"rt/water_reflect.hlsl", "rt/water_compose.hlsl"};
inline std::string water_hit_request(const char* req,bool capable) {
  std::string out=req;
  if(capable)out += out.find('?')==std::string::npos ? "?WATER_HIT_SHADE_LAYOUT=1" : ";WATER_HIT_SHADE_LAYOUT=1";
  return out;
}
inline std::string water_request(const char* req, bool cascade, bool scalp = false) {
  std::string out = req;
  (void)cascade;
  (void)scalp;
  if (out.compare(0, sizeof("rt/water_compose.hlsl") - 1, "rt/water_compose.hlsl") != 0 &&
      out.compare(0, sizeof("rt/water_reflect.hlsl") - 1, "rt/water_reflect.hlsl") != 0) return out;
  out += out.find('?') == std::string::npos ? "?WATER_SPEC_CASCADE=1" : ";WATER_SPEC_CASCADE=1";
  out += ";RT_SPEC_NATIVE=1";
  if (out.find("WATER_SHADOWS=1") != std::string::npos) out += ";RT_CASCADE_SUN_HIT=1";
  return out;
}

// The current-caster sun pipe (core/gi.cpp g_rt_shadow_current) runs this file only.
inline bool sun_current_entry(const char* file) { return std::strcmp(file, "gi/rt_shadow.hlsl") == 0; }

// Variant migration step G (docs/BUILD_AND_PACKAGE_EFFICIENCY.md "Migration plan"): gi_temporal_mv_sample and
// gi_prev_radiance_guard are specialization constants 1001 / 1002 (core/gi.cpp refresh), never request defines: their
// files always carry GI_SPEC_GUARDS=1. gi_temporal reads the motion vector at the rotating sample; the prev-radiance
// guard covers screen-space GI, every ray-traced GI trace and every D-RF file (shaders/gi/gi_prev_guard.hlsli).
inline bool temporal_mv_entry(const char* file) { return std::strcmp(file, "gi/gi_temporal.hlsl") == 0; }
inline bool prev_radiance_entry(const char* file) {
  return std::strcmp(file, "gi/gi_trace.hlsl") == 0 || std::strncmp(file, "gi/gi_trace_rt", 14) == 0 ||
         std::strncmp(file, "gi/rt_reflections", 17) == 0;
}
inline bool guard_entry(const char* file) { return temporal_mv_entry(file) || prev_radiance_entry(file); }

// The flags that may apply to `file` (scalp/OMM/tileset: every gi/ entry).
inline unsigned allowed(const char* file) {
  if (std::strncmp(file, "gi/", 3) != 0) return 0;
  unsigned f = kScalp | kOmm | kTileset;
  if (cascade_entry(file)) f |= kCascade | kSpecCascade;
  if (hair_origin::alternate_entry(file)) f |= kHairOrigin;
  if (sun_current_entry(file)) f |= kSunCurrent;
  if (hit_material_entry(file)) f |= kHitInstanced | kHitWindRgb | kHitIpGrass | kHitPool2s;
  if (gi_far_entry(file)) f |= kGiFar;
  if (rf_hit_gi_entry(file)) f |= kRfHitGi;
  if (occ_stats_entry(file)) f |= kOccStats;
  return f;
}

// Water supports native tilesets/scalp/hair via RT_SPEC_NATIVE (1008/1010).
// It traces the same-generation OMM reference TLAS with the host's superset alpha layout.
struct NoVariant { const char* key; const char* defines; };
inline constexpr std::array<NoVariant, 0> kWaterNoVariant{};

// The request string for `file` with `flags` (bits outside allowed(file) are ignored; the
// Hair origin needs the scalp). Defines are in the old wrapper nesting order, outer first,
// each name once: native markers, OMM, current sun, instanced, cascade, gi_far, then rf_hit_gi.
inline std::string request(const char* file, unsigned flags) {
  flags &= allowed(file);
  if (!(flags & kScalp)) flags &= ~unsigned(kHairOrigin);
  if (!(flags & kHitInstanced)) flags &= ~unsigned(kHitWindRgb | kHitIpGrass);
  // rt_spec_cascade is the only cascade path (qualified): every cascade pass but the current-caster sun gets the spec build.
  if (cascade_entry(file) && !(flags & kSunCurrent)) flags |= kSpecCascade;
  if (flags & kSunCurrent) flags &= ~unsigned(kSpecCascade);
  if (flags & kSpecCascade) flags &= ~unsigned(kCascade);   // the constant, not the request, carries it
  if (!(flags & kHitInstanced) || !(flags & kHitWindRgb) || (flags & (kHitIpGrass | kOmm | kTileset | kScalp | kHairOrigin)))
    flags &= ~unsigned(kHitPool2s);
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
  if (std::strncmp(file, "gi/", 3) == 0) add("RT_SPEC_NATIVE=1");
  if (hair_origin::alternate_entry(file)) add("RT_SPEC_HAIR_ORIGIN=1");
  if (flags & kOmm) add("RT_OMM_SCENE=1");
  if (flags & kSunCurrent) {
    add("RT_SUN_CURRENT=1");
    add("RT_CASCADE_SUN=1");
    add("RT_CASCADE_MASKS=1");
    add("RT_CASCADE_TMIN=0.0");
    add("RT_CASCADE_TMIN_REL=0.0");
  }
  // Steps S2a + S2c: rt_hit_material_instanced (1003), wind RGB (1004) and IP grass (1005) are constants of one module
  // per hit-material file (core/gi.cpp refresh): RT_SPEC_HIT_INST=1 always, never RT_HIT_INSTANCED or its sub-defines.
  if (hit_material_entry(file)) add("RT_SPEC_HIT_INST=1");
  if (flags & kHitPool2s) add("RT_HIT_POOL_2S=1");
  if (flags & kCascade) {
    add("RT_CASCADE_MASKS=1");
    if (std::strcmp(file, "gi/rt_shadow.hlsl") == 0) add("RT_CASCADE_SUN=1");  // the sun ray 0xFF
  }
  if (flags & kSpecCascade) {
    add("RT_SPEC_CASCADE=1");
    if (std::strcmp(file, "gi/rt_shadow.hlsl") == 0) add("RT_SPEC_CASCADE_SUN=1");
  }
  // Step S2b: gi_far is constant 1007 of the gi_hit_sun files (core/gi.cpp refresh), never RT_GI_FAR.
  if (gi_far_entry(file)) add("RT_SPEC_GI_FAR=1");
  if (rf_hit_gi_entry(file)) add("RF_SPEC_HIT_GI=1");
  if (occ_stats_entry(file)) add("RT_SPEC_OCC_STATS=1");
  if (guard_entry(file)) add("GI_SPEC_GUARDS=1");   // step G: always (the constants carry the two keys)
  return out;
}

// The value of specialization constant kSpecCascadeId for a request built with kSpecCascade: 1 when the
// pass would have had RT_CASCADE_MASKS (kCascade allowed for the file), else 0.
// Consumers of rt_common, including the reference passes outside the cascade set.
inline bool gpu_opt_entry(const char* file) {
  return cascade_entry(file) || std::strcmp(file, "gi/rt_hit_two_bounce.hlsl") == 0 ||
         std::strcmp(file, "gi/rt_local_shadows_match.hlsl") == 0 ||
         std::strcmp(file, "gi/rt_local_shadows_native_full.hlsl") == 0;
}

inline unsigned spec_cascade_value(const char* file, unsigned flags) {
  return (flags & kCascade) && (allowed(file) & kCascade) ? 1u : 0u;
}

}  // namespace bg3::shader_variant
