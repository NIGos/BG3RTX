// Unit tests for common/settings.h: preset expansion, layering, read-back,
// the user-ini writer, GPU capability rules and the JSON schema export.
//   bin/settings_test.exe                  run all tests (exit code = failures)
//   bin/settings_test.exe --write-schema   regenerate data/settings_schema.json
#include "../common/settings.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace bg3::settings;

static int g_failed = 0, g_checks = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    ++g_checks;                                                          \
    if (!(cond)) {                                                       \
      ++g_failed;                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                    \
  } while (0)

#define CHECK_EQ(a, b)                                                                                  \
  do {                                                                                                  \
    ++g_checks;                                                                                         \
    std::string _a = (a), _b = (b);                                                                     \
    if (_a != _b) {                                                                                     \
      ++g_failed;                                                                                       \
      std::printf("FAIL %s:%d: %s == %s ('%s' vs '%s')\n", __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str()); \
    }                                                                                                   \
  } while (0)

static std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

static std::string get(const Map& m, const char* key) {
  auto it = m.find(key);
  return it == m.end() ? "<missing>" : it->second;
}

static void test_parse() {
  Map m = parse_ini("# comment\n a = 1 \nb=2 # trailing\r\nnoequals\n=empty key\nc=\n");
  CHECK_EQ(get(m, "a"), "1");
  CHECK_EQ(get(m, "b"), "2");
  CHECK_EQ(get(m, "c"), "");
  CHECK(m.size() == 3);
}

static void test_preset_expansion() {
  Map high;
  for (auto& kv : expand_preset(kHigh)) high[kv.first] = kv.second;
  CHECK_EQ(get(high, "rr"), "1");
  CHECK_EQ(get(high, "rt_build"), "auto");
  CHECK_EQ(get(high, "rt_alpha"), "1");
  CHECK_EQ(get(high, "rt_shadows"), "1");
  CHECK_EQ(get(high, "gi_rt"), "1");
  CHECK_EQ(get(high, "gi_rr_rays"), "1");
  CHECK_EQ(get(high, "rt_local_shadows"), "1");
  CHECK_EQ(get(high, "ls_samples"), "0");
  CHECK_EQ(get(high, "rf_max_rough"), "0.6");
  CHECK_EQ(get(high, "fg"), "2");
  CHECK_EQ(get(high, "gi_strength"), "1.00");
  CHECK_EQ(get(high, "fps_cap"), "0");

  Map low;
  for (auto& kv : expand_preset(kLow)) low[kv.first] = kv.second;
  CHECK_EQ(get(low, "rr"), "0");
  CHECK_EQ(get(low, "rt_build"), "0");
  CHECK_EQ(get(low, "rt_alpha"), "0");
  CHECK_EQ(get(low, "rt_shadows"), "0");
  CHECK_EQ(get(low, "contact_shadows"), "1");
  CHECK_EQ(get(low, "gi_rt"), "0");
  CHECK_EQ(get(low, "gi_rays"), "2");
  CHECK_EQ(get(low, "gi_steps"), "12");
  CHECK_EQ(get(low, "rt_local_shadows"), "0");
  CHECK_EQ(get(low, "rt_reflections"), "0");

  Map med;
  for (auto& kv : expand_preset(kMedium)) med[kv.first] = kv.second;
  CHECK_EQ(get(med, "rt_alpha"), "0");
  CHECK_EQ(get(med, "ls_samples"), "1");
  CHECK_EQ(get(med, "rf_max_rough"), "0.4");

  Map ultra;
  for (auto& kv : expand_preset(kUltra)) ultra[kv.first] = kv.second;
  CHECK_EQ(get(ultra, "gi_rr_rays"), "2");
  CHECK_EQ(get(ultra, "ls_shadowed"), "3");   // shadow maps replaced where every caster is on screen
  CHECK_EQ(get(ultra, "rf_max_rough"), "0.8");
  CHECK_EQ(get(ultra, "fps_cap"), "58");
  CHECK_EQ(get(ultra, "fg"), "2");
  CHECK_EQ(get(ultra, "rt_rebuild_frames"), "600");

  CHECK(expand_preset(-1).empty());
  CHECK(preset_from_id(" ULTRA ") == kUltra);
  CHECK(preset_from_id("custom") == -1);
}

static void test_layering() {
  Map base = parse_ini("rr=1\nrf_max_rough=0.6\ngi_rr_rays=1\nrt_build=1\nfoo=base\nrt_shadows=1\n");
  Map user = parse_ini("preset=ultra\nrf_max_rough=0.7\nfoo=user\n");
  Map over = parse_ini("headless=1\nfg=0\n");
  Map m = merge_layers({&base, &user, &over});
  CHECK_EQ(get(m, "gi_rr_rays"), "2");      // from the user's preset
  CHECK_EQ(get(m, "rf_max_rough"), "0.7");  // user key beats the user's preset
  CHECK_EQ(get(m, "foo"), "user");          // user beats base
  CHECK_EQ(get(m, "fg"), "0");              // override file beats the preset
  CHECK_EQ(get(m, "headless"), "1");
  CHECK_EQ(get(m, "rt_build"), "1");        // auto, with consumers on
  CHECK_EQ(get(m, "preset"), "ultra");

  // A preset in a later layer overrides explicit keys of earlier layers.
  Map user2 = parse_ini("rr=1\nrf_max_rough=0.9\n");
  Map over2 = parse_ini("preset=low\n");
  Map m2 = merge_layers({&base, &user2, &over2});
  CHECK_EQ(get(m2, "rr"), "0");
  CHECK_EQ(get(m2, "rt_build"), "0");
  CHECK_EQ(get(m2, "rt_reflections"), "0");
  CHECK_EQ(get(m2, "rf_max_rough"), "0.9");   // Low's "Off" does not touch rf_max_rough

  // Unknown preset: ignored with a note; "custom" silently.
  std::vector<std::string> notes;
  Map bad = parse_ini("preset=insane\n");
  Map m3 = merge_layers({&base, &bad}, &notes);
  CHECK(notes.size() == 1);
  CHECK_EQ(get(m3, "rr"), "1");
  notes.clear();
  Map custom = parse_ini("preset=custom\n");
  merge_layers({&base, &custom}, &notes);
  CHECK(notes.empty());

  // Missing layers (nullptr / empty) change nothing.
  Map empty;
  Map m4 = merge_layers({&base, &empty, nullptr});
  CHECK_EQ(get(m4, "foo"), "base");
}

static void test_derived_rt_build() {
  Map m = parse_ini("rt_build=auto\nrt_shadows=0\ngi_rt=0\nrt_local_shadows=0\nrt_reflections=0\n");
  CHECK(resolve_derived(m));
  CHECK_EQ(get(m, "rt_build"), "0");
  m = parse_ini("rt_build=auto\nrt_reflections=1\n");
  resolve_derived(m);
  CHECK_EQ(get(m, "rt_build"), "1");
  m = parse_ini("rt_build=auto\nrt_debug=3\n");
  resolve_derived(m);
  CHECK_EQ(get(m, "rt_build"), "1");
  m = parse_ini("rt_build=1\n");
  CHECK(!resolve_derived(m));
  CHECK_EQ(get(m, "rt_build"), "1");
  // Medium with every RT effect switched off by the user: no scene build.
  Map base;
  Map user = parse_ini("preset=medium\nrt_shadows=0\ngi_rt=0\nrt_local_shadows=0\nrt_reflections=0\n");
  CHECK_EQ(get(merge_layers({&base, &user}), "rt_build"), "0");
}

static void test_read_back() {
  for (int p = 0; p < kPresetCount; ++p) {
    Map m;
    Map user = parse_ini(std::string("preset=") + kPresetId[p] + "\n");
    apply_layer(m, user);
    std::vector<int> v = knob_values(m);
    CHECK(v == preset_values(p));
    CHECK(matching_preset(v) == p);
  }
  // Hand-edited keys that match no choice read back as custom (-1).
  Map m = parse_ini("gi=1\ngi_rt=0\ngi_rays=3\n");
  CHECK(knob_value(*find_knob("gi"), m) == -1);
  // Slider read-back.
  m = parse_ini("gi_strength=0.85\nls_strength=0.85\nrf_strength=0.850\n");
  CHECK(knob_value(*find_knob("rt_intensity"), m) == 85);
  m = parse_ini("gi_strength=0.85\nls_strength=1\nrf_strength=1\n");
  CHECK(knob_value(*find_knob("rt_intensity"), m) == -1);
  m = parse_ini("fps_cap=0\n");
  CHECK(knob_value(*find_knob("fps_cap"), m) == 0);
  m = parse_ini("fps_cap=10\n");   // below the slider range
  CHECK(knob_value(*find_knob("fps_cap"), m) == -1);
}

// "High is today's bg3rtx.ini": the developer defaults must read back as High,
// so the High preset changes nothing for developers.
static void test_dev_ini_is_high() {
  std::string text = read_file(BG3RTX_SOURCE_DIR "/bg3rtx.ini");
  CHECK(!text.empty());
  Map base = parse_ini(text);
  CHECK(matching_preset(knob_values(base)) == kHigh);
  CHECK_EQ(get(base, "dev"), "1");
  CHECK_EQ(get(base, "dump_shaders"), "1");

  // The release profile appended: developer keys off, settings unchanged.
  Map rel = parse_ini(text + read_file(BG3RTX_SOURCE_DIR "/data/release.ini"));
  CHECK_EQ(get(rel, "dev"), "0");
  CHECK_EQ(get(rel, "dump_shaders"), "0");
  CHECK_EQ(get(rel, "rt_stats"), "0");
  CHECK(matching_preset(knob_values(rel)) == kHigh);

  // High over the developer ini changes no key except rt_build 1 -> auto (-> 1).
  Map user = parse_ini("preset=high\n");
  Map m = merge_layers({&base, &user});
  Map plain = merge_layers({&base});
  for (auto& kv : plain)
    if (!same_value(get(m, kv.first.c_str()), kv.second))
      CHECK_EQ(kv.first + "=" + get(m, kv.first.c_str()), kv.first + "=" + kv.second);
}

// common/settings.h is the one source of every default: kDefaults must hold exactly the keys
// and values of bg3rtx.ini, kReleaseDefaults those of data/release.ini
// (python scripts/gen_settings_defaults.py regenerates both after an ini edit).
static void check_table_equals_ini(const KV* table, size_t n, const char* file) {
  Map ini = parse_ini(read_file(std::string(BG3RTX_SOURCE_DIR "/") + file));
  CHECK(!ini.empty());
  Map tab;
  for (size_t i = 0; i < n; ++i) {
    if (tab.count(table[i].key)) CHECK_EQ(std::string(file) + ": duplicate " + table[i].key, "");
    tab[table[i].key] = table[i].value;
  }
  bool same = true;
  for (auto& kv : ini)
    if (get(tab, kv.first.c_str()) != kv.second) {
      same = false;
      CHECK_EQ(std::string(file) + " " + kv.first + "=" + get(tab, kv.first.c_str()),
               std::string(file) + " " + kv.first + "=" + kv.second);
    }
  for (auto& kv : tab)
    if (!ini.count(kv.first)) {
      same = false;
      CHECK_EQ(std::string("settings.h has ") + kv.first + ", " + file + " does not", "");
    }
  CHECK(ini.size() == tab.size());
  if (!same) std::printf("common/settings.h defaults are stale: run python scripts/gen_settings_defaults.py\n");
}

static void test_defaults_one_source() {
  check_table_equals_ini(kDefaults, sizeof kDefaults / sizeof kDefaults[0], "bg3rtx.ini");
  check_table_equals_ini(kReleaseDefaults, sizeof kReleaseDefaults / sizeof kReleaseDefaults[0], "data/release.ini");
  // Every release key is a key of bg3rtx.ini (the profile only switches developer keys off).
  for (const KV& kv : kReleaseDefaults) {
    bool found = false;
    for (const KV& d : kDefaults) found = found || std::string(d.key) == kv.key;
    CHECK(found);
  }

  // The fallback: kDefaults with the release profile on top.
  CHECK(code_default("gi_rays") && std::string(code_default("gi_rays")) == get(parse_ini(read_file(BG3RTX_SOURCE_DIR "/bg3rtx.ini")), "gi_rays"));
  CHECK_EQ(code_default("dump_shaders"), "0");
  CHECK_EQ(code_default("dev"), "0");
  CHECK(code_default("no_such_key") == nullptr);
  CHECK_EQ(lookup(Map{}, "rr"), code_default("rr"));

  // "High = the developer defaults": the High preset writes the table's value for every key
  // it sets, and the defaults alone (no ini at all) read back as High.
  for (auto& kv : expand_preset(kHigh)) {
    const char* d = code_default(kv.first);
    CHECK(d != nullptr);
    if (d && !same_value(d, kv.second)) CHECK_EQ(kv.first + "=" + d, kv.first + "=" + kv.second);
  }
  Map defaults = code_defaults();
  CHECK(matching_preset(knob_values(defaults)) == kHigh);
  Map resolved = merge_layers({&defaults});
  CHECK_EQ(get(resolved, "rt_build"), "1");   // auto with the RT effects on
  // A user preset over the defaults still expands and overrides them.
  Map user = parse_ini("preset=low\n");
  Map m = merge_layers({&defaults, &user});
  CHECK_EQ(get(m, "rt_build"), "0");
  CHECK_EQ(get(m, "gi_rays"), "2");
  CHECK(matching_preset(knob_values(m)) == kLow);
}

static void test_user_ini_writer() {
  std::vector<int> v = preset_values(kHigh);
  v[knob_index("reflections")] = 3;   // Rough
  v[knob_index("fps_cap")] = 90;
  std::string existing = "# old header\npreset=low\nrf_max_rough=0.4\nmy_tweak=5 # mine\nrt_tmax=200\n";
  std::string out = render_user_ini(existing, kHigh, v, {{"rt", "0"}});
  Map w = parse_ini(out);
  CHECK_EQ(get(w, "preset"), "high");
  CHECK_EQ(get(w, "rf_max_rough"), "0.8");
  CHECK_EQ(get(w, "rt_reflections"), "1");
  CHECK_EQ(get(w, "fps_cap"), "90");
  CHECK_EQ(get(w, "my_tweak"), "5");      // unmanaged keys kept
  CHECK_EQ(get(w, "rt_tmax"), "200");
  CHECK_EQ(get(w, "rt"), "0");            // forced hardware limit
  CHECK(w.find("gi_rt") == w.end());      // unchanged knobs are not written
  CHECK(out.find("old header") == std::string::npos);

  // Round trip: what the file selects is what was written.
  Map base;
  Map merged;
  apply_layer(merged, base);
  apply_layer(merged, w);
  CHECK(knob_values(merged) == v);

  // Unchanged preset: only the preset line.
  Map w2 = parse_ini(render_user_ini("", kMedium, preset_values(kMedium)));
  CHECK(w2.size() == 1);
  CHECK_EQ(get(w2, "preset"), "medium");
}

static void test_dependencies_and_caps() {
  std::vector<int> v = preset_values(kHigh);
  v[knob_index("rt_geometry")] = 0;
  CHECK(!unmet_need(*find_knob("reflections"), 2, v).empty());
  CHECK(unmet_need(*find_knob("reflections"), 0, v).empty());
  CHECK(unmet_need(*find_knob("rr"), 1, v).empty());

  Gpu g5090{0x10DE, 0x2B85, "NVIDIA GeForce RTX 5090", true, true};
  Gpu g4070{0x10DE, 0x2786, "NVIDIA GeForce RTX 4070", true, true};
  Gpu g3060{0x10DE, 0x2503, "NVIDIA GeForce RTX 3060", true, true};
  Gpu g4090l{0x10DE, 0x2717, "NVIDIA GeForce RTX 4090 Laptop GPU", true, true};
  Gpu g1660{0x10DE, 0x21C4, "NVIDIA GeForce GTX 1660 SUPER", false, true};
  Gpu amd{0x1002, 0x744C, "AMD Radeon RX 7900 XTX", true, true};
  Gpu a6000{0x10DE, 0x2230, "NVIDIA RTX A6000", true, true};
  CHECK(nv_series(g5090) == 50);
  CHECK(nv_series(g4070) == 40);
  CHECK(nv_series(g3060) == 30);
  CHECK(nv_series(a6000) == 30);   // by device id
  CHECK(nv_series(g1660) == 0);
  CHECK(suggest_preset(g5090) == kUltra);
  CHECK(suggest_preset(g4070) == kMedium);
  CHECK(suggest_preset(g3060) == kLow);
  CHECK(suggest_preset(g4090l) == kMedium);
  CHECK(suggest_preset(g1660) == kLow);
  CHECK(suggest_preset(amd) == kLow);
  CHECK(gpu_caps(g5090) == (kCapRayQuery | kCapDlss | kCapFg | kCapMfg));
  CHECK(gpu_caps(g3060) == (kCapRayQuery | kCapDlss));
  CHECK(gpu_caps(amd) == kCapRayQuery);

  // Capability clamping: a 3060 gets no frame generation, AMD no RR either,
  // no ray query no RT geometry.
  std::vector<int> c = preset_values_for(kHigh, gpu_caps(g3060));
  CHECK(c[knob_index("frame_generation")] == 0);
  CHECK(c[knob_index("rr")] == 1);
  c = preset_values_for(kHigh, gpu_caps(amd));
  CHECK(c[knob_index("rr")] == 0);
  c = preset_values_for(kLow, gpu_caps(g1660));
  CHECK(c[knob_index("rt_geometry")] == 0);
  CHECK(c[knob_index("gi")] == 1);   // screen-space GI needs no RT

  CHECK(key_change_needs_restart("fg", "0", "2"));
  CHECK(!key_change_needs_restart("fg", "2", "4"));
  CHECK(key_change_needs_restart("rt_alpha", "0", "1"));
  CHECK(!key_change_needs_restart("rf_max_rough", "0.6", "0.8"));
  CHECK(key_change_needs_restart("sharc_device", "1", "0"));   // device features at creation
  CHECK(!key_change_needs_restart("sharc", "0", "1"));          // the cache itself is live
  CHECK(!key_change_needs_restart("sharc_log2", "22", "21"));   // reallocated by the core
}

static Map on_map(const Knob& k, int c) {
  KVList pairs;
  knob_kvs(k, c, pairs);
  Map m;
  for (auto& kv : pairs) m[kv.first] = kv.second;
  return m;
}

// Keys every GUI entry wrote before the 2026-11-01 rationalization (docs/GUI_AUDIT_20261101.md):
// each must still be written by a knob or be listed in kAutomaticKeys (no orphan key).
static const char* const kOldKnobKeys[] = {
    "rt_cascade_casters", "ls_box", "ls_k", "ls_rest_samples", "rt_native_scalp", "rt_alpha_spill",
    "rt_native_tileset", "rt_native_hair", "rt_hair_physical_origin", "rt_hit_material",
    "rt_hit_material_instanced", "rt_hit_material_recycle", "fg_letterbox", "rt_hit_material_wind_rgb",
    "gi_hit_direct", "gi_hit_sun", "gi_far", "gi_far_m", "rf_hit_shade", "rf_hit_gi", "rt_hit_emission",
    "rt_hit_reference", "rt_omm", "rt_water_geometry", "rt_water_reflections", "rt_water_compose",
    "rt_water_shadows", "rt_water_ls_shadows", "rt_water_refraction", "rt_water_game_shadow",
    "rt_sun_cascade_current", "rt_vram_guard", "rt_vram_compact", "rt_capture_elastic"};

// The old knob ids, each with the one key its "On" wrote.
static const char* const kOldOn[][2] = {
    {"exp_roof_shadows", "rt_cascade_casters"}, {"exp_box_lights", "ls_box"},
    {"exp_hit_material", "rt_hit_material"}, {"exp_hit_material_instanced", "rt_hit_material_instanced"},
    {"exp_hit_material_wind_rgb", "rt_hit_material_wind_rgb"},
    {"exp_gi_hit_direct", "gi_hit_direct"}, {"exp_gi_hit_sun", "gi_hit_sun"}, {"exp_rf_hit_shade", "rf_hit_shade"},
    {"exp_rf_hit_gi", "rf_hit_gi"}, {"exp_water_geometry", "rt_water_geometry"},
    {"exp_water_reflections", "rt_water_reflections"}, {"exp_water_compose", "rt_water_compose"},
    {"exp_water_shadows", "rt_water_shadows"}, {"exp_water_ls_shadows", "rt_water_ls_shadows"},
    {"exp_water_game_shadow", "rt_water_game_shadow"}, {"exp_water_refraction", "rt_water_refraction"},
    {"exp_native_scalp", "rt_native_scalp"}, {"exp_native_hair", "rt_native_hair"},
    {"exp_native_tileset", "rt_native_tileset"}};

static void test_gui_levels() {
  // Qualified memory management is automatic; three user-facing experimental entries remain.
  int user_exp = 0;
  std::vector<std::string> dev;
  for (const Knob& k : kKnobs) {
    if (k.experimental && !k.dev_only) ++user_exp;
    if (k.dev_only) dev.push_back(k.id);
    CHECK(knob_visible(k, true));
    CHECK(knob_visible(k, false) == !k.dev_only);
    // Developer-only choices are the last ones of a knob (the GUI stops at the first one).
    bool seen_dev = false;
    for (int c = 0; c < choice_count(k); ++c) {
      if (seen_dev) CHECK(k.choices[c].dev);
      seen_dev = seen_dev || k.choices[c].dev;
      CHECK(!(c == 0 && k.choices[c].dev));
      // Every mark is a key the choice writes; Off has none.
      for (const char* mk : k.choices[c].mark) {
        if (!mk) continue;
        CHECK(c > 0);
        bool found = false;
        for (const KV& kv : k.choices[c].kv) found = found || (kv.key && std::string(kv.key) == mk);
        CHECK(found);
      }
      for (const KeyNeed& n : k.required_on)
        if (n.key) CHECK(n.min_choice >= 1 && n.min_choice < choice_count(k));
    }
    // Every key a knob writes is a key of bg3rtx.ini.
    for (const std::string& key : knob_keys(k)) {
      if (!code_default(key)) CHECK_EQ(std::string(k.id) + " writes unknown key " + key, "");
    }
    // The launcher only has these pages (tools/launcher/ui/app.js `sections`, plus Developer):
    // a knob in any other category would be in the schema but never shown.
    if (!k.dev_only) {
      const std::string c = k.category;
      CHECK(c == "Lighting" || c == "Surfaces" || c == "Performance" || c == "Memory" || c == "Advanced");
    }
    // Off in every preset for experimental knobs.
    if (k.experimental)
      for (int p = 0; p < kPresetCount; ++p) CHECK(k.preset[p] == 0);
  }
  CHECK(user_exp == 10);   // Includes the default-off RR hair coverage resolve candidate, the temporal post candidate and strand hair on screen.
  CHECK(dev == (std::vector<std::string>{"exp_hair_gi_cull", "exp_hair_visibility", "exp_hair_gi_mean", "exp_cascade_static_only", "exp_hair_physical_origin", "exp_alpha_spill", "perf_hitmat_lists", "perf_hook_state", "perf_submit_batch", "exp_hit_reference", "exp_ls_exact_k", "exp_current_sun"}));
  const auto* submit_batch=find_knob("perf_submit_batch");CHECK(submit_batch!=nullptr);
  if(submit_batch){
    CHECK(submit_batch->apply==Apply::Live);CHECK(submit_batch->dev_only);
    for(int p=0;p<kPresetCount;++p) CHECK(submit_batch->preset[p]==0);
    CHECK_EQ(code_default("perf_submit_batch"),"0");
    CHECK(knob_value(*submit_batch,parse_ini("perf_submit_batch=1\n"))==1);
  }
  for (const char* id : {"exp_hit_reference", "exp_ls_exact_k", "exp_current_sun"}) CHECK(find_knob(id)->experimental);
  CHECK(dev_mode("1"));
  CHECK(!dev_mode("0"));
  CHECK(!dev_mode(code_default("dev")));   // the release profile hides them

  // Sun and qualified box shadows stay in the main list; only Ultra enables boxes.
  const Knob* roof = find_knob("roof_shadows");
  const Knob* box = find_knob("box_lights");
  CHECK(roof && box);
  if (roof && box) {
    CHECK(!roof->experimental && !box->experimental && !roof->advanced && !box->advanced);
    CHECK(knob_visible(*box, false));
    CHECK(!box->dev_only);
    CHECK(choice_count(*box) == 3);
    CHECK_EQ(box->choices[1].label, "Shadowed");
    CHECK_EQ(box->choices[2].label, "All");
    for (int c = 0; c < 3; ++c) CHECK_EQ(get(on_map(*box, c), "ls_box"), std::to_string(c));
    CHECK(roof->preset[kUltra] == 2);
    for (int p = 0; p < kPresetCount; ++p) CHECK(box->preset[p] == (p == kUltra ? 2 : 0));
    for (int p = 0; p < kPresetCount; ++p) {
      Map preset;
      for (const auto& kv : expand_preset(p)) preset[kv.first] = kv.second;
      CHECK_EQ(get(preset, "ls_box"), p == kUltra ? "2" : "0");
      Map effective = code_defaults();
      apply_layer(effective, parse_ini(std::string("preset=") + kPresetId[p] + "\n"));
      CHECK_EQ(get(effective, "ls_box_honour_shadow"), "1");
    }
    CHECK(knob_index("roof_shadows") < knob_index("gi"));
    CHECK(knob_index("box_lights") > knob_index("exp_water"));
  }

  // Migration selects the highest legacy signal and saving completes that bundle.
  const auto& bounce = *find_knob("exp_hidden_surface");
  for (auto signal : {std::pair<const char*, int>{"rt_hit_material", 1}, {"rt_hit_emission", 1},
                      {"gi_hit_direct", 1}, {"gi_hit_sun", 1}, {"gi_far", 2},
                      {"rf_hit_shade", 3}, {"rf_hit_gi", 3}}) {
    Map old = code_defaults(); old[signal.first] = "1";
    CHECK(knob_value(bounce, old) == signal.second);
    auto values = knob_values(old);
    auto saved = parse_ini(render_user_ini("", -1, values));
    for (const auto& kv : on_map(bounce, signal.second)) CHECK_EQ(get(saved, kv.first.c_str()), kv.second);
  }
  CHECK_EQ(get(on_map(*box, box->preset[kUltra]), "ls_box"), "2");
  CHECK_EQ(get(on_map(*box, box->preset[kHigh]), "ls_box"), "0");
  CHECK_EQ(get(on_map(*roof, 1), "rt_cascade_casters"), "0");
  CHECK_EQ(get(on_map(*roof, 2), "rt_cascade_casters"), "1");
  for (const char* id : {"exp_cascade_static_only", "exp_hair_physical_origin", "exp_alpha_spill"}) {
    CHECK(!knob_visible(*find_knob(id), false)); CHECK(knob_visible(*find_knob(id), true));
  }

  // Each level writes exactly these keys (a lower level resets the higher levels' keys).
  struct Level {
    const char* knob;
    std::vector<const char*> keys;
    std::vector<std::vector<const char*>> values;   // per choice
  };
  const Level levels[] = {
      {"exp_hidden_surface",
       {"rt_hit_material", "rt_hit_material_instanced", "rt_hit_material_wind_rgb", "gi_hit_direct", "gi_hit_sun",
        "rf_hit_shade", "rf_hit_gi", "rt_hit_emission", "rt_emissive", "gi_far", "gi_far_m"},
       {{"0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "6"},
        {"1", "1", "1", "1", "1", "0", "0", "1", "1", "0", "6"},
        {"1", "1", "1", "1", "1", "0", "0", "1", "1", "1", "6"},
        {"1", "1", "1", "1", "1", "1", "1", "1", "1", "1", "6"}}},
      {"exp_water",
       {"rt_water_geometry", "rt_water_reflections", "rt_water_compose", "rt_water_shadows", "rt_water_ls_shadows",
        "rt_water_game_shadow", "rt_water_refraction"},
       {{"0", "0", "0", "0", "0", "0", "0"},
        {"1", "1", "1", "0", "0", "0", "0"},
        {"1", "1", "1", "1", "1", "1", "0"},
        {"1", "1", "1", "1", "1", "1", "1"}}},
      {"exp_native_coverage", {"rt_native_hair", "rt_native_scalp", "rt_native_tileset"},
       {{"0", "0", "0"}, {"1", "1", "0"}, {"1", "1", "1"}}},
  };
  for (const Level& l : levels) {
    const Knob* k = find_knob(l.knob);
    CHECK(k != nullptr);
    if (!k) continue;
    // Promoted (Advanced, in Ultra): RT water (M5), Hidden-surface shading "+ Bounce" and Emission "Hidden hits" (M4),
    // Far-field sun bounce 6 m (gf2 031330).
    const std::string id = l.knob;
    CHECK(k->experimental == (id != "exp_water" && id != "exp_hidden_surface" && id != "exp_hit_emission" && id != "exp_gi_far") &&
          !k->dev_only);
    CHECK(choice_count(*k) == int(l.values.size()));
    for (int c = 0; c < choice_count(*k); ++c) {
      Map m = on_map(*k, c);
      size_t written = 0;
      for (size_t j = 0; j < l.keys.size(); ++j) {
        if (!l.values[c][j]) {
          CHECK(!m.count(l.keys[j]));
          continue;
        }
        ++written;
        CHECK_EQ(std::string(l.knob) + "[" + std::to_string(c) + "] " + l.keys[j] + "=" + get(m, l.keys[j]),
                 std::string(l.knob) + "[" + std::to_string(c) + "] " + l.keys[j] + "=" + l.values[c][j]);
      }
      CHECK(m.size() == written);
      CHECK(knob_value(*k, m) == c);   // each level reads back as itself
    }
    CHECK(knob_value(*k, code_defaults()) == 0);
  }
  CHECK(find_knob("local_shadows")->choices[3].dev);
  CHECK(find_knob("exp_hidden_surface")->apply == Apply::Restart);
  CHECK(find_knob("exp_water")->apply == Apply::Restart);
  CHECK(find_knob("exp_native_coverage")->apply == Apply::Live);
  CHECK(find_knob("exp_hair_physical_origin")->advanced);

  // No orphan key: every key the old entries wrote is still written by a knob or is automatic.
  for (const char* key : kOldKnobKeys) {
    bool automatic = false;
    for (const char* k : kAutomaticKeys) automatic = automatic || std::string(k) == key;
    if (!is_knob_key(key) && !automatic) CHECK_EQ(std::string("orphan key ") + key, "");
    CHECK(code_default(key) != nullptr);
  }
  CHECK(find_knob("exp_fg_letterbox") == nullptr);
  for (const char* id : {"exp_vram_guard", "exp_vram_compact", "exp_capture_elastic"})
    CHECK(find_knob(id) == nullptr);
  CHECK_EQ(code_default("rt_vram_guard_budget_mb"), "0");
  // Automatic fixes: default 1, written by no knob, in no preset or release override (A/B keys only).
  for (const char* k : kAutomaticKeys) {
    CHECK(!is_knob_key(k));
    CHECK_EQ(code_default(k), "1");
    for (int p = 0; p < kPresetCount; ++p) { Map m; for (auto& kv : expand_preset(p)) m[kv.first] = kv.second; CHECK(m.count(k) == 0); }   // no preset writes it
    CHECK(!key_change_needs_restart(k, "1", "0"));
  }
  // A hand-set automatic key (A/B off) survives a save.
  CHECK_EQ(get(parse_ini(render_user_ini("fg_letterbox=0\n", kHigh, preset_values(kHigh))), "fg_letterbox"), "0");

  // Old knob ids resolve to the new knob, and their "On" key alone reads back as the alias level.
  for (const auto& o : kOldOn) {
    const Knob* k = find_knob(o[0]);
    CHECK(k != nullptr);
    if (!k) continue;
    int want = -2;
    for (const KnobAlias& a : kKnobAliases)
      if (std::string(a.old_id) == o[0]) want = a.on_choice;
    CHECK(want >= 1 && want < choice_count(*k));
    Map m = code_defaults();
    m[o[1]] = "1";
    if (knob_value(*k, m) != want)
      CHECK_EQ(std::string(o[0]) + " -> " + std::to_string(knob_value(*k, m)), std::string(o[0]) + " -> " + std::to_string(want));
  }
  for (const KnobAlias& a : kKnobAliases) CHECK(knob_index(a.knob) >= 0 && knob_index(a.old_id) == knob_index(a.knob));

  // Dependencies: shown (unmet_need, unmet_key_need) and enforced on save (enforce_needs).
  {
    const int hs = knob_index("exp_hidden_surface");
    const int nc = knob_index("exp_native_coverage"), ho = knob_index("exp_hair_physical_origin");
    auto v = preset_values(kHigh);
    v[hs] = 2;
    v[knob_index("reflections")] = 0;
    CHECK(unmet_need(kKnobs[hs], 2, v).empty());
    CHECK(!unmet_need(kKnobs[hs], 3, v).empty());
    v[knob_index("gi")] = 1;
    CHECK(!unmet_need(kKnobs[hs], 1, v).empty());
    v = preset_values(kHigh);
    v[ho] = 1;
    CHECK(!unmet_need(kKnobs[ho], 1, v).empty());
    v[nc] = 1;
    CHECK(unmet_need(kKnobs[ho], 1, v).empty());
    CHECK(!unmet_need(kKnobs[hs], 1, preset_values(kMedium)).empty());   // Medium: opaque geometry only
    // enforce_needs lowers the chain and nothing else.
    v = preset_values(kHigh);
    v[hs] = 3;
    v[knob_index("reflections")] = 0;
    auto changed = enforce_needs(v);
    CHECK(v[hs] == 2);
    CHECK(changed == std::vector<int>{hs});
    v[knob_index("rt_geometry")] = 1;
    enforce_needs(v);
    CHECK(v[hs] == 0);
    for (int p = 0; p < kPresetCount; ++p) {
      auto pv = preset_values(p);
      CHECK(enforce_needs(pv).empty());
    }
    // Raw prerequisites from a level on: GI fallback 0 matters from + Bounce only.
    Map src = code_defaults();
    src["gi_rt_fallback"] = "1";
    CHECK(!unmet_key_need(kKnobs[hs], 1, src).empty());
    CHECK(!unmet_key_need(kKnobs[hs], 2, src).empty());
    src["gi_rt_fallback"] = "0";
    CHECK(unmet_key_need(kKnobs[hs], 3, src).empty());
    src["rt_bvh_split"] = "0";
    CHECK(!unmet_key_need(kKnobs[hs], 1, src).empty());
    CHECK(unmet_key_need(kKnobs[hs], 0, src).empty());
  }

  // Restart notes: the first enable of hidden-surface shading (rt_hit_material, the instanced rows'
  // startup patch, the wind RGB family) and of each water level; everything above is live.
  CHECK(key_change_needs_restart("rt_hit_material", "0", "1"));
  CHECK(key_change_needs_restart("rt_hit_material_wind_rgb", "0", "1"));
  CHECK(key_change_needs_restart("rt_hit_material_wind_rgb", "1", "0"));
  CHECK(key_change_needs_restart("rt_hit_material_instanced", "0", "1"));
  CHECK(!key_change_needs_restart("rt_hit_material_instanced", "1", "0"));
  CHECK(!key_change_needs_restart("rt_hit_material_recycle", "0", "1"));
  for (const char* key : {"gi_hit_direct", "gi_hit_sun", "rf_hit_shade", "rf_hit_gi", "rt_hit_emission", "rt_emissive",
                          "gi_far", "gi_far_m", "ls_box", "rt_alpha_spill", "fg_letterbox", "ls_k", "rt_hit_reference"})
    CHECK(!key_change_needs_restart(key, "0", "1"));
  for (const char* key : {"rt_water_compose", "rt_water_shadows", "rt_water_ls_shadows", "rt_water_game_shadow",
                          "rt_water_refraction"}) {
    CHECK(key_change_needs_restart(key, "0", "1"));
    CHECK(!key_change_needs_restart(key, "1", "0"));
  }
  CHECK(!key_change_needs_restart("rt_water_geometry", "0", "1"));
  for (const char* key : {"rt_native_tileset", "rt_omm", "rt_sun_cascade_current"})
    CHECK(key_change_needs_restart(key, "0", "1"));
  CHECK(!key_change_needs_restart("rt_native_scalp", "0", "1"));
}

// The user's bg3rtx_user.ini of 2026-11-01 (copied read-only into tests/fixtures/settings): it loads as
// levels, and saving it keeps every key it had and adds only the keys of those levels.
static void test_user_ini_migration() {
  const std::string text = read_file(BG3RTX_SOURCE_DIR "/tests/fixtures/settings/user_ini_20261101.ini");
  CHECK(!text.empty());
  const Map base = parse_ini(read_file(BG3RTX_SOURCE_DIR "/bg3rtx.ini"));
  const Map user = parse_ini(text);
  Map raw;
  apply_layer(raw, base);
  apply_layer(raw, user);
  std::vector<int> v = knob_values(raw);
  for (int i = 0; i < kKnobCount; ++i)
    if (v[i] < 0) CHECK_EQ(std::string(kKnobs[i].id) + " reads back as custom", "");
  CHECK(preset_from_id(get(user, "preset")) == kUltra);
  CHECK(v[knob_index("box_lights")] == 2);
  CHECK(v[knob_index("exp_alpha_spill")] == 1);
  CHECK(v[knob_index("exp_hidden_surface")] == 3);
  CHECK(v[knob_index("exp_water")] == 3);
  CHECK(v[knob_index("exp_native_coverage")] == 0);
  CHECK(v[knob_index("roof_shadows")] == 2);   // Ultra
  // The file is already enforceable: nothing it selects lacks a dependency in Ultra.
  auto enforced = v;
  CHECK(enforce_needs(enforced).empty());

  const std::string out = render_user_ini(text, kUltra, v);
  const Map w = parse_ini(out);
  // Effective values of the saved file (bg3rtx.ini, then its preset, then its keys): since RT water is on in
  // Ultra, the file no longer needs to write the water keys (they equal the preset's), so check effective values.
  const Map ew = merge_layers({&base, &w});
  // Every key of the old file keeps its value (except where a level adds keys it lacked).
  for (auto& kv : user)
    if (!same_value(get(ew, kv.first.c_str()), kv.second))
      CHECK_EQ(kv.first + "=" + get(ew, kv.first.c_str()), kv.first + "=" + kv.second);
  for (const char* key : {"rt_water_compose", "rt_water_shadows", "rt_water_refraction"})
    CHECK(!w.count(key));   // from the Ultra preset now
  // The levels' keys the file lacked.
  for (const char* key : {"rt_hit_material_wind_rgb", "rf_hit_gi", "rt_water_game_shadow"})
    CHECK_EQ(get(ew, key), "1");
  CHECK(get(ew, "rt_hit_material_recycle") == "1");   // automatic (default 1): no level writes it
  CHECK_EQ(get(ew, "rt_emissive"), "1");            // Emission at ray hits: Ultra's own level now (Hidden hits)
  CHECK(out.find("rt_emissive=1") == out.rfind("rt_emissive=1"));   // once, no longer under "Other keys"
  CHECK_EQ(get(w, "rt_xfb_mb"), "1024");            // unmanaged keys kept
  CHECK_EQ(get(w, "rt_cascade_mb"), "427");
  CHECK_EQ(get(w, "rt_debug"), "0");
  // Round trip: the saved file reads back as the same levels, now exactly.
  Map raw2;
  apply_layer(raw2, base);
  apply_layer(raw2, w);
  CHECK(knob_values(raw2) == v);
  CHECK_EQ(render_user_ini(out, kUltra, v), out);
  // No added key needs a restart any more: the wind RGB rows and the pond's game shadow are turned on by the
  // Ultra preset itself now (Hidden-surface shading "+ Bounce", RT water; raw holds the preset's 1).
  int restart = 0;
  for (const char* key : {"rt_hit_material_wind_rgb", "rf_hit_gi", "rt_water_game_shadow"})
    restart += key_change_needs_restart(key, lookup(raw, key), get(ew, key));
  CHECK(restart == 0);
  CHECK_EQ(lookup(raw, "rt_hit_material_wind_rgb"), "1");
  CHECK_EQ(lookup(raw, "rt_water_game_shadow"), "1");
}

static void test_rendering_experiments() {
  const auto kept=parse_ini(render_user_ini("rt=0\nrt_bvh_split=0\ncustom_key=7\n",kHigh,preset_values(kHigh)));
  CHECK_EQ(get(kept,"rt"),"0");CHECK_EQ(get(kept,"rt_bvh_split"),"0");CHECK_EQ(get(kept,"custom_key"),"7");
  CHECK(!unmet_key_need(*find_knob("exp_omm"),1,kept).empty());
  CHECK(unmet_key_need(*find_knob("exp_omm"),0,kept).empty());
  CHECK(unmet_key_need(*find_knob("exp_omm"),1,code_defaults()).empty());
  CHECK(schema_json().find("\"required_on\"")!=std::string::npos);
  // Every experimental knob: its keys are off in every preset and a saved non-Off value reads back.
  for(const Knob& k:kKnobs){
    if(!k.experimental)continue;
    for(int p=0;p<kPresetCount;++p){Map m;for(auto& kv:expand_preset(p))m[kv.first]=kv.second;
      for(const std::string& key:knob_keys(k))if(m.count(key)&&!same_value(m[key],code_default(key)))CHECK_EQ(std::string(k.id)+" preset "+key+"="+m[key],"");}
    for(int c=1;c<choice_count(k);++c){
      auto values=preset_values(kHigh);values[knob_index(k.id)]=c;
      auto parsed=parse_ini(render_user_ini("# preserved\ncustom_key=7\n",kHigh,values));
      CHECK_EQ(get(parsed,"custom_key"),"7");CHECK(knob_value(k,parsed)==c);
      CHECK(!unmet_need(k,c,preset_values(kLow)).empty());
    }
  }
  // ls_k (TRACK_D_LOCAL_LIGHTS 9.11), developer only: needs Local light shadows Full.
  {
    const auto* lk=find_knob("exp_ls_exact_k");CHECK(lk!=nullptr);
    if(lk){
      CHECK(choice_count(*lk)==3);CHECK_EQ(code_default("ls_k"),"0");CHECK_EQ(code_default("ls_rest_samples"),"1");
      for(int c=0;c<=2;++c){Map on=on_map(*lk,c);CHECK_EQ(get(on,"ls_k"),std::to_string(c));CHECK_EQ(get(on,"ls_rest_samples"),"1");CHECK(knob_value(*lk,on)==c);}
      auto v=preset_values(kHigh);v[knob_index("exp_ls_exact_k")]=1;CHECK(unmet_need(*lk,1,v).empty());
      v[knob_index("local_shadows")]=1;CHECK(!unmet_need(*lk,1,v).empty());
    }
  }
  CHECK(find_knob("exp_native_coverage")->apply==Apply::Live);
  Gpu gpu{0x10de,0x2b85,"NVIDIA GeForce RTX 5090",true,true};
  CHECK(!choice_supported(*find_knob("exp_omm"),1,gpu_caps(gpu)));
  gpu.opacity_micromap=true;CHECK(choice_supported(*find_knob("exp_omm"),1,gpu_caps(gpu)));
  gpu.ray_query=false;CHECK(!(gpu_caps(gpu)&kCapOpacityMicromap));
  CHECK(schema_json().find("\"experimental\": true")!=std::string::npos);
  CHECK(schema_json().find("\"experimental\": false")!=std::string::npos);
  CHECK(schema_json().find("\"developer\": true")!=std::string::npos);
  CHECK(schema_json().find("opacity_micromap")!=std::string::npos);
}

static void test_schema_file() {
  std::string want = schema_json();
  std::string have = read_file(BG3RTX_SOURCE_DIR "/data/settings_schema.json");
  // Line endings may differ with git's autocrlf.
  std::string norm;
  for (char ch : have)
    if (ch != '\r') norm += ch;
  if (norm != want)
    std::printf("data/settings_schema.json is stale: run bin/settings_test.exe --write-schema\n");
  CHECK(norm == want);
}

// Stage 3 of the M5 water promotion (m5_promotion passed 11-01): RT water "+ Refraction" in Ultra, Off elsewhere.
static void test_ultra_water() {
  const char* keys[] = {"rt_water_geometry", "rt_water_reflections", "rt_water_compose", "rt_water_shadows",
                        "rt_water_ls_shadows", "rt_water_game_shadow", "rt_water_refraction"};
  const Knob* w = find_knob("exp_water");
  CHECK(w != nullptr);
  if (!w) return;
  CHECK(!w->experimental && w->advanced && !w->dev_only);
  CHECK(w->preset[kUltra] == 3 && w->preset[kLow] == 0 && w->preset[kMedium] == 0 && w->preset[kHigh] == 0);
  for (int p = 0; p < kPresetCount; ++p) {
    Map m;
    for (auto& kv : expand_preset(p)) m[kv.first] = kv.second;
    for (const char* k : keys) {
      const std::string want = p == kUltra ? "1" : "0";
      CHECK_EQ(std::string(kPresetId[p]) + " " + k + "=" + get(m, k), std::string(kPresetId[p]) + " " + k + "=" + want);
    }
    // Ultra: every dependency met (knobs and the startup keys: rt, sharc_device).
    std::vector<int> v = preset_values(p);
    CHECK(enforce_needs(v).empty());
    if (p == kUltra) {
      Map eff = code_defaults();
      for (auto& kv : expand_preset(p)) eff[kv.first] = kv.second;
      CHECK(unmet_need(*w, 3, preset_values(p)).empty());
      CHECK(unmet_key_need(*w, 3, eff).empty());
      eff["sharc_device"] = "0";   // no 64-bit shader integers: the compose cannot be created
      CHECK(!unmet_key_need(*w, 1, eff).empty());
      CHECK(knob_value(*w, eff) == 3);
    }
  }
  // The compose keys need a restart to turn on and are live to turn off; geometry and reflections are live.
  for (const char* k : {"rt_water_compose", "rt_water_shadows", "rt_water_ls_shadows", "rt_water_game_shadow",
                        "rt_water_refraction"}) {
    CHECK(key_change_needs_restart(k, "0", "1"));
    CHECK(!key_change_needs_restart(k, "1", "0"));
  }
  CHECK(!key_change_needs_restart("rt_water_geometry", "0", "1") && !key_change_needs_restart("rt_water_reflections", "0", "1"));
  CHECK(key_change_needs_restart("sharc_device", "1", "0"));
  CHECK(w->apply == Apply::Restart);
  // Before the Experimental section in the GUI order (after every main entry, before the first experimental one).
  int iw = knob_index("exp_water"), first_exp = kKnobCount;
  for (int i = 0; i < kKnobCount; ++i)
    if (kKnobs[i].experimental) first_exp = (std::min)(first_exp, i);
  CHECK(iw < first_exp);
  const Knob& hair_resolve = *find_knob("rr_hair_resolve");
  CHECK(hair_resolve.experimental && !hair_resolve.dev_only);
  CHECK(hair_resolve.apply == Apply::Live);
  CHECK_EQ(hair_resolve.choices[1].need[0].knob, "rr");
  CHECK_EQ(hair_resolve.required_on[0].key, "rr_post");
  CHECK_EQ(hair_resolve.required_on[0].value, "1");
  for (int preset = 0; preset < kPresetCount; ++preset) {
    Map expanded;
    for (const auto& kv : expand_preset(preset)) expanded[kv.first] = kv.second;
    CHECK_EQ(get(expanded, "rr_hair_resolve"), "0");
  }

}

static void test_product_policy() {
  // CPU perf experiments remain hidden from players, including their Off choices.
  for (const char* id : {"perf_hitmat_lists", "perf_hook_state", "perf_submit_batch"}) {
    const Knob& k = *find_knob(id);
    CHECK(k.dev_only);
    CHECK(!knob_visible(k, false));
    CHECK(knob_visible(k, true));
    CHECK_EQ(code_default(id), "0");
    for (int p = 0; p < kPresetCount; ++p) CHECK(k.preset[p] == 0);
    for (int c = 0; c < choice_count(k); ++c) {
      CHECK(!choice_visible(k, c, false));
      CHECK_EQ(choice_maturity(k, c), "developer");
    }
  }
  const auto& emission = *find_knob("exp_hit_emission");
  Map legacy = code_defaults();
  apply_layer(legacy, parse_ini("preset=ultra\ndev=0\nrt_hit_emission=2\nrt_emissive=1\nunknown_future_key=kept\n"));
  CHECK(knob_value(emission, legacy) == -1);
  CHECK(!choice_visible(*find_knob("local_shadows"), 3, false));
  CHECK(choice_visible(*find_knob("local_shadows"), 3, true));
  auto presented = presentation_values(legacy, false);
  CHECK(presented[knob_index(emission.id)] == -1);
  Map saved = parse_ini(render_user_ini("rt_hit_emission=2\nrt_emissive=1\nunknown_future_key=kept\n", kUltra, presented));
  CHECK_EQ(get(saved, "rt_hit_emission"), "2");
  CHECK_EQ(get(saved, "unknown_future_key"), "kept");
  Map reloaded = code_defaults(); apply_layer(reloaded, saved);
  CHECK(knob_value(emission, reloaded) == -1);
  CHECK_EQ(choice_maturity(*find_knob("exp_hidden_surface"), 3), "experimental");
  CHECK(!choice_available(*find_knob("exp_hidden_surface"), 3, false, false, ~0u));
  CHECK(choice_available(*find_knob("exp_hidden_surface"), 3, false, true, ~0u));
  CHECK_EQ(choice_maturity(*find_knob("exp_hidden_surface"), 2), "qualified");
  CHECK_EQ(choice_maturity(*find_knob("box_lights"), 2), "qualified");
  CHECK_EQ(find_knob("exp_water")->category, "Surfaces");
  CHECK_EQ(find_knob("reflex")->category, "Performance");
  CHECK_EQ(find_knob("exp_water")->cost.status, "Estimate");
  CHECK_EQ(find_knob("exp_omm")->cost.status, "Measured");
  const auto& omm = *find_knob("exp_omm");
  const auto& masks = *find_knob("exp_vt_alpha_masks");
  CHECK(!omm.experimental && !omm.dev_only && omm.advanced && knob_visible(omm, false));
  CHECK(!masks.experimental && !masks.dev_only && knob_visible(masks, false));
  CHECK_EQ(choice_maturity(omm, 1), "qualified");
  CHECK(omm.apply == Apply::Restart);
  for (int p = 0; p < kPresetCount; ++p) {
    Map preset;
    for (const auto& kv : expand_preset(p)) preset[kv.first] = kv.second;
    for (const char* key : {"rt_omm", "rt_omm_dyn", "rt_vt_alpha_masks", "rt_alpha_fine_lod"})
      CHECK_EQ(get(preset, key), p == kUltra ? "1" : "0");
    CHECK_EQ(get(preset, "rt_omm_reference"), "0");
    auto values = preset_values(p); CHECK(enforce_needs(values).empty());
  }
  Map disabled = on_map(omm, 0);
  for (const char* key : {"rt_omm", "rt_omm_dyn", "rt_alpha_fine_lod", "rt_omm_reference"})
    CHECK_EQ(get(disabled, key), "0");
  CHECK_EQ(find_knob("exp_water")->choices[3].label, "Reflections + shadows + refraction");

  auto edited = preset_values(kMedium);
  edited[knob_index("rr")] = 0;
  edited[knob_index("reflex")] = 0;
  const auto original = edited;
  auto lowered = preview_dependencies(edited, code_defaults(), ~0u, false);
  CHECK(edited == original); // Preview has no side effects.
  CHECK(lowered.values[knob_index("local_shadows")] == 0);
  CHECK(lowered.values[knob_index("frame_generation")] == 0);
  auto enabled = preview_dependencies(edited, code_defaults(), ~0u, true);
  CHECK(enabled.blocked.empty());
  CHECK(enabled.values[knob_index("rr")] == 1);
  CHECK(enabled.values[knob_index("reflex")] == 1);
  CHECK(enabled.changed.size() == 2);
  CHECK(enabled.required_keys.empty());
  edited[knob_index("local_shadows")] = 2;
  CHECK(unmet_need(*find_knob("local_shadows"), 2, edited).empty()); // Full does not require RR.
  edited[knob_index("rr")] = -1;
  edited[knob_index("local_shadows")] = 1;
  CHECK(!preview_dependencies(edited, code_defaults(), ~0u, true).blocked.empty());
  CHECK(!preview_dependencies(original, code_defaults(), kCapRayQuery, true).blocked.empty());

  // Startup preview lists raw adjustments, but does not duplicate geometry's own keys.
  auto water = preset_values(kHigh);
  water[knob_index("exp_water")] = 3;
  Map startup = code_defaults(); startup["rt"] = "0"; startup["sharc_device"] = "0";
  auto preview = preview_dependencies(water, startup, ~0u, true);
  CHECK(preview.required_keys.size() == 2);
  CHECK_EQ(startup["rt"], "0");
  for (int p = 0; p < kPresetCount; ++p) {
    for (unsigned caps : {0u, unsigned(kCapRayQuery), unsigned(kCapRayQuery | kCapDlss), ~0u}) {
      auto values = preset_values_for(p, caps);
      for (int i = 0; i < kKnobCount; ++i) {
        CHECK(unmet_need(kKnobs[i], values[i], values).empty());
        CHECK(choice_supported(kKnobs[i], values[i], caps));
        if (kKnobs[i].kind == Kind::Choice)
          CHECK_EQ(choice_maturity(kKnobs[i], values[i]), kKnobs[i].dev_only ? "developer" : "qualified");
      }
    }
  }
  auto chain = preset_values(kLow);
  chain[knob_index("exp_hidden_surface")] = 2;
  Map raw = code_defaults(); raw["gi_rt_fallback"] = "1";
  auto transitive = preview_dependencies(chain, raw, ~0u, true);
  CHECK(transitive.blocked.empty());
  CHECK(transitive.values[knob_index("exp_hidden_surface")] == 2);
  CHECK(transitive.values[knob_index("rt_geometry")] == 2);
  CHECK(transitive.values[knob_index("gi")] == 2);
  CHECK(transitive.changed.size() == 2);
  CHECK(transitive.required_keys.size() == 1);
  CHECK_EQ(transitive.required_keys[0].first, "gi_rt_fallback");
  CHECK_EQ(transitive.required_keys[0].second, "0");
  CHECK_EQ(choice_maturity(emission, -1), "custom");
  CHECK(!choice_visible(emission, -1, false));
  CHECK(!choice_visible(emission, 99, true));
  CHECK(!preview_dependencies({}, raw, ~0u, true).blocked.empty());
  // Old partial water files migrate to a complete level; unmatched custom sliders survive saving.
  Map old = code_defaults(); apply_layer(old, parse_ini("preset=high\nrt_water_refraction=1\ngi_strength=0.63\nls_strength=0.71\n"));
  CHECK(knob_value(*find_knob("exp_water"), old) == 3);
  auto custom = presentation_values(old, false);
  CHECK(custom[knob_index("rt_intensity")] == -1);
  auto retained = parse_ini(render_user_ini("gi_strength=0.63\nls_strength=0.71\n", kHigh, custom));
  CHECK_EQ(get(retained, "gi_strength"), "0.63");
  CHECK_EQ(get(retained, "ls_strength"), "0.71");
  CHECK(schema_json().find("\"maturity\": \"experimental\"") != std::string::npos);
}

static void test_apply_modes() {
  for(const auto& k:kKnobs) {
    CHECK(k.apply!=Apply::Unspecified);
    CHECK(apply_label(k.apply)[0]!=0);
    for(int c=0;c<choice_count(k);++c) {
      CHECK(k.choices[c].apply!=Apply::Unspecified);
      CHECK(apply_label(k.choices[c].apply)[0]!=0);
      CHECK(knob_apply(k,c)==k.choices[c].apply);
    }
  }
  CHECK_EQ(apply_label(Apply::Live),"Live");
  CHECK_EQ(apply_label(Apply::Hitch),"Applies after a short hitch");
  CHECK_EQ(apply_label(Apply::Restart),"Restart to apply");
  // Persisted startup resources cannot be added by a core reload (core/gi.cpp).
  CHECK(key_change_needs_restart("rt_hair_physical_origin","0","1"));
  CHECK(!key_change_needs_restart("rt_hair_physical_origin","1","0"));
  for (const auto* key : {"rt_hair_visibility", "rt_hair_pool_trim", "rt_hair_cheap", "rt_hair_gi_mean"}) {
    const auto& knob = *find_knob(std::string("exp_") + (key + 3));
    CHECK(knob.experimental);
    CHECK(knob.dev_only == (std::string(key)=="rt_hair_visibility" || std::string(key)=="rt_hair_gi_mean"));
    CHECK_EQ(code_default(key), "0");
    CHECK(!key_change_needs_restart(key, "0", "1"));
    for(int preset=0;preset<kPresetCount;++preset)CHECK(knob.preset[preset]==0);
    for(int choice=0;choice<2;++choice)CHECK(knob_apply(knob,choice)==Apply::Live);
  }
  // Player trial toggles are visible, default-off and live in both directions.
  for (const auto* id : {"rr_hair_resolve", "rr_post", "exp_water_compose_shift", "exp_water_hit_shade",
                         "exp_native_coverage", "exp_hair_groom", "exp_hair_pool_trim", "exp_hair_cheap"}) {
    const auto& k=*find_knob(id);
    CHECK(k.experimental && knob_visible(k,false) && !k.dev_only);
    CHECK(k.choices[0].apply==Apply::Live || k.choices[0].apply==Apply::Hitch);
    CHECK(k.choices[1].apply==Apply::Live || k.choices[1].apply==Apply::Hitch);
    for(int p=0;p<kPresetCount;++p)CHECK(k.preset[p]==0);
  }
  CHECK_EQ(get(on_map(*find_knob("rr_post"),0),"rr_post"),"1");
  CHECK_EQ(get(on_map(*find_knob("rr_post"),1),"rr_post"),"0");
  for(const char* key : {"rt_native_hair","rt_native_scalp","rt_hair_groom","rr_post"}) {
    CHECK(!key_change_needs_restart(key,"0","1"));
    CHECK(!key_change_needs_restart(key,"1","0"));
  }
  CHECK(find_knob("exp_native_coverage")->choices[2].dev);
  const auto& hair_gi_cull = *find_knob("exp_hair_gi_cull");
  CHECK(hair_gi_cull.dev_only && hair_gi_cull.experimental);
  CHECK_EQ(code_default("rt_hair_gi_cull"), "0");
  CHECK(!key_change_needs_restart("rt_hair_gi_cull", "0", "1"));
  CHECK(!key_change_needs_restart("rt_hair_gi_cull", "1", "0"));
  for (int preset = 0; preset < kPresetCount; ++preset) CHECK(hair_gi_cull.preset[preset] == 0);
  for (int choice = 0; choice < 2; ++choice) CHECK(knob_apply(hair_gi_cull, choice) == Apply::Live);
  CHECK(key_change_needs_restart("rt_bvh_split","0","1"));
  CHECK(!key_change_needs_restart("rt_water_compose","0","0"));
  for (int c = 0; c < 4; ++c) CHECK(knob_apply(*find_knob("exp_hidden_surface"), c) == Apply::Restart);
  for (int c = 0; c < 3; ++c) CHECK(knob_apply(*find_knob("sun_shadows"), c) == Apply::Live);
  const auto& static_casters = *find_knob("exp_cascade_static_only");
  CHECK(static_casters.dev_only && static_casters.experimental);
  CHECK_EQ(code_default("rt_cascade_static_only"), "0");
  for (int c = 0; c < 2; ++c) {
    CHECK(knob_apply(static_casters, c) == Apply::Live);
    CHECK_EQ(get(on_map(static_casters, c), "rt_cascade_static_only"), std::to_string(c));
  }
  CHECK_EQ(static_casters.choices[1].need[0].knob, "sun_shadows");
  CHECK(static_casters.choices[1].need[0].min_choice == 2);
  CHECK(knob_apply(*find_knob("exp_water"),0)==Apply::Live);
  CHECK(knob_apply(*find_knob("exp_water"),3)==Apply::Restart);
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--write-schema") {
    std::ofstream f(BG3RTX_SOURCE_DIR "/data/settings_schema.json", std::ios::binary);
    f << schema_json();
    std::printf("wrote data/settings_schema.json\n");
    return 0;
  }
  test_apply_modes();
  test_product_policy();
  test_parse();
  test_preset_expansion();
  test_layering();
  test_derived_rt_build();
  test_read_back();
  test_dev_ini_is_high();
  test_defaults_one_source();
  test_user_ini_writer();
  test_dependencies_and_caps();
  test_rendering_experiments();
  test_gui_levels();
  test_user_ini_migration();
  test_ultra_water();
  test_schema_file();
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed;
}
