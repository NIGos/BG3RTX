## Layer hook CPU since 2026-11-05: no base regression; lock-free leave for the strand swap hook (branch claude/cpu-hooks, READY_FOR_LIVE, 2026-11-14)

- Where 12.4 came from: `[cpu] capture hooks ... total` minus `driver draw (XFB)` ("hook bodies"), the OFF windows of
  `opt-cpu1-inn.1.025350` (12.39 / 12.38; kept in `tools/live_batches/opt_r3/cpu2/profile-cpu1.md`). Job: the
  opt-profile inn job, Ultra static, `visible_unfocused_fg` (30 fps), fg=2, dev=0, RTX Hair never started,
  `cpu_timing=1 detailed_logging=1`. The timer sums QPC scopes of every recording thread (not wall time).
  11.7 was never measured: it is 12.4 minus the three separately qualified receipts (0.13 + 0.30 + 0.24).
- The same job today (`opt-profile-inn.1.131746`, main 6f6cef07, 9 static periods): total 12.13 [12.00-12.25],
  hook bodies 11.91. Against the measured 12.39 that is -0.48; against the derived 11.7 it is +0.2, inside what
  summing three receipts with OFF spreads of 0.02 to 0.09 can give. Slots, 2026-11-05 cpu_opt1 OFF -> today:
  pre-draw 8.66 -> 8.57, pass after 2.59 -> 2.27, bookkeeping 0.526 -> 0.503, driver draw 0.292 -> 0.224,
  hit material prepass 1.57 -> 1.39. Draws today 6920 per frame, 3217 captured (not in the kept 2026-11-05 receipts).
- The 13.3-13.8 of `perf-ab-inn.1.135354` is the same timer under other conditions: focused, no cap (34 fps), fg=3,
  dev=1. Every scope is about 10% up, also those no hair code touches (lighting-thread material query 0.89 -> 1.04,
  driver alpha descriptor updates 0.51 -> 0.55 ms/frame), and off-a to off-b drifts 0.43 in the same run. Not a
  like-for-like regression. "CPU dispatch tracking pre" (13.0-13.5) is another quantity: the lighting thread's
  CmdDispatch hook including the core's on_dispatch_pre and its scene wait (8.6-9.0 ms); it must not be added.
- Code diff 2aa5dcf8 -> main of the per-draw paths (`layer/cmd.cpp`, `tracker.cpp`, `geocapture.cpp`,
  `hit_material.cpp`, `alphatest.cpp`, `bvhsplit.cpp` bvh_classify/bvh_captured): with RTX Hair off only stride
  arithmetic on globals (`rt_capture_wide_split`) and one bool (`hair_capture::enabled`); no new lock, map or atomic.
  Started with the bundle and switched off live: `hair_primary_wanted` (cmd.cpp:820) costs one frame_index compare
  and three loads per indexed draw, and is false two frames after the switch; grooms, slot tracking and hair capture
  read 0 through the `rt_hair` gate. Nothing to fix there.
- Found, with RTX Hair on: every indexed game draw (about 6900 per frame at the inn) entered `hair_primary_swap`,
  took the global `g_mu`, did up to six hash lookups, and in D16 depth-only passes also the tracker lock
  (`geo_cascade_layer`), to learn it is not hair. This sits outside every `[cpu]` scope, so no log line shows it.
- Fix (no key, behaviour identical): `hair_primary::DrawFilter` (common/hair_primary.h), two never-cleared bit sets
  written under `g_mu` before a pipeline enters `g_hair_pipes`/`g_variant`/`g_cascade_clone` or a draw key enters
  the published set or the depth latch. `hair_primary_swap` leaves lock-free when the pipeline and the key were never
  added; a set bit only sends the draw to the unchanged locked path.
- Offline: `hair_draw_filter_test` (400000 random steps: the filter never skips a draw the locked rule acts on) and
  its synthetic cost for 7000 non-hair draws (3 runs after 14:18, thread sum; a model, not a game saving): 1 thread
  29 -> 6.3 ns/draw (0.20 -> 0.04 ms/frame); 4 recording threads 164-171 -> 10-11 ns/draw (1.15-1.19 -> 0.07-0.08).
  `scripts/build.ps1 -Test`: 226/226 CTests pass (204 s, run 14:19-14:23, none blocked).
- No offline harness runs the real hook path (replay packs replay GPU passes, the cpu_opt tests are helper
  microbenchmarks), so 2aa5dcf8 vs main cannot be measured offline. The one live that would: the staged perf-ab inn
  job from a test tree at 2aa5dcf8 and at main, RTX Hair never started, same window mode; compare `capture hooks`.
- To confirm this fix live: perf-ab inn, hair on windows, old vs new layer (restart): expect lower `[rt] scene wait`
  and rendered frame time with strands on; `[cpu] capture hooks` must not change (the swap hook is outside it).
  Cost: GPU 0. Shader request delta 0, ABI unchanged, no new setting.
