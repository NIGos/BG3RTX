// Pins the M5.1 water policy (common/water_policy.h): the class of every asset-DB water and
// near-water family (the forward FOR variants, the flags and names the DB gives them, see the
// evidence block of the header), the audited FS/VS pairs, and the per-draw record (roles read
// from the bound material CB at the DB offsets, defaults and flags when unreadable).
#include "../common/water_policy.h"
#include "../common/water_scene.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace bg3::water;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

struct Fake {
  std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> cb;
  unsigned reads = 0;
  bool read(uint32_t set, uint32_t binding, uint64_t offset, void* dst, size_t n) {
    ++reads;
    auto it = cb.find({set, binding});
    if (it == cb.end() || offset + n > it->second.size()) return false;
    std::memcpy(dst, it->second.data() + offset, n);
    return true;
  }
  void put(uint32_t set, uint32_t binding, uint32_t off, float v) {
    auto& m = cb[{set, binding}];
    if (m.size() < off + 4) m.resize(off + 4, 0);
    std::memcpy(m.data() + off, &v, 4);
  }
};

static Bg3MatSlot scalar(uint8_t set, uint8_t binding, uint16_t off, float def) {
  Bg3MatSlot s{};
  s.kind = BG3_MAT_KIND_SCALAR;
  s.set = set;
  s.binding = binding;
  s.offset = off;
  s.value[0] = def;
  return s;
}

// A DB material as tools/assets/extract/materials.py writes it (flags and class from the DB).
static Bg3MaterialInfo mat(const char* name, uint32_t cls, uint32_t flags, uint32_t pass = 1) {
  Bg3MaterialInfo m{};
  m.template_name = name;
  m.cls = cls;
  m.flags = flags;
  m.pass = pass;
  m.instanced = 1;
  m.material_cb_set = 1;
  m.material_cb_binding = 1;
  return m;
}
constexpr uint32_t kPlaneFlags = BG3_MAT_F_DISCARDS | BG3_MAT_F_FORWARD_LIT | BG3_MAT_F_OPAQUE | BG3_MAT_F_REFRACTIVE |
                                 BG3_MAT_F_WATER | BG3_MAT_F_WORLD_POSITION_OFFSET;
static uint32_t hi(uint64_t v) { return uint32_t(v >> 32); }

int main() {
  // 1. The audited pairs: every traced forward plane is supported, with its family.
  for (uint32_t i = 0; i < kAuditedCount; ++i) {
    const Audited& a = kAudited[i];
    std::string name = std::string("Fluids/") + a.name;
    const Bg3MaterialInfo m = mat(name.c_str(), BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags);
    CHECK(classify(a.fs, hi(a.vs), &m, name) == kSupported);
    CHECK(family_of(a.fs, hi(a.vs)) == i);
    // The same FS with another VS (a different geometry convention) is unknown, never captured.
    CHECK(classify(a.fs, hi(a.vs) ^ 1u, &m, name) == kUnknown);
    // The deferred / depth / velocity variants of the template are not forward water.
    for (uint32_t pass : {0u, 2u, 3u, 6u, 7u}) {
      const Bg3MaterialInfo d = mat(name.c_str(), BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags, pass);
      CHECK(classify(a.fs, hi(a.vs), &d, name) == kNotWater);
    }
  }
  CHECK(kAuditedCount == 16);
  // The pre-audit of 2026-11-01 (tools/water_audit.py): appended, so the families 0..8 keep their index.
  CHECK(family_of(0xced49ee370940201ull, 0x60d4357du) == 9);    // huge_PL_Shadow, the docks VS
  CHECK(family_of(0xd44c390bcbb9eea5ull, 0x1d64d76au) == 10);   // flowmap_Brine
  CHECK(family_of(0xfb7cc2ea451d3e08ull, 0xd7d9539du) == 11);   // huge_PL_SphereMask
  // Flymm Cargos basement (PL_noCaustics): the instanced and the static variant are two families.
  CHECK(family_of(0x2bd6cb9de954c2f9ull, 0xffc5bb29u) == 12);
  CHECK(family_of(0x269de9047e587ea2ull, 0x8cdf7b4au) == 13);
  CHECK(family_of(0x269de9047e587ea2ull, 0xffc5bb29u) == kNoFamily);   // the static FS with the instanced VS
  CHECK(family_of(0x4af3450ed83beb10ull, 0xbf2b63d0u) == 14);   // flowmap_OnlyNormals
  CHECK(family_of(0x7813e701eb642285ull, 0x0f00693cu) == 15);   // Debris
  // Not audited: WIP (its VS displaces x, y and z), Avernus_BloodRiver (not FLUID_Water: unknown by name).
  CHECK(family_of(0x0f9a18741e9f7790ull, 0x0516c804u) == kNoFamily);
  {
    const std::string wip = "Fluids/FLUID_Water_Plane_WIP";
    const Bg3MaterialInfo m = mat(wip.c_str(), BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags);
    CHECK(classify(0x0f9a18741e9f7790ull, 0x0516c804u, &m, wip) == kUnknown);
    const std::string br = "Fluids/FLUID_Avernus_BloodRiver";
    const Bg3MaterialInfo b = mat(br.c_str(), BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags & ~BG3_MAT_F_FORWARD_LIT);
    CHECK(classify(0x5446be18a63164adull, 0xd6b0f0e7u, &b, br) == kUnknown);
  }
  // Every pair is unique (family_of returns the first match).
  for (uint32_t i = 0; i < kAuditedCount; ++i)
    for (uint32_t j = i + 1; j < kAuditedCount; ++j)
      CHECK(!(kAudited[i].fs == kAudited[j].fs && hi(kAudited[i].vs) == hi(kAudited[j].vs)));
  // The S1 docks plane (COVERAGE gap 1).
  CHECK(family_of(0x7b44a567771ecbecull, 0x60d4357du) == 0);
  // The S8 Water Queens House planes (asset-DB pairs, VS dumps read 2026-11-01).
  CHECK(family_of(0x99c1fcef82b253c1ull, 0xe0530238u) == 7);
  CHECK(family_of(0xc4e92ccdb08dd777ull, 0x8113bd39u) == 8);
  // Pairs are exact: ExtraWaves' FS with the docks VS (an identical body, one output less) is unknown.
  CHECK(family_of(0x99c1fcef82b253c1ull, 0x60d4357du) == kNoFamily);
  {
    // The ExtraWaves record: its own DB offsets (Reflectance_Base s1b1+276, Roughness_Base +284).
    const std::string xw = "Fluids/FLUID_Water_Plane_flowmap_huge_PixelLighting_ExtraWaves";
    Bg3MaterialInfo m = mat(xw.c_str(), BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags);
    m.slot[BG3_MAT_REFLECTANCE] = scalar(1, 1, 276, 0.35f);
    m.slot[BG3_MAT_ROUGHNESS] = scalar(1, 1, 284, 0.0f);
    Fake f;
    f.put(1, 1, 276, 0.25f);
    f.put(1, 1, 284, 0.0625f);
    const WaterRec r = make_record(f, 0x99c1fcef82b253c1ull, 0xe0530238u, &m, xw);
    CHECK(r.cls == kSupported && r.family == 7 && r.flags == 0);
    CHECK(r.reflectance == 0.25f && r.roughness == 0.0625f && f.reads == 2);
  }
  // 2. Other forward water families of the DB.
  struct Case { const char* name; uint32_t cls, flags; Class want; };
  const Case cases[] = {
      // planes not audited (or another shader pair of an audited name): counted as unknown
      {"Fluids/FLUID_Water_Plane_Minimap", BG3_MAT_CLASS_WATER, BG3_MAT_F_OPAQUE | BG3_MAT_F_WATER, kUnknown},
      {"Fluids/FLUID_Water_Plane_flowmap_huge_PixelLighting_ExtraWaves", BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags, kUnknown},
      {"Fluids/FLUID_Avernus_BloodRiver", BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags & ~BG3_MAT_F_FORWARD_LIT, kUnknown},
      // falls, mist, foam, bridges: alpha-blended volumes, not an interface
      {"Fluids/FLUID_Water_Fall_Core", BG3_MAT_CLASS_WATER, BG3_MAT_F_ALPHA_BLEND | BG3_MAT_F_WATER, kExcludedFlow},
      {"Fluids/FLUID_Water_Fall_Foam", BG3_MAT_CLASS_WATER, BG3_MAT_F_FOLIAGE | BG3_MAT_F_WATER, kExcludedFlow},
      {"Fluids/FLUID_Water_Fall_Mist", BG3_MAT_CLASS_WATER, BG3_MAT_F_ALPHA_BLEND | BG3_MAT_F_WATER, kExcludedFlow},
      {"Fluids/FLUID_Water_Bridge_Magic", BG3_MAT_CLASS_WATER, BG3_MAT_F_UNLIT | BG3_MAT_F_WATER, kExcludedFlow},
      // VFX with the water flag or class
      {"Effects/Model/VFX_Model_AlphaBlend_DefaultLit_Emissive_DirectionalOnly_Refraction_River_01",
       BG3_MAT_CLASS_REFRACTIVE, BG3_MAT_F_VFX | BG3_MAT_F_WATER | BG3_MAT_F_EMISSIVE, kExcludedVfx},
      {"Effects/Cinematic/VFX_Cinematic_Tower_Water_Whirlpool_01", BG3_MAT_CLASS_WATER, BG3_MAT_F_VFX | BG3_MAT_F_WATER,
       kExcludedVfx},
      // not water: lava stays D-EM's forward emissive, glass and other forward transparents
      {"Fluids/FLUID_Lava_Plane_Flowmap", BG3_MAT_CLASS_OPAQUE, BG3_MAT_F_EMISSIVE | BG3_MAT_F_OPAQUE, kNotWater},
      {"Fluids/FLUID_Lava_Plane_Flowing", BG3_MAT_CLASS_OPAQUE, BG3_MAT_F_EMISSIVE | BG3_MAT_F_WATER, kNotWater},
      {"Base/Base_Glass_Simple", BG3_MAT_CLASS_REFRACTIVE, BG3_MAT_F_REFRACTIVE, kNotWater},
      {"Effects/Model/VFX_Model_AlphaBlend_DefaultLit_Base_WPO_MaskScrolling_AlphaErosion_FakeSunBacklit_Waterfall_01",
       BG3_MAT_CLASS_TRANSPARENT, BG3_MAT_F_VFX | BG3_MAT_F_ALPHA_BLEND, kNotWater},
      {"Fluids/FLUID_Minimap_Const", BG3_MAT_CLASS_OPAQUE, BG3_MAT_F_OPAQUE, kNotWater},
  };
  for (const Case& c : cases) {
    const Bg3MaterialInfo m = mat(c.name, c.cls, c.flags);
    CHECK(classify(0x1111222233334444ull, 0x55556666u, &m, c.name) == c.want);
  }
  CHECK(classify(0x7b44a567771ecbecull, 0x60d4357du, nullptr, "") == kNotWater);

  // 3. The record of the S1 docks plane: roles at the DB offsets (set 1 b1 +256 / +264).
  std::string docks = "Fluids/FLUID_Water_Plane_flowmap_huge_PixelLighting";
  Bg3MaterialInfo m = mat(docks.c_str(), BG3_MAT_CLASS_REFRACTIVE, kPlaneFlags);
  m.slot[BG3_MAT_REFLECTANCE] = scalar(1, 1, 256, 0.35f);
  m.slot[BG3_MAT_ROUGHNESS] = scalar(1, 1, 264, 0.0f);
  {
    Fake f;
    f.put(1, 1, 256, 0.5f);
    f.put(1, 1, 264, 0.125f);
    const WaterRec r = make_record(f, 0x7b44a567771ecbecull, 0x60d4357du, &m, docks);
    CHECK(r.magic == kMagic && r.cls == kSupported && r.family == 0 && r.flags == 0);
    CHECK(r.fs_lo == 0x771ecbecu && r.fs_hi == 0x7b44a567u && r.mat_flags == kPlaneFlags);
    CHECK(r.reflectance == 0.5f && r.roughness == 0.125f && f.reads == 2);
  }
  // Unreadable CB: the DB defaults, flagged.
  {
    Fake f;
    const WaterRec r = make_record(f, 0x7b44a567771ecbecull, 0x60d4357du, &m, docks);
    CHECK(r.cls == kSupported && r.flags == (kFlagRoughnessDefault | kFlagReflectanceDefault));
    CHECK(r.reflectance == 0.35f && r.roughness == 0.0f);
  }
  // Non-finite and out-of-range values: the defaults, flagged per role.
  for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -0.25f, 1.5f}) {
    Fake f;
    f.put(1, 1, 256, bad);
    f.put(1, 1, 264, 0.25f);
    const WaterRec r = make_record(f, 0x7b44a567771ecbecull, 0x60d4357du, &m, docks);
    CHECK(r.flags == kFlagReflectanceDefault && r.reflectance == 0.35f && r.roughness == 0.25f);
  }
  // FLUID_Water_Plane: no roughness role (only Reflectance at +52).
  {
    std::string plane = "Fluids/FLUID_Water_Plane";
    Bg3MaterialInfo p = mat(plane.c_str(), BG3_MAT_CLASS_REFRACTIVE, BG3_MAT_F_DISCARDS | BG3_MAT_F_OPAQUE |
                                                                       BG3_MAT_F_REFRACTIVE | BG3_MAT_F_WATER);
    p.slot[BG3_MAT_REFLECTANCE] = scalar(1, 1, 52, 0.25f);
    Fake f;
    f.put(1, 1, 52, 0.3f);
    const WaterRec r = make_record(f, 0x68068cab0ff17c8dull, 0xbbdf7b8au, &p, plane);
    CHECK(r.cls == kSupported && r.family == 6 && r.flags == kFlagNoRoughnessRole);
    CHECK(r.roughness == 0.0f && r.reflectance == 0.3f && f.reads == 1);
  }
  // Unsupported classes: identity and class only, nothing read.
  {
    Fake f;
    f.put(1, 1, 256, 0.5f);
    const WaterRec r = make_record(f, 0x7b44a567771ecbecull, 0x12345678u, &m, docks);
    CHECK(r.magic == kMagic && r.cls == kUnknown && r.family == kNoFamily && f.reads == 0 && r.reflectance == 0.0f);
    const WaterRec n = make_record(f, 1, 2, nullptr, "");
    CHECK(n.magic == kMagic && n.cls == kNotWater && n.mat_flags == 0 && f.reads == 0);
  }
  // 5. M5.2 scene publication (common/water_scene.h): the layer -> core channel without ABI.
  {
    Scene pub{};
    Scene got{};
    CHECK(read(&pub, got) && usable(true, got, 10) == kNoScene);   // never published: version 0
    Scene a{};
    a.tlas = 0x1234;
    a.built_frame = 100;
    a.cam[0] = 1000.0f, a.cam[1] = 20.0f, a.cam[2] = -3000.0f;
    a.cam_ok = 1;
    a.reflectance = 0.35f;
    a.draws = 2;
    a.triangles = 1450000;
    publish(&pub, a);
    CHECK(pub.seq == 2 && pub.version == kSceneVersion);
    CHECK(read(&pub, got) && got.tlas == 0x1234 && got.built_frame == 100 && got.draws == 2 && got.triangles == 1450000);
    CHECK(usable(true, got, 100) == kUsable && usable(true, got, 102) == kUsable);
    CHECK(usable(true, got, 103) == kStale && usable(true, got, 99) == kStale);   // too old / from the future
    CHECK(usable(false, got, 100) == kTorn);
    Scene none = got;
    none.tlas = 0;
    CHECK(usable(true, none, 100) == kNoTlas);
    // A write in progress (odd seq) is never read.
    pub.seq |= 1u;
    CHECK(!read(&pub, got));
    pub.seq += 1u;
    publish(&pub, a);
    CHECK((pub.seq & 1u) == 0 && read(&pub, got));
    // Concurrent writer and reader: every successful read is a complete publication.
    Scene live{};
    std::atomic<bool> stop{false};
    std::thread w([&] {
      for (uint64_t i = 1; i <= 20000; ++i) {
        Scene s{};
        s.tlas = i;
        s.built_frame = i;
        s.triangles = i * 3;
        publish(&live, s);
      }
      stop.store(true, std::memory_order_release);
    });
    unsigned ok = 0, bad = 0;
    while (!stop.load(std::memory_order_acquire)) {
      Scene s{};
      if (read(&live, s) && s.version == kSceneVersion) {
        ++ok;
        if (s.tlas != s.built_frame || s.triangles != s.tlas * 3) ++bad;
      }
    }
    w.join();
    CHECK(bad == 0);
    CHECK(read(&live, got) && got.tlas == 20000);
    (void)ok;
    // The camera shift: p_build = p_now + (cam_now - cam_build); 0 when unknown or absurd.
    float sh[3];
    const float now[3] = {1001.5f, 20.0f, -2999.0f};
    shift(a, now, true, sh);
    CHECK(sh[0] == 1.5f && sh[1] == 0.0f && sh[2] == 1.0f);
    shift(a, now, false, sh);
    CHECK(sh[0] == 0.0f && sh[1] == 0.0f && sh[2] == 0.0f);
    Scene nc = a;
    nc.cam_ok = 0;
    shift(nc, now, true, sh);
    CHECK(sh[0] == 0.0f && sh[2] == 0.0f);
    const float far[3] = {3e6f, 0.0f, 0.0f};
    shift(a, far, true, sh);
    CHECK(sh[0] == 0.0f);
    const float nan[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    shift(a, nan, true, sh);
    CHECK(sh[0] == 0.0f);
    // F0 in the game's convention: 0.16 r^2; S1 docks 0.35 -> 0.0196; bad roles -> IOR 1.33.
    CHECK(std::fabs(f0_of(0.35f) - 0.0196f) < 1e-7f);
    CHECK(f0_of(0.0f) == 0.0f && std::fabs(f0_of(1.0f) - 0.16f) < 1e-7f);
    CHECK(f0_of(-0.1f) == 0.02f && f0_of(1.5f) == 0.02f && f0_of(std::numeric_limits<float>::quiet_NaN()) == 0.02f);
  }
  std::printf("water_policy_test: %u checks passed (%u audited pipelines, %zu families)\n", checks, kAuditedCount,
              sizeof cases / sizeof cases[0]);
  return 0;
}
