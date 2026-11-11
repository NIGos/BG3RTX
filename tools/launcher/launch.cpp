// Paths, game detection, the launch environment and the settings state.
#include "launcher.h"
#include "version.h"

#include <tlhelp32.h>

#include <algorithm>
#include <map>
#include <regex>

namespace launcher {

namespace st = bg3::settings;

// ---- text helpers ----

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

bool read_file(const std::wstring& path, std::string& out) {
  out.clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  char buf[8192];
  DWORD n = 0;
  BOOL ok;
  while ((ok = ReadFile(h, buf, sizeof buf, &n, nullptr)) && n) out.append(buf, n);
  DWORD error = ok ? ERROR_SUCCESS : GetLastError();
  CloseHandle(h);
  if (!ok) { out.clear(); SetLastError(error); }
  return ok != FALSE;
}

bool write_file_atomic(const std::wstring& path, const std::string& text, std::wstring* err) {
  std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (err) *err = L"cannot create " + tmp;
    return false;
  }
  DWORD n = 0;
  BOOL ok = WriteFile(h, text.data(), DWORD(text.size()), &n, nullptr) && n == text.size();
  if (!FlushFileBuffers(h)) ok = FALSE;
  if (!CloseHandle(h)) ok = FALSE;
  if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    if (err) *err = L"cannot write " + path;
    return false;
  }
  return true;
}

bool file_exists(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool dir_exists(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring parent(const std::wstring& p) {
  size_t s = p.find_last_of(L"\\/");
  return s == std::wstring::npos ? std::wstring() : p.substr(0, s);
}

// ---- paths ----

std::wstring exe_dir() {
  wchar_t buf[MAX_PATH * 4] = {};
  GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
  return parent(buf);
}

std::wstring root_dir() { return parent(exe_dir()); }
std::wstring user_ini_path() { return root_dir() + L"\\bg3rtx_user.ini"; }
std::wstring base_ini_path() { return root_dir() + L"\\bg3rtx.ini"; }
const std::string& product_version() {
  static const std::string version = [] { std::string text; read_file(root_dir() + L"\\VERSION.txt", text); return display_version(text); }();
  return version;
}
static std::wstring launcher_ini_path() { return root_dir() + L"\\bg3rtx_launcher.ini"; }

bool valid_game_dir(const std::wstring& dir) { return !dir.empty() && file_exists(dir + L"\\bin\\bg3.exe"); }

std::wstring game_dir_from_path(const std::wstring& path) {
  std::wstring d = path;
  for (int i = 0; i < 6 && !d.empty(); ++i) {
    if (valid_game_dir(d)) return d;
    d = parent(d);
  }
  return {};
}

static std::wstring reg_string(HKEY root, const wchar_t* key, const wchar_t* value) {
  wchar_t buf[1024] = {};
  DWORD size = sizeof buf;
  if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
  return buf;
}

std::wstring detect_game_dir() {
  // The Steam uninstall entry has the exact folder.
  std::wstring loc = reg_string(HKEY_LOCAL_MACHINE,
                                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 1086940",
                                L"InstallLocation");
  if (valid_game_dir(loc)) return loc;
  // Otherwise every Steam library folder.
  std::wstring steam = reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
  std::vector<std::wstring> libs;
  if (!steam.empty()) {
    std::replace(steam.begin(), steam.end(), L'/', L'\\');
    libs.push_back(steam);
    std::string vdf;
    if (read_file(steam + L"\\steamapps\\libraryfolders.vdf", vdf)) {
      size_t pos = 0;
      while ((pos = vdf.find("\"path\"", pos)) != std::string::npos) {
        size_t a = vdf.find('"', pos + 6), b = a == std::string::npos ? a : vdf.find('"', a + 1);   // paths hold no quotes
        if (a == std::string::npos || b == std::string::npos) break;
        std::string p = vdf.substr(a + 1, b - a - 1), u;
        for (size_t i = 0; i < p.size(); ++i) {
          if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') ++i;
          u += p[i];
        }
        libs.push_back(widen(u));
        pos = b + 1;
      }
    }
  }
  for (const std::wstring& lib : libs) {
    std::wstring d = lib + L"\\steamapps\\common\\Baldurs Gate 3";
    if (valid_game_dir(d)) return d;
  }
  return {};
}

std::wstring saved_game_dir() {
  std::string text;
  if (!read_file(launcher_ini_path(), text)) return {};
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    std::string line = st::trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = nl == std::string::npos ? text.size() : nl + 1;
    if (line.rfind("game_dir=", 0) == 0) return widen(st::trim(line.substr(9)));
  }
  return {};
}

bool save_game_dir(const std::wstring& dir, std::wstring* err) {
  // Other launcher-only keys (the Preparation card's, tools/launcher/preparation.cpp) are kept.
  std::string old, rest;
  read_file(launcher_ini_path(), old);
  size_t pos = 0;
  while (pos < old.size()) {
    size_t nl = old.find('\n', pos);
    std::string line = old.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? old.size() : nl + 1;
    const std::string t = st::trim(line);
    if (t.empty() || t.rfind("game_dir=", 0) == 0 || t.rfind("# BG3RTX.exe: the game folder", 0) == 0) continue;
    rest += t + "\n";
  }
  return write_file_atomic(launcher_ini_path(),
                    "# BG3RTX.exe: the game folder (the one with bin\\bg3.exe).\ngame_dir=" + narrow(dir) + "\n" + rest,
                    err);
}

bool game_running() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe{sizeof pe};
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
    found = _wcsicmp(pe.szExeFile, L"bg3.exe") == 0 || _wcsicmp(pe.szExeFile, L"bg3_dx11.exe") == 0;
  CloseHandle(snap);
  return found;
}

// ---- launching ----

LaunchPlan make_plan(const std::wstring& game_dir, const std::wstring& exe_override,
                     const std::vector<std::wstring>& args, bool clean, int fg) {
  LaunchPlan p;
  bool game = exe_override.empty();
  p.exe = game ? game_dir + L"\\bin\\bg3.exe" : exe_override;
  p.cwd = parent(p.exe);
  p.args = args;
  // The layer is found through VK_ADD_LAYER_PATH (bin\VkLayer_bg3rtx.json) and
  // enabled for this process only: no registry or game-folder changes.
  p.env_set = {{L"VK_ADD_LAYER_PATH", exe_dir()},
               {L"VK_INSTANCE_LAYERS", L"VK_LAYER_BG3RTX_host"},
               {L"VK_LOADER_LAYERS_ENABLE", L"VK_LAYER_BG3RTX_host"}};
  if (game) {
    // steam_api initialises without going back through the Larian launcher.
    p.env_set.push_back({L"SteamAppId", L"1086940"});
    p.env_set.push_back({L"SteamGameId", L"1086940"});
  }
  // NVIDIA's present layer is Smooth Motion: it must be off for DLSS-G.
  if (fg >= 2 || clean) p.env_set.push_back({L"DISABLE_LAYER_NV_PRESENT_1", L"1"});
  if (clean) {
    p.env_set.push_back({L"DISABLE_VK_LAYER_VALVE_steam_overlay_1", L"1"});
    p.env_set.push_back({L"DISABLE_VK_LAYER_VALVE_steam_fossilize_1", L"1"});
    p.env_set.push_back({L"EOS_OVERLAY_DISABLE_VULKAN_WIN64", L"1"});
    p.env_set.push_back({L"DISABLE_GALAXY_OVERLAY", L"1"});
  }
  // Developer/test variables must not leak into a player's game.
  p.env_unset = {L"BG3RTX_INI", L"BG3RTX_USER_INI", L"BG3RTX_BACKGROUND", L"BG3RTX_SAVE_ONLY",
                 L"VK_LOADER_LAYERS_DISABLE", L"BG3RTX_ASSETDB", L"BG3RTX_SE_DIR", L"BG3RTX_SAVE_LOG"};
  return p;
}

static std::wstring quote(const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
  std::wstring o = L"\"";
  size_t bs = 0;
  for (wchar_t c : a) {
    if (c == L'\\') {
      ++bs;
      continue;
    }
    if (c == L'"') o.append(bs * 2 + 1, L'\\');
    else o.append(bs, L'\\');
    bs = 0;
    o += c;
  }
  o.append(bs * 2, L'\\');
  return o + L"\"";
}

std::wstring command_line(const LaunchPlan& p) {
  std::wstring c = quote(p.exe);
  for (const std::wstring& a : p.args) c += L" " + quote(a);
  return c;
}

std::wstring describe(const LaunchPlan& p) {
  std::wstring d = L"exe: " + p.exe + L"\ncwd: " + p.cwd + L"\ncommand line: " + command_line(p) + L"\n";
  for (auto& kv : p.env_set) d += L"env: " + kv.first + L"=" + kv.second + L"\n";
  for (auto& k : p.env_unset) d += L"env unset: " + k + L"\n";
  return d;
}

bool compose_environment(const LaunchPlan& p, const wchar_t* inherited, std::wstring& out) {
  out.clear();
  if (!inherited) return false;
  // Case-insensitive like Windows; sorted as CreateProcess expects.
  struct Less {
    bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
  };
  std::map<std::wstring, std::wstring, Less> env;
  std::vector<std::wstring> special;   // "=C:=C:\..." entries keep their place first
  for (const wchar_t* e = inherited; *e; e += wcslen(e) + 1) {
    std::wstring s = e;
    size_t eq = s.find(L'=', 1);
    if (s[0] == L'=' || eq == std::wstring::npos) {
      special.push_back(s);
      continue;
    }
    env[s.substr(0, eq)] = s.substr(eq + 1);
  }
  for (auto& k : p.env_unset) env.erase(k);
  for (auto& kv : p.env_set) env[kv.first] = kv.second;
  for (auto& s : special) out += s + L'\0';
  for (auto& kv : env) out += kv.first + L"=" + kv.second + L'\0';
  if (out.empty()) out += L'\0';
  out += L'\0';
  return true;
}

bool launch(const LaunchPlan& p, DWORD* pid, std::wstring* err) {
  if (!file_exists(p.exe)) {
    if (err) *err = L"not found: " + p.exe;
    return false;
  }
  std::wstring cmd = command_line(p);
  wchar_t* inherited = GetEnvironmentStringsW();
  std::wstring env;
  bool ok = compose_environment(p, inherited, env);
  if (inherited) FreeEnvironmentStringsW(inherited);
  if (!ok) {
    if (err) *err = L"Cannot read the launch environment.";
    return false;
  }
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(p.exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, env.data(),
                      p.cwd.c_str(), &si, &pi)) {
    if (err) *err = L"CreateProcess failed (error " + std::to_wstring(GetLastError()) + L")";
    return false;
  }
  if (pid) *pid = pi.dwProcessId;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

std::wstring caps_text(unsigned caps) {
  std::wstring t = (caps & st::kCapRayQuery) ? L"ray tracing yes" : L"ray tracing NO";
  t += (caps & st::kCapDlss) ? L", Ray Reconstruction yes" : L", Ray Reconstruction no";
  t += (caps & st::kCapMfg) ? L", frame generation 2x/3x/4x"
       : (caps & st::kCapFg) ? L", frame generation 2x"
                             : L", frame generation no";
  t += (caps & st::kCapOpacityMicromap) ? L", opacity micromaps yes" : L", opacity micromaps no";
  return t;
}

// ---- settings state ----

void load_state(State& s) {
  auto read_optional = [](const std::wstring& path, std::string& text) {
    if (read_file(path, text)) return true;
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  };
  bool base_ok = read_optional(base_ini_path(), s.base_text);
  bool user_ok = read_optional(user_ini_path(), s.user_text);
  s.settings_readable = base_ok && user_ok;
  s.base = st::parse_ini(s.base_text);
  s.user = st::parse_ini(s.user_text);
  st::Map raw;
  st::apply_layer(raw, s.base);
  st::apply_layer(raw, s.user);
  s.values = st::knob_values(raw);
  s.loaded = s.values;
  s.dlss5_runtime_path = s.loaded_dlss5_runtime_path = st::lookup(raw, "dlss5_nr_runtime");
  for(size_t i=0;i<kShortcutCount;++i){const char* key=bg3::hotkeys::ids[i];auto v=st::lookup(raw,key);const char* fallback=st::code_default(key);s.shortcuts[i]=s.loaded_shortcuts[i]=!v.empty()?v:fallback?fallback:bg3::hotkeys::defaults[i];}
  auto it = s.user.find("preset");
  int p = it == s.user.end() ? -1 : st::preset_from_id(it->second);
  if (p < 0) p = st::matching_preset(s.values);
  s.preset = p < 0 ? st::kHigh : p;
}

st::KVList forced_keys(const State& s) {
  st::KVList f;
  for(size_t i=0;i<kShortcutCount;++i)f.emplace_back(bg3::hotkeys::ids[i],s.shortcuts[i]);
  f.emplace_back("dlss5_nr_runtime", s.dlss5_runtime_path);
  // No ray query: nothing to enable at device creation.
  if (s.have_gpu && !(s.caps & st::kCapRayQuery)) f.emplace_back("rt", "0");
  return f;
}

bool save_state(State& s, std::wstring* err) {
  if (!s.settings_readable) {
    if (err) *err = L"Settings could not be read. Close and retry before saving.";
    return false;
  }
  std::string text = st::render_user_ini(s.user_text, s.preset, s.values, forced_keys(s));
  if (!write_file_atomic(user_ini_path(), text, err)) return false;
  s.user_text = text;
  s.user = st::parse_ini(text);
  s.loaded = s.values;
  s.loaded_shortcuts = s.shortcuts;
  s.loaded_dlss5_runtime_path = s.dlss5_runtime_path;
  return true;
}

int effective_fg(const State& s) {
  st::Map raw;
  st::apply_layer(raw, s.base);
  st::apply_layer(raw, s.user);
  return std::atoi(st::lookup(raw, "fg").c_str());
}

// ---- game upscaler guard ----

// Sets one int32 ConfigEntry of graphicSettings.lsx; `only_if_absent` keeps an existing value.
static bool set_lsx_int(std::string& text, const char* key, int value, bool only_if_absent,
                        std::vector<std::string>* changes) {
  std::regex entry(std::string("value=\"") + key +
                   "\"/>\\s*<attribute id=\"Type\" type=\"int32\" value=\"0\"/>\\s*"
                   "<attribute id=\"Value\" type=\"int32\" value=\"(-?\\d+)\"");
  std::smatch m;
  if (std::regex_search(text, m, entry)) {
    if (only_if_absent || std::atoi(m.str(1).c_str()) == value) return true;
    if (changes) changes->push_back(std::string(key) + " " + m.str(1) + "->" + std::to_string(value));
    text.replace(size_t(m.position(1)), size_t(m.length(1)), std::to_string(value));
    return true;
  }
  size_t children = text.find("<children>");
  if (children == std::string::npos) return false;   // not the file we know: leave it alone
  size_t eol = text.find('\n', children);
  if (eol == std::string::npos) return false;
  const std::string nl = eol > 0 && text[eol - 1] == '\r' ? "\r\n" : "\n";
  std::string node = "                <node id=\"ConfigEntry\">" + nl +
                     "                    <attribute id=\"MapKey\" type=\"FixedString\" value=\"" + key + "\"/>" + nl +
                     "                    <attribute id=\"Type\" type=\"int32\" value=\"0\"/>" + nl +
                     "                    <attribute id=\"Value\" type=\"int32\" value=\"" + std::to_string(value) +
                     "\"/>" + nl + "                </node>" + nl;
  text.insert(eol + 1, node);
  if (changes) changes->push_back(std::string(key) + " added=" + std::to_string(value));
  return true;
}

std::string guard_upscaler_text(const std::string& lsx, std::vector<std::string>* changes) {
  std::string text = lsx;
  // All or nothing: a file only one of the two edits fits is returned unchanged, with no change reported (the caller
  // would otherwise back up and rewrite the same text, and say "set back to DLSS", at every launch).
  if (!set_lsx_int(text, "ResolutionUpscaleType", 3, false, changes) || !set_lsx_int(text, "DLSS2Mode", 2, true, changes)) {
    if (changes) changes->clear();
    return lsx;
  }
  return text;
}

std::wstring guard_game_upscaler(const State& s) {
  st::Map raw;
  st::apply_layer(raw, s.base);
  st::apply_layer(raw, s.user);
  if (std::atoi(st::lookup(raw, "game_dlss_guard").c_str()) != 1) return {};
  if (std::atoi(st::lookup(raw, "rr").c_str()) != 1) return {};
  wchar_t local[MAX_PATH] = {};
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  if (!n || n >= MAX_PATH) return {};
  std::wstring path = std::wstring(local) + L"\\Larian Studios\\Baldur's Gate 3\\graphicSettings.lsx";
  std::string text;
  if (!read_file(path, text)) return {};   // first start: the game writes its own file
  std::vector<std::string> changes;
  std::string fixed = guard_upscaler_text(text, &changes);
  if (changes.empty()) return {};
  std::wstring err;
  if (!write_file_atomic(path + L".bg3rtx-bak", text, &err) || !write_file_atomic(path, fixed, &err))
    return L"game upscaler guard: could not write graphicSettings.lsx: " + err;
  std::string what;
  for (const std::string& c : changes) what += (what.empty() ? "" : ", ") + c;
  return L"game upscaler guard: graphicSettings.lsx set back to DLSS (" + widen(what) + L")";
}

}  // namespace launcher
