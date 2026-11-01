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
