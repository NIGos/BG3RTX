#pragma once
// M5.1 water POLICY (docs/PLAN_M5_WATER.md, rt_water_geometry): which forward draws are
// water surfaces the layer captures, and the per-draw record. Pure functions: no Vulkan, no
// globals. They run in the hot-reloaded core (core/em_policy.cpp, through
// Bg3CoreApi::em_record with Bg3EmRecordIn::forward == BG3_EM_REQ_WATER and decal = VS hash >> 32:
// no ABI change), so
// a rule or a new audited variant needs only a core rebuild. The layer (layer/water.cpp)
// keeps every Vulkan object: the soup, the conversion, the BLAS/TLAS and the counters.
// tests/water_policy_test.cpp pins the rules on the asset-DB families; tests/
// emissive_core_test.cpp the core route.
//
// Evidence (2026-11-01, offline: asset DB, captures/shaders, captures/alpha_cache, the ten
// traces of 2026-10-30):
// - Every traced forward water plane is a StaticInstancedForward FLUID_Water_Plane* variant
//   drawn in the forward pass (one B10G11R11 target + the D24S8 depth, render resolution).
// - Their vertex shaders displace the vertex in world Y only (FLUID_* world_position_offset:
//   a flowmap at the mesh UV and a scrolling noise at world xz * m10 + time * m11, set 1 b1),
//   then write gl_Position = camera-relative world * set 0 b2 _m2, the G-buffer convention:
//   the XFB capture records the displaced (animated) positions and rt_convert recovers them
//   with the pass's camera UBO, as for the emissive soup. FLUID_Water_Plane (68068cab) has
//   no displacement. The normals are not recomputed from the displacement (vertex tangent
//   frame only): the shading normal is the FS's (flow-mapped normal maps), not XFB's.
// - Depth: no traced water FS writes gl_FragDepth, and the D24S8 attachment is bit-identical
//   before and after every water-only forward pass (S1 docks and five other views; DB
//   DepthStateID 6 for these templates): water is depth-tested, not depth-written. The
//   G-buffer depth under water is the bed's.
//
// Reader concept (all members required):
//   bool read(uint32_t set, uint32_t binding, uint64_t offset, void* dst, size_t n);
#include "assetdb_api.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bg3::water {

enum Class : uint32_t {
  kNotWater = 0,      // not a water material (the host ignores the draw)
  kSupported = 1,     // an audited FS/VS pair: captured
  kUnknown = 2,       // another forward Fluids/ water variant (not in kAudited): counted, not captured
  kExcludedFlow = 3,  // falls, mist, foam, magic bridges (alpha-blended volumes): counted
  kExcludedVfx = 4,   // VFX with the water flag (rivers, ocean VFX planes): counted
  kClassCount = 5
};

// One audited forward water pipeline: the FS and the VS it was traced with.
struct Audited {
  uint64_t fs, vs;
  const char* name;   // the template, below Fluids/
};
// Traced forward water draws (captures 20261030-1527xx = S1 docks, -1618xx..-1630xx), each VS
// checked: displacement in Y only, gl_Position = camera-relative world * set 0 b2 _m2.
inline constexpr Audited kAudited[] = {
    {0x7b44a567771ecbecull, 0x60d4357d7b95d027ull, "FLUID_Water_Plane_flowmap_huge_PixelLighting"},
    {0x1d4bff2e04a42c4cull, 0x824dcd7bda34c184ull, "FLUID_Water_Plane_flowmap"},
    {0xfda628a5f89e5b6eull, 0xb02e4616e698ab88ull, "FLUID_Water_Plane_flowmap_PixelLighting"},
    {0x64d9cb772d595ce4ull, 0xf049191991bccdc6ull, "FLUID_Water_Plane_flowmap_huge_PixelLighting_V2"},
    {0x06e66a6fbd88a6b3ull, 0x7a485e6aa7dac575ull, "FLUID_Water_Plane_flowmap_PixelLighting_noCaustics_Shadows"},
    {0x857ccb90efda19c3ull, 0xffc5bb290d99c144ull, "FLUID_Water_Plane_flowmap_noCaustics"},
    {0x68068cab0ff17c8dull, 0xbbdf7b8a2aa13eabull, "FLUID_Water_Plane"},
    // S8 Water Queens House (live log m5w2 s8, 2026-11-01: counted unknown). Not in a trace: the
    // pairs are the asset DB's StaticInstancedForward variants (both modules of one bshd), the
    // dumps in captures/shaders. ExtraWaves' VS is 60d4357d's plus a second UV output;
    // NoCaustics_Waves' noise is at world xz * m5 + time * m7, the offset clamped below by m15.
    {0x99c1fcef82b253c1ull, 0xe0530238c64ad123ull, "FLUID_Water_Plane_flowmap_huge_PixelLighting_ExtraWaves"},
    {0xc4e92ccdb08dd777ull, 0x8113bd39597e2bd9ull, "FLUID_Water_Plane_flowmap_NoCaustics_Waves"},
    // Pre-audit of every dumped Fluids forward pair (2026-11-01, offline, tools/water_audit.py: the asset DB's
    // pairs, the dumps in captures/shaders; no trace). VS: gl_Position = vec4(P, 1) * set 0 b2 _m2 with y
    // negated, P = the transformed vertex + vec3(0, h, 0) (Debris: none); FS: no discard, no FragDepth.
    // huge_PL_Shadow shares 60d4357d with the docks plane. PL_noCaustics is the Flymm Cargos basement water
    // (level CTY_DockWarehouseBasement_A, NAT_RVR_DockWarehouseBasement_*): both its instanced and its
    // static (non-instanced: the per-object matrix) variant. Not audited: WIP 0f9a1874/0516c804
    // (the VS displaces x, y and z by one height), BloodRiver (not FLUID_Water), the undumped variants.
    {0xced49ee370940201ull, 0x60d4357d7b95d027ull, "FLUID_Water_Plane_flowmap_huge_PixelLighting_Shadow"},
    {0xd44c390bcbb9eea5ull, 0x1d64d76a57981794ull, "FLUID_Water_Plane_flowmap_Brine"},
    {0xfb7cc2ea451d3e08ull, 0xd7d9539d80ed55ebull, "FLUID_Water_Plane_flowmap_huge_PixelLighting_SphereMask"},
    {0x2bd6cb9de954c2f9ull, 0xffc5bb290d99c144ull, "FLUID_Water_Plane_flowmap_PixelLighting_noCaustics"},
    {0x269de9047e587ea2ull, 0x8cdf7b4a7c02a18cull, "FLUID_Water_Plane_flowmap_PixelLighting_noCaustics"},
    {0x4af3450ed83beb10ull, 0xbf2b63d076573791ull, "FLUID_Water_Plane_flowmap_OnlyNormals"},
    {0x7813e701eb642285ull, 0x0f00693c5c54c611ull, "FLUID_Water_Plane_Debris"},
};
inline constexpr uint32_t kAuditedCount = uint32_t(sizeof kAudited / sizeof kAudited[0]);
inline constexpr uint32_t kNoFamily = UINT32_MAX;

// Must match a future shaders/rt/water_common.hlsli (no shader reads it yet).
struct WaterRec {
  uint32_t magic;          // kMagic: written by this policy only (an older core leaves an EmRec)
  uint32_t cls;            // Class
  uint32_t family;         // index in kAudited, kNoFamily otherwise
  uint32_t flags;          // kFlag*
  uint32_t fs_lo, fs_hi;
  float roughness;         // Roughness_Base (perceptual), from the bound material CB
  float reflectance;       // Reflectance_Base (the G-buffer's specular reflectance convention)
  uint32_t mat_flags;      // Bg3MatFlags of the DB material
  uint32_t pad[7];
};
static_assert(sizeof(WaterRec) == 64, "WaterRec is 16 words");
inline constexpr uint32_t kMagic = 0x52544157u;   // 'WATR'
enum : uint32_t {
  kFlagRoughnessDefault = 1,     // Roughness_Base not readable / not finite / outside [0,1]: the DB default
  kFlagReflectanceDefault = 2,   // the same for Reflectance_Base
  kFlagNoRoughnessRole = 4,      // the material has no roughness role (FLUID_Water_Plane): 0
};

inline bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// The VS is matched on its upper word (the repository's 8-hex family name): the core route
// carries it in Bg3EmRecordIn::decal (32 bits). The FS is matched on all 64 bits.
inline uint32_t family_of(uint64_t fs, uint32_t vs_hi) {
  for (uint32_t i = 0; i < kAuditedCount; ++i)
    if (kAudited[i].fs == fs && uint32_t(kAudited[i].vs >> 32) == vs_hi) return i;
  return kNoFamily;
}

// The class of a draw from its DB material (info null: not in the DB) and its shader pair
// (vs_hi: the upper word of the VS hash).
// Lava, glass and every non-water material are kNotWater (lava stays D-EM's kMatForward).
inline Class classify(uint64_t fs, uint32_t vs_hi, const Bg3MaterialInfo* info, std::string_view name) {
  if (!info || info->pass != 1) return kNotWater;   // the forward (FOR) variant only
  const bool water_flag = (info->flags & BG3_MAT_F_WATER) != 0 || info->cls == BG3_MAT_CLASS_WATER;
  const bool fluid_water = contains(name, "FLUID_Water");
  if (contains(name, "Lava") || (!water_flag && !fluid_water)) return kNotWater;
  if ((info->flags & BG3_MAT_F_VFX) || !contains(name, "Fluids/")) return kExcludedVfx;
  if (contains(name, "_Fall") || contains(name, "Mist") || contains(name, "Bridge") || contains(name, "Foam"))
    return kExcludedFlow;
  return family_of(fs, vs_hi) != kNoFamily ? kSupported : kUnknown;
}

// One scalar role read from the bound material CB, else its DB default (flagged).
template <class Reader>
float role(Reader& rd, const Bg3MaterialInfo& mi, int which, uint32_t flag, uint32_t& flags) {
  const Bg3MatSlot& s = mi.slot[which];
  float v = s.value[0];
  if (s.kind != BG3_MAT_KIND_SCALAR || s.offset == 0xFFFF) return flags |= flag, v;
  float b = 0.0f;
  if (rd.read(s.set, s.binding, s.offset, &b, 4) && std::isfinite(b) && b >= 0.0f && b <= 1.0f) return b;
  flags |= flag;
  return std::isfinite(v) ? v : 0.0f;
}

// The record of one draw. Unsupported classes get the class and identity only (nothing read).
template <class Reader>
WaterRec make_record(Reader& rd, uint64_t fs, uint32_t vs_hi, const Bg3MaterialInfo* info, std::string_view name) {
  WaterRec r{};
  r.magic = kMagic;
  r.fs_lo = uint32_t(fs);
  r.fs_hi = uint32_t(fs >> 32);
  r.cls = classify(fs, vs_hi, info, name);
  r.family = family_of(fs, vs_hi);
  if (!info) return r;
  r.mat_flags = info->flags;
  if (r.cls != kSupported) return r;
  if (info->slot[BG3_MAT_ROUGHNESS].kind == BG3_MAT_KIND_NONE) {
    r.flags |= kFlagNoRoughnessRole;
    r.roughness = 0.0f;
  } else {
    r.roughness = role(rd, *info, BG3_MAT_ROUGHNESS, kFlagRoughnessDefault, r.flags);
  }
  r.reflectance = role(rd, *info, BG3_MAT_REFLECTANCE, kFlagReflectanceDefault, r.flags);
  return r;
}

}  // namespace bg3::water
