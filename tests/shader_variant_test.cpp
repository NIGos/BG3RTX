// shaders/variants.txt == every request core/gi.cpp can make (common/shader_variant.h over
// every shaders/gi/*.hlsl and every flag set). `shader_variant_test <root> --write` rewrites it.
#include "../common/shader_variant.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
using namespace bg3::shader_variant;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main(int argc, char** argv) {
  CHECK(argc >= 2);
  const std::filesystem::path root = argv[1];
  const bool write = argc >= 3 && std::string(argv[2]) == "--write";

  CHECK(kSpecWaterHitShade == 1016);
  CHECK(water_hit_request("rt/water_compose.hlsl", false) == "rt/water_compose.hlsl");
  // Fixed spellings: migrated keys share one request; the remaining defines keep their order.
  CHECK(request("gi/rt_shadow.hlsl", 0) == "gi/rt_shadow.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;RT_SPEC_CASCADE_SUN=1");  // spec only
  CHECK(request("probe.hlsl", kTileset | kOmm) == "probe.hlsl");
  CHECK(request("gi/gi_ao_apply.hlsl", kCascade | kHairOrigin | kSunCurrent) == "gi/gi_ao_apply.hlsl?RT_SPEC_NATIVE=1");
  CHECK(request("gi/rt_origin.hlsl", kHairOrigin) == "gi/rt_origin.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1");  // Hair origin needs the scalp
  CHECK(request("gi/rt_shadow.hlsl", kCascade) == "gi/rt_shadow.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;RT_SPEC_CASCADE_SUN=1");
  CHECK(request("gi/rt_shadow.hlsl", kSunCurrent | kCascade) ==
        "gi/rt_shadow.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SUN_CURRENT=1;RT_CASCADE_SUN=1;RT_CASCADE_MASKS=1;RT_CASCADE_TMIN=0.0;RT_CASCADE_TMIN_REL=0.0");
  CHECK(request("gi/gi_trace_rt.hlsl", kTileset | kHairOrigin | kOmm | kScalp | kCascade) ==
        "gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_OMM_SCENE=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(!cascade_entry("gi/rt_sun_resolve.hlsl") && !cascade_entry("gi/rt_hit_two_bounce.hlsl"));
  // rt_hit_material_instanced: only the hit-material consumers, last but cascade; never the ordinary files.
  CHECK(request("gi/rt_hit_material.hlsl", kHitInstanced) == "gi/rt_hit_material.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1");
  CHECK(request("gi/gi_trace_rt_hit_direct.hlsl", kHitInstanced | kCascade | kTileset) ==
        "gi/gi_trace_rt_hit_direct.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/gi_trace_rt.hlsl", kHitInstanced) == "gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/rt_shadow.hlsl", kHitInstanced) == "gi/rt_shadow.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;RT_SPEC_CASCADE_SUN=1");
  // gi_far: the gi_hit_sun files only, last; never the direct-only or ordinary files.
  CHECK(request("gi/gi_trace_rt_hit_sun.hlsl", kGiFar | kCascade) == "gi/gi_trace_rt_hit_sun.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;RT_SPEC_GI_FAR=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/gi_trace_rt_sharc_hit_sun_em.hlsl", kGiFar) == "gi/gi_trace_rt_sharc_hit_sun_em.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;RT_SPEC_GI_FAR=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/gi_trace_rt_hit_direct.hlsl", kGiFar) == "gi/gi_trace_rt_hit_direct.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/gi_trace_rt.hlsl", kGiFar) == "gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/rt_reflections_hit_shade.hlsl", 0) == request("gi/rt_reflections_hit_shade.hlsl", kRfHitGi));
  // rf_hit_gi: the four rf_hit_shade files only, last; never the ordinary D-RF or the GI files.
  CHECK(request("gi/rt_reflections_hit_shade.hlsl", kRfHitGi | kCascade) ==
        "gi/rt_reflections_hit_shade.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;RF_SPEC_HIT_GI=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/rt_reflections_sharc_hit_shade_em.hlsl", kRfHitGi | kHitInstanced) ==
        "gi/rt_reflections_sharc_hit_shade_em.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;RF_SPEC_HIT_GI=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/rt_reflections.hlsl", kRfHitGi) == "gi/rt_reflections.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/rt_hit_material.hlsl", kHitWindRgb) == "gi/rt_hit_material.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1");   // needs the instanced key
  CHECK(request("gi/rt_hit_material.hlsl", kHitInstanced | kHitWindRgb) == "gi/rt_hit_material.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1");
  CHECK(request("gi/rt_shadow.hlsl", kHitInstanced | kHitWindRgb) == request("gi/rt_shadow.hlsl", 0));
  CHECK(request("gi/rt_hit_material.hlsl", kHitIpGrass) == "gi/rt_hit_material.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1");   // needs the instanced key
  CHECK(request("gi/rt_hit_material.hlsl", kHitInstanced | kHitWindRgb | kHitIpGrass) ==
        "gi/rt_hit_material.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1");
  CHECK(request("gi/gi_trace_rt_hit_sun.hlsl", kRfHitGi) == "gi/gi_trace_rt_hit_sun.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;RT_SPEC_GI_FAR=1;GI_SPEC_GUARDS=1");
  // rt_spec_cascade, the only cascade path: one request for both cascade values; the constant carries kCascade.
  CHECK(request("gi/gi_trace_rt.hlsl", 0) == "gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/gi_ao_apply.hlsl", 0) == "gi/gi_ao_apply.hlsl?RT_SPEC_NATIVE=1");   // not a cascade pass: unchanged
  CHECK(request("gi/gi_trace_rt.hlsl", kSpecCascade | kCascade) == "gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;GI_SPEC_GUARDS=1");
  CHECK(request("gi/gi_trace_rt.hlsl", kSpecCascade) == request("gi/gi_trace_rt.hlsl", kSpecCascade | kCascade));
  CHECK(request("gi/rt_shadow.hlsl", kSpecCascade | kCascade) == "gi/rt_shadow.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1;RT_SPEC_CASCADE_SUN=1");
  CHECK(request("gi/rt_shadow.hlsl", kSpecCascade | kCascade | kSunCurrent).find("RT_SPEC_CASCADE") == std::string::npos);
  CHECK(request("gi/gi_ao_apply.hlsl", kSpecCascade) == "gi/gi_ao_apply.hlsl?RT_SPEC_NATIVE=1");   // not a cascade pass
  CHECK(spec_cascade_value("gi/gi_trace_rt.hlsl", kCascade) == 1 && spec_cascade_value("gi/gi_trace_rt.hlsl", 0) == 0);
  CHECK(spec_cascade_value("gi/gi_ao_apply.hlsl", kCascade) == 0);
  // Main's generated two-sided pool path retains its define and configuration restrictions.
  const auto pool = kHitInstanced | kHitWindRgb | kHitPool2s;
  CHECK(request("gi/rt_hit_material.hlsl", pool) ==
        "gi/rt_hit_material.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HIT_INST=1;RT_HIT_POOL_2S=1;RT_SPEC_CASCADE=1");
  CHECK(request("gi/rt_hit_material.hlsl", kHitInstanced | kHitPool2s) == request("gi/rt_hit_material.hlsl", kHitInstanced));
  for (unsigned excluded : {kHitIpGrass, kOmm, kTileset, kScalp})
    CHECK(request("gi/rt_hit_material.hlsl", pool | excluded) == request("gi/rt_hit_material.hlsl", (pool & ~kHitPool2s) | excluded));
  CHECK(request("gi/rt_shadow.hlsl", pool) == request("gi/rt_shadow.hlsl", 0));

  // Box cascade coverage and diagnostic values share their migrated modules.
  for (const char* f : {"gi/rt_local_shadows_box.hlsl", "gi/rt_local_shadows_full_box.hlsl", "gi/rt_local_shadows_box_k.hlsl",
                        "gi/rt_local_shadows_full_box_k.hlsl", "gi/rt_local_shadows_native_full_box.hlsl", "gi/rt_occ_stats.hlsl"}) {
    CHECK(cascade_entry(f));
    CHECK(request(f, 0) == request(f, kCascade));
    CHECK(spec_cascade_value(f, kCascade) == 1);
  }
  CHECK(kOccStats != kHitPool2s);
  CHECK(kSpecIdOccStats == 1011);
  CHECK(kSpecIdHairGiCull == 1017);
  CHECK(kSpecIdHairCheap == 1018 && kSpecIdHairGiMean == 1019);
  std::set<unsigned> hair_slots;
  for(unsigned base=0;base<512u;++base)for(unsigned cheap=0;cheap<2u;++cheap)for(unsigned mean=0;mean<2u;++mean){
    const unsigned slot=base|(cheap?kHairCheapSlot:0u)|(mean?kHairGiMeanSlot:0u);
    CHECK(slot<kPipelineSlots && hair_slots.insert(slot).second);
  }
  // Every existing installed slot retains a distinct cull-on sibling; toggling
  // back addresses the original pipeline instead of destroying/rebuilding it.
  for (unsigned base = 0; base < kHairGiCullSlot; ++base) {
    const auto off = base | hair_gi_cull_slot("gi/gi_trace_rt.hlsl", false);
    const auto on = base | hair_gi_cull_slot("gi/gi_trace_rt.hlsl", true);
    CHECK(off == base && on != off && on < kPipelineSlots);
  }
  CHECK(hair_gi_cull_slot("gi/rt_shadow.hlsl", true) == 0u);
  // rt_hair_groom (constant 1020): its own slot bit above every other, on every gi/ file only.
  CHECK(gpu_opt_entry("gi/rt_hit_two_bounce.hlsl") && gpu_opt_entry("gi/rt_shadow.hlsl"));
  CHECK(!gpu_opt_entry("gi/gi_temporal.hlsl") && !gpu_opt_entry("probe.hlsl"));
  CHECK(kSpecIdCompactHitState == 1024 && kSpecIdAlphaMetadataCache == 1022 && kSpecIdWaterEmptyTiles == 1023);
  std::set<unsigned> gpu_opt_slots;
  for(unsigned base=0;base<kCompactHitStateSlot;++base)for(unsigned compact=0;compact<2;++compact)for(unsigned alpha=0;alpha<2;++alpha){
    unsigned slot=base|(compact?kCompactHitStateSlot:0u)|(alpha?kAlphaMetadataCacheSlot:0u);
    CHECK(slot<kPipelineSlots && gpu_opt_slots.insert(slot).second);
  }
  CHECK(kSpecIdHairGroom == 1020);
  for (unsigned base = 0; base < kHairGroomSlot; ++base) {
    const auto on = base | hair_groom_slot("gi/rt_shadow.hlsl", true);
    CHECK((base | hair_groom_slot("gi/rt_shadow.hlsl", false)) == base && on != base && on < kPipelineSlots);
  }
  CHECK(hair_groom_slot("rt/water_reflect.hlsl", true) == 0u);
  CHECK(hair_gi_cull_entry("gi/gi_trace_rt.hlsl"));
  CHECK(hair_gi_cull_entry("gi/gi_trace_rt_sharc_hit_sun_em.hlsl"));
  CHECK(!hair_gi_cull_entry("gi/gi_trace.hlsl"));
  CHECK(!hair_gi_cull_entry("gi/rt_origin.hlsl"));
  CHECK(!hair_gi_cull_entry("gi/rt_shadow.hlsl"));
  CHECK(!hair_gi_cull_entry("gi/rt_local_shadows.hlsl"));
  CHECK(!hair_gi_cull_entry("gi/rt_reflections.hlsl"));
  CHECK(request("gi/rt_local_shadows_full_box.hlsl", 0).find("RT_SPEC_OCC_STATS=1") != std::string::npos);
  CHECK(request("gi/rt_local_shadows_full_box.hlsl", 0) == request("gi/rt_local_shadows_full_box.hlsl", kOccStats));
  CHECK(request("gi/rt_shadow.hlsl", kOccStats) == request("gi/rt_shadow.hlsl", 0));

  std::set<std::string> want;
  size_t bases = 0;
  for (const auto& e : std::filesystem::directory_iterator(root / "shaders" / "gi")) {
    if (!e.is_regular_file() || e.path().extension() != ".hlsl") continue;
    const std::string file = "gi/" + e.path().filename().string();
    ++bases;
    const unsigned a = allowed(file.c_str());
    if (request(file.c_str(), 0) != file) want.insert(request(file.c_str(), 0));   // e.g. GI_SPEC_GUARDS on every request
    for (unsigned m = a; m; m = (m - 1) & a)   // every non-empty subset of the allowed flags
      {
        const std::string r = request(file.c_str(), m);
        if (r != file) want.insert(r);
      }
  }
  CHECK(bases > 0);
  for (const char* r : kFixedRequests) {   // outside gi/: each names an existing file
    const char* end=std::strchr(r, '?');
    const std::string f(r, end ? size_t(end-r) : std::strlen(r));
    CHECK(std::filesystem::exists(root / "shaders" / f));
    want.insert(water_request(r, false));
    want.insert(water_request(r, true));
  }
  // Cascade state changes only specialization, for both water families and every layout.
  CHECK(water_request("rt/water_reflect.hlsl", false) == "rt/water_reflect.hlsl?WATER_SPEC_CASCADE=1;RT_SPEC_NATIVE=1");
  CHECK(water_request("rt/water_reflect.hlsl", true) == water_request("rt/water_reflect.hlsl", false));
  CHECK(water_request("rt/water_compose.hlsl?WATER_SHADOWS=1", true) ==
        "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_SPEC_CASCADE=1;RT_SPEC_NATIVE=1;RT_CASCADE_SUN_HIT=1");
  CHECK(water_request("rt/water_view.hlsl", true) == "rt/water_view.hlsl");
  for (const char* r : kFixedRequests) CHECK(water_request(r, false) == water_request(r, true));
  CHECK(water_request("rt/water_reflect.hlsl", false, false) == water_request("rt/water_reflect.hlsl", false, true));
  CHECK(water_request("rt/water_view.hlsl", false, true) == "rt/water_view.hlsl");
  for (const char* r : kFixedRequests)
    for (bool cascade : {false, true})
      CHECK(water_request(r, cascade, false) == water_request(r, cascade, true));
  // rt_water_rf_edge: both values use the same module, including every record layout.
  CHECK(water_rf_edge_request("rt/water_compose.hlsl", false) == "rt/water_compose.hlsl?WATER_SPEC_RF_EDGE=1");
  CHECK(water_rf_edge_request("rt/water_compose.hlsl", true) == "rt/water_compose.hlsl?WATER_SPEC_RF_EDGE=1");
  for (const char* r : kFixedRequests) {
    if (std::strncmp(r, "rt/water_compose.hlsl", 21) != 0 || std::strstr(r, "WATER_HIT_SHADE_LAYOUT"))
      continue;
    const std::string e = water_rf_edge_request(r, true);
    bool listed = false;
    for (const char* q : kFixedRequests) listed = listed || e == q;
    CHECK(listed);
  }
  for (const char* b : kWaterBases) {
    CHECK(std::filesystem::exists(root / "shaders" / b));
    for (int c = 0; c < 4; ++c) want.insert(water_request(b, c & 1, (c & 2) != 0));
  }
  const std::filesystem::path manifest = root / "shaders" / "variants.txt";
  if (write) {
    std::ofstream o(manifest, std::ios::binary);
    o << "# Generated by tests/shader_variant_test.cpp <root> --write: every GI variant request the core can\n# make (common/shader_variant.h). tools/compile_shaders.py compiles each one.\n";
    for (const auto& r : want) o << r << "\n";
    std::printf("wrote %zu variants of %zu bases\n", want.size(), bases);
    return 0;
  }
  std::set<std::string> have;
  std::ifstream in(manifest);
  CHECK(in.good());
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    CHECK(have.insert(line).second);
  }
  for (const auto& r : want)
    if (!have.count(r)) std::printf("missing from shaders/variants.txt: %s\n", r.c_str());
  for (const auto& r : have)
    if (!want.count(r)) std::printf("not requestable (shaders/variants.txt): %s\n", r.c_str());
  CHECK(have == want);
  std::printf("shader_variant_test: %d checks, %zu variants of %zu bases\n", checks, want.size(), bases);
  return 0;
}
