# Meta-audit 2026-10-31 (main d2ed3fa)

Read-only audit run at the end of the consolidation, to find what the project is missing or closed too early.
Repeat one at every consolidation pause (AGENTS.md "Lean live tests" and the /goal plan).

## 1. Coverage (by screen share)
- **H, M. Forward water has no RT.** 89.2% of the S1 docks view; rays see the sea bed. Next: M5.1 `rt_water_geometry`.
- **H, S-M. Camera-fade dither walls are solid in the TLAS** (`layer/geocapture.cpp:1231-1237`: discarding shaders not in the alpha table are captured solid). Inn: 12.08% of primary px have a TLAS hit in front of the raster, also static. Affects sun, GI and reflections in interiors. Next: identity test (INN_ROTATION_MATCH), then the dither hash in any-hit behind a key (QUALITY Q14).
- **H, M. Box lights**: 36.6% of placed lights; `ls_box` OFF; accuracy gate open (`ls_debug=4` reference recomputes about a quarter of the game's light); 3,329 unshadowed box lights leak through walls; unsupported at hits.
- **H/M, M. Hit material covers only static native VT opaque surfaces**: skinned, dynamic, alpha cutouts, decals, translucency and non-VT fall back. No pixel-share ranking of the `hit material ... reasons` yet.
- **M, M. Emission never reaches hits** (D-EM has no consumer): 8.3% of S5, up to 26.7% of the S6 lava view.
- **M, L-H. Roofs missing from the TLAS**: QA `sun_missing_casters` 73.8 per mille in S7.
- **L-M.** Forward VFX (5.1% in S8), decals at secondary hits, glass, hair partial for D-LS.
- **The coverage tool is stale**: `scripts/qa/coverage.py` HANDLING dated 10-27; the measured captures were in deleted worktrees. Refresh: update HANDLING, recapture S1, S3-S9, S14 on current main, then `--update-doc`.
- **Never captured**: S2, S10-S13 (act 2/3, mod-warning dialog).

## 2. Decisions closed on weak evidence
- Job cb2 expected the removed `rt_scene_wait=2` and ran a rebase-era gate: retire it.
- GI R3 "0 negative px" cannot fail (`gi_hit_direct` only replaces a zero fallback); the mean change was below the OFF-to-OFF drift. Use a participation share and an energy delta that can fail.
- M4.1 is "qualify in motion" but the motion bursts were dropped for the disk budget.
- M4.2 "mean within z x SE" cannot pass (near-zero SE, known missing terms). Gate per term with a bias budget.
- `ls_check` "0 px differ in any bit" is not derived; COVERAGE box shares rely on `ls_debug=4`, which its own validation finds inaccurate.
- "Not reproducible" defects without a cause: camp rugs/planks, Astarion hairline (the Hair/Scalp class-5 identity gap is real), dialogue flicker.
- Features with only CPU/fixture/GPU-helper evidence stay READY_FOR_LIVE: hair guard, TileBlend/Tileset fixtures, `rf_hit_shade`.
- Single-scene gates: `ls_box` (S7 + S6), D-RF (foundry), sun replace (partial), `rt_scene_wait=1` (inn, camp).

## 3. Silent fallbacks
- Pool/soup match counters (138/139/142/143) counted but not logged (logged from 2026-10-31: `rt epoch split`); `pan_primary.hlsl` never fills the opaque/vt/tex slots.
- Hit-material "unsupported" reasons are in no job's expect; no floor on the shaded share.
- Sun fallback to the native mask when the scene is not ready: three user windows had zero samples and zero fallback.
- No counter for solid discard geometry (the dither-wall source).
- Off-frustum lights at hits silently unsupported; `projector lights listed/px (not handled)` has no gate.

## 4. Physical correctness (QUALITY R0)
- At hits: AO fixed at 1, no emission, no projector lights, no local diffuse probes, no fog (`shaders/rt/native_hit_shade.hlsli`).
- GI hidden-hit bounce is added only where the fallback was zero; non-zero but incomplete screen values are never corrected.
- 3,329 unshadowed box lights light through walls.
- Sun at hits ignores off-frustum and occluder-incomplete regions.
- COVERAGE gap 6 ("RT sun unclouded") looks stale: `bg3_sun.hlsli` applies cloud shadows now.

## 5. Process and tooling
- Jobs must check scene preconditions (nonzero participation) before the A/B.
- Stale gates and scripts: rebase-era QA modules, cb2, the coverage HANDLING table.
- Missing test scenes: inn cut-away walls, polished-stone off-screen reflections (S8), act 2 darkness, lava at hits.
- Missing metrics: pixel share per hit-qualification reason, draw-ID coverage dump, isolated GPU ms for `gi_hit_direct`, fps cost of `rt_scene_wait`.

## Before closing M4
1. Gates that can fail: predicted energy delta, minimum participation, per-term bias budget.
2. M4.1 with an OFF and an ON turn, on a scene with confirmed hidden hits, with isolated GPU ms.
3. M4.2 at S8 and one more scene, with the hit-material reason histogram as a share of tier-(b) pixels.
4. The inn dither-wall identity test first: 12% false in-front geometry corrupts every M4 measurement indoors.
5. Retire cb2 and the rebase gates, refresh the coverage table, recapture S1-S14.
