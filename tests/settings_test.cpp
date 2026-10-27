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
  test_user_ini_writer();
  test_dependencies_and_caps();
  test_schema_file();
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed;
}
