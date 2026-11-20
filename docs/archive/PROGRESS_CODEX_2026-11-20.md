# BG3RTX progress archive - 2026-11-20

Full pre-fold text cut from docs/PROGRESS_CODEX.md on 2026-11-20 (main 08cb1e99): the eight fragments folded that day
(claude/final-core, final-layer, final-tools, leftovers-small, r2-core, r2-layer, r2-tools, release-prep) and the
2026-11-15 PROGRESS they were folded into, verbatim. The condensed version is in [PROGRESS_CODEX](../PROGRESS_CODEX.md).

# BG3RTX progress - 2026-11-15 (main 08cb1e99)

Live tests: the user allowed autonomous lives on the evening and night of 2026-11-14 while the PC was free. That go is
spent: a new session must ask again, and tell the user right before launching the runner. Pending live evidence is READY_FOR_LIVE.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). RTX Hair: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 6g-6k.
Scene handout: [SCENE_HANDOUT_DESIGN](SCENE_HANDOUT_DESIGN.md) 8.5-8.8.
Full text of the 2026-11-14 entries below: [archive 2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md) (night),
[archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md) (day); older: [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md).
Installed test app: 0.9.6-hairtest18 (commit 1c09e039) = main for everything shipped.

## Round 2, stream C: gi_far_roof_emission key, equivalence gate robustness, dlss5 lint, release docs, verify_rc version (branch claude/r2-tools, offline, 2026-11-20)

- `gi_far_roof_emission=0` is now a raw developer key (bg3rtx.ini after `gi_far_roof_early`, generated defaults,
  settings_test release-default list, pipeline_readiness: the GI trace family, spec constant 1054).
  Staged, not queued: `tools/live_batches_staged/r2_tools/` r2-inn-roof-em (inn, Ultra, FG 3x, unfocused, static:
  key 0 / 1 / 0, cost and energy windows, then one GI replay pack for the offline PT reference). predicted.json
  first. Frozen and live lint 1 job, 0 failing; snapshot append only.
- `release_shader_equivalence.py`: the run of 2026-11-20 had no error text because a crashed spirv-opt prints
  nothing and the exception ended the gate. Now `spirv_equivalence.OptError` names the stage, passes and exit code
  (NTSTATUS: 0xC0000005 access violation and others); a failed pair is retried once with one optimizer round on
  both sides (still a proof), recorded as a fallback; otherwise the request is ERROR with entry, assignment, side
  and the kept module (`<cache>/release_equivalence/failed/`), and the other requests run on. A dead worker names
  its request. ERROR exits 1. Default schedule and receipt keys unchanged. CTest `release_shader_equivalence_test`.
- Why it ran 30 minutes: against the worktree the NaN clamp (3d7c8a99) makes all 8 hit_sun_em requests really
  differ, so each failed universal check fell into its 2^13 Cartesian cross (ETA 13 h). The first difference now
  decides a request (without `--exhaustive`): same verdict in 74 s. `--current REV` compares a commit: 163e70bc
  against its parent with 1054 frozen to 0: 6 equivalent (universal), 2 identical, control PASS, 248 s.
  The spirv-opt crash itself did not recur in about 25 minutes of the old cross; the gate now names it if it does.
- Staged dlss5 job (untracked since 2026-11-05) committed: PNG windows 10.5 s (8.1 s held no complete trace), its
  6 readiness queries and the NR GPU pattern appended to the frozen snapshot. Frozen and live lint 0 failing.
- verify_rc: the Setup skip reads the install's `VERSION.txt` (hairtest18) and names a different Apps
  DisplayVersion beside it (script_guards_test). Note: `reg.exe` in the Bash sandbox reads DisplayVersion
  hairtest10, PowerShell reads hairtest18: the RC's name came from such a view; the file read is robust.
- Docs: frame_replay.md and TRACK_C_GI no longer document `--define GI_FAR_M` (now `--push radius=6`); no tool
  uses GI_FAR_M, GI_DEBUG_SWIZZLE or id 1012. RC_CHECK readiness refreshed for e2770961 (candidate 745a1fff, zip
  910c03b6, setup 22da20f2; three new keys off; not-verified 13-15). Release notes, Nexus changelog, evidence:
  the groom decode fix (offline) and 15-50 s of strand-variant pipeline creation at start, also with RTX Hair off.
- Tooling: `perf_ab/make_jobs.py` holds `energy_phase` and `write_made` (4 and 5 copies before; energy phases
  byte-identical). merge_scope's PYTHONPATH (stream A) matches CTest; merge_scope_test asserts it. Staged README
  states; no batch sets only removed keys, so none archived (perf-ab-wide-split is obsolete in purpose, kept).
- Left: the r2-inn-roof-em live (needs a user go); `.scratch/setup-0.9.6-beta/SHA256SUMS.txt` still lists the
  976df3df hashes; the analyzers' window loops (queue_gates helpers already shared, the rest differs per batch).

## BLAS chunk refusals, tracker handle reuse, hair variants (branch claude/r2-layer, offline, READY_FOR_LIVE, 2026-11-20)
- Cause of the 162 refusals (user log 16:47-16:48, 137 cascade-led): at 16:48:11 the chunks held 4038 MB in 26 chunks
  (mean 155 MB, so nearly all larger than a 64 MB base chunk) for 1273 MB live, 0 empty. Small per-draw and cached
  ranges settle in the chunks made for the cascade casters (99-189 MB) and grooms (72-136 MB): fullest-first placement
  takes their tails, and a retained empty chunk joins any arena. One surviving small range pins 100-190 MB that no range
  over 64 MB can use, so the next cascade rebuild needs a new chunk past the cap and the casters are left out.
- `rt_as_size_class` (default 0, live): ranges up to a base chunk only in base chunks, larger ones only in larger chunks
  (`as_chunks::same_class`). Replay of the dolly in `as_chunk_alloc_test` (traffic scaled to the log, 8 seeds x 1380
  frames, cap 4045 MB): 53 refusals (all cascade, every seed) -> 2; reserve mean 3812 -> 3713 MB for 1459 live; the
  colonised large chunks (19-22 of them, 3.4-3.9 GB holding 0.9-1.7 GB) are gone. `rt_as_chunk_fit` made it worse
  (33 alone, 2 with the class key): unchanged at 0.
- `rt_cascade_keep_refused` (default 0, live): the new cascade BLAS is created before the old one is released; if
  refused, the previous BLAS is traced, translated from its own origin (`cascade::rebase`), instead of no casters. Its
  signature, triangle counts and update scratch stay the kept BLAS's, so no refit runs over another caster list.
- Always on: `[bvh] BLAS refusals in N frames: cached, per-draw/groom, cascade (previous BLAS kept, left out)` once per
  300 frames, only when there were refusals. `[mem] ours` adds the free space inside chunks over 64 MB and both keys.
- Review fix (tracker.cpp): `DestroyBuffer`, `DestroyImage`, `DestroyImageView` erased their records after the driver's
  destroy. A `CreateBuffer` + `BindBufferMemory` on another thread that got the freed handle in between lost its
  `buffer_mem` record for good (camera, ring and constant reads of that buffer fail). Records now go first.
- Review, detector only: the 16384-slot validation status ring can wrap onto the done word of a batch still waiting (up to
  240 frames, a recorded command buffer not yet submitted); counted in `[bvh] status ring wrapped onto ...`. Not seen.
- Hair variants: 519 = per G-buffer hair pipeline 1 fallback + 16 strand + 16 probe slots (15 x 33) + 24 prepass,
  velocity and cascade clones. In the user's log the first 132 took 17.2 s and the other 387 0.24 s (same-session
  driver cache hits): lazy creation of the duplicates saves nothing; the cost is the unique compiles that
  `rt_hair_header_early` turns into cross-launch cache hits. Skipping them with RTX Hair off needs a deep copy of the
  create info and the game's shader modules (destroyed after creation): not a release-size change. Left.
- Live check staged (not queued): `tools/live_batches_staged/r2_layer` (r2-inn, inn dolly, both keys 0/1/0, 8.6 min).
  Decisive: ON "free in partly used chunks (N in chunks over a base chunk)" N at most 30 MB against 300-2000 MB OFF;
  refusals ON <= OFF; energy within 0.5%; GPU ms equal. At this machine's 4224 MB cap refusals may be 0 in both states:
  then `rt_cascade_keep_refused` is not exercised there.
- Water M5.4c at Vg = 0 (`common/water_fs_patch.h:699`, coordinator item): Dg = D (1 - Vg) / Vg is 0 x inf = NaN
  there, the compose rejects the record (`g_ok`) and keeps C. No small fix: D = Vg D_u, so at Vg = 0 the FS holds no
  D_u (lum(D) is 0 too); D_u needs the cloud-layer phi chain of Lsun rebuilt with Vg = 1 (its products sit in branch
  blocks that do not dominate the stores). A guard Dg = 0 changes no pixel (the record still has no sun term: `sunrec`
  false), only the gsh counters. Not changed; the fully game-shadowed water keeps the game's shadow.
- Stream C: `pipeline_readiness.py` gained the two keys (host only, no pipeline).

## Round 2 core and shaders: skipped unread work (sun roof test, RF probe at w = 0, GI temporal in RR), two early-return fixes, water passes without readers (branch claude/r2-core, offline verified, 2026-11-20)

Fixes (image identical unless noted; no ABI change, no new key, `shaders/variants.txt` delta 0):
- `rt_shadow.hlsl`: the roof-volume test (up to 512 volumes per traced pixel) runs only where `rt_sun_resolve`
  reads it: vis != fold, t_off <= 0 (roof-query sizing), roof mode != 0, a debug view or the trust counters.
  Proof: with vis == fold the resolve writes lerp(mask, lerp(fold, vis, trust), s) = lerp(mask, vis, s) for any
  trust (fold + trust * 0); the sizing reads `roof` only where t_off <= 0. Cost: not measured; the
  `kSunTrace` gpu line shows it in the next Ultra live (most pixels have vis == fold).
- `rt_reflections.hlsl` (compact probe state, Ultra): the probe term is skipped where w = 0 (rough, miss,
  hidden, off screen, aborted) unless validation, rf_debug or rf_stats read it. Output bit-identical for finite
  probe terms. Inn pack 093603-61 (spec 1031=1): one dispatch each, baseline and candidate bit-equal to each
  other and to live (specular, diffuse, hit distance, counters). Paired 40-pair replay: -0.034 ms (95% CI
  -0.037 to -0.027); the known sustained-replay 1-ULP instability shows on both sides (A 4/40, B 2/40).
- `gi_temporal.hlsl`: history 1 (RR mode, Ultra) writes the fresh sample directly: no MV, history or depth-slope
  loads at full resolution. FMix(hist, cur, 1) may evaluate as hist + (cur - hist); cur is the intended value
  (rounding-level change only, energy equal).
- `core/water.cpp`: water_primary and water_reflect (two full-screen passes) run only when a reader exists
  (rt_debug 21-24, stats, WATER_RR_HIT). Experimental water only.
- `core/gi.cpp`: a refused rt_debug dispatch no longer skips run_post's tail (history parity, closing barrier,
  restore_bindings); a refused pan_primary restores the game's bindings before returning. Debug paths only.
  Identity vs e2770961: 201 / 225 byte-identical, the 24 changed are the three files' requests. 238 / 238 CTests.

Found, not fixed (owner or A/B needed):
- Water game-shadow record (`common/water_fs_patch.h:699`, layer): Dg = D (1 - Vg) / Vg is NaN at Vg = 0, so fully
  game-shadowed water pixels are never restored (`water_compose.hlsl:500`). Store D_u (1 - Vg) instead. Stream A.
- `rr_post_temporal.hlsl:51`: the history depth test (3x3 range + 2 ulps) rejects most taps under camera motion;
  energy-safe, little smoothing. `rr_post.hlsl:250` (non-temporal `final`, Low-High) lacks the jitter offset that
  `temporal_final` applies. Both image-changing.
- `core/gi.cpp:3650-3672`: rt_shadow waits for the whole GI chain; it reads none of its outputs. Recording it
  right after rt_origin may overlap 0.05-0.2 ms (GPU A/B needed).
- GI ray cone `pa * 2.0` (`gi_trace_rt.hlsl:141, 313`) assumes half resolution; in RR mode (full res) the
  alpha-test footprint is one mip too coarse. Image-changing, small.
- CPU: `stats_enabled()` (5 lookups, ~25 calls per frame) and ~100 raw cfg lookups in run_pre/run_post: about
  40 us per frame together; a per-frame memo like `core/frame_memo.h`.
Read and clean: GI screen part of gi_trace_rt (1-355, 737-end), gi_composite, gi_ao_apply, gi_contact, gi_trace,
rt_origin, rt_sun_resolve, rt_local_shadows (all variants), rt_reflections, water_compose / view, rr_post,
rr_guides, rr_strand_aa, hair fibre passes, hair_primary_*; every dispatch grid in core/*.cpp matches numthreads.
Tools (stream C), `--freeze-spec`: no crash in 3 min on hit_sun_em (baseline 967b68cc). `spirv_equivalence.opt()`
prints stdout+stderr only, so a native spirv-opt crash has no text (add exit code, input); after a first DIFFERENT
it still checks every Cartesian assignment (12292 here, about 3.5 h) instead of stopping.

## Final consolidation, stream C: launcher, release docs, verify_rc, tooling (branch claude/final-tools, offline, 2026-11-20)

- Launcher defects fixed (tests in `tests/launcher_ui_test.js` / `.py`):
  - The GPU list (Help & support, 2+ adapters) was rebuilt and closed by every refresh while shaders or hair styles
    prepare (every 2 s). It is now held like a dragged slider until a choice or blur.
  - A WebView page that did not load within 60 s closed the launcher with exit 2 and no window. The player now gets
    the native window (`failed` -> -1); review shots still fail with 2.
  - A preset change listed every moved setting in the strip (6 lines at 640 px). It names three, counts the rest and
    shows all in the tooltip.
  - Preparation's lead said "Runs in the background"; the converter is in the launcher's job object and stops when
    the window closes. It now says it runs while the window is open and continues where it stopped.
- Reviewed, no defect: rapid clicks (absolute values, serial bridge), Undo after preset/opt-in, Retry, restart
  indicator after a game restart (session token), missing game folder, WebView missing -> native. While the game
  runs: one 2 s timer, two process snapshots, one `*.hcap` listing and two small status reads per tick; no WebView
  work when unchanged; minimised = WebView suspended.
- Headless Edge: 21 views x 1280x800, 1000x700, 640x700, 0 layout problems; shots in `docs/ui_rework/shots_final/`.
- Docs: release notes and Nexus changelog no longer name an RTX Hair switch on Home; Nexus description and sticky
  post: no Save, "Choose folder…", keys on Hotkeys (F7 added), four experimental options, no "Disable other Vulkan
  overlays" checkbox (removed long ago), "Create a report…". High preset text names Low-latency scene (settings.h,
  schema). "Softer at distance" cited from the pixel-LOD study (RTX_HAIR_PRIMARY 5v) in RELEASE_EVIDENCE and RC_CHECK.
  HANDOFF: RTX Hair is an opt-in Beta control, not hidden. SCENE_HANDOUT_DESIGN: duplicate 8.6, stale "off by default".
- verify_rc: an existing BG3RTX Apps entry makes the Setup row SKIPPED with the install named (Result "PASS with 1
  skipped row", exit 2), not FAIL. Shared helper `scripts/release/setup_presence.ps1`; test in `script_guards_test`.
- Tooling: Python CTests whose unittest cases skip now report Skipped (`SKIP_REGULAR_EXPRESSION`), not a pass.
  Dead staged batch `hair_pixel_lod` removed. perf_ab and handout_qual already share `sane_gates.py`; gi_1ray_qual
  keeps its historical m4 energy band (decision done).
- Slowest CTests (s): hair_groom 46.5, release_lifecycle 37.3, ps_suite 32.2, launcher_diagnostics_fixtures 32.2,
  merge_green 23.3, release_install 23.1, pt_reference 20.5, qa_detectors 19.1, hair_primary_gpu 17.4,
  frame_replay_ls 11.7 (380 s summed over 237).
- Not committed: `tools/live_batches_staged/dlss5/` fails lint (frozen: readiness query absent from the snapshot;
  live: PNG windows guarantee 0 complete traces).

## Final consolidation, host layer (branch claude/final-layer, offline, 2026-11-20)
- User log 16:45-16:50 (0.9.6-hairtest18, Ultra, FG 2x, RTX Hair toggled live): no device or swapchain error, no
  `VK_ERROR`, guard quiet except 16:47:15-20 (LOW at 26.4 GB used). Peak used 27.1 GB (16:48:13; 26.5 GB on 14 Nov),
  ours 8.7-15.3 GB tracked. 46 Streamline "Frame rate over 100 ms" warnings, in bursts at loads and live hair toggles.
  Defects found: (1) two groom LODs decoded 9 times each (1.07 s) under five 100 ms hitches; (2) 162 BLAS chunk refusals at
  the chunks' cap, 137 in windows led by the cascade casters (left out those frames; 4.0 GB reserved, 1.2 GB live); (3) 17.4 s of strand-variant pipeline
  creation at launch (19.6 s in the 14 Nov log).
- Groom decode (fix, on): the pool range is taken from the index's counts before the decode; a refused pool growth
  no longer throws a 30-190 ms decode away each build. Check: no LOD id with several "decoded" lines before its
  "uploaded" line.
- Tracker: freed sets and the sets of reset or destroyed pools leave `sets`, `dynamic_counts`, `set_layouts`
  (descriptor_epoch_test). `detailed_logging` adds `[tracker] descriptor sets N ...`: flat over a long session.
- `[mem] ours` now splits the BLAS chunk reserve: pending, free in partly used chunks, per arena, oversize chunks.
  The cause of item (2) is not settled offline: in vr-inn 23:38:05 (still) 2157 MB in 28 chunks held 554 live,
  2 pending, 360 empty, so 1241 MB were holes in partly used chunks. A Python model of the arenas (grooms 72-136 MB
  compacted to about 30%, cascade 90-155 MB, 600 per-draw slots) gave no gain for "empty chunks only for ranges of half
  their size" or a separate groom arena (-5% to +5%): no key shipped. The next user log names the split.
- `rt_hold_pack_base` (default 0, live): the 24-byte hold pack also for the 64-byte layout. Predicted: 91.5 -> 34 MB
  written per static-camera build at S8, 57 MB less pool, about 0.05 ms GPU. READY_FOR_LIVE.
- `rt_hair_header_early` (default 0, startup): the strand header is made at device creation. Its address is a
  specialization constant of 519 variants; a new address each launch missed the driver cache: 14.7-48.5 s in 18 of 25
  queue launches, 190-501 ms in the 7 whose address repeated. READY_FOR_LIVE: two launches with the key, same
  `header at 0x...` both times and `strand variants: 519 pipelines in` under 1 s in the second.
- Small: `log_stats` gate cached per cfg_poll; cascade draws read two keys through CfgHot; `rt_hair_dense_sample` maps
  pruned. Kept: `log()` flush per line (9 lines/s in release, the runner reads the live log, crash safety).
- Left, with reasons: mesh pool repack of grooms (the groom BLAS, the camera BLAS and header rows hold point addresses:
  a move is a rebuild, not small); pixel-LOD camera BLAS compaction (key off in every preset, about 16 MB per groom);
  refit skip for unchanged per-draw BLAS (needs a GPU content stamp); deferred strand-variant creation (the create
  infos die with the call: a deep copy of the chain); dead-code sweep not started.
- Stream C: add `rt_hold_pack_base=0` and `rt_hair_header_early=0` to bg3rtx.ini / settings.h (Advanced).
- Live check (staged in words only): S8, static 2 x 120 frames with `rt_hold_pack_base` 0/1/0: `hold_pack_bytes`
  up, `hold_copy_bytes` down, energy within 0.5%, 0 held-draw rejections `why 2`; plus the two-launch header check.

## Final consolidation, core and shaders: dead code out, refresh lookups once per frame, NaN-safe giRaw clamp, hidden-hit emission roof key (branch claude/final-core, offline verified, 2026-11-20)

- Dead code removed: `GI_DEBUG_SWIZZLE` and `GI_FAR_M` blocks (gi_trace_rt.hlsl), the unreachable `kCascade` request
  branch (shader_variant.h: every kCascade file is a cascade entry, so the constant carries it) and spec id 1012
  (retired, never reuse). 225 / 225 requests byte-identical to 8d870bf4. `shaders/variants.txt` delta 0.
- Kept: `RT_SCENE_REBASE` / `RT_REBASE_LIGHT_CAMERA` (dead at run time, but `tools/native_hair_gpu_probe` compiles
  its `query_rebase.spv` with them) and the `RT_GI_FAR` define build (`tools/spec_cascade_check.py` reference).
- `refresh()` (core/gi.cpp) reads its 14 cfg keys and 6 persist words once per frame index (`core/frame_memo.h`,
  CTest `frame_memo_test`; build and 238 / 238 CTests pass). About 25 calls per frame, about 0.1 ms CPU estimated before; not measured live.
  A key change applies from the next frame. No ABI change (host `frame_index` already exists). Image unchanged.
- giRaw clamp (gi_trace_rt.hlsl): `select(x <= 65504, x, 65504)` turned a NaN into 65504. Now overflow and +Inf
  store 65504, a NaN stores 0 (fallback0's bounce). Not reachable today. Proof: only the 28 hit-GI requests change;
  their id-agnostic SPIR-V differs in the final select only, equal for every finite input.
- `gi_far_roof_emission` (spec constant 1054, default 0, live, raw key): the far roof rule now also for the emission
  of a hidden hit beyond the base radius (the screen branch already drops such rays). Physically the cut-away roof
  would hide that emitter: the kept emission is a leak. Off: the em files specialize to the old program
  (identity: only the 6 hit_sun_em requests differ; with 1054 frozen to 0 and every other constant symbolic, 6 / 6
  equal the baseline after the shared spirv_equivalence normalizer. `release_shader_equivalence.py --freeze-spec`
  crashed in spirv-opt after 30 min on its exhaustive cross: a tools item).
  A/B to stage (not run): PT reference at a roofed interior with an emitter outside (inn), key 0 / 1 / 0 in one run;
  predicted: GI energy lower or equal with 1, closer to the PT reference; cost within noise (the roof loop runs
  only for emitters beyond 1 m).

Read, no change:
- Hit-GI loop (gi_trace_rt.hlsl 355-737, release path raygen + far + sun + em): no redundant traces; ray count is
  the push constant; stats and SHaRC work are behind uniform branches. Small items left: the raygen prepass
  projects its two hits again in the loop (ALU only), `hit_material_publication_ready()` is one load per hidden ray.
- Handout EXPLAIN (hq-s8-ultra 215258, GI trace -0.36 ms): the core counters show the hit wait on every handout frame
  (hit passes on the committed scene = handout frames +-1 per 120), so the hit GI traces the committed pair as OFF.
  The ON pipeline is another specialization (constant 1051: traces by stamp address), so its register allocation
  differs; hq-s8-modes 205520 read equal. Benign. Small gap: the frame check uses the present counter, which FG
  presents advance; the counters show it does not happen in practice.
- GI images (2560x1067, GI at full resolution): about 202 MB, all bound by the push sets. Hair origin (43.7 MB) is
  already 1x1 unless RTX Hair's `rt_hair_physical_origin` is on at startup; water images are lazy; SHaRC allocates
  nothing without its headers. Skipping rf_hit (5.5 MB), rt_vis (10.9), origin or aux (21.8 each) needs 1x1 dummies
  and a reallocation on live toggles: not done before release.
- Per-frame descriptor writes: push descriptors per dispatch and the sun set ring (9 writes) only. Pipeline builds:
  no new cause found beyond the known startup burst, the `_em` switch and the RTX Hair hotkey.

## Release 0.9.6-beta prepared offline: audit, notes, candidate, staged final smoke (branch claude/release-prep, READY_FOR_LIVE, 2026-11-15)

- User decision 2026-11-15: finalise for release, RTX Hair Off by default. Nothing uploaded, tagged, installed or queued.
- Audit of `common/settings.h`, `bg3rtx.ini`, `data/release.ini` and `enforce_release_policy`: no inconsistency found,
  no settings change. A release player sees 23 controls, 4 more behind "Show experimental options" (Water surface
  reflections, two Frame Generation pacing tests, Strand hair edge resolve); 32 developer knobs stay hidden.
- RTX Hair: Off in Low, Medium, High, Ultra and without a user file; a Lighting control with the Beta badge; On keeps
  all 26 bundle keys under the release policy in every preset with experimental options 0 and 1.
  `test_release_rtx_hair` now pins this (it covered Ultra with experimental options 1 only).
- Diagnostics: `rt_hair_dense_sample` pinned 0 by release.ini; `rt_hair_card_fill` is a hidden developer knob reset
  to 0; `rt_hair_groom_async` and `rt_as_chunk_fit` are raw keys at 0 with no control.
- Presets: Low-latency scene and RTX Hair motion stability On in High and Ultra; the four VRAM policies 1 everywhere.
  Without a user file the package resolves to High without OMM and masks (the launcher writes a preset on first use).
- Version: 0.9.6-beta (the repo's published names carry -beta); `tools/launcher/version.h` was already 0.9.6.0.
- Docs: `docs/release/RELEASE_NOTES_0.9.6.md`, `RELEASE_EVIDENCE_0.9.6.md` (source of every number),
  `nexus/changelog_0.9.6.txt`; player README, USER_SETTINGS, Nexus description and sticky post updated.
- `verify_rc.ps1`: the payload guard ran `python` (Store alias, exit 1), now `py -3`; default file count 230 (was 223);
  the hand-written readiness section of RC_CHECK.md survives a rerun.
- Candidate from commit 976df3df, lean layer: `dist/BG3RTX-0.9.6-beta.zip` (230 files, 74.0 MB),
  `-symbols.zip`, `.scratch/setup-0.9.6-beta/BG3RTX-0.9.6-beta-Setup.exe` (57.7 MB, unsigned), all in the main
  checkout with `SHA256SUMS.txt`. Hashes in `docs/release/RC_CHECK.md`.
- Verification: verify_rc 11 of 12 PASS (build, archive, manifest 230, hashes, exclusions, licences, version, install,
  update, uninstall, cleanup). Setup row FAIL only because the user's install owns the Apps entry: the compiled
  setup was never executed. Extra checks: 0 granny2, licence files byte-equal, release pins effective in the packaged ini.
- Final smoke staged in `tools/live_batches_staged/release_final/` (predicted.json first): `final-s8-ultra-high`
  (Ultra then High at S8, about 3.8 minutes) and `final-stock-hair-dialogue1` (about 3.4 minutes). Frozen lint and
  live preflight 0 failing; snapshot +3 queries, append only. New gates: 0 handout fallbacks, 0 dropped opaque draws.
- Tests: `scripts/build.ps1 -Test` 237 of 237; merge_scope tooling exit 0. `shaders/variants.txt` delta 0, no ABI change.
- Open, in RC_CHECK.md "Not verified": no live in the shipped profile since 2026-11-14 (blocker unless the user accepts
  their own hairtest18 sessions); Setup.exe never run on this PC (blocker for publishing the setup, not the ZIP).
- Stale elsewhere, not edited here: HANDOFF says RTX Hair is "hidden in release"; it is a visible Beta control.

## Leftovers landed: licence attributes, two spec tools, three result notes (branch claude/leftovers-small, docs and tools only, 2026-11-15)
- `.gitattributes`: `docs/release/licences/** -text -whitespace`. Licence evidence keeps its stored bytes (9 files
  CRLF, 9 LF in the index); `git status` shows none of them modified after the change, nothing renormalised.
- `tools/spec_omm_alias_probe.py` (from origin/variant-migration) runs unchanged: 3/3 modules valid, control 33
  differing lines, both alias values equivalent.
- `tools/spec_constant_proto.py` (from worktree-agent-a0dba2e7) needed one fix: today's old modules carry the later
  spec constants (1001+), so the old side is now frozen and folded like the new side, and canonical() comes from
  `tools/spirv_equivalence.py` (the doc references say so). Result today: spec 0 = old plain, spec 1 = old cascade
  (873 globals, 24802 body lines, 0 differ), control 4 body lines; DXC 6.3 s for two modules vs 3.3 s for one.
- No runtime, shader, setting or CTest list change. Cost 0 ms.

## ReShade live rs1 was INVALID for its question (branch reshade-compat, still unverified, 2026-11-05)
- rs1 (Water Queens House, `sl_init_after_create=1`): `[compat] ReShade not loaded`. ReShade 6's global layer needs
  a ReShade.ini in the game folder, and it was removed before the run. The ReShade case is still unverified.
- Without ReShade the key was harmless: trigger = the game's NGX Init, `slSetVulkanInfo: ok`, FG 2.00, no mini-dump.
- Next: one rerun with ReShade enabled for BG3 (the user's choice), then default 1 if the gates hold.
  Full text: TRACK_B_FG.md, after the FG settings table.

## Strand density: the tips dither is the measurable coverage loss (branch claude/hair-density, offline, 2026-11-07)
- LOD, converter and radius are not causes. With `rt_hair_groom_tips=1` the strand/card pixel ratio of the main draw
  is 0.931-0.934 against 0.952-0.973 with tips off; small draws 0.79-0.84 against 0.93-0.97 (under the swap bound 0.891).
- Kept: `rt_hair_groom_tips=0` (no code). Full text: RTX_HAIR_PRIMARY.md after 5d.

## Primary-only minimum strand footprint: rejected offline (branch claude/hair-footprint, REJECTED, 2026-11-07)
- Widened strands with a keep-dither: accumulated residual 0.079-0.20 against 0.059 for today's thin single ray,
  mean 0.250-0.329 against the reference 0.332. The DLSS jitter already stratifies the thin strand's sample.
- Rejected before any build. Table and reasons: RTX_HAIR_PRIMARY.md after 5d. The branch's `test_footprint_rejected`
  was not landed: on main its any-hit check fails (-3.6% against its 3% bound), and the bound is not ours to tune.

## 2026-11-14, night: VRAM right-sizing and the consolidation round

### VRAM right-sizing: four policies On by default (branches claude/vram-rightsize, vram-rightsize-2, vram-result)
- Evidence: the user's play logs of 22:12-22:27 (Ultra, FG 2x, RTX Hair On): ours 10.2-12.6 GB of 23-27.5 GB used, and
  26 hitches over 100 ms in 10 s starting with a Streamline `waitCPUFence` timeout at 26.5 GB.
- Five policies behind live keys, no shader or ABI change, 0 GPU ms:
  - `rt_capture_demand_size`: RT capture sized by the need without the held 40% alpha region. On (-0.6 to -0.9 GB).
  - `rt_capture_peak_reserve`: the rise reserve no longer stacks on the high level. On (-0.05 to -0.43 GB).
  - `rt_capture_split_mb`: presets by the 64-byte base stride (RT 1024 MB, cascade slot 427 MB). On.
  - `rt_mesh_pool_trim`: a shorter pool with the same offsets when its end is free and a quarter is saved. On.
  - `rt_as_chunk_fit`: stays 0. Predicted 0-200 MB of empty chunks, up to 911 MB seen; 22 chunks created in the inn
    dolly-out with the key, 22 and 23 without. Prediction disproved: no saving, no harm.
- Live `vr-s8.1.232616` and `vr-inn.1.233221` (Ultra, RTX Hair On, FG 3x, 22 of 22 phases; INVALID only because the
  job's `post` used `python`, which hit the Store alias; the analyzer run by hand reports 0 missing observations).
  VRAM used 16422-16456 -> 15648 MB at S8 (-0.8 GB) and 18587-18895 -> 17697 MB at the inn (-1.0 GB).
  RT capture 1408 -> 736 MB (S8), 1776-2047 -> 1184 MB (inn; dolly peak 1776). Cascade, 3 slots: -50 to -430 MB.
- Faults in every state of both runs: 0 dropped draws, 0 buffer full, guard normal, 0 refusals, 0 scene wait timeouts,
  0 handout fallbacks, 0 strands to cards, no `VK_ERROR`. GPU ms equal (S8 RT 10.98 on, 11.38 off; inn 12.93, 12.93).
- Energy inside noise: S8 -0.25%, inn -0.18% (floor 0.5%). Shrink hitches 36-56 ms (trims 38 and 48 ms), rare.
  The inn dolly-out has 209-248 ms hitches in every state: the dolly's own BLAS builds.
- Launch with the defaults confirmed afterwards (hx1 001645, coordinator's reading, no fragment): RT capture first
  buffer 1024 MB, cascade 3 x 427 MB, 0 frame-watchdog trips, 0 dropped draws, guard quiet.
- Open: after a dolly the inn's BLAS chunks hold 2.3-2.7 GB for 0.65-0.8 GB live (back to 0.9-1.0 GB after about
  15 s still): the largest reserve in motion, untouched. The mesh pool cannot shrink at the inn while a groom sits at its
  end (2047 MB for 690 used). Pixel-LOD camera BLAS not compacted. `rt_as_cap_occupied` stays 0 (never run).
  The user's 26.5 GB sessions predate these fixes: re-check from their next play log.
- Check: `[vram]` lines are always on; `detailed_logging=1` adds `[mem] pools reserved/used MB` (RT capture 700-1200 MB
  at rest, not 1408-2047). Task Manager: dedicated GPU memory of bg3.exe 0.8-1 GB lower in the same place.

### Consolidation, host layer (branch claude/consol-layer, offline; no shader or ABI change)
- Elastic capture growth: captures grew in 16 MB quanta, each a whole new buffer (ten RT reallocations of 1.4-1.8 GB
  for +16 to +112 MB at 22:15, guard LOW twice). A growth is now at least 1/8 of the size when the budget allows:
  the same sequence takes 3 reallocations and ends at 2016 instead of 1840 MB. CTest `rt_capture_elastic_test`.
- `rt_hair_groom_async` (new key, default 0, live): groom read and LZMS decode on a below-normal worker instead of inside
  the scene build (228 decodes in two sessions, median 47 ms, max 203 ms). The draw keeps cards a few frames longer.
  READY_FOR_LIVE, unverified live: decode ms must leave `[rt] scene wait` max; card runs must stay in the hx1 gate.
- Shader source scan on the present thread: every 5 s with dev=0 (was 250 ms; 0.3-0.8 ms per scan). Unchanged with dev=1.
- Streamline: the layout of a destroyed FG input that left the watch ring is erased (stale layout on a reused handle).

### Consolidation, core and shaders (branch claude/consol-core, offline, READY_FOR_LIVE)
- Every level load allocated the 13 GI images (about 180 MB at 2560x1067) twice, at half then full resolution, and cleared
  the history twice: `rr_active` goes false during a loading screen. Seen in 12 of 12 queue sessions and both play sessions.
- Fix `core/rr_mode_latch.h`: the GI keeps its RR mode while RR is inactive for less than 30 presents. A real fallback
  (SR, FSR, TAA) switches after the hold; `gi_rr` 0/1 overrides at once. CTest `rr_mode_latch_test`. No shader change, no key.
- Check: after loading a save from the menu the log has no new `gi: resources` line (before: two). The startup pair stays.
- Removed `shaders/probe.hlsl` and `shaders/rr_hair.hlsl` (nothing requests them). `shaders/variants.txt` delta 0.
- Found clean: no pipeline rebuild without a cause; no core memory growth; history and guide stores are guarded.

### Consolidation, launcher and tools (branch claude/consol-tools, offline)
- Launcher: the 2 s refresh sends a state only when it changed (was 30 KB and a page rebuild every tick). A slider drag
  is no longer cut by the refresh. Rendered frame limit On starts at the preset's 58 fps (was 30). The hair converter
  runs at idle priority while the game runs; Play stops the precompile and the converter at once.
- Layout audited in headless Edge at 1280x800, 1000x700 and 640x700 (`node tools/launcher/ui_shots.js <out dir>`,
  0 problems; shots in `docs/ui_rework/shots_review/`). At 640 px all pages are reachable. Detail texts are player-facing.
- Tests: 16 PowerShell tests (`ps_suite_test`) and 6 Python tests that nothing ran are in CTest; two had rotted and are fixed.
  New `test_old_user_files`. All 355 keys of `bg3rtx.ini` are read by layer or core (3 are launcher keys).
- Live lint gates the 17 active jobs (0 failing) and counts 107 historical ones apart (the old "71 failing" were historical).
- Scripts: `build.ps1 -Test` and the Setup check name Smart App Control when it blocks fresh exes; `remove-worktree.ps1`
  refuses outside the main checkout and refuses test-main; a job `post` written with `python` runs through `py -3`.
- Not done: the launcher was never opened in the real WebView2 window; the minimised-window suspend is unseen there.

### Findings reported, not fixed
- `layer/tracker.cpp:515-523`: `Tracker::sets`, `dynamic_counts`, `set_layouts` are never erased on free, reset or pool
  destroy; `layout_dynamic` never on layout destroy. Growth unmeasured (needs a counter line in a long session).
- Hold snapshots copy 91.5 MB per static-camera build at the 64-byte stride (247 copies; the 24-byte pack exists only at 88).
- 282 draws are re-captured and refit every frame with a static camera at S8 (capture hooks 6.1 ms CPU over all threads).
- `refresh()` in `core/gi.cpp` runs about 25 times per frame (about 0.1 ms CPU, estimated); needs a host settings generation (ABI).
- Hidden-hit emission beyond the roof bound is kept at `shaders/gi/gi_trace_rt.hlsl:531` and dropped at `:460`: needs a PT A/B.
- `gi_trace_rt.hlsl:746` maps a NaN to 65504 (not reachable today). Dead `#if` branches (`RT_SCENE_REBASE`,
  `RT_REBASE_LIGHT_CAMERA`, `GI_DEBUG_SWIZZLE`, `GI_FAR_M`) are left for a pass that already recompiles `rt_common.hlsli`.

### Disk cleanup (night, with the user's approval; coordinator's report, no fragment)
- About 311 GB of files deleted (old captures, `.scratch/hair-c`, old setups and packages, `hair_grooms_v2_20261110`,
  merged worktrees); 164 merged remote branches and 531 local ones deleted; tags untouched.
- ANOMALY: free space on E: still read 482.3 GB afterwards by every unelevated method. Unexplained; the user was asked to
  check in Explorer and with `vssadmin list shadowstorage`.
- Kept: all 8 replay packs (several are referenced), the hairtest16-17 setups, `.scratch/saved_from_worktrees/`.

## 2026-11-14, day (one line each; full text in [archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md))
- RTX Hair instability under camera motion solved for converted styles: `rt_hair_header_cam_gpu=1`, `rt_hair_header_scene_pass=1`, `rt_hair_coverage_keep=1` in the 26-key bundle; hx1 202905: 0 strands->cards in all six windows; entering heads show cards for about 3 frames. Six obsolete keys removed. Still Off in every preset.
- Open for hair: styles without a groom file stay cards (S8 index counts 3399, 7086); hitch frames fixed offline only; `rt_hair_card_fill` is an Advanced option with no visible gain.
- Scene handout ("Low-latency scene") On in High and Ultra with `rt_scene_handout_hit_wait=1`: scene wait 3.5 -> 0.3 ms (S8), 8.5 -> 2.9 ms (inn); frame time unchanged on Ultra, about -0.5 ms on High; energy equal; 0 fallbacks. The first "1 to 3.5 ms" gain was dropped work and is withdrawn.
- Sensible gates (`tools/live_batches/sane_gates.py`): FAIL only beyond the OFF noise plus a floor; an unpredicted drop is EXPLAIN.
- Performance investigation: the release profile is not slower than on 2026-11-05 (GI much cheaper); the VRAM guard latched LOW after the OS halved the budget, fixed in `common/vram_guard.h`, unconfirmed in play; per-draw hair mutex removed; queue log pruning fixed.
- Launcher "Direction A" with instant apply merged and installed; rendered only in headless Edge.

## RTX Hair, 2026-11-10/11
- Motion stability ON in High and Ultra (`rt_hair_mv_card=2`, user decision; needs a restart). hm1 camera-only share 11% (off 98%), ratio 0.77.
- Detail at distance (pixel LOD, Off / 1/4 / 1/8, applies live): heads 35-60 px keep 1/4; strand G-buffer -27% (1/4), -34% (1/8). Camera BLAS about 16 MB per groom, not compacted.
- Holes under a fast dolly: cause 1 found then (`rt_hair_header_cam_gpu`), cause 2 on 2026-11-14 (above). Other branches of those days: see the archive.

## Current user decisions
- Stock visible hair ships; strand hair hidden until its post-release update. Experimental page in release: only Water surface reflections (off).
- The game-derived asset database ships in the archive. GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP plus portable ZIP; manual download. Lean: only the tests that are really needed. Optimisations that qualify are always on.
- 2026-11-11: RTX Hair motion stability On in High and Ultra. 2026-11-14: launcher Direction A, instant apply, dark only.
- 2026-11-14: Low-latency scene On in High and Ultra, always with the hit wait fix ("Si, accendilo", "Tienilo acceso").
- 2026-11-14: sensible gates (fail beyond noise plus floor). Lives allowed that evening and night while the PC was free; ask again in a new session.
- 2026-11-14 night: the four VRAM right-sizing policies On by default after the live result; disk cleanup approved.
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
