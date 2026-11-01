## Water on Ultra (rt_cascade_casters) and capture sizes for wide vertices (branch worktree-agent-ae9b318078664bcac, READY_FOR_LIVE, 2026-11-01)
- Bug 1: core/water.cpp skipped every water pass while `rt_cascade_casters` (Ultra) was on: user log
  `[water] reflections (300 frames, 0 traced)`, `views 0`. Now the water reflect/compose pipelines build
  the cascade requests (common/shader_variant.h `water_request`): `RT_CASCADE_MASKS=1` (reflection,
  refraction and local-light rays trace 0x01, as core/gi.cpp's cascade variants) and, with WATER_SHADOWS,
  `RT_CASCADE_SUN_HIT=1` (only the water sun ray sees the roof casters, as gi_hit_sun at a GI hit).
  The key is read live like gi.cpp; a pipeline of the other request never runs. The skip stays (counted)
  for rt_omm, rt_native_tileset, rt_native_scalp, rt_native_hair, rt_hair_physical_origin, logged once
  per change: `[water] reflections skipped: no water variant for ...`.
- Shaders: 5 new requests in shaders/variants.txt, all compile; the 4 existing compose requests
  (water_copy and water_compose) are byte-identical SPIR-V to main; cascade variants have the same
  descriptor bindings and push size as the plain ones (spirv-cross reflect).
- m5_water (5 jobs) and m5_promotion (2 jobs) now run with `rt_cascade_casters=1`; they expect the
  `... RT_CASCADE_MASKS=1 water_reflect: built` (and compose) lines and forbid the variant skip.
- Bug 2: with rt_hit_material the capture vertex is 64 B, not 24 B, but Ultra kept rt_xfb_mb 384 and
  rt_cascade_mb 160 (user run: `opaque peak 510.6 MB of a 384 MB buffer`, cascade `buffer full 86-132`).
  A cap left at its default now follows the startup stride, rounded: 384 -> 1024 and 160 -> 427 at 64 B
  (88 B hair: 1408 / 587, the sizes the hair jobs set by hand). Any other value is used as set.
  common/rt_capture_budget.h `effective_mb`, unit tested in rt_capture_budget_test. Logged:
  `[geo] RT capture buffer N MB (rt_xfb_mb S, B-byte vertices)` and `[cascade] capture buffer N MB (...)`.
  The cascade stats line now prints the effective size (and the stale literal 64 is gone).
- Cost: none measured yet (water now runs on Ultra; its passes cost what the m5 jobs measure). Memory:
  +640 MB RT capture and +267 MB per cascade capture buffer with rt_hit_material at default caps. Layer change: game restart.
- User check: Ultra in S8 Water Queens House: the water reflects, and `[water] reflections (... N traced)`
  has N > 0; with rt_hit_material, no `opaque draws dropped` and cascade `buffer full 0`.
