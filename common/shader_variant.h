#pragma once
#include <array>
// GI shader requests and specialization constants (replaces the per-flag wrapper trees that were
// shaders/{native_tileset,hair_origin,omm,native_scalp}/.../gi/{cascade/}).
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
// Variant bits of core/gi.cpp's Pipe::variants (1 was rt_scene_rebase; 64 retired).
enum : unsigned { kCascade = 2, kScalp = 4, kOmm = 8, kHairOrigin = 16, kTileset = 32, kHitInstanced = 128,
                  kGiFar = 256, kRfHitGi = 512, kHitWindRgb = 1024, kOccStats = 1u << 13, kSpecCascade = 1u << 16, kHairFibreGi = 1u << 18 };
// Bits 11/12 are retired; the occ diagnostic uses bit 13 and kSpecCascade bit 16.
// rt_spec_cascade (docs/BUILD_AND_PACKAGE_EFFICIENCY.md 1, S1): with kSpecCascade a cascade_entry file's request
// carries RT_SPEC_CASCADE=1 (rt_shadow.hlsl also RT_SPEC_CASCADE_SUN=1) instead of RT_CASCADE_MASKS (and
// RT_CASCADE_SUN), whatever kCascade says: kCascade then only sets specialization constant 1000
// (kSpecCascadeId, spec_cascade_value) at pipeline creation.
inline constexpr unsigned kSpecCascadeId = 1000;
// Specialization constant ids, one per on/off key (docs/BUILD_AND_PACKAGE_EFFICIENCY.md "Migration plan"): the one
// registry, so branches never pick the same id. Reserved for the steps that are not merged yet; an id is never reused.
enum SpecId : unsigned {
  // Reserved retired IDs: 1027 rt_origin_lazy_normal, 1028 gi_roof_reject. Never reuse.
  // Retired: 1001 GI motion sample, 1002 previous-radiance guard. Never reuse.
  kSpecIdCascade = kSpecCascadeId,   // GI and water cascade masks, including sun-ray selection
  kSpecIdHitInstanced = 1003,        // S2  RT_HIT_INSTANCED
  kSpecIdWindRgb = 1004,             // S2  RT_HIT_WIND_RGB
  // Retired: 1005 IP grass, 1006 generated two-sided pool rows. Never reuse.
  kSpecIdGiFar = 1007,               // S2  RT_GI_FAR
  kSpecIdTileset = 1008,             // S3  RT_NATIVE_TILESET
  kSpecIdHairOrigin = 1009,          // S3  RT_HAIR_PHYSICAL_ORIGIN
  kSpecIdScalpHair = 1010,           // S3  RT_NATIVE_SCALP + RT_NATIVE_HAIR (always together)
  kSpecIdOccStats = 1011,            // S3  RT_OCC_STATS (branch worktree-agent-a0c9be1ab336eae1a)
  kSpecIdOmmScene = 1012,            // S4  RT_OMM_SCENE
  kSpecIdRfHitGi = 1013,             // S5  RF_HIT_GI
  kSpecIdWaterRfEdge = 1014,         // S5  WATER_RF_EDGE
  kSpecIdWaterComposeShift = 1015,   // wf9 compose scene camera rebase
  // Retired: 1017 rt_hair_gi_cull, 1018 rt_hair_cheap, 1019 rt_hair_gi_mean. Never reuse.
  // Retired: 1016 water hit shading, 1021 water hit cost. Never reuse.
  // Retired: 1022 alpha metadata cache, 1023 empty water tiles, 1024 compact hit state. Never reuse.
  kSpecIdWaterSurfaceFresnel = 1025, // M5.6 clean interface / column partition
  kSpecIdHairGroomShadowT = 1026,    // C2: strand shadow transmittance, default pre-C2 opaque query
  // Hair stream B (branch claude/hair-m2-m4-light) takes ids from 1044 up (1040/1041 retired, 1042/1043 stream A).
  kSpecIdHairGroomAoT = 1044,        // M2.3: strand AO/GI ray transmittance, default opaque strands
  kSpecIdRfCompactProbeState = 1031, // RF: evaluate receiver probes after hit shading
  // Retired: 1041 rf_fast (paired saving, sustained replay exactness failed). Never reuse.
  // Retired: 1037 group-shared origin depth (no paired saving). Never reuse.
  // Retired 2026-11-05: 1032 lane permutation (slower), 1033 aux AO R16F, 1040 ls_fast (0.012 ms). Never reuse.
  // Retired 2026-11-05: 1034 deferred hidden-hit sun wave (slower). Never reuse.
  // Retired 2026-11-05: 1035 gi_far_roof_early, 1036 gi_far_roof_identity (measured losses). Never reuse.
  // Retired: 1039 gi_far_lean (exact, below occupancy target; paired timing losses). Never reuse.
  kSpecIdGiFarRoofEarly = 1038,     // exact far hidden-hit roof rejection; diagnostics force OFF
  kSpecIdCompactHitState = 1029,    // GI: retain the normal instead of the full hit across direct lighting
  kSpecIdHairGroom = 1020,           // rt_hair_groom: strand groom TLAS in the secondary queries (gi/ files)
  kSpecIdHairFibreMapped = 1030,     // M4.3 rt_hair_fibre_mapped: fibre response for mapped local lights (gi/hair_fibre.hlsl)
  kSpecIdHairGroomCoverage = 1042,   // M3.2 rt_hair_groom_coverage: two stratified strand rays per pixel (rt/hair_primary_gbuffer)
  kSpecIdRrHairGuides = 1043,        // M3.3 rr_hair_guides: class 5 RR guides (rr_guides.hlsl), metalness 0, fibre f0
  kSpecIdHairShadingSpace = 1045,    // 5g rt_hair_macro_normal / rt_hair_tangent_world: strand RT0/RT3 spaces (rt/hair_primary_gbuffer)
  kSpecIdHairFibreCheck = 1046,      // rt_hair_fibre=2: the fibre pass's in-frame consistency check (gi/hair_fibre.hlsl)
  // SER prototype (branch claude/opt-ser-gi): the raygen GI trace's reorder hint (GI_TRACE_RAYGEN only, offline tool
  // tools/gi_trace_rgen.hlsl; no game request). 1047-1049 left to the hair streams.
  kSpecIdGiSer = 1050,
};
// S3: full-box counters always bind set 4 b1; constant 1011 selects the diagnostic.
inline constexpr unsigned kHairGroomSlot = 2048u, kPipelineSlots = 32768u;
// rt_hair_groom (constant 1020 on every gi/ file): its own installed slots, so a live A/B toggles between two
// warmed pipelines instead of rebuilding (a pass waiting for its build is skipped).
inline unsigned hair_groom_slot(const char* file, bool enabled) {
  return std::strncmp(file, "gi/", 3) == 0 && enabled ? kHairGroomSlot : 0u;
}
inline bool occ_stats_entry(const char* file) { return std::strcmp(file, "gi/rt_local_shadows_full_box.hlsl") == 0; }

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
      "gi/sharc_debug.hlsl", "gi/sharc_inject.hlsl", "gi/sharc_inject_lock.hlsl", "gi/rt_occ_stats.hlsl",
      // rt_hair_fibre: the strand-pixel pass after D-LS traces the same local-light rays
      "gi/hair_fibre.hlsl",
      // rt_hair_fibre_spec: the strand-pixel pass after D-RF traces its reflection rays with D-RF's masks
      "gi/hair_fibre_spec.hlsl"};
  for (const char* n : names)
    if (std::strcmp(n, file) == 0) return true;
  return false;
}

// Fixed requests outside gi/ (core/water.cpp): M5.4's compose variant with the 40-byte records and
// the sun ray (shaders/rt/water_compose.hlsl WATER_SHADOWS; the plain file stays M5.3's), and M5.4b's
// with the 96-byte records and the local-light rays (WATER_LS on top of WATER_SHADOWS), and M5.5's with
// the 104-byte records and the refraction ray (WATER_REFR on top of both); M5.4c's WATER_GSH on top of each
// (the records + 8 bytes: the game's sun shadow replaced on the FS that sample it).
inline constexpr const char* kFixedRequests[] = {"rt/hair_primary_count.hlsl",
                                                 "rt/hair_primary_gbuffer.hlsl",
                                                 "rt/hair_primary_velocity.hlsl",
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
                                                 "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1"};

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


// The flags that may apply to `file` (scalp/OMM/tileset: every gi/ entry).
inline unsigned allowed(const char* file) {
  if (std::strncmp(file, "gi/", 3) != 0) return 0;
  unsigned f = kScalp | kOmm | kTileset;
  if (cascade_entry(file)) f |= kCascade | kSpecCascade;
  if (hair_origin::alternate_entry(file)) f |= kHairOrigin;
  if (hit_material_entry(file)) f |= kHitInstanced | kHitWindRgb;
  if (gi_far_entry(file)) f |= kGiFar;
  if (rf_hit_gi_entry(file)) f |= kRfHitGi;
  if (occ_stats_entry(file)) f |= kOccStats;
  if (std::strcmp(file, "gi/gi_composite.hlsl") == 0) f |= kHairFibreGi;
  return f;
}

// Water supports native tilesets/scalp/hair via RT_SPEC_NATIVE (1008/1010).
// It traces the same-generation OMM reference TLAS with the host's superset alpha layout.
struct NoVariant { const char* key; const char* defines; };
inline constexpr std::array<NoVariant, 0> kWaterNoVariant{};

// The request string for `file` with `flags` (bits outside allowed(file) are ignored; the
// Hair origin needs the scalp). Defines are in the old wrapper nesting order, outer first,
// each name once: native markers, OMM, instanced, cascade, gi_far, then rf_hit_gi.
inline std::string request(const char* file, unsigned flags) {
  flags &= allowed(file);
  if (!(flags & kScalp)) flags &= ~unsigned(kHairOrigin);
  if (!(flags & kHitInstanced)) flags &= ~unsigned(kHitWindRgb);
  // rt_spec_cascade is the only cascade path (qualified): every cascade pass gets the spec build.
  if (cascade_entry(file)) flags |= kSpecCascade;
  if (flags & kSpecCascade) flags &= ~unsigned(kCascade);   // the constant, not the request, carries it
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
  // Steps S2a + S2c: rt_hit_material_instanced (1003) and wind RGB (1004) are constants of one module
  // per hit-material file (core/gi.cpp refresh): RT_SPEC_HIT_INST=1 always, never RT_HIT_INSTANCED or its sub-defines.
  if (hit_material_entry(file)) add("RT_SPEC_HIT_INST=1");
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
  // Adds shaderInt64 and physical storage buffer reads: keep those capabilities out of the base request.
  if (flags & kHairFibreGi) add("RT_HAIR_FIBRE_GI_REFERENCE=1");
  return out;
}

// The value of specialization constant kSpecCascadeId for a request built with kSpecCascade: 1 when the
// pass would have had RT_CASCADE_MASKS (kCascade allowed for the file), else 0.
inline unsigned spec_cascade_value(const char* file, unsigned flags) {
  return (flags & kCascade) && (allowed(file) & kCascade) ? 1u : 0u;
}

}  // namespace bg3::shader_variant
