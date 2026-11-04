## Reset-aligned frame windows (branch short-windows, offline verification, 2026-11-04)

- Measured comparisons use settling frames plus one complete stats period. FPS only estimates seconds and launch caps.
- rt_stats_period_frames=0 preserves native cadences; prepared jobs request 60. rt_stats_reset is an acknowledged token edge.
- The runner waits for settings/pipelines, settling frames and the exact reset acknowledgement. The first complete period counts; legacy logs retain the straddle skip.
- Pending GPU samples are discarded on reset. Windows wait for completed producer lines and requested trace exports before changing settings.
- Standalone reconstruction settling uses 32 actual frames, or 108 for temporal EMA decay below 1/1024.
- Periodic structural gates need one complete period. Repeated OFF/ON windows and energy capture counts remain; no active noise derivation requires a period above 60.
- No shader or ABI change. shaders/variants.txt request delta: 0. Default/release rendering settings remain unchanged.
- Estimated scheduled seconds (excluding load/readiness): hv7 303.9 -> 44.4; wos1 161.3 -> 131.3; rel1 99.8 -> 89.9. These are cap estimates, not live timings.
- gi1 and ts1 are absent from this checkout's jobs/generators; their duration comparisons need source paths.
- Active lint 31/31 and additional wos1/rel1/rel3 lint 3/3 pass. All six combined dryruns pass.
- Full layer/core build passed; settings 4,736 checks, targeted CTests 6/6, live_flow 66, live_guard 23, queue-gate regressions and camera plan 687 passed. Runner fields validated for all 31 active jobs plus wos1/rel1/rel3.
- Trace-only capture windows retain their requested events and wait for actual exports; FPS estimates cannot advance them early.
- Pool/pak lifetime totals retain snapshot semantics; they do not establish fresh attempts in a reset period.
- No game launched, live evidence collected, Git metadata changed or commit created. Candidate is not live-qualified.
