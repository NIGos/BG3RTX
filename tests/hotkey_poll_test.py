"""Exercise the production hotkey poll with concurrent present callers, without a game."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HotkeyPollTest(unittest.TestCase):
    def _run(self, scenario):
        text = (ROOT / 'layer/util.cpp').read_text(encoding='utf-8')
        start = text.index('static bool ab_poll_hotkey() {')
        end = text.index('\nstatic bool cfg_read_files();', start)
        source = FIXTURE + text[start:end] + MAIN
        compiler = shutil.which('cl.exe')
        self.assertIsNotNone(compiler, 'run through scripts/build.ps1 -Test (MSVC environment)')
        with tempfile.TemporaryDirectory(prefix='hotkey-poll-') as temp:
            path = Path(temp)
            (path / 'test.cpp').write_text(source, encoding='utf-8')
            result = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', '/MT',
                                     '/I' + str(ROOT), 'test.cpp', '/Fe:test.exe'],
                                    cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / 'test.exe'), scenario], capture_output=True,
                                    text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_concurrent_presents_consume_one_nr_edge(self):
        self._run('nr')

    def test_concurrent_presents_consume_one_rtx_hair_edge(self):
        # hotkey_rtx_hair (Ctrl+Shift+F7): one held key on two queues flips rt_hair once, logs one line; the next press
        # flips it back. The override starts from the file value (rt_hair=1: the first press turns RTX Hair off).
        self._run('hair')


MAIN = r'''
int main(int argc, char** argv) {
  const bool hair = argc > 1 && std::strcmp(argv[1], "hair") == 0;
  held_key = hair ? 0x76 : 0x77;
  std::barrier start(3);
  std::atomic<int> changed_polls{0};
  auto present = [&] { start.arrive_and_wait(); if (ab_poll_hotkey()) ++changed_polls; };
  std::thread a(present), b(present);
  start.arrive_and_wait(); a.join(); b.join();
  if (hair) {
    if (hair_toggles != 1 || g_hair_override != 0 || !g_hair_off || changed_polls != 1 || nr_toggles != 0) return 11;
    if (ab_poll_hotkey() || hair_toggles != 1) return 12;          // still held: no second edge
    held = false; if (ab_poll_hotkey()) return 13;
    held = true; if (!ab_poll_hotkey()) return 14;                 // pressed again: on
    return hair_toggles == 2 && g_hair_override == 1 && !g_hair_off ? 0 : 15;
  }
  // The same held key on two queues must produce exactly one bypass transition.
  if (nr_toggles != 1 || g_nr_bypass != 1 || changed_polls != 1 || hair_toggles != 0) return 1;
  if (ab_poll_hotkey() || nr_toggles != 1) return 2;
  held = false; if (ab_poll_hotkey()) return 3;
  held = true; if (!ab_poll_hotkey()) return 4;
  return nr_toggles == 2 && g_nr_bypass == 0 ? 0 : 5;
}
'''

FIXTURE = r'''
#include "common/experimental_ab.h"
#include "common/hotkeys.h"
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
using namespace bg3;
struct LARGE_INTEGER { long long QuadPart = 0; };
constexpr int VK_CONTROL = 0x11, VK_SHIFT = 0x10, VK_MENU = 0x12, VK_F10 = 0x79;
std::atomic<bool> held{true};
std::atomic<int> held_key{0x77};
void QueryPerformanceCounter(LARGE_INTEGER* p) { p->QuadPart = 1; }
void QueryPerformanceFrequency(LARGE_INTEGER* p) { p->QuadPart = 1000000; }
int GetAsyncKeyState(int key) {
  return held && (key == VK_CONTROL || key == VK_SHIFT || key == held_key) ? 0x8000 : 0;
}
uint64_t frame_index() { return 1; }
std::mutex g_cfg_mu;
settings::Map g_cfg;
bool g_nr_startup = true;
hotkeys::Key g_shortcuts[hotkeys::kCount] = {hotkeys::parse(hotkeys::defaults[0]), hotkeys::parse(hotkeys::defaults[1]),
                                             hotkeys::parse(hotkeys::defaults[2])};
std::atomic<bool> g_ab_off{false};
std::atomic<int> g_nr_bypass{-1}, nr_toggles{0}, hair_toggles{0};
std::atomic<int> g_hair_override{-1};
std::atomic<bool> g_hair_off{false};
bool hair_file_on() { return true; }
template<class... T> void log(const char* format, T...) {
  if (std::strcmp(format, "[dlss5] hotkey: NR %s") == 0) ++nr_toggles;
  if (std::strncmp(format, "[hair] hotkey %s: RTX Hair %s", 29) == 0) ++hair_toggles;
}
int cfg_int(const char* key, int fallback) {
  if (std::strcmp(key, "dlss5_nr_bypass") == 0) return g_nr_bypass < 0 ? 0 : int(g_nr_bypass);
  if (std::strcmp(key, "ab_hotkey") != 0) return fallback;
  // This call is reached after a hotkey edge predicate has read its was_*_down. Without the poll lock, let both
  // queues reach that predicate before either consumes the edge. With the lock, the first caller times out and
  // completes.
  static std::mutex mu;
  static std::condition_variable cv;
  static int arrived = 0;
  std::unique_lock<std::mutex> lock(mu);
  if (++arrived == 1) cv.wait_for(lock, std::chrono::milliseconds(300), [&] { return arrived >= 2; });
  else cv.notify_all();
  return 1;
}
'''

if __name__ == '__main__':
    unittest.main()
