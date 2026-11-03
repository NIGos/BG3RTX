#include "../common/sun_roof_selection.h"
#include <cmath>
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

int main() {
  SunRoofSelection q;
  float cam[3] = {100, 2, 100};
  CHECK(q.update(1, false, 3, 50, cam, 600, 1));
  CHECK(q.radius == 600 && q.center[0] == 100);
  CHECK(!q.update(1, false, 3, 50, cam, 400, 2));
  CHECK(q.update(1, false, 3, 50, cam, 400, 30));
  cam[0] += 6;
  CHECK(q.update(1, false, 3, 50, cam, 400, 31));

  // A fixed world roof 450 m away falls out of the old 600 -> 400 m query.
  // The whole-level query retains it, including roofs admitted only by padding.
  CHECK(q.update(1, true, 3, 50, cam, 600, 32));
  CHECK(q.center[0] == 0 && q.center[1] == 0 && q.center[2] == 0);
  const float world_radius = q.radius;
  CHECK(std::isfinite(world_radius) && world_radius > 1e30f);
  for (uint64_t frame = 33; frame < 633; ++frame) {
    cam[0] = 106 + float(frame % 8) * 0.0001f;
    cam[1] = 2 + std::sin(float(frame)) * 0.01f;
    // Periodic refresh, changing farthest visible pixel and even a camera cut
    // must not change which roofs are uploaded from this level.
    cam[2] = frame > 300 ? 1000.0f : 100.0f;
    CHECK(!q.update(1, true, 3, 50, cam, frame % 2 ? 600.0f : 400.0f, frame));
    CHECK(q.radius == world_radius && q.center[0] == 0 && q.center[2] == 0);
  }
  CHECK(q.update(2, true, 3, 50, cam, 400, 633));   // level load
  CHECK(q.update(2, true, 4, 50, cam, 400, 634));   // live padding
  CHECK(q.update(2, true, 4, 51, cam, 400, 635));
  q.valid = false;   // roofs disabled or camera temporarily unavailable: list cleared
  CHECK(q.update(2, true, 4, 51, cam, 400, 636));   // immediate restoration
  CHECK(q.update(2, false, 4, 51, cam, 400, 637));  // A/B back to old query
  CHECK(q.center[2] == cam[2] && q.radius == 400);
  CHECK(!q.update(2, false, 4, 51, cam, 400, 638));
  q = SunRoofSelection{};   // core hot reload/release
  CHECK(q.update(2, true, 4, 51, cam, 400, 639));
  std::printf("sun roof selection: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
