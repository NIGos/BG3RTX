## FG x3 in-game pacing (branch fgx3, READY_FOR_LIVE, 2026-11-06)

- Round 1 commit 4ddec20f fixes state sampling and failed tag/constants readiness.
  Round 2 takes enabled x3 periods as the case, per the user's focus clarification.
- Proven symptom: uninterrupted x3 15:07:12.105-15:07:34.117 has 21.48 rendered
  fps, seven >100 ms gaps and 276 destroy-wait calls. Neighboring enabled x2
  sections give 27.31-33.02 fps. Scene/motion are uncontrolled; no GPU-ms/p99
  or scanout measurements exist in that supplied release session.
- Scanned 220 retained main queue logs (519.9 MB), 1,024 result rows, TRACK
  history and FG evidence. No retained queue x3/x4 marker; historical x3 is
  TV-standby functionality with dropped frames, not smoothness qualification.
- Proven broad retirement policy: old tagged game images wait on the newest
  global input fence. Actual serialization cost is unmeasured. SDK default
  blocks the client's presenting queue until FG GPU processing completes;
  Vulkan FG VSync is unsupported. Both are plausible x3 cost/pacing factors.
- Public Vulkan metering extension/feature enablement was absent despite SDK
  availability. New fg_present_metering=0 is support-checked, restart-required;
  SDK private fallback/cadence impact is unknown. Off in all presets.
- Both candidates require experimental_options=1; metering applies at restart.
- Candidate fg_destroy_wait_exact=0: Advanced, Experimental options, live,
  Off in all presets. Track each game input's actual tagged consumption fence;
  incomplete destruction polls through the existing deferred route, preserving
  view/image/allocation lifetime. Unknown/pending use stays conservative.
- Swapchain boundary invalidates SDK semaphore caches; candidate-only successful
  idle establishes completion there. No per-frame idle or parallelism override.
- Foreground log: executable basename + class only, sampled at 250 ms; no titles.
  Cadence reports exact/global/pending attempts, blocking/polls, wait ms, metering
  batches/feedback and SDK proxy-present/GetState/ReflexSleep CPU timing.
- Full analysis, scene table, requirements and stall bounds:
  [fgx3-analysis](../evidence/fgx3-analysis.md); all retained runs are indexed.
- No wrong x3 generated count, NR-after-hudless stage or Boost necessity proved.
  Camera-cut/reset, real simulation markers and x3 input/pacing QA remain gaps.
- Validation: build.ps1 -Test PASS 206/206 (CTest 138.61 s); settings 4352/4352.
  Shader request delta 0; ABI unchanged. No game, queue writes or push.
- Live 2026-11-06 (fx1/fx2, S8 Ultra + NR 6.5 ms, unfocused run, rendered capped 30 fps,
  rotate windows): x3 exact 3.00, no SDK pauses, p99 35.8-40.3 ms vs x2 36.2-38.2 ms; 0 destroy
  waits in every window, so fg_destroy_wait_exact is unmeasured (kept off). Metering on: proxy
  present CPU max 24-26 ms in 3 windows (off: 0.4-3.8 ms) and 1-7 non-focus pauses in a static
  run: REJECTED. User's x3 stutter not reproduced; focus pauses (launcher) remain the proven cause.
