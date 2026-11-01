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
  (id tables: 7b44a567 huge_PixelLighting, and for S8 99c1fcef ExtraWaves and c4e92ccd NoCaustics_Waves, whose
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

## Open questions

- Does the water pipeline write depth? Answered offline (M5.1 status): no; the gate compares t_w with
  the bed depth. The live `depth write` counts confirm it per pipeline.
- S1 QuickSave_3784 is labelled the foundry (TEST_SCENES) but COVERAGE measured the sea there.
  The `mm2` expect line requires supported water draws, so a view without water is INVALID.
- Previous-frame water reuse needs a deformation/flow motion contract; M5.1 measures static only.
