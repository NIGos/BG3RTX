# Plan M5: water (coverage gap 1)

2026-10-31, docs only. Main 776441e, ABI 24. Design and gates: [P6_WATER.md](P6_WATER.md)
(10-28, still valid). This file updates it to the current architecture and orders the work.
Runs after the consolidation live batch passes and after M4.1 (HANDOFF "Next" 3).
Live job for increment 1: [tools/live_batches/m4_m5_first/](../tools/live_batches/m4_m5_first/README.md).

## Current state

- No water code, key or default. Forward water (`FLUID_Water_Plane_flowmap_huge_PixelLighting`)
  changes 89.2% of the S1 view (COVERAGE gap 1); there the G-buffer is empty (29.6% of S1) or
  holds the sea bed. D-RF at the docks: 85% of traced rays start below the horizon (the bed under
  the water, TRACK_D_REFLECTIONS 10.4). GI, AO, sun and D-RF all see the bed or nothing.
- The asset DB already flags water: `BG3_MAT_CLASS_WATER`, `BG3_MAT_F_WATER`
  (`common/assetdb_api.h`); FLUID roles (ColorShallow/Deep, Roughness_Base, Reflectance_Base,
  Flowmap, WaveStrength) are in the DB, not yet exposed per role (P6 inputs table).

## What changed since P6_WATER (10-28) and what it means for water

| Change | Effect on the water plan |
|---|---|
| D-EM captures forward draws (`kMatForward`, lava) into its own soup, converts them with the pass's camera UBO, builds a separate TLAS + records per game submission (`layer/emissive.cpp`, bvhsplit scene 1) | P6 steps 1-2 are mostly done as a pattern: water = the same path with its own class, soup, records and TLAS. `emissive.cpp` explicitly excludes `BG3_MAT_F_WATER` today. One frame of latency (consumers see last submission's build) |
| D-EM policy moved to the core (ABI 24, `common/emissive_policy.h`, `core/em_policy.cpp`) | Water draw classification and record fields go in a pure `common/water_policy.h` with tests, evaluated in the core; the host only captures and builds |
| Shader variants are defines (`?NAME=1`, `common/shader_variant.h`, `shaders/variants.txt`) | Water consumers are defines on existing passes, not wrapper files; key off must give byte-identical SPIR-V |
| Hit-material provider (`rt_hit_material`) and `native_hit_direct` | Reflected rays from water can shade static VT hits (M4.2 tier b); until then they keep the native reflection |
| Typed descriptor contracts after GI R2 (`common/native_hit_gi_bindings.h`) | Every water dispatch gets its own typed contract, checked against SPIR-V |
| `cfg_hot()`, `BG3RTX_DIAGNOSTICS` CMake option | Water stats/coverage code is diagnostic host code where it lives in the layer |
| Queue authorization receipts (M0), drafts outside pending | Live water jobs follow tools/live_batches/ |

The ABI cost: the core needs the water scene (TLAS, record buffer, build camera), like the D-EM
scene (ABI 18). One bump to ABI 25, merged early, alone (AGENTS: ABI changes conflict).

## Increments, in order (each off by default)

### M5.1 Water geometry and same-frame coverage, no colour change: `rt_water_geometry=0`
- Capture: forward draws whose FS is a DB water material (`BG3_MAT_F_WATER`, pass FOR), exact
  shader variant matched; unknown FLUID variants are counted, not captured. Own soup with
  overflow counters, own BLAS/TLAS (not the opaque TLAS: no water blockers in other queries).
- Record per draw: material id, Roughness_Base, Reflectance_Base (DB roles from the mapped
  constant buffer at record time, as D-EM radiance), triangle range, build camera.
- Coverage pass (core, after the build, static camera): primary ray per pixel against the water
  TLAS, hit t_w, against the opaque depth t_o. Counts: water hit, in front of opaque, behind
  opaque beyond `depth_tol` (a violation: visible water cannot be behind the bed), hit on an
  empty G-buffer pixel. `rt_debug=21`: green in front, red behind, blue on empty G-buffer.
- Files: `layer/emissive.cpp` capture path generalized or a sibling `layer/water.cpp`;
  `common/water_policy.h` + `tests/water_policy_test.cpp`; `common/host_api.h` (ABI 25 water
  scene); `core/water.cpp` (coverage pass, stats); `shaders/gi/water_coverage.hlsl`;
  `common/settings.h`, `bg3rtx.ini`, GUI `exp_water_geometry` (live like `rt_emissive`: every VS is
  already patched for XFB; experimental, off in presets).
- Offline: policy tests on the DB water/lava/glass/VFX families (lava stays D-EM, glass and
  VFX excluded); settings test; shader compile; key off: identical SPIR-V and no change in
  any existing pass (the water TLAS is bound nowhere else).
- Live gate (job `mm2`, S1): `[water] capture` reports supported draws > 0 and the unknown
  count; violations (behind opaque) 0 beyond the derived depth tolerance; in-front + empty
  coverage compared with the forward pass's changed-pixel share of an existing COVERAGE capture
  of the same save (review, not tuned); final image OFF/ON/OFF equal within the OFF-repeat noise
  (P6 "Composition": the key changes no colour); no device loss, no validation IDs beyond the
  baseline on a `-Validation` run.
- Cost: `[water] build GPU` ms (capture + convert + BLAS + TLAS). D-EM built 36k triangles in
  0.31 ms; the "huge" plane's triangle count is unknown, report it.

#### M5.1 status (2026-11-01, branch m5-water, offline; READY_FOR_LIVE)
- Built: `rt_water_geometry` (live, GUI `exp_water_geometry`, `rt_water_xfb_mb=32`/`_max_mb=256`). The D-EM
  pass hook (geocapture `em_pre_draw`, the B10G11R11 + D24S8 forward/effects signature) hands each draw to
  `layer/water.cpp`; audited draws get per-draw XFB into the water soup, rt_convert with the pass camera
  (set 0 b2), one BLAS (a geometry per draw) + a one-instance TLAS in a fenced command buffer of ours after
  the game's submit. Nothing binds it (no image change). Key off: no hook, nothing allocated.
- Policy in the core without an ABI bump: `common/water_policy.h` via `Bg3CoreApi::em_record` with
  `forward == BG3_EM_REQ_WATER` (VS hash >> 32 in `decal`); a core without it returns no `WATR` magic and
  nothing is captured. Classes: supported (7 audited FS/VS pairs, below), unknown (other forward Fluids
  water variants), excluded flow (falls, mist, foam, bridges), excluded VFX; lava, glass are not water.
- Audited pairs (traced 2026-10-30, each VS read): 7b44a567/60d4357d huge_PixelLighting (S1 docks),
  1d4bff2e/824dcd7b flowmap (S1), fda628a5/b02e4616 flowmap_PixelLighting, 64d9cb77/f0491919 huge_PL_V2,
  06e66a6f/7a485e6a PL_noCaustics_Shadows, 857ccb90/ffc5bb29 flowmap_noCaustics, 68068cab/bbdf7b8a Water_Plane.
  The DB has 31 Fluids templates; FLUID_Water_Plane* forward FS without a trace are counted as unknown.
- Vertex shaders: world Y displacement only (flowmap at the mesh UV, noise at world xz * m10 + time * m11,
  set 1 b1), gl_Position = camera-relative world * `_m2`: XFB captures the animated surface. The normal is
  the vertex frame (not the displaced one): M5.2 takes the shading normal from the FS.
- Depth (open question answered): water does not write depth. No water FS writes FragDepth, and the
  D24S8 attachment is bit-identical before and after all ten traced water-only forward passes (DB
  DepthStateID 6 for these templates); mixed passes changed 13 and 427 of 682k px (other draws). The
  coverage gate compares t_w with the bed depth: visible water must be in front of it. The live log counts
  the pipelines' depth-write state (`depth write on/off/dynamic`).
- Log: `[water] capture` (supported/unknown/excluded draws, triangles, overflow, failures),
  `[water] coverage` (captured share of water draws and triangles, depth-write state, sampled positions:
  non-finite, collapsed, height range), `[water] build GPU` (median, p90, geometries, triangles per build),
  `[water] families`.
- Not done: the per-pixel coverage pass (needs the water scene in the core: ABI 25) and the record buffer
  on the GPU (no consumer). Live job: `tools/live_batches/m5_water/` (replaces the `mm2` draft).

### M5.2 Ray-traced reflections on the water, measured: `rt_water_reflections=0`
(Reordered 2026-11-01: the trace comes first, the native-term recompute is part of M5.3's composition.)
- Built (branch m5-water-rf, offline, READY_FOR_LIVE): `core/water.cpp`, `shaders/rt/water_reflect.hlsl`,
  `shaders/rt/water_view.hlsl`, `common/water_scene.h` (+ `tests/water_policy_test.cpp`). GUI
  `exp_water_reflections` (Experimental, needs `exp_water_geometry`). Live key, off in every preset.
- Scene without ABI: layer/water.cpp publishes each submitted build (TLAS, the pass's absolute camera
  from its UBO byte 208, the first supported draw's roles) in persist memory `bg3rtx.water_scene`
  (sequence lock, the BG3_DEVICE_INFO_KEY pattern); the core uses a build at most 2 frames old.
  The ABI 25 water scene (records on the GPU) can replace it later; main's ABI 25 is the replay pack.
- Where: core/gi.cpp run_post after the GI composite (diffuse + specular = the final lit opaque,
  pre-fog, pre-SSS), two dispatches with the scene TLAS + alpha set of the other RT passes:
  `water_primary` (water TLAS vs the bed depth, shifted by cam_now - cam_build: exact static, one
  frame of water animation lag) and `water_reflect` (one mirror ray per visible water pixel).
- Normal: the GEOMETRIC normal of the displaced wave mesh, from the neighbours' water hits (per axis
  the better-continuing neighbour); world up where an axis has none (counted). The FS's shading
  normal (three flow-mapped normal maps, 7b44a567 `_1205`) is not reconstructed: M5.3 needs it.
- Radiance: tier (a) only, as D-RF: the hit projects on screen, depth_same_surface at that pixel's
  geometric normal (k = 1), ray-facing, not skin. Misses (sky), hidden, off-screen and aborted rays
  are unresolved and counted, never invented. Weight E = Schlick(F0 = 0.16 Reflectance_Base^2).
- Output: `wOut` (L, E) at render resolution, the composition input; nothing in the game's image
  changes. Views over the forward water target (it has STORAGE: trace 20261030-152728 i50311 usage
  31), written at the end of passes whose first/last FS is audited water: `rt_debug` 21 coverage
  (the M5.1 view), 22 L, 23 tiers, 24 E*L.
- Log: `[water] reflections` (water share, in front / empty G-buffer / at bed / behind bed, ray
  tiers, normal fallbacks, mean E, L, E*L, shift, skipped frames by reason) and `[water] reflections
  GPU` (median, p90; scene size and age; F0; view count) every rt_timing_frames.
- Offline: 71 CTests (water_policy_test: publication, torn reads under a concurrent writer, staleness,
  shift, F0), all shaders compile, ordinary SPIR-V byte-identical (no existing shader changed).
- Live: job `m5w2` in tools/live_batches/m5_water/ (S1 docks, OFF/ON/OFF PNGs, views 22 and 21, cost).
- Gate: image ON = OFF within the OFF-repeat spread; behind bed 0; tiers, E and cost reported.

#### Composition plan (M5.3, not built)
1. The water FS (7b44a567, end of main) is opaque: `C = mix(Refr, (Lw * X + G) * T + S, a)`, Refr the
   colour copy (set 2 b4), X `_2063` / G `_2062` atmosphere terms, T, S froxel fog (set 2 b17), a the
   depth/foam opacity `_784`, and `Lw = direct + (Kd + Ks) * k`, k = set 2 b0 `_m0[0]` * 100.
2. `Ks` (`_3379` after the probe loop) = probes (cube b22 at the roughness mip, local b32[] by the b20
   records) * (F0 * DFG.x + DFG.y), DFG the LUT b25 at (N.V, roughness): the only reflection term.
3. The pass target has STORAGE, so a delta pass at the water pass's end replaces Ks exactly:
   `C' = C + a * T * X * k * w * (E_lut * L_rt - Ks)`, w = 1 on tier (a) hits, 0 otherwise (C' = C).
4. It needs the FS's own N (normal maps + flow, `_1207`), roughness `_1480`, F0 `_1470`, a, T, X, Ks per
   water pixel: recomputed from its sets 1-2 (queried in the layer's water draw hook, as D-EM does),
   with the trace's E and L_rt taken at that N (the geometric normal of M5.2 only locates the surface).
5. First with replacement off: recomputed C against the game's C over water pixels (D-EN4 style),
   gate below the encoding bound. Fallback if a or the fog cannot be recomputed exactly: a water
   pipeline swap (an FS define variant computing Ks from our wOut).
6. No double count: the native Ks is subtracted exactly; unresolved rays keep it. Gates: P6
   "Composition", "Physics", "Motion" (one moving run, key toggled), S1 and S8; tier (b) after M4.2.

### M5.3 Water reflections composed: `rt_water_compose=0`

#### M5.3 status (2026-11-01, branch m5-compose, offline, READY_FOR_LIVE)
- Built: `rt_water_compose` (GUI `exp_water_compose`, Experimental, needs `exp_water_reflections`; enabling needs a
  restart, off is live). Files: `common/water_compose.h` (record, buffer, ownership check),
  `common/water_fs_patch.h` (FS patch), `layer/water.cpp` (variants, swap), `core/water.cpp` +
  `shaders/rt/water_compose.hlsl` (copy, compose, rt_debug 25), `tests/water_compose_test.cpp`.
- Plan step 4 (recompute the FS's N, a, T, X, Ks from its sets) was dropped: N needs the FS's interpolated UV
  and tangent frame, and a, T, X need its flow-mapped textures at that UV; none of it exists after the pass.
  The "minimal extra copy" instead: the FS writes its own values. `water_fs_patch.h` adds to the audited FS
  (only 7b44a567 huge_PixelLighting has an id table yet; the other six keep the probe, counted "without") eight
  stores at the end of main through a physical-storage-buffer address baked into the module (the game's
  layout and sets untouched) plus EarlyFragmentTests (valid: no discard, no depth write). Record per pixel:
  `A = a T X E`, `B = a T X k Ks`, N, C, t, roughness (ids `_784 _4096.w _2063 _3799 _3379 _3303 _3352 _1207
  _4335 _1480 _5`). The patched dump passes `spirv-val` (vulkan1.3) and decompiles to the intended stores.
- Composition at the end of each water pass: `C' = max(C + A L - B, 0)` where the mirror ray along the FS's
  own N resolves on screen (tier a, as M5.2), the probe kept otherwise. L is lit radiance (b10 + b11), whose
  unit is the probe's times k (D-EN4), so A has no k and B does: a ray that sees what the probe holds
  changes nothing. Kept as well: roughness above `rf_mirror_rough` (the probe is a filtered lobe), N.V <= 0,
  and records whose C is not the target's (overlapping water fragments store in any order; the check is
  one ulp of B10G11R11, derived). The traced path's own fog (water to the hit) is not applied (as D-RF).
- Limits: the variants exist only if the key was on when the water pipelines were created; 160 MB record
  buffer (`rt_water_compose_mb`, 5.2 Mpx); shaderInt64 required (`sharc_device=1`); the pass must have an
  audited water FS first or last (as the M5.2 views); compose needs this frame's lighting recorded first.
- Log: `[water] compose variants` (layer), `[water] compose` (records, owned, kept, tiers, unresolved share),
  `compose energy` (mean C, B removed, A L added, mean delta absolute and % of C), `compose GPU` (copy +
  compose dispatches). Offline: 73 CTests (+water_compose_test with the dumped FS), 745 shaders, ordinary
  SPIR-V byte-identical (no existing shader changed), QA detectors, dry run of `m5w3`.
- Live: job `m5w3` (S1, compose on at launch, OFF/ON/OFF PNGs, rt_debug 25, cost), after `m5w2`.

### M5.4 Water shadows: `rt_water_shadows=0` (sun and local visibility on the water receiver).
### M5.5 Transmission and absorption, only after medium and material mapping are proven.

## Open questions

- Does the water pipeline write depth? Answered offline (M5.1 status): no; the gate compares t_w with
  the bed depth. The live `depth write` counts confirm it per pipeline.
- S1 QuickSave_3784 is labelled the foundry (TEST_SCENES) but COVERAGE measured the sea there.
  The `mm2` expect line requires supported water draws, so a view without water is INVALID.
- Previous-frame water reuse needs a deformation/flow motion contract; M5.1 measures static only.
