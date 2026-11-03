# BG3RTX progress - 2026-11-02 evening (main 7929040, ABI 26)

One current summary per area, no history. Everything older, verbatim: [archive/PROGRESS_20261102_morning.md](archive/PROGRESS_20261102_morning.md)
(the 19 folded fragments of 11-01 / 11-02 and the summary before this rewrite), [archive/PROGRESS_20261101_evening.md](archive/PROGRESS_20261101_evening.md),
[archive/PROGRESS_20261101.md](archive/PROGRESS_20261101.md), [archive/PROGRESS_CODEX_2026-11-01.md](archive/PROGRESS_CODEX_2026-11-01.md),
[archive/PROGRESS_CODEX_2026-10-31.md](archive/PROGRESS_CODEX_2026-10-31.md).
Branches write `docs/progress/<branch>.md`; `py -3 scripts/fold_progress.py` folds them here. Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md).
Goal and stage order: [GOAL_NEXT.md](GOAL_NEXT.md).

## Report 2026-11-02 19:30 (main 7929040, ABI 26) - consolidation paused, PC freed for the user
- Merged: stream caps + source trim (-0.4 GB VRAM), NaN GI history fix, OMM option B + VT alpha masks (game-wide catalogue 1,677 materials, generated on the user's PC), recipe rows from the asset DB, water RR/FG motion vectors, preprocessed-text shader cache (header-only edit: 2892 compiles to 0, 35 s), AGENTS.md compile rules. All new keys off.
- Rejected / stays off: IP grass (+1.9 ms, +1.5 GB, +3 points share), generated recipe rows (+2.3 points camp), every water flicker key. rf_hit_gi off (hit AO matches the screen offline; gap is in instanced hits, needs a pack with instanced rows).
- Water flicker at the port: closed, it is the game's own water (wf3-wf7: our compose, RR, sun-shadow ray, glints and FG all excluded; with RT the water is calmer, 0.54 vs 0.77-0.86).
- Dialogue shadows: RT AO darkens characters most (AO off +18..42%); at the port opaque hair cards add to it (native hair +27%). Hair cards are not alpha-tested in RT at all (no CHAR_Hair shader in alpha_fs.txt).
- OMM: live upper bound camp ~5.7-6.3 ms (forced opaque). Masks cover ~97% of camp alpha candidates. Dynamic micromap bake built (rt_omm_dyn, worktree-agent-a53f0b584fa39317b), not yet run: om4 waits.
- In progress (user rule: finish the build improvement before more big compiles): spec-constant migration on `variant-migration` (G guards + S2a done; 2986 requests on main to ~980; target ~100-180). Then one merge-green, live scc1, then rebase and run: occ diagnostic + dialogue batch, native hair (async compile, water hair) + hair_qual, om4, m4_generated rerun.
- Try in game: S8 port courtyard pool is darker with RT (our sun shadow removes glints in shade): say if you prefer a softer water shadow edge. Native hair at the port dialogue should brighten characters (first enable still compiles until the hair branch lands).
- Questions: see below (ls_box=2 visual check, GUI_AUDIT_20261102 five yes/no, project licence, NVIDIA notification).

## Report 2026-11-02 04:10 (main fe8f75d)
- Done: stage 1 live (smoke_ultra valid with every Ultra key; scene wait 0 timeouts in gated windows; water 120/120 composed; shader cache 2nd launch 44 read 0 compiled; FG letterbox 2560x1440 in 3840x1600 swapchain FG 2.00 -> fg_letterbox automatic). Stage 2 GUI: Experimental 28 -> 5 entries, levels for water/materials.
- Fixed: traced frames skipped the hit-material capture (layer/geocapture.cpp), so all earlier traced energies of hit features measured them off; re-measured after the fix.
- Ultra now: + Bounce, Emission at ray hits (Hidden hits), Far-field sun bounce 6 m (gi_far: S8 -2.01% vs predicted -2.0%, camp -2.42%; +3.2-3.7 ms GI trace). smoke_ultra with all: valid, median frame 16.65 ms (was 13.75).
- M6 step 1: native ambient / PT reference S8 1.44, camp 0.84, inn 0.78: not consistent, no global correction.
- M4: S6 diagnostic: GI hit shading adds light (+1.6% pack invariant), no darkening. Alpha-tested static meshes get material rows (rt_hit_material_pool_alpha, live-tested S6).
- Box lights ls_box=2: energy within band, +0.32 ms S6 / +0.20 ms S7. Needs your visual check (question below).
- Open: rf_hit_shade stays experimental (bias); lava flowmap and camp grass coverage; SHaRC; release hardening (stage 6).
- Try in game: Ultra preset at Water Queens House (S8) and camp: softer far-field bounce darkening under roofs/foliage, emissive lava light in the Adamantine Forge.
- Question: ls_box=2 in the S6 save (Adamantine Forge): the lower-right walkway right of the front disc and the central pillar should darken slightly (ledge shadow). OK to turn it on in Ultra?

## User questions and decisions

- Open: **ls_box=2 in Ultra?** (question above). Until you answer, Box light shadows stay Off in every preset.
- Open: **GUI_AUDIT_20261102** five yes/no decisions; **project licence**; **NVIDIA notification** (Streamline/NGX).
- Open (look): water sun-shadow edge: soften (more rays / temporal visibility) or keep game glint in shade?
- Decided 11-02: OMM path delegated (option B); assetdb and VT masks generated on the user's PC, not shipped;
  gi_reproj guards live approved (now as spec constants); dropped Codex refs may be deleted; packaging paused;
  no compile time on work a planned build improvement makes unnecessary.
- Open (not blocking): **`rt_hit_emission` 1 or 2** (1 = hidden hits only, 2 = also screen hits). Ultra uses 1;
  2 is developer only.
- Decided: **`rf_hit_shade` stays Experimental until its bias is within the game's own spread** (tier (a)
  L_b / L_a S8 0.925, S6 1.089, camp 1.097). The threshold is not lowered.
- Decided: **`ls_k` stays off** in every preset (developer only).
- Decided: GUI audit approved (docs/GUI_AUDIT_20261101.md): levels instead of single keys; emission writes `rt_emissive`.
- Decided: crash dumps stay off for players. Later a diagnostic mode builds a package (logs, effective settings,
  GPU info, crash dump) with a save the user picks, copied read-only; it never reads saves on its own.
- Decided: disk: keep what a test or comparison still reads; delete superseded packs and analysed raw dumps.

## rt_omm qualification prepared: observables + omm_qual camp job (branch worktree-agent-af7061844a7459eec, READY_FOR_LIVE, 2026-11-02)
- Re-check on main 566db3d (ABI 25): both DLLs build, CTests pass, OMM variants compile, key-off shaders
  byte-identical. Nothing was broken; no ABI change.
- New observables (all behind rt_omm=1, off by default): layer `[omm] last N builds` (alpha-tested triangles
  per build, with a capture, with attached micromaps and their %, variants, bytes, cumulative baked
  microtriangle states from a device-local bake counter); core `omm check` (rt_stats=1, rtStats 435..442:
  same-ray visibility / closest-hit compare against the reference scene, violations, candidates per trace);
  live key `rt_omm_reference` (OMM variants trace the reference scene, guard kept) for an in-launch A/B.
- Job `tools/live_batches/omm_qual`: om1 camp rt_omm=1 (diag-check, cost REF / ON / REF, 3 PNG), om2 camp
  rt_omm=0 baseline (cost OFF x2, 1 PNG). About 7.2 min, 0.26 GB. Dry run and lints pass. Not authorized,
  not run. predicted.json: energy null, violations 0, coverage about 0, cost null (sign only).
- Expectation: reject. Only one plain-texture recipe can bake (no VT foliage), and a known state needs
  alpha 1 at every mip down to 1x1, so cutout triangles stay UNKNOWN. When ranges are attached, the guard
  makes five per-lane host-visible atomics per ray query.
- Docs: docs/P4_OMM.md section 1 and 1a.

## rt_omm follow-up: guard cost fix, LOD and VT plan (branch worktree-agent-af7061844a7459eec, offline, 2026-11-02)
- Merged main; the OMM check moved to rtStats 443..447 (5 counters; main took 435..442).
- Guard: scene-current check once per wave and invocation instead of five per-lane atomics per query;
  content epochs in BAR VRAM (`device_local=1` on the log line). Off by default (rt_omm=0).
- Coverage at camp (lr1 alpha table): the one audited recipe is 0.06% of about 700k alpha triangles; VT
  738494714d50b4c7 is 91%. The classification rule cannot raise that; VT masks can.
- Not done (not small): classification at the any-hit LOD. The LOD is per ray, so a static state agrees only
  under a footprint gate (option A, exact) or a fixed fine-LOD policy for baked materials (option B, image
  change). Plan with costs, and the offline VT mask plan, in docs/P4_OMM.md section 11.

## Offline VT alpha masks for camp grass (branch worktree-agent-af7061844a7459eec, READY_FOR_LIVE, 2026-11-02)
- Key `rt_vt_alpha_masks` (live, off; GUI Experimental "Offline cutout masks"): matched VT records of the camp
  grass shader 738494714d50b4c7 read offline BC4 masks (data/assetdb/vt_alpha, ADDED only) instead of the tile cache.
- Masks: 7 textures, 1.65 MB, extracted with LSLib from the game's tile sets (tools/assets/vt_alpha_masks.py).
- Offline check on the camp pack: bit-exact where the page is resident; alpha test agrees 98.51% (1.48% coarser pages).
- Option A share: 1.15% of shadow-ray pairs at best, ~0% of GI rays at camp distances: not worth building.
- shader_identity_test flake fixed (sqlite WAL switch ignored the busy timeout; retried). 97/97 CTests.
- Live job tools/live_batches/vt_alpha (camp OFF/ON/OFF, 4.1 min); not run.

## Water reflection flicker: rt_water_rf_edge screen-edge fade; water_flicker wf2 job (branch worktree-agent-a81baa03b972b117a, READY_FOR_LIVE, 2026-11-02)
- Cause in code (shaders/rt/water_compose.hlsl): the compose switches each water pixel per frame between the traced
  term (tier 1: C + A L - B) and the probe (C) with no weight and no temporal smoothing. A L and B share the weight E,
  not the radiance (bins 29-31 log L / probe). Under a turn the screen border moves every frame, so hits crossing it
  flip pixels between two radiances. D-RF fades at the border (rf_edge); the water passes did not.
- Fix: key `rt_water_rf_edge` (0, live): WATER_RF_EDGE compose variants (7 requests + cascade twins in variants.txt),
  C' = C + w (A L - B), w = D-RF's edge fade (rf_edge 0.05 uv). Key off: 19/19 water requests byte-identical to main
  (tools/shader_identity.py); key on: 14/14 compile and pass spirv-val. Build + 96 CTests pass.
- Job wf2-s8-fg2 (replaces wf1): A/B of rt_water_rr_hitdist and rt_water_rf_edge in one launch. All 4 turns in the same
  direction from the same pose (ret-* turns back between them); every key toggles 9 s before its burst (a plan step's
  set applies at capture_at: wf1's rot-on burst started at its toggle); rt_debug 21 coverage traces at the static pose
  and in the turn as masks (new `pan_artifacts --mask`, coverage_mask unit test). 199 s, 5 bursts (about 11.5 GB,
  deleted after the analysis). Dryrun + lints pass. predicted.json: shot-rr and shot-edge energy null, cost 0;
  rot-edge masked transient rate below both rot-off by more than the OFF-OFF spread.
- Water energy: the gate's dump 5c3fdabc runs BEFORE the forward water pass, so every M5 energy gate was blind.
  The plain after-water dump (423e0548) is dominated by the water FS's sun glints (max up to 1584; a measured-only
  key moved it +2.5%): m5w3's "compose -2%" was that noise, in the water pass's dump before the compose ran.
  New `energy_fs` "water-compose" (tools/m4_promotion_gate.py): 5c3fdabc + the in-frame compose delta (next
  scene-image dump minus the water pass's own dump, frame.json order). OFF-OFF 0.01%. Frame deltas: reflections
  -0.12% / -0.19%, sun shadows -9.7%, local shadows -2.0%, refraction -0.7%. Verdict: physically right, no
  bug (analysis: PLAN_M5_WATER M5.3c). `--energy-fs` re-reads old runs; wf2 uses it.
- wf2-s8-fg2 110212 (VALID, unfocused 30 fps bursts, ~21 px/frame turn). pan_artifacts transient pixel-frames per
  calibrated pixel-frame, water mask (rt_debug 21 in the turn, 105k px) / whole frame: off-a 2.25% / 1.63%, rr 2.33% /
  1.76%, edge 3.29% / 1.87%, off-b 2.32% / 1.42%. rt_water_rr_hitdist: null (wf1's -25% was the confound).
  rt_water_rf_edge: WORSE on water (+42%, OFF spread 0.07 points): rejected, stays off. Energy (water-compose):
  rr +1.9%, edge +1.1%, band 2.03% (OFF spread 0.36%; with the sun shadows on, the in-frame delta carries glint
  noise too: -9.5..-11.4% per shot). GPU: rf 0.570 vs 0.568/0.569 ms, compose 0.41 ms in every window.
  Rays here: screen hit 15.0%, hidden 6.5%, off screen 2.1%, miss 76.4%: the screen border is the smallest switch.
  Bursts deleted (11.9 GB); report: scratchpad wf2_110212/report.json.
- Next: a rotation burst with rt_water_compose=0 (live) in the same run, to learn whether the compose causes the water
  flicker at all; then the hit/miss and hidden switches (76% miss, 6.5% hidden, A L / B below 0.5 for most composed
  pixels: every flip is a large step), driven by the FS's animated normal.
- Open: the hidden-tier switch (tier 2) is still binary; BG3's orbit camera moves the eye, so it can flicker too.

## IP grass camp job INVALID: layer stats lines once per window, snapshot gates as events (branch worktree-agent-a2c38a4884e982692, READY_FOR_LIVE, 2026-11-02)
- Cause (hig1-ip-grass-camp-fg2 08:26): the layer's `[hit material instanced]` lines printed only from a query on
  frame % 120 == 0. Frames 1800 and 1920 were rejected (`source epoch current 1800 source 1799`), so 1 line in 3 diag
  windows. That line (frame 2040, IP grass prepass/G-buffer/mapped 0) sat in diag-off-b with the key off: correct, not a
  family-4 counter bug. Its "pairs same frame 31506" are since the run start (no earlier window line).
- The live toggle works: the layer shows 0 IP grass rows in diag-off-b; core rows need `h1.w == pc.frame` (no stale
  rows). The core clip "checked 44952" in diag-off-a is about 5 ON frames after the diag-on cfg read (29.012, line
  29.247: 0.057% of 78M lookups vs 1.4% in the ON line). The gate's straddle rule already skips such lines.
- Fix: `instanced::stats_window_due` (common/native_hit_instanced.h): the first query that reaches the log in each
  120-frame window prints both lines. Unit test: native_hit_instanced_test section 7.
- Gates: the per-family line is one query's snapshot, not a summary, so its two expect_every gates are now
  `"event": true`. At about 10 frames/s with rt_stats=1 the 120-frame cadence is 10-13 s, so the diag windows are now
  20/25/20 s (25 s is the lint's maximum; diag-on needs 2 core clip lines). The plan is 164 s.
- Re-gating the old log with the new gates: diag-off-b passes; diag-on and diag-off-a still have no line (the code fix
  is needed). Build + 96/96 CTests pass. No shader change.
- The dryrun's static checks pass. Its fake queue runs fail on main too (`-Force`): foreign expired jobs get recorded in the
  fake root.
- Rerun: rebuild the layer (game restart), rerun hig1-ip-grass-camp-fg2 as is.

## Wind RGB, mask and long-session reads; capacity line per window, snapshot gates as events (branch worktree-agent-a2c38a4884e982692, 2026-11-02)
- m4w-wind-rgb-s8-fg2 08:32 VALID, every gate holds: wind RGB clip checked 7.9M, bit-exact all, rejected 0.0000%;
  qualified 45.0% -> 63.5% (+18.4 points, all three instanced families: the A/B toggles rt_hit_material_instanced);
  energy -0.00% (band 1.30%); cost +0.67 ms (gi trace pass 2.41 -> 3.08 ms). Kept on in Ultra (no change).
- hmm1-mask-s8-fg2 VALID: qualified +0.65 points (predicted 1.1, range 0.3-3.0), energy +0.04% (band 1.30%), cost +0.006 ms.
  hmm2-mask-inn-fg2 INVALID for the gate only: the `[hit material capacity]` line printed only on a frame % 120 query, and
  2040/2280/2400/2760 were rejected (source epoch), 1 line per diag window, while the gate wanted 2. Now the first query of
  each 120-frame window logs it (instanced::stats_window_due), and every snapshot gate (`hit material capacity`,
  `hit material instanced ... prepass`) is `"event": true` (m4_hit_mask, m4_wind_rgb, m4_pool_alpha). Re-gated, hmm2 passes:
  qualified 18.0% -> 30.1% (+12.1 points, above the 0.5-6 range), energy -0.80% (band 1.20%), cost +0.92 ms trace pass.
- ls1-long-session-camp-fg2 08:45: no unbounded leak. AS is 0.25 GB static and 0.8-1.4 GB in pan/rotate windows
  (transient BLAS). The 64 MB chunks are never freed: 58 -> 66 chunks (+512 MB) until cycle 14, then flat for 8 cycles,
  and VRAM is flat from cycle 13 (18.2-18.7 GB). VRAM trend now compares cycles 12-14 with 20-22 (passes on this log).
  The pool records line needs 120 frames at ~10 fps: the diag-pool windows are 20 s (plan 41 min). Records stay 411-417.
- Open (separate VRAM item): 66 x 64 MB chunks reserve 4.2 GB for 0.25-1.4 GB of AS (fragmentation, never freed).

## OMM upper bound windows in the hig1 camp rerun (branch worktree-agent-a2c38a4884e982692, READY_FOR_LIVE, 2026-11-02)
- Existing hot key `rt_alpha_test=0`: alpha-tested camera TLAS instances get FORCE_OPAQUE (bvhsplit.cpp 1536/1598, rtscene.cpp 1211), geometry kept;
  cascade casters already skip cutouts. No code change. Plan: omm-ub-on / -opaque / -on2, 25 s each before restore (300-frame gpu lines,
  10 s cadence at 30 fps: 2 per window). Gates per window: rt passes GPU cost, gi trace pass median, scene wait 0 timeouts.
  Predicted: unknown; >= ~3 ms saved pursue VT OMM, < 1 ms close it. Plan 239 s, about 6 min with the load.

## Game data audit: what each RT feature reads, and from where (branch worktree-agent-a1f09aa8ed1b0702d, docs only, 2026-11-02)
- New docs/GAME_DATA_AUDIT.md: per feature the data it needs, its source (runtime / asset DB / hand constant), live
  coverage, better game data not used, licensing, and a ranked action list. No code change.
- Already exact: water compose (FS's own N and DFG LUT), probes/ambient/DFG, b25/b29 lights, sun colour and clouds,
  XFB geometry (skinning, wind, terrain), non-VT alpha, emissive exact families.
- Replaceable by exact game data: VT alpha from the page cache (1.48% of camp alpha test on coarser pages), lava heat
  constant 0.5, roof volume pads (block 85% of S8 sun-lit far hits), rho x receiver ambient at far hits, class 4
  front lobe only. Schlick survives only in the water_reflect diagnostic.
- Top 5: (1) VT masks for every VT alpha-test material, enumerated from the asset DB (the extractor reads one replay
  pack and a hard-coded LSLib path today); (2) recipe rows from the DB's bindings + classifier; (3) cascade caster
  roofs instead of volumes for GI far rays; (4) vertex COLOR in the XFB stream (Tileset rows, lava heat, Tileset
  BGM); (5) class 4 transmission at hits.
- Licensing: masks, DB and any GR2/texture extraction are generated on the user's machine (LSLib MIT or a C++ port).

## Water flicker attribution job wf3-s8-fg2 (branch wf3-nocompose, READY_FOR_LIVE, 2026-11-02)
- wf3-s8-fg2 (foreground: wf2 ran unfocused at 30 fps): calibration burst, then rot-off-a / rot-nocompose / rot-off-b,
  same direction from the same pose, every water image key (compose, sun / local shadows, refraction, game shadow)
  on / off / on, toggled 9 s before each burst; rt_debug 21 masks static and in the turn; energy and cost shots.
  161 s, 4 bursts (about 9.2 GB, deleted after the analysis). Dryrun + lints pass. wf2's job removed (analysed).
- predicted.json (committed before the run): shot-nocompose energy +12.6% (water-compose measure: the Ultra water
  keys remove -0.0217 in frame at this pose, wf2), compose GPU -0.41 ms. Flicker: nocompose water-masked rate within
  the OFF spread = the game's own water flickers; clearly below = ours (compose switches, shadow ray noise).
- Result 141227 (VALID, foreground, bursts 48-50 fps): water-masked transient rate rot-off-a 3.38%, rot-nocompose
  3.53%, rot-off-b 3.61%; whole frame 2.92 / 3.17 / 2.66%. Every water image key off (compose not running during the
  burst: no compose GPU line 14:15:45-59) leaves the flicker unchanged: it is not the RT water compose. Energy
  (water-compose) nocompose +11.13% vs predicted +12.6% (band 2.12%): as predicted. Compose GPU 0.41 ms (0.52-0.57
  in the turn), 0 when off. Bursts deleted (9.5 GB); report: scratchpad wf3_141227/report.json.
- Next (not the compose: its switches, temporal stabilisation and tier 2/3 hit shading would leave this rate
  unchanged): RR's inputs on water (the water writes no depth and no motion: RR reprojects the surface with the
  bed's), A/B rr on/off or a water motion/depth input; else the game's own water (flow-mapped normals, probe).
- rt_water_rf_edge marked REJECTED and rt_water_rr_hitdist NULL in bg3rtx.ini (both stay 0, no GUI entry).

## wf1 water flicker run 093458: gate fix, analysis, rerun prepared (branch wf-gate-fix, 2026-11-02)
- INVALID only through the job: the smoke-ultra "one trace written in each static window" every-window gate was
  pointed at the cost windows (no trace there). Now the shot windows; with it the run passes all 7 gates
  (queue_gates on the kept log). The 3 K16 bursts were written (2.4 GB each, transport ok).
- Key rt_water_rr_hitdist toggled live as planned (`WATER_RR_HIT=1 ... built` at 09:36:54 and 09:38:14).
  Energy: shot-on -0.07% (band 1.25%, predicted null): pass. GPU: water reflections 0.128 ms, GI trace 4.00,
  reflections 0.570 ms ON vs 0.569 OFF: no cost.
- Flicker: not decided. pan_artifacts needs a static calibration burst; the job had none, so every rotation burst
  is INCOMPLETE (and the tool crashed on a numpy bool in its JSON: fixed). Own metric (second temporal difference of
  the reprojected final luma, all valid samples): OFF-a 0.00870, ON 0.00873, OFF-b 0.00761: no difference beyond
  OFF-OFF. It is dominated by reprojection error at edges, not water-specific (the bursts carry no water mask), and
  the key changes RR's hit distance only where the water reflection resolves on screen (8-15% of the water rays).
- Rerun 100826 (foreground, calibrated): pan_artifacts final-luma transient pixel-frames per calibrated pixel-frame,
  whole frame (no water mask in the bursts): rot-off-a 2.20%, rot-on 1.65%, rot-off-b 2.18% (events 1.68 / 1.31 /
  1.67%): ON 25% below both OFFs, OFF-OFF spread 1%. Caveats: ON rotates the other way (other content, 80k vs 112k
  calibrated px) and its burst started at the toggle (the variant built 6 ms after arming); gi_luma events rose ON
  (0.31% vs 0.14 / 0.15%). Energy -0.10% (band 1.25%); water reflections 0.119 ms in all three cost windows.
  Verdict: consistent with a partial fix, not proven; next run: ON burst a few seconds after the toggle, same
  rotation direction for all three, and a water-coverage mask (rt_debug 21 trace at the static pose).
- Rerun prepared earlier: cal-static burst first (4 bursts, about 9.4 GB transient, deleted after). The run's bursts and
  traces deleted (7.1 GB).

## S8 water reflection flicker in rotation: causes, rt_water_rr_hitdist (off), live job (branch water-flicker, READY_FOR_LIVE, 2026-11-02)
- Causes found in the code (the compose's frame-level stability is fine: 0 on/off flips):
  1. RR's specular hit distance on water pixels is D-RF's at the G-buffer surface = the bed (core/gi.cpp:3117
     set_spec_hit_distance(g_rf_hit); shaders/gi/rt_reflections.hlsl:484; the water writes no depth,
     water_reflect.hlsl:6-14), often kNoHit (rough bed): RR reprojects the mirror image with the wrong virtual depth
     when the orbit camera translates. Most likely; fix under test.
  2. RR's normal/roughness guides are the bed's G-buffer (layer/ngx.cpp:12-16 rr_guides), not the mirror water.
  3. The water pixels' motion vectors are the bed's (no water depth/velocity): surface parallax of the water plane
     against the bed below.
  4. The compose resolves per pixel (tier a, water_compose.hlsl header): with the FS's flow-mapped normals the
     resolved/unresolved mask moves as reflected samples leave the screen in rotation (C' = C + A L - B vs C).
  5. water_reflect's water TLAS is one build old (age 1.07 frames, shifted by translation only): its normal and
     hit distance lag the animated waves by a frame (compose uses the FS's own N, t).
- Fix behind `rt_water_rr_hitdist` (default 0, live): request rt/water_reflect.hlsl?WATER_RR_HIT=1 writes the mirror
  ray's hit distance (tier 1) or no hit into g_rf_hit on water pixels (binding 30 in the water push set, a stand-in
  when off). Key-off SPIR-V byte-identical (shader_identity vs main 566db3d); variant compiles; variants.txt,
  shader_variant_test, variant_matrix_test, settings_test pass.
- Live job tools/live_batches/water_flicker wf1-s8-fg2 (unfocused, 163 s, 3 K16 bursts about 7 GB): energy null,
  cost 0, flicker via pan_artifacts on rotation bursts ON vs both OFF. lean_dryrun gained -Unfocused and -MaxBursts.

## VT alpha: va1 result, game-wide catalogue, miss log, first-install design (branch vt-masks-catalogue, 2026-11-02)
- va1 (camp, VALID): masks alone are image- and cost-neutral (energy +0.24%, band 1.95%; rt passes and GI ms
  unchanged). Disproved: the coarser-page share did not drop (camp grass reads its resident mip 2).
- Catalogue (all paks): 1,677 alpha-tested VT materials, 125 templates, 1,174 textures; BC4 1,713 MB (levels 0..2),
  1,305 MB (level 0). Masked so far 65. data/assetdb/vt_alpha/vt_alpha_catalogue.json added.
- Key `rt_vt_alpha_miss_log` (live, off): ranked captures/vt_alpha_misses.tsv across sessions;
  vt_alpha_masks.py --misses extracts them.
- First-install generation design in docs/P4_OMM.md 11.2h (about 10 s per texture, 30-60 min for all in parallel,
  1.3-1.7 GB output, LSLib needs .NET 8).

## Offline VT alpha masks for every camp VT alpha shader (branch vt-masks-camp-all, offline, 2026-11-02)
- 16 more VT alpha shaders at camp (same recipe as the grass): 65 masks, 79.2 MB, ADDED to data/assetdb/vt_alpha
  (index/manifest `_camp2`); the layer now reads every vt_alpha_index*.txt. Two sub-tile textures (13,092
  triangles) cannot be extracted and keep the page cache.
- Agreement on the camp pack: alpha test 99.05% triangle-weighted (0.94% differ on coarser pages).
- Masked share: 99.4% of camp VT triangles; estimated ~97% of shadow and ~98% of visibility alpha candidates
  (was 18% / 37% with the grass only). va1/om3 expects accept any mask count.

## Small defects: inn capture split, A/B toggle latency, roof-cut leak estimate (branch small-defects, 2026-11-02)
- Inn "opaque draws dropped (buffer full) 0.02/frame" (lr2 044237, 0.01 at 023108): both windows at the inn rotation,
  alpha region 547-613 MB (alpha demand 418 MB) + opaque peak 427-440 MB in 1024 MB. alpha_region kept alpha's 1.5x
  reserve while opaque had 1.1x (about 373 MB last frame): the opaque jump overflowed with 195 MB of alpha slack.
  Fix (common/rt_capture_budget.h): when both reserves do not fit, split the buffer by the two demands (same relative
  headroom), never below the alpha floor: 541 / 483 MB there. Unit test in rt_capture_budget_test. Layer (relaunch).
- A/B latency: `[ab] toggle latency: X ms from the toggle (frame F) to the present of frame F+2, the first recorded
  entirely with ray tracing OFF|ON` (layer/util.cpp; includes a shader compile on the render thread). New key
  `ab_off` (default 0) toggles the same A/B from the settings, since the queue cannot press Ctrl+Shift+F9.
  smoke_ultra su1: ab-off / ab-on phases (8 s each) and three expects on the `[ab]` lines.
- gi_far roof-cut leak (estimate, no replay: a live batch was running): screen hits beyond 1 m are 4.9% of S8 rays
  (31.9M of 655.6M); the roof rule blocks 85% of the qualified sun-lit far hits there (24.2M of 28.4M), so most
  far rays from roofed pixels do cross a cut roof. A screen hit through it needs on-screen geometry above the room
  within 6 m (neighbouring walls and roofs). Upper bound: the whole far bounce gain, +0.88% of S8 screen radiance
  offline. Measurable on the packs with one rt_stats counter (visible, t > R0, !gi_far_roof_clear) on S8 142819-1
  and inn 013914-3 (casters in the pack) with --sun-trust.

## SHaRC on Ultra: live job prepared; cheaper gi_far measured (branch sharc-offline, READY_FOR_LIVE, 2026-11-02)
- SHaRC offline: not measurable on the packs. frame_replay has no SHaRC set 4, and a one-frame pack has no cache
  history (the cache is filled by screen injection over frames; 2.10-2.16 found 1.5-30% of non-screen hits live).
  What it could add, from the PT splits: S8 off screen up to 35.8% + multi-bounce 24.9% of the PT bounce, camp off
  screen 38.1%. gi_far already takes visible-far and qualified hidden sun-lit hits. No code change: the sharc +
  gi_far + hit_sun variant exists (gi_trace_rt_sharc_hit_sun.hlsl, RT_GI_FAR).
- Live job `tools/live_batches/sharc_far` sh1-s8-fg2 (113 s, 3 PNG, dryrun passes): Ultra + gi_far, only sharc
  toggles. predicted.json +1.5% (carried over from 2.15, written before the run). Observables: energy, cache
  found share, GI trace and resolve/inject cost.
- Cheaper gi_far (replay, live defines, rt_stats 0, GPU median of 9, `frame_replay --push rays=N`):
  | pack | off, 2 rays | 6 m, 2 rays | 6 m, 1 ray | 4 m, 2 rays | 3 m, 2 rays |
  |---|---|---|---|---|---|
  | S8 giRaw.a / ms | 0.8790 / 0.92 | 0.8320 / 4.31 | 0.8321 / 1.73 | 0.8444 / 2.68 | 0.8528 / 2.36 |
  | camp giRaw.a / ms | 0.8149 / 2.68 | 0.7492 / 7.36 | 0.7492 / 3.90 | 0.7636 / 6.62 | - |
  One ray per pixel (gi_rr_rays=1 for the whole GI pass) keeps the mean visibility and bounce (S8 bounce 0.01763 vs
  0.01761) at 40-53% of the 2-ray cost; the price is variance into RR. 4 m / 3 m lose 26% / 44% of the S8 change.
  Not changed; sh1 now also runs gi_rr_rays 2/1/2 (energy null, GI trace -2.6 ms, flicker via rays_flicker.py:
  the QA temporal detector on 3 traces per state, OFF-OFF null). Plan 203 s, 12 traces.
- Risk noted: gi_far screen hits beyond 1 m take no roof rule; a ray leaving a roofed room through the cut-away roof
  can reach on-screen exterior ground (S8 went darker as predicted, so not dominant).
- lean_regression lr1/lr2 (04:12 INVALID): the shader expect now takes the core's `gi: gi/...RT_CASCADE_MASKS=1 ...
  generation N` (cache reads have no compile line); diag-primary 15 -> 25 s (camp at 11 fps did not close one
  120-frame stats window in 15 s). lean_dryrun: the motion-trace lint covers every trace_at repeat.

## Generated settings block without the key count (branch settings-gen-nocount, kept, 2026-11-02)
- scripts/gen_settings_defaults.py no longer writes "N keys of bg3rtx.ini": that one line changed with every added
  key and conflicted on every branch adding one (spec-cascade-s1, omm-option-b). Tables unchanged; settings_test and
  100 CTests pass. Branches that add keys now conflict only when they touch the same ini lines.

## rf_hit_gi re-measure prepared; the S6 light-visibility suspect tested offline (branch rf-hit-gi-2, READY_FOR_LIVE, 2026-11-02)
- The tier (a) ratios S6 1.090 (m4p3 161852) and S8 0.925 (m4p4 113146) come from `diag-*` windows without a trace, so
  the 385efd1 trace flaw (hit material skipped on traced frames) did not touch them. They predate the game's lobes at
  hits (RF_HIT_GI -> NATIVE_HIT_GAME_BRDF, merged with rf-hit-box): expected now S6 1.064 (1.17 with rf_hit_gi), S8
  0.925 (1.015 with rf_hit_gi).
- S6 visibility suspect (rf light rays lit 71% vs 56% in the GI): the light ray's alpha footprint is not it. On the S6
  pack 035919 the GI pass gives lit 16512 / 29609 (55.8%, as live) with the footprint x 1, x 16 and x 256 (identical
  counters). The two lit shares belong to different hit populations (reflection hits see more of the lava-lit far
  side; rays to lights of radius >= 10 m 90% vs 80%). No visibility fix: nothing found to fix offline. The reflection
  pass cannot be replayed yet (frame_replay replays gi_hit_direct only).
- Live job `tools/live_batches/rf_hit_gi_ab` (S6 + S8, 2 x 190 s, about 10 min, 0.4 GB): diag-rf / diag-gi1 / diag-gi2 /
  diag-gi1-box2 with rf_stats, cost and shots rf off / on / off on Ultra as merged. Predictions in its predicted.json.
  Dry run and lints pass.

## GI history NaN/Inf safety recovered from the codex research stack (branch recover-gi-history-nan, offline verified, 2026-11-02)

- From codex 06f62f9 (gi_temporal guard) and 9078be6 (NGX MV scale), see docs/CODEX_BRANCH_TRIAGE.md.
- shaders/gi/gi_temporal.hlsl: rejected history now writes the fresh sample by select, not
  `lerp(hist, cur, 1)` (= hist * 0 + cur, NaN for a NaN/Inf history texel, which then never recovered);
  a non-finite reprojected position is rejected before the float-to-int conversion.
- layer/ngx.cpp: MV_Scale_X/Y are stored only when both are read and finite (else the last valid scale).
- Pure fix, no key: the same values for finite inputs. Only gi/gi_temporal.hlsl (and its variants)
  changes SPIR-V; the layer part needs a game restart.
- Not recovered (needs a variant key and a live pan A/B): motion read at the depth sample pixel in
  gi_temporal, and rejecting off-screen previous-radiance reprojection (triage items 2 and 3).

## sh1 SHaRC + gi_rr_rays, lean regression on the new Ultra (branch rays-flicker-rerun, analysis + sh2 job, 2026-11-02)
- SHaRC on Ultra + gi_far (sh1 043322, S8): **fail**. Scene colour -0.86% against the committed +1.5% (band 1.47%):
  darker, not brighter (the carried-over prediction is disproved: cache hits now override qualified hidden hits,
  whose shading the cache's screen radiance undercuts). Cache found 23.8% of the queried non-screen hits; leak test
  10.1% of cache hits from occluded cells (5.4% of its contribution, rejected). Cost: GI trace +0.49 ms, reflections
  +0.17 ms, resolve 0.03 + inject 0.06 ms: about +0.75 ms. Stays off.
- gi_rr_rays 1 vs 2 (same run): energy +0.11% (band 1.47%, predicted null): pass. GI trace 4.32 -> 2.02 ms (-2.30,
  predicted -2.6), whole GI -2.04 ms, frame time 15.8 -> 12.9 ms. Flicker (rays_flicker.py, 3 traces per state):
  void, the OFF-b vs OFF-a null failed (excess regions 3.4 / 21 per mille in its two directions; 1 ray vs OFF-a / OFF-b
  9.3 / 31.7 per mille in the same places); mean temporal std 1 ray 0.00456 between the OFFs' 0.00426 and 0.00471.
  Test fixed, not the theory: job sh2-s8-fg2 (rays only, 6 traces per state, 132 s, about 1.1 GB), predicted.json
  written before it. Not into Ultra until its null passes and the 1-ray set shows no excess.
- Lean regression on main aeb248b (new Ultra) vs the 02:27 runs: no correctness regression. Primary match camp 94.05%
  (94.05), S6 99.86% (99.77), inn 84.8% (73.8, includes the rotations); scene wait 0 timeouts everywhere (inn had 157 of
  600); capacity: inn opaque dropped 0.02/frame (0.01 before: pre-existing, fails the 0 gate in both); ls_check any-bit
  30.5M px (26.9M). Cost is the new Ultra: GI trace camp 1.22 -> 7.65 ms (frame 16.7 -> 22.0), inn 0.45 -> 5.05,
  S6 0.32 -> 3.56 (frame 9.9 -> 12.7), from the hidden-surface bundle and gi_far's 2-ray 6 m pass.
- Deleted the 53 analysed trace folders of today's gf/bx/ef/sh1/lr runs (2.2 GB); packs kept.

## Preprocessed-text shader cache key + derived prune cap (branch pp-cache-key, kept, 2026-11-02)
- tools/shader_identity.py: second key level in compile_many (compile_shaders.py, merge-green, every
  *_shader_test.py gate): an entry whose include-closure key misses is looked up by its DXC -P text
  (#line, CRLF, blank lines dropped) + DXC id + args + entry; a hit reuses the SPIR-V and links the closure key.
  compile_shaders.py backfills old entries once (34 s). BG3RTX_SHADER_PP=0 turns it off.
- Self-check: `--pp-verify N` (shader_identity.py, compile_shaders.py) compiles N random requests from scratch
  and fails on a difference; merge-green runs N = 4 (+4 compiles per merge).
- Measured, comment-only edit of shaders/rt/rt_common.hlsli, merge-green's shader step: before 2892 entries to
  compile (40-50 min idle; 120 took 810 s today under load, ~5 h extrapolated); after 0 compiles, 35 s
  (2892 by preprocessed text); --pp-verify 4: 4/4 identical. Earlier samples: 52/52 agree (study branch).
- Prune: cap = max(cap, 1.25 x protected SPIR-V) (2.47 GB protected -> 3.0 GB; the fixed 2 GB cap was always
  exceeded); rows sharing a protected blob are kept.
- shader_identity_test 47 checks (fake preprocessor: comment edit compiles nothing, code edit its dependents,
  self-check catches a blind key, failed preprocess falls back, backfill, derived cap). Build + 100 CTests pass.
- TODO: the DirectXShaderCompiler NCSA licence text is nowhere on disk or in the repo; external/licences still
  lacks DirectXShaderCompiler-LICENSE.txt (package.ps1 warns). Fetch it from the DXC repository by hand.

## Hit-material stream caps from the RT capture (branch hm-stream-caps, READY_FOR_LIVE, 2026-11-02)
- Cause: at camp the fixed caps (deferred 4 x 32 MB, source 128 MB, basis 256 MB) overflowed with IP grass on: 119-134
  deferred rows dropped, wind / wind RGB captured 0, qualified share 26.4% -> 24.3% (hig1 09:54).
- Fix: native_hit_resources.h stream_caps from the capture size (cap.xfb_size at scene_prepare): source = the
  capture's wide-stride alpha floor share (40%, capture_budget::kAlphaFloorPctWide) in 32 MB granules, deferred chunks
  = source / 32 MB + 1, basis = 2 x source; the old caps for small or unknown captures. Ultra (1024 MB capture):
  source 384 MB, deferred 13 chunks (416 MB), basis 768 MB. Buffers stay sized to the need: VRAM grows only by use.
  New line `[hit material streams] frame N live X MB: source / deferred / query basis` reports what they hold.
  Unit test: native_hit_pipeline_test (stream_caps).
- Predicted VRAM at camp with IP grass on: about +55 MB source per slot, +55 MB deferred per packet, +110 MB basis
  per query in flight over hig1 09:54 (the evicted wind rows); measured by the streams line.
- hig1 job: wind and wind RGB captured > 0 with stream 0 in diag-on, capacity no overflow in every diag window
  (event, settle_s 1). Old log fails them (the eviction), as it should. Layer change: restart.

## Source copies freed once their frame's queries retired; IP grass verdict (branch hm-source-trim, READY_FOR_LIVE, 2026-11-02)
- Cause: every scene slot (8) kept its source copy until it was prepared again. hig1 10:27: source 2144 MB in 8
  slots with IP grass on (512 MB off), with at most 2 queries alive. Only queries of the slot's own frame read it.
- Fix: collect() returns a slot's copy to the recycle pool once its frame moved on and its owners retired
  (source_releasable, native_hit_resources.h, unit test). The streams line now prints slots holding a copy and the
  pool. Predicted: off 512 -> about 128 MB, on 2144 -> about 550-800 MB (2 live slots + up to one in the 512 MB pool).
- IP grass cost (+1.9 ms GI trace, cost windows): no new traversal path (same TLAS, same alpha test); it is per-hit
  work on newly qualified grass hits: the instanced clip check (2 x 3 vertices), 3 texture layers, and the light and
  sun shadow rays of gi_hit_direct / gi_hit_sun, which traverse the alpha-tested grass. Inherent to qualifying those hits.
- Verdict: keep rt_hit_material_ip_grass off (not in Ultra): +1.9 ms and about +1.5 GB VRAM after this trim, for
  +3 points of share (the 42.6% line covers few lookups) and no measurable energy change (+0.19%, band 2.19%).
  Revisit after OMM (the shadow rays' alpha cost) or in a scene where it changes the image.

## Event gates skip snapshots within settle_s of the settings read; hig1 camp rerun read (branch gate-event-settle, 2026-11-02)
- hig1 09:54 was INVALID only for diag-off-a: frame 1679 printed key-on IP grass rows 28 ms after the key-off read (a
  query recorded before it). queue_gates.py: event gates take "settle_s" (frames in flight x frame time: 8 x 0.11 s at
  rt_stats -> 1 s), set on every snapshot gate (hit material capacity / instanced). Re-gated: every-window gates ok (9).
- IP grass verdict: NOT qualified as is. Clip bit-exact (4.4M / 5.2M checked, 0 rejected), energy -0.01% (band 2.37%), cost
  -0.42 ms rt passes (OFF spread 0.18). But qualified share 26.4% OFF -> 24.3-24.7% ON: the IP grass rows (0.5-0.6M
  triangles) fill the deferred stream (4 x 32 MB, overflow 119-134) and evict the wind and wind RGB rows (captured 0);
  source stream 117/128 MB, basis 234/256 MB. Next: raise the layer caps (deferred_chunks, source/query caps), then rerun.
- OMM upper bound (rt_alpha_test 1/0/1, complete lines): rt passes 8.01-8.03 / 3.78 / 8.16-9.22 ms; gi trace 6.77 /
  3.08 / 6.96-6.98 ms; also ls 1.03 -> 0.41 ms, rf 1.16 -> 0.39 ms. "rt passes" includes the GI trace (core/gi.cpp
  rt_timestamp: sun shadow + GI trace/temporal/AO + sun resolve; ls and rf timed apart): total about 4.3 + 0.6 + 0.8 = 5.7 ms.
  Loose bound: forced opaque also ends every ray at the first cutout, also over clear texels (78% of evaluated alpha
  candidates are clear), which OMM still traverses.

## Ultra preset now (common/settings.h kKnobs, presets {Low, Medium, High, Ultra})

- RT water "+ Refraction" (all 7 water keys; restart to raise). Water chain 0.53-0.67 ms, up to 520 MB records.
- Hidden-surface shading "+ Bounce" (rt_hit_material, _instanced, _wind_rgb, gi_hit_direct, gi_hit_sun).
- Emission at ray hits "Hidden hits" (rt_hit_emission=1, rt_emissive=1): +0.08 ms GI trace (m4p5 021511).
- Far-field sun bounce 6 m (gi_far=1, gi_far_m=6): +3.2 ms S8, +3.7 ms GI trace camp.
- Not in Ultra: Box light shadows (question), + Reflections (rf_hit_shade), Native coverage, hair origins, OMM, cutout spill.
- smoke_ultra with everything above: valid in every window, median frame 16.65 ms (13.75 before gi_far and emission).
- Automatic (default 1, no GUI entry, keys kept for A/B): rt_hit_material_recycle, _capture_unlocked, _lean_hooks,
  rt_scene_wait, fg_letterbox, rt_hit_material_pool_alpha.

## Stage 1: live check of the bugfix round (done)

- smoke_ultra (su1, S8): every-window gates ok; capacity 0 ([geo] drops, cascade buffer full, mesh pool full, guard 0);
  scene wait 0 timeouts in every plan window. Traced frames wait up to 4x rt_scene_wait_ms (48 ms).
- Scene wait with hit materials (sw3 22:13): lean_hooks 1 gave 0 timeouts in every pan and rotation (0: 1-7 per
  120 frames); hit-material hooks ~2 ms instead of 5-6. Remaining mutex waits 0.6-0.7 ms/frame (leaf `pending_mutex`).
- Water stability: 120/120 composed, "[water] compose stability" uncomposed 0, non-finite terms 0 (fail-closed compose).
- Shader cache (`%LOCALAPPDATA%\BG3RTX\shader_cache`, `BG3RTX_SHADER_CACHE=none` disables): second launch 44 read,
  0 compiled; background prefetch of the last 5 sessions' requests. Log `[shaders] cache: N hits, M compiled ...`,
  `[ab] toggle cost: longest gap between presents X ms`.
- FG letterbox: game 2560x1440 in a 3840x1600 swapchain, FG displayed/rendered 2.00 -> `fg_letterbox` default 1
  (a no-op when the sizes are equal). Log `[sl] game output ... backbuffer subrect`.

## Stage 2: GUI (done)

- Experimental 28 -> 5 entries: Native coverage, Physical hair ray origins, Opacity micromaps, Preserve cutout
  capture, and the levels above Ultra's. RT water, Hidden-surface shading, Emission, Far-field are level knobs in Advanced.
- Developer only (dev=1): lighting reference, exact-k (`ls_k`), current cascade sun, `rt_hit_emission=2`.
- Old user inis migrate (`kKnobAliases`, `Choice::mark`; settings_test `test_user_ini_migration`, fixture
  tests/fixtures/settings/user_ini_20261101.ini). settings_test 934 checks pass.

## Stage 3: M5 water (done, in Ultra)

- Plan: [PLAN_M5_WATER.md](PLAN_M5_WATER.md). m5_promotion passed at S8, Flymm, camp (energy gate fixed for the
  double-counted sun term: S8 shot-on -10.78% vs -11.21%, tolerance 2.2%).
- Costs: reflections 0.113 ms, compose 0.098 ms, refraction 0.24 ms (69.6% composed, energy +0.07%), ls shadows 0.18 ms.
- Flicker fix: compose keeps the lighting's own scene pair (common/water_compose.h pick_scene) instead of refusing.
- Open: M5.5 steps 2-4 only if measurable; debris families not mapped.

## Stage 4: M4 hidden-surface shading (in progress)

- Plan: [PLAN_M4_REFLECTIONS_GI.md](PLAN_M4_REFLECTIONS_GI.md). Bundle "+ Bounce" in Ultra (m4_promotion without rf_hit_shade).
- Trace fix (385efd1, layer): traced frames now keep the hit-material capture; before, every traced energy of
  gi_hit_* / gi_far / rf_hit_shade / hit emission measured the key off. All quoted energies are after the fix.
- S6 diagnostic: GI hit shading adds light (+1.6% pack invariant), no darkening. The earlier S6 darkening was the
  animated lava (54.5% of luminance) and shots right after a DXC compile.
- `rt_hit_material_pool_alpha` (default 1, automatic with rt_hit_material): rows for alpha-tested pool meshes.
  m4s6 035213: S6 qualified 14.8% -> 52.1% (predicted +23 points), energy +1.06% / -0.38% (predicted +0.57%, band 2.84%), +0.46 ms GI trace.
- `rt_hit_material_mask` (off, ini only): Base_MSK_VT rows. Live `m4_hit_mask` (S8 + inn) READY_FOR_LIVE, predicted S8 +1.1 points.
- `rf_hit_shade` (+ Reflections, Experimental): bias above the spread, +1.4 to +1.8 ms. `rf_hit_gi` adds the GI term
  and the game's BRDF at hits (hd_game_scatter, box_light_test section 8). Expected S8 0.925 -> 1.0-1.03,
  S6 1.090 -> 1.12-1.22; the remaining S6 excess points at visibility (lit 71% of rf light rays vs 56% in the GI).
- Coverage blockers (tools/hit_material_blockers.py): S6 lava discs FLUID_Lava_Plane_Flowmap (46.2% pool without a
  row, animated flowmap: a new recipe); camp soup alpha foliage IP_Wind_GrassFlowers (row reason 60%); S8 Tileset needs
  vertex COLOR in the pool basis, HeightGradient needs world height. Not started.
- Check in game: Ultra, S3 camp: bounce never darker; S6 lava light in reflections and bounce.

## Local lights (D-LS)

- `ls_box=2` (Box light shadows "All", off in every preset): shadows the 3,329 unshadowed box lights.
  Live bx1 S6 / bx2 S7: cost +0.32 / +0.20 ms (ls pass), energy -0.54 / -1.35% (S6 band 2.00%), +0.10 / -0.13% (S7).
  S6 removes 5.45% of the lit luminance (18.0% of 369M rays occluded). Visual check: the question above.
- `ls_k`: off (user decision). D-EM projected check `rt_em_check_slices=16`: em_forge check-sliced max 0.026 ms per run
  (< 1 ms: pass from the log; a formal rerun only for a valid receipt).

## M6 PT reference and far field

- Ambient ratio E_native x AO / (k x PT far + PT env): S8 1.44, camp 0.84 (k 0.75), inn 0.78 (k 0.25). Not
  consistent: no global correction. S8 is the outlier: next question is what S8 has that the others lack.
- `gi_far` 6 m in Ultra: gf2 S8 031330 -2.01% vs committed -2.0% (band 1.32%), view vis ON/OFF 0.9471 (predicted
  0.947); camp 033251 -2.42% (band 2.29%). The earlier -0.1% gap was the trace bug above.
- Packs kept: replay-20261102-013545-2 (camp), replay-20261102-013914-3 (inn), S8 142819-1. Next: SHaRC (stage 5).
- `gi_debug=2` (view bounce) is not usable for energy: R11G11B10 saturates (max R 65024).

## Release check (stage 6 input)

- Default path vs f6b6636: every differing request is behind an off key. data/release.ini pins 21 diagnostics off.
- Open: release.ini forces rt_emissive=0 (players cannot enable Emission at ray hits); ls_box/rt_emissive in
  release.ini to revisit now that emission is in Ultra. Details: docs/RELEASE_CHECK_20261101.md.
- Still to do: long sessions, acts 2-3, installer, README, licences, the diagnostic package.

## Caps and gates (watch)

- Roof volumes 512 (largest level 398); mesh pool max 2047 MB; hit-material basis cap 768 MB; alpha floor 40% at wide
  strides (not yet confirmed live); cascade casters grow to demand + 25%.
- smoke_ultra forbids variant skips, roof WARNING, nonzero guard, `over kBasisCapBytes`, `mesh pool full at`.
- Every lean job: capacity, scene-wait and exposure-hold lints; `expect_every` per window (`when_present` for cascade).
- Tables: docs/GATE_AUDIT_20261101.md. rtStats: rf_hit_gi 419..426, wind RGB 427..434, kStatsCounters 435.

## Tooling

- `tools/shader_identity.py` (cached; merge-green shader step ~1-5 min); `frame_replay --push`; `tools/longrun.py`;
  host-atomics lint; `tools/m4_promotion_gate.py`, `tools/m5_promotion_gate.py` (colour images only);
  lean_dryrun fails motion-trace expects in phases without trace_at; auto-run `-Borderless`, `-GameResolution`.

## How to test in game

Settings window, preset Ultra. "Restart" = raise the level, quit, start again; lowering is live. Ctrl+Shift+F9
switches every RT effect off and on (now immediate with the shader cache).
- **RT water** (S8 Water Queens House pool, on in Ultra): columns and temple reflected; far half of the pool in
  shadow, no specks; the pool bed shifts with the view; camp pond (S3) tree shadows. No blinking while panning.
- **Hidden-surface shading + Bounce** (on in Ultra): S3 camp, never darker; Q/E rotation keeps shadows on their casters.
- **Emission at ray hits** (on in Ultra): S6 Adamantine Forge, lava light in bounce and reflections.
- **Far-field sun bounce** (on in Ultra): S8 and camp, Off / 6 m: rooms slightly darker under roofs and foliage where
  the game's ambient was brighter; no light leaks under roofs.
- **Box light shadows All** (Advanced, off): S6 lower-right walkway and central pillar darken slightly; S7 inn: no
  light into the next room.
- **+ Reflections** (Experimental level): S8 polished stone; rf_debug=8 shows L_a red / L_b green.

## Live batches ready (READY_FOR_LIVE, need your go)

1. `m4_hit_mask` (S8 + inn, ~9 min, 0.4 GB): Base_MSK_VT rows, predicted S8 +1.1 points.
2. `em_forge ef1` rerun (S6): formal receipt only; the gate already passes from the log.
3. rf_hit_gi tier (a) ratio at S8 / S6 (m4_promotion m4p3/m4p4 jobs) before any change to + Reflections.
4. smoke_ultra again if ls_box=2 goes into Ultra.
