// Background test runs (auto-run.ps1 -Background, env BG3RTX_BACKGROUND=1).
//
// BG3 stops rendering when its window loses focus or is minimised, so test
// runs used to force the game to the foreground, which takes the PC away from
// the user. Here the game is told it always has focus instead:
//   - SDL_PollEvent drops focus-lost / minimise / hide window events;
//   - SDL_GetWindowFlags reports input focus and never minimised or hidden;
//   - GetForegroundWindow returns the game's own window;
//   - SDL_RaiseWindow / SetForegroundWindow / SDL_MinimizeWindow do nothing,
//     so the game never steals the foreground from the user.
// Streamline and DLSS-G (sl.common, sl.pcl, nvngx_dlssg) also check
// GetForegroundWindow and stop generating frames when the game is not in
// front; their imports are patched as they load (a watcher thread).
// Run borderless (auto-run sets Fullscreen=0, FakeFullscreenEnabled=1) so the
// desktop mode never changes. Key input for -Move is posted to the window.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <set>
#include <psapi.h>
#include "host.h"

namespace {

using PollEvent = int(__cdecl*)(void*);
using GetFlags = uint32_t(__cdecl*)(void*);
using VoidWin = void(__cdecl*)(void*);

PollEvent real_poll = nullptr;
GetFlags real_flags = nullptr;
decltype(&GetForegroundWindow) real_gfw = nullptr;
int g_dropped = 0;

// SDL2 ABI: SDL_Event.type at 0; SDL_WindowEvent.event (Uint8) at offset 12.
constexpr uint32_t SDL_WINDOWEVENT = 0x200;
constexpr uint8_t EV_HIDDEN = 2, EV_MINIMIZED = 6, EV_LEAVE = 11, EV_FOCUS_LOST = 13;
constexpr uint32_t WIN_HIDDEN = 0x8, WIN_MINIMIZED = 0x40, WIN_INPUT_FOCUS = 0x200, WIN_MOUSE_FOCUS = 0x400;

bool drop(const void* ev) {
  auto p = static_cast<const uint8_t*>(ev);
  uint32_t type;
  memcpy(&type, p, 4);
  if (type != SDL_WINDOWEVENT) return false;
  uint8_t e = p[12];
  return e == EV_HIDDEN || e == EV_MINIMIZED || e == EV_LEAVE || e == EV_FOCUS_LOST;
}

int __cdecl hk_poll(void* ev) {
  for (;;) {
    int r = real_poll(ev);
    if (!r || !ev || !drop(ev)) return r;
    if (++g_dropped <= 20) bg3::log("[bg] dropped SDL window event %u (focus/minimise/hide)", unsigned(static_cast<uint8_t*>(ev)[12]));
  }
}

uint32_t __cdecl hk_flags(void* w) {
  uint32_t f = real_flags(w);
  return (f | WIN_INPUT_FOCUS | WIN_MOUSE_FOCUS) & ~(WIN_HIDDEN | WIN_MINIMIZED);
}

void __cdecl hk_nop_win(void*) {}
BOOL WINAPI hk_sfw(HWND) { return TRUE; }

// The game's own top-level window: the largest visible one of this process.
HWND game_window() {
  struct Find { DWORD pid; HWND best; LONG area; } f{GetCurrentProcessId(), nullptr, 0};
  EnumWindows([](HWND h, LPARAM lp) -> BOOL {
    auto& f = *reinterpret_cast<Find*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT r;
    if (pid == f.pid && IsWindowVisible(h) && GetWindowRect(h, &r)) {
      LONG a = (r.right - r.left) * (r.bottom - r.top);
      if (a > f.area) { f.area = a; f.best = h; }
    }
    return TRUE;
  }, reinterpret_cast<LPARAM>(&f));
  return f.best;
}

HWND WINAPI hk_gfw() {
  HWND h = game_window();
  return h ? h : real_gfw();
}

void patch(HMODULE mod, bool foreground_only = false) {
  auto base = reinterpret_cast<uint8_t*>(mod);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return;
  for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
    if (!imp->OriginalFirstThunk || !imp->FirstThunk) continue;
    auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
    auto iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++iat) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      auto n = reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
      void* hook = nullptr;
      if (foreground_only) {
        if (strcmp(n, "GetForegroundWindow")) continue;
        if (!real_gfw) real_gfw = reinterpret_cast<decltype(real_gfw)>(iat->u1.Function);
        hook = reinterpret_cast<void*>(&hk_gfw);
      } else if (!strcmp(n, "SDL_PollEvent")) { real_poll = reinterpret_cast<PollEvent>(iat->u1.Function); hook = reinterpret_cast<void*>(&hk_poll); }
      else if (!strcmp(n, "SDL_GetWindowFlags")) { real_flags = reinterpret_cast<GetFlags>(iat->u1.Function); hook = reinterpret_cast<void*>(&hk_flags); }
      else if (!strcmp(n, "SDL_RaiseWindow") || !strcmp(n, "SDL_MinimizeWindow")) hook = reinterpret_cast<void*>(&hk_nop_win);
      else if (!strcmp(n, "SetForegroundWindow")) hook = reinterpret_cast<void*>(&hk_sfw);
      else if (!strcmp(n, "GetForegroundWindow")) { real_gfw = reinterpret_cast<decltype(real_gfw)>(iat->u1.Function); hook = reinterpret_cast<void*>(&hk_gfw); }
      if (!hook) continue;
      DWORD old = 0;
      VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
      iat->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
      VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
    }
  }
}

struct Init {
  Init() {
    char v[8];
    if (!GetEnvironmentVariableA("BG3RTX_BACKGROUND", v, sizeof v) || v[0] != '1') return;
    patch(GetModuleHandleW(nullptr));
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
      std::set<HMODULE> done;
      for (;;) {
        HMODULE mods[1024];
        DWORD need = 0;
        if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) {
          for (DWORD i = 0; i < need / sizeof(HMODULE); ++i) {
            char nm[MAX_PATH] = {};
            K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], nm, MAX_PATH);
            if ((_strnicmp(nm, "sl.", 3) && _strnicmp(nm, "nvngx_dlssg", 11)) || done.count(mods[i])) continue;
            done.insert(mods[i]);
            patch(mods[i], true);
            bg3::log("[bg] focus check patched in %s", nm);
          }
        }
        Sleep(2000);
      }
    }, nullptr, 0, nullptr);
  }
} g_init;

}  // namespace
