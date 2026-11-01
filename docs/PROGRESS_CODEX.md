# BG3RTX progress - 2026-11-01 (main cc81e02, ABI 25)

One current summary. Older entries, verbatim: [archive/PROGRESS_20261101.md](archive/PROGRESS_20261101.md)
(11-01 until the afternoon rewrite), [archive/PROGRESS_CODEX_2026-11-01.md](archive/PROGRESS_CODEX_2026-11-01.md),
[archive/PROGRESS_CODEX_2026-10-31.md](archive/PROGRESS_CODEX_2026-10-31.md). Branches write
`docs/progress/<branch>.md`; `py -3 scripts/fold_progress.py` folds them here. Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md).

## Open questions for the user

- **`rf_hit_shade` bias.** Tier (a) L_b / L_a is 0.926 at S8 (-7%) and 1.091 at S6 (+9%). The strict M4.2 gate
  fails (any bias is significant with 10^7 hits). Accept "bias within 9%" for Ultra, or keep it out of the
  bundle? Conservative default: out of Ultra until you decide. Its cost is also +1.4 to +1.8 ms (rf pass).
- **`rt_hit_emission` 1 or 2** (not blocking): 1 = hidden hits only, 2 = also screen hits (the screen value has
  no emission). Conservative default: 1.
- Answered: **`ls_k` stays off** (user decision, 11-01).

## S6 m4p3 non-finite alarm: the game's depth, not the bundle (branch worktree-agent-ad96013fbc66f09f7, offline, 2026-11-01)

- The `nonfinite 8692` of shot-step-gi was the gate summing every dump of three traces: OFF-a 2917 +
  step 2865 + OFF-b 2910, all in the game's own D32F 2048x2048 depth of pass `76433aef` (also with every
  RT key off). The measured scene colour `5c3fdabc` has 0 non-finite values in all 8 S6 traces.
- `tools/m4_promotion_gate.py`: scene-colour count per trace, other dumps listed per trace and marked
  game-owned or ours. Test case in `tests/m4_promotion_gate_test.py`, now in CTest.
- The darkening (step-gi -1.05%, sun -1.82%, inst -3.10%) stays open: not NaN in the measured buffer.
- Guard in the hit-GI variants only (key-off SPIR-V byte-identical): non-finite or negative hit term
  -> fallback0 (counter 417); mean bounce above the RGBA16F max -> 65504 (counter 418), so no +Inf can
  reach gi_composite's revert (which would drop ~36 px of bounce and keep the occlusion: darker).
  Logged on the `GI hit direct` line. Kept, off with the keys; cost: two predicated counters.
- Rerun m4p3: read `non-finite hit terms (fallback0)` and `bounce over the half-float max` in diag-on.
  Both 0: the darkening has another cause (next: the hit-GI alpha set vs b.alpha, scene drift).

## M5.4 water sun shadows: record R = C - D, not D (branch worktree-agent-abd9303a6e22b012e, READY_FOR_LIVE, 2026-11-01)
- Live m5w4 (S8): shadow geometry right, shadowed pool dark with black and bright specks. `clamped 145573`
  is the 120-frame total (1213 per frame = 0.44% of sun records), not half of the records.
- Audit of the three dumps: D is exactly the FS's sun term times dC/dSun (a, T, X in G; no later multiply,
  no exposure or fog mismatch, no ambient in it, disjoint from M5.3's B). Cause: precision. C - D was
  formed from the B10G11R11 target (1 ulp = 2^-6 / 2^-5 of C) and a half D; where the sun is most of C
  (shadowed pool median 66%, the glint up to C ~ 976) that rounding exceeds the rest.
- Fix (key-on only): the FS stores R = C - D (f32, then half) and lum(D) in words 8-9; the compose writes
  V C + (1 - V) R. Key off byte-identical (M5.3 patch FNV pinned, plain compose SPIR-V SHA1 unchanged).
- Test 4j: the FS model through the real encodings; at V = 0 the compose equals the FS with its sun colour
  set to 0 within half rounding; the old formula fails it.
- m5w4 job: expect_every `clamped` < 120 per window in precheck-shadows-on and cost-on (fails on the old run).
- Expected live: occluded ~62.6%, removed ~80% of D and ~46% of mean C (unchanged: the FS's own sun share),
  clamped ~0, no specks. The shadowed half stays darker than the lit half (its water is mostly sun).

## Box lights `ls_box=2` (branch worktree-agent-ab66efcab091c48ec, offline, READY_FOR_LIVE, 2026-11-01)

- Causes (lr3 S6 08:49:39): 0.009 traced box lights/px because `ls_box=1` traces only the shadow-mapped
  ones (evaluated 1.141/px, listed 2.44/px: not culling); those ran past their shadow view's near plane
  (847,547 of 847,557 occluded). `projector validation px 0`: it counts only under `ls_debug=4`.
  `ls_debug=4` "a quarter": AO 0 x mix(1, AO, m0.y) leaves half the direct light at S7 (m0.y 0.5) in both;
  the residual is additive (the game's in-scatter, not zeroed). Details: TRACK_D_LOCAL_LIGHTS 9.10.
- `ls_box=2` (GUI "All box lights", off): unshadowed boxes traced to the end of their light-side fade-in
  (visibility only, can only darken); shadow-mapped ones only to the near plane. New in-play accuracy
  check `ls box bound` (recompute vs the stored B10G11R11 value's upper neighbour) and `ls box trace`,
  device-local counters reduced per group, copied into rtStats 385-416 (after gi_far 372-382 and the
  rf box part 383-384). `box_light_test` checks the math against f469db78; `bl_reaches` is shared by
  D-LS and the rf_hit_shade box early-out.
- Key-off SPIR-V: before the merge, 992 of 1244 byte-identical vs 904c41e; the rest are the LS_BOX
  variants and the rf_hit_shade ones (same box math, shared). Expected +0.15 to +0.3 ms on the D-LS
  pass. Live: `tools/live_batches/box_lights` (S6 + S7, ~10 min, 0.65 GB; dry run passes). Not run.
- User check: S6 forge and S7 inn with "All box lights": no light leaking into the next room or
  through floors; the main-menu skull keeps its beam.

## D-EM projected check: device-local counters, sliced, timed (branch worktree-agent-a088270f73e9c47a5, offline, READY_FOR_LIVE, 2026-11-01)

- Finding (inferred from m4p3-s6-fg2 112657): `rt_emissive` 0 -> 1 raised the 300-frame average `gi: GPU cost`
  1.65 -> 10.4 ms with flat pass medians: one frame in 16 about 140 ms more. That frame ran the calibration's
  projected check (em_debug.hlsl mode 0, every `rt_em_check_frames=16`): up to 3 emissive ray queries per pixel
  plus 4-14 atomics per pixel into the host-visible `emStats` (PCIe). A periodic hitch for anyone with rt_emissive=1.
- Not a pure diagnostic: `rt_em_calib` scales the approximate families from it, and `rt_hit_emission` skips an
  approximate family until it is calibrated. So it stays on by default, made cheap instead of turned off.
- Change (core + em_debug.hlsl, no ABI, no layer change): counters in a core device-local buffer, reduced per
  workgroup and per wave/record, then a `flush` entry adds the non-zero words into `emStats` (plain stores; the
  host's layout and reads are unchanged). New key `rt_em_check_slices=16`: one interleaved 4x4 slice per frame,
  every pixel still once per 16 frames; 1 = the old full-screen run. The calibration gates count full checks
  (`em::calib_checks`, `calib_px_per_check`, unit-tested); `scripts/qa/em_gate.py` scales N the same way.
- New line per rt_timing_frames: `[em] check GPU cost (N frames): R runs (V views), X ms per run median (check
  pass, max, timed), Y ms/frame; P pixels per run; schedule ...`. The cost is still inside `gi: GPU cost`.
- Expected: per run well under 1 ms (1/16 of the rays, no PCIe atomics), no spike. Not measured yet.
- host_atomics_lint: the D-EM stats entry left the host-visible baseline (14 -> 13 known); `emDev` is device.
- Shaders: only em_debug.hlsl changed (rt_emissive=1 only); every other shader is untouched, so byte-identical.
- Live check (em_forge `ef1-af-em-fg2`, +36 s): phases check-off / check-full (slices 1) / check-sliced (16),
  12 s each; dry run passed. Gate: check-sliced max per run < 1 ms; `gi: GPU cost` mean in check-sliced within
  the check-off spread plus the logged ms/frame; approx families still show `scale` != 1.000.
- Note for readers of the layer's `[em] projected check` / `ours/game per family`: "frames" and bright px per
  frame are now per run (1/16 of the pixels). Docs: TRACK_D_EMISSIVE.md 13.24.

## gi_far go / no-go on the full S8 pack: keep off (branch worktree-agent-a01aebd66c10db204, offline, 2026-11-01)

- Pack `replay-20261101-142819-1` (rc1, casters on, VT cache, lighting inputs 19 of 19). The caster instance has no
  geometry in the pack (tris 0, mask 0), so roofs still come from the asset DB: `PT_ROOFS=1` for the PT and the new
  `frame_replay --sun-trust` (`tools/pt_roofs.py --trust`, 159 padded volumes) for the gi_hit_sun pass.
- PT reference (roofs, real materials, env, sun LUT): far field 53.6% on screen beyond 1 m (visible 15.2, hidden 38.3),
  off screen 35.8% (not split), live domain 8.4%; sun / local 70 / 30%; k = 15.2. Native vs PT far + env:
  E x AO / ref 1.44 (tiles p10 0.90, median 1.75, p90 3.01); the preliminary 0.70 without roofs flips.
- Emulation of gi_far 6 m on the pack matches the live `GI far` line within 2% (visibility 0.833 vs 0.834, bounce
  0.0146 vs 0.0147). Predicted image change: 4 / 6 / 8 m -1.4 / -2.0 / -2.4% of the screen radiance (6 m: ambient
  -2.83%, bounce +0.88%), toward the reference (per-tile error 1.40 -> 1.31 -> 1.30, no overshoot).
- Live S8 measured +2.87% (diffuse +2.93%): even the bounce alone gives only +0.88%, so the live change is not
  explained by the GI far line. Energy gate fails. Kept off; no rule change indicated.
- Next: one live ON / OFF window with `ls_stats=1` and the GI images in the trace (giRaw, folded AO, gDiffuse before /
  after the composite) to find the stage downstream of giRaw; suspect the D-LS `ao_direct` give-back on the folded AO.
- Tools: `frame_replay` prints `GI hit sun` / `GI far`, accepts `--sun-trust`, and `--native-ambient` writes
  per-pixel PFMs (E, as lit, AO, PT total / live / env, k terms). CTests frame_replay_test and pt_reference_test pass.

## User decisions (branch main, decided, 2026-11-01)

- `rf_hit_shade` bias (S8 -7.5%, S6 +9%): option 2. It stays experimental until its bias is within the
  game's own spread. First the GI bounce at the hit (shared with gi_far) and the box-light fix (`ls_box=2`),
  then re-measure; the threshold is not lowered. The user can try it now: Experimental, Native hit
  materials (restart), then Shaded hidden-surface reflections, at S8 Water Queens House.
- `ls_k` stays off in every preset.
- Disk: keep what a test or comparison still reads; delete superseded packs and analysed raw dumps.

## Consolidation report, 2026-11-01

- Done: water M5.1-M5.5 step 1 built and live at S8; hit-material instanced pairing fix; M6 PT reference and replay
  pack with lighting inputs; `gi_far` (off); tooling (every-window gates, atomics lint, progress fragments).
- Merged to main (all off by default): M5.1-M5.5 step 1, M4.4 emission, instanced rows + pairing fix, replay
  writer and inputs, PT reference, `gi_far`, `ls_k` (LS_K variant), queue `expect_every`, atomics lint.
- Not merged: M4 Ultra bundle (promotion batch failed), M5.4 R = C - D fix (pending live), `ls_box=2` (offline),
  `low-prio` (live game keeps Normal priority).
- Open: S6 Forge NaN with the bundle (2.9k -> 8.7k px, being fixed); bundle cost; `gi_hit_sun` at S3; M4.1 motion.
- Try in game: S8 Water Queens House, water options in order (below); S14 instanced foliage; S6 reflections.
- Nothing new is on in Ultra today. Only `rt_cascade_casters` (10-31) is.

## M4 hit shading (Experimental, NOT in Ultra; promotion blocked)

Plan and gate table: [PLAN_M4_REFLECTIONS_GI.md](PLAN_M4_REFLECTIONS_GI.md) "Promotion".
- `rt_hit_material` (restart): qualified share of hit lookups S8 48.1%, S6 27.9% (S8 was 0.4%). Classifier
  built share 54.4%. Next families: [MATERIAL_FAMILIES.md](MATERIAL_FAMILIES.md).
- `rt_hit_material_instanced` (startup patch, then live): the wind tolerance theory was disproved live (m4i
  09:39). The real cause was pairing across frames and instance order; fix 4925937 merged. Live at S14: 0 clip
  rejects in every window (gate < 0.04%), qualified 22% -> 37-42%. Cost gi trace +0.14 ms. Gate passes.
- `gi_hit_direct` + `gi_hit_sun` (live): never darker (R3 0 negative px), S8 gi trace +0.52 ms together.
  `gi_hit_sun` S3 gate not run; M4.1 motion and isolated cost not run.
- `rf_hit_shade` (live): bias above (user question). Tier (b) shaded share S8 24%, S6 3%. Cost +1.4 to +1.8 ms,
  unexplained (budget 0.2-0.4 ms); split `hit_ao_rays` 0/2 next.
- `rt_hit_emission` (live, needs `rt_emissive=1` at startup): S6 participation 6.9% of rf lookups.
- **Promotion batch `m4_promotion`: FAILED.** S6 Forge NaN pixels 2.9k (OFF) -> 8.7k (bundle ON). Whole-GI
  cost with the bundle: camp +4.8 ms, S8 +3.4 ms, S6 +9.1 ms. The preset branch
  (worktree-agent-a9e0aabd4d7f18008) stays unmerged. The NaN fix is in progress; rerun after it.

## M5 water (all Experimental, off; live at S8 Water Queens House)

Plan: [PLAN_M5_WATER.md](PLAN_M5_WATER.md). `QuickSave_3784` loads the foundry, so all water jobs run at S8.
- **M5.1** `rt_water_geometry` (live): both S8 planes captured (7b44a567, c4e92ccd), views 120 of 120. No image change.
- **M5.2** `rt_water_reflections` (live): GPU 15 ms -> 0.113 ms (device-local counters). Water in front 7.6%,
  rays screen hit 5.1%, hidden 4.3%. Measured only, no image change.
- **M5.3** `rt_water_compose` (restart): every frame composes now (112-120 of 120, was 3). Compose cost
  7.2 -> 0.098 ms (statistics device-local). The probe stays on 87% of owned pixels; mean change -0.35 to
  -0.65%. Open: traced / probe ratio 0.31 needs a reference crop; one moving window.
- **M5.4** `rt_water_shadows` (restart): live m5w4 shows the right shadow (the far rim shades the far half of the
  pool), but with black and bright specks: 1213 per frame (0.44% of sun records), rounding of C - D in the
  B10G11R11 target. Fix (the FS stores R = C - D) on branch worktree-agent-abd9303a6e22b012e, 80 CTests,
  pending live. Gate: clamped < 120 per ON window.
- **M5.4b** `rt_water_ls_shadows` (restart): live S8 sum check 100% within 0.098%, ratio of the means 1.0000.
  Only 0.2% of S8 water has a local light. Cost 0.18 ms. Passes; little visible at S8.
- **M5.5 step 1** `rt_water_refraction` (restart): live S8 69.6% of transmissive pixels composed, bed shift 24 px
  median, energy +0.07% (within noise), cost 0.24 ms. Passes.
- Next: S1 docks need a save with open sea; `rt_water_hit_shade` for the 8.8% hidden rays.

## M6 PT reference and far field (offline; `gi_far` Experimental, off)

- PT reference (`frame_replay --pt-reference`, merged): no real-time PT now (ReSTIR PT estimate 6.5-9 ms at 4K
  DLSS Performance). The first S8 numbers (sun 99%, 56-86% beyond 1 m) had no roofs; with `PT_ROOFS=1` the far
  field on screen fell 56% -> 33% and the PT total 14x.
- Replay pack with all textures: 3.06 GB. Offline hit material on it: 48.7% qualified, 99.58% bit-equal.
- `--native-ambient`: native ambient vs PT far field + env 0.70 (preliminary: no roofs in that pack).
- `gi_far` / `gi_far_m` (GUI `exp_gi_far` 4 / 6 / 8 m, needs `gi_hit_sun`): merged off. S8 replay bounce 32-54%
  of the PT's visible far tier; +0.8 / +1.2 / +1.6 ms at 4K Performance. Go/no-go needs > ~30% far field
  from a casters-on, VT-cache, native-ambient pack. Live batch `gi_far` prepared, not queued.

## Local lights (D-LS)

- `ls_k` (LS_K variant, GUI `exp_ls_exact_k`): S7 cost 0.717 -> 0.555 ms (k1, -23%). The 3-4% darker buffer was
  the game's bloom min filter, not the estimator (c1951a4). **Stays off: user decision.**
- `ls_box` (Experimental): game box lights are 37.9% of S6 lit luminance, 0.009 traced per pixel, all occluded;
  validation has 0 samples. Branch worktree-agent-ab66efcab091c48ec: `ls_box=2` shadows the 3,329 unshadowed box
  lights, with an accuracy bound and `box_light_test`. Offline, not merged.
- `gi_hit_direct` still skips projector (box) lights at GI hits.

## Tooling (merged unless noted)

- Queue `expect_every`: a gate must hold in every ON window. It turned m4i 093913 and m5w3 083416 INVALID.
- CTest `host_atomics_lint_test`: no new shader atomic into a host-visible buffer (13 baseline targets).
- Progress fragments + `scripts/fold_progress.py`; cb2 retired; coverage HANDLING dated 11-01.
- Offline work at BelowNormal priority (merged). Branch `low-prio` (not merged): the live game keeps Normal
  priority, because BelowNormal starved the level load behind dxc.
- Replay pack (`replay_pack`, `replay_pack_cap_mb`) and `frame_replay` (about 3 s per shader edit).
- `python -m tools.m4_promotion_gate <log>`: the promotion gate table.

## Stage 0 (closed 10-31) and watch items

- Real fps, focused, diagnostics off: camp 60 RT on / 93 off, inn ~71 / 89, forge ~100.
- `rt_stats` + `rt_primary_check` cost ~80 ms per frame: never in cost windows.
- Watch (no cause): camp rug/plank rectangles, Astarion hairline, dialogue shadow flicker.
- Inn rotation: faded walls keep casting shadows (user decision). `pool hit match` 34-37% rotating vs 99.6%
  static is not explained (META_AUDIT_20261101 3).

## How to test in game

All in the settings window, section Experimental (off in every preset). Leave the preset on Ultra, so GI is
Ray-traced, Reflections are on and Roof sun shadows (cascade casters) are on. "Restart" means: turn it on,
quit, start the game again. Turning any of these off is live.

**Water** (S8 Water Queens House, the pool under the temple and columns). In this order, each needs the one before:
1. Water geometry capture (`exp_water_geometry`, live). No visible change.
2. Water reflections, measured (`exp_water_reflections`, live). No visible change; debug views only.
3. Water reflections, composed (`exp_water_compose`, **restart**). Reflections of the columns and temple on the pool.
4. Water sun shadows (`exp_water_shadows`, **restart**). The far half of the pool in shadow. Known bug until the
   R = C - D fix merges: black and bright specks in the shadowed half.
5. Water local-light shadows (`exp_water_ls_shadows`, **restart**). Almost nothing at S8 (0.2% of the water lit).
6. Water refraction (`exp_water_refraction`, **restart**). The pool bed should shift with the view, not slide.

**Hit materials** (reflections and bounce of surfaces the screen does not show):
1. Native hit materials (`exp_hit_material`, **restart**). Everything below needs it.
2. Wind and two-sided foliage (`exp_hit_material_instanced`, **restart** to enable, then live). S14 foliage.
3. Shaded hidden-surface bounce (`exp_gi_hit_direct`, live).
4. Sunlit hidden-surface bounce (`exp_gi_hit_sun`, live, needs 3). S3 Campsite 27h: shaded areas get
   bounce from sunlit ground; it must never get darker.
5. Shaded hidden-surface reflections (`exp_rf_hit_shade`, live). S8 polished stone: surfaces behind the camera
   show lit, not the flat probe colour. About 7% darker than the screen at S8, 9% brighter at S6.
6. Emission at ray hits (`exp_hit_emission`, Hidden hits, live; needs Emissive geometry capture `rt_emissive=1`
   at startup). S6 Adamantine Forge: lava and braziers in reflections.

**Known bug:** S6 Forge with the whole hit bundle on shows NaN pixels (black or white dots, 8.7k vs 2.9k off).
Being fixed. Report the save and the option combination if you see dots elsewhere.

Ctrl+Shift+F9 switches every RT effect off and on. These options cost GPU time (bundle: +3.4 to +9.1 ms whole
GI, water about 0.6 ms with every water option); quality is not qualified for Ultra yet.

## Milestones

| Milestone | State |
|---|---|
| M0 | Queue authorization and ownership done. |
| M1 | Watch items above; offline cutout fixes ready. |
| M2 | 27 native-matched components; room closure unproven. Only when an offscreen need requires it. |
| M3 | Hit materials: S8 48.1%, S6 27.9%, S14 37-42% qualified. |
| M4 | All keys built and live; Ultra promotion blocked by the S6 NaN and cost; `rf_hit_shade` bias is the user's call. |
| M5 | M5.1-M5.5 step 1 live at S8, off; M5.4 speck fix pending live; S1 not measured. |
| M6 | PT reference offline; `gi_far` off, waiting for a roofs-on reference pack. |
