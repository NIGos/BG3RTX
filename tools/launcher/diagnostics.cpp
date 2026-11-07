#include "diagnostics.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include <iomanip>

namespace launcher::diagnostics {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t hard_file = 8 * 1024 * 1024, hard_total = 32 * 1024 * 1024;
constexpr size_t hard_count = 64;
struct Handle {
  HANDLE h = INVALID_HANDLE_VALUE;
  ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};
// A whole disk mounted as a folder (e.g. E:\Storage) is a real volume, not a redirect
// into another tree: Windows resolves it to a volume GUID only for true mount points.
bool volume_mount_point(const fs::path& q) {
  std::wstring dir = q.wstring();
  if (dir.empty()) return false;
  if (dir.back() != L'\\') dir += L'\\';
  wchar_t volume[64];
  return GetVolumeNameForVolumeMountPointW(dir.c_str(), volume, 64) != 0;
}
bool safe_path(const fs::path& p) {
  if (!p.is_absolute()) return false;
  for (const auto& part : p) if (part == L".." || part == L".") return false;
  // Reject alternate data streams, devices and UNC paths as well as ancestors
  // that are junctions or symlinks. This service accepts ordinary local drive paths only
  // (a whole-volume mount point on the way is allowed).
  const auto s = p.wstring();
  if (s.size() < 3 || s[1] != L':' || s[2] != L'\\' || s.find(L':', 2) != s.npos) return false;
  for (auto q = p; !q.empty(); q = q.parent_path()) {
    DWORD a = GetFileAttributesW(q.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) && !volume_mount_point(q)) return false;
    if (q == q.parent_path()) break;
  }
  return true;
}
bool handle_matches(HANDLE h, const fs::path& p) {
  wchar_t name[32768];
  DWORD n = GetFinalPathNameByHandleW(h, name, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!n || n >= 32768) return false;
  std::wstring actual(name, n), expected = L"\\\\?\\" + p.wstring();
  return _wcsicmp(actual.c_str(), expected.c_str()) == 0 && safe_path(p);
}
std::string utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
std::string windows_error(DWORD code) {
  wchar_t* message = nullptr;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
  std::string text = n ? utf8(std::wstring(message, n)) : "Unknown Windows error";
  if (message) LocalFree(message);
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();
  return std::to_string(code) + ": " + text;
}
std::string env(const wchar_t* key) {
  wchar_t b[32768]; DWORD n = GetEnvironmentVariableW(key, b, 32768);
  return n && n < 32768 ? utf8(std::wstring(b, n)) : "";
}
void replace_ci(std::string& s, const std::string& from, const char* to) {
  if (from.empty()) return;
  auto lower = [](std::string x) { for (auto& c : x) c = char(std::tolower(static_cast<unsigned char>(c))); return x; };
  size_t pos = 0;
  auto f = lower(from);
  while ((pos = lower(s).find(f, pos)) != s.npos) { s.replace(pos, from.size(), to); pos += strlen(to); }
}
uint32_t crc(const std::string& s) {
  uint32_t c = ~0u;
  for (unsigned char b : s) { c ^= b; for (int i = 0; i < 8; ++i) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); }
  return ~c;
}
std::string stamp(FILETIME t) {
  SYSTEMTIME s{}; FileTimeToSystemTime(&t, &s);
  char b[64]; snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02uZ", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, s.wSecond);
  return b;
}
bool cancelled(const Progress& cb, size_t d, size_t t) { return cb && !cb(d, t); }
}

std::string redact_text(std::string text, const fs::path& root) {
  // Longest known paths first; also redact slash and JSON-escaped spellings.
  for (auto p : {utf8(root.wstring()), env(L"USERPROFILE"), env(L"LOCALAPPDATA"), env(L"APPDATA")}) {
    replace_ci(text, p, "[LOCAL_PATH]");
    std::string slash = p; std::replace(slash.begin(), slash.end(), '\\', '/'); replace_ci(text, slash, "[LOCAL_PATH]");
    std::string escaped; for (char c : p) { escaped += c; if (c == '\\') escaped += c; }
    replace_ci(text, escaped, "[LOCAL_PATH]");
  }
  using R = std::regex;
  const auto ic = R::icase;
  text = std::regex_replace(text, R(R"(([a-z]:[\\/]+(?:Users|Documents and Settings)[\\/]+)[^\\/\r\n"']+)", ic), "$1[USER]");
  text = std::regex_replace(text, R(R"((["']?\b(?:api[_-]?key|access[_-]?token|refresh[_-]?token|auth[_-]?token|client[_-]?secret|private[_-]?key|license[_-]?key|password|passwd|secret|token|key)\b["']?\s*[:=]\s*)(?:"[^"\r\n]*"|'[^'\r\n]*'|[^\s,;&}\]\r\n]+))", ic), "$1[REDACTED]");
  text = std::regex_replace(text, R(R"((\bBearer\s+)[a-z0-9._~+/-]+=*)", ic), "$1[REDACTED]");
  text = std::regex_replace(text, R(R"((https?://)[^\s/@:]+:[^\s/@]+@)", ic), "$1[REDACTED]@");
  text = std::regex_replace(text, R(R"(\b(?:sk-[a-zA-Z0-9_-]{16,}|gh[pousr]_[a-zA-Z0-9_]{16,}|AKIA[A-Z0-9]{16})\b)"), "[REDACTED]");
  // Environment/command lines are not useful report inputs; drop whole lines.
  text = std::regex_replace(text, R(R"((^|\n)[^\r\n]*(?:command.?line|environment\s*[:=])[^\r\n]*)", ic), "$1[OMITTED environment/command line]");
  return text;
}

Snapshot prepare_report(const Request& r, Progress cb) {
  Snapshot out;
  std::error_code directory_error;
  if (!safe_path(r.install_root) || !fs::is_directory(r.install_root, directory_error)) {
    out.message = "Choose a local installation folder that is not a link."; return out;
  }
  if (r.max_files < 8 || r.max_files > hard_count || r.max_file_bytes == 0 || r.max_file_bytes > hard_file ||
      r.max_total_bytes < 65536 || r.max_total_bytes > hard_total || r.issue.size() > 65536 || r.system_text.size() > 65536) {
    out.message = "Report could not be prepared. Check the description length and retry."; return out;
  }
  if (!r.include_logs && !r.include_settings && !r.include_system && r.issue.empty()) {
    out.message = "Select report contents or describe the problem."; return out;
  }
  out.status = Status::Complete;
  std::vector<std::string> notes;
  auto note = [&](std::string s) { notes.push_back(std::move(s)); out.status = Status::Partial; };
  auto add = [&](std::string name, std::string text, std::string info = "") {
    text = redact_text(std::move(text), r.install_root);
    if (out.files.size() + 1 >= r.max_files || out.bytes + text.size() + 32768 > r.max_total_bytes) {
      note(name + ": Omitted: report size or file limit reached."); return false;
    }
    out.bytes += text.size(); out.files.push_back({std::move(name), std::move(text), std::move(info)}); return true;
  };
  std::vector<fs::path> inputs = {L"bg3rtx.log", L"bg3rtx.prev.log", L"bg3rtx.ini", L"bg3rtx_user.ini", L"VERSION.txt"};
  // Bounded, nonrecursive discovery. Never even list captures or a save folder.
  auto discover = [&](const fs::path& dir, bool streamline) {
    if (!safe_path(dir)) { note("Log folder skipped because it is a link."); return; }
    WIN32_FIND_DATAW data{};
    HANDLE h = FindFirstFileW((dir / L"*").c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return;
    size_t seen = 0;
    do {
      if (++seen > 512) { note("Directory scan bounded at 512 entries"); break; }
      if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      fs::path name(data.cFileName);
      std::string n = utf8(name.wstring());
      bool allowed = streamline ? std::regex_match(n, std::regex(R"([A-Za-z0-9_.-]+\.(log|txt))", std::regex::icase))
        : std::regex_match(n, std::regex(R"(bg3rtx(?:\.[A-Za-z0-9_-]+)*\.log(?:\.\d+)?)", std::regex::icase));
      if (!allowed) continue;
      auto rel = streamline ? fs::path(L"sl_logs") / name : name;
      if (std::find(inputs.begin(), inputs.end(), rel) == inputs.end()) {
        if (inputs.size() >= r.max_files) { note("Additional logs omitted (file limit)"); break; }
        inputs.push_back(rel);
      }
    } while (FindNextFileW(h, &data));
    FindClose(h);
  };
  if (r.include_logs) {
    discover(r.install_root, false);
    discover(r.install_root / L"sl_logs", true);
  }
  std::string effective;
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (cancelled(cb, i, inputs.size())) { out = {}; out.status = Status::Cancelled; out.message = "Report cancelled. No files saved."; return out; }
    if ((i < 2 || i >= 5) ? !r.include_logs : !r.include_settings) continue;
    auto path = r.install_root / inputs[i];
    const std::string label = i < 5 ? utf8(inputs[i].wstring()) : "log-" + std::to_string(i) + ".txt";
    if (!safe_path(path)) { note(label + ": skipped (linked path)"); continue; }
    Handle f;
    f.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f.h == INVALID_HANDLE_VALUE) {
      note(label + ": missing or unreadable (Windows error " + windows_error(GetLastError()) + ")"); continue;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(f.h, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || !handle_matches(f.h, path)) {
      note(label + ": unsafe input skipped"); continue;
    }
    uint64_t size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size > r.max_file_bytes) { note(label + ": oversized; omitted rather than truncate credentials"); continue; }
    std::string text; text.reserve(size_t(size));
    bool ok = true;
    for (uint64_t read = 0; read < size;) {
      if (cancelled(cb, i, inputs.size())) { out = {}; out.status = Status::Cancelled; out.message = "Report cancelled. No files saved."; return out; }
      char b[65536]; DWORD got = 0;
      if (!ReadFile(f.h, b, DWORD(std::min<uint64_t>(sizeof(b), size - read)), &got, nullptr)) { ok = false; break; }
      if (!got) break;
      text.append(b, got); read += got;
    }
    if (!ok || text.size() != size) { note(label + ": read failed or file changed; omitted"); continue; }
    if (text.find('\0') != text.npos) { note(label + ": binary/unsupported text encoding; omitted"); continue; }
    if (!text.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0)) {
      note(label + ": invalid UTF-8; omitted"); continue;
    }
    std::string info_text = "Snapshot last-write UTC: " + stamp(info.ftLastWriteTime);
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t{}, today{}; t.LowPart = info.ftLastWriteTime.dwLowDateTime; t.HighPart = info.ftLastWriteTime.dwHighDateTime;
    today.LowPart = now.dwLowDateTime; today.HighPart = now.dwHighDateTime;
    if (today.QuadPart > t.QuadPart && today.QuadPart - t.QuadPart > 24ull * 60 * 60 * 10000000) {
      info_text += "; older than 24 hours (may be stale)"; note(label + ": older than 24 hours; check session timestamp");
    }
    if (i == 0) {
      std::istringstream lines(text); std::string line;
      std::string knobs, keys;
      while (std::getline(lines, line)) if (line.find("[cfg]") != line.npos) {
        if (line.find("knobs:") != line.npos) { knobs = line; keys.clear(); }
        if (line.find("keys:") != line.npos) keys = line;
      }
      if (!knobs.empty() || !keys.empty()) effective = "Latest logged configuration from bg3rtx.log only; " + info_text
        + ". Logged session data, NOT currently saved settings.\n" + knobs + '\n' + keys + '\n';
    }
    if (i == 2) { std::ostringstream id; id << "Packaged defaults CRC32 (raw snapshot, not a security hash): " << std::hex << crc(text); info_text += "; " + id.str(); }
    add((i < 2 || i >= 5 ? "logs/" : "settings/") + label, text, info_text);
  }
  if (r.include_logs) {
    if (effective.empty()) { note("Effective configuration unavailable from current log"); effective = "No current log configuration available. Saved settings are separate.\n"; }
    add("settings/effective-log.txt", effective);
  }
  if (r.include_system) {
  if (r.system_text.empty()) note("Graphics inspection unavailable");
  std::string system = r.system_text.empty() ? "Graphics inspection unavailable.\n" : r.system_text;
  // RtlGetVersion reports the actual Windows build without manifest version lies.
  OSVERSIONINFOW os{}; os.dwOSVersionInfoSize = sizeof(os);
  using Version = LONG(WINAPI*)(OSVERSIONINFOW*);
  auto version = reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
  if (version && version(&os) == 0) system += "\nWindows build: " + std::to_string(os.dwMajorVersion) + "." + std::to_string(os.dwMinorVersion) + "." + std::to_string(os.dwBuildNumber);
  DEVMODEW mode{}; mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) system += "\nPrimary display: " + std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight) + " @ " + std::to_string(mode.dmDisplayFrequency) + " Hz (game mode unconfirmed)\n";
  add("system/system.txt", system);
  }
  if (!r.issue.empty()) add("issue.txt", r.issue);
  std::ostringstream manifest;
  manifest << privacy_text << "\n\nRead-only bounded snapshots. Text redaction is best effort; review every file.\n";
  for (const auto& f : out.files) manifest << f.name << " (" << f.text.size() << " bytes) " << f.note << '\n';
  manifest << "\nMissing/skipped/stale items:\n";
  for (const auto& n : notes) manifest << n << '\n';
  std::string m = redact_text(manifest.str(), r.install_root);
  out.bytes += m.size(); out.files.push_back({"CONTENTS.txt", m, "Report manifest"});
  out.message = out.status == Status::Complete ? "Report ready to preview." : "Partial report ready. See CONTENTS.txt for omitted files.";
  return out;
}

ExportResult save_report_zip(const Snapshot& s, const fs::path& path, Progress cb) {
  if (s.status != Status::Complete && s.status != Status::Partial) return {Status::Failed, "Preview a report before saving."};
  if (!safe_path(path) || path.extension() != L".zip" || s.files.empty() || s.files.size() > hard_count) return {Status::Failed, "Choose a new .zip file in an existing local folder that is not a link."};
  uint64_t bytes = 0;
  std::vector<std::string> names;
  for (const auto& e : s.files) {
    if (e.name.empty() || e.name.size() > 255 || e.name.find("..") != e.name.npos || e.name.front() == '/' ||
        e.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-") != e.name.npos ||
        std::find(names.begin(), names.end(), e.name) != names.end()) return {Status::Failed, "Report contains an invalid file. Prepare it again."};
    names.push_back(e.name); bytes += e.text.size();
  }
  if (bytes > hard_total) return {Status::Failed, "Report is too large to save."};
  if (cancelled(cb, 0, s.files.size())) return {Status::Cancelled, "Save cancelled."};
  Handle output;
  // DELETE access + FILE_DISPOSITION_INFO deletes the exact owned handle on
  // cancellation/failure, even if an ancestor is renamed. Never delete by path.
  output.h = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (output.h == INVALID_HANDLE_VALUE) return {Status::Failed, "Report not saved. Retry with a new filename or another folder. Existing files are kept. Windows error " + windows_error(GetLastError()) + "."};
  auto fail = [&](Status status, const char* msg) {
    FILE_DISPOSITION_INFO d{TRUE};
    if (!SetFileInformationByHandle(output.h, FileDispositionInfo, &d, sizeof(d)))
      return ExportResult{Status::Failed, "Report not saved. An incomplete ZIP remains; do not share it. Choose another filename. Windows error " + windows_error(GetLastError()) + "."};
    return ExportResult{status, msg};
  };
  if (!handle_matches(output.h, path)) return fail(Status::Failed, "Destination changed or is a link. Choose another folder.");
  // ZIP32 STORE: standard ZIP, no runtime/script/SDK dependency. Bounded payload.
  uint32_t offset = 0; bool ok = true;
  size_t done = 0;
  bool was_cancelled = false;
  auto put = [&](const std::string& b) {
    for (size_t p = 0; p < b.size();) {
      if (cancelled(cb, done, s.files.size())) { was_cancelled = true; return false; }
      DWORD n = DWORD(std::min<size_t>(65536, b.size() - p)), written = 0;
      if (!WriteFile(output.h, b.data() + p, n, &written, nullptr) || written != n) return false;
      p += n; offset += n;
    }
    return true;
  };
  auto u16 = [](std::string& b, uint16_t v) { b += char(v); b += char(v >> 8); };
  auto u32 = [&](std::string& b, uint32_t v) { u16(b, uint16_t(v)); u16(b, uint16_t(v >> 16)); };
  std::string central;
  for (const auto& e : s.files) {
    if (cancelled(cb, done, s.files.size())) return fail(Status::Cancelled, "Save cancelled. Incomplete ZIP removed.");
    uint32_t start = offset, hash = crc(e.text), length = uint32_t(e.text.size());
    std::string h; u32(h, 0x04034b50); u16(h, 20); u16(h, 0x800); u16(h, 0); u16(h, 0); u16(h, 33);
    u32(h, hash); u32(h, length); u32(h, length); u16(h, uint16_t(e.name.size())); u16(h, 0); h += e.name;
    if (!put(h) || !put(e.text)) { ok = false; break; }
    u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0x800); u16(central, 0); u16(central, 0); u16(central, 33);
    u32(central, hash); u32(central, length); u32(central, length); u16(central, uint16_t(e.name.size()));
    u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0); u32(central, 0); u32(central, start); central += e.name;
    ++done;
  }
  uint32_t central_start = offset;
  if (ok) ok = put(central);
  std::string end; u32(end, 0x06054b50); u16(end, 0); u16(end, 0); u16(end, uint16_t(s.files.size())); u16(end, uint16_t(s.files.size()));
  u32(end, uint32_t(central.size())); u32(end, central_start); u16(end, 0);
  if (ok) ok = put(end) && FlushFileBuffers(output.h);
  if (!ok) return fail(was_cancelled ? Status::Cancelled : Status::Failed, "Save stopped. Incomplete ZIP removed; retry or choose another folder.");
  return {s.status, s.status == Status::Partial ? "Partial report saved. Review CONTENTS.txt before sharing." : "Report saved on your PC."};
}
int exit_code(Status s) { return s == Status::Complete ? 0 : s == Status::Partial ? 1 : s == Status::Cancelled ? 3 : 2; }
} // namespace launcher::diagnostics
