// Test-run save selection (docs/TEST_SCENES.md).
//
// With BG3RTX_SAVE_ONLY=<save folder name> set, the game's directory listing
// of Savegames\Story shows only that folder, so -continueGame loads it. Nothing
// on disk changes: the player's saves stay where they are, and Steam Cloud
// (which syncs that folder) sees no difference. The game itself sorts its saves
// by the save time stored inside each package, so file timestamps cannot do
// this.
//
// bg3.exe imports FindFirstFileExW / FindFirstFileW / FindNextFileW from
// KERNEL32; those IAT slots are patched when this DLL loads. MSVCP140 is
// patched as well in case the listing goes through std::filesystem.
//
// With BG3RTX_SAVE_LOG=<file> set (scripts/auto-run.ps1), every save folder this
// game creates in Savegames\Story (CreateDirectoryW, or MoveFileExW into it) is
// appended to that file, one folder name per line. auto-run.ps1 parks exactly
// those afterwards, so with two games running it never moves a save another
// game, or the player, wrote.
#include <windows.h>
#include <cwctype>
#include <mutex>
#include <set>
#include <string>
#include "host.h"

namespace {

std::wstring g_only;              // folder name to keep
std::mutex g_mu;
std::set<HANDLE> g_story;         // find handles listing Savegames\Story
int g_listings = 0, g_hidden = 0, g_patched = 0;

decltype(&FindFirstFileExW) real_ffex = nullptr;
decltype(&FindFirstFileW) real_ff = nullptr;
decltype(&FindNextFileW) real_fn = nullptr;
decltype(&FindClose) real_fc = nullptr;
decltype(&CreateDirectoryW) real_cd = nullptr;
decltype(&MoveFileExW) real_mv = nullptr;
std::wstring g_save_log;          // BG3RTX_SAVE_LOG

std::wstring lower(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
  return s;
}

// A listing of the Story folder itself ("...\Savegames\Story\*"), not of one save.
bool is_story_listing(LPCWSTR pattern) {
  if (!pattern) return false;
  std::wstring p = lower(pattern);
  for (auto& c : p) if (c == L'/') c = L'\\';
  auto at = p.rfind(L"\\savegames\\story\\");
  return at != std::wstring::npos && p.find(L'\\', at + 17) == std::wstring::npos;
}

bool hide(const WIN32_FIND_DATAW& d) {
  if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
  if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..")) return false;
  return _wcsicmp(d.cFileName, g_only.c_str()) != 0;
}

// Advance past hidden entries; false when the listing has nothing left.
bool skip_hidden(HANDLE h, WIN32_FIND_DATAW* d) {
  while (hide(*d)) {
    ++g_hidden;
    if (!real_fn(h, d)) return false;
  }
  if (wcscmp(d->cFileName, L".") && wcscmp(d->cFileName, L".."))
    bg3::log("[save] listing shows %ls (%d hidden so far)", d->cFileName, g_hidden);
  return true;
}

HANDLE first(HANDLE h, LPCWSTR pattern, WIN32_FIND_DATAW* d) {
  if (h == INVALID_HANDLE_VALUE || !is_story_listing(pattern)) return h;
  std::lock_guard<std::mutex> lock(g_mu);
  ++g_listings;
  int before = g_hidden;
  bool any = skip_hidden(h, d);
  // Logged here, not at load: the log is not set up yet when this DLL initialises.
  bg3::log("[save] Story listing %d: showing only %ls, %d other saves hidden (%d import slots patched)",
      g_listings, g_only.c_str(), g_hidden - before, g_patched);
  if (!any) {
    real_fc(h);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  g_story.insert(h);
  return h;
}

HANDLE WINAPI hk_ffex(LPCWSTR name, FINDEX_INFO_LEVELS lvl, LPVOID data, FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags) {
  HANDLE h = real_ffex(name, lvl, data, op, filter, flags);
  return first(h, name, static_cast<WIN32_FIND_DATAW*>(data));
}

HANDLE WINAPI hk_ff(LPCWSTR name, LPWIN32_FIND_DATAW data) {
  return first(real_ff(name, data), name, data);
}

// Handle values are reused: forget a listing when it closes, or a later,
// unrelated listing that gets the same handle would be filtered too.
BOOL WINAPI hk_fc(HANDLE h) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_story.erase(h);
  }
  return real_fc(h);
}

BOOL WINAPI hk_fn(HANDLE h, LPWIN32_FIND_DATAW d) {
  if (!real_fn(h, d)) return FALSE;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_story.count(h)) return TRUE;
  if (skip_hidden(h, d)) return TRUE;
  SetLastError(ERROR_NO_MORE_FILES);
  return FALSE;
}

// A save folder itself ("...\Savegames\Story\<name>"): its name, else empty.
std::wstring story_child(LPCWSTR path) {
  if (!path) return {};
  std::wstring p = path;
  for (auto& c : p) if (c == L'/') c = L'\\';
  while (!p.empty() && p.back() == L'\\') p.pop_back();
  std::wstring l = lower(p);
  auto at = l.rfind(L"\\savegames\\story\\");
  if (at == std::wstring::npos || l.find(L'\\', at + 17) != std::wstring::npos) return {};
  return p.substr(at + 17);
}

void record_save(LPCWSTR path) {
  std::wstring name = story_child(path);
  if (name.empty() || g_save_log.empty()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  FILE* f = nullptr;
  if (_wfopen_s(&f, g_save_log.c_str(), L"ab") != 0 || !f) return;
  int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), int(name.size()), nullptr, 0, nullptr, nullptr);
  std::string u(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, name.c_str(), int(name.size()), u.data(), n, nullptr, nullptr);
  u += "\n";
  fwrite(u.data(), 1, u.size(), f);
  fclose(f);
}

BOOL WINAPI hk_cd(LPCWSTR path, LPSECURITY_ATTRIBUTES sa) {
  BOOL ok = real_cd(path, sa);
  if (ok) record_save(path);
  return ok;
}

BOOL WINAPI hk_mv(LPCWSTR from, LPCWSTR to, DWORD flags) {
  BOOL ok = real_mv(from, to, flags);
  if (ok) record_save(to);
  return ok;
}

int patch_module(HMODULE mod) {
  if (!mod) return 0;
  auto base = reinterpret_cast<uint8_t*>(mod);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return 0;
  int patched = 0;
  for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
    if (!imp->OriginalFirstThunk || !imp->FirstThunk) continue;
    auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
    auto iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++iat) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      auto n = reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
      bool filter = !g_only.empty(), track = !g_save_log.empty();
      void* hook = filter && !strcmp(n, "FindFirstFileExW") ? reinterpret_cast<void*>(&hk_ffex)
                 : filter && !strcmp(n, "FindFirstFileW")   ? reinterpret_cast<void*>(&hk_ff)
                 : filter && !strcmp(n, "FindNextFileW")    ? reinterpret_cast<void*>(&hk_fn)
                 : filter && !strcmp(n, "FindClose")        ? reinterpret_cast<void*>(&hk_fc)
                 : track && !strcmp(n, "CreateDirectoryW")  ? reinterpret_cast<void*>(&hk_cd)
                 : track && !strcmp(n, "MoveFileExW")       ? reinterpret_cast<void*>(&hk_mv) : nullptr;
      if (!hook) continue;
      DWORD old = 0;
      VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
      iat->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
      VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
      ++patched;
    }
  }
  return patched;
}

struct Init {
  Init() {
    wchar_t buf[512];
    DWORD n = GetEnvironmentVariableW(L"BG3RTX_SAVE_ONLY", buf, 512);
    if (n && n < 512) g_only = buf;
    n = GetEnvironmentVariableW(L"BG3RTX_SAVE_LOG", buf, 512);
    if (n && n < 512) g_save_log = buf;
    if (g_only.empty() && g_save_log.empty()) return;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    real_cd = reinterpret_cast<decltype(real_cd)>(GetProcAddress(k32, "CreateDirectoryW"));
    real_mv = reinterpret_cast<decltype(real_mv)>(GetProcAddress(k32, "MoveFileExW"));
    real_ffex = reinterpret_cast<decltype(real_ffex)>(GetProcAddress(k32, "FindFirstFileExW"));
    real_ff = reinterpret_cast<decltype(real_ff)>(GetProcAddress(k32, "FindFirstFileW"));
    real_fn = reinterpret_cast<decltype(real_fn)>(GetProcAddress(k32, "FindNextFileW"));
    real_fc = reinterpret_cast<decltype(real_fc)>(GetProcAddress(k32, "FindClose"));
    g_patched = patch_module(GetModuleHandleW(nullptr)) + patch_module(GetModuleHandleW(L"msvcp140.dll"));
  }
} g_init;

}  // namespace
