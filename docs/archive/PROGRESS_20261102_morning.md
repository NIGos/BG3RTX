# BG3RTX progress - 2026-11-01 evening (main d089b7b, ABI 25)

One current summary, about 150 lines. Everything older, verbatim: [archive/PROGRESS_20261101_evening.md](archive/PROGRESS_20261101_evening.md)
(the 17 folded fragments of 11-01 and the summary before this rewrite), [archive/PROGRESS_20261101.md](archive/PROGRESS_20261101.md),
[archive/PROGRESS_CODEX_2026-11-01.md](archive/PROGRESS_CODEX_2026-11-01.md), [archive/PROGRESS_CODEX_2026-10-31.md](archive/PROGRESS_CODEX_2026-10-31.md).
Branches write `docs/progress/<branch>.md`; `py -3 scripts/fold_progress.py` folds them here. Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md).

## User questions and decisions

- Decided: **`rf_hit_shade` stays Experimental until its bias is within the game's own spread** (tier (a)
  L_b / L_a S8 0.925, S6 1.089, camp 1.097). The threshold is not lowered. First fix: `rf_hit_gi` (below).
- Decided: **`ls_k` stays off** in every preset.
- Decided: disk: keep what a test or comparison still reads; delete superseded packs and analysed raw dumps.
- Open (not blocking): **`rt_hit_emission` 1 or 2** (1 = hidden hits only, 2 = also screen hits). Default 1.
- Open: live tests are **paused by the user**. The batches below are READY_FOR_LIVE; none is queued.

## Live read of gi_far, box_lights, em_forge (branch worktree-agent-a777f91256eb118af, analysis + job fix, 2026-11-02)
- gi_far (gf1 camp 01:54, gf2 S8 01:57; valid, exposure held, ratio 1.0000). Energy, scene colour / s1b13[0]:
  camp ON +0.17% (band 2.81%, no prediction); S8 ON -0.10% vs predicted -2.0% (band 1.38%): gap persists, gate
  fails. Cost (GPU trace pass, ON - mean OFF): S8 1.09 -> 4.35 ms (+3.26, OFF spread 0.003), camp 3.43 -> 6.90 ms
  (+3.47). The S8 frame time is confounded by the 01:57-01:59 build; GPU ms only.
- S8 stages: view vis (gi_debug=3) ON / OFF 0.9998 (predicted 0.948) while the run's `GI far` line (0.840 vs
  0.887) and the ls give-back (x2.4, 120-frame stats) moved. Replay (new `frame_replay --push radius=6`,
  `rt_stats=0|1`), S8 pack 142819-1, hit_sun + RT_GI_FAR + live defines: giRaw.a 0.8320 vs 0.8790 off, rt_stats
  0 and 1 identical: the pass is correct.
- Cause (fixed, READY_FOR_LIVE): layer/geocapture.cpp skipped the hit-material capture in every trace frame
  (`!trace_recording()`): no deferred packet, `rt_hit_material_use` fails, the hit GI falls back to the base pass in
  the traced frames only. Every trace-based energy of gi_hit_direct / gi_hit_sun / gi_far / rf_hit_shade / hit
  emission measured key off. Now skipped only when the trace takes the G-buffer itself (geo_capture=1, default 0).
  Layer change (relaunch). gi_far stays off; predicted.json rewritten before the rerun (gf2 -2.0%, view vis 0.947).
  Live: queue gf2-s8-fg2 alone, test tree on this commit.
- The `view bounce` stage (gi_debug=2) is not usable: d1669194 max R = 65024 (R11G11B10 saturated), mean
  (2.49, 2e-5, 7e-6): a few overflowed red pixels set the mean. Its 0.870 ratio means nothing.
- box_lights ls_box=2 (bx1 S6 02:01, bx2 S7 02:05; valid). Cost, ls pass ON - mean OFF: S6 0.449 -> 0.772 ms
  (+0.32, slightly above the expected 0.15-0.3), S7 0.736 -> 0.936 ms (+0.20). Energy within the band: S6 -0.54 /
  -1.35% (band 2.00%, OFF spread 1.39%), S7 +0.10 / -0.13% (band 1.66%).
- S6 mode 2: unshadowed boxes traced 369M rays / 120 frames, 18.0% occluded, removed 5.45% of the lit luminance;
  shadow-mapped part 0.165% only. Near-plane cut 1.96M rays, but "occluded where the atlas term is 1" did not
  fall (211k mode 1 -> 211k mode 2, already 6.7% of the shadow-mapped occluded rays in mode 1, not the lr3
  ~100%): the near-plane half has no measurable effect here. S7: 26.5% of 84M unshadowed rays occluded, removed
  0.48%. Bound: `at or above the upper neighbour` 0.026% px (S6) and 0.16-0.18% (S7), the same in mode 1 and 2
  (not a mode-2 over-estimate); inconclusive 0.
- S6 PNGs: ON darkens a band on the lower-right walkway (planks right of the front disc, in shadow of the raised
  ledge) and the central pillar; view-removed shows the removed light as long strips on that walkway. User check:
  S6 save, lower-right walkway and the pillar, ls_box 0/2. S7: no change beyond moving characters; view-removed
  is stippled on the floor around the stage and under the balcony rail (noisy 1-ray visibility for RR).
- em_forge ef1 (02:09) INVALID only through the job: the runner prints `[motion-trace] phase=N` only for phases with
  trace_at (scripts/auto-run.ps1:694-699); check-off/full/sliced have none. Fix: expect the `[pan-plan] window 7/8/9`
  set lines instead (all match the 02:09 runner.txt). lean_dryrun.ps1 now fails any expect_runner motion-trace line
  of a phase without trace_at (fails on the old job, all 17 lean dryruns pass).
- em_forge gate from the same log: check-sliced 0.016 ms per run median, max 0.026 ms (< 1 ms: pass), 0.017 ms/frame;
  check-full 0.113 ms per run, 0.007 ms/frame; gi: GPU cost check-off 2.770-2.786, sliced 2.778-2.837 ms (within
  the off spread + 0.017). A rerun is needed only for a formally valid receipt.

## GUI audit stage 2 done: fg_letterbox automatic, default 1 (branch worktree-agent-a604b8c794b0a930c, offline, 2026-11-02)

- Found on main 5136099: the audit proposal is already implemented (ec9665a, then the M4/M5 promotions).
  Experimental section 28 -> 6 entries (Emission at ray hits, Far-field sun bounce, Native coverage,
  Physical hair ray origins, Opacity micromaps, Preserve cutout capture); RT water and Hidden-surface
  shading are level knobs in Advanced (+ Refraction and + Bounce in Ultra). Emission writes `rt_emissive`.
  Developer only (dev=1): lighting reference, exact-k (`ls_k`), current cascade sun, `rt_hit_emission=2`.
  Old knob ids migrate through `kKnobAliases` / `Choice::mark` (settings_test `test_user_ini_migration`).
  `rt_hit_material_recycle`, `_capture_unlocked`, `_lean_hooks`, `rt_scene_wait`: default 1, no entry.
- This branch: `fg_letterbox` default 0 -> 1, moved from `kIniOnlyKeys` (removed, now empty) to
  `kAutomaticKeys`. The layer logic is already automatic: `fg::output_extent` returns the pass extent
  whenever a sampled image has it, so equal sizes are unchanged; it only acts when the swapchain is larger.
  Evidence: smoke_ultra su1 2026-11-02 01:08 with `fg_letterbox=1`: every-window gates ok (7), FG
  displayed/rendered 2.00, scene wait 0 timeouts.
- Files: bg3rtx.ini, common/settings.h (generated defaults), layer/streamline.cpp (fallback literal 1,
  host layer: needs a game restart), tests/settings_test.cpp, docs/USER_SETTINGS.md, docs/GUI_AUDIT_20261101.md.
- Presets: knob values unchanged. Effective key changed in every preset: `fg_letterbox` 0 -> 1 (no-op
  at equal sizes). Old user inis: the old Off was never written (only knobs differing from the preset
  are), an old On wrote `fg_letterbox=1`, the new default; a hand-set 0 is kept for A/B.
- Not live-verified: the larger-desktop case itself (3840x1600 game in a 3840x2160 swapchain). su1 ran
  with the pass at the output size. Check: borderless on a 3840x2160 desktop, FG on, log line
  `[sl] game output ... backbuffer subrect`, FG displayed/rendered 2.00, no UI ghosting at the bars.
- Live batch prepared, not run: tools/live_batches/fg_letterbox (one S8 job, 75 s, borderless via the new auto-run.ps1
  `-Borderless`, fg_letterbox 1 -> 0 -> 1; gates and predicted.json in its README). dryrun + lints pass. 95/95 CTests.
- fl1 022810 INVALID: borderless gave a 3840x1600 swapchain (desktop now 3840x1600; 3840x2160 on 11-01 14:16-14:28).
  Job now adds `-GameResolution 2560x1440` (new auto-run.ps1 option, graphicSettings restored after the run).

## M6 step 1: native ambient against the PT reference on 3 scenes (main, measured, 2026-11-02)

- Packs captured live (rc2 camp 01:34, rc3 inn 01:37, both valid), replayed offline with the casters in the TLAS
  (post_replay.ps1 -Scene). Kept: replay-20261102-013545-2 (camp), replay-20261102-013914-3 (inn), S8 142819-1.
- E_native x AO / (k x PT far + PT env), tiles p10 / median / p90:
  - S8 (Water Queens House): 1.44, 0.90 / 1.75 / 3.01 (earlier pack, docs/PT_FEASIBILITY_OFFLINE.md)
  - camp (outdoor, sun 98%): 0.84, 0.63 / 0.81 / 1.15; far field 74% of the reference, k 0.75
  - inn (interior, probes 99.9% of froxels): 0.78, 0.53 / 0.77 / 1.02; far field 82%, k 0.25
- Verdict: not consistent across scenes (S8 native brighter, camp and inn darker by about 20%). No systematic
  ambient correction (goal rule: only if coherent on every scene). Camp and inn agree within their tile spread;
  S8 is the outlier, so the next question is what S8 has that the others lack, not a global scale.
- Next in M6: the gi_far gap (-0.3% live against -2% predicted), then SHaRC.

## Hit-material coverage: alpha-tested pool meshes get pool rows (branch hm-coverage-2, READY_FOR_LIVE, 2026-11-02)
- Ranking (replay packs, `tools/hit_material_blockers.py`; live `hit material` lines of m4p 01:13-01:28):
  S6 021627: pool alpha without a row 39.0% (depth FS 84d5df19 Base_AlphaTest_VT 27.5%, 5afd54a9 11.5%), pool
  without a row 46.2% (inst 96/97, 30-37 m discs: FLUID_Lava_Plane_Flowmap bd5dbda2/0b8dfd73, animated flowmap with
  time, vertex colour and world position: a new recipe, not started). Camp: row reason 60% (soup alpha foliage,
  IP_Wind_GrassFlowers: per-draw rows like wind RGB). Inn: base 68%. S8: 68% qualified now.
- Cause of the S6 alpha share: Base_AlphaTest_VT STI uses the audited G-buffer pair 2d2ac3c3/14678345 (asset DB);
  its pool records were qualified (live m4p3 012015: 66 records) but only 21 had a UV/basis, because bvhsplit
  gave alpha pool meshes neither a basis range nor a row (rec holds the alpha record).
- Fix, live key `rt_hit_material_pool_alpha=0`: basis ranges and rows for alpha pool meshes too; the row slot + 1 in
  InstInfo meta bits 16..31 (`nh_pool_alpha_slot_shift`), rec keeps the alpha record. `rt_hit_material_pool` reads it.
- Offline: 95/95 CTests (pool test pins the meta encoding); key off, both packs bit-identical with the new SPIR-V (S8 142819 giRaw fnv1a a54c4dfd1cc9acb9, S6 021627 5cf77062bdef5c21, as before). The gain needs live rows (the packs hold none).
- Live job `tools/live_batches/m4_pool_alpha` (S6, 4.4 min, 0.2 GB), dry run passed. Predicted: qualified +23 points
  (10-39), shot-on +0.5%.

## Ultra: Far-field sun bounce 6 m (branch gi-far-ultra, READY_FOR_LIVE smoke_ultra, 2026-11-02)
- Gates (trace fix in): gf2 S8 031330 scene colour -2.01% vs committed -2.0% (band 1.32%), view vis ON/OFF 0.9471
  (predicted 0.947); camp 033251 -2.42% (band 2.29%, no prediction written; same sign, size as the S8 per-ray
  visibility drop scales: camp 0.816 -> 0.746 vs S8 0.889 -> 0.842, x1.5).
- Cost, GI trace pass: S8 1.09 -> 4.31 ms (+3.23), camp 3.26 -> 6.97 ms (+3.71); frame time +4.4 / +4.0 ms.
- Change: `exp_gi_far` leaves Experimental (Advanced, after Emission at ray hits), presets {0, 0, 0, 1}; schema
  regenerated; settings_test 934 checks pass. Not merged: smoke_ultra with it first.
- User check: S8 (Water Queens House) and camp, Ultra; Far-field sun bounce Off / 6 m: rooms slightly darker where the
  game's ambient was brighter than the traced far light; no light leaks under roofs.

## Water compose flicker: keep the lighting's scene pair instead of refusing (branch worktree-agent-afa742daadc3632ff, READY_FOR_LIVE, 2026-11-01)
- Bug (user, main 89dd500, Ultra, all water keys, rt_cascade_casters=1): water reflection and shadows switch
  on/off each frame. Log 20:35-20:41: 211 to 245 composed of 300 frames in the bad windows.
- Cause: "refused: no scene" (51 to 84 per 300 frames), not the lighting age. It tracks the scene-wait
  timeouts (116 and 133 per 600 in the same minutes): the lighting traced the previous build, the late
  build of its frame then committed with an index below the next pass's, and compose_at_pass_end refused
  a newer scene that was not this frame's. Each such pass left the water uncomposed for one frame.
- Frame generation is not a factor: frame indices count the app's presents only (DLSS-G's generated ones
  go through Streamline below the layer; [sl] line: 60 indices, 120 displayed). The 3-index lighting bound
  is now written as 1 x (1 + 2 frames recorded ahead), same value; the old FG derivation was wrong.
- Fix (core only, no ABI change): common/water_compose.h pick_scene. A pass on an earlier lighting whose
  current scene is neither the stash's nor this frame's binds the lighting's own pair, kept alive for this
  frame through rt_omm_scene_reference (stamps that slot; refuses a retired one; no OMM work with rt_omm off,
  which water needs). rt_scene_set is still never called for an older build (it would wait on the game's thread).
- New line "[water] compose stability": frames with a water pass, composed, uncomposed (on/off flips), the
  no-lighting refusals by cause (cleared, old, ahead) and the earlier-lighting cases (current slot stale,
  newer build before this frame, newer pair unavailable, lighting's pair kept).
- Gates: expect_every "uncomposed 0 (on/off flips 0)" in every compose-on window of m5w3..m5w6, m5p1..m5p3
  and su1. Unit tests: pick_scene call order (no rt_scene_set for an older build), lighting_refusal,
  ComposeContinuity flips (water_compose_test).
- Not done: holding the previous frame's delta when even the lighting's slot is retired (expected 0;
  the stability line counts it). Live check: su1 or m5p1 at S8, the stability line in every window,
  and the water should no longer blink while panning. Cost: two host calls per pass, no GPU change.

## Release check and GUI audit (branch worktree-agent-af4955ea85416c41c, offline, 2026-11-01)

- Default-path identity vs f6b6636 (the last fully VALID lean_regression, 08:39): 628/1242 requests identical.
  All 614 that differ are behind off keys (gi_hit_*/gi_far 320, rf_hit_shade/rf_hit_gi 192, ls_box 60,
  rt_hit_material/reference 40, water compose 1, em_debug 1). The 30 requests the lean runs compiled are
  identical, except the two box variants, which compiled only with ls_box=1. No bug.
- The 14:22 lr1 was on f97a793, not 23b6d65, and INVALID: FG at 1x on a desktop larger than the game's
  output, the case `fg_letterbox` fixes. Not a code regression.
- Defaults f6b6636 -> main: no existing value changed; 13 new keys, all off or inert; 9 new Experimental knobs.
- data/release.ini pins 21 more diagnostics off (rt_primary_check, cpu_timing, crash_dump, pan/replay
  captures, sharc/ls checks, diagnostic logs). They are already 0 in bg3rtx.ini, so the shipped values do not
  change. settings_test 681 checks, 0 failed. Details: docs/RELEASE_CHECK_20261101.md.
- Open for the user: rt_emissive has no GUI entry and release.ini forces it to 0, so "Emission at ray hits"
  cannot be enabled in a player build. Also: ls_box/rt_emissive in release.ini, and crash_dump for players.
- GUI audit (proposal only): 28 Experimental entries -> 8 (hit-shading bundle, RT water levels, native
  coverage group; letterbox, recycle and foliage rows automatic; exact-k, current sun and reference
  developer-only). docs/GUI_AUDIT_20261101.md.

## GUI rationalization: 28 experimental entries -> 8 levels (branch worktree-agent-ae1f991dc62380b40, offline, 2026-11-01)

- What changed (docs/GUI_AUDIT_20261101.md, user-approved): `common/settings.h` kKnobs. Main list gains Roof sun
  shadows (`roof_shadows`) and Box light shadows (`box_lights`: Off / Shadowed / All, off in every preset).
  Experimental: Hidden-surface shading (Off / Materials / + Bounce / + Reflections, writes the 8 hit keys incl.
  `_recycle` and `_wind_rgb`), Emission at ray hits (writes `rt_emissive` itself), Far-field 6 m, RT water (Off /
  Reflections / + Shadows / + Refraction, 7 keys), Native coverage (Off / Hair + scalp / + Tilesets), Physical hair
  origins (advanced), OMM, cutout spill. Developer only (dev=1): lighting reference, exact-k, current cascade sun,
  `rt_hit_emission=2`. `fg_letterbox`: no entry, ini key kept (the layer has no automatic mode yet).
- Every ini key still works. Old files: a level is picked from its marker keys (`Choice::mark`), old knob ids
  resolve through `kKnobAliases`. The user's 11-01 file loads as Box All, Hidden + Reflections, Emission Hidden,
  RT water + Refraction; saving adds `_recycle`, `_wind_rgb`, `rf_hit_gi`, `rt_water_game_shadow` (2 need a restart).
- Restart rules fixed: `rt_hit_material_wind_rgb` (startup only), `rt_hit_material_instanced` 0->1. The launcher's
  restart marker compares against the saved files, so migrated keys show it. Save lowers choices whose dependency
  is off (`enforce_needs`).
- settings_test: level key sets, no orphan key, aliases, dependencies, restart notes, fixture migration
  (`tests/fixtures/settings/user_ini_20261101.ini`, copy of the user's file).

## How to test in game (replaces the per-key lists)

Settings window, Experimental (off in every preset), preset Ultra. "Restart" = raise the level, quit, start again;
lowering is live. Ctrl+Shift+F9 switches every RT effect off and on.
- **RT water** (S8 Water Queens House pool): Reflections (**restart**): columns and temple reflected. + Shadows
  (**restart**): far half of the pool in shadow, no specks; camp pond (S3) tree shadows. + Refraction (**restart**):
  the pool bed shifts with the view.
- **Hidden-surface shading**: Materials (**restart**): no visible change, Q/E rotation keeps shadows on their casters
  (buffer reuse). + Bounce (live): S3, never darker. + Reflections (live): S8 polished stone, rf_debug=8 L_a/L_b.
- **Emission at ray hits** (live, needs Hidden-surface shading): S6 lava in reflections.
- **Box light shadows** All: S6 forge, S7 inn, no light into the next room.
- **FG letterbox**: add `fg_letterbox=1` to bg3rtx_user.ini by hand (fake fullscreen on a larger desktop).

## Silent degradations: caps, variant matrix, gates (branch worktree-agent-a29080dd15a8a2223, merged / live-confirmed in part, 2026-11-01)

- Roof volumes: `kSunMaxVolumes` 256 -> 512 (shaders `SUN_MAX_VOLUMES`). CTY_Main_A had dropped 103 of 359
  (radius 1980 m); the largest level has 398 (`roof_cap_test`). GPU cost not measured live yet.
- `sun stats` says why there are no volumes ("level unknown"): "0 roof volumes, scene top -96.8" was the menu.
  Scene wait "73 / 168 skipped": menu and loading only, 0 in game. No change.
- CTest `variant_matrix_test`: every key pair has its variant; the only skips are the water passes under
  rt_omm, tileset, scalp, hair, hair origin (`kWaterNoVariant`; their 24 requests compile). Log
  convention `skipped: no variant for <keys>`.
- Set-1 out-of-range reads were nonzero exactly in the runs whose mesh pool doubled to 2048 MB (2^31;
  0.7-4.2M per frame). The pool now stays at 2047 MB and within maxStorageBufferRange (`[bvh] mesh pool
  full at` when full). smoke_ultra 23:48: guard 0 for the whole run (live-confirmed).
- Cascade casters capture grows on overflow to demand + 25% (`[cascade] buffer full: ... grows to`);
  the casters line prints `demand max`.
- Hit-material basis: over-cap meshes are logged (`over kBasisCapBytes`); at 256 MB 128-255 meshes waited
  (demand 410-565 MB), so the cap is 768 MB.
- Alpha "region full 0.30" after loading: the region had shrunk to its 171 MB floor; the S8 load frame
  wanted 223.8 MB. The floor at a wide stride is 40% of the buffer (409 MB of 1024; the day's peaks:
  alpha 404.6 MB, opaque 463.8 MB). Not yet confirmed live.
- smoke_ultra forbids the variant skip, roof WARNING, nonzero guard, `over kBasisCapBytes` and
  `mesh pool full at`. Table: docs/GATE_AUDIT_20261101.md section 8.

## Persistent runtime shader cache, background prefetch, A/B toggle cost (branch worktree-agent-a270898c2f4538e6f, READY_FOR_LIVE, 2026-11-01)

- Cause of the startup greybox: every launch compiled each runtime request with DXC on the render
  thread (44 in the 20:35 log). Offline, the same 35 files (50 entries) take 47 s of serial DXC; the
  biggest are 6 s (gi_trace_rt_hit_sun_em) and 10 s (rt_reflections_hit_shade_em).
- Cause of the slow Ctrl+Shift+F9: the toggle only overrides cfg keys; pipelines are kept per variant.
  But after ON the emissive scene is a few frames stale, so the non-emission fallback variants
  (gi_trace_rt_hit_sun, rt_reflections_hit_shade) were requested for the first time and compiled
  synchronously: +6 s and +10 s after the 20:36:32 toggle.
- Fix (layer only, no ABI change, no new ini key): SPIR-V cache in `%LOCALAPPDATA%\BG3RTX\shader_cache`
  (`BG3RTX_SHADER_CACHE` -> `<dir>\runtime`, `none` = off), keyed exactly as tools/shader_identity.py
  (C++ twin in layer/shader_cache.cpp; on the real tree all 44 keys equal Python's). Checksummed entries,
  corrupt = miss + recompile, LRU prune above 512 MB. Hot reload rebuilds only entries whose key changed
  (common/ is now watched too). A background thread prefetches the last 5 sessions' requests
  (requests.txt) at the first request, so the fallback twins are ready before any toggle.
- Expected: warm launch = tree 25-55 ms + keys 5-27 ms + 44 reads ~50 ms (measured offline) instead
  of ~45 s of DXC; toggle stall from 16 s to a pipeline creation.
- Logs: `[shaders] cache: N hits, M compiled, F failed, K ms building (render thread waited W ms)`,
  `[shaders] cache: prefetched ...`, per compile `compiled in X ms`, `[ab] toggle cost: longest gap
  between presents X ms`.
- Tests: shader_cache_test (golden keys, disk tree = memory tree, hit/miss/corrupt/prune in %TEMP%),
  shader_cache_key_test.py (Python side of the golden file).
- Live check (one launch twice, any save): second launch shows `N hits, 0 compiled` and a shorter
  greybox; press Ctrl+Shift+F9 twice and read the `[ab] toggle cost` line.

## User decisions: GUI and diagnostics (branch main, decided, 2026-11-01 evening)

- GUI audit (docs/GUI_AUDIT_20261101.md on branch worktree-agent-af4955ea85416c41c) approved as proposed:
  one "RT water" entry with levels; one "Hidden-surface shading" bundle with levels (foliage rows and buffer
  reuse automatic); "Current cascade sun" and exact-k leave the GUI (ini / developer only); "Emission at ray
  hits" writes rt_emissive itself so players can enable it. 28 experimental entries become 8.
- Crash dumps stay off for players. Later: a diagnostic mode in which the user produces a diagnostic package
  (logs, effective settings, system/GPU info, crash dump if any) to attach to a report, together with a save
  the user chooses. The tool may only copy a save the user explicitly selects, read-only; it never reads or
  changes saves on its own (AGENTS "Never").

## Ultra preset: RT water "+ Refraction" (branch ultra-water-preset, READY_FOR_LIVE, 2026-11-01)
- Stage 3 of the M5 water promotion (m5_promotion passed at S8, Flymm and camp). Redone on main's one
  "RT water" entry (common/settings.h kKnobs), not the old 8-entry commit 7cff2bb (not merged).
- RT water leaves Experimental: Advanced, before the Experimental section; presets {Off, Off, Off,
  + Refraction}. Ultra writes all 7 water keys = 1 (game-shadow replacement included); other presets 0.
- Startup rules: required_on adds sharc_device=1 (64-bit shader integers) next to rt=1; turning compose,
  shadows, local shadows, game shadow or refraction on needs a restart, off is live (key_change_needs_restart).
- settings_test test_ultra_water: Ultra writes every water key, other presets none, dependencies met in
  Ultra, sharc_device=0 unmet, restart rules, GUI order. The 11-01 user ini migration now reads the water
  keys from the preset (one restart marked fewer). Schema regenerated.
- Jobs: m5p1..m5p3 and su1 drop the explicit water keys (preset=ultra sets them; the plans keep their
  per-phase toggles). The test tree must hold this branch.
- Merge only after a smoke_ultra run passes with it. Cost: the water chain 0.53-0.67 ms (m5_promotion
  cost-on), up to 520 MB of water records.

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

## Scene wait in motion: phase probe, unlocked material captures, alpha floor by stride (branch scene-wait-motion, READY_FOR_LIVE, 2026-11-01)

- Live su1 21:26 (S8, Ultra keys, rt_hit_material_recycle=1): static windows 0 timeouts, wait mean ~2.8 ms (was
  456/600); pans and rotations 1-7 timeouts per 120 frames, mean 5.2-7.6 ms, max 12-22 ms. No cpu_timing in that run.
- Probe (always on, no cost): the scene thread's phase (prepass recording, scene lock, hit material prepare, BVH build,
  alpha commit, or no capture yet) is counted at every timeout and logged after the scene-wait line:
  "[rt] scene wait timeouts by the scene thread's phase: ...". It names the remaining delay in one run.
- Candidate fix `rt_hit_material_capture_unlocked=0/1` (live, default 0): the G-buffer material hooks
  (deferred_begin, pool_observe) read their draw's bindings without the provider mutex, which the prepass hooks
  take on every draw while the scene is recorded; in motion more draws are captured (new records), so the prepass
  thread waited longer. Only this recording's thread writes its packet until finalized (others read finalized ones).
- Alpha region: rt_alpha_mb left at 64 follows the vertex size like rt_xfb_mb (171 MB at 64 B). su1's first
  window made 0.58 alpha draws/frame opaque while the region sat at its floor after the load (demand 224 MB; later
  windows 0.00 at 89 MB region / 59 MB demand). rt_capture_budget_test covers it.
- Job: tools/live_batches/scene_wait/jobs/sw3-motion-s8-fg2.json (117 s, pans Q/E unlocked 0/1/0 + two rotations
  with 1, cpu_timing=1, gate 0 timeouts in cost-on-*, base windows log the baseline). Dryrun passes.

## rf_hit_shade S6 direct excess: the BRDF, not the boxes (branch rf-hit-box, READY_FOR_LIVE, 2026-11-01)
- Live m4p3 161852 (`rf hit shade boxes`): the boxes are 0.00508 of tier (a) L_b 0.0764 (6.6%), already at the
  game's visibility with the AO factor; point/spot are 0.0606 of the 0.0657 direct. The box path (channel, falloff,
  cookie, shadow strength, AO factor) matches f469db78. What differs for every light: the hit used the declared
  FresnelBlend, the screen the game's Burley x mix(1, 0.6622, a) + GGX/Smith-joint/Schlick.
- Fix (behind `rf_hit_gi`, define RF_HIT_GI -> NATIVE_HIT_GAME_BRDF): point/spot, box and sun at hits use
  `hd_game_scatter` (common/native_hit_direct.h), with the same domain checks and status codes. Left out: the
  screen-quad AA roughness term (a hit has no derivatives).
- Test: `box_light_test` section 8: hit box term == the game's box loop on 98k random records/surfaces (0 failed;
  2^-14 relative plus the GGX-peak and grazing N.L bounds). FresnelBlend / game energy over the same cases 0.92.
- S8 replay proxy (gi_hit_direct with the define, the same hits and lights): direct sum 36.34 -> 35.31 (-2.8%),
  supported set unchanged.
- Expected tier (a) L_b / L_a: S6 1.090 -> about 1.06 without the GI term, about 1.12-1.22 with rf_hit_gi
  (was 1.15-1.25). S8 0.925 -> unchanged (its direct is 0.0057 of 0.14), about 1.0-1.03 with rf_hit_gi.
  The remaining S6 excess is not in the lobe or the boxes: the next suspect is visibility (lit 71% of rf light rays
  vs 56% in the GI at S6).
- Gates: 91/91 CTests. rf_hit_shade_shader_test PENDING: stopped at 481/1464 (0 failed) when a live run started; rerun with no bg3.exe (cached).

## m5_promotion gates: energy prediction double count, conditional cascade gate (branch m5p-energy-capacity-gates, done, 2026-11-01)
- S8 energy "BRIGHTER than predicted" was the tool's prediction, not the image. The compose energy line's
  "mean delta over owned" is lum(C') - lum(C) of the composed pixels with every term of the variant
  (water_compose.hlsl d). With the sun shadows on it held their sun removal on the composed (glint) pixels:
  -0.137 of the -0.343 per owned pixel, which the shadows line counts again.
- tools/m5_promotion_gate.py now takes the reflection's own term, (A L - B) per composed pixel x composed
  share (-0.0014 per owned pixel at S8). Re-run on the 21:38 logs: S8 step-sun -11.17% vs -11.29%, step-ls
  -10.32% vs -11.27%, shot-on -10.78% vs -11.21% (tolerance 2.2%): all match. Flymm and camp unchanged (match).
- Cascade capacity gate: new expect_every option "when_present" (scripts/queue_gates.py): a window without
  the selected line passes (the pass did not run), present lines are judged as before. The canonical
  CAPACITY_GATES cascade entry has it, the capacity lint requires it, 19 jobs updated. Flymm printed 4
  casters lines during the load and none in a cost window (an interior has no sun cascade pass).
- Gates re-run on the existing logs with the updated jobs: m5p1 ok (13), m5p2 ok (10, was INVALID), m5p3 ok (11).
- Tests: m5_promotion_gate_test (sun-inclusive mean delta not counted twice), queue_gates_test (when_present
  absent/present, lint flags a cascade gate without it).

## Flymm "+828 non-finite": depth texels, not water; compose fail-closed (branch m5p-energy-capacity-gates, READY_FOR_LIVE, 2026-11-01)
- Per-dump counts of all 20 m5p2 traces: every RR colour input has 0 non-finite pixels, every colour image
  0. All the counted ones are in one 2048x2048 D32F depth image, dumped 9 times per frame, with
  3328..3527 infinite texels per frame with every key off (OFF-a alone: 29952..31743 totals). The gate's
  sum over every dump made frame-to-frame depth variation look like water NaN.
- tools/m5_promotion_gate.py counts colour images only (formats not D*); Flymm rows now flag nothing.
- Compose fail-closed anyway (shaders/rt/water_compose.hlsl, every variant): C' = max(Cu, 0) only where
  Cu is finite, else the game's C, counted in word 56 (common/water_compose.h kCsNonFinite, compose_writes);
  the base/sun/GSH variants reduce 57 words. Logged as "non-finite terms N" in the compose stability line.
  water_compose_test reproduces 0 x Inf, Inf radiance, a half-packed Inf and Inf - Inf. All 14 compose
  variants compile and validate; the other 2019 requests are byte-identical to main.
- Gate: expect_every "non-finite terms 0" in every compose-on window of m5w3..m5w6, m5p1..m5p3, su1
  (the old logs predate the word: the next run judges it). sw3-motion: cascade gate when_present.

## Hit-material per-draw hooks: lean path (branch hook-cost, READY_FOR_LIVE, 2026-11-01)

- sw3 21:55 (recycle 1, capture_unlocked 0/1): every pan timeout in phase "prepass recording"; capture end to commit
  ~1.1 ms. `[cpu] hit material` in pans: prepass hooks 2.7-3.2, G-buffer material hooks 2.5-3.5, query 0.9 ms/frame;
  inside: collect 1.3-1.5 ms, 1.2-1.8 allocations (31-40 MB) and 1.2-1.9 frees per frame (a free ~0.6-0.75 ms).
  capture_unlocked=1 did not remove the timeouts.
- `rt_hit_material_lean_hooks=0/1` (live, default 0): no vkFreeMemory on a recording thread (evicted / idle buffers
  freed at present), best-fit reuse (`take_fit`: same usage/memory, size within [need, 2 x need]; moving query sizes
  no longer allocate + evict), collect() once per frame (forced only at the packet/query bound), the G-buffer draw key
  only for draws that qualify (the family/pipeline-declined majority skipped it), prepass candidate vector reserved.
- New split on the `[cpu] hit material` line: draw keys and mutex waits (inside the hook phases), `lean_hooks N`.
- Expected: frees and allocations off the hooks (~1.3-1.6 ms), collect ~0.1 ms; the rest of the hook time is the
  per-draw key (bindings, mapped reads, buffer sizes) and the join: the new split says whether the next cut is there.
  Target hooks < 1 ms total and 0 timeouts in pans: needs the live A/B, not yet shown.
- Job sw3-motion-s8-fg2 now A/Bs lean_hooks 0/1/0 (capture_unlocked 1). 93/93 CTests, lints, dryrun pass.

## Pool hit materials: why 14.9% of S8 lookups have no row, Base_MSK_VT rows (branch hm-pool-mask, READY_FOR_LIVE, 2026-11-01)
- Cause of "pool without row" (S8 replay, 14.9% of lookups, 133 instances): class A pairs with no recipe ("other
  pair" 67 draws / 172 instances in the live log; basis ready 99/100, slots free). By S8 trace draws/frame:
  HeightGradient 54df894a/7a07134e 17.9, Tileset 6044c8cf/02807a68 13, then five smaller pairs (MSK 5).
- Built the smallest: Base_MSK_VT 2d2ac3c3/a5f44c48 (`LayoutMask`, live key `rt_hit_material_mask=0`, ini only:
  the GUI knob "Hidden-surface shading" holds 8 keys, its kv[8] limit, so it is not in it yet). Classifier vs 14678345: only RT1 (albedo tint by a 2D colour mask and three colours) and RT2.y at
  byte 12 differ (tests/material_families_test.py BaseMask; native_hit_pool_test pins the recipe and nv_mask_albedo).
- Tileset needs per-vertex COLOR in the pool basis; HeightGradient needs world height and more than 256 B of row.
  Both are infrastructure; not started.
- Offline: build + 91/91 CTests; key-off S8 replay bit-identical (fnv1a a54c4dfd1cc9acb9, all counters equal) with
  the new SPIR-V. The after-share cannot be replayed (pack rows/views are live captures). Shader identity vs 89dd500
  PENDING: stopped at 112/1464 (0 failed) when the live batch started; rerun when no bg3.exe (resumes from the cache).
  Expected: only requests including native_hit_material.hlsli change (an extra branch that does nothing without mask rows).
- Live batch `tools/live_batches/m4_hit_mask` (S8 + inn, 2 x 144 s, ~9 min, 0.4 GB): dry run passed. Gate: pool
  pairs line lists a5f44c48 R/B > 0 in diag-on, absent in diag-off; capacity and scene-wait 0. Predicted S8 +1.1 pts.
- Note: the first baseline frame_replay ran while a bg3.exe was up (it had exited a minute later); later runs checked.

## Hit-material scene-wait fixes on by default; prepass hooks off the provider mutex (branch hit-material-defaults, 2026-11-01)

- sw3 22:13 VALID: lean_hooks 1 windows 0 scene-wait timeouts in every pan and rotation (lean 0: 1-7 per 120 frames),
  hit-material hooks ~2 ms instead of 5-6. Kept: rt_hit_material_recycle, rt_hit_material_capture_unlocked and
  rt_hit_material_lean_hooks default 1 (bg3rtx.ini, settings defaults, layer fallbacks); automatic, no GUI entry
  (kAutomaticKeys), no longer written by the Hidden-surface shading levels; keys kept for A/B (0).
- Remaining mutex waits (0.6-0.7 ms/frame): with lean_hooks the per-draw prepass hooks take a leaf `pending_mutex`
  instead of the provider mutex; the map's other writers hold both. Not measured live yet (expect ~0 waits in the
  `[cpu] hit material` "mutex waits" field).
- settings_test 910 checks, 93/93 CTests pass.

## Bugfix round, 2026-11-01 evening (from the user's all-keys-on run, bg3rtx.log 17:11-17:31; all merged, offline)

1. Stale scene with hit materials (`[rt] scene wait` 5-456 timeouts / 600, mean 12 ms): the provider allocated and
   freed hundreds of MB of GPU buffers per frame on the commit path. Fix `rt_hit_material_recycle` (live, off): reused
   buffers, one `collect()` per packet, query outside the scene lock. Needs live (`scene_wait`, `smoke_ultra`).
2. Water skipped while `rt_cascade_casters=1` (Ultra): water now builds `RT_CASCADE_MASKS=1` (+ `RT_CASCADE_SUN_HIT=1`)
   variants; the 4 existing compose requests stay byte-identical. Water jobs now run with casters on.
3. Capture buffers auto-scaled by vertex size (`common/rt_capture_budget.h`): default 384 / 160 MB become 1024 / 427
   at 64 B vertices (hair 88 B: 1408 / 587). Values set by hand are used as set.
4. Capacity (`[geo]` drops 0, cascade buffer full 0) and scene-wait (0 timeouts) gates in every lean job, with
   `--lint-capacity` / `--lint-scene-wait` in every dryrun and in queue_gates_test.
5. New `smoke_ultra` job: Ultra + every qualified experimental key, water traced > 0 in every window.
6. FG letterbox (`fg_letterbox`, live, off): FG on a 3840x2160 desktop with the game at 3840x1600. Needs live.
7. rtStats renumber: `rf_hit_gi` 419..426, wind RGB 427..434, kStatsCounters 435.
8. `tools/shader_identity.py` cache: merge-green's shader step ~1-5 min instead of 20-30 (cold 1607 s once).

## Per-stage state (key, deciding numbers, status)

**M4 hit shading** (Experimental, not in Ultra). Plan: [PLAN_M4_REFLECTIONS_GI.md](PLAN_M4_REFLECTIONS_GI.md).
- `rt_hit_material` (restart): qualified S8 48.1%, S6 27.9%. Needs `rt_hit_material_recycle=1` (scene-wait gate).
- `rt_hit_material_instanced`: S14 0 clip rejects (gate < 0.04%), qualified 22% -> 37-42%, +0.14 ms. Passes.
- `rt_hit_material_wind_rgb` (startup, off): third instanced family (S8 up to +13 points expected). Merged.
  Open: its identity decision for RT_HIT_INSTANCED variants (336 requests differ with instanced on). Live `m4_wind_rgb`.
- `gi_hit_direct` + `gi_hit_sun` (live): never darker, S8 +0.52 ms together. S3 gate, motion, isolated cost not run.
  Offline invariant (S8 pack): hit vs base alpha bit-equal on 100% of 2.73 M px (`tools.gi_hit_invariant`).
- `rf_hit_shade` (live): bias above, +1.4 to +1.8 ms (unexplained vs 0.2-0.4 budget). Experimental.
- `rf_hit_gi` (live, off, needs rf_hit_shade): GI bounce at reflection hits + ls_box=2 box rule. Expected S8
  0.925 -> 1.0-1.03, S6 1.089 -> 1.15-1.25 (S6 excess is in the direct term). Merged; key off byte-identical.
- `rt_hit_emission` (live, needs `rt_emissive=1`): S6 6.9% of rf lookups.
- S6 m4p3 darkening (-1.05 / -1.82 / -3.10%): the animated lava (54.5% of luminance, follows the game's glow buffer)
  and shots 1-2 frames after a 4 s DXC compile. The "8692 non-finite" was the game's D32F depth: 0 in our scene colour.
  Live check `m4_promotion/s6_diag` ready. Guard in hit-GI variants (counters 417/418).
- Promotion: `m4_promotion` FAILED on 10-31 (cost camp +4.8, S8 +3.4, S6 +9.1 ms). Preset branch
  worktree-agent-a9e0aabd4d7f18008 stays unmerged until it passes.

**M5 water** (Experimental, off). Plan: [PLAN_M5_WATER.md](PLAN_M5_WATER.md).
- M5.1 geometry, M5.2 reflections (0.113 ms), M5.3 compose (0.098 ms, mean -0.35 to -0.65%): live at S8.
- M5.4 `rt_water_shadows`: R = C - D fix merged (clamped 145573 -> expected ~0, occluded ~62.6%). Gate clamped < 120.
- M5.4b `rt_water_ls_shadows`: sum check 100% within 0.098%, 0.18 ms. Passes.
- M5.4c `rt_water_game_shadow` (restart, needs rt_water_shadows): replaces the game's sun visibility on the two
  shadow-sampling families (camp pond). Merged, off; cascade variants too; 11/11 water requests byte-identical.
- M5.5 step 1 `rt_water_refraction`: 69.6% composed, energy +0.07%, 0.24 ms. Passes.
- Families: 16 supported pairs, 9 compose tables (`tools/water_audit.py`); Flymm basement mapped. Debris not mapped.
- Energy observable fixed: post-water image gives m5w4 -9.10% vs predicted -9.39% (the +2.20% was the exposure).
- Promotion: `m5_promotion` (m5p1 S8, m5p2 Flymm, m5p3 camp; 16.8 min, ~2.5 GB) READY_FOR_LIVE. Preset commit
  worktree-agent-a14b8f58ffc130189 (water chain on in Ultra) unmerged until it passes.

**M6 PT reference / far field** (offline; `gi_far` off).
- S8 pack with casters + VT cache: far field 53.6% on screen beyond 1 m; native vs PT far + env 1.44.
- `gi_far` 6 m predicted -2.0% of screen radiance; live S8 +2.87% was the eye adaptation: exposure-normalised
  -0.32% (camp -0.14%). Sign agrees, size does not. Stays off; next gf1 + gf2 with exposure held.
- Replay packs now carry the cascade casters (`casters.bin`, `PT_CASTERS=1`); `replay_capture/ambient` ready.

**Local lights (D-LS)**
- `ls_k`: off (user decision).
- `ls_box=2` (off, merged): shadows the 3,329 unshadowed box lights to their fade-in end. Expected +0.15-0.3 ms.
  Live `box_lights` (S6 + S7) and a visual check pending.
- D-EM projected check: device-local counters, `rt_em_check_slices=16` (no 140 ms hitch every 16 frames expected).
  Live `em_forge ef1` gate: check-sliced max per run < 1 ms. Not run.

**Frame generation**: `fg_letterbox` (off) needs a 3840x2160-desktop run: FG decision want 2, displayed 2.00,
no UI ghosting at the bars. Then default 1.

**Tooling** (merged): `expect_every`; host atomics lint; exposure-normalised two-sided energy gate
(`tools/m4_promotion_gate.py`, docs/GATE_AUDIT_20261101.md: 43 OK / 24 weak / 3 broken); `qa_exposure_hold` in 17
energy-gated plans (lint `--lint-exposure-hold`); `tools/longrun.py` progress + caches; `tools/shader_identity.py`.

**Stage 0 / watch**: real fps focused camp 60 RT / 93 off, inn ~71 / 89, forge ~100. `rt_stats` + `rt_primary_check`
~80 ms/frame: never in cost windows. Watch: camp rug rectangles, Astarion hairline, dialogue shadow flicker,
inn `pool hit match` 34-37% rotating.

## How to test in game

Settings window, section Experimental (off in every preset). Keep the preset on Ultra (GI Ray-traced, Reflections on,
Roof sun shadows on). "Restart" = turn it on, quit, start again. Turning any off is live. Ctrl+Shift+F9 switches
every RT effect off and on.

**Stability first** (with any hit-material option): the fix `rt_hit_material_recycle` has no GUI entry yet (todo:
expose it in Experimental, then default 1 once `scene_wait` passes). After that: rotate with Q/E at S8 or camp,
shadows must stay on their casters. Not yet confirmed live.

**Water** (S8 Water Queens House, the pool under the temple). Works on Ultra now. In order, each needs the one before:
1. Water geometry capture (live), 2. Water reflections measured (live): no visible change.
3. Water reflections composed (**restart**): columns and temple reflected on the pool.
4. Water sun shadows (**restart**): the far half of the pool in shadow, no black or bright specks.
5. Water local-light shadows (**restart**): almost nothing at S8.
6. Water refraction (**restart**): the pool bed shifts with the view.
7. Water game shadow replaced (**restart**, needs 4): S3 Campsite pond.

**Hit materials** (reflections and bounce of surfaces the screen does not show):
1. Native hit materials (**restart**). Everything below needs it.
2. Wind and two-sided foliage (**restart**, then live): S14 foliage. 2b. RGB wind foliage (startup): S8 bushes.
3. Shaded hidden-surface bounce (live). 4. Sunlit hidden-surface bounce (live, needs 3): S3, never darker.
5. Shaded hidden-surface reflections (live): S8 polished stone; ~7% darker than the screen at S8, 9% brighter at S6.
6. Indirect light in hidden-surface reflections (live, needs 5): rf_debug=8 shows L_a red / L_b green.
7. Emission at ray hits (live, needs Emissive geometry capture at startup): S6 lava in reflections.

**Box lights**: "All box lights" (`ls_box=2`): S6 forge, S7 inn: no light into the next room or through floors.
**FG letterbox** (`fg_letterbox`, no GUI entry yet): fake fullscreen 3840x1600 on a 3840x2160 desktop, FG must show 2x.

Cost: the hit bundle +3.4 to +9.1 ms whole GI; water about 0.6 ms with every option.

## Live batches ready (READY_FOR_LIVE, not authorized), in order

1. `smoke_ultra` (su1, S8, ~4 min): Ultra + every qualified key. Add `rt_hit_material_recycle=1` and
   `fg_letterbox=1` to the job before queuing. Gates: water traced > 0, capacity 0, scene-wait 0 in every window.
2. `scene_wait` (S8 + camp, 176 s each): recycle 0/1/0. Expected with 1: 0 timeouts, wait mean 2-5 ms.
3. `m5_promotion` (S8, Flymm, camp; 16.8 min, ~2.5 GB, delete traces per job) -> merge the water preset if it passes.
4. `m4_promotion` m4p3/m4p4 with `rf_hit_gi` (21.9 min, 1960 MB) and `m4_promotion/s6_diag` (4.5 min,
   replay_pack_cap_mb=3300).
5. `replay_capture/ambient` (camp + inn packs, ~5.5 min, up to 7 GB): native ambient vs reference.
6. `m4_wind_rgb` (S8, 144 s).
7. Box lights visual check (`box_lights`, S6 + S7, ~10 min, 0.65 GB).
Also prepared: `gi_far` gf1 + gf2 (8.5 min), `em_forge` ef1, `m5_water`, `lean_regression`.
