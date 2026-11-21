# BG3RTX progress - 2026-11-20 (main 08cb1e99)

Live tests: paused by the user on 2026-11-20 ("Non ora"). A new session must ask again, and tell the user right before
launching the runner. Pending live evidence is READY_FOR_LIVE, not a pass.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). Release check: [RC_CHECK](release/RC_CHECK.md).
Full text of the 2026-11-15 and 2026-11-20 entries: [archive 2026-11-20](archive/PROGRESS_CODEX_2026-11-20.md); older:
[2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md), [2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md), [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md).
Installed test app: 0.9.6-hairtest18 (commit 1c09e039). The user played it on 2026-11-20 and found everything fine.

## Overnight run: release profile changes, applied overnight, confirm or revert (coordinator, merged, 2026-11-21)

- `rt_hold_pack_base=1` (fl-s8-holdpack, A/B in one run): copies per static build 91.0 -> 35.6 MB, scene build GPU
  1.044 -> 0.810 ms median, mesh pool used -53 MB, energy equal (ON 0.16323, OFF 0.16316-0.16333).
- `rt_hair_header_early=1` (fl-header-early, two launches): same header address, 519 strand pipelines 15.4 s -> 0.27 s.
- `rt_as_size_class=1` + `rt_cascade_keep_refused=1` (r2-inn): 0 refusals in both states on this PC (benefit unseen here;
  replay 53 -> 2), energy -0.03%, costs within OFF noise, fewer chunks over 64 MB (5-13 vs 4-29).
- `rt_hair_groom_async=1` in the RTX Hair On bundle (27 keys; r4-inn, hx1a): inn outward dolly frame time p99 98-111 ->
  40 ms, scene wait max 163-167 -> 6-7 ms; hx1a 30/30 gates, 0 strands->cards, 0 grooms dropped. Cost: a head decoded
  for the first time shows cards for about 0.6 s (19-frame runs, cause: slot key churn of heads appearing together).
- Release smoke with these keys on: Dialogue1 15/15 gates; S8 Ultra + High 29/29 window gates (final-s8-ultra-high
  055556), post-analysis REVIEW_REQUIRED (visual, checked: correct), costs within 0.1 ms of 2026-11-04 or lower.

## Overnight run: fixes found by the smoke and the rounds (coordinator, merged, 2026-11-21)

- OMM was off in High and Ultra since the scene handout default (2026-11-14): tickets registered on an ended game
  recording retained OMM until device destroy; every handout line said "OMM accelerated commits 0". Fixed
  (claude/omm-diag); live 66/66 lines accelerated. A/B (oa-s8, accelerated vs reference in one run): Ultra GI -0.33 ms,
  RT -0.14..-0.33 ms (noise); High RT +0.14..+0.20 ms and GI +0.13..+0.22 ms; energy +0.07% / +0.02%. Question below.
- Water one-frame "no scene" flips under the handout (7-24% of 60-frame periods at S8): a later pass now takes the newer
  committed scene (claude/water-noscene). Live: 28/29 periods "no scene 0", the other a scene-wait timeout case.
- Groom BLAS full build into compacted storage (memory overwrite with RTX Hair on), status ring UB (claude/r3-layer).
- Groom decode scheduling: only the newest wanted LOD decodes (one wasted decode fewer), no regression.
- Test tooling: smoke gates derived from the exported profile (High has no water/casters), stats waits time out, water
  and handout counters reset with the window, readiness ignores a superseded GI raygen build, energy windows sized from
  the live corpus, ab-on readiness derived, downgrade gate INFO (a downgrade traces the reference scene, correct image).
- New developer keys, off: `rt_mesh_pool_trim_move` (-491 MB at the hx1 hair layout, needs a live), `rt_sun_overlap`,
  `rr_post_jitter_align` (0-3).

## Overnight run: tested and kept off (coordinator, 2026-11-21)

- `gi_far_roof_emission`: not testable offline (1054 lives only in the emission variant; pack writer, replay and PT
  have no emission); live energy +0.25% (noise). Stays off until emission packs exist.
- `rt_sun_overlap`: RT interval 7.53 / 7.50 / 7.32 ms, no gain. `rr_post_jitter_align=1` on High: 2-4x more background
  flicker (premise wrong). Ultra temporal shift (=3): no defect shown.
- High OMM downgrades (about 1 per 60 frames, reference scene, correct image): no pass waits for the commit in High;
  a fix changes the handout protocol. Left for later.

## Water compose "no scene" on handout frames (branch claude/water-noscene, fix, READY_FOR_LIVE, 2026-11-21)

- Symptom: S8 Ultra, one uncomposed water frame (an on/off flip of reflections and shadows) in 4-8 % of the 60-frame
  windows (final-s8-ultra-high .014936 5/23, .033318 4/32, .040347 8/118, .050126 6/25).
- Cause (`core/water.cpp` compose, the handout branch): with rt_scene_handout=2 the lighting stash holds the dummy TLAS
  of frame F. A later pass accepted only the committed scene of F (`water_handout_pick` kCommitted). When frame F+1's
  build had already committed (b >= now), the pass was refused. An ordinary stash takes that newer pair
  (pick_scene kNewer). Log proof: in every clean window with "no scene 1" (or 2), "newer build before this frame" is 59
  (or 58) of 60 passes. Every other pass saw a committed build older than this frame, so the refused one saw b >= now.
- Fix: `WaterPick::kNewer` (`common/scene_handout.h` water_handout_pick + water_handout_newer). The pass takes the newer
  pair with take_newer (rt_scene_set does not wait when b >= now). The shift is off for kNewer, as for an ordinary stash.
  The stash's own committed scene still wins. A stash whose build has not committed yet is still refused (no wait).
  Pure fix, no key, no shader edit, no ABI change. Unit cases in tests/scene_handout_test.cpp; build -Test 239/239.
- New counters at the end of "[water] compose stability": newer scene taken, refused with the stash's build not
  committed yet, other. Rerun: final-s8-ultra-high at S8 Ultra; "no scene 0" and "uncomposed 0" in the static and pan
  windows, "newer scene taken" > 0 where the old run had refusals, no rise in compose GPU ms.

## Readiness: a selected hit-GI raygen pipeline resolves a superseded raygen build (branch claude/rel-gate5, READY_FOR_LIVE, 2026-11-21)
- final-s8-ultra-high.1.040347 timed out at ab-on again with 180 s. Cause (log lines 6320-6330): at ab_off 1 -> 0 the
  first frame has no emission list, so core starts the sun raygen build (GI_TRACE_RAYGEN=1, "the pass waits") and runs
  the trace as compute; 34 ms later it selects the resident emission raygen pipeline ("runs as a ray tracing pipeline,
  emission variant"). The sun build is superseded and never logs "generation", so the runner waited for it forever.
- Fix (runner scripts/camera-plan.ps1 Get-CameraPlanMissing and its offline mirror readiness_budget.estimate): the line
  "gi: gi_rt_pipeline: the hit-GI trace runs as a ray tracing pipeline" (either variant) resolves the pending
  gi_trace_rt_hit_sun[_em].hlsl ...GI_TRACE_RAYGEN=1 builds started before it. Other builds, and raygen builds started
  after the selection, still need their completion.
- The ab-on and High settings-reset allowance stays what the preflight computes (180 s, no VALID median yet).
- Tests: camera_plan_test.ps1 (+4, the exact log lines), live_flow_test (+1), release_qual_test, batch_jobs_lint_test,
  camera_plan_fields_test, hair_reload_test. Lint without and with --frozen: release_final, r3_core, r4_hitch 0 failing.

## Release smoke: readiness waits derived per phase (branch claude/rel-gate4, READY_FOR_LIVE, 2026-11-21)
- final-s8-ultra-high.1.033318 failed at b1-...--ab-on: "pipeline readiness timed out ... gi_trace_rt_hit_sun".
  ab_off 1 -> 0 rebuilds the GI sun raygen pipeline in 11.9-12.6 s in the driver (runs 014227, 01:43, 033318);
  make_final.py gave every non-first readiness wait a fixed 10 s. The Ultra windows before ab-off passed (no
  fail-fast; compose stability uncomposed 0, water traced, 0 scene-wait timeouts).
- make_final.py `readiness_timeouts`: startup 60 s; a phase that changes a key of pipeline_readiness.TABLE (not a
  CONTROL key; ab_off=1 records no RT pass) gets readiness_budget.timeouts' allowance: the learned VALID median plus
  margin, else the unproven pipeline allowance (pipeline_readiness.PIPELINE_TIMEOUT_S, 180 s, the value apply()
  already used); 10 s only where no pipeline key changes. S8: ab-on and High settings-reset 180 s, the rest 10 s;
  watchdog 694 s (12 min, under the 15-minute launch rule); Dialogue1 unchanged (60 s startup, 300 s).
- Lint without and with --frozen: release_final 5, r3_core 2, r4_hitch 1 jobs, 0 failing. Tests: release_qual_test
  (+1: rebuild phases > 12.6 s, ab-off and startup unchanged), live_flow_test, batch_jobs_lint_test. No C++ change.

## Staged jobs pass the runner preflight (branch claude/rel-gate3, READY_FOR_LIVE, 2026-11-21)
- Session combined-e-20261121e refused three jobs at preflight (.scratch/coord/combined_e_runner.log).
- release_final energy-static: the generator sized windows with the frozen snapshot's models; the preflight lints
  with the live VALID corpus. The difference is the fps model, not the stall (live p95 stall 2.79 s, under the
  declared 5.18 s): Dialogue1 p10 fps 10.0 snapshot against 7.1 live, so energy-static was 6.40 s against a 6.46 s
  minimum. The staged generator now sizes with `flow.tighten_files` on the live corpus (the preflight's model).
- With the live S8 model (8.3 fps against 20) final-s8-ultra-high plans 188 s plus a 130 s readiness allowance,
  over the fixed 300 s cap. make_final.py `watchdog` keeps 300 s as the floor and raises it to the preflight's
  own estimate (plan + readiness + 120 s load): 438 s, max_minutes 8; Dialogue1 stays 300 s. The real S8 rate in
  run 014936 was about 30 fps, so this is a watchdog, not the expected duration.
- r3-d3-jitter: forbid `nonfinite` matched the normal "non-finite terms 0 (... kCsNonFinite)" water line. It now
  uses `live_flow.NONFINITE_EVENT` (a count above 0), the pattern repair_known_forbids already used, now one
  shared constant. r3-s8-sun-overlap and r4-inn had no such forbid.
- Lint without --frozen (the preflight): release_final 5 jobs, r3_core 2, r4_hitch 1, all 0 failing; --frozen too.
- Tests: release_qual_test, live_flow_test, batch_jobs_lint_test, build.ps1 -Test.

## Release smoke final-s8-ultra-high: two test defects fixed, staged jobs regenerated (branch claude/rel-gate2, READY_FOR_LIVE, 2026-11-21)
- Run final-s8-ultra-high.1.014936 failed twice for test defects, not render defects.
- High half hang: rel2-smoke-high-s8 is a deepcopy of the Ultra smoke and kept the water reflections, compose
  stability, non-finite-by-water and cascade casters gates. Today's High profile exports rt_water_reflections=0,
  rt_water_compose=0, rt_cascade_casters=0, so those lines never come; live_flow turns gates into wait_stats and
  b2-...--static-a waited until the 300 s job cap. Fix: release_qual/make_jobs.py `PRODUCERS` +
  `drop_unproduced` keep a gate only in windows whose exported state has its producer keys on (core/water.cpp
  water_rf_wanted/compose_wanted, the cascade casters pass); make_final.py applies it to every job before
  `qualify`. High drops exactly those 4 gates; Ultra and Dialogue keep all of theirs.
- Runner: a frame window whose stats lines never come had no timeout after the frames completed (wait_log_timeout
  covers readiness only). It now ends at max(10 s, wait_log_timeout) after the frame target with
  `[stats-wait] timed out phase=.. label=.. missing: ..` and result `stats wait timed out: <label>`
  (scripts/camera-plan.ps1 Test-CameraPlanStatsTimedOut, Get-CameraPlanStatsMissing; auto-run.ps1).
- Ultra fail-fast: the first `[water] compose stability (60 frames)` line of b1-...--static-a counted 114 water
  frames with 1 uncomposed. water_stats_reset (the rt_stats_reset token) reset the compose window but not the
  ComposeContinuity counts, so the line straddled the previous energy-static phase (rt_stats=0, float trace
  stall). Fix in core/water.cpp: the token also resets those counts (continuity kept, so a flip right after the
  reset still counts). Stats only, no image change, no key. The gate is unchanged: a real uncomposed frame inside
  a window still fails.
- Regenerated tools/live_batches_staged/release_final from today's profile (new off-by-default keys in the set);
  `--extend-snapshot` appended 3 readiness queries to lint_corpus_snapshot.json.
- Tests: release_qual_test (+2: drop rule, staged job has no gate or wait_stats without a producer; the second
  fails on the old staged job), live_flow_test, batch_jobs_lint_test, camera_plan_test.ps1 (+9),
  camera_plan_fields_test, hair_reload_test, build.ps1 -Test. Lint `--frozen`: 5 jobs, 0 failing.
- Left: the non-frozen lint reports the energy-static trace schedule on main as well (unchanged by this
  branch); readiness_cached_replay.ps1 needs old logs under Documents (environment). Needs a live go to rerun.

## Ultra temporal post without jitter terms: rr_post_jitter_align=3 (branch claude/r5-ultra-shift, READY_FOR_LIVE, 2026-11-21)

- Question (from r3-d3-jitter, claude-r3-core-live.md): the temporal path reprojects its history by previous - current
  jitter and reads it at uv + jitter (`layer/rrpost.cpp` rrpost_temporal / rrpost_final). On High the same +jitter lookup
  made the blurred background flicker 2-4x, because the game's DoF layers do not carry the jitter there. The code cannot
  settle it: the terms are self-consistent only for jittered A/C/B (1D model in tests/rr_post_temporal_test.cpp: each
  convention is stationary for its own content and moves for the other).
- Switch (layer only, no shader edit): `rr_post_jitter_align=3` sets both temporal terms to 0 (history by the motion alone,
  output at the plain uv; the real jitter is still recorded). Without the temporal pass 3 is the plain final, like 0.
  Developer choice "No jitter shift (check)". Key 0: the same float arithmetic (common/rr_post_temporal.h helpers).
  No ABI change, variants.txt delta 0, cost 0 ms.
- Staged, not queued: `tools/live_batches_staged/r5_ultra_shift` r5-d3-ultra-shift (Dialogue3, Ultra, 0/3/0, K8 bursts,
  presented K8, float energy), `predicted.json`, `analyze_jitter.py`. Lint 0 failing with and without --frozen.

## Inn outward-dolly hitches: synchronous groom LOD decodes (branch claude/r4-hitch, READY_FOR_LIVE, 2026-11-21)
- Cause (queue log r2-inn.1.020820, Ultra, RTX Hair On, `rt_hair_groom_async=0`): all 36 frame gaps of 90 ms to 1 s
  in the 12 dolly windows follow a `[hair groom] <id> LOD n decoded (v3)` line within 33 ms (e.g. 02:10:15.416
  84ef20d4a0aa148b LOD 0 decoded in 157 ms, frame 2914 at 15.446 took 222 ms). The other 12 gaps are energy traces.
  The read and LZMS decode run inside the scene build (`groom_lod`, layer/bvhsplit.cpp), so the frame waits on the
  build: scene wait max 151-186 ms, frame time p99 110-118 ms in the outward windows (static and inward: 37 ms,
  3-10 ms). 6-11 decodes per outward window, 0.5-1.1 s of decode; the same LODs come back in every outward
  window (84ef20d4a0aa148b LOD 0 five times): the groom pool sits at 464-496 of 512 MB and evicts them.
- Not in the game's streaming: every long dolly frame has one of our decodes just before it.
- Fix: the existing `rt_hair_groom_async` (default 0, READY_FOR_LIVE since 2026-11-15, CTest
  `hair_groom_decode_test`) moves the read and decode to a below-normal worker. No new key. Added: the decode line
  says `decoded (v3, worker)` when the worker ran it, so a live proves the state per window; the key is listed as
  host-only in tools/live_batches/pipeline_readiness.py.
- Live staged (not queued): `tools/live_batches_staged/r4_hitch` r4-inn, 0 / 1 / 0 in one inn launch with r2-inn's
  windows, ~8.5 min. Gates: every ON outward decode on the worker, at least one in-build decode per OFF outward
  window. Predicted: ON outward p99 at the inward level (37-50 ms, at most 60), scene wait max under 40 ms, no
  Streamline "Frame rate over 100 ms" lines in ON dollies, energy equal within 0.5%, GPU ms equal.
- Open: the pool keeps evicting LODs the dolly brings back (re-decodes); a budget question, not this fix.

## Layer round 1: groom BLAS full build into compacted storage, mesh pool trim with moves, status ring UB (branch claude/r3-layer, offline verified, 2026-11-21)

- Pure fix, groom BLAS (`layer/bvhsplit.cpp` groom work, `hair_groom::groom_storage` in `common/hair_groom_budget.h`).
  After compaction the storage is the compacted size, but a full build (no refit: the previous build record dropped or
  unsubmitted around loads, `rt_dyn_refit=0`, or the compaction-copy frame itself) was written into it. That breaks
  the "destination at least accelerationStructureSize" rule: writes past the AS into the neighbouring chunk ranges.
  Now: undo the copy in its own frame, or re-create the full size (`g.compact = 0`, compacted again later). New
  counter at the end of the `[hair groom] frame N` line: "full builds into re-grown compacted storage N".
  Expected 0 except after a dropped build record. Unit test in `tests/hair_groom_asset_test.cpp`.
- `rt_mesh_pool_trim_move` (default 0, live, READY_FOR_LIVE). With native hair a single range at the pool's end blocked
  the trim. Now cached meshes (`kReady`) and hold snapshots past the cut move below it in the trim's in-command copy.
  Groom, hair bounds, OMM and pending ranges stay pinned. Meshes move only while `native_hit::pool_compact_freeze`
  holds (as for the repack); otherwise only holds move. The planner is `vram_compact::pool_trim_move_plan`.
  hx1 S8 001645 00:19:54 (1479 MB for 648 used, target 988, "no trim"): the planned cut is 988 MB (-491 MB) when no
  pinned range lies above 1109 MB (75% of 1479). With a pinned groom range at 1303 MB, still no trim (the quarter
  rule): the earlier estimate of -167 MB in that case was wrong. 8 layouts in `tests/vram_compact_test.cpp`.
  The key-off path, its log line included, is unchanged.
- Pure fix: the `status_slots` alias detector read and incremented `aliased` unsequenced (undefined behaviour).
- Not done: pixel-LOD camera BLAS compaction (coordinator: skip).
- Live to confirm (hx1 S8 hair job, `rt_mesh_pool_trim_move=1` from the start):
  - a `trimmed ... ranges moved below the cut` line where the key-off run logs "no trim";
  - `[mem] ours` mesh pool MB down by the trimmed amount;
  - 0 held-draw rejections (`why 2`) and no new hit-material or validation rejects;
  - energy within noise;
  - the re-grown counter is reported (any value is valid; above 0 only after dropped records).
- Cost: one copy of the kept bytes (as a growth) at most once per quiet window (254 frames). GPU ms not measured offline.

## Post jitter alignment without the temporal pass: rr_post_jitter_align (branch claude/r3-core, READY_FOR_LIVE, 2026-11-21)

- Cause: the plain output grade (`shaders/rr_post.hlsl` `final`, Low-High) read the jittered render-resolution A/C/B at
  the unjittered uv; the jitter reached `final` only when `rr_post_temporal` ran (`layer/rrpost.cpp`). The DoF and
  bloom layers then move by the Halton jitter (~1 render px peak to peak) against the stable RR image: DoF-edge shimmer.
- Fix (layer only, no shader edit): `rrpost_temporal` stores the jitter before its key-off return; with
  `rr_post_jitter_align` 1 `rrpost_final` runs the existing `temporal_final` entry (final plus exactly the shift) on the
  plain A/C/B. 2 = opposite sign, the sign check for both paths (the convention was never checked: AUDIT_RT row 9).
  Pipeline not built yet or no jitter: the plain final (logged `[rrpost] jitter align`). Off: same command stream.
- Key: `rr_post_jitter_align` 0 (Advanced, developer knob "Depth of field edge alignment"), live, game restart for the
  layer DLL. Cost 0 ms by construction. No ABI change, variants.txt delta 0, shader identity unchanged (no shader edit).
- Offline: `tests/rr_post_temporal_test.cpp` (jittered ramp, 16 phases: aligned stationary, unaligned moves, wrong sign
  twice as far, mean kept). Live staged: `tools/live_batches_staged/r3_core` r3-d3-jitter (Dialogue3, High, 0/1/0).
- User check: Test Dialogue3 on High, hair and fine edges over a blurred background: with On the edge stops crawling.

## Sun trace beside the GI trace: rt_sun_overlap (branch claude/r3-core, READY_FOR_LIVE, 2026-11-21)

- `core/gi.cpp` run_pre: with the key the shadow mask goes to GENERAL before the GI block, the sun trace is recorded
  right after the GI trace command (inside `hit_gi_dispatch` before the ray tracing path's RT->ALL barrier, which does
  not wait for a compute dispatch; after the dispatch on the other paths), the barrier after the GI trace orders both,
  and the sun resolve runs beside the GI temporal pass. No data dependency (read): the GI reads neither the sun mask nor
  rtVis; the sun passes write only texels 8-47 of the sun-trust image, the GI reads 0-7 and 48+.
- With the key the sun trace has no per-pass timestamps (an ALL_COMMANDS timestamp between the traces would wait) and
  runs inside the GI trace's interval: judge `rt passes: GPU cost`. Key `rt_sun_overlap` 0 (Advanced, developer knob),
  live (core hot reload). Expected gain not derivable offline (bounded by the 0.68 ms sun trace); may be negative.
- Offline replay of both orders: not possible with tools/frame_replay (one dispatch per pack: gi_hit_direct,
  reflections, local shadows; no rt_shadow / rt_sun_resolve, no dispatch sequence). Live staged: r3-s8-sun-overlap
  (S8 Ultra static, 0/1/0, cost only).

## r3 lives: rr_post_jitter_align rejected, rt_sun_overlap no gain (branch claude/r3-core-live, rejected, 2026-11-21)

- r3-d3-jitter (Dialogue3, High, 0/1/0, `.1.034009`, 12/12 gates). Output-grade luma, 8 frames per state. Frame-to-frame
  |diff| x 1000 in the blurred background (A < 0.95), by C gradient: OFF-a / ON / OFF-b 0.76 / 1.59 / 0.79,
  0.89 / 2.52 / 0.95, 2.24 / 4.29 / 2.30. Fully blurred (A < 0.05), C-gradient pixels: 0.48 / 1.86 / 0.56 (x),
  0.49 / 2.08 / 0.48 (y): ON flickers 2-4x more on most of the image. The top 1% gradient bin and DoF transitions are
  lower with ON (11.5 vs 15.9 / 19.4; std 0.0040 vs 0.0050 / 0.0053), but the in-focus control falls just as much
  (0.0114 vs 0.0166 / 0.0195): that is the dialogue animation, not the key.
- Cause of the failed prediction: the premise was wrong. In OFF the output over the blurred background is already
  steady (|diff| 0.48 at C-gradient pixels vs 0.30 flat), and C itself moves little there (1.33 / 1.40 vs flat
  1.16 / 0.71). The game's DoF layers do not carry the full jitter there, so a whole-frame shift of A/C/B adds motion. A single global sign cannot be right; value 2 would add the same motion. The sign is not decided.
- Energy (float trace means, luma): scene before post 0.27016 / 0.26919 / 0.26736, RR output 0.29779 / 0.29829 /
  0.29471, output grade 0.3588 / 0.3584 / 0.3567: ON inside the OFF range.
- Decision: rr_post_jitter_align stays 0 in every preset (no claude/jitter-on). Open question for Ultra: `temporal_final`
  applies the same +jitter lookup (and `temporal` the jitter difference) to these A/C/B. That could add the same
  background motion under its 1/16 history. Not measured here; worth one 0/1 window at static Dialogue3 with the shift
  forced to 0 before trusting the Ultra convention.
- r3-s8-sun-overlap (S8 Ultra, `.1.030240`): rt passes OFF-a 7.53, ON 7.50, OFF-b 7.32 ms (medians of 3 periods). ON lies
  inside the OFF drift (0.21 ms): no gain beyond noise plus floor. The overlap ran: in ON the sun's per-pass line is
  gone and the GI trace median includes it (5.81 / 6.03 / 5.54 ms). Here the sun trace costs only 0.30 ms, so the gain
  is capped at 0.3 ms. The 9.72 / 9.63 / 9.47 ms quoted before are "gi: GPU cost" (whole GI interval), not the RT total.
  rt_sun_overlap stays 0.
- Summaries: `tools/live_batches_staged/r3_core/RESULT_20261121.json`, `analyze_jitter.py`. Raw K8 bursts and presented
  captures deleted (about 520 MB); the three float traces (11 MB) are kept.

## OMM stayed off for the whole session under the scene handout (branch claude/omm-diag, offline verified, 2026-11-21)

- Symptom: final-s8-ultra-high 043643 failed the High OMM check, "compared 0, not compared 269494583". Not a limit of
  the diagnostic: OMM was off in every normal frame of both halves. All 92 `[rt] scene handout` lines say "OMM
  accelerated commits 0", and only the first window has "downgraded 1".
- Cause: at 04:37:07.397, 30 ms after the first 8 micro-map bakes, `[omm] rejected ticket use ... retaining until
  idle`. The handout (rt_scene_handout=2, default in High and Ultra since 2026-11-14) registers the accelerated slot's
  tickets on the game recordings that bound the handed-out set (`handout_tickets`). One of them had already ended
  recording. `omm::register_ticket_use` treats that as a lost provenance and latches `retained`. From then on
  `omm::available_for` is false: no rt_omm_dyn micro-maps, no accelerated TLAS, no proof, so every query traces the
  reference scene. The 2026-11-04 receipt that passed ran before the handout was the default.
- Fix (pure, layer): `omm::try_register_ticket_use` for the handout. An ended or untracked user recording is a plain
  refusal: the caller already downgrades that slot's traversal to the reference, and nothing references the ticket.
  A missing or retiring ticket still retains, as before. Other callers are unchanged.
- Test: `tests/omm_submit_state_test.cpp` checks that an ended or unknown recording refuses the ticket and is not tracked.
- Live to confirm (High S8 smoke): "OMM accelerated commits" above 0 in the normal handout lines, no "[omm] rejected
  ticket use", and the diag-omm window "compared > 0, not compared 0, violations 0".

## OMM live A/B staged, High downgrades explained, release downgrade gate made INFO (branch claude/omm-ab, offline, 2026-11-21)

- Why High downgrades and Ultra does not (final-s8-ultra-high 052606):
  - Ultra has rt_hit_material=1 with rt_scene_handout_hit_wait=1. Its hit-material passes wait for the commit
    (`[core] scene handout: ... N settled to the committed scene`, about all 60 of 60). So the game's lighting
    command buffer is still recording when the commit registers the micro-map tickets.
  - High has rt_hit_material=0: "0 settled" in every High line. The passes run on the handed-out set without waiting,
    and about 1 frame in 60 the buffer has already ended at the commit. Its tickets are declined
    (`try_register_ticket_use`) and the slot traces the reference scene.
  - Not fixed, not small: registering tickets ahead (at the hand-out) or committing earlier changes the handout
    protocol. The downgrade gives a correct image.
- Release gate (`tools/live_batches_staged/release_final/make_jobs.py`): "no OMM downgrade" is gone. Hit-material query
  failures and fallbacks still fail. Downgrades are INFO with their count per window
  (`release_qual/analyze_final.py` omm_downgrades). release_final regenerated from today's profile (MSVC profile.exe);
  frozen and live lint 0 failing. Test: `tests/release_qual_test.py`.
- Staged `tools/live_batches_staged/omm_ab` (READY_FOR_LIVE, not queued), job oa-s8, one S8 launch:
  - release Ultra, then release High through its settings reset; dev=1 (the release policy resets the developer knob);
  - rt_omm_reference 1 / 0 / 1 live (push constant bit 28); rt_omm itself needs a restart;
  - per preset and state the smoke's static, Q/E pans and R/L rotations (one 120-frame period after a 60-frame
    settle), plus 3 float traces with the exposure held;
  - `analyze.py`: RT passes "lower", GI "not higher" against the OFF pair (sane_gates), energy per preset,
    downgrades INFO.
  - 625 s planned plus 260 s readiness allowance; predicted.json first.
  - `pipeline_readiness.py`: rt_omm_reference selects no pipeline.

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

## rt_hair_groom_async in the RTX Hair On bundle (branch claude/hair-async, applied overnight 2026-11-21, confirm or revert)
- Bundle change: `rt_hair_groom_async=1` in the On choice of `exp_hair_groom_primary` (common/settings.h, 27 keys), its
  settings_test list and count, the bg3rtx.ini bundle comment, data/settings_schema.json. The rt_hair live switch gates
  it with the rest of the bundle (layer/util.cpp hair_gated). Code default stays 0; RTX Hair Off is unchanged.
- Evidence: live r4-inn.1.030500 (inn, 0/1/0): outward dolly p99 98-111 -> 40 ms, scene wait max 163-167 -> 6-7 ms,
  every ON decode on the worker, energy within the OFF trace range (firelight, not the key). Its '[hair groom] frame'
  lines: kept on the built LOD while a decode is pending 54-59 groom-frames per 120 frames in the ON outward dollies
  (OFF 0-24), groom dropped 0 in every window.
- Live staged (not queued): `tools/live_batches_staged/hair_async` hx1a-s8-hair-async, hx1's S8 plan and gates with the
  key from the start, about 99 s of phases. Added gates: every decode of the moving windows on the worker, '(groom
  dropped)' 0 in the bundle windows; reported: kept groom-frames, decodes per window, hx1's card runs. hair_stable's
  bundle copy gained the key (its check_bundle reads the schema). Lint with and without --frozen: 0 failing.
- Decision rule (predicted.json): confirm if every hx1 gate holds, no groom is dropped and the card-run median of
  entering heads stays at most 6 frames (202905: 3); else revert the bundle line, the key stays READY_FOR_LIVE.
- Live hx1a-s8-hair-async.1.033523 (S8, 2026-11-21, done, 30 every-window gates ok): CONFIRMED by the predicted rule.
  Bundle windows (in-on, out-on, orbit-r, orbit-l, in-on2, out-on2): 0 strands->cards, 0 frames on cards, watchdog 0;
  card runs of entering heads median 3 in all six (max 3; hx1 202905: medians 3/3/6/3/3/3, max 9). Groom dropped 0 in
  every window; kept on the built LOD 3 (in-on) and 4 (orbit-r) groom-frames, 0 elsewhere. 20 decodes, all on the
  worker (warmup 10, pre-in 8, orbit-r 2). Keep-off control 10/10/10 and 10/10/130 (202905: 1/9/10, 10/10/72).
- Cost seen outside the gated windows: a head whose groom is decoded for the first time stays on cards 18-21 frames
  (pre-in, 3 runs, 30 fps: about 0.6 s) and 18-25 at the load (warmup, 5 runs). Predicted "at most 3 frames more"
  was wrong: the one worker decodes serially, also LODs the dolly no longer wants (16fa158ca4825f93 LOD 0, 2, 1 in
  0.4 s). Without the key that time is a frame freeze instead (r4-inn: 100-220 ms frames). No old pre-in numbers kept.
  Follow-up (not done): drop queued decodes nobody asks for any more, or decode the newest request first.
- Round 3 (layer stream, no key: scheduling only): a queued decode is a wake-up; the worker decodes what
  `DecodeSlots::take` names, the newest request still wanted (asked in the newest frame or the one before), and forgets
  queued requests nobody asks for (common/hair_groom_decode.h, layer/hair_groom.cpp). CTest hair_groom_decode_test
  (44 checks) replays the pre-in sequence: LOD 0, 2, 1 asked while the worker is busy -> only LOD 1 decodes, and before
  an older still-wanted request. Predicted for the hx1a rerun (predicted.json rerun_round3): pre-in card runs median
  12-18 frames (from 19), pre-in decodes 6-8 (from 8), bundle windows unchanged.

## 2026-11-20: release candidate 0.9.6-beta rebuilt from main 08cb1e99

- `dist/BG3RTX-0.9.6-beta.zip` (230 files), `-symbols.zip` and `.scratch/setup-0.9.6-beta/BG3RTX-0.9.6-beta-Setup.exe`
  (unsigned); `VERSION.txt` commit 08cb1e99; `SHA256SUMS.txt` regenerated. Hashes in RC_CHECK.
- verify_rc: 11 PASS, Setup SKIPPED (another install present: 0.9.6-hairtest18). Older candidates in `.scratch/old_rc_745a1fff/`.
- The packaged `bg3rtx.ini` carries the five new keys at 0. Nothing published: publishing is the user's action.

## 2026-11-20: two consolidation rounds (final-* then r2-*, offline, merged)

### Host layer (claude/final-layer, claude/r2-layer)
- Source: the user's log of 16:45-16:50 (hairtest18, Ultra, FG 2x, RTX Hair toggled live): no `VK_ERROR`, peak 27.1 GB used
  (ours 8.7-15.3 GB), 162 BLAS chunk refusals (137 led by the cascade casters), 17.4 s of strand-variant pipeline creation.
- Groom decode fix (on): the pool range is taken before the decode, so a refused growth no longer repeats a 30-190 ms
  decode (two LODs were decoded 9 times). Check: no LOD id with several "decoded" lines before its "uploaded" line.
- Tracker (on): freed sets and the sets of reset or destroyed pools leave the descriptor maps (descriptor_epoch_test);
  `DestroyBuffer` / `DestroyImage` / `DestroyImageView` erase their records before the driver's destroy (a reused
  handle on another thread lost its `buffer_mem` record).
- Logs (always on): `[mem] ours` splits the BLAS chunk reserve; `[bvh] BLAS refusals in N frames` once per 300 frames when there were refusals.
- Cause of the refusals: small ranges settle in the large cascade and groom chunks and pin 100-190 MB each, so a
  cascade rebuild needs a new chunk past the cap and the casters are left out.
- New keys, all 0, live, READY_FOR_LIVE:
  - `rt_as_size_class`: base-size ranges only in base chunks. Replay (8 seeds x 1380 frames): refusals 53 -> 2.
  - `rt_cascade_keep_refused`: on a refusal the previous cascade BLAS is traced (rebased) instead of no casters.
  - `rt_hold_pack_base`: the 24-byte hold pack also for the 64-byte layout. Predicted 91.5 -> 34 MB per static build.
  - `rt_hair_header_early` (startup): the strand header address is stable across launches, so the 519 strand variants
    hit the driver cache. Pipeline creation 14.7-48.5 s at launch -> under 1 s predicted on the second launch.
- Staged jobs: `tools/live_batches_staged/final_layer/` (fl-s8-holdpack, fl-header-early: two launches),
  `tools/live_batches_staged/r2_layer/` (r2-inn: inn dolly, both chunk keys 0/1/0, 8.6 min; at this PC's cap refusals may be 0 in both states).

### Core and shaders (claude/final-core, claude/r2-core)
- Dead code out: `GI_DEBUG_SWIZZLE`, `GI_FAR_M`, the unreachable `kCascade` branch, spec id 1012 (retired). 225 / 225
  requests byte-identical. `RT_SCENE_REBASE` / `RT_REBASE_LIGHT_CAMERA` kept (a hair probe tool compiles them).
- `refresh()` reads its cfg keys once per frame index (`core/frame_memo.h`, CTest `frame_memo_test`). No ABI change.
- giRaw clamp: a NaN now stores 0, overflow and +Inf 65504 (not reachable today; only the 28 hit-GI requests change).
- Skipped unread work (image identical): sun roof-volume test only where the resolve reads it; RF probe term skipped
  at w = 0 (replay -0.034 ms); GI temporal in RR mode writes the fresh sample directly; water_primary / water_reflect
  run only with a reader. Two early-return paths in debug dispatches now restore the game's bindings.
- `gi_far_roof_emission` (spec constant 1054, 0, live): the far roof rule also for a hidden hit's emission beyond the base
  radius. Off = old program (6 / 6 hit_sun_em requests equivalent). Staged: `tools/live_batches_staged/r2_tools/`
  r2-inn-roof-em (inn, key 0/1/0, then a GI replay pack for the PT reference). Predicted: GI energy lower or equal, closer to PT.
- 238 / 238 CTests. `shaders/variants.txt` delta 0 in both rounds. No ABI change (31).

### Launcher, docs and tools (claude/final-tools, claude/r2-tools)
- Launcher: the GPU list no longer closes on every 2 s refresh; a WebView page that fails to load now falls back to the
  native window (was exit 2, no window); a preset change names three moved settings, the rest in a tooltip; the
  preparation text says it runs while the window is open. 21 views x 3 sizes, 0 layout problems (headless Edge).
- verify_rc: an existing install makes the Setup row SKIPPED (exit 2), naming the version from the install's `VERSION.txt`.
- `release_shader_equivalence.py`: a crashed spirv-opt is named (stage, passes, exit code) and retried once; the first
  difference decides a request (74 s instead of a 13 h cross); `--current REV` compares a commit with its parent.
- Python CTests whose cases skip report Skipped. The dlss5 staged job is committed (lint 0 failing). Release notes,
  Nexus texts and RELEASE_EVIDENCE name the groom decode fix and the 15-50 s strand-variant creation at start.

### Known, not fixed (owner or A/B needed)
- Water game shadow at Vg = 0 (`common/water_fs_patch.h:699`): Dg is NaN, the pixel keeps the game colour. No small fix
  (D_u needs the cloud-layer chain rebuilt with Vg = 1); a guard changes no pixel.
- `rr_post_temporal.hlsl:51` rejects most history under camera motion (energy-safe). `rr_post.hlsl:250` (non-temporal
  post, Low-High) lacks the jitter offset. Both image-changing.
- rt_shadow waits for the whole GI chain (`core/gi.cpp:3650-3672`); recording it earlier may save 0.05-0.2 ms (GPU A/B).
- GI ray cone `pa * 2.0` (`gi_trace_rt.hlsl:141, 313`) is one mip too coarse in RR mode (full resolution). Small.
- Validation status ring can wrap onto a waiting batch (detector `[bvh] status ring wrapped` only; never seen).
- The mesh pool cannot shrink while a groom sits at its end. Pixel-LOD camera BLAS not compacted (about 16 MB per groom).
- 282 draws re-captured and refit every frame with a static camera at S8. `stats_enabled()` and ~100 raw cfg lookups
  in run_pre/run_post (about 40 us per frame).

## 2026-11-15: release 0.9.6-beta prepared (claude/release-prep, claude/leftovers-small)
- User decision: finalise for release, RTX Hair Off by default. Settings audit found no inconsistency: 23 controls,
  4 behind "Show experimental options", 32 developer knobs hidden. `test_release_rtx_hair` pins RTX Hair Off in all presets.
- Release notes, evidence and Nexus changelog written; verify_rc fixes (`py -3`, 230 files). Final smoke staged in
  `tools/live_batches_staged/release_final/` (`final-s8-ultra-high`, `final-stock-hair-dialogue1`, about 7 minutes).
- Leftovers: licence files keep their bytes (`.gitattributes`); `tools/spec_omm_alias_probe.py` and
  `tools/spec_constant_proto.py` landed. Cost 0 ms.

## 2026-11-14, night (one line each; full text in [archive 2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md))
- VRAM right-sizing: `rt_capture_demand_size`, `rt_capture_peak_reserve`, `rt_capture_split_mb`, `rt_mesh_pool_trim` On by default after vr-s8 / vr-inn: -0.8 GB at S8, -1.0 GB at the inn, 0 faults, energy within noise. `rt_as_chunk_fit` stays 0.
- Layer consolidation: elastic capture growth (ten 1.4-1.8 GB reallocations -> three); `rt_hair_groom_async` (0, READY_FOR_LIVE); shader scan every 5 s in release; Streamline stale-layout erase.
- Core consolidation: GI images no longer reallocated twice per level load (`core/rr_mode_latch.h`, READY_FOR_LIVE log check); `probe.hlsl` and `rr_hair.hlsl` removed.
- Launcher and tools: refresh sends only changes, slider drag fixed, 58 fps start limit, converter at idle priority; 22 tests added to CTest; live lint counts active jobs only.
- Disk cleanup with the user's approval (about 311 GB, merged branches); the E: free-space reading stayed 482.3 GB (unexplained).

## 2026-11-14, day (one line each; full text in [archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md))
- RTX Hair instability under camera motion solved for converted styles (26-key bundle, hx1 202905: 0 strands->cards). Still Off in every preset.
- Open for hair: styles without a groom file stay cards; hitch frames fixed offline only; `rt_hair_card_fill` Advanced with no visible gain.
- Scene handout ("Low-latency scene") On in High and Ultra with the hit wait: scene wait 3.5 -> 0.3 ms (S8), 8.5 -> 2.9 ms (inn).
- Sensible gates (`tools/live_batches/sane_gates.py`): FAIL only beyond the OFF noise plus a floor; an unpredicted drop is EXPLAIN.
- Performance: release profile not slower than 2026-11-05; VRAM guard LOW latch fixed; launcher "Direction A" merged.

## Earlier hair notes (2026-11-05 to 2026-11-11)
- Motion stability On in High and Ultra (`rt_hair_mv_card=2`). Pixel LOD (Off / 1/4 / 1/8): strand G-buffer -27% / -34%.
- Strand density: the tips dither is the measurable coverage loss; `rt_hair_groom_tips=0` kept (2026-11-07).
- Primary-only minimum strand footprint rejected offline (2026-11-07). ReShade rs1 INVALID (ReShade not loaded, 2026-11-05).

## Current user decisions
- Stock visible hair ships; RTX Hair is an opt-in Beta control (Lighting > RTX Hair, Ctrl+Shift+F7), Off in every preset. Experimental page in release (behind Show experimental options, all off): Water surface reflections, two Frame Generation pacing tests, Strand hair edge resolve.
- The game-derived asset database ships in the archive. GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP plus portable ZIP; manual download. Lean: only the tests that are really needed. Optimisations that qualify are always on.
- 2026-11-11: RTX Hair motion stability On in High and Ultra. 2026-11-14: launcher Direction A, instant apply, dark only.
- 2026-11-14: Low-latency scene On in High and Ultra, always with the hit wait fix. Sensible gates.
- 2026-11-14 night: the four VRAM right-sizing policies On by default; disk cleanup approved.
- 2026-11-15: finalise 0.9.6-beta for release, RTX Hair Off by default. 2026-11-20: lives paused ("Non ora").
- Pending: RTX Hair On in a preset; `rt_hair_card_fill` stays an Advanced option unless the user wants it.

## Older entries (2026-11-04 to 2026-11-09; full text in the archives [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md), [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md))
- Releases: 0.9.3-beta, 0.9.4-beta (RC PASS), 0.9.5-beta packaged 2026-11-08; build.ps1 prefers VS 2022; packaging on a mounted-folder disk; launcher picks the RTX GPU.
- Scene handout design and increments 1-5 (2026-11-08/09), mesh cache budget charged with its own BLAS, BLAS chunk caps, VRAM reserve from the observed rise.
- RTX Hair 2026-11-04 to 11-09: groom storage v3 and converter `bg3rtx_hairconv`, swap and join fixes, strand fibre shading, edge quality, warm-up,
  switchable while playing (Ctrl+Shift+F7), header camera from the GPU, strand-resolve pairing, capture stride split, M1-M5 quality items (mostly READY_FOR_LIVE).
- Lighting: RT pipeline GI trace (on), compact GI hit state (always on), early roof rejection (on), exact local-shadow optimisation, RF probe work (mixed),
  SER feasibility, wide split on by default, rise reserve on, Home dashboard and Hotkeys page.
- Rejected: far GI live state, conservative GI-only TLAS, GI origin audit, BLAS flags replay, sun wave, default Ultra RF follow-ups.
- Audits (fixed offline): DLSS 5, FG, core completion contract, L11/L12/L17 submission, suspect review, S-F05 R11G11B10, ReShade isolation, queue cancellation.
