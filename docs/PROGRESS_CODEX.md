# BG3RTX progress - 2026-11-02 morning (main 2e5b4c2, ABI 25)

One current summary per area, no history. Everything older, verbatim: [archive/PROGRESS_20261102_morning.md](archive/PROGRESS_20261102_morning.md)
(the 19 folded fragments of 11-01 / 11-02 and the summary before this rewrite), [archive/PROGRESS_20261101_evening.md](archive/PROGRESS_20261101_evening.md),
[archive/PROGRESS_20261101.md](archive/PROGRESS_20261101.md), [archive/PROGRESS_CODEX_2026-11-01.md](archive/PROGRESS_CODEX_2026-11-01.md),
[archive/PROGRESS_CODEX_2026-10-31.md](archive/PROGRESS_CODEX_2026-10-31.md).
Branches write `docs/progress/<branch>.md`; `py -3 scripts/fold_progress.py` folds them here. Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md).
Goal and stage order: [GOAL_NEXT.md](GOAL_NEXT.md).

## Report 2026-11-02 04:10 (main 2e5b4c2)
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
- Open (not blocking): **`rt_hit_emission` 1 or 2** (1 = hidden hits only, 2 = also screen hits). Ultra uses 1;
  2 is developer only.
- Decided: **`rf_hit_shade` stays Experimental until its bias is within the game's own spread** (tier (a)
  L_b / L_a S8 0.925, S6 1.089, camp 1.097). The threshold is not lowered.
- Decided: **`ls_k` stays off** in every preset (developer only).
- Decided: GUI audit approved (docs/GUI_AUDIT_20261101.md): levels instead of single keys; emission writes `rt_emissive`.
- Decided: crash dumps stay off for players. Later a diagnostic mode builds a package (logs, effective settings,
  GPU info, crash dump) with a save the user picks, copied read-only; it never reads saves on its own.
- Decided: disk: keep what a test or comparison still reads; delete superseded packs and analysed raw dumps.

## Ultra preset now (common/settings.h kKnobs, presets {Low, Medium, High, Ultra})

- RT water "+ Refraction" (all 7 water keys; restart to raise). Water chain 0.53-0.67 ms, up to 520 MB records.
- Hidden-surface shading "+ Bounce" (rt_hit_material, _instanced, _wind_rgb, gi_hit_direct, gi_hit_sun).
- Emission at ray hits "Hidden hits" (rt_hit_emission=1, rt_emissive=1): +0.08 ms GI trace (m4p5 021511).
- Far-field sun bounce 6 m (gi_far=1, gi_far_m=6): +3.2 ms S8, +3.7 ms GI trace camp.
- Not in Ultra: Box light shadows (question), + Reflections (rf_hit_shade), Native coverage, hair origins, OMM, cutout spill.
- smoke_ultra with everything above: valid in every window, median frame 16.65 ms (13.75 before gi_far and emission).
- Automatic (default 1, no GUI entry, keys kept for A/B): rt_hit_material_recycle, _capture_unlocked, _lean_hooks,
  rt_scene_wait, fg_letterbox.

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
- `rt_hit_material_pool_alpha` (off, live-tested S6): basis ranges and rows for alpha-tested pool meshes (row slot in
  InstInfo meta bits 16..31). Before: S6 pool alpha without a row 39.0%. Key off: S8 / S6 packs bit-identical.
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
