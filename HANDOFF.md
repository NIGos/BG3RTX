# HANDOFF: R0 sun replace (`rt_sun_replace`)

Branch `worktree-agent-aff062a21c1600fda`, main `51a2db9` merged. Design and results:
`docs/TRACK_D_RT.md` section 12. QA additions: `docs/QA.md`.

## Implemented
- `rt_sun_replace=1` (default, live): the traced sun visibility (soft, sun-disc sampled)
  **replaces** the game's sun mask where trusted. Untrusted pixels keep `min(game, traced)`.
- Trust is the product of three tests:
  - **roofs**: the sun ray passes through no roof volume (asset DB API v3 `roof_volumes`:
    roofed rooms, covered floors, roof floors; padded 1 m sideways and 3 m up);
  - **off-screen**: the ray stays in the view frustum until it is 6 m above the pixel
    (`rt_sun_rise`) and, with `rt_sun_top=1` (default), above the highest visible point
    + 2 m. Trust ramps in from 0.75 of that path;
  - **proximity**: the game's mask is lit within 1.5 m (`rt_sun_near`) on a surface at a
    similar depth (`gi/rt_sun_resolve.hlsl`).
- Without a known asset-DB level, nothing is trusted (safe fallback).
- Cloud shadows: unchanged. The lighting applies them after the mask.
- **In-frame A/B (new):** `rt_sun_resolve.hlsl` compares each pixel's written mask with
  `min(game, traced)` of the same frame:
  - brightened by > 0.1 at the game's shadow edge (its own mask > 0.1, or lit within
    `rt_sun_near`/2);
  - brightened farther from it;
  - darker than min by > 0.1 (impossible with `rt_sun_roof_mode=0`).
  - Outputs:
    - log line `sun check (N frames, replace R): brightened by > 0.1 against min(game, RT)
      on X% of px (... per frame): at the game's shadow edge E%, farther F% (far: Y% of
      px); darker than min Z% of px; mean gain ...`, every 600 frames;
    - debug view `rt_debug=7`: the changed pixels over the lit diffuse in grey; yellow =
      edge, red = far, blue = darker;
    - the QA trace `sun_check` and the detectors `sun_check_far` (warning, 0.5‰),
      `sun_check_edge` (info) and `sun_check_darker` (gate, 0.01‰), with crops.
- QA phases (13 traces): the trace-to-trace `rep_on`/`sun_min` pair is gone.
  - `sun_dbg` (`rt_debug=2`; blue = the written mask), `sun_check` (`rt_debug=7`) and
    `sun_trust` (`rt_debug=6`) are each evaluated on their own, so `same_view` no longer
    voids them.
  - `python -m scripts.qa run` takes `--set k=v,...` (keys for every trace).
- Files:
  - `core/sun_trust.{h,cpp}`: upload ring, staged into a device-local `R32_UINT` image,
    counters 0-20 read back, `sun stats` and `sun check` log lines;
  - `shaders/gi/sun_trust.hlsli`, `shaders/gi/rt_sun_resolve.hlsl`;
  - `shaders/gi/rt_shadow.hlsl`: roof and off-screen trust;
  - `shaders/gi/rt_debug.hlsl`: views 6 and 7, and the blue channel of view 2;
  - `core/gi.cpp` (small): extras 47/48, `sunt_begin`/`sunt_end`, the resolve dispatch,
    `rt_debug` up to 7;
  - `common/assetdb_api.h` + `layer/assetdb.cpp`: API v3, tested in `tests/assetdb_test.cpp`;
  - `bg3rtx.ini`: `rt_sun_*`;
  - `scripts/qa`.
- Stats: none of the shared `rtStats` counters (0..117) are used; the counters live in the
  trust image.

## Verified (numbers)
- Build clean; `assetdb_test` OK (26 roof volumes within 120 m of the foundry floor, and
  the floor point lies inside one).
- Cost: the host-visible first version cost 14-16 ms/frame of `rt passes`. With the
  device-local image, `rt passes` is 0.5-0.7 ms total (S7). The sun passes are not timed
  alone.
- Coordinator run `captures/qa/sunrep` (S3, S14, S8, S7, S1; `rt_bvh_split=0`):
  - **no leaks**: `sun_replaced` (written lit where the game's mask is < 0.1) is 0‰ in all
    five scenes, against `sun_missing_casters` of 42.9‰ (S7), 5.1‰ (S14) and 4.9‰ (S3);
  - non-finite: 0 everywhere;
  - roof volumes keep 85-97% of the game-dark & traced-lit pixels in S7, S14 and the S1
    foundry;
  - **too conservative outdoors**: in S3, 94-98% of those pixels fail the off-screen test,
    because the scene top is at the tree tops. Fully plus partly replaced: 0-6%;
  - the `brightening rt_sun_replace` crops of that run were animation, so that A/B is
    removed.
- The in-frame A/B (`sun check`, `rt_debug=7`, `sun_check`) is new and compiled only: not
  run in game yet.

## In-game tests still needed (serial, `-Background`)
Build first: `.\scripts\build.ps1`. Needed: `data\assetdb\` (copied from main) and the
Script Extender bridge installed, otherwise the level is unknown and replace stays off. The
`sun stats` line shows `replace 0` and `0 roof volumes` in that case.

1. **In-frame A/B, default trust:**
   `python -m scripts.qa run --scenes S3,S14,S8,S7,S1 --out captures\qa\suncheck`
   - Read `sun_check_*` in each scene's `result.json` / report, and the `sun check` lines
     in `run.json` `log_tail`.
   - Pass:
     - `sun_check_darker` 0;
     - `nonfinite` 0;
     - interiors (S7, S8, the S1 foundry view): brightened total < 0.5‰;
     - every `crops/sun_check_far_*.png` shows the game's over-wide penumbra or acne
       (right per R0), not a missing caster.
   - Fail:
     - any blue (darker);
     - a far (red) region on a floor under a roof or balcony, or across a whole surface;
     - red in the shadow of something beyond the screen edge facing the sun.
   - `sun_check_edge` is expected (the game's PCF is wider than the 0.263° sun).
2. **In-frame A/B, `rt_sun_top=0`** (the outdoor fix to decide on):
   `python -m scripts.qa run --scenes S3,S14,S8 --set rt_sun_top=0 --out captures\qa\suncheck_top0`
   - Compare against test 1: brightened share and far share.
   - Pass: far crops still at shadow edges (right per R0), interiors unchanged.
   - If it passes, set `rt_sun_top=0` as the default in `bg3rtx.ini` and
     `core/sun_trust.cpp`.
   - Then copy 2-3 crops per scene to `docs/img/sun_replace_*` and fill in TRACK_D 12.3.
3. **Validation:**
   `.\scripts\auto-run.ps1 -NoBuild -NoAnalyze -Background -Validation -Save "QuickSave_3784" -Traces 1 -TimeoutSec 900 -Set "rt=1,gi=1,rt_shadows=1,rt_local_shadows=1,rt_reflections=1,rt_sun_replace=1"`
   - Compare the VUIDs against the same command with `rt_sun_replace=0`, and with
     `rt_shadows=0`, on the same build.
   - Pass: no IDs that only appear with the replace.
   - Places that could show:
     - the binding 47 image (`r32ui`; the fallback view is RGBA32UI, used only if the
       trust image failed to allocate);
     - the copies in `sunt_begin`/`sunt_end`.
4. **Motion:**
   `.\scripts\auto-run.ps1 -NoBuild -NoAnalyze -Background -Move -Save "Lower City - 91h" -Traces 3 -TimeoutSec 900`
   - Pass: `sun stats` and `sun check` lines present, `darker` 0, no non-finite values.
   - Watch for interiors briefly lit after a teleport: the camera readback lags 8 frames,
     and the volumes are re-queried when the camera moves more than 5 m.
5. **Cost:** S14 twice, `-Set rt_sun_replace=0` vs `=1`; compare the `rt passes: GPU cost`
   lines. Expected: < 0.1 ms difference (32 taps per pixel plus up to 256 boxes; S7 has 79).

## Stalls seen in my runs
The game stopped presenting with the process alive. None of these is attributed to this
change.
- r2 S7 at 02:22:56;
- r3 S3 at 03:57:32;
- r4 S3 at 05:27:05, with the replace inactive (bridge uninstalled, level unknown);
- r1 S1 at ~00:43: the TLAS stopped after the first trace.

## Known limits
- The scene top and the rise are heuristics. An off-screen caster taller than them, beyond
  the screen edge facing the sun, could shift a shadow edge by up to `rt_sun_near`.
- Roof volumes come from trigger shapes, not the game's actual roof fading. A roofed room
  whose roof is drawn keeps `min` too (no brightening there; harmless).
