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

static void test_rendering_experiments() {
  const char* ids[]={"exp_native_scalp","exp_native_hair","exp_native_tileset","exp_omm","exp_current_sun","exp_hit_material","exp_hit_material_instanced","exp_gi_hit_direct","exp_gi_hit_sun","exp_rf_hit_shade","exp_water_geometry","exp_water_reflections","exp_water_compose","exp_water_shadows","exp_water_ls_shadows"};
  const char* keys[]={"rt_native_scalp","rt_native_hair","rt_native_tileset","rt_omm","rt_sun_cascade_current","rt_hit_material","rt_hit_material_instanced","gi_hit_direct","gi_hit_sun","rf_hit_shade","rt_water_geometry","rt_water_reflections","rt_water_compose","rt_water_shadows","rt_water_ls_shadows"};
  static_assert(std::size(ids)==std::size(keys));
  for(size_t i=0;i<std::size(ids);++i){
    const auto* knob=find_knob(ids[i]);CHECK(knob!=nullptr);if(!knob)continue;
    CHECK(knob->experimental);CHECK(choice_count(*knob)==2);CHECK_EQ(code_default(keys[i]),"0");
    for(int p=0;p<kPresetCount;++p){Map m;for(auto& kv:expand_preset(p))m[kv.first]=kv.second;CHECK_EQ(get(m,keys[i]),"0");CHECK(knob->preset[p]==0);}
    KVList pairs;knob_kvs(*knob,1,pairs);Map on;for(auto& kv:pairs)on[kv.first]=kv.second;
    CHECK(knob_value(*knob,on)==1);CHECK_EQ(get(on,keys[i]),"1");
    auto values=preset_values(kHigh);values[knob_index(ids[i])]=1;
    auto user=render_user_ini("# preserved\ncustom_key=7\n",kHigh,values);auto parsed=parse_ini(user);
    CHECK_EQ(get(parsed,"custom_key"),"7");CHECK(knob_value(*knob,parsed)==1);
    CHECK(!unmet_need(*knob,1,preset_values(kLow)).empty());
  }
  const auto kept=parse_ini(render_user_ini("rt=0\nrt_bvh_split=0\ncustom_key=7\n",kHigh,preset_values(kHigh)));
  CHECK_EQ(get(kept,"rt"),"0");CHECK_EQ(get(kept,"rt_bvh_split"),"0");
  CHECK(!unmet_key_need(*find_knob("exp_omm"),1,kept).empty());
  CHECK(unmet_key_need(*find_knob("exp_omm"),0,kept).empty());
  CHECK(unmet_key_need(*find_knob("exp_omm"),1,code_defaults()).empty());
  CHECK(schema_json().find("\"required_on\"")!=std::string::npos);
  CHECK(key_change_needs_restart("rt_native_hair","0","1"));
  CHECK(key_change_needs_restart("rt_native_tileset","0","1"));
  CHECK(key_change_needs_restart("rt_omm","1","0"));
  CHECK(key_change_needs_restart("rt_sun_cascade_current","0","1"));
  CHECK(key_change_needs_restart("rt_hit_material","0","1"));
  CHECK(!key_change_needs_restart("gi_hit_direct","0","1"));
  CHECK(!key_change_needs_restart("rt_hit_material_instanced","1","0"));   // startup patch, then live
  CHECK(key_change_needs_restart("rt_water_compose","0","1"));    // variants are created with the pipelines
  CHECK(!key_change_needs_restart("rt_water_compose","1","0"));   // off is live (the game's pipeline)
  auto gi_sources=code_defaults();gi_sources["rt_hit_material"]="1";gi_sources["gi_rt_fallback"]="1";
  CHECK(!unmet_key_need(*find_knob("exp_gi_hit_direct"),1,gi_sources).empty());
  gi_sources["gi_rt_fallback"]="0";CHECK(unmet_key_need(*find_knob("exp_gi_hit_direct"),1,gi_sources).empty());
  gi_sources["rt_hit_material"]="0";CHECK(!unmet_key_need(*find_knob("exp_gi_hit_direct"),1,gi_sources).empty());
  CHECK(unmet_key_need(*find_knob("exp_gi_hit_direct"),0,gi_sources).empty());
  CHECK(!key_change_needs_restart("gi_hit_sun","0","1"));
  auto sun_sources=code_defaults();
  CHECK(!unmet_key_need(*find_knob("exp_gi_hit_sun"),1,sun_sources).empty());   // gi_hit_direct=0 by default
  sun_sources["gi_hit_direct"]="1";CHECK(unmet_key_need(*find_knob("exp_gi_hit_sun"),1,sun_sources).empty());
  CHECK(unmet_key_need(*find_knob("exp_gi_hit_sun"),0,sun_sources).empty());
  // gi_far: experimental, off in every preset, live, needs gi_hit_sun; choices off / 4 / 6 / 8 m.
  {
    const auto* far=find_knob("exp_gi_far");CHECK(far!=nullptr);
    if(far){
      CHECK(far->experimental);CHECK(choice_count(*far)==4);CHECK_EQ(code_default("gi_far"),"0");CHECK_EQ(code_default("gi_far_m"),"6");
      for(int p=0;p<kPresetCount;++p){Map m;for(auto& kv:expand_preset(p))m[kv.first]=kv.second;CHECK_EQ(get(m,"gi_far"),"0");CHECK(far->preset[p]==0);}
      const char* metres[]={"4","6","8"};
      for(int c=1;c<4;++c){KVList pairs;knob_kvs(*far,c,pairs);Map on;for(auto& kv:pairs)on[kv.first]=kv.second;
        CHECK_EQ(get(on,"gi_far"),"1");CHECK_EQ(get(on,"gi_far_m"),metres[c-1]);CHECK(knob_value(*far,on)==c);}
      CHECK(knob_value(*far,code_defaults())==0);
      CHECK(!key_change_needs_restart("gi_far","0","1"));CHECK(!key_change_needs_restart("gi_far_m","6","8"));
      auto far_sources=code_defaults();
      CHECK(!unmet_key_need(*far,1,far_sources).empty());   // gi_hit_sun=0 by default
      far_sources["gi_hit_sun"]="1";CHECK(unmet_key_need(*far,1,far_sources).empty());
      CHECK(!unmet_need(*far,2,preset_values(kHigh)).empty());   // needs the Sunlit hidden-surface bounce knob
    }
  }
  CHECK(!key_change_needs_restart("rf_hit_shade","0","1"));
  auto rf_sources=code_defaults();
  CHECK(!unmet_key_need(*find_knob("exp_rf_hit_shade"),1,rf_sources).empty());   // rt_hit_material=0 by default
  rf_sources["rt_hit_material"]="1";CHECK(unmet_key_need(*find_knob("exp_rf_hit_shade"),1,rf_sources).empty());
  rf_sources["rt_reflections"]="0";CHECK(!unmet_key_need(*find_knob("exp_rf_hit_shade"),1,rf_sources).empty());
  CHECK(unmet_key_need(*find_knob("exp_rf_hit_shade"),0,rf_sources).empty());
  // rt_hit_emission (M4.4): three choices, off in every preset, live, needs the D-EM capture at startup.
  {
    const auto* em=find_knob("exp_hit_emission");CHECK(em!=nullptr);
    if(em){
      CHECK(em->experimental);CHECK(choice_count(*em)==3);CHECK_EQ(code_default("rt_hit_emission"),"0");
      for(int p=0;p<kPresetCount;++p){Map m;for(auto& kv:expand_preset(p))m[kv.first]=kv.second;CHECK_EQ(get(m,"rt_hit_emission"),"0");CHECK(em->preset[p]==0);}
      for(int c=1;c<=2;++c){KVList pairs;knob_kvs(*em,c,pairs);Map on;for(auto& kv:pairs)on[kv.first]=kv.second;
        CHECK_EQ(get(on,"rt_hit_emission"),c==1?"1":"2");CHECK(knob_value(*em,on)==c);}
      CHECK(!key_change_needs_restart("rt_hit_emission","0","2"));
      auto em_sources=code_defaults();em_sources["rt_hit_material"]="1";
      CHECK(!unmet_key_need(*em,1,em_sources).empty());   // rt_emissive=0 by default
      em_sources["rt_emissive"]="1";CHECK(unmet_key_need(*em,1,em_sources).empty());CHECK(unmet_key_need(*em,2,em_sources).empty());
      em_sources["rt_hit_material"]="0";CHECK(!unmet_key_need(*em,2,em_sources).empty());
      CHECK(unmet_key_need(*em,0,em_sources).empty());
    }
  }
  // ls_k (TRACK_D_LOCAL_LIGHTS 9.11): off / k1 / k2, off in every preset, live, needs Local light shadows Full.
  {
    const auto* lk=find_knob("exp_ls_exact_k");CHECK(lk!=nullptr);
    if(lk){
      CHECK(lk->experimental);CHECK(choice_count(*lk)==3);CHECK_EQ(code_default("ls_k"),"0");CHECK_EQ(code_default("ls_rest_samples"),"1");
      for(int p=0;p<kPresetCount;++p){Map m;for(auto& kv:expand_preset(p))m[kv.first]=kv.second;CHECK_EQ(get(m,"ls_k"),"0");CHECK(lk->preset[p]==0);}
      for(int c=0;c<=2;++c){KVList pairs;knob_kvs(*lk,c,pairs);Map on;for(auto& kv:pairs)on[kv.first]=kv.second;
        CHECK_EQ(get(on,"ls_k"),c==0?"0":c==1?"1":"2");CHECK_EQ(get(on,"ls_rest_samples"),"1");CHECK(knob_value(*lk,on)==c);}
      CHECK(!key_change_needs_restart("ls_k","0","1"));
      auto v=preset_values(kHigh);v[knob_index("exp_ls_exact_k")]=1;CHECK(unmet_need(*lk,1,v).empty());   // High: Full local shadows
      v[knob_index("local_shadows")]=1;CHECK(!unmet_need(*lk,1,v).empty());   // Fast (RIS) cannot take it
      CHECK(!unmet_need(*lk,2,preset_values(kLow)).empty());
    }
  }
  CHECK(!key_change_needs_restart("rt_native_scalp","0","1"));
  CHECK(find_knob("exp_native_scalp")->apply==Apply::Hitch);
  Gpu gpu{0x10de,0x2b85,"NVIDIA GeForce RTX 5090",true,true};
  CHECK(!choice_supported(*find_knob("exp_omm"),1,gpu_caps(gpu)));
  gpu.opacity_micromap=true;CHECK(choice_supported(*find_knob("exp_omm"),1,gpu_caps(gpu)));
  gpu.ray_query=false;CHECK(!(gpu_caps(gpu)&kCapOpacityMicromap));
  CHECK(schema_json().find("\"experimental\": true")!=std::string::npos);
  CHECK(schema_json().find("\"experimental\": false")!=std::string::npos);
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

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--write-schema") {
    std::ofstream f(BG3RTX_SOURCE_DIR "/data/settings_schema.json", std::ios::binary);
    f << schema_json();
    std::printf("wrote data/settings_schema.json\n");
    return 0;
  }
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
  test_schema_file();
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed;
}
