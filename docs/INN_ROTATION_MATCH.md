# Inn rotation primary match (~83.5%): what the mismatch is

Source: `cb2-scene-wait-rotation-inn-fg2.1.182224` (inn, FG 2, 30 fps cap), 120-frame `rt stats`/`rt epoch` windows per phase.

| window | match | in front | behind | miss | pool match (share of hits) | soup match |
|---|---|---|---|---|---|---|
| static, wait 1 (p00/p01) | 98.93-98.98 | 0.17 | 0.79 | 0.09 | 99.67 (79%) | 96.7 |
| rotation, wait 0 (p03) | 30-31 | 40 | 24 | 4.4 | 23 (82%) | 72 |
| rotation, wait 1 (p07, p19) | 82.4-87.1 | 11.5-16.2 | 0.6-1.0 | 0.6-1.1 | 80-86 (79%) | 94-97 |
| rotation, wait 2 (p11, p15) | 80.9-92.0 | 6.4-17.6 | 0.5-0.8 | 0.8-1.0 | 79-92 (79%) | 93-97 |
| static after rotation, wait 2 (p13) | 87.0-87.2 | 12.08 (+-0.01) | 0.55 | 0.25 | 85.2 (82%) | 96.7 |

## What it is made of
- Not camera or timing. A timing error is two-sided (wait 0: 40% in front, 24% behind). With wait 1/2, behind
  and miss stay at the static level and only "in front" grows. Every window has scene wait 600/600 "got this
  frame's scene", 0 timeouts, and "instance data changed between record and submit 0".
- It persists at a static camera. p13, the view the rotation left behind, holds 12.08% in front, steady to
  0.01 over ten windows. The mismatch belongs to the view, not to the motion.
- Not capacity: geo drops 0, alpha/records full 0, 0 deferred, 0 rejected, 0 lost, TLAS "none 0" (cache 468 -> 1778 meshes).
- Not alpha leaks: "behind" (through a cutout) stays at 0.6%. The "opaque / vt / tex" split reads 0.000 because
  `pan_primary.hlsl` never counts slots 5-7 (only `rt_shadow.hlsl` with rt_debug=3/5 does): no evidence there.
- It is static (pool) geometry. Pool hits match 80-85%, the per-draw soup 94-97%, and pool is 79% of the hits.
  0.79 x ~19% accounts for about the whole in-front share.

## Most likely cause
Screen-door dithered camera cut-away (occlusion fade) of inn walls, beams and pillars between the camera and the
party. The game still issues these draws and discards most of their pixels with a `gl_FragCoord` dither. The
capture keeps discarding FS that are not in the alpha table as solid (`layer/geocapture.cpp` near line 1233;
QUALITY Q14; TRACK_D_ALPHA_GI 7.1), so the TLAS holds a wall that the raster shows through. Orbiting inside
the inn keeps such occluders in view. Open scenes have few, hence ~97% elsewhere. A LOD cross-fade would give
small in-front distances and a cut-away wall large ones; the next test separates the two.

## The mechanism, from the shader dump (2026-10-31)
- It is not a screen-door dither. 94 depth-prepass fragment shaders (46 of them also in `alpha_fs.txt`) cut
  with a world-space capsule: `if (flag == 1.0) f = min(clamp(max(0.5 n + d(P, m4->m0), 0) / m1),
  clamp(max(0.5 n + d(P, m4->m2), 0) / m3))`, `n` an edge noise in [-1, 1], `discard if alpha * f < cut`
  (cut 1e-4 to 1.0). The capsule is the frame UBO at set 0 binding 6; `flag` is a per-draw float in a set 1
  UBO at a shader-specific offset (`scripts/camera_fade_table.py` -> `shaders/rt/camera_fade_fs.txt`).
- The IGN + 8-step temporal dither in other prepass shaders (`52.98 * fract(dot(...))`) is a distance (draw
  range) fade, not the camera cut.
- User decision: the cut is for the camera only. Faded walls keep occluding in every RT effect (the game's
  lighting does not change when they dissolve either). So the TLAS is unchanged; only the measurement changes.

## rt_primary_fade_class (diagnostics only, layer + core)
- With `rt_primary_epoch=1`, `rt_bvh_split=1`, alpha on: the layer reads each fade shader's flag at capture,
  marks enabled draws in InstInfo (bit 14) and stores the capsule in the scene stamp (bytes 192..243).
  `gi/pan_primary_fade.hlsl` counts hits on marked draws; in front of the raster and within `d < r + 0.5` of a
  segment (the largest region the game can cut) is the class "faded occluder (expected)".
- Logs: `[camera fade]` every 300 captures (fade-shader draws, cut enabled, triangles, marked TLAS instances,
  the last capsule) and `rt fade class` with each 120-frame stats window (match, in front, behind and miss with
  the faded occluders excluded; marked-draw fronts outside the envelope stay mismatches).
- Key off: all 704 existing SPIR-V modules byte-identical; capture, TLAS and InstInfo unchanged.
- Unverified until live: that the capsule is in the same world frame as the view UBO's m6 (the FS adds m6 to
  the camera-relative position; the check adds the trace camera). Which end is the camera does not matter.

## Next test (one launch, inn, about 3 min, about 150 MB)
Repeat the p11/p12 rotation pair (wait 1), then hold the p13 view: 15 s of rt_stats + epoch, one trace with
`rt_debug=8` (tlas_front QA: log2 distance and opaque/alpha flag), one with `rt_primary_identity=1` (instance id
per front pixel -> mesh key, VS/FS from the `[bvh]` draw sample). Core-only, hot-reloaded: also log the pool/soup
front/behind counters 138/139/142/143, which are already counted. Confirmed if the front pixels are opaque, far in
front (high e8) and their FS is a discarding non-alpha-table dither. Fix (QUALITY Q14 plan): detect camera-fade
dither FS in `alpha_table.py`, record the per-draw fade, run the same dither hash in the any-hit of camera-coherent
rays, behind a key (off by default). Gate: p13 front 12% -> ~0.2% in the same run, mean brightness unchanged.

## Pool lag check (2026-10-31, branch pool-lag)
- Hypothesis: pool instances use a camera origin from another frame. The code has none to get wrong. The TLAS
  matrix is the game's camera-relative instance row, copied verbatim at vkQueueSubmit (`bvh_on_submit`), the same
  memory the G-buffer VS reads; the BLAS is object space (`mesh_extract.hlsl`: P = M0^-1 W0, W0 from the soup).
  The class-A VS (`646a930e`) multiplies by set 0 binding 2 `_m2`, the same matrix `xfb_to_world` inverts.
  Holds (`rt_dyn_hold`, also marked pool) are dropped while the camera moves.
- A slide would be two-sided (front and behind); the rotation excess is front only.
- The mesh audit already compares the submit-time matrix with the same frame's raster capture. New layer line:
  `[bvh] audits while the camera moved: N ok, M failed, max error E x tolerance`. No key, no behaviour change.
- Live test: tools/live_batches/pool_lag (README has the gates).
