// Pins the rt_scene_wait=2 decision and statistics (common/scene_wait.h): the RT passes of
// frame N skip the CPU wait only when the previous frame's camera-relative scene was built with
// exactly frame N's camera; every other case keeps the rt_scene_wait=1 wait.
#include "../common/scene_wait.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace bg3::scene_wait;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

static void pose(float* p, float yaw, float x) {
  for (int i = 0; i < 56; ++i) p[i] = 0.f;
  p[0] = std::cos(yaw); p[2] = std::sin(yaw); p[5] = 1.f; p[8] = -std::sin(yaw); p[10] = std::cos(yaw);
  p[52] = x; p[53] = 10.f; p[54] = -3.f;
}

static void test_decide() {
  float a[56], b[56];
  pose(a, 0.3f, 100.f);
  pose(b, 0.3f, 100.f);
  Inputs in;
  in.scene_used = true; in.now = 50; in.scene_pose = a; in.lighting_pose = b;

  // Frame N's scene already committed (built == now, or a later stamp): no wait in any mode.
  in.built = 50; CHECK(decide(in) == Decision::current);
  in.built = 51; CHECK(decide(in) == Decision::current);
  // Previous frame's scene, bit-equal camera: trace it without waiting.
  in.built = 49; CHECK(decide(in) == Decision::no_wait);
  CHECK(!waits(Decision::no_wait) && !waits(Decision::current));
  // Projection jitter and upload-ring words are not the pose: still no wait.
  b[32] = 0.001f; b[40] = 7.f; CHECK(decide(in) == Decision::no_wait);
  // One ulp of rotation or origin: the camera moved, wait as mode 1.
  pose(b, 0.3f, 100.f); b[0] = std::nextafter(b[0], 2.f); CHECK(decide(in) == Decision::wait_moved);
  pose(b, 0.3f, 100.f); b[54] = std::nextafter(b[54], 0.f); CHECK(decide(in) == Decision::wait_moved);
  pose(b, 0.31f, 100.f); CHECK(decide(in) == Decision::wait_moved);
  // Non-finite poses never match.
  pose(b, 0.3f, 100.f); a[52] = NAN; b[52] = NAN; CHECK(decide(in) == Decision::wait_moved);
  pose(a, 0.3f, 100.f); pose(b, 0.3f, 100.f);
  // Missing cameras: wait.
  in.lighting_pose = nullptr; CHECK(decide(in) == Decision::wait_no_pose);
  in.lighting_pose = b; in.scene_pose = nullptr; CHECK(decide(in) == Decision::wait_no_pose);
  in.scene_pose = a;
  // Older than the previous frame, or no scene: wait (mode 1 handles the pause).
  in.built = 48; CHECK(decide(in) == Decision::wait_stale);
  in.built = 0; in.now = 0; in.scene_used = false; CHECK(decide(in) == Decision::wait_stale);
  in.scene_used = true; in.now = 50; in.built = 49;
  // The current-cascade sun needs this frame's native epoch: always wait.
  in.sun_current = true; CHECK(decide(in) == Decision::wait_sun);
  in.built = 50; CHECK(decide(in) == Decision::current);   // but not when already current
  CHECK(waits(Decision::wait_sun) && waits(Decision::wait_moved) && waits(Decision::wait_no_pose) &&
        waits(Decision::wait_stale));
}

static void test_stats() {
  Stats s;
  CHECK(s.mean_fallback_ms() == 0 && s.saved_ms() == 0);
  for (int i = 0; i < 100; ++i) s.add(Decision::no_wait);
  s.add(Decision::wait_moved, {true, true, false, 4.0});
  s.add(Decision::wait_moved, {true, false, false, 12.0});
  s.add(Decision::wait_stale, {false, false, true, 0.0});
  s.add(Decision::wait_no_pose, {true, true, false, 2.0});
  s.add(Decision::wait_sun, {true, true, false, 6.0});
  s.add(Decision::current);
  CHECK(s.frames == 106 && s.no_wait == 100 && s.current == 1);
  CHECK(s.moved == 2 && s.stale == 1 && s.no_pose == 1 && s.sun == 1);
  CHECK(s.waited == 4 && s.got == 3 && s.timeouts == 1 && s.skipped == 1);
  CHECK(s.wait_ms == 24.0 && s.wait_max == 12.0);
  CHECK(s.mean_fallback_ms() == 6.0 && s.saved_ms() == 600.0);
  CHECK(!s.due());
  char line[512];
  CHECK(s.format(line, sizeof line) > 0);
  const std::string l = line;
  CHECK(l.rfind("[rt] scene wait mode 2: 106 frames, 100 no wait (same camera), 5 fallback "
                "(2 moved, 1 no camera, 1 stale, 1 sun), 1 already current; fallback waits 4 "
                "(3 got, 1 timeouts, 1 skipped), wait 0.226 ms/frame, max 12.000 ms; "
                "CPU saved est 5.660 ms/frame (mean fallback wait 6.000 ms)", 0) == 0);
  // The window resets; the mean fallback wait (the estimate's base) carries over.
  s.reset_window();
  CHECK(s.frames == 0 && s.no_wait == 0 && s.waited == 0 && s.wait_max == 0);
  CHECK(s.mean_fallback_ms() == 6.0);
  s.add(Decision::no_wait);
  CHECK(s.saved_ms() == 6.0);
  for (uint64_t i = 1; i < Stats::kWindow; ++i) s.add(Decision::current);
  CHECK(s.due());
}

int main() {
  test_decide();
  test_stats();
  std::printf("scene_wait_test: %u checks passed\n", checks);
  return 0;
}
