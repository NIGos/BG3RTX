## Release logging (branch quiet-log, offline verification, 2026-11-04)

- Periodic `log_stats` covers emissive/census, OMM, alpha/hair, cascade, geometry/BVH,
  bridge motion, local lights, water/sun and GPU/pacing/watch reports (77 report call sites).
  Statistics windows still reset while quiet, including side-effecting counters.
- Startup, configuration, state changes, warnings/errors and device-fault reports remain visible.
  Routine VRAM usage is limited to 1 line/minute; pressure/refusal reports remain visible.
- Original user baseline: about 1.7 MB / 7 minutes = 0.243 MB/minute. Exact reduction is
  unavailable: installed log contents were inaccessible. Quiet-state cadence is an offline
  prediction (about 1 VRAM summary/minute plus events), not a measured game result.
- Previous verification: `scripts/build.ps1 -Test` 172/172 pass (186 s tests, 8 s build);
  `-ReleaseLayer` PASS, diagnostics OFF (23 s). Shader cache needed a local snapshot.

## Detailed logging switch (branch quiet-log, offline verification, 2026-11-04)

- Added player key `detailed_logging`, label **Detailed logging**, exact bug-report help text,
  Off in every preset. Both launcher UIs show it beside Report a problem in Help & support;
  it needs a game restart and does not mark the rendering preset Custom.
- The setting is latched at startup. It joins `dev`, `rt_stats`, `ls_stats` and positive
  `rt_stats_period_frames` in the statistics gate; SDK info also accepts Detailed logging.
- Log-only per-frame Vulkan GPU timestamp recording and readback now use the same gate
  across NGX/RR, RR post, FG input copy, GI/RT/local shadows, SHaRC, water, hair and capture.
  No feature depends on those timestamps; functional transform-feedback queries remain.
  Per-allocation fault-range lines are gated; ranges remain in memory for fault reports.
- Startup records `[cfg] detailed logging: 0/1`. Both report dialogs inspect that last-session
  marker and suggest enabling logging, restarting and reproducing when it was Off.
- Updated USER_SETTINGS, packaged README and Nexus reporting steps. Defaults/schema generated.
  No ABI or shader changes; `shaders/variants.txt` request delta 0. No live GPU cost claimed.
- All 19 active/final live jobs explicitly request `detailed_logging=1` at startup, with generator
  coverage and original cadence/gates retained. Frozen lint: active 17/17, final 2/2 pass;
  historical evidence limitations remain. Batch lint: 10 tests pass.
  Readiness matching ignores only the logging key because it cannot build pipelines.
- Follow-up `scripts/build.ps1 -Test`: 173/173 PASS (184.22 s tests, 0 s incremental build;
  initial recompilation 40 s). Settings/schema and launcher UI pass; local
  `BG3RTX_SHADER_CACHE` snapshot used as in the previous verification.
- Check when separately authorized: Help & support > Detailed logging On, save, restart,
  reproduce, then Report a problem. Off keeps quiet play; On restores diagnostic cost lines.
  No game launched and no commit made.
