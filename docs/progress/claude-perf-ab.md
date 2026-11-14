## Performance A/B batch staged; release profile 2026-11-05 vs 2026-11-14 (branch claude/perf-ab, READY_FOR_LIVE, 2026-11-14)

- The user reports a large frame-rate loss in play with Ultra, FG 3x, RTX Hair On, distant detail 1/4,
  `rt_hair_mv_card=2`, `rr_hair_strand_aa=2`, sometimes DLSS 5 NR. Staged, not queued:
  `tools/live_batches_staged/perf_ab/` (generator, 5 jobs and plans, `predicted.json`, `analyze.py`, README).
- Release profile rerun today (`opt-profile-*.1.13*.log`, static windows, GPU ms) against the 2026-11-05
  "after" runs. The base path is not slower, except scene build at S8 and at the inn on High.

| Scene | Preset | Scene build | GI | Local shadows | Reflections | RR |
|---|---|---|---|---|---|---|
| Water Queens House | Ultra | 1.18 -> 1.84 | 12.10 -> 8.20 | 0.59 -> 0.79 | 1.10 -> 1.10 | 2.52 -> 2.42 |
| Water Queens House | High | 1.27 -> 1.69 | 2.24 -> 1.82 | 0.12 -> 0.17 | 0.44 -> 0.40 | 2.48 -> 2.80 |
| Inn | Ultra | 2.49 -> 2.59 | 21.18 -> 15.99 | 2.00 -> 1.97 | 2.11 -> 1.98 | 2.54 -> 2.76 |
| Inn | High | 2.14 -> 2.66 | 3.07 -> 2.66 | 0.87 -> 0.86 | 0.58 -> 0.56 | 2.57 -> 2.74 |
| Dialogue1 | Ultra | 1.76 -> 1.69 | 14.12 -> 10.94 | 1.25 -> 1.22 | 1.18 -> 1.05 | 2.54 -> 2.44 |
| Dialogue1 | High | 1.68 -> 1.70 | 2.80 -> 2.63 | 0.83 -> 0.77 | 0.36 -> 0.34 | 2.43 -> 2.51 |

- Camp could not run: its save "Campsite - 27h" no longer exists. Single static windows from separate runs:
  under about 0.3 ms per pass is noise. Both runs were unfocused at the 30 fps cap with stock hair and FG 2x,
  so they say nothing about frame time or about the user's hair configuration.
- Today's logs show the hold path copying a lot per 120 builds in the static windows: S8 Ultra hold pack 0 jobs
  and copy 29640 jobs (10.9 GB), S8 High pack 1200 and copy 38764 (14.7 GB), inn Ultra pack 120 and copy 63985
  (17.1 GB). No 2026-11-05 log is left to compare; job C measures it against the split off.
- Jobs A and B (`perf-ab-s8`, `perf-ab-inn`): one focused, uncapped launch each (`fps_cap=0`: the 58 fps cap
  would hide costs), static camera, 6 windows of 60 settle + 360 frames with a stats reset: stock cards / RTX
  Hair On / + distant detail 1/4 / without shadow and AO transmittance / strands in lighting only
  (`rt_hair_groom_primary=0`) / stock cards again (noise floor). All toggles are live because the game starts
  with the bundle, `rt_hair_mv_card=2` and the nearest trace; the origin reservation and the 1049 velocity
  pipelines stay in the off windows.
- Skipped: the bundle with `rt_hair_groom=0`. The groom key gates the strand swaps (`layer/util.cpp`), so that
  window would show cards. The lighting-only window is its valid complement.
- Job C (`perf-ab-wide-split-0`, `-1`): the opt-profile-s8 state without pans, `rt_capture_wide_split` 0 and 1
  (startup key, two launches): scene build, `[bvh]` hold pack and copy jobs, mesh pool MB.
- Job D (`perf-ab-dlss5-s8`, optional): `dlss5_nr=1` at start, `dlss5_nr_bypass` 1 / 0 / 1; valid only if the
  launcher has already staged `sl/nvngx_dlssnr.dll` for the test tree, else it fails fast on `NR created`.
- Analyzer: `<log>.perf.md`, one table per job (median and range of every GPU cost line, scene wait, `[pace]`
  median and p99, CPU submit, VRAM, guard lines, off-b minus off-a as noise). Checked on today's logs.
- Gates: live_flow preflight (`--tree .` and `--frozen`) 5 jobs, 0 failing; 23 readiness queries appended to
  the lint snapshot (append only); batch_jobs_lint, live_flow, opt_profile and test_registration tests pass.
- Expected time after load: s8 135 s, inn 252 s by the lint model (about 125 s at 25 fps), wide split 47 s
  each, dlss5 74 s. No code change, no live run. After the user's go: read each `<log>.perf.md` against `predicted.json`.
