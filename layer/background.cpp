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
//
// Real input filter (bg_input.h): while the real foreground window is not the
// game, SDL2.dll's PeekMessageW/GetMessageW drop the user's mouse messages and
// WM_MOUSELEAVE (which made SDL jump to the real cursor), and the game's
// SDL_GetGlobalMouseState answers with the runner's posted pointer instead of
// GetCursorPos/GetAsyncKeyState. Mouse messages the runner posts carry
// kPostedMark in wParam and pass with the mark stripped.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <set>
#include <psapi.h>
#include "bg_input.h"
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

// --- Real input filter -------------------------------------------------------
using PeekMsg = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using GetMsg = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using GlobalMouse = uint32_t(__cdecl*)(int*, int*);
PeekMsg real_peek = nullptr;
GetMsg real_getmsg = nullptr;
GlobalMouse real_global_mouse = nullptr;
// The runner's pointer, from the last marked message (game thread writes, any thread reads).
std::atomic<HWND> g_posted_hwnd{nullptr};
std::atomic<uint32_t> g_posted_xy{0}, g_posted_buttons{0};
std::atomic<uint32_t> g_drop_mouse{0}, g_drop_leave{0}, g_passed{0}, g_global_polls{0};
std::atomic<int> g_filter_state{-1};
std::atomic<ULONGLONG> g_next_report{0};
std::atomic<uint32_t> g_reported{0};

// True while the real foreground window belongs to another process.
bool filter_active() {
  HWND fg = GetForegroundWindow();   // this module's import is never patched: the real one
  DWORD pid = 0;
  if (fg) GetWindowThreadProcessId(fg, &pid);
  int on = pid != GetCurrentProcessId() ? 1 : 0;
  if (g_filter_state.exchange(on) != on)
    bg3::log("[bg] input filter %s (foreground pid %lu)", on ? "active: real mouse dropped" : "idle: game is foreground", pid);
  return on != 0;
}

// At most one line per 10 s, only when a counter moved.
void report() {
  ULONGLONG now = GetTickCount64();
  if (now < g_next_report.load()) return;
  g_next_report = now + 10000;
  uint32_t m = g_drop_mouse, l = g_drop_leave, p = g_passed, q = g_global_polls;
  if (g_reported.exchange(m + l + p + q) == m + l + p + q) return;
  bg3::log("[bg] dropped real input: mouse %u leave %u; runner mouse passed %u; global polls answered %u", m, l, p, q);
}

void filter_msg(MSG* m) {
  using namespace bg3::bginput;
  bool candidate = (m->message >= kMouseFirst && m->message <= kMouseLast) || m->message == kMouseLeave || m->message == kMouseHover;
  if (!candidate) return;
  switch (classify(m->message, m->wParam, filter_active())) {
    case Verdict::Pass: return;
    case Verdict::PassStrip:
      m->wParam &= ~WPARAM(kPostedMark);
      ++g_passed;
      if (m->message != WM_MOUSEWHEEL && m->message != WM_MOUSEHWHEEL) {   // wheel lParam is in screen coordinates
        g_posted_hwnd = m->hwnd;
        g_posted_xy = uint32_t(m->lParam);
      }
      g_posted_buttons = sdl_buttons(m->wParam);
      break;
    case Verdict::Drop:
      ++(m->message == kMouseLeave || m->message == kMouseHover ? g_drop_leave : g_drop_mouse);
      m->message = WM_NULL;   // dispatched as a no-op; hwnd and time stay intact
      break;
  }
  report();
}

BOOL WINAPI hk_peek(LPMSG m, HWND h, UINT lo, UINT hi, UINT flags) {
  BOOL r = real_peek(m, h, lo, hi, flags);
  if (r && m && (flags & PM_REMOVE)) filter_msg(m);
  return r;
}

BOOL WINAPI hk_getmsg(LPMSG m, HWND h, UINT lo, UINT hi) {
  BOOL r = real_getmsg(m, h, lo, hi);
  if (r > 0 && m) filter_msg(m);
  return r;
}

HWND game_window();

// SDL_GetGlobalMouseState reads GetCursorPos and GetAsyncKeyState: the user's mouse.
// While filtering, answer with the runner's last posted pointer (or the client centre).
uint32_t __cdecl hk_global_mouse(int* x, int* y) {
  if (!filter_active()) return real_global_mouse(x, y);
  ++g_global_polls;
  HWND h = g_posted_hwnd;
  POINT pt{};
  if (h) {
    uint32_t xy = g_posted_xy;
    pt = {LONG(int16_t(xy & 0xffff)), LONG(int16_t(xy >> 16))};
  } else if ((h = game_window()) != nullptr) {
    RECT r{};
    GetClientRect(h, &r);
    pt = {(r.right - r.left) / 2, (r.bottom - r.top) / 2};
  }
  if (h) ClientToScreen(h, &pt);
  if (x) *x = pt.x;
  if (y) *y = pt.y;
  return h ? g_posted_buttons.load() : 0;
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

enum class Patch { Game, ForegroundOnly, Sdl };

void patch(HMODULE mod, Patch mode = Patch::Game) {
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
      if (mode == Patch::Sdl) {
        if (!strcmp(n, "PeekMessageW")) { real_peek = reinterpret_cast<PeekMsg>(iat->u1.Function); hook = reinterpret_cast<void*>(&hk_peek); }
        else if (!strcmp(n, "GetMessageW")) { real_getmsg = reinterpret_cast<GetMsg>(iat->u1.Function); hook = reinterpret_cast<void*>(&hk_getmsg); }
      } else if (mode == Patch::ForegroundOnly) {
        if (strcmp(n, "GetForegroundWindow")) continue;
        if (!real_gfw) real_gfw = reinterpret_cast<decltype(real_gfw)>(iat->u1.Function);
        hook = reinterpret_cast<void*>(&hk_gfw);
      } else if (!strcmp(n, "SDL_PollEvent")) { real_poll = reinterpret_cast<PollEvent>(iat->u1.Function); hook = reinterpret_cast<void*>(&hk_poll); }
      else if (!strcmp(n, "SDL_GetGlobalMouseState")) { real_global_mouse = reinterpret_cast<GlobalMouse>(iat->u1.Function); hook = reinterpret_cast<void*>(&hk_global_mouse); }
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
    HMODULE sdl = GetModuleHandleW(L"SDL2.dll");
    if (sdl) patch(sdl, Patch::Sdl);
    CreateThread(nullptr, 0, [](LPVOID sdl) -> DWORD {
      // Logged here, outside the loader lock and static initialisation.
      if (sdl)
        bg3::log("[bg] input filter on: while another window is foreground, real mouse messages, WM_MOUSELEAVE and "
                 "SDL_GetGlobalMouseState are replaced; runner mouse messages carry wParam mark 0x%llx (peek %d getmsg %d global %d)",
                 (unsigned long long)bg3::bginput::kPostedMark, real_peek != nullptr, real_getmsg != nullptr, real_global_mouse != nullptr);
      else
        bg3::log("[bg] input filter off: SDL2.dll not loaded");
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
            patch(mods[i], Patch::ForegroundOnly);
            bg3::log("[bg] focus check patched in %s", nm);
          }
        }
        Sleep(2000);
      }
    }, sdl, 0, nullptr);
  }
} g_init;

}  // namespace
