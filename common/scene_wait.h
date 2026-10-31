#pragma once
// rt_scene_wait=2 (docs/TRACK_D_RT.md, "rt_scene_wait"): when the first RT query of frame N may
// skip the CPU wait for frame N's scene build, and the per-300-frame statistics of that choice.
//
// Why a GPU-side dependency alone cannot replace the wait: the scene build and the RT passes
// already execute in GPU order (one vkQueueSubmit, G-buffer command buffer first). What the wait
// orders is CPU recording: the RT passes bind the scene slot's TLAS and set 1 (InstInfo, records,
// soup, alpha views) when they are recorded, and frame N's slot, TLAS and set are chosen and
// written only when the G-buffer pass ends on another thread, 1-5 ms later (or never: skipped
// or dropped builds). A semaphore cannot change handles that are already recorded.
//
// The safe partial: the scene is camera-relative, so the previous frame's scene is in the same
// space as frame N exactly when the lighting camera of frame N equals the camera that scene was
// built with (bit-equal view rotation and origin, common/camera_epoch.h). Then the RT passes
// trace it without waiting; any other case waits exactly as rt_scene_wait=1.
#include "camera_epoch.h"

#include <cstdint>
#include <cstdio>

namespace bg3::scene_wait {

enum class Decision : uint8_t {
  current,       // frame N's scene is already committed: nothing to wait for
  no_wait,       // previous frame's scene, same camera as frame N: trace it without waiting
  wait_moved,    // the camera moved since the scene was built: mode 1 wait
  wait_no_pose,  // a camera (lighting or scene) is unreadable: mode 1 wait
  wait_stale,    // no scene, or older than the previous frame: mode 1 wait (and its pause logic)
  wait_sun,      // rt_sun_cascade_current needs this frame's native epoch: mode 1 wait
};

struct Inputs {
  bool scene_used = false;
  uint64_t built = 0, now = 0;
  const float* scene_pose = nullptr;     // 56 floats of the scene's camera UBO, or null
  const float* lighting_pose = nullptr;  // 56 floats of frame N's lighting camera UBO, or null
  bool sun_current = false;              // rt_sun_cascade_current is on
};

inline Decision decide(const Inputs& in) {
  if (in.scene_used && in.built >= in.now) return Decision::current;
  if (!in.scene_used || in.built + 1 != in.now) return Decision::wait_stale;
  if (in.sun_current) return Decision::wait_sun;
  if (!in.scene_pose || !in.lighting_pose) return Decision::wait_no_pose;
  return same_camera_pose(in.scene_pose, in.lighting_pose) ? Decision::no_wait : Decision::wait_moved;
}

inline bool waits(Decision d) { return d != Decision::current && d != Decision::no_wait; }

// One fallback wait as wait_for_frame_scene ran it.
struct WaitOutcome { bool waited = false, got = false, skipped = false; double ms = 0; };

struct Stats {
  uint64_t frames = 0, current = 0, no_wait = 0, moved = 0, no_pose = 0, stale = 0, sun = 0;
  uint64_t waited = 0, got = 0, timeouts = 0, skipped = 0;
  double wait_ms = 0, wait_max = 0;
  // Every fallback wait since start: the mean a no-wait frame would have waited (estimate).
  uint64_t all_waits = 0;
  double all_wait_ms = 0;

  static constexpr uint64_t kWindow = 300;

  void add(Decision d, const WaitOutcome& w = {}) {
    ++frames;
    switch (d) {
      case Decision::current: ++current; return;
      case Decision::no_wait: ++no_wait; return;
      case Decision::wait_moved: ++moved; break;
      case Decision::wait_no_pose: ++no_pose; break;
      case Decision::wait_stale: ++stale; break;
      case Decision::wait_sun: ++sun; break;
    }
    if (w.skipped) ++skipped;
    if (!w.waited) return;
    ++waited;
    ++(w.got ? got : timeouts);
    wait_ms += w.ms;
    if (w.ms > wait_max) wait_max = w.ms;
    ++all_waits;
    all_wait_ms += w.ms;
  }
  double mean_fallback_ms() const { return all_waits ? all_wait_ms / double(all_waits) : 0.0; }
  // CPU time the no-wait frames of this window did not block, estimated at the mean fallback wait.
  double saved_ms() const { return double(no_wait) * mean_fallback_ms(); }
  bool due() const { return frames >= kWindow; }
  // The live batch greps this line ("[rt] scene wait mode 2:").
  int format(char* out, size_t n) const {
    const double f = frames ? double(frames) : 1.0;
    return std::snprintf(out, n,
        "[rt] scene wait mode 2: %llu frames, %llu no wait (same camera), %llu fallback "
        "(%llu moved, %llu no camera, %llu stale, %llu sun), %llu already current; fallback waits %llu "
        "(%llu got, %llu timeouts, %llu skipped), wait %.3f ms/frame, max %.3f ms; "
        "CPU saved est %.3f ms/frame (mean fallback wait %.3f ms)",
        (unsigned long long)frames, (unsigned long long)no_wait,
        (unsigned long long)(moved + no_pose + stale + sun), (unsigned long long)moved,
        (unsigned long long)no_pose, (unsigned long long)stale, (unsigned long long)sun,
        (unsigned long long)current, (unsigned long long)waited, (unsigned long long)got,
        (unsigned long long)timeouts, (unsigned long long)skipped, wait_ms / f, wait_max,
        saved_ms() / f, mean_fallback_ms());
  }
  void reset_window() {
    const uint64_t a = all_waits;
    const double am = all_wait_ms;
    *this = Stats{};
    all_waits = a;
    all_wait_ms = am;
  }
};

}  // namespace bg3::scene_wait
