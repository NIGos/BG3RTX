# BG3RTX progress - 2026-11-01 (main ac57418, ABI 25)

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
