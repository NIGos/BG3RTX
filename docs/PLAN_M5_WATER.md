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

### M5.2 Native water term recompute, replacement disabled: `rt_water_check=0`
- Extract the forward FLUID shader's reflection, sun and local terms from the dumped SPIR-V
  (`captures/shaders`), recompute the reflection term at water pixels with the same resources
  and compare (D-EN4 style, as `rf_validate`). Needs the water receiver's displaced shading
  normal and roughness: from the shader's own evaluation, not from XFB positions.
- Gate: mean |residual| over water pixels below the derived encoding/derivative bound.

### M5.3 Water reflections: `rt_water_reflections=0`
- Replace only the validated reflection term at its composition point: a forward FS define
  variant if the layer can swap the water pipeline, else a delta pass with the exact old term.
  Hit radiance: tier (a) screen, tier (b) once M4.2 passes, else the native term.
- Gate: P6 "Composition", "Physics", "Motion" (one moving run, key toggled), S1 and S8.

### M5.4 Water shadows: `rt_water_shadows=0` (sun and local visibility on the water receiver).
### M5.5 Transmission and absorption, only after medium and material mapping are proven.

## Open questions

- Does the water pipeline write depth? Answered offline (M5.1 status): no; the gate compares t_w with
  the bed depth. The live `depth write` counts confirm it per pipeline.
- S1 QuickSave_3784 is labelled the foundry (TEST_SCENES) but COVERAGE measured the sea there.
  The `mm2` expect line requires supported water draws, so a view without water is INVALID.
- Previous-frame water reuse needs a deformation/flow motion contract; M5.1 measures static only.
