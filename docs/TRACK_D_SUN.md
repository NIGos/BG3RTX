# Track D: RT sun

Part of Track D (hardware ray tracing); index and old section map: [TRACK_D_RT.md](TRACK_D_RT.md).
Section 12 keeps its original number.

## 12. Sun replace: the traced visibility replaces the game's sun mask (`rt_sun_replace`, QUALITY R0)

Until now the RT sun was folded into the game's sun mask (`set1/b7`) as `min(game, traced)`:
it could add contact detail and shadows, never remove a shadow. R0 asks for the physically
correct result, so the traced visibility now **replaces** the mask wherever every caster that
could shadow the pixel is in our BVH. Elsewhere `min(game, traced)` stays the fallback, so a
missing caster never becomes a light leak.

### 12.1 Design

Two passes in the pre-lighting hook (`core/gi.cpp`, a few lines around the sun dispatch):
1. `gi/rt_shadow.hlsl` traces as before (soft, sun-disc sampled), writes `min(game, traced)`,
   and computes the **geometric trust** per pixel along the sun's centre direction:
   - **Roofs** (`rt_sun_roofs`): the sun ray is tested against oriented boxes of the asset
     DB's roof volumes (`Bg3AssetDbApi` v3 `roof_volumes`: roofed RoomTriggers, covered
     floors, roof floors; box triggers as is, prism triggers as polygon bounds × Height),
     padded by `rt_sun_roof_pad` (1 m) sideways and `rt_sun_roof_up` (3 m) upwards (not
     derived yet: the DB holds no roof meshes, see 12.4), the ones within the roof query
     radius of the camera, nearest first, at most `kSunMaxVolumes` (512; shaders
     `SUN_MAX_VOLUMES`). The radius is
     derived per frame (`core/sun_trust.cpp roof_radius`): the farthest pixel whose trust
     still depends on the roof test (read back from `rt_sun_resolve.hlsl`, header byte 48)
     + `rt_tmax` (the ray's reach), so every volume a counted ray can meet is in the query;
     `rt_sun_roof_radius` > 0 overrides it. Volumes beyond the cap are dropped farthest first,
     logged once per level (`sun: WARNING N roof volumes ...`) and counted in `sun stats`
     (`over kSunMaxVolumes on N frames`). `scripts/roof_stats.py` (asset DB): at most 398
     volumes in a level (CTY_Main_A), at most 183 within 300 or 600 m of any volume centre
     and 287 within 1000 m. The old cap 256 held only up to 600 m: on 2026-11-01 the radius
     reached 1980 m in CTY_Main_A (farthest roof-tested pixel 1680 m) and 103 of 359 volumes
     were dropped on 306 of 600 frames. 512 holds every level whole (`roof_stats.py --check`,
     CTest `roof_cap_test`); smoke_ultra forbids the WARNING. A ray that starts
     in or passes through one is untrusted: the camera may have cut that roof away (it is
     then missing from the TLAS but still in the game's cascades). This covers "inside a
     roofed room", "under a floor with a higher floor" and "in the shadow of a roof".
     Under them `rt_sun_roof_mode` keeps `min(game, traced)` (0, default), the game's mask
     (1) or forces shadow (2).
   - **Off-screen casters** (`rt_sun_offscreen`): the sun ray must stay inside the view
     frustum (+ `rt_sun_margin` in uv) until it has risen above the **scene top** (the
     highest visible point within `rt_tmax`, read back from the GPU, + `rt_sun_top_pad`)
     and at least `rt_sun_rise` (6 m) above the pixel. The frustum sides are linear in the
     ray parameter in the `frustum_inverse()` space, so this is four divisions. Trust ramps
     from 0 to 1 as the inside part goes from `rt_sun_ramp` (0.75) to 1 of the needed path.
2. `gi/rt_sun_resolve.hlsl` (only when replace is active) writes `lerp(keep, traced, trust)`
   into the mask with trust = the geometric trust alone (0 under a roof volume, else the
   off-screen trust). The former **proximity to the game's lit mask** term (taps on rings
   out to `rt_sun_near`, trust `saturate(near·2 − 0.5)`) is **removed** (QUALITY R0): it
   started from the pixel's own game mask, so even `rt_sun_near=0` never replaced a pixel
   the game held below 0.25, and it made the game's mask the judge of its own errors.
   Interiors and cut-away roofs are held by the roof volumes; geometry never captured is
   the off-screen term's job. The ring search remains only for the A/B debug class
   (12.2) when `rt_sun_near` > 0. `qa_zero` bit 0 (QA ambient floor) writes 0 into the
   mask here and in `rt_shadow.hlsl`, so it holds with `rt_sun_replace` on and off.

Without the asset DB level (no database, the level unknown, no camera read back yet, or
no set 0 b2 at the lighting dispatch) nothing is trusted unless `rt_sun_roofs=0`.

**Cloud shadows** are kept: the mask pass (`0687fa32`) binds only the cascades and the far
cache (no set 5, no cloud textures); the lighting multiplies the mask by both cloud-shadow
terms afterwards (`f469db78:1384-1421`, `bg3_sun.hlsli`). Replacing b7 leaves them applied.

**Data path.** The core writes a host-visible ring slot (header + volumes,
`core/sun_trust.cpp`) and stages it with `vkCmdCopyBufferToImage` into a device-local
`R32_UINT` image (binding 47, one texel per 4 bytes). The shaders read it and count into it;
the counters, the scene top and the camera are copied back (`sunt_end`) and read 8 frames
later. The first version read the volumes and did its per-wave atomics directly on the
host-visible buffer: **14-16 ms per frame** of `rt passes`. Through the device-local image
the `rt passes` total is back to 0.5-0.7 ms (S7, below), the level it had before.

Debug: `rt_debug=2` now shows the written visibility in blue (white = game, traced and
written all lit); `rt_debug=6` shows the trust (red roof volume, blue off-screen fallback,
green trusted, dim where the traced ray hit; magenta, "far from the game's lit mask", no
longer occurs). The log prints every 600 frames `sun stats (...)`: traced/lit pixels, the
share of "game dark & traced lit" pixels that were replaced, partly replaced, kept by
roofs, kept off screen, the mean brightening, the roof query radius with the farthest
roof-tested pixel, and the frames over `kSunMaxVolumes`.

### 12.2 In-frame A/B (`rt_debug=7`, `sun check` log line)

A trace-to-trace A/B (`rt_sun_replace` 1 vs 0) does not isolate the effect: water and wind
in foliage change 28-68% of the albedo between consecutive traces (QA run
`captures/qa/sunrep`, coordinator, `rt_bvh_split=0`). `rt_sun_resolve.hlsl` therefore
compares, per pixel and frame, the written mask with `min(game, traced)`:
- **brightened**: the written R8 code above min's by at least one code (1/255). Both are
  R8 UNORM codes (`rtVis` is RGBA8, the game's mask R8), so one step is the smallest
  difference that reaches the game and there is no fp16 stage in between. Split into
  **at the game's shadow edge** (the game's own mask partly lit, code > 0, or lit (> 0.5,
  the geometric edge of a symmetric filter) on the inner two rings, within
  `rt_sun_near`/2; with `rt_sun_near=0` only the own mask counts) and **farther**. The split
  is a debug class only;
- **darker**: written below min by at least one code (only `rt_sun_roof_mode=2` can do that).

Counters 16-20 of the trust image, logged every 600 frames as `sun check (...)`: brightened
share of all pixels, its edge/far split, darker share, and the mean gain (and its share at
the edge). `rt_debug=7` shows the changed pixels over the frame's lit diffuse in grey
(yellow edge, red far, blue darker); the QA `sun_check` trace decodes it.

### 12.3 Results (QA harness, 2026-10-27; trace-to-trace phases, before the in-frame A/B)

Coordinator's run `captures/qa/sunrep` (S3, S14, S8, S7, S1; merged main, `rt_bvh_split=0`)
and earlier runs of mine. Shares of the traced pixels that are game-dark and traced-lit,
and what the trust did with them (`sun stats`, 600-frame windows):

| Scene | Game dark & traced lit | Replaced (fully + partly) | Kept by roofs | Kept off screen | Kept far from game-lit |
|---|---|---|---|---|---|
| S7 Blushing Mermaid (roof cut away) | 15.9-26.6% | 0-0.5% | 93.7-96.8% | 3.2-5.8% | 0% |
| S14 Lower City | 3.1-11.7% | 0-0.2% | 85-94% | 6-15% | 0% |
| S8 Water Queens House | 0.16-0.50% | 1-2.4% | 1-43% | 57-97% | 0% |
| S3 Campsite | 5.7-9.9% | 2-5.9% | 0% | 94-98% | 0% |
| S1 foundry / docks | 23.8% / 0.5% | 0% / 9.4% | 97.5% / 81% | 2.5% / 1.6% | 0% / 7.9% |

- **No leaks.** `sun_replaced` (written lit where the game's mask is < 0.1) is **0‰ in all
  five scenes**, against `sun_missing_casters` (traced lit, game dark) of 42.9‰ (S7), 5.1‰
  (S14), 4.9‰ (S3): the roof volumes hold every interior and the cut-away roofs
  ([sun_replace_s7_interior.png](img/sun_replace_s7_interior.png): lighting | red game
  mask, green traced, blue written; green without blue = kept). Non-finite: 0 everywhere.
- **The replace is too conservative outdoors.** In Campsite 94-98% of the disagreeing
  pixels fail the off-screen test: the scene top is the highest visible point (the tree
  tops, 13.6 m), and a sun ray from the ground leaves the frustum before it gets there.
  `rt_sun_top=0` (new) drops the scene top and asks only for `rt_sun_rise` above the pixel;
  the proximity test (`rt_sun_near`) still limits any leak to bands at the game's shadow
  edges. Which default is right needs the in-frame A/B (HANDOFF.md).
  `rt_sun_top` and `rt_sun_top_pad` (0 kept): removed 2026-10-31, see CONSOLIDATION.md C1.
- The `brightening rt_sun_replace` crops of that run were animation (waves in S8, swaying
  foliage in S3), which is why that comparison is gone.
- Cost: `rt passes` 0.5-0.7 ms in total in S7 (the sun passes are not timed on their own).

### 12.4 Not done / to verify (HANDOFF.md has the commands)
- The in-frame A/B in S3, S14, S8, S7, S1 with `rt_sun_top=1` and `rt_sun_top=0`: the far
  brightened crops must be the game's over-wide penumbrae or acne, not missing casters.
- Cost of the two sun passes in isolation.
- `-Validation` against a main baseline, and a `-Move` run.
- The scene top and the rise are heuristics: an off-screen caster taller than either is
  missed near the screen edge facing the sun (since the proximity term is gone, nothing
  else bounds that leak).
- **Not derivable from the asset DB** (`scripts/roof_stats.py`; the DB has triggers, no
  scenery meshes): `rt_sun_roof_pad` 1 m, `rt_sun_roof_up` 3 m and `rt_sun_rise` 6 m. What
  it does measure: roof-volume heights p50 6.5 m / p95 41 m; FloorTriggers of one building
  span p50 8.3 m / p95 77 m (max 180 m: giant floor boxes, not buildings); consecutive
  floors touch (gap p50 0.00 m), and floor triggers reach p50 22 m beyond their attached
  room sideways, so neither tells the roof overhang. Experiments: (a) roof pad/up: with
  the camera outside a roofed building (roof drawn, in the TLAS), trace up/outwards from
  the volume's top face and sides and record the hit extents per volume (a one-off
  `rt_debug` pass or offline TLAS dump) in S7, S14, S8: pad = p100 lateral overhang,
  up = p100 roof height above the trigger top; (b) rise: per level, the p100 height of
  TLAS instances above the ground under them near the frustum edge facing the sun, or
  `rt_sun_top=1` with the scene top once the off-screen ramp is fixed.

## Current native-caster sun scene (default off)

`rt_sun_cascade_current=0`: source/record-order-correct sun-only TLAS clone,
private same-submit build insertion, cloned alpha/stamp, strict whole-ray coverage.
Design, tests and remaining material/LOD/volume gates: docs/SUN_CURRENT_SCENE.md.
This is not a passing exploration-shadow gate; zero certified pixels is INVALID.
