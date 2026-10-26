#include "host.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
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
  if (!g_log) g_log = _fsopen((root_dir() + "\\bg3rtx.log").c_str(), "w", _SH_DENYNO);  // readable while running
  if (g_log) {
    fputs(line, g_log);
    fflush(g_log);
  }
  OutputDebugStringA(line);
}

// ---- config: key=value lines, '#' comments ----

static std::mutex g_cfg_mu;
static std::unordered_map<std::string, std::string> g_cfg;
static uint64_t g_cfg_mtime = 0;

void cfg_poll() {
  char env[MAX_PATH] = {};
  const std::string path = GetEnvironmentVariableA("BG3RTX_INI", env, MAX_PATH) ? std::string(env)
                                                                                 : root_dir() + "\\bg3rtx.ini";
  uint64_t mt = file_mtime(path);
  if (mt == g_cfg_mtime) return;
  g_cfg_mtime = mt;

  std::unordered_map<std::string, std::string> next;
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "r") == 0 && f) {
    char line[512];
    while (fgets(line, sizeof line, f)) {
      std::string s(line);
      size_t hash = s.find('#');
      if (hash != std::string::npos) s.resize(hash);
      size_t eq = s.find('=');
      if (eq == std::string::npos) continue;
      auto trim = [](std::string v) {
        size_t a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
      };
      next[trim(s.substr(0, eq))] = trim(s.substr(eq + 1));
    }
    fclose(f);
  }
  if (next.empty() && mt) {
    // Caught mid-write (an editor or script truncates before writing): keep
    // the previous settings and read again on the next poll.
    g_cfg_mtime = 0;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_cfg_mu);
    g_cfg.swap(next);
  }
  log("[cfg] bg3rtx.ini read, %zu keys", g_cfg.size());
}

static bool cfg_raw(const char* key, std::string& out) {
  std::lock_guard<std::mutex> lock(g_cfg_mu);
  auto it = g_cfg.find(key);
  if (it == g_cfg.end()) return false;
  out = it->second;
  return true;
}

int cfg_int(const char* key, int fallback) {
  std::string v;
  return cfg_raw(key, v) ? std::atoi(v.c_str()) : fallback;
}

float cfg_float(const char* key, float fallback) {
  std::string v;
  return cfg_raw(key, v) ? float(std::atof(v.c_str())) : fallback;
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
