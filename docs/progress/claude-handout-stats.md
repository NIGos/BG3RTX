## Scene wait and handout lines start at the stats reset (branch claude/handout-stats, offline verified, 2026-11-21)

- Symptom: final-s8-ultra-high 050126, window b2-rel2-smoke-high-s8--static-a failed "no OMM downgrade". Its line at
  05:03:43.105 ("60 frames ... OMM accelerated commits 51, downgraded 9") came 0.26 s after the reset token=4 at
  05:03:42.844, so its 60 frames were mostly the warm-up after the preset change.
- Cause (`layer/rtscene.cpp`): only `wait_for_frame_scene` reset the wait counters on the token, and the handout
  counters never reset. Under rt_scene_handout=2 the frames are counted by `rt_scene_handout`, which never checked it.
- Fix: `wait_stats_window()` (under g_wait_mu, then g_handout_mu) runs at both counting sites. It resets the wait
  counters, the handout counters, OMM accelerated and downgraded, the hit-material queries and the stamp downgrades.
  Stats only: no image change, no key, no ABI change.
- Downgrades: one is counted only for an accelerated commit whose ticket a user recording could not take (it ended
  before the commit). Pending micro-maps commit the reference scene and count in neither counter. So a downgrade
  inside a window is still counted and still fails the gate.
- Test: `tests/gpu_logging_gate_test.py` checks the helper, the two counting sites and the lock order.
