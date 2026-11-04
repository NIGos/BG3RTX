## Strand publish reads the build camera without the scene lock (branch claude/hair-camera-lock, READY_FOR_LIVE, 2026-11-04)

- 7cb36371 killed every strand job in the load (exit 0xFFFF7001, no crash log, last line the pixel record):
  hair_primary_publish runs inside the scene build, where rt_after_capture holds rtscene's g_mu, and its new
  rt_build_camera call locked g_mu again (a recursive std::mutex lock ends the process).
- Fix: rt_this_build_camera, set under the lock before bvh_build and read without it. Test: hair_groom_test checks
  hair_primary.cpp never calls the locking rt_build_camera. Live check: hv6 runs past the load.
