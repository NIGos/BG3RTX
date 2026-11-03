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
  nothing is captured. Classes: supported (9 audited FS/VS pairs, below), unknown (other forward Fluids
  water variants), excluded flow (falls, mist, foam, bridges), excluded VFX; lava, glass are not water.
- Audited pairs (traced 2026-10-30, each VS read): 7b44a567/60d4357d huge_PixelLighting (S1 docks),
  1d4bff2e/824dcd7b flowmap (S1), fda628a5/b02e4616 flowmap_PixelLighting, 64d9cb77/f0491919 huge_PL_V2,
  06e66a6f/7a485e6a PL_noCaustics_Shadows, 857ccb90/ffc5bb29 flowmap_noCaustics, 68068cab/bbdf7b8a Water_Plane.
  S8 (11-01, no trace: the pair is the asset DB's StaticInstancedForward bshd, both modules dumped in
  captures/shaders; `python -m tools.assets shader <hash>` names it): 99c1fcef/e0530238 huge_PL_ExtraWaves (VS =
  60d4357d plus a second UV output), c4e92ccd/8113bd39 flowmap_NoCaustics_Waves (noise at world xz * m5 + time *
  m7, offset clamped below by m15). Both: Y-only displacement, the same gl_Position, no discard, no FragDepth.
  The DB has 31 Fluids templates; FLUID_Water_Plane* forward FS outside the list are counted as unknown.
  Pre-audit (11-01, offline, `py -3 -m tools.water_audit`: every Fluids forward pair of the DB with its dumps; CTest
  `water_audit_test` keeps the tables equal to the derivation and spirv-vals every patched FS). 16 supported pairs:
  the 9 above plus ced49ee3/60d4357d huge_PL_Shadow, d44c390b/1d64d76a flowmap_Brine, fb7cc2ea/d7d9539d
  huge_PL_SphereMask, 2bd6cb9d/ffc5bb29 and 269de904/8cdf7b4a PL_noCaustics (instanced, static), 4af3450e/bf2b63d0
  flowmap_OnlyNormals, 7813e701/0f00693c Debris (VS: vec4(P, 1) * set 0 b2 _m2, y negated, P = vertex + vec3(0, h, 0)
  or none). Compose ids (9 FS): 7b44a567, 99c1fcef, c4e92ccd plus fda628a5, 64d9cb77, d44c390b, fb7cc2ea, 2bd6cb9d,
  269de904 (the same end of main, sun without game visibility, three light loops). Supported without ids: 06e66a6f
  and ced49ee3 (Lsun carries the game's cascade shadow, 7 Dref samples: M5.4 would shadow twice), Debris (a mask
  scales the probe term and the direct factor), 1d4bff2e, 857ccb90, 68068cab, 4af3450e (no light loops: another
  lighting model). Rejected: WIP 0f9a1874/0516c804 (the VS displaces x, y and z). Not water by name: BloodRiver.
  No dump: the GlowLichen, flowmap_Shadows, flowmap_Test, Minimap, Reflections instanced pairs and every static
  variant but PL_noCaustics (the m5_promotion jobs dump the missing modules).
  Flymm Cargos Basement (save level CTY_Main_A; basement level CTY_DockWarehouseBasement_A, planes
  `NAT_RVR_DockWarehouseBasement_Dock_NoSub` / `_Grates`): FLUID_Water_Plane_flowmap_PixelLighting_noCaustics, mapped.
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
- Gate: image ON = OFF within the OFF-repeat spread; in front > 0 at a view with visible water; tiers, E and cost reported.
  "Behind bed" is occluded water, not a violation: the water draw writes no depth but depth-tests.
  m5w2 (11-01): QuickSave_3784 loaded the Steel Watch Foundry, not the docks: the sea plane 19 m below
  the camera was hit by 95.75% of the rays and occluded at 100% (no water on screen), so no reflection
  ray ran. Cost 15.1-15.9 ms: the statistics' per-wave atomics went to host-visible memory (gi GPU cost
  2.1 -> 19.5 ms). Fixed (branch water-fix): device-local counters, reduced per 16x16 group, copied to the host slot.
  m5w2 at S8 Water Queens House (11-01): cost 0.113 ms, in front 7.55%, screen hit 5.1%, hidden 4.3%. The scene
  has 7b44a567 (captured, 14474 triangles) and c4e92ccd NoCaustics_Waves (unknown then); 99c1fcef ExtraWaves only
  before the save loaded. `views 0 over 0 water passes` in the rt_debug 21/22 phases: the views (and M5.3) took a
  pass only when its first or last FS was audited water, and at S8 the water draws sit between other forward
  draws. Fixed: the core's water policy notes the command buffer of each supported draw (em_record), and the pass
  end takes it; the log adds `(K with water between other draws)`. Both S8 pairs added (above).

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
  (id tables, 9 FS since the 11-01 pre-audit above: 7b44a567 huge_PixelLighting, and for S8 99c1fcef ExtraWaves and c4e92ccd NoCaustics_Waves, whose
  end of main is the same `mix(Refr, fma(Lw X + G k (+ foam), T, S), a)` with Lw = direct + (Kd + Ks) k; all three
  patched dumps pass spirv-val; the other six keep the probe, counted "without") eight
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
  audited water FS first or last or a supported water draw (as the M5.2 views); compose needs a lighting
  of this or the previous app frame (after m5w3, below).
- Log: `[water] compose variants` (layer), `[water] compose` (records, owned, kept, tiers, unresolved share),
  `compose energy` (mean C, B removed, A L added, mean delta absolute and % of C), `compose GPU` (copy +
  compose dispatches). Offline: 73 CTests (+water_compose_test with the dumped FS), 745 shaders, ordinary
  SPIR-V byte-identical (no existing shader changed), QA detectors, dry run of `m5w3`.
- Live: job `m5w3` (S8 Water Queens House, compose on at launch, OFF/ON/OFF PNGs, rt_debug 25, cost), after `m5w2`.

#### M5.3 after m5w3 (S8, 2026-11-01, branch worktree-agent-a5b61ff5fff911842, offline; READY_FOR_LIVE)
m5w3 deciding lines: `compose variants: FS patched 4 ... water draws on a variant 174, without 0` (patching
works); `compose (120 frames, 3 composed, 1.00 passes/frame) ... passes without this frame's lighting 117`;
`compose energy: composed 12.75% of owned: probe removed (B) 0.01383, traced added (A L) 0.00452`; compose
GPU 0.022 ms, reflections 0.12 ms.
- BUG 1 (3 of 120 frames composed). run_post, and so the lighting copy and the stash, is recorded when the
  lighting's command buffer ends (core/gi.cpp `flush`); the game records the forward water pass in another
  command buffer, mostly before that. The pass required the stash of the same frame index and refused 117 of
  120. The GPU runs the frame's lighting before its forward pass in either case, so the compose now reads
  only GPU-current lighting data: one radiance/depth pair (already) and ONE camera block (was per slot) that
  each copy overwrites; the scene TLAS + set is the stash's when it is this frame's, else the pass stamps the
  current pair itself (`rt_scene`, `rt_scene_set` only for a build of this frame: never a wait). The stash
  (push constants, statistics slot) may be the previous lighting's: `lighting_use` in water_compose.h,
  lag 3 frame indices (2 presents per app frame with FG + 1 between the two recordings). A lighting that
  skips the copy clears the stash. Residual: if the frame's own lighting skips AFTER the pass was recorded,
  that pass composes with the previous frame's radiance and camera (a consistent pair, one frame old).
  Log tail: `passes on this frame's lighting N, on the previous lighting M, refused: no recent lighting N,
  no scene N; newer scene taken N`.
- ISSUE 2 was a statistics bug. The shader counted tier k at word 6 + k: tier 1 into the ray count and
  every logged tier was the next one. Corrected from the same line (rays = traced + tier 1): screen hit
  15.1%, hidden 6.6%, off screen 2.2%, sky miss 76.1% of traced rays, the M5.2 split (14.8 / 5.9 / 1.6 /
  77.7). Fixed (words 8..13, `kCsTier1`, test). Misses already keep the probe, which is the game's own
  environment at R (the TLAS has no sky and stops at rf_tmax): that is "the game's sky" for them. Tier (b)
  for hidden + off-screen hits (8.8% of rays) is not feasible in the compose pass: rf_hit_shade needs the
  game's lighting set 1 (query_set at the lighting dispatch), the D-LS sun set and the material set from
  `rt_hit_material_use`, all bound in the lighting's command buffer, and the forward pass is recorded
  before it (BUG 1). Plan, next step (`rt_water_hit_shade=0`): at the lighting (run_post, after water_rf),
  trace the previous frame's records (N, t; static water: P from the camera shift as M5.2) and shade their
  tier 2/3 hits with `native_hit_lit` (TRACK_D_REFLECTIONS 14) into an L_b image with the hit's t; the
  compose takes L_b where its own ray's hit distance matches (depth_tol of the hit) and the material is
  qualified, else the probe. Gate as rf_hit_shade: the tier (a) L_b / L_a check on water rays first.
- ISSUE 3 (A L / B = 0.33 where composed) is not a unit factor. The algebra is exact: A = a T X E,
  B = a T X k Ks are dC/dKs of the FS's end of main, so the change is a T X E (L - k Sp) (unit test 4b with
  the FS model: composed = the FS with k Sp replaced by L, for any L; L = k Sp is the identity). Units: the
  FS's k is set 2 b0 `_m0[0]` * 100, the pre-exposure (the same buffer scales its light colours), and the
  lighting's probe term is S * s1b13[0] * 100 with the same value (rrpost probe: b0 = b1 = s1b13); E is the
  FS's own LUT value in both A and Ks. So the composed hits are a third as bright as the probe's radiance in
  that direction: tier 1 means on-screen geometry, where the global probe holds sky or a distant capture.
  Physically justified darkening (the probe leak); mean delta over owned -0.32% of mean C. The new line
  `compose ratio A L / B over N composed px: below 0.5 X%, 0.5..2.0 Y%, above 2.0 Z%` decides it live: a
  unit mismatch puts every pixel in one bin, occlusion spreads them with mass near 1 (diagnostic split).
- Expect lines (m5w3): `compose (120 frames, (1[01]\d|120) composed`: with the fix every lighting slot
  gets a pass; 100 allows a slot without one where the recording order flips between frames.

#### M5.3 compose cost (m5w3-fg2 rerun, 2026-11-01, offline fix; READY_FOR_LIVE)
After the BUG 1 fix every frame composed, but `compose GPU 7.2 ms median, p90 8.0` at the main S8 view
(records ~290k/frame), 2.0-2.6 ms at the view with 85k records; frame time OFF 13.25 ms, ON 15.70 ms.
M5.2 traces the same rays in 0.08-0.12 ms, so it was not ray cost.
- Cause: the statistics. `cStats`, the params and the camera block lived in the host-visible buffer
  (`alloc_host_buffer`), and the compose did ~25 wave atomics per record wave into it, 6 of them returning
  the old value (the 64-bit carry): a PCIe round trip each, the 15 ms bug of m5w2 again. The log fits it:
  cost is linear in the records, 7.9 ms at 297k and 2.1 ms at 85k (27 us per 1000 records, about 0.9 us
  per 32-lane wave), with an intercept near 0 ms. The copy (2.7 Mpx, ~100 MB) and the image barriers are
  not the cost.
- Fix: `g_cdev` (device-local, `make_device_buffer`) holds the statistics slots, the params (now
  `CmdUpdateBuffer` at the lighting) and the camera block. The host buffer only receives each slot's
  256 B of statistics by `CmdCopyBuffer` after every compose dispatch, after the end timestamp, so it is
  outside the timed region. The shader reduces every word per 8x8 group in groupshared first: one L2 atomic
  per non-zero word per group. The 64-bit sums stay exact: a group's quantised sum is at most
  64 x 1e4 x 4096 < 2^32 (`kCsGroupSumMax`, static_assert). The composed colour, the target writes and
  the record consumption are unchanged (the diff touches only cstat / csum64 and adds the flush): the
  same image and the same statistics integers. Test 4e of water_compose_test checks that the group
  adds equal the per-wave adds, including the worst case (every lane at the clamp) across the carry.
- The log line is split: `compose GPU T ms median, p90 P (...); copy C ms median, p90 ..; compose
  dispatches D ms median, p90 ..` (`compose_ms`, test 4f).
- Expected cost: under 0.5 ms (copy ~0.05 ms: ~100 MB at the 5090's bandwidth; compose ~0.1-0.2 ms,
  the M5.2 cost for the same rays plus one 32 B record read per pixel). The m5w3 expect line now needs
  `compose GPU 0\.[0-4]\d\d ms median`: the 0.5 ms bound is 4 times M5.2's 0.12 ms for the same rays over
  two full-screen passes. Frame time ON minus OFF should fall from 2.45 ms to within the OFF spread.

2026-11-03 cleanup: retire rt_water_rr_mv, rt_water_fg_mv and rt_water_glint_aa.
Retain rt_water_rf_edge and rt_water_rr_hitdist off: their whole-water verdicts are invalid
for reflections (wf8); re-measure with the wf8/wf9 reflection-only metric.
See [KEY_AUDIT_20261103.md](KEY_AUDIT_20261103.md) for verdict sources and retained candidates.
The historical measurements remain here; they do not close the reflection-specific defect in M5.3d.

M5.3b screen-edge fade (`rt_water_rf_edge=0`, live, WATER_RF_EDGE on every compose variant, 2026-11-02): the
compose's choice is binary per pixel and per frame (tier 1: C + A L - B, else C with the probe), with no temporal
smoothing, and A L matches B only in its weight E, not in radiance (bins 29-31 log L / probe). Under a camera turn the
hidden / same-surface test is fixed to the view origin, but the screen border moves: hits leaving the screen switch
the pixel to the probe in one frame (S8 harbour flicker). The key applies D-RF's own fade
(gi/rt_reflections.hlsl, `rf_edge` 0.05 uv): C' = C + w (A L - B), w = saturate(min(quv, 1 - quv) / rf_edge). Both
terms carry E, so w blends two estimates of one reflection (no double count). Key off: every request byte-identical.
Live test wf2-s8-fg2 110212: REJECTED. Water-masked transient rate in the turn 3.29% with the key vs 2.25 / 2.32%
off (whole frame 1.87% vs 1.63 / 1.42%); cost +0.002 ms. The border is the rarest switch here (off screen 2.1% of
rays vs hidden 6.5%, miss 76.4%), and the 0.05 uv band modulates many hits by a weight that moves with the animated
normal. rt_water_rr_hitdist in the same run: null (2.33%). Next: a turn with rt_water_compose=0 (does the compose
cause the water flicker?), then the hit/miss and hidden switches.

M5.3c water energy audit (2026-11-02, offline on the kept S8 traces of m5w3 121606 / 093518 and m5p1 213849):
- The energy gates of M5 read 5c3fdabc, which runs BEFORE the forward water pass (frame.json order): blind to all of
  M5. The water pass's own dump is taken before the core's on_pass_end (layer/cmd.cpp end_pass), so the compose's
  writes first appear in the next dump of the scene image (423e0548 on S8).
- The plain after-water mean is not usable: the water FS's sun glints reach 640..1584 (dump max) and one such pixel
  moves the frame mean ~0.8%. rt_water_reflections (measured only, no water pixel changes) moved it +2.5% (m5p1), and
  the "-2%" of rt_water_compose in m5w3 is in the water pass's own dump too (before the compose ran): glint noise.
- In-frame measure (m4_promotion_gate `energy_fs` "water-compose"): 5c3fdabc + sum over water passes of (next
  scene-image dump - the water pass's dump). OFF-OFF spread 0.01% (m5p1). Results, frame relative:
  reflections compose -0.12% (121606), -0.19% (m5p1 step; 093518 -0.61% within its 1.5% OFF spread); its own log
  says -0.65% of the water's C over 8% of the frame: -0.085%. Sun shadows -9.7%, local-light shadows -2.0%,
  refraction -0.7% (m5p1 steps).
- Reflections: physically right, no bug found. Composed pixels are only tier 1 (10-15% of the rays; 77-89% miss
  to the sky and keep the probe). A L / B: 56.6% below 0.5, 34.5% in 0.5..2, 8.9% above 2, ratio of the means 0.78:
  a spread, not one factor (a unit error in E, k = _3799 or the pre-exposure would move every pixel by the same
  factor). The probe is sky-dominated, the resolved hits are walls and cliffs. A and B are the FS's own terms
  (its DFG LUT E, not water_reflect's Schlick stand-in; B subtracted once: water_compose_test).
- Sun shadows: physically right. The S8 FS (c4e92ccd NoCaustics_Waves) light the water with sun visibility 1. The
  rt_debug 26 trace (214156) shades the sea on the far side of the platform from a low sun (pillar shadows on the
  floor point the same way), the sunken courtyard pool inside its walls, and keeps the sea towards the sun lit.
  Occluded 69.7% of traced sun records, removed 94% of their D (R = C - D keeps sky and probe). The darkening is
  the removed glints (dump max 904 -> 222). Not yet checked: the traced visibility against the game's cascade on the
  opaque pixels next to the water (the M5.4c comparison).

M5.3d reflection-specific flicker attribution (wf8, 2026-11-03, prepared offline; no live run):
- The user's repeated report is reflections switching on/off while turning. wf2-wf7 whole-water transient and
  presented residual rates include native sun glints and do not establish a reflection-specific cause or exclusion.
- Existing rt_debug 25 encodes signed relative delta (composed minus probe-only divided by C luminance) and tier 1
  versus unresolved. Disable water sun/local shadows, refraction, game shadows and glint AA from startup: d includes
  every water change, so leaving those enabled would confound the reflection delta again. Same-frame record markers
  distinguish covered tier-1 zero delta from untouched black. No new shader requests or DXC work.
- wf8 captures three 8-frame diagnostic bursts (static calibration, two same-direction turns), two 8-frame native
  compose-off bursts, and five 16-present beauty windows in one S8 launch; tools/live_batches/water_flicker_wf8.
  The metric derives the threshold from native encoding plus static adjacent-frame noise and counts contribution
  on/off events, including tier 1 -> unresolved and unresolved -> tier 1. Tiers 2-6 are grouped by the existing view;
  water_reflect's different tier decision must not substitute for the compose decision.
- Beauty comparisons use the diagnostic reflection-support union mask, not the whole-water mask. Native compose-off
  water-output switching is a proxy: SSR is not isolated from probes, animated normals, refraction and glints. RR-off
  is attribution only. No result has been measured; old aggregate nulls cannot close these candidates.
- No qualified cheap stabilizer exists. Temporal tier hysteresis would need reprojected tier and radiance history,
  disocclusion/age validation and an off-by-default specialization constant. It is described, not implemented while
  shader compiles are busy. See predicted.json for candidate outcomes and README for budget and exact commands.

### M5.4 Water shadows: `rt_water_shadows=0` (sun and local visibility on the water receiver).

#### Design (2026-11-01, from the three S8/S1 FS dumps, spirv-dis --raw-id)
- What lights the water. In all three mapped FS (7b44a567, 99c1fcef, c4e92ccd) the end of main is
  `Lw = ((Ld + Sd) + (Ls + Ss)) * m + (Kd + Ks) * k`: Ld/Ls the local lights' diffuse/specular sums of the
  clustered loop (set 2 b16 records, b10-b12 tiles), Sd/Ss the sun's diffuse/specular times N.L, m =
  mix(1, 1, b26 _m1) = 1. The sun radiance is `Lsun = sun colour (atmosphere) * clouds * b0 exposure`
  (c4e92ccd `_3224 = _1485 * (_1657 * _20._m0[0])`, `_1657` = the two cloud-shadow layers b29/b30 and
  b4x): NO cascade or other geometric visibility. The FS has no shadow-map sample at all; its only
  textureGather is the local lights' shadow atlas (b28, records b27). So, correcting the brief: the game's
  water gets no sun shadow from the temple or the columns (sun visibility 1, clouds only), and its local
  lights with a game shadow map are shadowed by the atlas, the others not.
- The record can carry the sun term: `D = a T X (Sd + Ss) m` is dC/dDsun times the unshadowed sun term
  (the M5.3 G = a T X times the FS's own SSA values; ids per FS in water_fs_patch.h kFsIds: 7b44a567
  %3889 %3892 %3898, 99c1fcef %4087 %4090 %4096, c4e92ccd %3310 %3313 %3319). Two more words per record
  (half3 D + a marker), 40 B instead of 32.
- Replacement, visibility only: `C' = max(C - (1 - V) D, 0)` (built as `V C + (1 - V) R`, R = C - D
  recorded by the FS: see "M5.4 fix" below), V from one ray towards the sun disc from
  the FS's own surface point (P = t dir, its shading N for the offset; rt_shadow.hlsl's disc sample,
  `sun_radius`, `rt_tmax`) into the scene TLAS (no water in it: no self-shadow). Lit and unknown
  (aborted) rays keep C bit-identically; the clouds stay in D (they are the game's radiance, not
  visibility). With M5.3 on, the reflection term is added in the same expression (independent terms).
  The water's position is the lighting's camera times the FS's t, as M5.3 (one frame old when the pass
  composes with the previous lighting).
- Energy rule: the unshadowed radiance is the FS's own; only V changes. Static camera: ON can only be
  darker, by exactly the occluded records' D (logged as "removed"); V = 1 everywhere is the identity
  (unit test 4e). Brightening is impossible by construction (D >= 0, clamp at 0).
- Local lights (M5.4b, built 2026-11-01: see "M5.4b status" below; the design changed: every unshadowed
  light is traced, not one RIS light, and the FS's own froxel list is walked): the loop sums are not separable per light in the record. Plan was: record
  Ld + Ls (G times, two more words) and reuse the D-LS selection (TRACK_D_LOCAL_LIGHTS RIS, one light
  per pixel) only for lights WITHOUT a game shadow map (V_game = 1 in the FS): evaluate that light's
  unshadowed term with the FS's own BRDF terms (Burley diffuse 0.3183, GGX with the FS's F0/roughness),
  trace its visibility, `C' = C - (1 - V_k) D_k / p_k`. Gate first (D-EN4 style): our sum of D_k over all
  unshadowed lights reproduces the recorded local sum within half precision where no shadowed light
  contributes; shadow-mapped lights keep the atlas (replacing them needs the FS's per-light atlas term).
- Cost estimate: one first-hit-terminating ray per water pixel with a sun term. The deferred sun pass
  traces 2.73 M rays in 0.08-0.11 ms (TRACK_D_ALPHA_GI); S8's water is ~8% of the view, so the sun ray
  adds ~0.01-0.02 ms to the compose dispatch (M5.3: 0.022 ms copy + compose). Expected `[water] shadows
  GPU` < 0.05 ms.

#### M5.4 status (2026-11-01, branch worktree-agent-ae7ec6c83bc4f7137, offline, READY_FOR_LIVE)
- Built (sun only): `rt_water_shadows` (GUI `exp_water_shadows`, Experimental, needs
  `exp_water_reflections`; enabling needs a restart like rt_water_compose, off is live). Works alone or
  with `rt_water_compose`. Files: `common/water_compose.h` (40-B record, `record_d`, `shadow`, flags, sun
  statistics words 32-43), `common/water_fs_patch.h` (sun ids, `patch_fs(..., sun)`), `layer/water.cpp`
  (40-B buffer and the sun patch when the key is on at creation; swap with either key), `core/water.cpp`
  (variant choice, flags, device-local statistics, `[water] shadows`), `shaders/rt/water_compose.hlsl`
  `?WATER_SHADOWS=1` (sun ray, per-group reduced statistics, rt_debug 26).
- Key off: the patch, the buffer and the compose shader are M5.3's word for word (test pins the FNV of the
  three patched modules made by main 92c71e0; the plain water_compose.hlsl compiles to identical SPIR-V;
  no other shader changed). The WATER_SHADOWS variant also moves M5.3's own counters to device-local
  memory, reduced per 8x8 group (the plain M5.3 variant still adds per wave into the host slot).
- Offline: water_compose_test 4e (the FS model: shadow = the FS with sun visibility V, identity at V = 1,
  D never above C, both swaps together) and the sun patch of the three dumps (spirv-val clean, decompiles to
  `D = G * ((Sd + Ss) * m)` stores at words 8-9); 75 CTests; shader_variant_test lists the fixed request.
- Log: `[water] shadows (N frames, P passes, variant sun): sun records R/frame = x% of owned (no sun term,
  invalid); traced y% of sun records, occluded z% of traced (traced shadow share w% of sun records),
  unknown; sun term D, removed, % of D; mean removed over owned = q% of mean C; clamped; GPU median, p90`.
  `[water] compose: FS ... patched (a -> b words, with the sun term)`; compose record buffer `of 40 B`.
- Live: job `m5w4-water-shadows-s8-fg2` (tools/live_batches/m5_water). Deciding: `[water] shadows` with
  traced > 0 and occluded > 0 in the ON phase at S8 (the temple and columns over the pool), `removed`
  > 0 and only on water (shot-on minus shot-off-a coloured only where rt_debug 26 is red), shot-off-a vs
  shot-off-b within the OFF repeat noise, GPU median against the estimate.

#### M5.4 fix: the record holds R = C - D (2026-11-01, branch worktree-agent-abd9303a6e22b012e, offline, READY_FOR_LIVE)
- Live m5w4 at S8 (log `m5w4-water-shadows-s8-fg2.1.121950`): the shadow geometry is right (the far,
  sun-side rim shadows the far half of the pool; the game's glint sat inside that shadow), the shadowed
  half dark with black and bright specks. `clamped 145573` is the 120-frame total (the log prints the raw
  window counter), not per frame: 1213 per frame = 0.44% of the sun records, 0.7% of the occluded ones.
- Audit of the three dumps (spirv-dis --raw-id): D is exactly dC/dSun times the sun term. Sd = Lsun x
  diffuse x N.L, Ss = Lsun x GGX x N.L, Lsun = sun colour x clouds x b0 (the probe has b0 x 100: both the
  FS's own); C = mix(refr, fma(fma(Lw, X, e), T, S), a): a, T and X are all in G; no later multiply, no
  exposure or fog mismatch, no ambient or caustics in Sd/Ss, M5.3's B is a disjoint term. So C - D is the
  FS with the sun at 0 in exact arithmetic.
- Root cause: precision. The compose formed C - D from the B10G11R11 target (one ulp 2^-6 of C for R/G,
  2^-5 for B; the ownership check allows 1 ulp) and a half D. On the shadowed pool the sun is a median
  66% of C (rt_debug 26 share; 10% of the pixels above 97%, the glint up to C ~ 976 with a rest ~0.05):
  there the target's rounding is larger than the rest, so C - D clamps to black or keeps up to an ulp of
  the glint (bright specks). The model test (4e) used exact floats and could not see it.
- Fix (key-on path only): the FS stores R = C - D in f32 then half, and lum(D) (words 8-9:
  `half2(R.r, R.g)`, `half2(R.b, lum D)`, no new words); the compose writes `V C + (1 - V) R` (+ A L - B
  with M5.3, - F sum with M5.4b): R where occluded, C bit for bit where lit or unknown. Statistics: sun term
  and removed from lum(D). Key off: the M5.3 patch FNV and the plain water_compose SPIR-V unchanged (SHA1
  7695c1ff... before and after); the sun and LS compose variants change and pass spirv-val.
- Test 4j (would have caught it): 20000 random FS terms (sun 1e-3..3e3, up to the glint) through the real
  encodings (B10G11R11 6 / 5 bits, nearest and toward zero; the record in half): at V = 0 the compose equals
  the FS evaluated with its sun colour set to 0 within the record's half rounding; the old C_target - D
  fails it (counted) and clamps.
- Expected live (same S8 view): occluded % of traced ~62.6%, sun term D ~0.22 per sun record, removed
  ~80% of D and ~46% of mean C (the darkening is the FS's own sun share: unchanged), `clamped` 0 in every
  ON window (was ~145k per 120 frames; gate below 120 per window, expect_every), no black or bright specks
  in the shadowed half (shot-on). The shadowed half stays darker than the lit half: the FS's water there
  is mostly sun (refraction of the shaded pool floor plus the sky probe remain, blue-tinted).
- Open, same class: the M5.4b local-light removal still subtracts F sum (1 - V_k) c_k from the target's C,
  so a pixel lit almost only by one occluded unshadowed lantern can speckle the same way. Not a small fix:
  which lights are occluded is known only in the compose, so the FS would have to record Q = C - F L
  (half3, two more words after main's refraction words 24-25: record 112 B, buffer sizing, offsets, pins)
  and the compose write Q + F (sum_k s_k c_k - removed). Do it if m5w5 shows `clamped` above ~0.

#### M5.4b status: local-light shadows (2026-11-01, branch worktree-agent-acc8d7bdca31e7569, offline, READY_FOR_LIVE)
- What the FS does (spirv-dis --raw-id of the three dumps): three loops over the froxel's list (set 2 b10
  froxels, b11 index list, b12 lights: points, then spots; b16 projectors), each carrying (specular,
  diffuse) as loop header phis, with the game's atlas term (b27 views, b28 atlas) only for lights with
  `m7` bit 0. The set 2 b10/b11/b12/b26/b27 decorations are the D-LS ones of f469db78 (b23/b24/b25/b39/b40;
  same offsets, 80-B lights, 544-B std140 constants), and the loops' per-light terms are bg3_light_eval's.
- Built: `rt_water_ls_shadows` (GUI `exp_water_ls_shadows`, Experimental; restart to enable, off is live;
  alone or with the compose and sun keys). The record grows to 96 B (`kRecordBytesLs`, 3 x the MB of
  `rt_water_compose_mb`): F = a T X m, F0, the points + spots sum L (the spot loop's header phis), the
  channel, the diffuse colour, the projector share, the froxel depth input, P, N and roughness in f32
  (ids per FS in water_fs_patch.h). The first water draw of a pass hands its own set 2 b0/b10/b11/b12/
  b26/b27/b28 to the core (core/em_policy.cpp, at the draw: the pass end may have another draw's sets);
  the WATER_LS compose binds them at 22..28 (bg3_lights.hlsli), walks the FS's froxel exactly as the FS
  (log2 z slice, pixel tile, counts), recomputes c_k (bg3_light_eval) and s_k (bg3_atlas_shadow, the
  12-tap PCF with the FS's IGN) and traces one hard ray to each unshadowed light's centre (ls_clear short).
  `C' = max(C [+ A L - B] [- (1 - V) D] - F sum_k (1 - V_k) c_k, 0)`. Shadow-mapped, lit and unknown lights
  keep their term: nothing occluded = the target is not written. Projector lights are not replaced.
- Sum match (the gate first, as planned): per record `max_c |sum_k c_k s_k - L| / max_c` binned at 2^-10 (one
  half ulp: the record's rounding, the f32 recompute ~1e-6) and 10%. Offline (water_compose_test 4g, the
  FS's loops transcribed from the glsl against bg3_light_eval, random lists: channels, ranges, cones,
  back-facing lights, both falloff models, the global scale, atlas-shadowed lights): 9124 of 9124 checked
  records within 2^-10; a dropped light is never within (3965 cases), an extra light is far. Live: the
  `sum check` bins of `[water] local shadows` (gate: within >= 99%, ratio of the means 1 within 0.1%).
- Offline: 76 CTests (4g sum match, 4h removal algebra: identity at V = 1 bit for bit, F = dC/dL, with the
  sun and reflection swaps; 4i words), the three LS-patched dumps pass spirv-val and decompile to the 14
  stores; the sun-only patch equals the M5.4 module word for word (FNV pinned), the M5.3 one likewise; the
  plain and WATER_SHADOWS compose SPIR-V byte-identical (SHA1 before / after), the LS variant validates.
- Log `[water] local shadows`: passes with / without the light list, local records, lights evaluated /
  shadow-mapped / traced per record, occluded % of traced, the sum check bins, removed % of F L and of mean
  C, projector share, clamped, GPU. rt_debug 27: red occluded (share), green lit, dark magenta only
  shadow-mapped lights, blue unknown, dark grey no local term, white sum check beyond 10%, yellow invalid.
- Cost estimate: D-LS traces 2.31 rays/px over 2.73 Mpx in 0.34 ms; the water records (~290k at S8's main
  view, ~10% of that) with the same list: ~0.04-0.08 ms, plus the atlas PCF of shadow-mapped lights for the
  check. Expected compose GPU ON minus OFF < 0.15 ms.
- Live: job `m5w5-water-ls-shadows-s8-fg2` (S8, 132 s, OFF/ON/OFF). If S8's water has no local light, its
  precheck expect line fails (INVALID, not a pass): then a lantern-lit water scene is needed.
### M5.5 Transmission and absorption, only after medium and material mapping are proven.

#### The game's medium (2026-11-01, from the three FS glsl and the Materials.pak templates)
Template values via `python -m tools.assets dump Public/Shared/Assets/Materials/Fluids/<name>.lsf`; the CB offsets
are the templates' StaticInstancedForward offsets (set 1 b1). Level instances may override them; the runtime CB
is not logged yet.
- Depth: `d = depth(set 2 b5) * _15._m4 - z`, the bed's linear depth minus the fragment's view depth (location 0):
  a difference along the camera axis at the pixel, NOT the path length along any ray (unrefracted or refracted).
- Opacity a (the mix weight of `C = mix(Refr, surface, a)`), with `ramp(x) = 1 - (1 - clamp(x))^2`:
  7b44a567 huge (the S8 sea): `clamp(foam^Foam_Opacity_Exponent * Foam_Opacity + max(ramp(d / OpacityBlendDistance),
  ramp(d / EdgeBlendDistance) * Opacity_Base) * Opacity + caustic mask) * FadeOpacity`; template OpacityBlendDistance
  = 0, so a = 1 beyond 0.1 mm: an opaque sea (the S8 shot shows no wreck through it). c4e92ccd NoCaustics_Waves (the
  S8 pool): `max(ramp(d / 4), ramp(d / 0.1) * 0.5 smoothstep(4096, 0, |P|^2)) * FadeOpacity`.
- Transmittance of the column = 1 - a: one grey scalar for R, G, B, a quadratic ramp in d. No exp(-sigma L), no
  wavelength. The water's colour comes from the lit surface instead: its diffuse albedo is
  `mix(mix(ColorShallow, ColorDeep, 1 - (1 - clamp(d / ColorBlendDistance))^ColorBlendExp), Color_Foam, foam)`,
  lit by the sun and local lights as a Burley surface at the wave normal (the in-scatter as a surface).
- Refraction: Refr = the pre-water colour copy (set 2 b4) at the pixel's own uv plus `N.xz * clamp(d / 0.5) *
  DistortWeightRefraction (0.1)` (a wave jitter of zero mean), applied only where the depth at the offset passes a
  test against the fragment's (no foreground pulled in), mirror-wrapped at the viewport edge. No Snell bending: the
  bed is seen where the VIEW ray meets it. An offset never leaves the screen (mirrored).
- Caustics (7b44a567 only): an 8 x 8 flipbook (set 1 b8) sampled at the bed point along the view ray, masked by
  CausticsMin/MaxDepth, ADDED to the surface colour (times CausticsStrength) and raised into a. No light transport.
- Every surface term, the probe reflection Ks included, is multiplied by a: where the water is clear the
  reflection fades (physically the interface reflects F(theta) at any depth), and the transmitted part is not
  weighted by 1 - F.

What is wrong against a ray-traced medium, in screen-impact order at S8: (1) no Snell displacement of the bed;
(2) transmittance over the camera-axis depth difference, not the refracted path length, and grey; (3) the
reflection scaled by a instead of a Fresnel split; (4) caustics as a texture; (5) screen-space limits (the bed
under a rim cannot be seen).

#### Quantified at S8 (offline, 2026-11-01)
Replay pack `replay-20261101-113740-1` (S8, the static view of m5w2 / m5w3: same image): its gDepth (R32F) and
camera rays give the bed; the water as flat planes at the logged heights (pool -10.97 m, sea -14.74 m from the
camera). Visible water (bed below the plane) 9.66% of the view (pool 2.05%, sea 7.61%), the m5w3 log's `in front
9.66%`. Refracted ray (n 1.326, flat normal) marched against the depth:

| | screen hit | hidden | off screen | Snell shift (median, p90) | path L_t / game d (median) |
|---|---|---|---|---|---|
| pool (transmissive) | 65.0% | 35.0% | 0.0% | 52 px, 55 px | 1.19 (p10 0.67, p90 1.60) |
| sea (a = 1, weight 0) | 60.2% | 37.8% | 2.0% | 19 px, 57 px | 0.91 |

Weighted by the pool's transmittance 1 - a (mean 0.16, median 0: the deep part is opaque; 41% of pool pixels have
1 - a > 0.05): screen hit 91.1%, hidden 8.9%, off screen 0%. Rays leaving the screen therefore change nothing at
S8 (0% of the transmissive water); the visible error is the missing Snell shift (the bed drawn ~52 px too far from
the camera). Hidden = the strip of bed under the near rim, which the camera cannot see directly. The game's ramp
evaluated at L_t instead of d would lower the pool's mean 1 - a on the hit pixels from 0.225 to 0.157.
Scripts: the session scratchpad (`refr.py`, `refr2.py`); not kept in the repo (one-off model, numbers above).

#### M5.5 design (smallest physically meaningful steps, each its own key, off by default)
1. Snell refraction through the game's medium (`rt_water_refraction`, BUILT, below). Only WHERE the bed is seen
   changes; the medium (1 - a) is the game's. Fits the compose exactly: C is linear in Refr with dC/dRefr = 1 - a.
2. Path-length medium (not built): record d and re-evaluate the family's own opacity ramp at the refracted path
   length L_t (c4e92ccd: a depends on d and three constants only; 7b44a567 also on foam and caustics, so only the
   ramp term is swapped). Identity: L_t = d at normal incidence under the camera axis. Gate: monotonic in L_t;
   brightness changes by design (report), never above the opaque surface term. Needs the runtime CB values (log
   the instance's OpacityBlendDistance / EdgeBlendDistance per draw first).
3. Fresnel split (not built): the reflection weighted by F(N.V) instead of a, the transmitted part by 1 - F. Needs
   the separation of a's "column opacity" from the surface terms; changes energy by design.
4. Hidden refracted hits (8.9% transmission-weighted at S8): tier (b) shading at the lighting (the
   `rt_water_hit_shade` plan of M5.3, refracted rays of the previous frame's records). Caustics stay out (P6: no
   focused transport from one ray).

#### M5.5 step 1 status (2026-11-01, branch worktree-agent-aa7c928fb45d26855 on M5.4b, offline, READY_FOR_LIVE)
- Built: `rt_water_refraction` (GUI `exp_water_refraction`, Experimental, needs `exp_water_reflections`; enabling
  needs a restart, off is live; alone or with the other water keys). Files: `common/water_compose.h` (104-B
  record, `record_rw`, `refract_swap`, `ior_from_f0`, `refract_dir`, kCsRr* words 96..119, kFlagRefr),
  `common/water_fs_patch.h` (`refr` id per FS: 7b44a567 %4332, 99c1fcef %4530, c4e92ccd %3668; words 24-25),
  `layer/water.cpp` (104-B buffer and patch when the key is on at creation), `core/em_policy.cpp` (the first water
  draw's set 2 b4, the colour copy), `core/water.cpp` (variant, binding 68, flags, `[water] refraction`),
  `shaders/rt/water_compose.hlsl` `?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1`.
- Record: the M5.4b record plus `w24 half2(Refr.r, Refr.g)`, `w25 half2(Refr.b, 1 - a)`. Compose: n from the FS's
  own F0 (`n = (1 + sqrt F0) / (1 - sqrt F0)`, 0.0196 -> 1.326, water's 1.333 within the game's rounding of
  Reflectance_Base: derived, not a constant of ours), `T = refract(-V, N, 1 / n)` with the FS's N, one ray into the
  scene TLAS from the FS's surface point, tier (a) as the reflection (projects, depth_same_surface, faces the ray),
  then the forward pass's own copy read bilinearly at the hit: `C' = max(C + (1 - a) (Copy(hit) - Refr), 0)`.
  Hidden, off screen, miss, aborted, N.V <= 0, a copy of another size: Refr kept (C' = C). The copy is the FS's own
  Refr source, so the units and whatever the copy already holds (exposure, any fog composited before the water)
  are the game's; nothing is added for the extra path under water (the medium is step 2).
- Energy rule: the medium is the FS's; Copy(hit) = Refr is the identity bit for bit, a = 1 never changes C (test
  4j). ON minus OFF = (1 - a) (Copy(hit) - Refr): a displaced sample of the same bed, logged as `ratio of the
  means` over composed records (no systematic brightening expected; reported, not gated: a geometry correction).
- Offline: water_compose_test 4j (the FS model: the swap equals the FS with Refr replaced for any Lc, identity bit
  for bit, opaque unchanged, dC/dRefr = 1 - a numerically, with the reflection swap together; n from F0 and back;
  Snell's law, unit length, plane of incidence, total internal reflection water to air; words and buffer sizes);
  the three refraction-patched dumps pass spirv-val and decompile to the two stores
  (`packHalf2x16(vec2(_3668.z, 1.0 - _425))`); the M5.4b patch word for word (FNV pinned: 5389606c.., 9e156a50..,
  092432cf..); plain, sun and LS compose SPIR-V byte-identical (SHA1 before / after); the REFR variant passes
  spirv-val and binds only pushed bindings (68 the copy). 75 CPU CTests pass (the GPU fixture not run: a live
  batch was on the PC).
- Log `[water] refraction`: passes with / without the colour copy, transmissive records (opaque, invalid, no usable
  copy), traced, tiers, transmission-weighted hidden share, screen shift px, (1 - a) Refr, game -> traced per
  composed record and their ratio, mean delta % of mean C, clamped, GPU. rt_debug 28: green composed (brightness
  1 - a), red hidden, blue off screen, magenta miss, white aborted, dark cyan not traced, dark grey opaque, orange no
  usable copy, yellow not owned / invalid.
- Cost estimate: one ray per transmissive record (S8: ~56k = the pool, 2.05% of 2.73 Mpx) plus 4 copy loads; M5.2
  traces 264k reflection rays in 0.12 ms with the primary pass: expected compose GPU ON minus OFF < 0.05 ms. The
  record grows 96 -> 104 B (the record buffer 480 -> 520 MB at the default `rt_water_compose_mb=160`).
- Live: job `m5w6-water-refraction-s8-fg2` (S8, 132 s, OFF/ON/OFF, rt_debug 28; tools/live_batches/m5_water, dry
  run passes, batch 21.1 min, 1.31 GB). User check: Water Queens House, the pool from the balcony: with the key on
  the tiles and steps under the water move towards the camera (the pool looks shallower) and follow the waves.

#### M5.4c status: the game's sun shadow replaced on the FS that sample it (2026-11-01, branch m5-water-game-shadow, offline, READY_FOR_LIVE)
- PL_noCaustics_Shadows 06e66a6f (S3 camp pond) and huge_PL_Shadow ced49ee3 multiply the sun radiance by the game's
  cascade visibility Vg (PCF of 7 Dref samples): every step to the colour is a product, so D = Vg D_u exactly.
  `tools/water_audit.py game_sun_shadow` derives Vg and refuses any other use of the Dref samples; both FS mapped.
- Key `rt_water_game_shadow=0` (restart to enable, needs rt_water_shadows). The record gains 8 B: Dg = D_u - D and Vg.
  Compose `;WATER_GSH=1` on each WATER_SHADOWS variant: traced lit -> C + Dg, occluded -> R (M5.4), unknown -> C.
  Without the key those FS store no sun term: the game shadow is kept, never applied twice. Log `[water] game shadow`.
- Ultra (rt_cascade_casters=1): water now traces the cascade requests (`RT_CASCADE_MASKS=1`, the sun ray
  `RT_CASCADE_SUN_HIT=1`: the roof casters shadow the water as they shadow the ground); before, every water pass was
  skipped on Ultra. The 3 GSH requests exist with and without the cascade defines (6 new in variants.txt).
- Offline: water_compose_test 4k (V = Vg gives C within rounding, Vg = 1 bit-identical, V = 0 gives R), water_audit_test
  (spirv-val of the gsh forms of all 11 FS). The 6 new requests compile; `tools/shader_identity.py --baseline main
  --filter water`: 11/11 existing water requests byte-identical. 88/88 CTests.
- Live: job `m5p3-camp-fg2` (Campsite - 27h, batch m5_promotion). User check: camp pond under the log bridge, Ultra:
  the bridge and trees shadow the water once (no double darkening), and the lit water keeps its sun glints.
- Not done: Debris (probe mask _774: an `amb` id would cover it).

## Promotion to Ultra (2026-11-01, batch tools/live_batches/m5_promotion, NOT run)

Gate table from the latest valid S8 runs (each key alone, static view; `py -3 -m tools.m5_promotion_gate <log>`).
Energy is the post-water image (RR colour input) over the exposure, against the compose's own accounting:

| key (log) | participation, every window | image energy: measured / predicted (tol.) | GPU ms | clamped |
|---|---|---|---|---|
| geometry (all) | 2 planes, 240 draws/window, overflow 0, failed 0 | - | build 0.28 | - |
| reflections (m5w2 140754) | in front 9.67%; rays screen 14.8%, hidden 5.9%, off 1.6%, miss 77.7% | +0.44% / 0 (0.50%): matches | 0.119 | - |
| compose (m5w3 121606) | 112-120 of 120 composed; 12.7% of owned; ratio bins 38/53/9%, means 0.31 | -2.19% / -0.07% (0.55%): DARKER | 0.096 | 0 |
| shadows, R = C - D (m5w4 141148) | occluded 62.7%; removed 46.3-46.7% of water C | -9.10% / -9.39% (0.83%): matches | 0.113 | 0 every window (was 145573) |
| ls_shadows (m5w5 122327) | 0.20% of owned; sum check 100.00% within 0.098%, ratio 0.99998-1.00001 | -2.56% / 0.00% (0.34%): DARKER | 0.181 | 0..46 per 120 frames |
| refraction (m5w6 131651) | transmissive 51.7%, screen hit 69.6%, shift 24.1 px, ratio 1.013 | +0.85% / +0.02% (3.08%): matches | 0.240 | 0 |

- Energy observable corrected. The m4 gate's 5c3fdabc is the lighting before the forward water pass: water reaches it
  only through the eye adaptation (m5w4: exposure +2.74%, "+2.20% brighter" while the shadows removed 46% of the
  water). The RR colour input after the water, divided by the probe's b0[0], shows -9.10% against the predicted -9.39%.
- Glint noise. One frame per state is not enough: the water's sun glints (up to ~1000 against an image mean of 0.18)
  carry about 100% of every frame-to-frame difference (about 1,900 preview pixels, OFF-b vs OFF-a included). The
  two DARKER rows (compose, ls; one frame each, the darkening already in the water pass before the compose) are
  within that noise (pooled OFF pairs: single-frame SD about 0.55%, up to 1.7% apart); unresolved until the batch's
  3+3 OFF and 3 ON frames with the exposure held.
- Missing before Ultra: the whole chain in one run; a second water scene (S1 docks no longer loads: Flymm Cargos
  Basement - 101h 05m, family unknown); a moving window (the compose always runs on the previous lighting: 114-120
  of 120 passes); multi-frame energy with the exposure held; frame time (every run was unfocused, 30 fps cap).
- Preset commit (separate, merge only if the batch passes): the six knobs leave Experimental (Advanced, before the
  Experimental section), on in Ultra only; the four restart keys also need sharc_device=1 (shaderInt64). Record
  buffer 520 MB at the default `rt_water_compose_mb=160` (104-B records).

## Open questions

- Does the water pipeline write depth? Answered offline (M5.1 status): no; the gate compares t_w with
  the bed depth. The live `depth write` counts confirm it per pipeline.
- S1 QuickSave_3784 is labelled the foundry (TEST_SCENES) but COVERAGE measured the sea there.
  The `mm2` expect line requires supported water draws, so a view without water is INVALID.
- Previous-frame water reuse needs a deformation/flow motion contract; M5.1 measures static only.

## Water hidden-hit reflection extension (branch rt-reflections-offscreen, 2026-11-03)

Design and full source map: [REFLECTIONS_PLAN.md](REFLECTIONS_PLAN.md), stage A. Candidate
`rt_water_hit_shade=0` uses native material shading for supported tier 2/3 hits in both the
geometric diagnostic and the forward-record composition trace. Tier 1 remains the screen
source. Unknown rows/lighting and sky misses retain the native FS probe. Preserve the geometric
tier histogram and report shaded/failure counts in existing device-local water statistics.

The image equation remains `C' = max(C + A L - B, 0)`, with A/B and the normal from the actual
water FS. This prevents double-counting; it does not qualify the inherited RF radiance bias.
Out-of-frustum light-grid queries remain unsupported, so a later world-light/probe provider is
required for full off-screen reflections. Directional game-environment sampling is a separate
stage, guarded against cut roofs and unknown geometry.

Qualification: ordinary key-off water SPIR-V identity; inspect replay inputs before claiming a
water replay result; one unqueued S8 port still + turn OFF/ON/OFF job, predicted.json first.
Use floating post-water colour/exposure and compose-delta accounting, never the pre-water dump
or bloom PNG mean. Incremental GPU cost is unmeasured; the existing water chain is 0.53-0.67 ms.
Off in every preset pending gates; Ultra only for first promotion. No live test was run here.

## wf9 compose camera correction (2026-11-03, offline candidate)

wf8 S8 turning lost tier 1 (25% static to 0.4-1.5% of covered water) while compose used the
previous lighting TLAS in about 95% of passes. `rt_water_compose_shift=0` remains the default;
SpecId 1015 selects the correction with no request or ABI change. The CPU signals only earlier
stash/stash-kept selections, not newer scenes. water_copy preserves outgoing CameraCB float4 13
in cCam bytes 400-415 before overwrite, with validity in w; first copy cannot read undefined data.
Compose binds the existing b64 camera buffer and shifts reflection, sun, local-light and refraction
origins by current minus build camera. Ray distances are translation invariant; screen hit reconstruction,
projection, light vectors and screen radiance stay in current camera space. The hidden-hit
shader receives the translated TLAS-space hit; its descriptor guard still requires frame/TLAS
identity. Scene shift uses flag 32, hidden-hit eligibility uses flag 64. No history or energy blend.
The Experimental GUI exposes Water camera correction, off in all presets. See the wf9 progress
fragment and tools/live_batches/water_flicker_wf9 for offline checks and the unqueued S8 prediction:
hidden share about 7-10%, covered-water tier 1 near 25%, static float energy within repeat noise.
GPU delta remains unmeasured (wf8 compose 0.10-0.15 ms). This is READY_FOR_LIVE, not a quality pass.
