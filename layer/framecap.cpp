// fps_cap without Streamline: a sleep limiter in the present path.
// With DLSS-G (Streamline active) the cap goes to Reflex instead
// (streamline.cpp, reflex_apply), which paces the game's frame start.
#include "host.h"

namespace bg3 {

static int64_t g_next = 0;   // QPC time the next present may go
static int g_logged_cap = -1;

void framecap_on_present() {
  int cap = cfg_int("fps_cap", 0);
  bool sl = sl_limits_frames();
  if (cap != g_logged_cap) {
    g_logged_cap = cap;
    if (cap > 0) log("[framecap] fps_cap %d: %s", cap, sl ? "Reflex frame limiter" : "sleep limiter in the present path");
    else log("[framecap] off");
  }
  if (cap <= 0 || sl) {
    g_next = 0;
    return;
  }
  if (cap > 1000) cap = 1000;
  static int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const int64_t interval = freq / cap;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  // First frame, or far behind (loading screen, hitch): restart the schedule.
  if (!g_next || now.QuadPart - g_next > interval * 4) {
    g_next = now.QuadPart + interval;
    return;
  }
  int64_t wait = g_next - now.QuadPart;
  // Sleep to ~1 ms before the deadline, then spin.
  if (timer && wait > freq / 1000) {
    LARGE_INTEGER due;
    due.QuadPart = -((wait - freq / 1000) * 10000000 / freq);   // relative, 100 ns units
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 100);
  }
  do {
    QueryPerformanceCounter(&now);
  } while (now.QuadPart < g_next);
  g_next += interval;
}

}  // namespace bg3
