## Scene wait: trace frames wait 4x the cap; the 0-timeout gate stays (branch scene-wait-trace, 2026-11-01)

- smoke_ultra 23:48 (defaults on): 13 timeouts in all, all in "prepass recording". 12 are before the plan's first
  window (23:49:05 and 23:49:26: the load, shader prefetch, first alpha overflow, mesh pool growth, 16 skipped
  frames), which no gated window judges. In the plan (7 windows, ~140 s) exactly 1: static-b's trace frame (trace
  recording 23:51:24.161, wait max 13.1 ms). static-a's trace frame waited 16.4 ms and just made it. Pans and
  rotations 0; mesh pool growth to 1024/2047 MB at 23:50:05-06 cost no timeout.
- Decision: no relaxed bound. The only in-plan miss is the test's own trace frame (the tracer records all 55 passes),
  so traced frames now wait up to 4x rt_scene_wait_ms (48 ms; one-off hitch frames anyway) and the trace PNG shows
  its own frame's scene. Logged: "[rt] scene wait: N traced frame(s) ... waited up to 48 ms". Gate stays 0.
- 95/95 CTests pass. Check: rerun smoke_ultra; expect 0 timeouts in every plan window.
