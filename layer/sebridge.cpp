// Script Extender bridge reader (docs/SE_BRIDGE.md).
//
// The BG3RTX_Bridge Lua mod (mods/BG3RTX_Bridge) rewrites two small text files with
// Ext.IO.SaveFile: lights.txt at 4 Hz (client context) and world.txt when it changes
// (server context). A reader thread polls their write time every 100 ms; when one
// changed it reads and parses it, and publishes a new immutable snapshot. The render
// thread only ever copies a shared_ptr. A file is accepted only when it ends with the
// END line (SaveFile rewrites the whole file, so a read can catch it half written; the
// next poll then gets the complete one).
//
// Freshness: an export counts only when it was written after this process started and
// is younger than se_stale_ms() (se_stale_periods = 4 measured export periods + the poll,
// about 1.1-1.5 s). Without the extender, the mod, or with
// se_bridge=0, se_snapshot() returns nullptr and everything falls back to the database.
#include "sebridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "host.h"

namespace bg3 {

namespace {

std::mutex g_mu;                                   // guards the snapshot pointers
std::shared_ptr<const SeSnapshot> g_snap;          // latest complete export (lights + world)
std::thread g_thread;
std::mutex g_stop_mu;
std::condition_variable g_stop_cv;
bool g_stop = false;
std::atomic<uint32_t> g_reads{0};
double g_process_start = 0;   // FILETIME seconds

// Export intervals (file write times of consecutive exports), the last kIntervals.
constexpr int kIntervals = 16;   // 4 s of exports: a hitch counts for that long, then drops out
std::mutex g_period_mu;
double g_interval[kIntervals] = {};
int g_n_interval = 0, g_interval_at = 0;

// "[se] motion": the speeds of the bridge's moving lights between consecutive exports (the
// measurement behind kSeMoveSpeedMax), logged every kMotionExports exports.
constexpr uint32_t kMotionExports = 120;   // 30 s at 4 Hz
struct Motion {
  std::unordered_map<uint64_t, std::array<float, 3>> last;   // id -> position in the previous export
  uint32_t exports = 0, samples = 0, over = 0;
  float v_max = 0;
  const char* name = "";
  uint8_t owner = 0;
};

void note_motion(Motion& m, const SeSnapshot& prev, const SeSnapshot& cur) {
  const double dt = cur.time - prev.time;
  const bool consecutive = cur.seq == prev.seq + 1 && dt > 0.05 && dt < 2.0;
  std::unordered_map<uint64_t, std::array<float, 3>> next;
  for (const SeLight& l : cur.lights) {
    if (l.owner == BG3_RTL_OWNER_PLACED) continue;
    next[l.id] = {l.pos[0], l.pos[1], l.pos[2]};
    auto it = m.last.find(l.id);
    if (!consecutive || it == m.last.end()) continue;
    float d2 = 0;
    for (int k = 0; k < 3; ++k) d2 += (l.pos[k] - it->second[k]) * (l.pos[k] - it->second[k]);
    const float v = float(std::sqrt(double(d2)) / dt);
    ++m.samples;
    m.over += v > kSeMoveSpeedMax;
    if (v > m.v_max) m.v_max = v, m.name = l.name, m.owner = l.owner;
  }
  m.last.swap(next);
  if (++m.exports < kMotionExports) return;
  if (m.samples)
    log_stats("[se] motion (%u exports): %u moving-light steps, fastest %.2f m/s (owner %u, %s), %u above %.1f m/s "
        "(a teleport or level change counts too); export interval max %.3f s",
        m.exports, m.samples, m.v_max, unsigned(m.owner), *m.name ? m.name : "-", m.over, kSeMoveSpeedMax,
        se_period_s());
  m.exports = m.samples = m.over = 0;
  m.v_max = 0;
}

double ft_seconds(const FILETIME& f) {
  ULARGE_INTEGER u;
  u.LowPart = f.dwLowDateTime;
  u.HighPart = f.dwHighDateTime;
  return double(u.QuadPart) * 1e-7;
}

double now_seconds() {
  FILETIME f;
  GetSystemTimeAsFileTime(&f);
  return ft_seconds(f);
}

bool file_time(const std::string& path, double& t) {
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a)) return false;
  t = ft_seconds(a.ftLastWriteTime);
  return true;
}

bool read_text(const std::string& path, std::string& out) {
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (16 << 20);
  if (ok) {
    out.resize(size_t(sz.QuadPart));
    DWORD got = 0;
    ok = ReadFile(h, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size();
  }
  CloseHandle(h);
  return ok;
}

bool ends_with_end(const std::string& t) {
  size_t e = t.find_last_not_of("\r\n \t");
  return e != std::string::npos && e >= 2 && t.compare(e - 2, 3, "END") == 0 &&
         (e < 3 || t[e - 3] == '\n');
}

void reader() {
  const std::string dir = se_dir();
  const std::string lights_path = dir + "\\lights.txt", world_path = dir + "\\world.txt";
  double t_lights = -1, t_world = -1;
  SeSnapshot cur;   // the merged state being built
  Motion motion;
  bool logged_first = false, logged_bad = false, logged_complete = false;
  for (;;) {
    {
      std::unique_lock<std::mutex> lk(g_stop_mu);
      if (g_stop_cv.wait_for(lk, std::chrono::milliseconds(100), [] { return g_stop; })) return;
    }
    if (!cfg_int("se_bridge", 1)) continue;
    // Heartbeat: the mod exports only while this file keeps changing, so a game
    // started without the layer pays nothing for the bridge.
    static int beat = 0, beat_n = 0;
    if (++beat_n >= 10) {   // once per second
      beat_n = 0;
      make_dirs(dir);
      std::string b = std::to_string(++beat) + "\n";
      write_file(dir + "\\active.txt", b.data(), b.size());
    }
    bool changed = false;
    double t;
    if (file_time(lights_path, t) && t != t_lights) {
      std::string text;
      SeSnapshot s;
      if (read_text(lights_path, text) && se_parse_lights(text, s)) {
        t_lights = t;
        s.time = t;
        if (cur.time > 0 && s.seq == cur.seq + 1 && t > cur.time) {
          std::lock_guard<std::mutex> lk(g_period_mu);
          g_interval[g_interval_at] = t - cur.time;
          g_interval_at = (g_interval_at + 1) % kIntervals;
          g_n_interval = (std::min)(g_n_interval + 1, kIntervals);
        }
        if (cur.time > 0) note_motion(motion, cur, s);
        s.region = cur.region;
        s.triggers = std::move(cur.triggers);
        if (std::strcmp(s.level, cur.level) != 0) logged_complete = false;
        cur = std::move(s);
        changed = true;
        g_reads.fetch_add(1, std::memory_order_relaxed);
        if (!logged_complete && t >= g_process_start && se_classification_complete(cur)) {
          logged_complete = true;
          log("[se] classification complete: export %u, level %s, %u current light entities, %zu explicit records",
              cur.seq, cur.level, cur.all, cur.lights.size());
        }
        if (!logged_first && t >= g_process_start) {
          logged_first = true;
          log("[se] bridge export %u read: level %s, %zu lights (of %u light entities), Lua %.0f us avg / %.0f us max "
              "per 4 Hz export, %.0f us classification so far",
              cur.seq, cur.level, cur.lights.size(), cur.all, cur.avg_us, cur.max_us, cur.classify_us);
        }
      } else if (!logged_bad && !text.empty() && text.compare(0, 9, "BG3RTX_SE") != 0) {
        logged_bad = true;
        log("[se] %s is not a bridge export (ignored)", lights_path.c_str());
      }
    }
    if (file_time(world_path, t) && t != t_world) {
      std::string text;
      SeSnapshot w;
      if (read_text(world_path, text) && se_parse_world(text, w)) {
        t_world = t;
        cur.region = w.region;
        cur.triggers = std::move(w.triggers);
        changed = true;
      }
    }
    if (changed) {
      auto p = std::make_shared<const SeSnapshot>(cur);
      std::lock_guard<std::mutex> lk(g_mu);
      g_snap = std::move(p);
    }
  }
}

std::mutex g_thread_mu;   // guards g_thread start/stop

void start() {
  std::lock_guard<std::mutex> tl(g_thread_mu);
  if (g_thread.joinable()) return;
  FILETIME c, e, k, u;
  if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) g_process_start = ft_seconds(c);
  {
    std::lock_guard<std::mutex> lk(g_stop_mu);
    g_stop = false;
  }
  g_thread = std::thread(reader);
  log("[se] bridge reader watching %s%s", se_dir().c_str(), cfg_int("se_bridge", 1) ? "" : " (off: se_bridge=0)");
}

}  // namespace

const char* se_intern(const std::string& s) {
  static std::mutex mu;
  static std::set<std::string> pool;
  std::lock_guard<std::mutex> lk(mu);
  return pool.insert(s).first->c_str();
}

// Two games at once (scripts/auto-run.ps1 shared runs) would both read and write the
// same bridge files. auto-run.ps1 then passes "bg3rtxse=<token>" on the game's command
// line; this process and its Lua (Ext.Utils.GetCommandLineParams) both see it, and
// both use the subfolder bg3rtx\<token>\ instead (docs/SE_BRIDGE.md).
std::string se_token() {
  static const std::string tok = [] {
    std::string t;
    const wchar_t* cl = GetCommandLineW();
    const wchar_t* at = cl ? wcsstr(cl, L"bg3rtxse=") : nullptr;
    if (at)
      for (const wchar_t* c = at + 9; *c && t.size() < 24; ++c) {
        if (!(iswalnum(*c) || *c == L'_' || *c == L'-')) break;
        t += char(*c);
      }
    return t;
  }();
  return tok;
}

std::string se_dir() {
  char buf[MAX_PATH];
  if (GetEnvironmentVariableA("BG3RTX_SE_DIR", buf, MAX_PATH)) return buf;
  if (!GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH)) return "";
  std::string d = std::string(buf) + "\\Larian Studios\\Baldur's Gate 3\\Script Extender\\bg3rtx";
  return se_token().empty() ? d : d + "\\" + se_token();
}

static bool read_count(std::istringstream& in, uint32_t& value) {
  std::string token;
  if (!(in >> token) || token.empty()) return false;
  for (char c : token) if (c < '0' || c > '9') return false;
  auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
}

bool se_parse_lights(const std::string& text, SeSnapshot& out) {
  if (text.compare(0, 11, "BG3RTX_SE 1") != 0 || !ends_with_end(text)) return false;
  out.all_known = out.classified_known = out.pending_known = out.out_known = false;
  std::istringstream in(text);
  std::string line;
  std::getline(in, line);   // magic
  bool header = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "END") return header;
    if (line.compare(0, 4, "seq ") == 0) {
      std::istringstream h(line);
      std::string k, level;
      while (h >> k) {
        if (k == "seq") h >> out.seq;
        else if (k == "level") { h >> level; out.level = se_intern(level == "-" ? "" : level); }
        else if (k == "host") h >> out.host[0] >> out.host[1] >> out.host[2];
        else if (k == "scan_us") h >> out.scan_us;
        else if (k == "avg_us") h >> out.avg_us;
        else if (k == "max_us") h >> out.max_us;
        else if (k == "all") { out.all_known = read_count(h, out.all); if (!out.all_known) return false; }
        else if (k == "classified") { out.classified_known = read_count(h, out.classified); if (!out.classified_known) return false; }
        else if (k == "pending") { out.pending_known = read_count(h, out.pending); if (!out.pending_known) return false; }
        else if (k == "out") { out.out_known = read_count(h, out.out); if (!out.out_known) return false; }
        else if (k == "classify_us") h >> out.classify_us;
        else { std::string skip; h >> skip; }
      }
      header = true;
    } else if (line.compare(0, 2, "L ") == 0) {
      SeLight l{};
      unsigned owner = 0, type = 0, channel = 0;
      unsigned long long id = 0;
      char name[256] = {};
      int n = sscanf_s(line.c_str() + 2, "%llu %u %u %f %f %f %f %f %f %f %f %f %u %u %f %f %f %f %255s", &id, &owner,
                       &type, &l.pos[0], &l.pos[1], &l.pos[2], &l.radius, &l.color[0], &l.color[1], &l.color[2],
                       &l.intensity, &l.kelvin, &l.flags, &channel, &l.flicker_speed, &l.flicker_amount, &l.move_speed,
                       &l.move_amount, name, unsigned(sizeof name));
      if (n < 18) continue;
      l.id = id;
      l.owner = uint8_t(owner);
      l.type = uint8_t(type);
      l.channel = uint8_t(channel);
      l.name = se_intern(n >= 19 && std::strcmp(name, "-") != 0 ? name : "");
      // The bridge (v1 Lua) calls every Light_Player_* template a character light. Only the
      // darkvision pair follows the controlled character; Light_Player_Fill_* are fill lights
      // at fixed world positions (the same coordinates in every run of a save). Those keep
      // owner "unknown" (a visual light) and lose the player-light flag, so that D-LS treats
      // them like any other light (docs/TRACK_D_RT.md 9.10). The v2 Lua does this itself.
      if (l.owner == BG3_RTL_OWNER_CHARACTER && (l.flags & BG3_RTL_PLAYER_LIGHT) &&
          std::strncmp(name, "Light_Player_Darkvision", 23) != 0) {
        l.owner = BG3_RTL_OWNER_UNKNOWN;
        l.flags &= ~uint32_t(BG3_RTL_PLAYER_LIGHT);
      }
      out.lights.push_back(l);
    }
  }
  return false;   // no END line
}

bool se_parse_world(const std::string& text, SeSnapshot& out) {
  if (text.compare(0, 17, "BG3RTX_SE_WORLD 1") != 0 || !ends_with_end(text)) return false;
  std::istringstream in(text);
  std::string line;
  std::getline(in, line);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "END") return true;
    std::istringstream l(line);
    std::string k;
    l >> k;
    if (k == "level") {
      std::string lv, r, region;
      l >> lv >> r >> region;
      out.region = se_intern(region == "-" ? "" : region);
    } else if (k == "T") {
      std::string kind, name, cur;
      l >> kind >> name >> cur;
      if (kind.empty() || name.empty()) continue;
      out.triggers.push_back({kind[0], se_intern(name), se_intern(cur == "-" ? "" : cur)});
    }
  }
  return false;
}

std::shared_ptr<const SeSnapshot> se_latest() {
  start();
  std::lock_guard<std::mutex> lk(g_mu);
  return g_snap;
}

double se_age_s(const SeSnapshot& s) { return now_seconds() - s.time; }

double se_now_s() { return now_seconds(); }

std::shared_ptr<const SeSnapshot> se_snapshot() {
  auto s = se_latest();
  if (!s || !cfg_int("se_bridge", 1)) return nullptr;
  if (s->time < g_process_start) return nullptr;   // an export of an earlier game session
  if (se_age_s(*s) * 1000.0 > se_stale_ms()) return nullptr;
  return s;
}

double se_period_s() {
  std::lock_guard<std::mutex> lk(g_period_mu);
  double p = kSeExportPeriodS;
  for (int i = 0; i < g_n_interval; ++i) p = (std::max)(p, g_interval[i]);
  return p;
}

// Staleness: an export is in use until the next one arrives, at most one period plus the
// reader's poll later. se_stale_periods (k, default 4) periods tolerate k-1 exports in a row
// that did not arrive (a hitch, a Lua tick that failed) before the bridge counts as gone and
// the lights fall back to the database. Measured periods: 0.289 s and 0.330 s mean over
// 97 and 186 exports (bg3rtx.log, bg3rtx.a.log, 2026-10-27), so k=4 gives 1.4-1.5 s.
double se_stale_ms() {
  const int fixed = cfg_int("se_stale_ms", 0);
  if (fixed > 0) return double(fixed);
  const int k = (std::max)(cfg_int("se_stale_periods", 4), 1);
  return (k * se_period_s() + kSePollS) * 1000.0;
}

float se_ulp(float m) {
  int e = 0;
  std::frexp((std::max)(std::fabs(m), 1e-30f), &e);   // m = f * 2^e, f in [0.5, 1)
  return std::ldexp(1.0f, e - 24);
}

float se_static_tol(const float p[3], float reach, float move_amount) {
  float m = (std::max)((std::max)(std::fabs(p[0]), std::fabs(p[1])), std::fabs(p[2])) + (std::max)(reach, 0.0f);
  // Per coordinate 4 ulp + the %.4f rounding; sqrt(3) of that for the 3D distance.
  return 1.7320508f * (4.0f * se_ulp(m) + kSePosQuant) + (std::max)(move_amount, 0.0f);
}

float se_static_radius_tol(float r) { return kSeRadiusQuant + 4.0f * se_ulp(r); }

float se_moving_radius_tol(float r) { return 0.02f * (std::max)(r, 0.1f) + 0.01f; }

bool se_classification_complete(const SeSnapshot& s) {
  return s.all_known && s.classified_known && s.pending_known && s.out_known &&
         s.classified == s.all && s.pending == 0 && s.out <= s.classified && s.out == s.lights.size();
}

bool se_covers(const SeSnapshot& s, const float p[3]) {
  if (!se_classification_complete(s)) return false; // Unknown absence retains the database fallback.
  const float e = kSeHostQuant;
  float d2 = 0;
  for (int k = 0; k < 3; ++k) d2 += (p[k] - s.host[k]) * (p[k] - s.host[k]);
  if (std::sqrt(d2) + e > kSeRange) return false;
  // The Lua scans cells cy-2..cy+2 (cy = floor(host.y / 16)); both cell rows must be certain.
  const int h0 = int(std::floor((s.host[1] - e) / kSeCell)), h1 = int(std::floor((s.host[1] + e) / kSeCell));
  const int p0 = int(std::floor((p[1] - e) / kSeCell)), p1 = int(std::floor((p[1] + e) / kSeCell));
  return (std::max)(std::abs(p1 - h0), std::abs(p0 - h1)) <= kSeCellsY;
}

uint32_t se_exports_read() { return g_reads.load(std::memory_order_relaxed); }

void se_stop() {
  std::lock_guard<std::mutex> tl(g_thread_mu);
  {
    std::lock_guard<std::mutex> lk(g_stop_mu);
    g_stop = true;
  }
  g_stop_cv.notify_all();
  if (g_thread.joinable()) g_thread.join();
}

}  // namespace bg3
