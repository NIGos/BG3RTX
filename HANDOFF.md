# HANDOFF: R0 sun replace (`rt_sun_replace`)

Branch `worktree-agent-aff062a21c1600fda`, main `51a2db9` merged. Design and results:
`docs/TRACK_D_RT.md` section 12. QA additions: `docs/QA.md`.

## Implemented
- `rt_sun_replace=1` (default, live): the traced sun visibility (soft, sun-disc sampled)
  **replaces** the game's sun mask where trusted. Untrusted pixels keep `min(game, traced)`.
- Trust is the product of three tests:
  - **roofs**: the sun ray passes through no roof volume (asset DB API v3 `roof_volumes`:
    roofed rooms, covered floors, roof floors; padded 1 m sideways and 3 m up);
  - **off-screen**: the ray stays in the view frustum until it is above the visible scene
    top (+2 m) and at least 6 m above the pixel (ramp from 0.75 of that path);
  - **proximity**: the game's mask is lit within 1.5 m on the same surface depth
    (`gi/rt_sun_resolve.hlsl`).
- Without a known asset-DB level, nothing is trusted (safe fallback).
- Cloud shadows: unchanged. The lighting applies them after the mask.
- Files:
  - `core/sun_trust.{h,cpp}` (new): upload ring, staged into a device-local `R32_UINT`
    image, counters read back, `sun stats` log line every 600 frames;
  - `shaders/gi/sun_trust.hlsli`, `shaders/gi/rt_sun_resolve.hlsl` (new);
  - `shaders/gi/rt_shadow.hlsl`: roof and off-screen trust, scene top, camera;
  - `rt_debug.hlsl`: view 6 = trust; view 2 blue = the written mask;
  - `core/gi.cpp`, small and local: extras 47/48, `sunt_begin`/`sunt_end`, resolve
    dispatch, set0 b2 query;
  - `common/assetdb_api.h` + `layer/assetdb.cpp`: API v3 `roof_volumes`, with a test in
    `tests/assetdb_test.cpp`;
  - `bg3rtx.ini`: `rt_sun_*` keys;
  - `scripts/qa`: `rep_on`/`sun_min`/`sun_dbg`/`sun_trust` phases, the `rt_sun_replace`
    effect, the `sun_replaced` detector.
- Stats counters: none of the shared `rtStats` counters are used (my own image).

## Verified (numbers)
- Build clean after the merge. `assetdb_test` OK: 26 roof volumes within 120 m of the
  foundry floor, and the floor point lies inside one.
- Cost: the first version read volumes and did per-wave atomics on host-visible memory, at
  14-16 ms/frame of `rt passes`. Fixed: with the device-local image, `rt passes` is
  0.5-0.7 ms total in S7. The two sun passes alone were not isolated.
- S7 Blushing Mermaid (interior, roof cut away; QA r3, pre-merge build):
  - 10.7-25.4% of traced pixels are game-dark and traced-lit;
  - 98-99% of those are kept by roof volumes, and 0% are replaced;
  - QA: `sun_missing_casters` 42.8‰, `sun_replaced` 0‰. The interior does not leak.
- S1 (pre-merge build):
  - foundry interior: 10.2% disagree; 83% kept by roofs, 17% off screen, 0% replaced;
  - docks: 0.06% disagree; half of it replaced, at shadow edges.
- Non-finite: 0 in all dumps of S7 and S1.
- Stalls (game stops presenting, process alive; not caused by this change):
  - r2 S7 at 02:22:56;
  - r3 S3 at 03:57:32;
  - r4 S3 at 05:27:05, with replace inactive (level unknown, Script Extender bridge
    uninstalled);
  - r1 S1 at ~00:43: the TLAS stopped after the first trace.

## In-game tests still needed (run serially; all `-Background`)
Build first: `.\scripts\build.ps1` (DLSS SDK from main's `external/`). The asset DB must
be in `data\assetdb\` (copied from main) and the Script Extender bridge must be installed,
or the level stays unknown and replace stays off.

1. **QA A/B, outdoor and interiors:**
   `python -m scripts.qa run --scenes S3,S14,S8,S7,S1 --out captures\qa\sunrep`
   (14 traces per scene).
   - Pass:
     - `nonfinite` 0;
     - `sun_replaced` small in interiors (S7, S8, the S1 foundry view: < 2‰);
     - every `sun_replaced_*` and `brightening_rt_sun_replace_*` crop is at a shadow edge
       or acne. In the replace/min ratio, blue areas hug an edge where the game's
       penumbra is wider than the traced one.
   - Fail: a brightened block far from any game-lit area, a lit floor under a roof or
     balcony, or light in the shadow of something off the screen's sun-facing edge.
   - Note the trust view (`sun_trust` trace, debug diffuse `d1669194`). Red and magenta
     must cover interiors; blue the screen edge towards the sun.
   - Copy 2-3 crops per scene to `docs/img/sun_replace_*` and fill in TRACK_D 12.2.
   - Read the `sun stats` lines from `<scene>/run.json` `log_tail` (replaced / kept by
     roofs / off screen / far shares).
   - If the scene top comes out absurd (for example 10224), recheck the `rt_tmax`
     distance filter.
2. **Validation:**
   `.\scripts\auto-run.ps1 -NoBuild -NoAnalyze -Background -Validation -Save "QuickSave_3784" -Traces 1 -TimeoutSec 900 -Set "rt=1,gi=1,rt_shadows=1,rt_local_shadows=1,rt_reflections=1,rt_sun_replace=1"`
   - Pass: no VUID IDs beyond the baseline `captures\validation_baseline_S1.log` (taken on
     c604311: 10788, 09600, 08114, 07988, 01000, 00118, 08737, plus the MessageIDs listed
     there). The merged main changes the baseline (the image-lifetime fix removes 01000),
     so compare against a main run with `rt_sun_replace=0` if in doubt.
   - Things that could appear:
     - binding 47 format (`r32ui` vs the `RGBA32UI` fallback view): only bound statically
       when the trust image failed to allocate;
     - the copy barriers in `sunt_begin`/`sunt_end`.
3. **Motion:**
   `.\scripts\auto-run.ps1 -NoBuild -NoAnalyze -Background -Move -Save "Lower City - 91h" -Traces 3 -TimeoutSec 900 -Set "rt_sun_replace=1"`
   - Pass: no stall attributable to the sun passes; `sun stats` lines present; no
     non-finite values.
   - The roof volumes re-query when the camera moves > 5 m. Watch for a frame where
     interiors flash lit after a teleport: the camera readback lags 8 frames, and volumes
     are queried around the old camera.
4. **Cost:**
   - Two runs on S14 with `-Set rt_sun_replace=0` and `rt_sun_replace=1`; compare the
     `rt passes: GPU cost` lines. Expected: < 0.1 ms difference.
   - The resolve pass reads 32 taps; the roof loop tests up to 256 boxes per pixel. S7 had
     79-101 volumes.

## Known limits
- The scene top is a heuristic. A caster taller than the visible scene, beyond the
  screen's sun-facing edge, could leak a band at most `rt_sun_near` wide next to lit areas.
- Roof volumes come from trigger shapes and do not follow the game's actual roof fading.
  A roofed room whose roof is drawn keeps `min` too (no brightening there; harmless).
- QA crops for the replace are only produced once the A/B runs above have been done.
