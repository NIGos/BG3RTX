// First-run preparation (preparation.h, docs/RTX_HAIR_ROADMAP.md 2b).
#include "preparation.h"
#include "launcher.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <shlobj.h>

namespace launcher::prep {

namespace {
std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  return s.substr(a);
}
std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o;
}
std::string q(const std::string& s) { return "\"" + esc(s) + "\""; }
const char* kKeys[] = {"hair_convert_all", "hair_while_playing", "prep_paused", "hair_granny"};
}  // namespace

Settings parse_settings(const std::string& ini) {
  Settings s;
  size_t pos = 0;
  while (pos < ini.size()) {
    size_t nl = ini.find('\n', pos);
    std::string line = trim(ini.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = nl == std::string::npos ? ini.size() : nl + 1;
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
    const bool on = v == "1";
    if (k == kKeys[0]) s.convert_all = on;
    else if (k == kKeys[1]) s.while_playing = on;
    else if (k == kKeys[2]) s.paused = on;
    else if (k == kKeys[3]) s.granny = v;
  }
  return s;
}

std::string merge_settings(const std::string& ini, const Settings& s) {
  std::string out;
  size_t pos = 0;
  while (pos < ini.size()) {
    size_t nl = ini.find('\n', pos);
    std::string raw = ini.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? ini.size() : nl + 1;
    const std::string line = trim(raw);
    const size_t eq = line.find('=');
    bool ours = false;
    if (eq != std::string::npos)
      for (const char* k : kKeys) ours |= trim(line.substr(0, eq)) == k;
    if (!ours && !(line.rfind("# Preparation", 0) == 0)) out += trim(raw) + "\n";
  }
  while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
  out += "# Preparation (launcher only): convert all hair styles, convert while playing, paused, the player's granny2.dll.\n";
  out += std::string(kKeys[0]) + "=" + (s.convert_all ? "1" : "0") + "\n";
  out += std::string(kKeys[1]) + "=" + (s.while_playing ? "1" : "0") + "\n";
  out += std::string(kKeys[2]) + "=" + (s.paused ? "1" : "0") + "\n";
  if (!s.granny.empty()) out += std::string(kKeys[3]) + "=" + s.granny + "\n";
  return out;
}

bool json_number(const std::string& json, const char* key, double& out) {
  const std::string k = std::string("\"") + key + "\"";
  size_t at = json.find(k);
  if (at == std::string::npos) return false;
  at += k.size();
  while (at < json.size() && (json[at] == ' ' || json[at] == ':')) ++at;
  if (at >= json.size() || !(json[at] == '-' || (json[at] >= '0' && json[at] <= '9'))) return false;
  char* end = nullptr;
  out = std::strtod(json.c_str() + at, &end);
  return end != json.c_str() + at;
}

std::string json_string(const std::string& json, const char* key) {
  const std::string k = std::string("\"") + key + "\"";
  size_t at = json.find(k);
  if (at == std::string::npos) return {};
  at += k.size();
  while (at < json.size() && (json[at] == ' ' || json[at] == ':')) ++at;
  if (at >= json.size() || json[at] != '"') return {};
  std::string out;
  for (++at; at < json.size() && json[at] != '"'; ++at) {
    if (json[at] == '\\' && at + 1 < json.size()) ++at;
    out += json[at];
  }
  return out;
}

Item hair_item(const HairInput& in, const Settings& s) {
  Item it{"hair", "RTX Hair"};
  if (!in.missing.empty()) {
    it.state = "unavailable";
    it.line = "Not available: " + in.missing + ".";
    it.detail = "Hair keeps the game's look until the converter is installed.";
    return it;
  }
  double ready = 0, needed = 0, finished = 0, bytes = 0;
  const bool have = json_number(in.status_json, "ready", ready) && json_number(in.status_json, "needed", needed);
  json_number(in.status_json, "finished", finished);
  json_number(in.status_json, "bytes", bytes);
  if (have) {
    char size[32];
    if (bytes >= 1e8) std::snprintf(size, sizeof size, "%.1f GB", bytes / 1e9);
    else std::snprintf(size, sizeof size, "%.0f MB", bytes / 1e6);
    it.line = std::to_string(int(ready)) + " of " + std::to_string(int(needed)) + " styles ready, " + size + " on disk.";
    if (needed > 0) it.progress = std::min(1.0, finished / needed);
  } else {
    it.line = "Not converted yet.";
    it.progress = 0;
  }
  if (!have && in.no_captures) {
    it.state = "waiting";
    it.line = "No hair styles captured yet.";
    it.detail = "The game captures each style the first time it shows it; conversion follows.";
    it.progress = -1;
    return it;
  }
  const bool left = !have || finished < needed;
  const std::string detail = json_string(in.status_json, "detail");
  // bg3rtx_hairconv --all without a usable granny2.dll (or game files): state "unavailable" with its reason. Shown as
  // a stopped run so the panel keeps its controls (choose granny2.dll, Styles seen in game, Try again).
  const bool all_unavailable = !in.running && json_string(in.status_json, "state") == "unavailable";
  if (in.running) {
    it.state = "running";
    it.detail = detail.empty() || detail.rfind("Converting", 0) != 0 ? "Converting. The first estimate appears within a minute." : detail;
    if (in.game_running) it.detail += " Using one core while you play.";
  } else if (s.paused && left) {
    it.state = "paused";
    it.detail = "Paused. Resume to continue; hair without a groom keeps the game's look.";
  } else if (in.failed) {
    it.state = "failed";
    it.detail = "Conversion stopped" + (in.error.empty() ? std::string(".") : ": " + in.error);
  } else if (all_unavailable) {
    const std::string why = json_string(in.status_json, "error");
    it.state = "failed";
    it.detail = "Convert all styles is not available" + (why.empty() ? std::string(".") : ": " + why);
  } else if (!left) {
    it.state = "done";
    it.detail = "All needed styles are done.";
  } else if (in.game_running && !s.while_playing) {
    it.state = "waiting";
    it.detail = "Waits until the game is closed.";
  } else {
    it.state = "waiting";
    it.detail = detail.empty() ? "Starts in a moment." : detail;
  }
  return it;
}

Item shader_item(const ShaderInput& in) {
  Item it{"shaders", "Shaders"};
  const std::string driver = "Your graphics driver still builds the first ray tracing pipeline in game, about a minute.";
  if (!in.available) {
    it.state = "unavailable";
    it.line = "Not available: the mod files are missing.";
    return it;
  }
  double done = 0, total = 0, compiled = 0, failed = 0;
  const bool have = json_number(in.status_json, "done", done) && json_number(in.status_json, "total", total);
  json_number(in.status_json, "compiled", compiled);
  json_number(in.status_json, "failed", failed);
  if (in.running) {
    it.state = "running";
    it.line = have ? "Preparing shaders: " + std::to_string(int(done)) + " of " + std::to_string(int(total)) + "."
                   : "Preparing shaders.";
    it.progress = have && total > 0 ? done / total : 0;
    it.detail = driver;
  } else if (have && json_string(in.status_json, "state") == "done") {
    it.state = "done";
    it.line = failed > 0 ? "Ready. " + std::to_string(int(failed)) + " could not be compiled; the game retries them."
                         : compiled > 0 ? "Ready. " + std::to_string(int(compiled)) + " compiled now." : "Ready.";
    it.detail = driver;
  } else if (in.game_running) {
    it.state = "waiting";
    it.line = "The game compiles what it needs while you play.";
    it.detail = driver;
  } else {
    it.state = "waiting";
    it.line = in.started ? "Stopped before the end; Play compiles the rest." : "Starts in a moment.";
    it.detail = driver;
  }
  return it;
}

HairFacts hair_facts(const HairInput& in, const Settings& s) {
  HairFacts f;
  f.captured = in.captured;
  const std::string& full = in.status_json;
  // Top-level fields only: the "tiers" objects have their own "total" / "finished".
  std::string j = full;
  if (const size_t a = j.find("\"tiers\""); a != std::string::npos)
    if (const size_t b = j.find(']', a); b != std::string::npos) j.erase(a, b - a + 1);
  double v = 0;
  auto num = [&](const char* key, int& out) { if (json_number(j, key, v) && v >= 0) out = int(v); };
  num("ready", f.ready);
  num("finished", f.finished);
  num("needed", f.needed);
  num("total", f.total);
  if (json_number(j, "bytes", v) && v >= 0) f.bytes = v;
  if (json_number(j, "eta_s", v) && v >= 0) f.eta_s = v;
  if (f.total < 0) num("all", f.total);   // bg3rtx_hairconv --all: every catalogue row
  f.source = json_string(j, "source");
  if (f.source.empty()) f.source = json_string(j, "mode");
  if (f.source == "game") f.source = "all";   // --all reads the game's paks
  if (f.source != "all" && f.source != "seen") f.source = s.convert_all ? "all" : "seen";
  if (in.failed) f.error = in.error.empty() ? "The converter stopped." : in.error;
  else f.error = json_string(j, "error");
  // "tiers": [{"tier": "companion", "total": 6, "finished": 6}, ...] (flat objects, no nesting inside).
  const size_t at = full.find("\"tiers\"");
  const size_t open = at == std::string::npos ? at : full.find('[', at);
  const size_t close = open == std::string::npos ? open : full.find(']', open);
  for (size_t p = open; close != std::string::npos && (p = full.find('{', p)) != std::string::npos && p < close;) {
    const size_t e = full.find('}', p);
    if (e == std::string::npos || e > close) break;
    const std::string obj = full.substr(p, e - p + 1);
    HairTier t;
    t.tier = json_string(obj, "tier");
    double n = 0;
    if (json_number(obj, "total", n)) t.total = int(n);
    if (json_number(obj, "finished", n)) t.finished = int(n);
    if (!t.tier.empty() && t.total > 0) f.tiers.push_back(t);
    p = e + 1;
  }
  return f;
}

const char* const kGrannyLine =
    "Convert all styles needs granny2.dll, which BG3RTX cannot include. If you have LSLib's ExportTool "
    "(github.com/Norbyte/lslib, Releases), point to the granny2.dll in its folder.";

std::string hair_exit_error(int code, const std::string& last) {
  std::string line = last;
  const std::string prefix = "bg3rtx_hairconv --all: ";
  if (line.rfind(prefix, 0) == 0) line = line.substr(prefix.size());
  switch (code) {
    case 2: return "The converter was started with wrong arguments (exit 2).";
    case 3: return "granny2.dll could not be used" + (line.empty() ? std::string(".") : ": " + line);
    case 4: return "The game files could not be read: check the game folder" + (line.empty() ? std::string(".") : " (" + line + ").");
    default: return line.empty() ? "The converter stopped (exit " + std::to_string(code) + ")." : line;
  }
}

std::string convert_all_reason(const std::string& granny, bool granny_ok, bool game_ok) {
  if (!game_ok) return "Choose the game folder first: all styles are read from the game files.";
  if (granny.empty()) return "Needs granny2.dll: choose it below.";
  if (!granny_ok) return "granny2.dll was not found at the chosen path. Choose it again.";
  return {};
}

std::wstring hair_command(const std::wstring& converter, const std::wstring& capture_dir, const std::wstring& groom_dir,
                          const std::wstring& report_dir, int threads, bool all, const std::wstring& game,
                          const std::wstring& granny) {
  // Trailing backslashes are doubled: game_dir=D:\Game\ would otherwise escape the closing quote and swallow the
  // next argument (the converter then exits 2).
  auto quote = [](const std::wstring& s) {
    size_t n = 0;
    while (n < s.size() && s[s.size() - 1 - n] == L'\\') ++n;
    return L"\"" + s + std::wstring(n, L'\\') + L"\"";
  };
  std::wstring cmd = quote(converter) + L" ";
  if (all) cmd += L"--all --game " + quote(game) + L" --granny " + quote(granny);
  else cmd += quote(capture_dir);
  return cmd + L" --out " + quote(groom_dir) + L" --report-dir " + quote(report_dir) + L" --threads " +
         std::to_wstring(threads) + L" --jobs 1";
}

bool granny_name_ok(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
  for (auto& c : name) c = wchar_t(towlower(c));
  return name == L"granny2.dll";
}

int jobs_for(bool game_running, unsigned cores) {
  if (game_running) return 1;
  return int(std::max(1u, std::min(4u, cores / 4)));
}

bool hair_should_run(const Settings& s, bool converter_ok, bool game_running, bool done_this_session, bool failed) {
  return converter_ok && !s.paused && !done_this_session && !failed && (!game_running || s.while_playing);
}

std::string to_json(const std::vector<Item>& items, const Settings& s, bool hair_has_work, const Extra& extra) {
  std::string o = "{\"items\":[";
  for (size_t i = 0; i < items.size(); ++i) {
    const Item& it = items[i];
    if (i) o += ",";
    o += "{\"id\":" + q(it.id) + ",\"title\":" + q(it.title) + ",\"line\":" + q(it.line) + ",\"detail\":" + q(it.detail) +
         ",\"state\":" + q(it.state);
    if (it.progress >= 0) {
      char p[32];
      std::snprintf(p, sizeof p, "%.4f", it.progress);
      o += std::string(",\"progress\":") + p;
    }
    o += "}";
  }
  o += std::string("],\"convertAll\":") + (s.convert_all ? "true" : "false") + ",\"whilePlaying\":" +
       (s.while_playing ? "true" : "false") + ",\"paused\":" + (s.paused ? "true" : "false") +
       ",\"pausable\":" + (hair_has_work ? "true" : "false");
  // The RTX Hair panel's numbers (absent fields stay absent) and the "Convert all styles" requirement.
  const HairFacts& f = extra.hair;
  std::string h;
  auto field = [&](const char* key, const std::string& value) { h += std::string(h.empty() ? "" : ",") + "\"" + key + "\":" + value; };
  if (f.ready >= 0) field("ready", std::to_string(f.ready));
  if (f.captured >= 0) field("captured", std::to_string(f.captured));
  if (f.total >= 0) field("total", std::to_string(f.total));
  if (f.finished >= 0) field("finished", std::to_string(f.finished));
  if (f.needed >= 0) field("needed", std::to_string(f.needed));
  if (f.bytes >= 0) field("bytes", std::to_string(int64_t(f.bytes)));
  if (f.eta_s >= 0) field("etaSeconds", std::to_string(int64_t(f.eta_s)));
  if (!f.source.empty()) field("source", q(f.source));
  if (!f.error.empty()) field("error", q(f.error));
  if (!f.tiers.empty()) {
    std::string t;
    for (const HairTier& x : f.tiers)
      t += std::string(t.empty() ? "" : ",") + "{\"tier\":" + q(x.tier) + ",\"total\":" + std::to_string(x.total) +
           ",\"finished\":" + std::to_string(x.finished) + "}";
    field("tiers", "[" + t + "]");
  }
  o += ",\"hair\":{" + h + "}";
  o += ",\"grannyHelp\":" + q(kGrannyLine);
  o += ",\"granny\":{\"path\":" + q(s.granny) + ",\"ok\":" + (extra.granny_ok ? "true" : "false") + "}";
  o += ",\"convertAllReason\":" + q(extra.all_reason) + "}";
  return o;
}

// ---- the live part ----
namespace {
std::wstring local_app_data() {
  PWSTR p = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) out = p;
  CoTaskMemFree(p);
  return out;
}
uint64_t mtime(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
  return (uint64_t(fad.ftLastWriteTime.dwHighDateTime) << 32) | fad.ftLastWriteTime.dwLowDateTime;
}
// The layer's captures (docs/RTX_HAIR_ROADMAP.md 2c): how many .hcap files and the newest one's time (new work).
uint64_t captures(const std::wstring& dir, int* count) {
  WIN32_FIND_DATAW fd{};
  uint64_t newest = 0;
  *count = 0;
  HANDLE f = FindFirstFileW((dir + L"\\*.hcap").c_str(), &fd);
  if (f == INVALID_HANDLE_VALUE) return 0;
  do {
    ++*count;
    newest = std::max(newest, (uint64_t(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime);
  } while (FindNextFileW(f, &fd));
  FindClose(f);
  return newest;
}
std::wstring quote(const std::wstring& s) { return L"\"" + s + L"\""; }

// One child process in its own kill-on-close job object.
struct Child {
  HANDLE job = nullptr, process = nullptr;
  DWORD exit_code = 0;
  bool start(const std::wstring& cmd, const std::wstring& cwd, const std::wstring& log) {
    stop();
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PRIORITY_CLASS;
    li.BasicLimitInformation.PriorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out = log.empty() ? INVALID_HANDLE_VALUE
                             : CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{sizeof si};
    if (out != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = si.hStdError = out;
      si.hStdInput = nullptr;
    }
    PROCESS_INFORMATION pi{};
    std::wstring line = cmd;
    const BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, out != INVALID_HANDLE_VALUE,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                                   cwd.c_str(), &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) { CloseHandle(job); job = nullptr; return false; }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    return true;
  }
  bool running() {
    if (!process) return false;
    if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return true;
    GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    process = nullptr;
    return false;
  }
  bool active() const { return process != nullptr; }
  void stop() {   // the whole tree (the batch's worker processes too)
    if (job) { TerminateJobObject(job, 1); CloseHandle(job); job = nullptr; }
    if (process) { WaitForSingleObject(process, 5000); CloseHandle(process); process = nullptr; }
  }
  ~Child() { stop(); }
};

std::string last_line(const std::wstring& path) {
  std::string text;
  if (!read_file(path, text)) return {};
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  const size_t nl = text.find_last_of('\n');
  std::string l = trim(nl == std::string::npos ? text : text.substr(nl + 1));
  return l.size() > 160 ? l.substr(0, 160) + "..." : l;
}
}  // namespace

struct Manager::Impl {
  std::wstring root, game, ini_path, report_dir, groom_dir, capture_dir, shader_status, converter;
  std::string missing;
  Settings s;
  Child hair, shaders;
  int hair_jobs = 0, capture_count = 0;
  bool hair_done = false, hair_failed = false, shaders_started = false, hair_all = false;
  bool hair_new_captures = false;   // captures changed while a run was active: it listed them at its start, so run again
  uint64_t capture_mtime = 0;
  std::string hair_error;
  bool granny_ok() const { return !s.granny.empty() && granny_name_ok(widen(s.granny)) && file_exists(widen(s.granny)); }
  bool game_ok() const { return !game.empty() && file_exists(game + L"\\bin\\bg3.exe"); }
  bool all_ok() const { return s.convert_all && granny_ok() && game_ok(); }
  void save() {
    std::string ini;
    read_file(ini_path, ini);
    write_file_atomic(ini_path, merge_settings(ini, s), nullptr);
  }
};

Manager::Manager(std::wstring root, std::wstring game_dir) : impl_(std::make_unique<Impl>()) {
  Impl& m = *impl_;
  m.root = std::move(root);
  m.game = std::move(game_dir);
  m.ini_path = m.root + L"\\bg3rtx_launcher.ini";
  const std::wstring local = local_app_data();
  m.report_dir = local + L"\\BG3RTX\\hair_batch";
  m.shader_status = local + L"\\BG3RTX\\shader_cache\\precompile.json";
  m.groom_dir = m.root + L"\\data\\assetdb\\hair_grooms";
  m.capture_dir = m.root + L"\\data\\assetdb\\hair_capture";
  m.converter = exe_dir() + L"\\bg3rtx_hairconv.exe";   // native: no Python, LSLib, granny2 or PowerShell 7
  std::string ini;
  read_file(m.ini_path, ini);
  m.s = parse_settings(ini);
  if (!file_exists(m.converter)) m.missing = "the hair converter is not installed";
  m.capture_mtime = captures(m.capture_dir, &m.capture_count);
}

Manager::~Manager() = default;

bool Manager::act(const std::string& action, int value) {
  Impl& m = *impl_;
  if (action == "pause") m.s.paused = true;
  else if (action == "resume") { m.s.paused = false; m.hair_failed = false; }
  else if (action == "start") { m.s.paused = false; m.hair_failed = false; m.hair_done = false; }   // run again now
  else if (action == "whilePlaying") m.s.while_playing = value != 0;
  else if (action == "convertAll") {
    // "Convert all styles" needs the player's granny2.dll and the game folder; "Styles seen in game" always works.
    if (value != 0 && !convert_all_reason(m.s.granny, m.granny_ok(), m.game_ok()).empty()) return false;
    m.s.convert_all = value != 0;
    m.hair_done = false; m.hair_failed = false;
  }
  else if (action == "clearGranny") { m.s.granny.clear(); m.s.convert_all = false; m.hair_done = false; }   // the source changed
  else return false;
  m.save();
  return true;
}

bool Manager::set_granny(const std::wstring& path) {
  Impl& m = *impl_;
  if (path.empty() || !granny_name_ok(path) || !file_exists(path)) return false;
  m.s.granny = narrow(path);
  m.hair_failed = false;
  m.hair_done = false;   // a saved "Convert all styles" becomes possible: a finished captures run must not block it
  m.save();
  return true;
}

std::wstring Manager::granny() const { return widen(impl_->s.granny); }

void Manager::tick(bool game_running) {
  Impl& m = *impl_;
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  // New captures (the game captured a style it drew without a groom) are work again.
  int count = 0;
  const uint64_t newest = captures(m.capture_dir, &count);
  if (newest != m.capture_mtime || count != m.capture_count) {
    m.capture_mtime = newest; m.capture_count = count; m.hair_done = false;
    if (m.hair.active()) m.hair_new_captures = true;
  }
  // Hair: bg3rtx_hairconv over the captures, oldest first (the order the game saw them), resumable by its state.
  if (m.hair.active() && !m.hair.running()) {
    if (m.hair.exit_code == 0) m.hair_done = !m.hair_new_captures;
    else { m.hair_failed = true; m.hair_error = hair_exit_error(int(m.hair.exit_code), last_line(m.report_dir + L"\\batch.log")); }
  }
  // Source: the styles the game captured, or (Convert all styles, with the player's granny2.dll) every style in the paks.
  const bool all = m.all_ok();
  const bool want = (all || m.capture_count > 0) && hair_should_run(m.s, m.missing.empty(), game_running, m.hair_done, m.hair_failed);
  const int threads = jobs_for(game_running, si.dwNumberOfProcessors);   // threads inside a style; one style at a time
  if (m.hair.active() && (!want || threads != m.hair_jobs || all != m.hair_all)) m.hair.stop();   // resumable
  if (!m.hair.active() && want) {
    CreateDirectoryW((m.report_dir.substr(0, m.report_dir.find_last_of(L'\\'))).c_str(), nullptr);
    CreateDirectoryW(m.report_dir.c_str(), nullptr);
    const std::wstring cmd = hair_command(m.converter, m.capture_dir, m.groom_dir, m.report_dir, threads, all, m.game, widen(m.s.granny));
    m.hair_new_captures = false;
    if (m.hair.start(cmd, m.root, m.report_dir + L"\\batch.log")) { m.hair_jobs = threads; m.hair_all = all; }
    else { m.hair_failed = true; m.hair_error = "the converter could not be started"; }
  }
  // Shaders: once per launcher session, before Play (never beside a running game: it compiles its own).
  if (m.shaders.active()) m.shaders.running();
  const bool layer = file_exists(exe_dir() + L"\\VkLayer_bg3rtx.dll") && file_exists(m.root + L"\\shaders\\precompile.txt");
  if (game_running && m.shaders.active()) m.shaders.stop();
  if (!m.shaders_started && layer && !game_running) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    m.shaders_started = m.shaders.start(quote(self) + L" --precompile-shaders " + quote(m.shader_status), m.root, L"");
  }
  // The card.
  std::string status;
  read_file(m.report_dir + L"\\preparation.json", status);
  HairInput hi{m.missing, status, m.hair.active(), game_running, m.hair_failed, m.hair_error, m.capture_count == 0 && !all,
               m.capture_count};
  Item h = hair_item(hi, m.s);
  std::string sstatus;
  read_file(m.shader_status, sstatus);
  ShaderInput sh{layer, sstatus, m.shaders.active(), game_running, m.shaders_started};
  Extra extra{hair_facts(hi, m.s), m.granny_ok(), convert_all_reason(m.s.granny, m.granny_ok(), m.game_ok())};
  json_ = to_json({h, shader_item(sh)}, m.s, h.state != "done" && h.state != "unavailable", extra);
}

// ---- BG3RTX.exe --precompile-shaders <status json> ----
namespace {
struct PrecompileStatus { std::wstring path; };
void write_status(const std::wstring& path, int done, int total, int compiled, int failed, const char* state) {
  char buf[256];
  std::snprintf(buf, sizeof buf, "{\"done\": %d, \"total\": %d, \"compiled\": %d, \"failed\": %d, \"state\": \"%s\"}\n", done,
                total, compiled, failed, state);
  write_file_atomic(path, buf, nullptr);
}
void on_progress(void* user, int done, int total, int compiled, int failed) {
  write_status(static_cast<PrecompileStatus*>(user)->path, done, total, compiled, failed, "running");
}
}  // namespace

int precompile_main(const std::wstring& status_path, const std::wstring& list_override) {
  // The layer's log goes to bg3rtx.precompile.log: the game's bg3rtx.log (and its previous session) stays as it was.
  SetEnvironmentVariableW(L"BG3RTX_RUN_TAG", L"precompile");
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  PrecompileStatus st{status_path};
  CreateDirectoryW(status_path.substr(0, status_path.find_last_of(L'\\')).c_str(), nullptr);
  HMODULE layer = LoadLibraryExW((exe_dir() + L"\\VkLayer_bg3rtx.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  using Fn = int (*)(const char*, int, void (*)(void*, int, int, int, int), void*, volatile long*);
  Fn fn = layer ? reinterpret_cast<Fn>(GetProcAddress(layer, "bg3rtx_precompile")) : nullptr;
  if (!fn) {
    write_status(status_path, 0, 0, 0, 0, "unavailable");
    return 3;
  }
  const std::wstring local = status_path.substr(0, status_path.find_last_of(L'\\'));
  const std::string lists = !list_override.empty()
                                ? narrow(list_override)
                                : narrow(root_dir() + L"\\shaders\\precompile.txt") + ";" + narrow(local + L"\\requests.txt");
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  const int threads = int(std::max<DWORD>(1, si.dwNumberOfProcessors > 2 ? si.dwNumberOfProcessors - 2 : 1));
  write_status(status_path, 0, 0, 0, 0, "running");
  volatile long cancel = 0;
  int last_done = 0, last_total = 0, last_compiled = 0, last_failed = 0;
  struct Track { PrecompileStatus* st; int *d, *t, *c, *f; } track{&st, &last_done, &last_total, &last_compiled, &last_failed};
  auto cb = [](void* user, int done, int total, int compiled, int failed) {
    auto* tr = static_cast<Track*>(user);
    *tr->d = done; *tr->t = total; *tr->c = compiled; *tr->f = failed;
    on_progress(tr->st, done, total, compiled, failed);
  };
  const int failed = fn(lists.c_str(), threads, cb, &track, &cancel);
  if (failed < 0) {
    write_status(status_path, 0, 0, 0, 0, "unavailable");
    return 3;
  }
  write_status(status_path, last_done, last_total, last_compiled, failed, "done");
  return 0;
}

}  // namespace launcher::prep
