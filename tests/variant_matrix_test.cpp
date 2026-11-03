// Combination matrix of the experimental keys (docs/GATE_AUDIT_20261101.md "Silent degradations").
// For every pair of keys (the raw keys of the Experimental knobs of common/settings.h plus the water,
// hit-material, box, D-LS and GI keys below), each variant-routed pass either has the shader variant
// it requests (shaders/variants.txt, compiled by tools/compile_shaders.py) or skips through the one
// declared table, shader_variant::kWaterNoVariant. The test fails when
//   - a pair needs a request that variants.txt does not hold (a pass would fail to compile or fall back),
//   - kWaterNoVariant gains or loses a key (a new skip-for-variant must be a deliberate edit here),
//   - a preset turns on a key under which a pass skips (the skip would ship silently).
//   variant_matrix_test <root> [--requests]   (--requests: print the missing water requests, one per line,
//                                              for py -3 tools/shader_identity.py <requests>)
#include "../common/settings.h"
#include "../common/shader_variant.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sv = bg3::shader_variant;
namespace st = bg3::settings;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

// The variant flags a key selects in core/gi.cpp refresh() (capability words set from these keys at
// startup or live). Keys absent here change no request.
static unsigned key_flags(const std::string& k) {
  if (k == "rt_cascade_casters") return sv::kCascade;
  if (k == "rt_native_scalp" || k == "rt_native_hair") return sv::kScalp;
  if (k == "rt_hair_physical_origin") return sv::kScalp | sv::kHairOrigin;
  if (k == "rt_omm") return sv::kOmm;
  if (k == "rt_native_tileset") return sv::kTileset;
  if (k == "rt_hit_material_instanced") return sv::kHitInstanced;
  if (k == "rt_hit_material_wind_rgb") return sv::kHitInstanced | sv::kHitWindRgb;
  if (k == "gi_far") return sv::kGiFar;
  return 0;
}

// Water traces the OMM reference tuple (ordinary shaders with the host superset
// AlphaDSL). Tileset remains unsupported; scalp/hair are routed since 2026-11-02.
static const char* const kExpectedWaterSkips[] = {"rt_native_tileset", "rt_hit_material_paks"};

int main(int argc, char** argv) {
  CHECK(argc >= 2);
  const std::filesystem::path root = argv[1];
  const bool print_requests = argc >= 3 && std::string(argv[2]) == "--requests";

  // Keys of the matrix.
  std::set<std::string> keys;
  for (const st::Knob& k : st::kKnobs) {
    if (!k.experimental || k.kind != st::Kind::Choice) continue;
    for (const st::Choice& c : k.choices) {
      if (!c.id) break;
      for (const st::KV& kv : c.kv)
        if (kv.key && std::strcmp(kv.value, "0") != 0) keys.insert(kv.key);
    }
  }
  static const char* const kExtra[] = {
      "rt_water_geometry", "rt_water_reflections", "rt_water_compose", "rt_water_shadows", "rt_water_ls_shadows",
      "rt_water_refraction", "rt_hit_material", "rt_hit_material_instanced", "rt_hit_material_wind_rgb",
      "rt_hit_material_recycle", "rt_hit_emission", "rt_emissive", "gi_hit_direct", "gi_hit_sun", "gi_far",
      "rf_hit_gi", "rf_hit_shade", "ls_box", "ls_k", "rt_cascade_casters", "rt_sun_cascade_current",
      "rt_omm", "rt_native_tileset", "rt_native_scalp", "rt_native_hair", "rt_hair_physical_origin"};
  for (const char* k : kExtra) keys.insert(k);
  CHECK(keys.size() >= 20);

  // The declared skip table is the expected one.
  std::set<std::string> skip_keys, expected(std::begin(kExpectedWaterSkips), std::end(kExpectedWaterSkips));
  for (const sv::NoVariant& n : sv::kWaterNoVariant) {
    CHECK(n.key && n.defines && *n.defines);
    skip_keys.insert(n.key);
  }
  for (const auto& k : skip_keys)
    if (!expected.count(k)) std::printf("new skip-for-variant: water skips under %s (add the variant instead)\n", k.c_str());
  for (const auto& k : expected)
    if (!skip_keys.count(k)) std::printf("expected skip gone: %s (good: drop it from kExpectedWaterSkips)\n", k.c_str());
  CHECK(skip_keys == expected);
  for (const auto& k : skip_keys) CHECK(keys.count(k));

  // The requests the core can make.
  std::set<std::string> have;
  {
    std::ifstream in(root / "shaders" / "variants.txt");
    CHECK(in.good());
    for (std::string line; std::getline(in, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty() && line[0] != '#') have.insert(line);
    }
  }
  std::vector<std::string> gi_files;
  for (const auto& e : std::filesystem::directory_iterator(root / "shaders" / "gi"))
    if (e.is_regular_file() && e.path().extension() == ".hlsl") gi_files.push_back("gi/" + e.path().filename().string());
  CHECK(!gi_files.empty());
  std::vector<std::string> water;   // the water requests before the cascade suffix
  for (const char* b : sv::kWaterBases) water.push_back(b);
  for (const char* r : sv::kFixedRequests) water.push_back(r);

  // Every pair (and every single key: a == b).
  const std::vector<std::string> kv(keys.begin(), keys.end());
  size_t pairs = 0, requests = 0, skipped = 0, missing = 0;
  std::map<std::string, size_t> skip_by_key;
  auto need = [&](const std::string& r, const std::string& base, const std::string& a, const std::string& b) {
    ++requests;
    if (r == base || have.count(r)) return;
    ++missing;
    std::printf("no variant: %s for %s + %s\n", r.c_str(), a.c_str(), b.c_str());
  };
  for (size_t i = 0; i < kv.size(); ++i)
    for (size_t j = i; j < kv.size(); ++j) {
      ++pairs;
      const std::string &a = kv[i], &b = kv[j];
      const unsigned f = key_flags(a) | key_flags(b);
      for (const auto& file : gi_files) need(sv::request(file.c_str(), f), file, a, b);
      const char* skip = skip_keys.count(a) ? a.c_str() : skip_keys.count(b) ? b.c_str() : nullptr;
      if (skip) {
        ++skipped;
        ++skip_by_key[skip];
        continue;
      }
      const bool cascade = (f & sv::kCascade) != 0;
      const bool scalp = (f & sv::kScalp) != 0;
      for (const auto& r : water) need(sv::water_request(r.c_str(), cascade, scalp), r.substr(0, r.find('?')), a, b);
    }
  CHECK(missing == 0);

  // No preset turns on a key under which a pass skips.
  for (int p = 0; p < st::kPresetCount; ++p)
    for (const st::Knob& k : st::kKnobs) {
      if (k.kind != st::Kind::Choice) continue;
      const st::Choice& c = k.choices[k.preset[p]];
      for (const st::KV& e : c.kv)
        if (e.key && std::strcmp(e.value, "0") != 0 && skip_keys.count(e.key)) {
          std::printf("preset %s turns on %s: the water passes skip (no variant)\n", st::kPresetId[p], e.key);
          CHECK(false);
        }
    }

  if (print_requests) {   // the water requests a variant for each skip key would make
    for (const sv::NoVariant& n : sv::kWaterNoVariant)
      for (const auto& r : water)
        for (int c = 0; c < 2; ++c) {
          std::string q = r + (r.find('?') == std::string::npos ? "?" : ";") + n.defines;
          std::printf("%s\n", sv::water_request(q.c_str(), c != 0).c_str());
        }
    return 0;
  }
  std::printf("variant_matrix_test: %d checks, %zu keys, %zu pairs, %zu requests all in shaders/variants.txt; "
              "%zu pairs skip the water passes (no variant):",
              checks, kv.size(), pairs, requests, skipped);
  for (const auto& [k, n] : skip_by_key) std::printf(" %s %zu", k.c_str(), n);
  std::printf("\n");
  return 0;
}
