## RT capture stride split: 88 bytes only for strand-frame shaders (branch claude/capture-stride, READY_FOR_LIVE, 2026-11-09)

- Bug: with rt_native_hair=1 every vertex shader was captured at 88 bytes per vertex. The zoomed-out S8 view
  (rv1 12:13 / 12:53, user settings) wanted about 2065 MB of the 2047 MB capture (opaque peak 1613 + alpha 446)
  and dropped 1-8 opaque draws per frame.
- New key `rt_capture_wide_split` (startup, default 0, needs rt_bvh_split=1): only the vertex shaders that
  export the native strand frame (236 of 2403 dumped VS) keep 88 bytes; all others are patched at the stride
  without native hair (64 with rt_hit_material). Wide draws get their own region at the top of the alpha
  region; cascade wide casters are a second geometry of the cascade BLAS. No native hair data is dropped.
- Readers: no shader change (split-mode readers take the stride per instance/geometry/push constant).
  Shader identity vs origin/main: 226/226 requests byte-identical; shaders/variants.txt delta 0. CPU: per-draw
  stride in geocapture, bvhsplit (draws, pool meshes, per-draw BLAS, OMM/groom/hold/extract jobs), rtscene
  (3-region conversion). Key off: identical code paths and values.
- Tests: new CTest capture_stride_test (activation, module stride, real patch_vs decorations 88/64, wide
  placement, cache/refit guards, S8 bytes).
- Expected S8 bytes: opaque 1613 -> about 1173 MB, alpha <= 446 MB: about 1.5-1.6 GB of 2047.
- Live (prepared, not run): rel095_verify rv2-s8-capture-stride (rv1 plan, key on from launch; a startup key
  cannot toggle in one run). Every window: opaque dropped 0.00, made opaque 0.00, stride split "left out 0.00",
  cascade buffer full 0.0; opaque peak vs rv1's 1613 MB; `[geo] RT capture stride split` and `[cascade]
  wide-stride casters` lines present. Report the gi GPU ms against rv1 (the 88-byte layout cost about 2 ms).
