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
    const std::string f(r, std::strchr(r, '?'));
    CHECK(std::filesystem::exists(root / "shaders" / f));
    want.insert(r);
    want.insert(water_request(r, true));
  }
  // rt_cascade_casters on the water passes (core/water.cpp): masks on every request, the sun ray with WATER_SHADOWS.
  CHECK(water_request("rt/water_reflect.hlsl", false) == "rt/water_reflect.hlsl");
  CHECK(water_request("rt/water_reflect.hlsl", true) == "rt/water_reflect.hlsl?RT_CASCADE_MASKS=1");
  CHECK(water_request("rt/water_compose.hlsl?WATER_SHADOWS=1", true) ==
        "rt/water_compose.hlsl?WATER_SHADOWS=1;RT_CASCADE_MASKS=1;RT_CASCADE_SUN_HIT=1");
  // rt_water_rf_edge: both values use the same module, including every record layout.
  CHECK(water_rf_edge_request("rt/water_compose.hlsl", false) == "rt/water_compose.hlsl?WATER_SPEC_RF_EDGE=1");
  CHECK(water_rf_edge_request("rt/water_compose.hlsl", true) == "rt/water_compose.hlsl?WATER_SPEC_RF_EDGE=1");
  for (const char* r : kFixedRequests) {
    if (std::strncmp(r, "rt/water_compose.hlsl", 21) != 0 || std::strstr(r, "WATER_GLINT_AA"))
      continue;
    const std::string e = water_rf_edge_request(r, true);
    bool listed = false;
    for (const char* q : kFixedRequests) listed = listed || e == q;
    CHECK(listed);
    // rt_water_glint_aa: the sun forms have their WATER_GLINT_AA twin (the plain form has no sun records).
    const std::string g = water_glint_request(r, true);
    bool glisted = g == r, plisted = g == r;
    for (const char* q : kFixedRequests) glisted = glisted || g == q, plisted = plisted || water_glint_prepass(g) == q;
    CHECK(glisted);
    CHECK(plisted);
  }
  for (const char* b : kWaterBases) {
    CHECK(std::filesystem::exists(root / "shaders" / b));
    want.insert(water_request(b, true));
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
