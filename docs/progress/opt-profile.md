## Optimization profiling batch (branch opt-profile, READY_FOR_LIVE, 2026-11-05)

- Prepared four log-only launches: S8, Campsite - 27h, The Blushing Mermaid - 96h 39m,
  Test Dialogue1. Each runs release Ultra then High, detailed logging throughout.
- Queue source folder: `tools/live_batches/opt_profile/jobs/`.
  Exact glob: `tools/live_batches/opt_profile/jobs/opt-profile-*.json` (four jobs).
- Expected durations including 120 s load: S8 3.26, camp 4.54, inn 4.58, Dialogue1 4.70 min;
  total 17.1 min. Hard timeout 300 s each, total cap 20 min. Readiness delays unproven.
- Per preset: static + slow pan, 360 frames each, reset at motion-ready, 120-frame
  cadence, one acknowledged period, at least two complete cost observations required.
  No traces/captures/diagnostics. Mode visible_unfocused_fg. Shader request delta 0.
- Resolved rt_build before readiness comparisons; disabled box/exact-k/temporal/water
  pipeline waits omitted using target state. Regenerated release_qual/final receipts.
- Analyzer emits per-pass Markdown for docs/OPTIMIZATION_20261105.md; shared RR denoise
  explicitly identified, watch CPU/driver totals retain interval units. Drops FG straddle.
- Frozen preflight: 4 profiling + 5 release jobs pass. Existing 79-log snapshot retained;
  15 required readiness queries appended without a full-log cache. 88 tests pass;
  frozen regeneration has no JSON hash drift. CPU timing requires the diagnostics-on test build.
- No runtime costs measured; no speedup/quality claim. Coordinator commits. No launch,
  pending-inbox writes or files over 20 MB. main already ancestor of HEAD; git merge main
  could not update ORIG_HEAD under the workspace permissions, so history is unchanged.
