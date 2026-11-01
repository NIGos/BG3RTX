#include "host.h"

#include "../common/settings.h"

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <share.h>

namespace bg3 {

static std::string dir_of_module() {
  HMODULE mod = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(&dir_of_module), &mod);
  char path[MAX_PATH] = {};
  GetModuleFileNameA(mod, path, MAX_PATH);
  std::string s(path);
  return s.substr(0, s.find_last_of("\\/"));
}

const std::string& bin_dir() {
  static const std::string dir = dir_of_module();
  return dir;
}

const std::string& root_dir() {
  static const std::string dir = [] {
    const std::string& b = bin_dir();
    return b.substr(0, b.find_last_of("\\/"));
  }();
  return dir;
}

const std::string& run_tag() {
  static const std::string tag = [] {
    char buf[64] = {};
    DWORD n = GetEnvironmentVariableA("BG3RTX_RUN_TAG", buf, sizeof buf);
    std::string t;
    if (n && n < sizeof buf)
      for (const char* c = buf; *c && t.size() < 24; ++c)
        if (isalnum(static_cast<unsigned char>(*c)) || *c == '_' || *c == '-') t += *c;
    return t;
  }();
  return tag;
}

std::string tagged(const std::string& base, const std::string& ext) {
  return run_tag().empty() ? base + ext : base + "." + run_tag() + ext;
}

std::string capture_stamp() { return run_tag().empty() ? now_stamp() : now_stamp() + "-" + run_tag(); }

std::string now_stamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
  return buf;
}

uint64_t file_mtime(const std::string& path) {
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
  return (uint64_t(fad.ftLastWriteTime.dwHighDateTime) << 32) | fad.ftLastWriteTime.dwLowDateTime;
}

void make_dirs(const std::string& path) {
  for (size_t i = 3; i <= path.size(); ++i)
    if (i == path.size() || path[i] == '\\' || path[i] == '/')
      CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
}

bool write_file(const std::string& path, const void* data, size_t size) {
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
  fwrite(data, 1, size, f);
  fclose(f);
  return true;
}

uint64_t hash64(const void* data, size_t size) {
  uint64_t h = 1469598103934665603ull;
  auto p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

// ---- log ----

static std::mutex g_log_mu;
static FILE* g_log = nullptr;

void log(const char* fmt, ...) {
  char msg[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  SYSTEMTIME st;
  GetLocalTime(&st);
  char line[2100];
  snprintf(line, sizeof line, "%02d:%02d:%02d.%03d  %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

  std::lock_guard<std::mutex> lock(g_log_mu);
  if (!g_log) g_log = _fsopen(tagged(root_dir() + "\\bg3rtx", ".log").c_str(), "w", _SH_DENYNO);  // readable while running
  if (g_log) {
    fputs(line, g_log);
    fflush(g_log);
  }
  OutputDebugStringA(line);
}

// ---- config: layered ini files (key=value lines, '#' comments) ----
// The layers, in order (common/settings.h):
//   1. bg3rtx.ini         developer / shipped defaults
//   2. bg3rtx_user.ini    written by the launcher; %BG3RTX_USER_INI% replaces the
//                         path, BG3RTX_USER_INI=none skips it (test runs)
//   3. %BG3RTX_INI%       per-run overrides (scripts/auto-run.ps1)
// A layer with preset=<id> expands that preset first; its own keys override it.
// Every layer is re-read when its mtime changes.

static std::mutex g_cfg_mu;
static std::mutex g_cfg_poll_mu;
static std::unordered_map<std::string, std::string> g_cfg;

struct CfgFile {
  std::string path;
  uint64_t mtime = ~0ull;   // matches no real mtime: read on the first poll
  settings::Map keys;
};
static CfgFile g_cfg_files[3];

static std::string env_str(const char* name) {
  char buf[MAX_PATH] = {};
  DWORD n = GetEnvironmentVariableA(name, buf, MAX_PATH);
  return n && n < MAX_PATH ? std::string(buf) : std::string();
}

static std::string read_text(const std::string& path) {
  std::string text;
  FILE* f = _fsopen(path.c_str(), "rb", _SH_DENYNO);
  if (!f) return text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
  fclose(f);
  return text;
}

static std::string base_name(const std::string& path) {
  size_t s = path.find_last_of("\\/");
  return s == std::string::npos ? path : path.substr(s + 1);
}

static void cfg_log_effective(const settings::Map& raw, bool derived) {
  std::string files;
  for (CfgFile& f : g_cfg_files) {
    if (f.path.empty() || !f.mtime || f.mtime == ~0ull) continue;
    auto p = f.keys.find("preset");
    files += (files.empty() ? "" : " + ") + base_name(f.path) + " (" + std::to_string(f.keys.size()) + " keys" +
             (p != f.keys.end() ? ", preset=" + p->second : std::string()) + ")";
  }
  log("[cfg] settings read: %s -> %zu keys", files.c_str(), raw.size());

  // The user-facing settings as they took effect (docs/USER_SETTINGS.md).
  std::vector<int> values = settings::knob_values(raw);
  int preset = settings::matching_preset(values);
  std::string knobs;
  for (int i = 0; i < settings::kKnobCount; ++i) {
    const settings::Knob& k = settings::kKnobs[i];
    std::string v = values[i] < 0                        ? std::string("custom")
                    : k.kind == settings::Kind::Choice ? std::string(k.choices[values[i]].id)
                                                         : std::to_string(values[i]);
    knobs += std::string(i ? " " : "") + k.id + "=" + v;
  }
  log("[cfg] knobs: %s (%s)", knobs.c_str(),
      preset >= 0 ? (std::string("preset ") + settings::kPresetId[preset]).c_str() : "custom");
  std::string keys;
  std::lock_guard<std::mutex> lock(g_cfg_mu);
  for (const settings::Knob& k : settings::kKnobs)
    for (const std::string& key : settings::knob_keys(k)) {
      auto it = g_cfg.find(key);
      keys += " " + key + "=" + (it == g_cfg.end() ? std::string("(default)") : it->second);
    }
  auto rb = g_cfg.find("rt_rebuild_frames");
  if (rb != g_cfg.end()) keys += " rt_rebuild_frames=" + rb->second;
  log("[cfg] keys:%s%s", keys.c_str(), derived ? " (rt_build from auto)" : "");
}

// A/B hotkey (ab_hotkey=1, the default): Ctrl+Shift+F9 switches every ray-traced effect
// off and on while playing, so the game's own image can be compared with ours live. It only
// overrides the settings the QA's all-off set uses (rt, gi, rt_reflections, rt_shadows,
// rt_local_shadows, rt_emissive, sharc); everything else (DLSS, frame generation) is untouched.
static std::atomic<bool> g_ab_off{false};
static const char* const kAbOffKeys[] = {"rt", "gi", "rt_reflections", "rt_shadows",
                                         "rt_local_shadows", "rt_emissive", "sharc"};

static bool ab_overridden(const char* key) {
  if (!g_ab_off.load(std::memory_order_relaxed)) return false;
  for (const char* k : kAbOffKeys)
    if (std::strcmp(k, key) == 0) return true;
  return false;
}

static void ab_poll_hotkey() {
  static bool was_down = false;
  bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
              (GetAsyncKeyState(VK_F9) & 0x8000);
  if (down && !was_down && cfg_int("ab_hotkey", 1)) {
    bool off = !g_ab_off.load();
    g_ab_off.store(off);
    log("[ab] Ctrl+Shift+F9: ray tracing %s (the game's own image %s)", off ? "OFF" : "ON",
        off ? "shown" : "replaced again");
  }
  was_down = down;
}

static void cfg_read_files();
static void cfg_hot_refresh();

void cfg_poll() {
  ab_poll_hotkey();
  cfg_read_files();
  cfg_hot_refresh();
}

static void cfg_read_files() {
  std::lock_guard<std::mutex> poll_lock(g_cfg_poll_mu);
  std::string user = env_str("BG3RTX_USER_INI");
  if (user.empty()) user = root_dir() + "\\bg3rtx_user.ini";
  else if (settings::lower(user) == "none" || user == "0") user.clear();
  const std::string paths[3] = {root_dir() + "\\bg3rtx.ini", user, env_str("BG3RTX_INI")};

  bool changed = false;
  for (int i = 0; i < 3; ++i) {
    CfgFile& f = g_cfg_files[i];
    uint64_t mt = paths[i].empty() ? 0 : file_mtime(paths[i]);
    if (paths[i] == f.path && mt == f.mtime) continue;
    settings::Map keys = mt ? settings::parse_ini(read_text(paths[i])) : settings::Map();
    if (keys.empty() && mt) {
      // Caught mid-write (an editor or script truncates before writing): keep
      // this layer's previous keys and read it again on the next poll.
      f.mtime = ~0ull;
      continue;
    }
    f.path = paths[i];
    f.mtime = mt;
    f.keys.swap(keys);
    changed = true;
  }
  if (!changed) return;

  // Layer 0: the defaults of common/settings.h, so a key no file sets has its table value
  // (and rt_build=auto resolves) exactly as if bg3rtx.ini had it.
  settings::Map raw = settings::code_defaults();
  std::vector<std::string> notes;
  for (CfgFile& f : g_cfg_files) {
    std::string n = settings::apply_layer(raw, f.keys);
    if (!n.empty()) notes.push_back(base_name(f.path) + ": " + n);
  }
  settings::Map next = raw;
  bool derived = settings::resolve_derived(next);
  {
    std::lock_guard<std::mutex> lock(g_cfg_mu);
    g_cfg.swap(next);
  }
  for (const std::string& n : notes) log("[cfg] WARNING %s", n.c_str());
  cfg_log_effective(raw, derived);
}

// The defaults of common/settings.h (settings::code_default) by key, parsed once.
struct CfgDefault {
  const char* text;
  int i;          // std::atoi(text): what cfg_int returns
  float f;        // float(std::atof(text)): what cfg_float returns
  bool numeric;   // text is a number (not "auto")
};

static const std::unordered_map<std::string, CfgDefault>& cfg_defaults() {
  static const std::unordered_map<std::string, CfgDefault> m = [] {
    std::unordered_map<std::string, CfgDefault> r;
    for (auto& kv : settings::code_defaults()) {
      const char* t = settings::code_default(kv.first);   // static storage, unlike kv.second
      char* end = nullptr;
      std::strtod(t, &end);
      r[kv.first] = {t, std::atoi(t), float(std::atof(t)), end != t && *end == 0};
    }
    return r;
  }();
  return m;
}

// A call site's fallback literal that disagrees with settings.h is stale: it is never used
// for a key the table has, so it only misleads the reader. Logged once per key and literal.
static void cfg_check_literal(const char* key, const CfgDefault& d, const std::string& lit) {
  static std::mutex mu;
  static std::unordered_map<std::string, std::vector<std::string>> logged;
  {
    std::lock_guard<std::mutex> lock(mu);
    auto& seen = logged[key];
    if (std::find(seen.begin(), seen.end(), lit) != seen.end()) return;
    seen.push_back(lit);
  }
  log("[cfg] code default for %s (%s) differs from settings.h (%s)", key, lit.c_str(), d.text);
}

// The merged value of `key`, else its settings.h default (before the first cfg_poll, or a
// key no layer has), else false (a key in no table: the caller's literal applies).
static bool cfg_raw(const char* key, std::string& out, const CfgDefault** def) {
  if (ab_overridden(key)) {   // the A/B hotkey: every ray-traced effect off
    *def = nullptr;
    out = "0";
    return true;
  }
  const auto& defs = cfg_defaults();
  auto d = defs.find(key);
  *def = d == defs.end() ? nullptr : &d->second;
  {
    std::lock_guard<std::mutex> lock(g_cfg_mu);
    auto it = g_cfg.find(d == defs.end() ? std::string(key) : d->first);
    if (it != g_cfg.end()) {
      out = it->second;
      return true;
    }
  }
  if (!*def) return false;
  out = (*def)->text;
  return true;
}

int cfg_int(const char* key, int fallback) {
  std::string v;
  const CfgDefault* d = nullptr;
  bool have = cfg_raw(key, v, &d);
  if (d && d->numeric && d->i != fallback) cfg_check_literal(key, *d, std::to_string(fallback));
  return have ? std::atoi(v.c_str()) : fallback;
}

float cfg_float(const char* key, float fallback) {
  std::string v;
  const CfgDefault* d = nullptr;
  bool have = cfg_raw(key, v, &d);
  if (d && d->numeric && d->f != fallback) {
    char lit[32];
    snprintf(lit, sizeof lit, "%g", double(fallback));
    cfg_check_literal(key, *d, lit);
  }
  return have ? float(std::atof(v.c_str())) : fallback;
}

// The hot keys of host.h's CfgHot, in its order, with the fallback literal of their call sites.
struct CfgHotKey { const char* key; int fallback; };
static const CfgHotKey kCfgHot[] = {
  {"geo_indirect_mb", 8}, {"rt_strips", 1}, {"rt_alpha_capture", 1}, {"rt_alpha_spill", 0},
  {"rt_native_tileset", 0}, {"rt_stats", 0}, {"rt_em_overlap_check", 0}, {"rt_cascade_casters", 0},
  {"geo_capture", 0}, {"pan_capture", 0}, {"fg_input_copy", 0},
  {"pan_inputs", 0}, {"rt_event_flush", 1}, {"sl_log_barriers", 0}, {"rt_build", 1},
  {"rt_bvh_split", 1}, {"rt_omm", 0}, {"rt_hit_material", 0}, {"rt_native_hair", 0},
  {"rt_native_scalp", 0}, {"rt_mesh_audit_frames", 600}, {"rt_mesh_audits", 2}, {"rt_emissive", 0},
  {"rt_em_backdrop", 0}, {"rt_em_decal", 1}, {"rt_sun_cascade_current", 0}, {"rt_cascade_index", 1},
  {"rt_alpha_cut", 1}, {"rt_alpha_stochastic", 1}, {"rt_alpha_unknown", 1}, {"rt_alpha_test", 1},
  {"rt_water_geometry", 0}, {"rt_water_compose", 0}, {"rt_hit_material_instanced", 0},
  {"rt_water_shadows", 0}, {"rt_water_ls_shadows", 0}, {"rt_water_refraction", 0},
};
static_assert(sizeof kCfgHot / sizeof kCfgHot[0] == size_t(CfgHot::Count), "kCfgHot must list every CfgHot");
static std::atomic<int> g_cfg_hot[size_t(CfgHot::Count)];
static std::atomic<bool> g_cfg_hot_ready{false};

static void cfg_hot_refresh() {
  for (size_t i = 0; i < size_t(CfgHot::Count); ++i)
    g_cfg_hot[i].store(cfg_int(kCfgHot[i].key, kCfgHot[i].fallback), std::memory_order_relaxed);
  g_cfg_hot_ready.store(true, std::memory_order_release);
}

int cfg_hot(CfgHot key) {
  const size_t i = size_t(key);
  if (!g_cfg_hot_ready.load(std::memory_order_acquire)) return cfg_int(kCfgHot[i].key, kCfgHot[i].fallback);
  return g_cfg_hot[i].load(std::memory_order_relaxed);
}

// ---- memory that outlives core reloads ----

static std::mutex g_persist_mu;
static std::unordered_map<std::string, std::unique_ptr<uint8_t[]>> g_persist;
static std::unordered_map<std::string, size_t> g_persist_size;

void* persist(const char* key, size_t size) {
  std::lock_guard<std::mutex> lock(g_persist_mu);
  auto it = g_persist.find(key);
  if (it != g_persist.end()) {
    if (g_persist_size[key] >= size) return it->second.get();
    log("[persist] '%s' grew from %zu to %zu bytes; contents reset", key, g_persist_size[key], size);
  }
  auto mem = std::make_unique<uint8_t[]>(size);
  memset(mem.get(), 0, size);
  void* p = mem.get();
  g_persist[key] = std::move(mem);
  g_persist_size[key] = size;
  return p;
}

}  // namespace bg3
