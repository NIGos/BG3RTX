# Track D: hardware ray tracing

## Procedural native Hair geometry - 2026-10-30, default-off candidate

`rt_native_hair` now uses dynamic AABB BLAS geometry with the original 88-byte
native triangle sidecar. Explicit primary/origin queries solve native fragment
depth directly; physical secondaries use native Shadow base-card/ray-view hard
coverage. A frozen main-camera pseudo-volume is not imposed on GI/reflections.
GPU bounds failure at scene-stamp byte 12 returns UNKNOWN across all four query
entry points, never false lit/missing geometry. Proven converter-inactive cards
remain inactive; unresolved FP32 edges are UNKNOWN. Refits preserve geometry type,
and held bounds are certified again for each scene. Existing sampler policy stays.
22/22 CTests, 310,883 shared geometry checks, 19,734 capture/strand checks and
95 ordinary/capable shader identity/validation gates pass. No live or GPU gate yet;
no all-family Hair closure claimed. Full source/model details: docs/NATIVE_HAIR.md.

## Native CHAR_Hair coverage - 2026-10-30, default off

Actual native strand coverage is implemented in isolated `codex/native-coverage`:
startup-only `rt_native_hair=0/1`, validated88-byte output capture, six depth/shadow
hash mappings, dual masks/noise, material root/density, strand-frame view coverage,
original camera phase/fade dither and native shadow hard branch. Record208 unchanged.
21/21 CTests, 19,716 Hair checks, all15nativeVS patch/validate, 95 ordinary SPIR-V
byte-identical and 95 capable compile/validate. No live deployment/GPU work.
Full Hair is OPEN: base-card triangles do not reproduce native fragment depth.
Exact derivation, data layout, supported source hashes and procedural plan:
docs/NATIVE_HAIR.md. Do not claim a complete face-shadow or origin-match gate.

## Native scalp hard-cut runtime candidate - 2026-10-30, default off

`rt_native_scalp=0/1` now selects a bounded runtime increment in isolated
`codex/native-coverage`. It does not implement full hair or camera raster dither.
Six verified scalp depth/shadow hashes use AlphaRec kind 3; p0.xy at bytes 48/52
hold the actual per-draw material edge/root scalars. The 208-byte record and host
ABI 18 remain unchanged. Static/instanced descriptors follow the table below;
existing UV capture must match native FS location 0 or the draw remains unsupported.

Camera depth additionally reads native Frame phase/mode at offsets 8/12. Mode 0
has native hard coverage; any nonzero mode is explicitly unsupported and retains
the baseline opaque path. Missing/nonfinite state and undefined smoothstep also
remain unsupported. Native shadow and tetrahedron programs have no frame dither.
Logs `[alpha scalp]` distinguish committed hard records, unsupported camera dither
and invalid/missing state. Existing view/region failures retain existing counters.

The capable shared alpha_eval samples B/A together, applies native smoothstep
and root multiplication, then the native hard cutoff 0.3333. It bypasses the
generic stochastic alpha threshold, even at coarse mips. All four normal query
entry points and origin queries share this dispatch. Its declared sampling policy
is existing isotropic RT-cone LOD with linear repeat/trilinear, max sampler LOD 16;
this is NOT native raster derivative/anisotropic or floating-point equivalence.
Shader code and native GLSL formula correspondence alone are not a GPU image gate.
The special cascade soup still lacks per-material UV records and is unchanged.

Key transitions: before new kind3 records can be created, the host atomically
publishes a sticky process capability through existing persist storage. Core
selects capable variants AFTER obtaining the actual TLAS/set pair. Acquire/release
ordering and the existing scene handoff prevent an ordinary shader consuming a
new record. The flag survives core reload and never clears on key-off: held and
retired records retain their original formula until retirement. Newly captured
key-off draws follow baseline paths. Fresh process key-off uses ordinary shaders.
Failed capable compilation leaves a compatible old variant or NULL, never an
ordinary pipeline interpreting kind3 as raw alpha. No new ABI storage fields.

Exact camera raster identity is not currently representable end-to-end:
AlphaRec has space for material/frame plus view-projection/viewport metadata, and
alpha_eval already knows triangle positions/UVs. But current descriptors retain
image view/layout, NOT native VkSampler state; Bg3Binding also omits sampler.
Native viewport/projection and original raster sample coordinates are not copied
into each alpha record. Secondary rays need an explicit RT footprint policy
rather than claiming original raster derivatives. These remain separate work.
Existing F10 files contain images and descriptor identities but no captured native
Frame scalar bytes; the close-up's actual mode is UNKNOWN. It may be unsupported
by this candidate. No live test, quality improvement or GPU cost is claimed.

Offline gate: full isolated build and 17/17 CTests pass. Native test passes
178,780 checks including 128 publication/scene-handoff/new-core cases. DXC compiles
all 95 ordinary entries byte-identically to 37299f2 and all 95 capable wrappers;
all capable modules pass spirv-val for Vulkan 1.3. Shader disassembly retains
native SmoothStep/Fma for the hard scalar transform. Reproduce with
`tests/native_scalp_shader_test.py --dxc <dxc.exe> --sharc <SDK include>` (two CPU
workers, no device). Evidence: docs/evidence/native-scalp-hard-20261030.json.
Main e277548 was then merged into this isolated branch, preserving both scalp and
OMM CTest targets. Full isolated build and 20/20 CTests pass; the merge changes no
layer/core/shader code, so the shader identity proof remains applicable.

## Native scalp coverage contract - 2026-10-30, offline increment

Branch `codex/native-coverage`, based on 857a616, is isolated from the user's game.
At 37299f2, `common/native_scalp_coverage.h` was a reusable scalar evaluator and
portable bounded capture adapter with no production caller. The later runtime
candidate above reuses its capture adapter for the representable hard branch.

Native references are verified Materials.pak SPIR-V, recovered using
`tools.assets.spirv.modules` and the layer's FNV hash. Complete inventory and native
GLSL live in the main capture report `captures/dialogue-shadow-review-20261030/`.
All 29 Hair/Scalp fragment programs are available; none needs an unknown-shader
fallback. This increment implements the scalar contract of these six scalp FS:

| Pass | Static FS | Instanced FS | Material / mask binding |
|---|---|---|---|
| Depth | 9e77575a9f377ec5 | 36eeb96ba83ea027 | 1/2 static, 0/1 instanced |
| DepthShadow | 03a2d9f645625c44 | 4096aa06e414de92 | 1/2 static, 0/1 instanced |
| DepthShadowTetrahedron | b1b923f0662a55b5 | 3535fd5f461f7d23 | 1/2 static, 0/1 instanced |

All material and texture descriptors are set 1, UV location 0, sampler set 0
binding 7. Material offsets 0 and 4 hold smoothstep edge and root multiplier.
For already native-filtered mask B/A, coverage is
`smoothstep(edge, clamp(edge + float(0.7), 0, 1), A) * fma(B - 1, root, 1)`.
Shadow passes retain coverage >= float(0.3333). Camera depth also reads Frame set 0
binding 0 offsets 8/12. Mode zero uses that same cut; nonzero uses the original
screen/frame dither expression and its TWO subtractions/comparison, including
native truncation remainder for negative phase. It does not substitute pc.frame
or an RT stochastic hash. Existing raw-A scalp shadow entries are not equivalent.

The adapter reads only 8 material bytes and, for camera depth, 8 Frame bytes.
It resets output before every attempt and publishes nothing after a failed read.
Missing frame/raster coordinates, nonfinite data and undefined smoothstep return
an incomplete status. CompleteScalar means evaluation of supplied scalar inputs,
not GPU sampling, floating-point or rasterization equivalence certification.

Required runtime integration remains concrete:
- Capture native sampler identity/state, image view/content/residency and the
  native filtered mask footprint; current synthetic wrap/cone sampling is an
  approximation and cannot establish exact coverage alone.
- Capture original per-draw viewport/projection and native Frame phase for camera
  dither. An arbitrary secondary-ray output pixel is not gl_FragCoord. Preserve
  this state across held draws with matching geometry and descriptor lifetime.
- Extend record dispatch behind a default-off key, using the unified alpha_eval
  path for visibility, closest, debug and origin queries. Cascade geometry's
  special-case query path must retain its own per-triangle material/UV records;
  the current cascade branch bypasses the normal alpha evaluator.
- Hair is a DIFFERENT recipe, requiring dual samples/noise, root parameters,
  tangent/view/front-face inputs, fades/dither and gl_FragDepth displacement. The
  current position+UV soup cannot encode all of that. Scalp never writes depth.
  Do not apply this scalp evaluator to Hair or treat a fragment depth offset as
  ordinary vertex displacement.

Standalone MSVC C++20 /W4 /WX /fp:strict test: 178,522 checks, including 43,191
higher precision shadow-reference and 2,826 dither-reference decisions, derived
rounding envelopes, real raw-alpha counterexamples and binding/failure tests.
CTest target registered. No GPU jobs; GPU cost and in-game appearance unmeasured.

## Base frame CPU cost - 2026-10-30, default-off candidate

`rt_alpha_stream=0/1` selects ordinary copies or aligned streaming loads for alpha
material constants from mapped game uploads. The split instance path already uses
streaming loads. Prefixes/tails stay inside the requested range; shader and ABI stay
unchanged. Capture once per pass reads the key, avoiding per-draw config overhead.

Static inn, Ultra 3840x1600, FG 0: capture CPU 4.224 -> 2.973 ms/frame, of which
pre-draw 2.989 -> 1.730. Build recording (included) ~0.97 and submit copies ~0.48 ms.
GPU chain / scene / RR: 2.54 / 1.67 / 2.36 -> 2.55 / 1.63 / 2.36 ms. Primary match
98.969 -> 98.968% median, overlapping ranges; exact-copy alignment/boundary CTest
passes. No final-image energy comparison. No VK errors, driver events or dumps.

NOT QUALIFIED for promotion: all stats-off A/B windows stay at a 30 fps ceiling.
WDDM GPU busy ~10.6 ms vs 33.3 ms presentation; no uncapped CPU/GPU bound claim.
Keep default 0 and branch unmerged until an authorized foreground speed gate passes.
Full rt_stats parity windows raise RT GPU cost from 1.03 to ~19.4 ms: never include
them in speed gates. Check: inn save, static view, toggle rt_alpha_stream live, compare
[cpu] pre-draw/capture and rendered fps with all stats off. See PROGRESS_CODEX and
docs/evidence/perf-base-20261030.json; job sup-perf-game-uncapped.

## Live FG-on primary gate — 2026-10-29, NOT passed

Static parity PASS at primary 99.5195-99.5215%, 18.7-18.8 fps and actual FG 2.00.
Motion matrix deliberately stopped after 13 complete phases plus partial phase 13:
posted 60 px/s input produces stationary periods and up to 98.58 deg/frame jumps;
baseline generated-presentation ratio falls to 1.00. It is INVALID qualification.
First negative FG 2 rotation primary median baseline/rebase/destroy-only:
21.299 / 85.984 / 20.5465%, versus static floor 99.501%. All sampled RT 120/120,
none 0. No visible-flash certification; different paths/views confound attribution.
Both keys remain off; no merge. See docs/evidence/p1-fg-live-20261029.json.

Translation rebasing alone is insufficient. Residual pool/soup mismatch and
scene/lighting pose separation support investigating older instance/dynamic
geometry or camera-buffer epochs, after qualifying no-focus relative rotation.
They do not prove a native matrix frame index or a Reflex cause. No new selector.

Cost evidence: in the same FG-2 static debug phase, disabling full per-ray stats
raises 15.5 to 19-20 fps, GI ~6.8 -> 0.379 ms and LS ~5.25 -> 0.530 ms. Source
p1-fg-static-effects.1.145628.log, 15:02:53-15:03:18. RT 2.872 ms is a mixed
300-frame interval. Historical 15.040 vs 4.269 ms remain confounded by settings,
scene/presentation; RR on in both. No isolated rebase performance regression.
Lightweight primary/epoch producer now counts with full stats off, with no
lighting change: 21 ordinary SPIR-V byte identities, 20 variants validate.


## P1 deciding configuration is FG ON — 2026-10-29

Latest steering supersedes the FG-off-only gate below: the visible symptom is
reported only with FG on. Paired fg=0/2 jobs use real foreground presentation;
FG 2 decides, FG 0 is reference. No runtime matrix has run. rt_scene_rebase and
fg_current_destroy_only remain default-off; no merge or renderer promotion.

The static job compares one GPU-verified view in both configurations: sun,
local shadows, GI, reflections and RR per-pass counters plus three repeats of
each debug view. Noise/preview/jitter-derived differences block the motion job.
The motion matrix tests rotation at three speeds/both directions and slow/fast
pans, with baseline/rebase-only/destroy-only in both FG configurations. Actual
generation, active effects/RT, GPU motion and >=18 rendered fps are required
with FG on; candidate primary match must reach the FG-on static level ~99.2%.
The destroy candidate additionally needs fewer interior FG toggles. See
tests/fixtures/p1_fg_{static,motion}_job.json and PROGRESS_CODEX.md.

The diagnostics record primary/FG-toggle association and sampled lighting pose.
Code audit finds app g_frame incremented once; generated presents go directly
to the next layer and update displayed counters. ReflexSleep/new token runs
after app present, including the initialized FG-off reference. This does not
prove causation or native instance-row epoch. No speculative Reflex change.
Seven CTests and 19 new matrix/static/fixture tests pass; no HLSL/ABI changes.

## P1 rotation qualification and epoch diagnostics — 2026-10-29, pending

Rotation is now the main primary-match gate, approximately static 99.2% with
active RT. Default-off `rt_scene_rebase=0` remains unchanged. New default-off
`rt_primary_epoch=0` selects a diagnostic primary variant: it uses the same
single closest ray, reports nearest-hit pool versus dynamic-soup cohorts, and
keeps misses in the global denominator. Ordinary shaders remain byte-identical.
GPU scene snapshot is enabled for this diagnostic even with rebasing off.

rtStats 136..143 hold cohort counts; 144..150 store source/lighting translation,
rotation separation and lighting forward; 151 validates the stamped snapshot,
152 marks diagnostic presence, and 155..157 hold native absolute camera origin.
CPU aggregation measures actual angular speed/signed yaw and static translation
with FP32 precision bounds. It does not infer motion from requested inputs.
These source-pose observations do not establish the frame of every native
instance matrix. If pool mismatch persists after origin correction, compare
actual same-draw GPU native rows to TLAS rows before asserting an older epoch.

Prepared foreground real-present FG-off QuickSave_3784 jobs: four static
90-second qualification windows, then sixteen 90-second static/rotation
off/on windows at 60/120/240 px/s, both directions. Strict USER_AWAY required.
No runtime result yet. Failed qualification blocks the A/B on the same build.
Expected reports: captures/p1-pan-flashes/rotation-{preflight,gate}-result.json.
Missing RT, ignored input, insufficient cadence or early exit invalidate it.

Old exit at 242 s was not the 750 s deadline or cleanup. Cause remains unknown.
FG off skipped VEH installation: fixed, with optional crash_dump and retained
process status. No crash fix claim before runtime qualification.
Old RT interval 15.592/15.040 ms off/on includes primary/GI/AO/sun; historical
4.269 ms had different scene/settings and missing RT. Residual remains flagged;
preflight isolates diagnostic overhead and static rebase delta in one run.
Six CTests, 14 Python gates, 18 input/policy checks, 21 ordinary shader identity
checks and 20 variant validations pass. No promotion or merge.

## P1 lighting-to-scene translation — 2026-10-29, gate incomplete

314840d: one real-present foreground FG-off job exited before slow/fast windows.
Late static primary match off 99.164-99.177%, on 99.157-99.177%, RT active and no
missing scene in interior windows. Median cadence 17.60 / 17.50 fps. Initial
static camera drift and early exit invalidate the quality gate. Moving result
is unknown. No merge; key stays off. On RT-query cost 15.040 ms; off 15.592 ms
includes camera drift and cannot establish a cost delta. Evidence and next epoch
hypothesis: docs/evidence/p1-origin-rebase-314840d.json. No further job this turn.

Key: `rt_scene_rebase=0` by default; selector `rt_scene_camera_match=0` stays off.
All four query producers in rt_common translate ray origins by `C_lighting -
C_scene`. Their directions and t values are invariant. Closest-hit object
transforms have that delta removed before returning to lighting-space callers.
Alpha candidates remain entirely in scene space, including ray cone footprints.
The frozen local-light reference applies the same infrastructure translation.

The instance analyzer proves native `W=M_instance*object` reaches clip space only
through the camera-relative `_m2`. Dynamic XFB inverts that same `_m2`; the existing
mesh extraction and audit compare `M*object` with converted XFB. Both paths thus
use the source G-buffer camera origin and world axes, not a camera-view rotation.
This establishes the coordinate convention, not the epoch of the native rows.
Native instance rows are still read at submit. GPU camera snapshots are read from
the actual conversion UBO in the build command buffer, into offset 256 of that
scene slot's expanded stamp buffer. No CPU pose is used to compute the shader
translation. A matching pose generation is required when rebasing is active.

The layouts reuse native camera binding 48 (RT) or 29 (D-LS/D-RF), preserving the
32-descriptor reflection limit. Small wrappers compile the experiment separately.
21 ordinary SPIR-V binaries are byte-identical; 19 variants validate. Runtime
cost, moving primary-match gate and in-game instructions follow the single A/B.


## P1 transient flashes — 2026-10-29, experimental

`pan_capture` is a positive request serial, default 0. Each new serial records
16 consecutive lighting frames. Named raw images retain recording and submit
indices. Readbacks remain in memory until the burst ends; no disk/PNG work or
queue idle wait occurs between those frames. ABI 15 exposes the core's private
images to this bounded host readback path. Both DLLs must match.

`pan_primary.hlsl` writes a separate primary-hit residual normalized by the
existing geometric depth bound. Its header preserves exact GPU camera float
bits in FP16 byte lanes. The ordinary sun shader and final image remain intact.
`pan_local.hlsl` exports ls_debug=3 visibility without replacing the diffuse.
It requires the non-k full path for valid masks. All normal variants are unchanged.
The analyzer must reject missing channels, gaps and invalid transport. Pose
offset remains unproven until scene-camera evidence is connected; a one-present
recording age must never be treated as a demonstrated lag. No rendering fix
is promoted. Runtime tests and contact sheets are pending.


**Phase 2 (2026-10-26): ray-traced sun shadows and AO run on the game's own Vulkan
device**, against acceleration structures rebuilt or refit every frame from the
captured G-buffer geometry. Off by default (`rt=0`). Results, costs and next steps
are in [section 6](#6-phase-2-results-acceleration-structures-and-ray-queries-on-the-games-device).
Sections 1-5 are phase 1 (the capture) and the original phase-2 plan. Section 7 is
phase 3 (alpha test, hybrid GI). Section 8 is the effect catalogue and roadmap for
further RT effects (see also [QUALITY.md](QUALITY.md)).

Goal of phase 1: prove that the frame's opaque scene geometry can be captured
every frame in camera-relative world space, as the input for BLAS/TLAS builds and
ray queries. **Result: it can, exactly.** All 835 draws (3,163,516 triangles) of
the G-buffer pass are captured; re-projected, they reproduce the frame's depth
buffer on 97.8% of pixels to 0.2%. 0% is missing, and the remaining 2.2% is
alpha-tested geometry that our triangles cover where the game discarded pixels.
Capture costs +0.45 ms GPU per frame. A BLAS refit of the same geometry costs 0.55 ms
and a full rebuild 2.5–3.4 ms on the RTX 5090. Validation-clean.

Captures (Steel Watch Foundry, the auto-run save), each with a `geo/` folder:
`captures/20261026-035645`, `-040924`, `-041511` (under validation) and `-041646`
(final design).

## 1. Survey: how the G-buffer vertex shaders form positions

Source: the decompiled shaders of `captures/20261025-235042` (`shaders/*.glsl`) plus
the draw census of its `frame.json`. The G-buffer pass (5 colour targets + D24S8) has
**835 draws, 36 distinct vertex shaders and 53 VS/FS pairs**, all `vkCmdDrawIndexed`
(no indirect draws). Every one of the 36 ends in the same two lines:

```glsl
precise vec4 W4  = vec4(P_object, 1.0) * M_instance;     // or W = W4.xyz + displacement
precise vec4 C   = vec4(W, 1.0) * cam._m2;                // set 0 / binding 2, ViewProj, row-major
C.y = -C.y;  gl_Position = C;                             // (sky: C.z = 0 as well)
```

Only the source of `M_instance` and the displacement added to `W` differ:

| Class | Position path | VS (draws in the capture) |
|---|---|---|
| A. Instanced static | `mat4(attr4, attr5, attr6, (0,0,0,1))`: the instance matrix rows are vertex attributes (per-instance stream) | 2d2ac3c3 (360), a618ddae (105), 6044c8cf (82), 54df894a (23), 2aff0bdc, c32ce535, 6660248, f474891e, 5575fc19, 646a930e; 6daa7974 = sky dome (forces z = 0) — 11 variants, ~600 draws |
| B. Per-draw UBO matrix, optionally GPU-skinned | `M = set1/b0._m0`; if `_m2 > 0` (or always) the position is read from SSBO `set0/b13` at `(gl_VertexIndex + base - 1) * 20` bytes, the output of the skinning compute pass (`cs 718d8329`) | 3812bc98 (100), f111e292, b1e18895, 1ed985dd, 939d1385 (switchable); 1cf36b97, e9407368, 41a168e2, 61239daf, 17970d7c, 13a9c9f5, 3927925b (always skinned) — 12 variants |
| C. Matrix assembled in the shader | from UBO/attribute data (`_44`, `_75`, `_76`, `_118`) | 5c258c7b, 1e9145e4, 0de1b186 (24), 5b0f1fc1 — 4 variants |
| D. Instanced + vertex animation | class A matrix, then `W += wind / cloth displacement` (noise, `textureLod`, per-material UBO params) | 50fbfceb, 735d5a19, 105e47e4, 80b73bae, 4121c54e, 8e93bb6b, e5dd82b6, 27ef2b49 — 8 variants (foliage, banners, chains) |
| E. Terrain | patch offset from `set?._m0[gl_InstanceIndex].xz`, height from a heightmap `textureLod`, then `* _m0` | 64e25689 (3 draws, 45k triangles) |

So positions come from four different places: vertex attributes, a per-draw UBO, a
skinning SSBO, and textures. Displacement is added after the instance transform. But the
last step is identical everywhere, and the space just before `* _m2` is the one we want.

### The camera UBO (set 0, binding 2, 224 bytes)

Measured in the capture (`report.json` → `camera_ubo.floats`):

| Offset | Member | Content |
|---|---|---|
| 0 | `_m0` mat4 | **View** (absolute world → view; rotation plus translation) |
| 64 | `_m1` mat4 | **Projection**: reversed infinite Z, near 0.1 at `[3][2]`, DLSS jitter in `[2][0..1]` |
| 128 | `_m2` mat4 | **ViewProj of camera-relative world**: its translation row is `(0, 0, 0.1, 0)`, so the camera sits at W = 0 |
| 192 / 196 | `_m3`, `_m4` | near 0.1, far 1500 |
| 208 | `_m6` vec3 | camera position in absolute world (e.g. −320.9, 19.9, −132.5) |

`W` is therefore **camera-relative world space**: world axes (Y up), origin at the eye.
Absolute world = `W + _m6`.

**Correction to FRAME_MAP:** the G-buffer normals (RT0) are *view space*, not
world-aligned. With the view rotation of `_m0` the captured triangle normals agree
with them at |dot| median 0.998 (94% of samples > 0.9). Without it the median is 0.77.
TRACK_C_GI.md (`set1/b1`, "view space") was right.

## 2. Mechanism: transform feedback of `gl_Position`

| Option | Verdict |
|---|---|
| (a) SPIR-V patch: store the pre-projection position to an SSBO | Works, but every variant needs its `W` located (it is a different SSA value in each of the 36 shaders, and classes C and D compute it inline). It also needs `vertexPipelineStoresAndAtomics`, a per-draw base offset (a push constant or a pipeline layout change), and index buffers to rebuild triangles from per-vertex data. |
| **(b) VK_EXT_transform_feedback** | **Chosen.** The patch is three decorations and one execution mode on the `Position` output, the same for every shader. XFB outputs whole primitives (indexed lists and strips are unrolled, instancing expanded), so it delivers a triangle soup with no index buffers. It covers skinning, wind, terrain and anything else by construction. |
| (c) CPU: read vertex and index buffers, rebuild transforms | Would have to reimplement GPU skinning, wind and terrain displacement, and read instance streams per class. Fragile, and needs readbacks. |

The cost of (b): XFB captures clip space, not `W`. Recovering `W` is exact, though.
`clip = [W, 1] · VP`, so the x, y and w rows of `VP` give a 3×3 linear system for `W`
(solved in double). It does not use z, so it is immune to the sky's `z = 0` trick and
to any depth bias. The residual on z then flags such vertices (3,072 sky vertices here).

### Implementation (`layer/geocapture.cpp`, `layer/spirv_xfb.h`)

- **Device.** With `geo_capture=1` (read at `vkCreateDevice`, so restart the game), the
  layer appends `VK_EXT_transform_feedback` and a `VkPhysicalDeviceTransformFeedbackFeaturesEXT`
  to a *copy* of the game's create info. The game's structs are not touched. If the
  driver refuses, the layer creates the device unmodified.
- **Shaders.** Every vertex shader module gets `OpCapability TransformFeedback`,
  `OpExecutionMode Xfb`, and `XfbBuffer 0 / XfbStride 16 / Offset 0` on its Position
  output (a variable or a `gl_PerVertex` member). Modules with GS/tess entry points or existing XFB
  layouts are skipped. Hashes, dumps and pass signatures use the game's original
  SPIR-V. `tools/xfb_patch_test.exe` patches the whole shader dump offline: **2385 of
  2385 vertex shaders patch, and all pass `spirv-val`**. A pipeline is only captured
  if its VS module was patched, the VS is its last pre-raster stage, and its topology is triangles.
- **Capture** (the traced frame's G-buffer pass, found by its signature): every draw
  is self-contained.
  - Before the draw: `vkCmdBindTransformFeedbackBuffersEXT` at a CPU-known offset
    (running sum of `indexCount/3 × instanceCount`; an upper bound for strips, an
    8 MB slab for indirect draws), then `vkCmdBeginTransformFeedbackEXT` with no
    counters, then a `VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT` query.
  - After the draw: end the query, then `vkCmdEndTransformFeedbackEXT`.
  - The query's primitives-written count gives the actual size. Written vs. needed
    detects overflow (bound size too small).
  - The XFB buffer is 768 MB of host-cached memory.
  - Two designs failed first:
    - Resuming XFB through counter buffers garbled the result: 131k of 3.16M triangles,
      wrong ranges. The counter written by End is not visible to the next Begin without
      a barrier, which a render pass cannot hold.
    - Keeping XFB active across the whole pass worked (identical coverage), but the game
      binds pipelines between draws. That is illegal while XFB is active
      (`VUID-vkCmdBindPipeline-None-02323`, found by the validation run).
- **After the pass** (outside it, in the game's command buffer):
  - Copy the depth (D24S8, depth aspect) and RT0 to host buffers.
  - Copy the camera UBO. Its buffer has only `UNIFORM_BUFFER` usage, so `vkCmdCopyBuffer`
    cannot be used. A 14-thread compute shader (`shaders/geo_ubo_copy.hlsl`, push
    descriptors) copies it. The game's compute bindings are then restored with `restore_bindings`.
- **Analysis** (present thread, once, ~0.9 s):
  - Solve `W`. Vertices that are non-finite in clip space are counted and kept out: the
    game parks culled vertices at NaN (73k vertices).
  - Rasterise the clip-space soup on the CPU (16 threads, reversed Z, w-clipping) and
    compare it per pixel with the game's depth.
  - Compare triangle normals with the G-buffer normals.
- **Outputs** in `captures/<ts>/geo/`:
  - `scene.bin`: header, camera UBO, viewport, draw table (range, VS/FS hash, pipeline,
    index count, instances), clip and world positions. Format in `scripts/geo_view.py`.
  - `report.json`: counts, coverage, per-VS triangles and matched pixels, the camera UBO.
  - `coverage.png`: green = match, red = ours in front, blue = ours behind, magenta = missing.
  - `depth_game.png`, `depth_ours.png`, and `normals_sample.bin`.
  - `scripts\geo_view.py <capture> [--obj]` writes `topdown.png` and, on request, `scene.obj`.
    `geo_obj=1` makes the layer write the OBJ directly (~600 MB).

Settings: `geo_capture`, `geo_xfb_mb` (768), `geo_max_draws` (16384), `geo_depth_tol`
(0.002), `geo_obj`, `geo_timing` (G-buffer pass GPU time, logged every
`geo_timing_frames`), `geo_xfb_always` (XFB into device memory every frame, for the cost
measurement), `geo_patch_dump=N`.

## 3. Results

### Coverage (`captures/20261026-040924/geo/report.json`; `-035645` agrees to 0.01%)

| | |
|---|---|
| Draws captured | **835 / 835**, 36 / 36 vertex shaders |
| Triangles | **3,163,516**, exactly Σ indexCount/3 · instances over all draws; 0 dropped, no overflow, 145 MB |
| Depth pixels with geometry | 2,731,520 (100%: the sky dome writes depth too) |
| **Match** (within 0.2% of the game's depth) | **97.82%** |
| Ours in front | 2.18%: alpha-tested grates, railings, chains, cut-out planks (red in `coverage.png`) |
| Ours behind | 0.003% (73 px, on silhouettes) |
| Missing | **0%** |
| Normals vs G-buffer (view rotated) | \|dot\| median 0.998, p10 0.98, 94% > 0.9 |
| Eye position solved from VP | (0, 0, 0): camera-relative confirmed |

The 2.2% "in front" is the any-hit problem for phase 2. Those triangles need their
alpha mask, which means UVs and the material texture. XFB can capture the UV output too
(location 0 in the class A shaders) with one more `Offset` decoration.

### Cost per frame (RTX 5090, 2560x1067 internal, G-buffer pass GPU time, median of 300 frames, `geo_timing=1`)

| | G-buffer pass |
|---|---|
| Unpatched shaders (`geo_capture=0`) | 0.240 ms |
| Patched shaders, XFB idle | 0.242 ms (the patch is free when inactive) |
| XFB active every frame (`geo_xfb_always=1`, 145 MB into device memory), final per-draw design | 0.685 ms (**+0.45 ms**) |
| (same, earlier pass-wide XFB, not spec-valid) | 0.836 ms |

Not yet measured, estimated by bandwidth: clip → world conversion of 9.5M vertices
(16 B in, 12 B out) is about 0.15 ms of compute. The one-off capture frame (host-memory
XFB, readback, CPU raster) costs about 1 s and is diagnostic only.

### BLAS/TLAS over the captured frame (`bin\geo_blas_bench.exe <scene.bin> 7`, own Vulkan 1.3 device, median of 7)

| Variant | Build | Refit (update) | Size |
|---|---|---|---|
| One BLAS, `PREFER_FAST_BUILD` | 2.52 ms | – | 162 MB (+24 MB scratch) |
| One BLAS, `PREFER_FAST_TRACE \| ALLOW_UPDATE` | 3.37 ms | 0.56 ms | 182 MB |
| 835 BLAS (one per draw) in one build call, `FAST_TRACE \| ALLOW_UPDATE` | 2.74 ms | 0.55 ms | 185 MB |
| TLAS over the 835 BLAS | 0.15 ms | | 0.14 MB |

So a naive "capture everything, rebuild everything" frame costs about 0.45 + 0.15 + 2.7
+ 0.15 ≈ **3.5 ms**. Refitting instead of rebuilding brings it to about 1.3 ms. Phase 2
can get well below that, because most of the soup is static (next section). The bench runs on its own
device so that it measures the same hardware without touching the game. Enabling
`VK_KHR_acceleration_structure` on the game's device is the first step of phase 2 (see below).

### What works, what does not (yet)

- Works:
  - Every G-buffer draw and shader variant, skinned characters, wind, terrain and sky,
    with exact triangle counts.
  - The world-space solve, coverage and normal agreement.
  - `geo_view.py` (the top-down render of the soup shows the level layout out to the
    far city geometry).
  - Validation-clean (section 4).
- Limits:
  - The analysis takes ~1 s on the present thread in the capture frame.
  - Only the G-buffer pass is captured. Forward/transparent passes, decals and the
    depth-only prepass are not.
  - No UVs or material IDs yet.
  - The capture runs in traced frames only. `geo_xfb_always` is a cost probe and writes
    to a scratch buffer nobody reads.

## 4. Validation

`auto-run.ps1 -Validation -Set geo_capture=1,geo_xfb_always=1` (Khronos validation
below our layer), capture `20261026-041511`.

- The set of message IDs is identical to the pre-existing baseline
  (`captures/validation_gi1_fixed.log` from the Track C pass). Those messages are all
  the game's own: its shader modules, `StorageImage-FormatMismatch`, layout 09600 and so on.
- No message mentions transform feedback. The capture result under validation is identical.
- One error came from this work and is fixed: `VUID-vkCmdBindPipeline-None-02323`
  (pipeline bound while XFB is active), which led to the per-draw design.
- Rules kept:
  - No game resource is written. XFB goes to our buffers, and the depth/RT0 copies
    transition back to the original layout.
  - The UBO copy dispatch restores the game's compute bindings.
  - Non-finite clip positions (the game's NaN-parked vertices) are filtered before
    anything uses them.

## 5. Phase 2 plan: BLAS/TLAS and ray-query shadows/GI (as planned; section 6 has what was done)

1. **Device.** Bump the instance API version to 1.2 (the game asks for 1.1) or use
   `VK_KHR_buffer_device_address`, then add `VK_KHR_acceleration_structure`,
   `VK_KHR_deferred_host_operations` and `VK_KHR_ray_query` with their features at
   `vkCreateDevice`, using the same copied-create-info scheme. Caveat: the game
   enables `VK_EXT_buffer_device_address`. The KHR and EXT extensions must not be enabled
   together, so the core 1.2 `bufferDeviceAddress` feature is the way. This needs a
   validation run of its own.
2. **Per-frame capture into device memory.**
   - XFB into a device-local ring (double-buffered).
   - Ranges are already known at record time: each draw's offset is the CPU prefix sum
     of `indexCount/3 × instanceCount`. All BG3 G-buffer draws are direct, and the
     queries confirmed the counts exactly. So the BLAS build ranges can be recorded in
     the same command buffer with no readback. The stream queries become a debug option.
   - Then a compute pass: clip → camera-relative world (one 3×3 solve per vertex, the
     matrix comes from the UBO copy), NaN vertices → degenerate.
3. **BLAS policy, keyed by draw identity** (pipeline + index buffer/offset + index
   count + instance stream; all available in the draw hook):
   - Class A/C (static): build once in *absolute* world (`W + _m6`), so camera motion
     does not invalidate it. Reuse while the identity persists; the TLAS instance is
     identity minus the camera offset.
   - Class B (skinned) and D (wind): refit each frame (≈0.55 ms for the whole frame is the
     upper bound; these are a fraction of the 3.16M triangles).
   - Terrain: build on change only.
   - Compaction for static BLAS (182 → ~100 MB expected).
4. **TLAS** every frame (0.15 ms for 835 instances), in camera-relative space for precision.
5. **Alpha-tested geometry** (the 2.2%): capture UV as a second XFB output, keep the
   material's alpha texture index per draw (from the FS descriptor set), and mark those
   geometries non-opaque. Ray queries then run `CandidateTriangle` alpha tests in the
   shader. Foliage-heavy scenes need this most.
6. **First consumers** (core, hot-reloadable; inject where Track C does):
   - Ray-query sun shadows: one ray per pixel from G-buffer position and normal, folded into the
     screen-space sun shadow mask like the current contact shadows.
   - Ray-query AO / one-bounce GI replacing Track C's screen-space tracing. Hits are shaded
     from the screen when on-screen, otherwise from a world-space radiance cache.
   - Denoise with RR (Track A).
7. **Coverage of other passes**: shadow-cascade casters outside the view frustum. The
   G-buffer only holds what the camera sees, plus what BG3 failed to cull (the far city
   here). Off-screen occluders for sun shadows would need XFB on the cascade passes too
   (same mechanism, depth-only VS are patched already).

## 6. Phase 2 results: acceleration structures and ray queries on the game's device

Everything here is behind `rt=1` (read at instance/device creation, so restart the
game) and off by default. Captures: Steel Watch Foundry, the auto-run save, an
interior. Code: `layer/rtscene.cpp` (device, scene build), `layer/geocapture.cpp`
(per-frame capture), `shaders/rt/xfb_to_world.hlsl`, `core/gi.cpp` with
`shaders/gi/rt_shadow.hlsl`, `rt_ao.hlsl` and `rt_debug.hlsl`.

### 6.1 Device

- **Instance API 1.1 → 1.3** (`rt_instance_api`, at `vkCreateInstance`). 1.2 is
  what the extension dependencies need. 1.3 also makes our own shaders valid,
  since DXC targets `vulkan1.3` (SPIR-V 1.6); under 1.1 or 1.2 validation reports
  `VUID-VkShaderModuleCreateInfo-pCode-08737` for every one of them. Streamline
  already raises the version to 1.3 with `fg>=2`.
- **Extensions and features.** `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`
  and `VK_KHR_deferred_host_operations` are added, together with
  `accelerationStructure` and `rayQuery`. They go into a *copy* of the create info,
  after Track D's transform feedback and before Streamline's requirements. The
  game's structs are not touched.
- **The BDA conflict.** The game enables `VK_EXT_buffer_device_address` and chains
  its feature struct (sType 1000244000, with `bufferDeviceAddress = TRUE`).
  - Adding the KHR extension next to it is forbidden (VUID-03328).
  - At API ≥ 1.2 the AS/ray query extensions no longer depend on the KHR
    extension. `vkGetBufferDeviceAddress` accepts the EXT feature
    (VUID-vkGetBufferDeviceAddress-bufferDeviceAddress-03324), so the EXT
    extension stays.
  - In addition the standalone core `VkPhysicalDeviceBufferDeviceAddressFeatures`
    is chained. Only `VkPhysicalDeviceVulkan12Features::bufferDeviceAddress` is
    excluded together with the EXT extension (VUID-04748).
  - Reason: once the device is 1.2+, `nvngx_dlss.dll` (the game's own DLSS, in its
    `bin/` folder) allocates memory with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`.
    That needs the core feature (VUID-VkMemoryAllocateInfo-flags-03331, found by
    the first validation run). The layer's `vkAllocateMemory` hook logs who does
    this. Our own BDA buffers then carry the allocation flag too. `rt_bda_core=0`
    switches the core feature off.
  - A game without the EXT extension gets the core feature alone.
- **Fallback.** If `vkCreateDevice` fails, it is retried without RT, then without
  XFB. If any feature is unsupported, or the game already chains AS/ray query
  structs, the device is left alone and the log says why.
- **Verified with RT on:**
  - Ray Reconstruction creates and evaluates (`[ngx] RR create … Success`).
  - The GI runs.
  - DLSS-G initialises with `fg=2` (`-RealPresent`): `slSetVulkanInfo: ok`,
    `DLSS-G state: ok`, `slDLSSGSetOptions(mode on…)`, with RT shadows traced in
    the same run.

### 6.2 Per-frame scene build (host, in the G-buffer pass's command buffer)

1. **Capture.** The phase-1 XFB path runs every frame into a 384 MB device-local
   buffer (`rt_xfb_mb`). That buffer also has storage, BDA and AS-build-input
   usage.
   - Only draws whose output size is exact (direct, triangle lists) are captured.
     That is all 835 here. The soup therefore has no gaps, and its size is known
     while recording: Σ indexCount/3 × instances.
   - The camera UBO (`set0/b2`) of the first captured draw is remembered.
   - A hash of (pipeline, count, instances) per draw identifies the draw sequence.
2. **Convert** (`rt/xfb_to_world.hlsl`, one thread per triangle, in place). Solves
   the x/y/w rows of `_m2` for camera-relative W, as in phase 1, then checks clip z
   against the matrix.
   - Triangles with a vertex whose z was overridden (the sky dome writes z = 0)
     collapse to a point. A shadow ray must never hit the sky.
   - Triangles with a non-finite vertex (culled vertices parked at NaN) collapse
     the same way.
   - Collapsed triangles have zero area: they are never hit but stay "active", so a
     later refit stays valid. NaN positions would make them inactive and break that
     rule.
3. **BLAS**: one geometry, non-indexed, opaque, `PREFER_FAST_BUILD | ALLOW_UPDATE`.
   - It is **refit** when the draw-sequence hash and the triangle count match the
     last build. That is valid by construction: same draws in the same order give
     the same triangles.
   - Otherwise it is **rebuilt**, and at least every `rt_rebuild_frames` (60)
     frames anyway, to bound BVH decay.
   - Storage grows by 25% when exceeded. The old storage goes to `defer_destroy`
     (new kind `BG3_DEFER_ACCELERATION_STRUCTURE`).
4. **TLAS**: one instance, identity transform (the soup is already camera-relative),
   written with `vkCmdUpdateBuffer` each frame. `FORCE_OPAQUE`, no facing cull.
5. **Barriers**:
   - XFB/compute/AS → transfer/compute/AS before the build;
   - compute → AS after the conversion;
   - AS → AS between BLAS and TLAS;
   - AS → compute/fragment after.
   - Before the next frame's G-buffer pass: AS/compute → XFB, because the next
     capture overwrites what this build read.
   - Compute bindings are restored after the build (`restore_bindings`).
6. **Host API v3** `rt_scene(&tlas, &frame)` gives the core the TLAS and the
   frame index it was recorded in.
   - The game records a frame's G-buffer *before* presenting the previous frame,
     and its lighting after. So the lighting nearly always sees the build stamped
     one frame earlier: in a typical 300-frame window, 0–35 "this frame" and the
     rest "previous frame".
   - It is still this frame's geometry. The GPU runs the build first (submission
     order), and the handle is the same object every frame. The primary-ray check
     below passes during a camera pan as well.
   - The core accepts a TLAS stamped this frame or the previous one, and otherwise
     falls back to the contact shadows.

Memory for 3.2M triangles: 384 MB XFB, 214 MB BLAS (capacity 3.6M), 13 MB scratch.

### 6.3 Ray-query passes (core, hot-reloaded; in the GI pre-lighting hook)

- **Sun shadows** (`rt_shadows=1`, `gi/rt_shadow.hlsl`, full resolution, 1 ray per
  pixel).
  - Position: `ray_at(uv) × linear depth`. Normal: G-buffer normal in lighting
    space. Direction: towards the sun (`cam.v0`).
  - The origin is offset along the normal by `rt_bias` × camera distance (0.002).
    `ACCEPT_FIRST_HIT_AND_END_SEARCH`, `rt_tmax` 300.
  - Back-facing pixels (N·L ≤ 0) are shadowed.
  - The result is folded into the game's sun shadow mask (`set1/b7`) before the
    tiled lighting: `mask *= lerp(1, vis, rt_strength)`. It can only darken.
    Off-screen casters, which the G-buffer does not contain, still come from the
    game's cascades.
  - The pass replaces the screen-space contact shadows whenever a TLAS exists.
    Without one (`rt=0`, loading screens) the contact shadows run as before.
  - It also works with `gi=0`.
- **AO** (`rt_ao=1`, `gi/rt_ao.hlsl`, needs `gi=1`). Runs at half resolution on
  exactly the samples `gi_trace` used, with `rt_ao_rays` (2) cosine rays of
  `rt_ao_radius` (1.0 world units).
  - Occlusion falls off smoothly with hit distance, `1 − (t/R)²`.
  - It overwrites the screen-space visibility (`giRaw.a`) before temporal
    accumulation. The existing temporal pass and `gi_ao_apply` fold it into the
    game's AO.
  - The bounce (rgb) stays screen-space.
- **Debug views** (`rt_debug`, written over the lit diffuse at the end of the tiled
  lighting; specular zeroed):
  - 1: sun visibility;
  - 2: game mask in red, traced visibility in green;
  - 3: primary-ray check of the TLAS against the depth buffer;
  - 4: accumulated ambient visibility.
- No non-finite values can reach game resources: visibility is 0/1 or a bounded
  falloff, and the mask is multiplied by a factor in [0, 1].

### 6.4 Results

**The TLAS is in the lighting's space, exactly.** `rt_debug=3` traces from the
camera origin along each pixel's frustum ray and compares the closest hit with the
depth buffer ([docs/img/rt_primary_check.png](img/rt_primary_check.png); scene colour
for reference: [rt_scene_colour.png](img/rt_scene_colour.png)).

| | |
|---|---|
| Hit within 0.5% of the depth | **98.1%** of pixels |
| Hit in front | 1.9%: the alpha-tested geometry of phase 1 (chains, grates, railings), opaque in the BLAS |
| Hit behind, miss | 0.0% |

So the camera-relative W of the G-buffer VS is the same space as the tiled
lighting's `ray(uv) × depth`, and its rotation matches too. During a camera pan
([rt_primary_check_pan.png](img/rt_primary_check_pan.png)) only alpha-tested
geometry is off, so the TLAS is the same frame's.

**Sun shadows** ([rt_sun_visibility.png](img/rt_sun_visibility.png), `rt_debug=1`)
come out as expected from the geometry:
- the character casts a sharp shadow down-left across the walkway;
- pillars, railings and pipes shadow the floors;
- the lower hall lies in the shadow of its walls;
- there is no visible self-shadowing acne at `rt_bias=0.002`.

Comparison with the game's sun mask:
- The test scene is an interior. The game's mask is lit on only 0.01% of pixels
  (mean 1.2e-4, dump of the R8 mask pass). Its cascades contain the building's
  roof, which the G-buffer does not.
- Where the mask is lit, along the top edge of the long diagonal beam at the top
  right, the traced visibility agrees (lit).
- A small cluster at the top-left corner is lit in the game's mask but traced as
  shadowed. It is probably an alpha-tested grate that the BLAS treats as opaque.
- Overall, 27 of 55 lit samples agree (2× downsampled dumps).
- Because the fold only darkens, **RT shadows change nothing visible in this
  scene**.

Outdoors:
- In 5 of 12 `-Move` runs the held pan key carried the camera out of the foundry
  over the docks within one to two minutes. The view there is sunlit, with about
  780 draws and 0.86M triangles.
  - All five were `rt=1` runs, but six other `rt=1` runs stayed inside. With so
    few `rt=0` runs (three), the difference is more likely the timing of the key
    injection than RT.
  - Frame rate was the same either way (about 190 presents/s headless).
- The game's mask is lit on most of that frame (mean 0.61,
  [rt_docks_game_sun_mask.png](img/rt_docks_game_sun_mask.png)).
- The lit result with RT shadows and RT AO folded in
  ([rt_docks_lit.png](img/rt_docks_lit.png)) shows consistent shadows from
  characters, ships and rigging, with no artifacts. (The black area is the sea:
  water is drawn later, by a forward pass.)
- A direct comparison there, traced vs mask with `rt_debug=2`, has not been
  captured yet: the pan did not reach the docks in the later runs. For that run,
  set `auto_min_draws=600 auto_max_draws=2000 auto_stable_s=20
  auto_stable_frames=1000000`. `auto_max_draws` is new in the tracer: a trace
  then waits for a light (outdoor) view.

**AO** ([rt_ao_visibility.png](img/rt_ao_visibility.png) vs screen-space
[ss_ao_visibility.png](img/ss_ao_visibility.png), `rt_debug=4`): contact
occlusion is crisper under pipes, railings and the character, and occluders that
are off-screen or behind a surface now count.

### 6.5 Cost (RTX 5090, 2560x1067, GPU timestamps, medians of 300 frames, 2.8–3.2M triangles)

| Step | Static camera | Every frame rebuilt (`rt_refit=0`) |
|---|---|---|
| XFB capture (G-buffer pass 0.31 → 0.91 ms, `geo_timing=1`) | +0.60 ms | +0.60 ms |
| Clip → world conversion | 0.27 ms | 0.27 ms |
| BLAS | **refit 0.39 ms** (95–98% of frames) | rebuild 2.3–2.5 ms |
| TLAS | 0.024 ms | 0.024 ms |
| Sun shadows, 1 ray/pixel full res | 0.08–0.11 ms | |
| + AO, 2 rays/pixel half res | +0.13 ms (0.24 ms together) | |
| **Total** | **≈ 1.5 ms** | ≈ 3.7 ms |

- `rt_fast_trace=1` (`PREFER_FAST_TRACE`): rebuild 3.3 ms, refit 0.56 ms, with no
  measurable gain in trace time (0.225–0.235 vs 0.235–0.241 ms). Fast build stays
  the default.
- The phase-1 bench (0.55 ms refit, 2.5–3.4 ms rebuild on its own device) is
  reproduced on the game's device.
- With `rt_debug=3` the extra primary ray brings the shadow pass to 0.25–0.36 ms.
- In a static view the draw sequence is identical for most frames, so most frames
  refit. A camera pan changes the culled draw set, and then frames rebuild.

### 6.6 Validation

`auto-run.ps1 -Validation -Set rt=1,rt_shadows=1,rt_ao=1` against the same run with
`rt=0`:
- the message IDs are identical: the game's own `09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, its BufferBlock-layout
  `08737`, one `vkCmdCopyBuffer-00118`, and `non-acquired-swapchain-image-used`
  from headless presentation;
- none mentions acceleration structures, ray queries, our buffers or the XFB.

Found and fixed along the way:
- `VUID-VkMemoryAllocateInfo-flags-03331`, caused by NGX's allocations once the
  device was 1.2. Fixed by enabling the core BDA feature, see 6.1.
- `VUID-VkPipelineShaderStageCreateInfo-stage-08771`, an old one: the core created
  its compute pipelines with a null module and a chained `VkShaderModuleCreateInfo`,
  which needs maintenance5. It now creates real modules.
- `08737` for our SPIR-V 1.6 shaders, gone with `rt=1` (API 1.3). With `rt=0` it
  remains, as before (instance API 1.1).

### 6.7 What does not work yet / next steps

1. **Alpha-tested geometry is opaque** (1.9% of pixels, and false shadows under
   grates and chains). Capture UV as a second XFB output and keep each draw's
   alpha texture. Mark those geometries non-opaque and alpha-test in the ray query
   (`CandidateTriangle`).
2. **Only what the camera sees casts shadows.** That means no off-screen casters
   and no roofs the game hides. The fold keeps the game's cascades for those, but
   then RT can only add detail where the cascades already say "lit". Next: XFB on
   the cascade passes (their depth-only VS are patched already) to capture the
   sun's view of the casters. **Implemented, off by default (`rt_cascade_casters`,
   section 8.5 "Cascade casters as implemented"); not gated yet.**
3. **Static/dynamic split** (section 5, step 3). **Done, see [section 11](#11-static-dynamic-bvh-split-instanced-object-space-blas-rt_bvh_split1).**
   The design differs from this plan: object-space BLAS per unique mesh with the
   game's per-instance matrices, not world-space per-draw BLAS.
4. **Conversion bandwidth** (0.27 ms): write a separate float3 buffer, or convert
   inside the BLAS build's vertex fetch by giving the BLAS clip positions plus a
   transform per geometry. That works once the split exists.
5. **Outdoor validation and soft shadows.**
   - Capture `rt_debug=2` (and `3`) at the docks (see 6.4) and measure agreement
     with the game's mask where it is nonzero.
   - Then the sun's angular size: cone-sampled rays, then RR or a small denoiser.
6. **RT GI**: replace the screen-space bounce with ray-query hits shaded from the
   screen when on-screen, otherwise from a radiance cache. Denoise with RR (Track A).

Settings (`bg3rtx.ini`):
- `rt`, `rt_build`, `rt_refit`, `rt_rebuild_frames`, `rt_fast_trace`, `rt_xfb_mb`,
  `rt_bda_core`;
- `rt_shadows`, `rt_strength`, `rt_bias`, `rt_tmax`;
- `rt_ao`, `rt_ao_rays`, `rt_ao_radius`;
- `rt_debug`, `rt_timing_frames`.

The log reports `[rt] scene build GPU …` (convert, BLAS, TLAS per rebuild/refit),
`[rt] last N frames: … rebuilds, … refits` and `[core] rt passes: GPU cost …`.

## 7. Phase 3 (2026-10-26): alpha-tested geometry and hybrid RT GI — PAUSED, WIP

Status: both parts work in game behind switches that are **off by default**
(`rt_alpha=0`, `gi_rt=0`). Paused on request before the final write-up; the
numbers below are from the runs done (Steel Watch Foundry, static camera,
RTX 5090, 2560x1067, GPU timestamps, 300-frame medians/averages).

### 7.1 Alpha test (`rt_alpha=1`, restart; `rt_alpha_test` is a live A/B switch)

- **Where the alpha test lives.** BG3's G-buffer FS never discard; the depth
  prepass does (G-buffer then depth-tests against it). So with `rt_alpha=1` the
  scene is captured from the **depth prepass** (depth-only D24S8 at render
  resolution; its small 5-draw sibling pass is learned and skipped), XFB stride
  24: `gl_Position` + the VS's lowest-location vec2 output (the UV in every
  prepass VS). `tools/xfb_patch_test <dump> <out> 24`: 2385/2385 VS patch and
  pass `spirv-val`.
- **Which draws alpha test, and with what.** `scripts/alpha_table.py` decompiles
  every depth-only discarding FS in the shader dump (1243) and writes
  `shaders/rt/alpha_fs.txt` (372 shaders):
  - `vt` (252): BG3's virtual texturing (Granite). Alpha = `.w` of the albedo
    tile, through an indirection texture (`texelFetch`, page bits in `.x`) into
    the tile cache array; per-draw UV transform + tile-set index in the material
    UBO (set 1 b0), tile-set constants (2 row-major mat4) in SSBO set 0 b12.
    `rt_common.hlsli` `vt_alpha()` replicates the lookup; mip from a ray cone.
  - `tex` (120): a plain texture's `.w`/`.x` (optional UBO uv scale). Only
    where our test is conservative: condition reads the sample directly; blends
    (`mix`) and branch-selected alpha are rejected (they let rays through solid
    pixels: 0.30% "behind" before this rule).
- **Per draw** (`layer/alphatest.cpp`): UBO/SSBO constants read from the game's
  mapped memory at the draw; views recorded; after the pass the records go to a
  buffer and the views into one of 8 ring descriptor sets (set 1 of the core's
  RT passes: records, UV soup, Texture2D[256], Texture2DArray[4], 2 samplers).
  Only views bound that frame. Alpha draws go to BLAS geometry 1 (not opaque);
  ray queries loop `Proceed()` and commit non-opaque candidates that pass.
- **Primary-ray check** (`rt_debug=3/5`, `rt_stats=1`, same view, live A/B):

  | | alpha test off | on |
  |---|---|---|
  | match | 98.12% | **99.09%** |
  | in front | 1.88% (vt cutouts 1.13, tex 0.01, opaque 0.73) | **0.91%** (vt 0.17, opaque 0.73) |
  | behind | 0.002% | 0.007% |

  The remaining 0.73% "opaque" is screen-door dithered geometry (camera
  cut-away / LOD fades, [rt_alpha_primary_check.png](img/rt_alpha_primary_check.png),
  purple) — correctly solid for secondary rays — plus unrecognised cutouts.
- **Cost**: shadows + AO 0.201 → 0.22 ms with the alpha test; BLAS: 2.23M opaque
  + 0.83–0.93M alpha triangles, refit 0.88 ms (convert 0.40 at stride 24),
  rebuild 3.4 ms; XFB buffer 384 MB, alpha region 64 MB.

### 7.1b Alpha test at secondary hits: grass fix (2026-10-26)

**Symptom.** Campsite: black, sharp-edged polygons on the ground under grass
(GI visibility 0; gone only with `gi=0`), the same under a twig in reflections,
and sun RT shadows of grass shaped like the card, not the blades.

**Root cause.** The grass and ground-cover family (`73849471`, 148 prepass draws
in Campsite; also `2f1b0ffd`, `b2d72ea7` and 21 other shaders, 24 in all) was
**not in `alpha_fs.txt`**, so its draws went into the TLAS as opaque geometry.
Its Granite alpha test keeps the selected sample in a `vec4` and reads `.w` from
it in the next statement (`_225 = mix(textureGrad(...), vec4(1,0,0,1), bvec4(... != 2202.0)); _226 = _225.w`);
`alpha_table.py` only recognised the inline `...2202.0)).w`. Flat cards lying
on the ground then blocked every GI ray from the pixels under them at t ~ 0.
Evidence (same view, `rt_debug=5`): the patches are "in front, opaque
geometry" (purple), not alpha-tested hits; `rt_debug=4` shows visibility 0 in
the same polygons; with `rt_stats=1` the alpha path itself returned solid only
by evaluation (no record 0%, no UV 0%, prim out of range 0% in steady state,
tile set not resident 0%, page from a coarser level ~1%), so none of the
fallbacks was the cause. Alpha-tested draws per frame went 108 → 412 once the
family was recognised. This is in the TLAS build, so it fixed every ray type
(GI, AO, sun and local shadows, reflections) at once.

**Second table bug (fixed, not the Campsite cause).** 76 vt shaders discard
twice: an early-out `alpha * mask < 0.001` (or 0.01), then
`fade * alpha * mask < 0.5`. The table took the first test's cut-off, so every
texel with any coverage was solid (whole cards solid at a filtered mip).
`effective_cut()` now takes the strictest constant cut-off of the tests that
keep the alpha as a factor (through `mix` only when both operands carry it,
`bvec` selects of the Granite sample allowed; UBO cut-offs and
`gl_FragCoord` screen-door dithers skipped). Line format: `cut=<effective>
cut0=<first>`; `rt_alpha_cut=0` (live) restores the first.

[img/alpha_campsite_crop0.png](img/alpha_campsite_crop0.png) (before | after | diff x4, render-res compose),
[img/alpha_campsite_crop1.png](img/alpha_campsite_crop1.png) (sun shadow of a twig clump: a rectangle before, the twigs after),
[img/alpha_campsite_vis_crop0.png](img/alpha_campsite_vis_crop0.png) (`rt_debug=4`),
[img/alpha_campsite_primary_crop0.png](img/alpha_campsite_primary_crop0.png) (`rt_debug=5`: purple opaque-in-front gone).

**Evaluation changes in `rt_common.hlsli`** (live switches, all on by default):
- `rt_alpha_stochastic=1`: the mip comes from the ray cone with the game's own
  Granite formula; the indirection entry already points at the finest resident
  ancestor of a missing page (its tile scale `e & 15` is smaller), and the read
  reports that level. Where the read is blurred (level > 1 above mip 0), the
  hard cut-off blends over one level into a hashed threshold (pcg3d of the hit's
  uv bits, the triangle and the frame), so P(solid) tends to the filtered alpha,
  i.e. the coverage: partial coverage of distant grass is unbiased under RR
  instead of all-solid or all-clear. Levels 0-1 keep the game's exact cut-off.
- `rt_alpha_unknown=1`: a candidate whose alpha cannot be read (no record, no
  UV, prim out of range, non-finite, non-resident tile set) lets visibility rays
  (GI, AO, reflections) through and stops shadow rays. Evidence: these cases
  are 0% in steady state and 0.6-2% of visibility candidates for a frame after
  a capture change (prim >= tris: an InstInfo/BLAS mismatch, not
  investigated further); a false "solid" there gives a hard black polygon in
  GI (the artefact class above), a false "clear" in a shadow gives a light
  leak, which is the worse error for shadows.
- `rt_stats=1` logs per ray class (`alpha candidates of visibility/shadow
  rays`): each fallback, vt/tex solid/clear, records with a loose first
  cut-off, blur level >= 2 / >= 4, stochastic share, pages from a coarser level.

**QA harness** (`python -m scripts.qa run --scenes S3`, Campsite; "before" is the
same build with the old table and `rt_alpha_cut/stochastic/unknown=0`):

| detector (‰ of the frame) | before | after |
|---|---|---|
| hard-dark, GI (final / lighting) | 10.9 / 2.35 | **0 / 0** |
| hard-dark, all RT (final) | 20.4 | **0** |
| sun extra casters | 15.8 | 3.3 |
| flicker excess area (std on/off) | 76.4 (1.73) | 15.6 (1.28) |
| brightening, GI (final) | 2.26 | 7.86 |
| brightening, sun RT shadows (final) | 0.53 | 4.40 |

Brightening rose where the grass no longer blocks light: under the log at the
bottom right the traced visibility now replaces the game's SSAO (which treats the
grass as solid) with ×2.8 more ambient, and the RT sun now passes between twig
blades where the game's shadow map is a blurred mass. These are flagged for
review, not confirmed errors; a run with `rt_alpha_stochastic=0` to separate
the stochastic part did not complete (the game was closed by another session's
run). S1 (foundry) with the fix: hard-dark GI 0.066‰, no baseline compared.

**Regression checks (2026-10-27, after merging main).** `-Validation` in Campsite
gameplay: same message IDs as the HEAD baseline. Non-finite: 0 in every capture,
including a trace taken while panning (Goblin Camp, `-Move`). `QuickSave_3784`
(old table + legacy switches vs new, same view): mean luminance -0.11%, no
visible change on the piers, railings and boats. Goblin Camp foliage was not
compared: that save enters a cutscene a few seconds after loading, at a varying
time, so the two runs did not see the same view.

**Not verified.** No `-Move` run reached its end: three runs (Goblin Camp,
Campsite, and the QA run after the merge) stopped presenting a few seconds after
gameplay or a trace (0 submits, GPU idle, process alive; a CPU-side stall, not a
device loss). Another worktree's QA runs show the same stall in S3 and S7, so it
is probably not this change, but this was not proven with a run of main alone.

**Cost** (Campsite, 2560x1067, 300-frame averages, shared GPU, noise ~0.2 ms):
4 x more alpha-tested draws (1.85M alpha triangles), visibility candidates
2.0M → 7.1M/frame; GI 2.95-3.0 → 3.0-3.4 ms, RT passes 1.35-1.40 → 1.40-1.53 ms.

**Not done.** 20 vt shaders keep a loose first cut-off (their 0.5 test reads
the alpha through `mix(1.0, alpha, ubo)` or a UBO cut-off; reading the UBO at
the draw would settle them). Blended-texture `tex` shaders (`42507cb4`) stay opaque.

### 7.2 Hybrid RT GI (`gi_rt=1`, live; see TRACK_C_GI.md)

`shaders/gi/gi_trace_rt.hlsl` replaces `gi_trace` (+`rt_ao`): same samples and
outputs, `gi_rays` alpha-tested ray queries to `gi_rt_radius` (1 m). Hit on
screen with matching depth: last frame's radiance; otherwise occlusion only
(`gi_rt_fallback=0`). Hits: 18% of 2.73M rays/frame; 86% shaded from the
screen, 11.7% hidden, 2.2% off screen.

| trace pass, 4 rays/sample, half res | GPU |
|---|---|
| screen-space (16 steps) | 0.248 ms |
| ray-traced, alpha test on / off | **0.175** / 0.124 ms |
| GI total (pre + post) ss + rt_ao → gi_rt | 0.44 → 0.35 ms |

Images: [gi_ss_visibility.png](img/gi_ss_visibility.png) vs
[gi_rt_visibility.png](img/gi_rt_visibility.png) (`rt_debug=4`; mean
visibility 0.74 vs 0.80: no screen-space halos at depth edges, crisper contact
occlusion), irradiance [gi_ss_irradiance.png](img/gi_ss_irradiance.png) vs
[gi_rt_irradiance.png](img/gi_rt_irradiance.png) (`gi_debug=1`).

### 7.3 Validation

`-Validation` with `rt=1 rt_alpha=0 gi_rt=0` vs `rt=1 rt_alpha=1 gi_rt=1`
(toggling rt_ao, rt_debug=5, rt_stats, rt_alpha_test live): identical message
IDs (game's own 09600, 10788, StorageImage-FormatMismatch, OutputNotWritten,
CopyBuffer-00118, 08737, headless swapchain); none from our work.

### 7.4 Not done / next steps

- Not verified: camera motion (`-Move`) with `rt_alpha=1` (rebuild path, prepass
  ordinal relearning), outdoor/foliage scenes, `rt_alpha` + `fg>=2`.
- `rt_stats=1` costs ~2 ms (atomics); measurement only.
- 781cd8d1-style cutouts (alpha chosen by a vertex-colour branch) and blended
  `tex` shaders stay opaque; would need a second captured varying.
- Off-screen hit radiance: a world-space radiance cache instead of fallback 0.
- Opacity micromaps would remove most candidate shader work.
- Decide default-on for `rt_alpha`/`gi_rt` after the motion run; update ROADMAP.

## 8. Effect catalogue and roadmap (analysis, 2026-10-26)

A feasibility study of further ray/path-traced effects. It was done without running
the game, from the decompiled shaders of `captures/20261025-235042` (mainly the
tiled lighting `f469db78`), the report of that capture, and the code of
Tracks A, C and D. The plans in sections 5-7 stay as they are. This section only adds
effects that were not already planned, plus the enablers they share. The quality
audit of the existing effects, and the rules every new effect must follow, are in
[QUALITY.md](QUALITY.md).

### 8.1 What the game gives us (data inventory)

Everything below is in the **lighting space**, which is camera-relative world
(`P = ray(uv) × linear depth`, the same space as the TLAS; see 6.4). Absolute world =
`P + _m6` of the camera UBO (set 0 b2). Light positions in the light array are in
this space too: the lighting computes `light.m0.xyz − P`.

| Data | Where | Have it? | Notes |
|---|---|---|---|
| Position, view-space normal, base colour, roughness, F0 (`0.16·spec²`/metal), metalness, light-channel bits (RT2.w bits 4-6) | G-buffer RT0-RT2, linear depth; bound to the tiled lighting at set 1 b1/b2/b3/b5 | **yes** (core snapshot) | Primary hit only. RT3 is almost empty. |
| **Local lights** | lighting set 1: b23 cluster headers (ivec4: x offset; y bits 0-11 points, 12-23 spots, 24-31 env probes; z bits 0-9 projector lights), b24 index list, **b25 light array** (struct `_50`) | **readable** (same `query_binding` route as the camera UBO; not done yet) | `_50`: m0.xyz position, m0.w = 1/radius; m1.xyz colour, m1.w spot cos-outer; m2.xyz spot direction, m2.w cos-inner; m3 < 0 → × global scale `b13[0]`; m4 falloff exponent; m5 falloff model/bias (sign selects the model); m6 light-channel mask; m7 bit 0 = casts a shadow, m7>>2 = shadow-atlas slot; m8 shadow strength. Clusters are froxels (log depth), so they only index lights for points inside the view frustum. |
| Projector (textured) lights | b29 (struct `_53`, row-major mat4x3 projection, cookie index into b45 `texture2D[]`) | readable | Third light loop in `f469db78`. |
| Local-light shadows | b40 (per-face mat4 + atlas rect + bias), b41 atlas | readable | Point lights: 4-face **tetrahedral** maps. 12-tap Vogel-disc PCF rotated by interleaved gradient noise (`fract(52.98·fract(0.0671·x + 0.00584·y))`), faded by camera distance (`b39 _m34`) and by m8. Lights without m7 bit 0 have **no shadow at all**. |
| Sun | camera CB set 0 b1: `_m0` direction (= `cam.v0`), `_m2` colour, or a transmittance-LUT colour when the atmosphere is on; × `b13[0]` | **yes** | **Angular radius:** the sun specular uses a representative-point disc with `sin θ = 0.0045901`, `cos θ = 0.9999894` (θ = 0.263°, the real sun). Shadow = mask b7 (ours to fold into) × two cloud-shadow textures. |
| Ambient | global sky cubemaps b34-b37 (irradiance + prefiltered spec, two sets blended by `b39 _m29`), local parallax-corrected box/sphere env probes (b33 list + b45 cube array), DFG LUT b38, × `b13[0]·100` | readable | Diffuse ambient = irradiance · diffuse colour · DFG.z · AO. Specular ambient = prefiltered probe · (F0·A + B) · Lagarde specular occlusion(AO, N·V, roughness). **There is no SSR in BG3.** All indirect specular comes from probes. |
| AO | b6 RGB10A2 `.x` = the game's **half-res SSAO** (units 88-91: `0effa9ff` → `af81b62d` deinterleave → `a6bb35c6` blur) | **yes** (we fold into it) | The lighting also multiplies **direct** light (local lights and sun) by `mix(1, AO, b0._m0.y)` (diffuse) and `mix(1, specOcc, b0._m0.y)` (specular). That value has not been read yet. |
| BRDF | `f469db78` | **known exactly** | GGX D, height-correlated Smith (the approximate form), Schlick F; renormalised Burley diffuse × `mix(1, 0.662, r)`; roughness ≥ 0.09, plus specular AA from the quad normal variance (`sqrt(r² + min(var, 0.18))`). |
| Atmosphere | inside the lighting: aerial-perspective transmittance multiplies both outputs, and the in-scatter is added to diffuse (b10) | known | So the lit diffuse buffer contains in-scatter. Negligible at gameplay distances (the planet is scaled, R ≈ 636 km), but not zero. |
| Emissive / sky / FX | a B10G11R11 render-resolution target written **before** the lighting (units 78-85: sky `c9f013fc`, half-res clouds, projected FX) and added in compose `5c3fdabc`: `(diffuse_after_SSS + specular + this) · fogT + fogInscatter` | not captured | Not in our `prevRadiance` (QUALITY Q8). Where emissive *materials* go is unverified: RT3 is near zero, so either this target or the forward pass. |
| SSS | units 121/122 (`6f535075`, `d1669194`), profiles in an SSBO (set 2 b15); they read our diffuse buffer | yes (we inject before it) | Screen-space blur, two passes. |
| Fog / volumetrics | froxel grid 285x119xZ: `c3e212ad` density, `ca8adce7` scattering (sun cascades via samplerShadow + local lights), `8a4e0720` integration; tricubic lookup in compose | known | Volumetric sun shafts already exist (from the cascades). |
| Transparents | forward pass unit 125 (46 draws, water among them), after a colour copy for refraction (unit 124) | **not in the TLAS** | Would need XFB on the forward pass. |
| Off-screen geometry | — | **no** | The TLAS holds what the camera's G-buffer (or prepass) drew: frustum-culled, **roofs cut away indoors**, no forward geometry. The shadow cascades contain more (roofs, casters around the view): 6.7 item 2. |
| Materials at secondary hits | — | **partly feasible** | `vt_alpha()` already samples the Granite tile set for alpha. The same lookup in the albedo layer gives the hit's **base colour** for the 252 VT shader families. Normal/physical layers and plain-texture materials would need more decoding. |

### 8.2 Composition: how a new term enters without double counting

Every effect must name the game term it replaces and use one of three patterns:

1. **Fold** through a game input whose meaning is verified. Example: visibility into AO
   `.x`, or into the sun mask b7. Use *replace* semantics when the new signal contains
   the old one (RT AO contains SSAO's occluders). Use *multiply* only when the two are
   independent (RT on-screen casters vs off-screen cascade casters).
2. **Exact delta.** Re-evaluate the game's term `T_game` from the decompiled code with
   the same bound resources, then write `out += T_ours − T_game` into b10/b11 at
   the post-lighting hook (where `gi_composite` runs, before SSS). Since `T_game` is
   computed exactly, the result is `out − T_game + T_ours ≥ 0` whenever
   `T_ours ≥ 0`, with no clamping bias. This works for per-light shadow terms, the
   specular probe term and the diffuse ambient term.
   - Limitation: the aerial-perspective transmittance is not replicated, so
     deltas fade back to the game's term beyond about 100 m.
3. **Lighting-shader patch** (enabler D-EN5): a SPIR-V patch of the `f469db78` family
   that multiplies or replaces specific terms by values from our images. It is the
   cheapest at run time, but the most fragile.

| Game term | Replaced by | Pattern |
|---|---|---|
| SSAO (b6 `.x`) | RT ambient visibility | fold, **replace** (today: multiply, see QUALITY Q2) |
| Sun mask (b7) | RT sun visibility (hard, then soft) | fold; multiply until cascade casters are captured, then replace within TLAS coverage |
| Per-light atlas shadow, or no shadow | RT per-light visibility (D-LS) | exact delta (v1), patch (v2) |
| Specular ambient (probes) | RT reflection (D-RF) | exact delta (v1), patch (v2) |
| Diffuse ambient (probes · AO) | RT/cached irradiance (D-GI2) | fold (today's hybrid) → exact delta |
| Emissive/FX target | (source of bounce only) | read-only (D-EM) |

### 8.3 Feasibility of the candidates (summary)

Costs are for the RTX 5090 at 2560x1067 (2.73 M pixels), scaled from what was measured:
- sun shadows: 2.73 M rays, 0.08-0.11 ms;
- hybrid GI: 2.73 M alpha-tested rays of 1 m, 0.175 ms.

That works out to about **0.04-0.07 ms per million short rays**. For long closest-hit
rays, assume 2-3× that.

| Candidate | Data needed → have? | Injection | Gain | Cost | Risk | Verdict |
|---|---|---|---|---|---|---|
| Soft sun shadows | sun dir + **angular radius 0.00459 rad** (found) → yes | sun mask b7 (as now) | medium: penumbrae grow with caster distance (~0.9% of the distance), mostly for buildings and trees | 0.10-0.15 ms + denoise (RR: 0) | the fold only darkens, so penumbrae appear only where the cascades are lit | **already planned** (6.7 item 5); refined in 8.5 |
| RT shadows for local point/spot lights | light array b25 (pos, 1/r, colour, cone, flags), clusters b23/b24, atlas b40/b41 → **readable** | exact delta at post-lighting | **high**: BG3 has many unshadowed fill and torch lights that leak through walls and floors, and its shadowed lights use a low-res tetrahedral atlas | 0.3-0.6 ms (stochastic 1-2 rays/px + RR) | art intent: some fill lights are meant to leak, so shadowing is per-light and opt-in | **new: D-LS** |
| ReSTIR DI / RTXDI | as D-LS; RTXDI SDK **not** in `external/` | sampler inside D-LS | medium: BG3 has tens of lights per view, not thousands. Worth it only with emissive triangles. | +0.2 ms over D-LS | RR wants permuted/randomised reuse | **new: D-DI** (after D-LS) |
| RT reflections (glossy and mirror) | roughness/F0/metal decode → yes; hit radiance: on-screen → yes (reprojected); off-screen → needs EN1 + EN2 (+EN3) | exact delta on the specular ambient (probes) | **very high**: there is no SSR in BG3; wet stone, polished floors and metal show only probe reflections | 0.5-1.0 ms | the TLAS lacks geometry behind the camera and above the screen (culled), so the probe is the fallback; RR needs specular hit distance and matrices | **new: D-RF** |
| Multi-bounce GI + radiance cache | TLAS, hit albedo (EN2), lights at hits (EN1), sky (b34) → mostly | today's hybrid (fold) → delta on the diffuse ambient | medium-high: the GI reach grows from 1 m to the room scale, with colour bleeding | 0.4-0.6 ms, 160 MiB (SHaRC) | **roofs are cut away indoors**: a TLAS miss is not sky, so misses must keep the game's probe; that needs cascade casters for closed rooms | radiance cache **planned** (C/D "next"); the multi-bounce, screen-seeded SHaRC design is **new: D-GI2** |
| Emissive surfaces as light | on-screen: the emissive/FX target (8.1) → capturable; as RTXDI lights: emissive triangles + radiance → **no** | bounce source (`prevRadiance`) | low-medium (glyphs, lava, braziers; flames are particles with their own point lights) | ~0.02 ms | where emissive materials go is unverified | **new: D-EM** (part a now, b deferred) |
| Translucency / SSS | SSS pixel mask (stencil/flags, unverified), TLAS for thickness → partly | delta into diffuse before the SSS blur | low-medium (back-lit ears, leaves, cloth) | 0.05-0.1 ms | finding the SSS/foliage classification | **new: D-TR** (low) |
| RT refraction (glass, water) | refractive surfaces are forward-drawn and not in the TLAS; the game's refraction samples the unit-124 copy | patch the forward FS families | low (BG3's water is stylised; screen-space refraction rarely fails at the isometric angle) | 0.3 ms + XFB on the forward pass | high: shader patching of forward materials; RR resampling caveats | **new: D-RFR**, deferred |
| RT for transparents/particles | forward pass not captured | — | low | — | — | **not recommended**. Do RR transparency guides instead (QUALITY Q12) |
| Volumetric light shafts with RT visibility | froxel grid known; sun-view geometry needs cascade capture | patch `ca8adce7`'s cascade lookup, or replicate it | low-medium: the game's froxels already sample the cascades; RT adds only on-screen detail | ~0.1 ms (1 ray per froxel) | patching a 1227-line compute shader | **new: D-VOL**, low |
| Full path tracing replacing the lighting | geometry incomplete (culled, roofs cut away, no forward), secondary materials approximate, light list view-culled, baked probe look, unshadowed art lights | replace b10/b11 | — | 5-8 ms | **infeasible as a product**: it cannot reproduce what the game does not draw | **new: D-PT**, a reference/debug mode only |

### 8.4 Enablers (shared by several effects)

**D-EN1 Light-list snapshot.**
- Add set 1 b13, b23, b24, b25, b29, b39 (the lighting constants), b40 and b41 to the
  core's `Snapshot` at the tiled-lighting dispatch, and bind them to our passes
  read-only.
- Write `shaders/rt/bg3_lights.hlsli`: the struct decode, the cluster walk (same
  froxel formula as `f469db78`: `log2(depth·far)·_m21 + _m22`), the exact
  unshadowed contribution (falloff models, spot cone, channel mask, ×`b13[0]`, AO
  factor `mix(1, AO, b0._m0.y)`), and the atlas PCF including its IGN rotation.
- Validation: `light_debug=1` recomputes Σ of all lights and compares it with the
  game's b10/b11 minus ambient and sun (by toggling the ambient terms via debug
  modes), so the mean error must be < 1%.
- Log statistics: number of lights, number shadowed, lights per pixel.
- Also read `b0._m0.y` (the AO-on-direct factor) and log it.
- Cost: 0.

**D-EN2 Hit material.**
- Capture the UV for **all** draws (stride 24, as `rt_alpha=1` does for the prepass)
  and keep a per-draw material record for every VT draw, not only alpha-tested
  ones: tile-set constants, UV transform, albedo layer.
- `hit_base_colour(prim, bary, cone)` = the `vt_alpha()` lookup returning `.rgb`.
- Plain-texture draws, and draws not recognised, get a per-draw average colour
  (sampled once from the G-buffer where the draw is visible) or 0.5 grey.
- Roughness/metal at hits: per-draw constants from the G-buffer median.
- Normal: geometric, from the triangle.

**D-EN3 Geometry retention and closure.**
- The static/dynamic split (6.7 item 3) plus **retention**: a static instance
  stays in the TLAS for `rt_retain_frames` (e.g. 300) after the game stops
  drawing it (culled, behind the camera).
- Together with cascade-caster capture (6.7 item 2, which also brings the roofs the
  camera cut-away hides), this gives off-screen geometry for reflections and GI,
  and closed rooms.
- Instance masks: bit 0 G-buffer, 1 cascade-only, 2 retained, 3 dynamic. Each
  effect chooses its masks. For example, the sun uses all of them; reflections
  exclude cascade-only geometry if it is missing faces that face the camera.

**D-EN4 Exact-delta library.**
- Replicate the game's specular ambient (global and local probe loop, DFG,
  Lagarde specular occlusion) and diffuse ambient from `f469db78` lines
  ~1210-1450, in `shaders/rt/bg3_ambient.hlsli`.
- Validate it against b10/b11 the same way as EN1.

**D-EN5 Lighting-shader patch hooks (v2 of the deltas).**
- A SPIR-V pass over the ~15 `f469db78` permutations that adds
  `ourFactor(pixel, term)` reads at three sites:
  - the per-light shadow factor (`_802`/`_1087`/`_1427`);
  - the specular ambient `_1684`;
  - the diffuse ambient `_1687`.
- This removes the ALU of re-evaluating the lights and makes the composition exact,
  including the transmittance.
- Do it only after the deltas are validated, as an optimisation.

### 8.5 Refinements to already-planned items

- **Soft sun shadows (6.7 item 5).**
  - Sample a cone of half-angle `rt_sun_radius` (default **0.00459 rad**, the game's
    own value) with uniform disc samples, 1 ray per pixel at full resolution.
  - With `rr=1`: per-pixel white noise from a proper hash (no checkerboard), fed to
    the mask raw; RR denoises.
  - With `rr=0`: NRD SIGMA (in `external/NRD`) from the occluder distance, or a
    penumbra-sized bilateral blur.
  - Keep the multiply fold until cascade casters exist, then replace within TLAS
    coverage (`mask = vis_rt` where the cascades' casters are all in the TLAS).
  - An artistic `rt_sun_scale` multiplier, since BG3's PCF cascades are softer than
    the real sun.
  - Cost 0.10-0.15 ms.
- **Cascade-caster capture (6.7 item 2).**
  - It is now also the prerequisite for closed interiors in GI and for volumetric
    shafts.
  - Capture the largest cascade first: one more XFB pass of about +0.3 ms, and
    duplicates with the G-buffer soup are harmless for any-hit rays.
  - **Implemented 2026-10-29 (`rt_cascade_casters=0`, off until gated), see below.**

#### Cascade casters as implemented (`rt_cascade_casters`)

Goal: RT sun shadows from what the camera does not draw but the cascades do, mainly
cut-away roofs, so the RT sun can replace the game's mask under roofs too.

- **Matrix source (established).** Every cascade VS in the shader dumps of
  `captures/20261026-165848` (`1cf36b97`, `a7bf37c6`, `646a930e`, `dcb21688`, ...)
  ends like the camera's: `gl_Position = vec4(W, 1) * set0.b2._m2`, y negated. So each
  cascade pass binds its own camera-layout UBO at set 0 binding 2 with its orthographic
  view-projection at +128, and W is the same camera-relative world (the prepass and the
  cascades share VS `646a930e`). The Shadow UBO `_m10[k]` is not needed.
- **Detection.** Depth-only, D16, square >= 1024, a layer of an array of >= 3 layers
  (the far shadow cache has 2). The cascade index is the depth view's base layer.
- **Capture** (`layer/geocapture.cpp`). Cascade `rt_cascade_index` (default 1: cascade 0
  is the smallest, 30 draws in the foundry; 1 covers the player's building and its
  neighbours; check the extents in the `[cascade] matrix` line) is captured by the
  existing XFB patch into its own buffer per frame parity (`rt_cascade_mb`, x2). Skipped:
  draws the camera source pass drew in this or the last frame (key: kind, counts, firsts,
  vertex binding 0, index range, and the instance stream binding or the set 1 binding 0
  UBO that places it); alpha-tested draws (the FS can discard; no UV or material here);
  indirect and non-sizeable topologies; draws with another set 0 binding 2.
- **Conversion.** Right after the pass, in its command buffer:
  `shaders/rt/xfb_to_world_ortho.hlsl` solves W from the x, y, z rows (the w row of an
  orthographic matrix is (0, 0, 0, 1)); a vertex whose w is not 1 collapses its triangle.
- **Next frame** (`rtscene.cpp`, `bvhsplit.cpp`; split builds only). The build of frame F
  takes F-1's soup (its command buffer submitted) and adds one BLAS (new each build; the
  old one is released once no live scene slot uses it) and one instance: translation
  `origin_cascade(F-1) - cam(F)`, mask 0x02, custom index 0xFFFFFF, not opaque. The cascade
  VS's world position is relative to the cascade UBO's own origin (byte 208), which is
  92-96 m from the camera's in S7 (`rt_cascade_origin=1`; 0 = the first version's
  `cam(F-1) - cam(F)`, which put the casters 92 m off and leaked).
- **Alpha-tested casters** (`rt_cascade_alpha=1`): 435 of 694 cascade-2 draws in S7 are
  alpha-tested; they go in as opaque (cutouts solid). Without them the RT still saw
  "game dark & RT lit" on 9.5% of traced px; with them 0.47%. `rt_cascade_mb=160`: cascade 2
  with alpha holds 1.47M triangles (~100 MB); cascade 3 overflows (340 draws dropped).
- **Shaders.** `RT_VIS_MASK` / `RT_HIT_MASK` in `rt_common.hlsli` are the literal 0xFF in
  ordinary builds (SPIR-V of all 66 existing outputs byte-identical). The core picks
  `gi/cascade/` (`gi/cascade_rebase/` with `rt_scene_rebase`) variants with the key on:
  mask 0x01 for GI, reflections, AO, local lights, origin and primary checks; the sun
  ray (`rt_shadow.hlsl`) traces 0xFF and ignores cascade hits nearer than
  `RT_CASCADE_TMIN` = 0.5 m (a coarser copy of the shaded surface; a roof is >= 2 m up).
- **Trust** (`core/sun_trust.cpp`). With `rt_cascade_roofs_trust=1` the roof volumes are
  not uploaded: roofs no longer stop the replace. The off-screen test stays.
- **Logs.** `[cascade] sun cascade pass found` once per layer; every `rt_timing_frames`:
  `[cascade] casters:` (draws seen / duplicates / alpha / added, triangles, MB, builds with
  the instance and why not, captured pass GPU ms) and `[cascade] matrix of cascade k`
  (w column error, det, extents of every cascade, UBO origin vs camera in m: the
  inversion's premises). `sun: roof volumes TRUSTED` on toggle; `sun stats` ends with
  `roof volumes trusted (cascade casters) 1`.
- **Gate result 2026-10-29 (S7 Blushing Mermaid, static, FG 2, live A/B in one run,
  sup-cascade2/3/4-s7-fg2).** Sun stats per 600 frames: key off (roof volumes): game dark &
  RT lit 29-31% of traced px, kept by roofs 99%, mean brightening 0.0004-0.0005/px. Key on
  (index 2, alpha 1, origin 1): game dark & RT lit 0.42-0.47%, kept by roofs 0%, replaced
  (lit) 13-15% of those, mean brightening 0.0010/px; game lit & RT shadowed 0.95% -> 1.2%.
  Origin 0: 0.0296/px (the leak). Cost: scene build 1.42 -> 2.72 ms GPU, cascade pass +0.43
  ms; rendered rate unchanged (30.0 fps cap in unfocused runs). Pending: the user's visual
  check (sharper roof shadow edges; no light through cut roofs; solid foliage cutouts in
  shadows would be the artefact to look for).
- **Gate (first plan).** Interior with the roof cut away, sun through windows: live A/B of
  `rt_cascade_casters`. Pass: matrix line orthographic, origin ~0 m; builds with the
  instance ~100%; with the key on `kept by roofs` 0% and no new light under the roof
  (sun check: brightened px not above key-off); outdoors, mean brightening equal within
  noise (no acne: `game lit & RT shadowed` not up). Cost: the casters line's pass ms and
  the `[rt] scene build GPU` split lines (the BLAS build, TLAS rebuilt every frame).
- **Known limits.** One frame of lag for moving casters (a moving character's shadow is
  the union of two poses where not deduplicated). Roofs outside cascade k's extent are
  not captured, but are trusted: raise `rt_cascade_index` if the extent is smaller than
  the view. Alpha-tested cascade casters are left out. Legacy (non-split) builds get no
  instance. The layer code needs a game restart once; the keys are live.
- **Radiance cache (TRACK_C next, 7.4).** D-GI2 below fixes the design.

### 8.6 New effects

Each entry gives the ID, the data needed, the injection point, the cost, the priority
and the steps. Priority: P1 next, P2 after P1, P3 later, P4 not recommended.

#### D-LS: ray-traced local-light shadows (P1)
- **Status (2026-10-26): implemented, on by default (`rt_local_shadows=1`).** Results, choices
  and numbers in [section 9](#9-d-ls-ray-traced-local-light-shadows-implemented). The plan below is kept as written.
- **Data:** EN1 (lights, clusters, atlas), the G-buffer, the TLAS (+ alpha).
- **Injection:** the post-lighting hook. Per pixel, the exact delta
  `Σ_i c_i·(v_i − s_i)`:
  - `c_i` is the unshadowed contribution (diffuse to b10, specular to b11);
  - `s_i` is the game's own shadow term (atlas PCF × fade × m8, or 1 when the
    light casts no shadow);
  - `v_i` is the ray-traced visibility.
- **Which lights:** `ls_mode=1` shadowed lights only (the game's intent, better
  shadows); `ls_mode=2` also unshadowed lights with radius > `ls_min_radius`
  (stops leaks); `ls_exclude_channel` for art lights.
- **Sampling:** v1 is deterministic, 1 ray to the light centre per light (hard).
  v2 samples the sphere by solid angle, using the radius from `_m5` if it encodes a
  source size (to verify), with 1-2 rays per pixel chosen by RIS over the cluster's
  lights weighted by `c_i`. The estimate is `Σ c_i s_i` (exact, subtracted) plus
  `c_j v_j / p_j` (stochastic, ≥ 0). RR denoises; with `rr=0`, SIGMA.
- **Cost:** v1 about 0.05 ms per light-per-pixel on average (measure the light
  count first); v2 0.25-0.4 ms.
- **Steps:**
  1. EN1, with its validation (Σ of lights matches the game's buffers).
  2. Debug view: per-pixel light count, and shadowed vs unshadowed lights.
  3. v1 deterministic, for shadowed lights only. A/B against the atlas in a
     torch-lit interior.
  4. `ls_mode=2`. A/B for light leaks under floors and through walls.
  5. v2 stochastic plus RR.
  6. Validation, motion run, cost table. Then default-on decision.

#### D-DI: ReSTIR DI over the local lights (P3)
- **Data:** as D-LS, plus emissive triangles (D-EM b) if they ever exist.
- **Injection:** the sampler inside D-LS v2; the composition is unchanged.
- **Cost:** +0.15-0.25 ms (temporal and spatial reuse, 1 shadow ray).
- **Steps:**
  1. Fetch RTXDI (not in `external/`).
  2. Initial RIS over the cluster lights.
  3. Temporal reuse with randomised neighbours, then spatial reuse with permutation
     sampling (the RR guide's rule, QUALITY R1).
  4. Visibility reuse.
  5. Compare its variance with D-LS v2 at equal cost. Keep it only if it wins in
     scenes with more than 8 lights per pixel.

#### D-RF: ray-traced reflections (P1)
- **Status (2026-10-26): tiers (a) + (c) implemented with D-EN4 and the RR hit distance.**
  Results in [section 10](#10-d-rf-ray-traced-reflections-implemented-tiers-a-and-c). The plan below is kept as written.
- **Data:**
  - G-buffer roughness (after the game's specular AA: `sqrt(r² + min(var, 0.18))`,
    then `r ≥ 0.09`), F0 and metalness.
  - Hit radiance in three tiers:
    - (a) on screen with matching depth: `prevRadiance`, reprojected (QUALITY Q1);
    - (b) off screen but in the TLAS: EN2 albedo × (sun with an RT shadow ray +
      EN1 lights + sky irradiance b34) + emissive 0;
    - (c) miss, or a cascade-only hit: the game's probe.
- **Injection:** exact delta on the specular ambient (EN4):
  `b11 += (L_rt · DFG(F0, NoV, r) · SO − probeSpec_game)`.
  It is blended toward the probe with a confidence weight: tier (a) 1, tier (b)
  `rf_offscreen_w`, tier (c) 0. The weight also fades to 0 at roughness
  `rf_max_rough` (0.6).
- **Sampling:**
  - 1 ray per pixel, GGX bounded-VNDF sampling, for pixels with r < `rf_max_rough`;
  - mirror (r < 0.1) gets a single deterministic ray;
  - RR gets `DLSSD.SpecularHitDistance` (FP16_MAX for misses) plus the
    world-to-view and view-to-clip matrices (QUALITY Q11).
  - With `rr=0`: NRD REBLUR specular.
- **Cost:**
  - about 1.4 M rays (≈ 50% of pixels) at 0.15-0.3 ms;
  - tier (b) shading 0.2-0.4 ms;
  - EN4 0.1 ms;
  - total **0.5-1.0 ms**.
- **Steps:**
  1. QUALITY Q11, the RR world-space normals and matrices.
  2. EN4, with its validation.
  3. Tiers (a) and (c) only: on-screen reflections with probe fallback. This is
     already "SSR quality with RT accuracy". Debug view: tier per pixel.
  4. Hit-distance output to RR. A/B on a wet or polished floor scene and on metal.
  5. EN2 + EN1, then tier (b). Measure the tier fractions with `rt_stats`.
  6. EN3 retention, which shrinks tier (c).
  7. Then specular occlusion from the same rays replaces Lagarde's for those pixels.

#### D-GI2: multi-bounce GI with a screen-seeded SHaRC cache (P2)
- **Data:** TLAS, EN2, EN1, the sky b34, absolute world (`+ _m6`) for hashing.
  `external/SHARC` (needs `-enable-16bit-types` for DXC).
- **Design (screen-seeded):**
  - **Update** pass: about 1/16 of pixels trace paths with 2-3 bounces. At each hit:
    if it is on screen with matching depth, its radiance is the game's own lit
    diffuse (exact, with all the game's lights and baked probes) and the path
    terminates; otherwise it is shaded as D-RF tier (b) and the path continues.
  - **Miss** rule: a miss *indoors* (or anywhere until EN3 closure) returns the
    game's diffuse probe irradiance at the last vertex, never the open sky. Roofs
    are cut away.
  - **Resolve:** the SHaRC accumulation, with a 32-64 frame cap and stale eviction.
  - **Query:** the existing GI rays (radius raised from 1 m to `gi_rt_radius` 8-30 m)
    read the cache at off-screen hits instead of fallback 0.
- **Injection:** unchanged (AO fold + bounce add), until EN4 allows an exact delta on
  the diffuse ambient.
- **Cost:** update 0.1-0.2 ms, resolve 0.05 ms, longer query rays +0.2 ms;
  **0.4-0.6 ms**, 160 MiB.
- **Steps:**
  1. EN2, EN1.
  2. SHaRC update/resolve on our own passes; hash-grid debug view.
  3. Query in `gi_trace_rt` for hidden and off-screen hits (replaces
     `gi_rt_fallback`).
  4. Raise the radius; A/B for colour bleeding and interior leaks.
  5. EN3 closure, then misses use the sky irradiance outdoors only.

#### D-EM: emissive surfaces (a: P1-small, b: P4)
- **(a) Emissive in the bounce.**
  - Find the pre-lighting additive target: B10G11R11 + D24S8 at render resolution,
    the passes before the tiled lighting (units 78-85), and the third colour input
    of compose `5c3fdabc`.
  - Add it to `prevRadiance` (it is available during the lighting).
  - Verify which materials write there (glowing glyphs? lava? or only sky/FX), and
    exclude the sky (depth = 0).
  - Cost 0.02 ms.
- **(b) Emissive triangles as lights** (RTXDI). This needs per-draw emissive
  detection and radiance. Not recommended until (a) shows that emissive
  surfaces are common.

#### D-TR: ray-traced thickness for translucency (P3)
- **Data:** a mask of SSS/foliage pixels (to find: G-buffer flag bits in RT2.w, the
  stencil, or the SSS profile index); TLAS.
- **Injection:** a delta into b10 before the SSS passes. A sun and light
  transmission term `c·albedo·exp(−σ·thickness)`, with the thickness from one
  ray along −N, inside the object.
- **Cost:** 0.05-0.1 ms.
- **Steps:**
  1. Find the mask.
  2. Thickness debug view.
  3. Back-lit leaves and ears A/B.
  4. Set the RR SSS guides (QUALITY Q12) first.

#### D-RFR: ray-traced refraction for glass and water (P4)
- **Data:** XFB on the forward pass (unit 125) for the refractive surfaces; their
  IOR and tint from the forward FS.
- **Injection:** patch the forward FS families' refraction sample (the unit-124
  copy).
- Deferred. RR's guide warns that refraction resampling breaks its sample
  independence.

#### D-VOL: RT sun visibility for the volumetrics (P3)
- **Data:** cascade-caster capture (6.7 item 2) for the sun-view geometry; the
  froxel grid of `ca8adce7`.
- **Injection:** a per-froxel sun visibility volume (1 ray per froxel), read by a
  patched `ca8adce7` in place of its cascade compare.
- **Cost:** about 0.1 ms.
- Worth it only if cascade-resolution aliasing in the shafts is visible in A/B.

#### D-PT: path-traced reference mode (P4, a tool, not an effect)
- A debug mode accumulating 1 path per pixel per frame over a static camera.
  - The primary hit comes from the G-buffer; secondary hits use EN2 materials,
    EN1 lights with RT shadows, and the sky.
- It writes to `rt_debug` only and replaces nothing.
- Purpose: a "how wrong is the hybrid" reference *within the captured geometry*,
  for the A/B metrics of QUALITY R6.
- Infeasible as a product for these reasons:
  - **Geometry:** culled and cut-away surfaces are not in any capture (even with
    EN3, interiors are closed only where cascades reach).
  - **Materials:** only the primary hit's material is exact.
  - **Lights:** the list is view-culled; emissive materials are unknown.
  - **Look:** the game's look depends on baked probes and deliberately unshadowed
    fill lights.
  - **Cost:** 5-8 ms with RR.

### 8.7 Ranking by value/effort

| Rank | Item | Value | Effort | Why |
|---|---|---|---|---|
| 0 | QUALITY P1 fixes (Q1, Q2, Q4, Q11) | high | low | they make every later effect correct and cheaper to denoise |
| 1 | Soft sun shadows (planned, 8.5) | medium | **low** | data complete (angular radius known), 0.1 ms, RR denoises |
| 2 | D-LS local-light shadows | **high** | medium | fixes BG3's most visible lighting flaw (leaks from unshadowed lights); all data readable |
| 3 | D-RF reflections, tiers (a)+(c) | **very high** | medium | BG3 has no SSR at all; the on-screen tier needs no new data |
| 4 | D-EM (a) emissive in the bounce | low-medium | **very low** | one target capture |
| 5 | Cascade-caster capture (planned) + EN3 retention | enabler | medium | closes interiors for GI, adds off-screen casters, shrinks D-RF tier (c) |
| 6 | D-RF tier (b) + EN2 | high | high | off-screen reflections |
| 7 | D-GI2 screen-seeded SHaRC | medium-high | high | room-scale GI; needs EN2 and closure to avoid leaks |
| 8 | D-DI ReSTIR DI | medium | medium | only after D-LS, and only with many lights |
| 9 | D-TR, D-VOL | low-medium | medium | niche |
| 10 | D-RFR, transparents/particles | low | high | forward-pass capture and shader patching |
| — | D-PT | reference only | medium | not a product |

## 9. D-LS: ray-traced local-light shadows (implemented)

Status (2026-10-26): **on by default** (`rt_local_shadows=1`, live; needs `rt=1`).
Code: `shaders/gi/rt_local_shadows.hlsl`, `shaders/rt/bg3_lights.hlsli` (the light list
and the game's per-light shading, D-EN1), `shaders/gi/rt_local_shadows_prep.hlsl`
(validation only), `core/gi.cpp` (snapshot, bindings, pass, timing, statistics).
Tools: `scripts/live_phases.py` (live setting phases during one auto-run),
`scripts/ab_compare.py` (A/B stills and crops from two captures).
Measured in the Steel Watch Foundry (the auto-run save), RTX 5090, 2560x1067.

### 9.1 The light list (D-EN1)

At the first tiled-lighting dispatch the core snapshots, with `query_binding`, the
lighting's own set 1 b0 (flags), b13 (global light scale), b23 (froxel headers),
b24 (index list), b25 (light array), b39 (lighting constants), b40 (shadow views),
b41 (shadow atlas) and set 0 b2 (the froxel depth scale), and re-binds them read-only
to the D-LS pass (push-descriptor bindings 21-29). Every offset comes from the SPIR-V
`Offset`/`ArrayStride` decorations of `f469db78` (listed in `bg3_lights.hlsli`).
If any of them is missing, the pass is skipped and the log says so once (R8).

| What | Value in the foundry |
|---|---|
| Light array b25 | 4096 slots of 80 bytes (327680 B); shadow views b40: 4096 x 96 B |
| Froxels b23 / index list b24 | 397440 B (24840 froxels) / 6.36 MB |
| Shadow atlas b41 | 8192x8192 R16_UNORM, SHADER_READ_ONLY |
| Lights listed in the pixel's froxel (points + spots) | **5.61 per pixel** |
| Lights the game actually evaluates (channel, range, cone, N·L > 0) | **2.31 per pixel**; 89% of pixels get at least one |
| ... of which have a game shadow map (`m7` bit 0) | **0.00**: in this scene no point/spot light casts a shadow |
| Projector lights listed (b29, third loop of `f469db78`) | 1.02 per pixel (not handled, see 9.7) |
| `set1/b0 m0.y`, the AO factor on direct light (QUALITY Q3) | **0.5** in gameplay (1.0 on the loading screen); `m0.x = m0.w = 1` |
| `b13[0]`, the global light scale (lights with `m3 < 0`) | 0.000407 |

So Q3 is answered: BG3 multiplies direct local and sun light by `mix(1, AO, 0.5)`.
Our AO fold (RT AO / RT GI visibility into `b6.x`) therefore also darkens direct light
by half its occlusion. That is the game's own formula, and it is kept.

### 9.2 What the pass does

Full resolution, after the lighting and **before** the GI composite (so next frame's
bounce source, `prevRadiance`, already contains the shadows), SSS and fog.

1. It rebuilds the surface exactly as `f469db78` does: `P = ray(uv)·depth`, the view
   normal rotated by `_m6` (not renormalised), F0 `mix(0.16·spec², base, metal)`, the
   diffuse colour, the light-channel bits, roughness `max(0.09, r)` with the game's
   specular AA (`sqrt(r² + min(var, 0.18))`; the 2x2 quad's normal differences are read
   from the neighbours instead of quad shuffles), N·V and the IGN of the atlas PCF.
2. It walks the pixel's froxel (`log2(depth·_m4)·_m21 + _m22`, tiles of `_m25`) through
   the point lights, then the spot lights, and recomputes each one's **unshadowed**
   diffuse and specular `c_i` with the game's terms: the range test, the
   `pow(sat(1 − x²), m4)` falloff with both bias models, the spot cone², `1/4π` (points)
   and `1/π` (spots), `b13[0]` for `m3 < 0`, GGX D, the approximate height-correlated
   Smith visibility, Schlick F, and renormalised Burley × `mix(1, 0.662, a)`.
3. For each light the game left **unshadowed** (`m7` bit 0 clear), it traces one ray
   (alpha-tested TLAS, `rt_occluded`) and adds the exact delta
   `out += c_i·(v_i − 1)·mix(1, AO, m0.y)` into b10 (diffuse) and b11 (specular, with the
   game's Lagarde specular occlusion in place of AO). Since `v_i ≤ 1`, a light can only
   lose what the game gave it. The result is clamped at 0 only against the
   transmittance, which the delta does not replicate (it is 1 indoors, see 9.3).
4. Non-finite guard on both writes (R5).

**Lights that have a game shadow map** (`ls_shadowed`, default 0 = *skip*): their
contribution is left exactly as the game computed it. Reason: the atlas is rendered
from the light and contains casters the TLAS does not have (culled walls, cut-away
roofs, everything off screen). Replacing it with the traced visibility could
*un*-shadow those, which is worse than the low-resolution penumbra it would fix.
- `ls_shadowed=1` replaces the atlas by the traced visibility (`c·(v − s)`, with `s`
  the game's own PCF term replicated exactly: tetrahedral face, 12-tap Vogel disc
  rotated by IGN, `textureGather` emulated with loads, camera-distance fade × `m8`).
- `ls_shadowed=2` takes `min(v, s)`.

Both are A/B options only. Neither could be judged here, because no light in the
foundry has a shadow map.

**Sampling (R1).**
- With RR (`rr=1`; `ls_soft=-1` follows it) each traced light gets one ray to a
  uniform point of its sphere by solid angle (radius `ls_source_radius`, 0.05 world
  units: BG3's lights have no source size, so this is an artistic value). White noise
  from `pcg3d(pixel, frame·1024 + light index)`. RR denoises the penumbra; nothing of
  ours filters it (R2).
- Without RR there is no denoiser, so the ray goes to the light centre (hard shadows,
  no noise).
- `ls_samples=1|2` instead traces 1-2 rays per pixel to lights chosen by RIS over the
  unshadowed luminance (unbiased; cheaper, noisier). It is an option, not the default,
  because 2.31 rays per pixel are affordable.

Rays start at `P + N·dist·rt_bias` and stop `ls_clear` (0.2) short of the light, so a
lamp's own housing does not shadow it completely:

| `ls_clear` | occluded light evaluations per pixel (of 2.31) |
|---|---|
| 0.05 | 1.00 |
| **0.2** (default) | **0.80** |
| 0.5 | 0.77 |

Most of the step from 0.05 to 0.2 is the fixture geometry around the light. Beyond
0.2 almost nothing changes, so the occlusion that remains is the scene's.

### 9.3 Validation of the recompute

`ls_debug=4` runs `rt_local_shadows_prep.hlsl` before the lighting. It sets the game's
AO to 0 (no ambient; direct light keeps `1 − m0.y` of itself, which the recompute
applies too) and its sun mask to 0. The game's diffuse + specular then hold only the
direct local lights, plus the terms the recompute does not model (projector lights,
the atmosphere's in-scatter). With `ls_stats=1` the core fits
`game = k·recomputed + r` per pixel, over the pixels whose froxel lists no projector
light (counters in `rtStats`, 120-frame windows):

| | AO 0 (`ls_debug=4`) |
|---|---|
| Fit over 1.20M lit pixels | **game = 1.07 × recomputed + 0.0134** |
| Mean recomputed / game | 0.0093 / 0.0234 |
| Game where no local light reaches (0.15M px) | 0.0181 |
| Recompute above the game (luminance) | 0.004% of the game's; 0.02% of pixels |

- The residual is an additive term of about the same size where no local light reaches
  at all (0.018) as under the lights (intercept 0.013).
- The recompute is never above the game's output. An error in a factor, the falloff or
  the BRDF would produce that somewhere.
- The slope of 1.07 is within what that spatially varying additive term can bias a
  least-squares fit by.
- The exact < 1% check of D-EN1 needs that residual removed too (the in-scatter
  replicated, or the projector lights added to the recompute). That is not done.
- `ls_debug=5` shows the recomputed light; `ls_debug=6` is the same check with AO = 1.

### 9.4 Results (A/B, same view, one run, live toggle)

`auto-run.ps1 -Traces 3` with `scripts/live_phases.py`: trace 1 on, trace 2 off (set live
right after trace 1), trace 3 `ls_debug=2` (the light removed, ×4); then `scripts/ab_compare.py`.

- [ls_foundry_off.png](img/ls_foundry_off.png) / [ls_foundry_on.png](img/ls_foundry_on.png):
  the presented frames (after RR, post and exposure), downscaled.
- [ls_foundry_removed.png](img/ls_foundry_removed.png): the light D-LS removes (×4,
  written over the lit diffuse, so it also passes through SSS and fog). This is where
  light previously leaked through geometry:
  - the floor behind the curved beam (right);
  - the beam's back face and the area above its lamp;
  - the walkway under the pipes and railings;
  - the machinery at the left;
  - the character's shadow wedge across the walkway, from the light above it.
- [ls_foundry_crops.png](img/ls_foundry_crops.png): three 1:1 crops of the 3840x1600
  frame, rows off / on / removed.

| Metric | Value |
|---|---|
| Light removed (unshadowed lights only) | **7.2% of the lit diffuse + specular luminance** (mean 0.0046 of 0.063) |
| Occluded evaluations | 0.80 of 2.31 per pixel (35%) |
| Mean luminance of the presented frame | −2.6% (exposure adapts partly); HDR compose output −2.3 to −2.7% |
| Non-finite values (every dumped image of all three traces) | **0** |

The render-resolution HDR images (compose, before RR) are not usable for a diff: the
per-frame jitter and the 1-spp noise of RR mode dominate it. The presented frames and
the debug view are the evidence.

Not done from R6:
- the docks, foliage and night-interior scenes. The `-Move` pan reached the docks once;
  there 0.07 lights are listed per pixel and none is evaluated, so the pass costs
  0.068 ms and changes nothing;
- FLIP against an accumulated reference;
- the temporal-stability metric.

### 9.5 Cost (GPU timestamps around the pass, medians of 300 frames)

| Configuration | Foundry, static |
|---|---|
| **Default: every unshadowed light, 1 ray each, soft (RR)** | **0.345 ms** (p10 0.342, p90 0.348) |
| Same, hard (`ls_soft=0`, the `rr=0` path) | 0.337 ms |
| RIS, 1 ray per pixel (`ls_samples=1`) | 0.219 ms |
| `-Validation`, default | 0.325-0.329 ms |
| Docks (no lights evaluated) / loading screen | 0.068 / 0.12 ms |
| `ls_stats=1` (wave-reduced atomics; measurement only) | 4-5 ms |

The budget (QUALITY R7) is 0.5 ms for local-light shadows: met. All rt passes together
(sun shadows + RT GI 0.26-0.28 ms, D-LS 0.35 ms) stay well inside the 2.5 ms Track D total.

These are the numbers when D-LS shipped. Later merges (per-class terms, ray origins, the
BVH split, per-light data) took it to 0.59-0.77 ms; 9.9 has the profile and the optimised
pass (0.39-0.42 ms in the foundry, same image).

### 9.6 Validation layer and robustness (R5, R8)

- `auto-run.ps1 -Validation`, `rt_local_shadows=0` against `1` with every mode toggled
  live in the lit foundry (`ls_stats`, debug views 1-6 including the validation
  pre-pass, `ls_samples=2` with `ls_shadowed=1`, `ls_shadowed=2`, live off and on):
  **identical message IDs**, all the game's own (`09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, `CopyBuffer-00118`, `08737`, the
  headless `non-acquired-swapchain-image-used`). No core faults.

- The first on-run showed one new ID, `VUID-vkCmdDispatch-imageLayout-00344`. The
  validation pre-pass lived in the main shader, so the sun mask (binding 16) was
  statically used by the post pass while it was in SHADER_READ_ONLY. Fixed by moving
  the pre-pass into `rt_local_shadows_prep.hlsl`.
- `-Validation -Move`: the pan left the foundry for the docks (a large change of
  geometry and an outdoor view). No faults, no non-finite values, IDs as above.
- Missing light-list bindings, no TLAS or a stale one (more than one frame old): the
  pass does not run, and the game's lighting is untouched. It only ever removes light
  the game added this frame.
- `fg=2` (`-RealPresent`, DLSS-G 2x) with D-LS on: DLSS-G status 0x0, 2 presents per
  rendered frame, 0 Streamline API errors; the pass costs the same (0.331 ms).

### 9.7 What does not work / next steps

- **Projector lights** (b29, about 1 per pixel here) are not shadowed. Their loop has
  its own falloff and cookie terms; adding them follows the same pattern.
- **Shadowed lights** are skipped by default. `ls_shadowed=1/2` are untested in a scene
  that has them (a torch-lit interior with shadow-casting lights is needed, R6).
- **Art intent.** Every unshadowed light is shadowed, including fill lights that may be
  meant to leak and the light that follows the active character (its wedge-shaped
  shadow on the floor). `ls_min_radius` and `ls_strength` exist as controls. A
  per-channel exclusion was not needed here. **Resolved for the character light
  in 9.8.**
- **Off-screen casters** are missing from the TLAS, so leaks through geometry the camera
  does not see remain (cascade-caster capture, 6.7 item 2, and D-EN3 retention).
- **Exactness.** The transmittance and in-scatter are not replicated (they are why 9.3
  stops at a fit), and the delta is clamped at 0 for that reason.
- **Light source radius.** BG3 gives none; 0.05 is a guess, to tune by eye.
  **Now per fixture class (9.8)**; still not a measured size.
- **`rr=0`**: hard shadows only (no SIGMA).

Settings (`bg3rtx.ini`, all live): `rt_local_shadows`, `ls_shadowed`, `ls_source_radius`,
`ls_soft`, `ls_clear`, `ls_min_radius`, `ls_samples`, `ls_strength`, `ls_debug`
(1 lights per pixel, 2 light removed ×4, 3 traced visibility, 4/5/6 validation),
`ls_stats`. The log reports `ls: first frame ...` (the bindings),
`ls: local-light shadow pass GPU median ...` and, with `ls_stats=1`, `ls stats` and
`ls validation fit`.

### 9.8 Per-light data: the Script Extender bridge and the asset database (2026-10-26)

Status: **on by default** (`ls_lights=1`, `ls_owners=15`, live).

Code:
- `core/ls_lights.cpp` (upload);
- `shaders/gi/rt_local_shadows_match.hlsl` (join);
- `rt_local_shadows.hlsl` (use);
- the data comes from `Bg3AssetDbApi` v2 (`layer/assetdb.cpp`, `layer/sebridge.cpp`;
  docs/SE_BRIDGE.md).

**What it does.**
1. **Upload.** Each frame the core uploads the runtime lights within
   `ls_lights_radius` (80 m) of the camera into a host-visible ring: up to 1,024
   records of 48 bytes. These are the bridge's lights merged with the database's placed
   lights, or the database alone without the bridge.
   - Each record holds the absolute position, the radius, the source radius, a match
     tolerance, the flags, owner, type and channel.
   - The list is re-queried on each new export, else every 0.25 s (was every 8 frames).
   - **Camera position.** The join pass writes it (set 0 b2 `_m6`) back into the ring.
     The core reads it kRing frames later to centre the next query. Before the first
     readback, the controlled character's position from the bridge is used.
2. **Join pass** (before D-LS, inside its timestamps). One thread per light array
   slot: 4096 threads, with the records staged through group-shared memory.
   - A slot matches the nearest record within its tolerance whose radius agrees with
     1/`m0.w`: 0.5 mm + 4 relative ulp for placed lights, 2% + 1 cm (not derived, logged)
     for moving ones. *Updated 2026-10-27, derivations in SE_BRIDGE.md "Join tolerances".*
   - Tolerance (2026-10-27): √3 × (4 ulp at the position + 0.05 mm) + `MovementAmount` for
     placed lights; that plus `ls_dyn_speed` (8 m/s) × the measured export age (≈ 3-4 m)
     for character lights and every other light the bridge reports. (Before: 5 cm, 4 m and
     `ls_dyn_tol` 1.5 m, replaced; the text below describes the lights it applies to)
     (items, and visual lights whose owner was not found, such as the VFX lights of
     carried weapons: `VFX_LightSources_WPN_DippedInFire`, owner 0). They move between
     the 4 Hz exports. Until 2026-10-27 items had 0.5 m and the rest 5 cm, so a carried
     weapon's light went unmatched while its carrier walked and lost its fixture radius
     (the ray's end clearance at the hand).
   - The result goes to a 4096×1 RGBA32UI image: source radius, flags, owner | type,
     and the record index.
3. **D-LS** reads its slot for every light it would trace:
   - the light's **source radius** replaces `ls_source_radius` for its soft-shadow
     sphere (the fixture class, SE_BRIDGE.md "Source radius");
   - a light whose **owner** bit is clear in `ls_owners` is left exactly as the game lit
     it;
   - unmatched lights keep `ls_source_radius` and are traced as before.

**Art intent: the character light (the 9.7 question).**
- **What the light is.** The light that follows the active character is the darkvision
  pair:
  - `Light_Player_Darkvision_Spot_Drow`: spot, radius 24, intensity 4000;
  - `Light_Player_Darkvision_Point_Default`: point, radius 18, intensity 0, `FillLight`.

  Both sit 2 m above the controlled character.
- **The game's own flags.** Their templates set `Shadow = false` and
  `CastShadow = false`. The game never shadows them.
- **What it is for.** It is a readability light: it keeps the controlled character and
  the ground around them visible in dark areas.
- **What tracing did.** Traced, its own carrier occludes it. The character's head and
  shoulders shadow the body, and a wedge falls on the floor. That darkens the character
  the light exists to show.
- **Decision.**
  - Character-owned lights are **not shadowed** by default: `ls_owners=15` (unknown,
    placed, item, scenery).
  - This follows the game's own shadow-caster flag for this light.
  - `ls_owners=31` traces them as before.
  - Placed, item and scenery lights are still shadowed even though most of them also
    have `Shadow = false`. Their leaks through walls are what D-LS exists to fix (9.4),
    and the game's flag there means "no shadow map", not "meant to leak".

A/B, same view, one run, live switch (`scripts/live_phases.py`, `scripts/ab_compare.py`):
- [sebridge_charlight_crop.png](img/sebridge_charlight_crop.png): 1:1 crop of the
  presented frame. Left is `ls_owners=31` (character light traced), middle the default,
  right |diff| ×4.
  - Traced, the character is visibly darker: the light above them is blocked by their
    own head and hat.
  - The default restores the game's look of the character.
  - The full frames are [shadowed](img/sebridge_charlight_shadowed.png) and
    [default](img/sebridge_charlight_default.png).
  - Mean luminance +0.15%; 5.8% of pixels change by more than 2/255, almost all of them
    on the character.
- [sebridge_owner_view.png](img/sebridge_owner_view.png): `ls_debug=9`. The evaluated
  light, luminance-weighted, coloured by owner:
  - white: placed lights;
  - orange: the glasshouse lamp items in the background right;
  - magenta: around the character (the darkvision light);
  - red: unmatched (none here).
- [sebridge_radius_crop.png](img/sebridge_radius_crop.png): per-light data off
  (`ls_lights=0`, 0.05 m for every light) against on, at the machinery on the left.
  - The changes are small penumbra differences on rims lit by fill lights (0.6 m
    instead of 0.05 m).
  - Over the frame, 8.3% of pixels change by more than 2/255, and the character
    accounts for most of it.

**Numbers** (foundry, `ls_stats=1`, 120-frame windows):

| | Value |
|---|---|
| Listed point/spot lights covered (join check) | database 26/40 (65%) → bridge + database 40/40 (100%) |
| D-LS light evaluations with per-light data | 100% matched (2.49 per px) |
| Evaluations not traced by owner (the character light) | 16.6%: it lights most of the walkway |
| Light removed by D-LS, all owners / default | 16.5% / 15.9% of the lit luminance |
| Non-finite values (every dumped image, four traces) | 0 |

**Cost.** GPU timestamps around the join pass and D-LS together, medians of 300 frames,
foundry, static, one run, live phases:

| Configuration | ms |
|---|---|
| per-light data off (`ls_lights=0`: the 9.5 path) | 0.587-0.590 |
| **bridge + database (default)** | **0.628** |
| database only (`se_bridge=0`) | 0.68 |

- **Bridge + database: +0.04 ms.** That covers the join pass, the larger penumbrae of
  fill lights, and the character light no longer being traced.
- **Database only: +0.09 ms.** The character light is still traced, and fill lights get
  the larger radius.
- **The base itself has risen** from 9.5's 0.345 ms to 0.59 ms on this branch (after
  the RR and BVH-split merges). That is not caused by this change. See 9.9 for where the
  time went and the optimised pass.

**Validation.**
- `-Validation` on the foundry, against a baseline run of main without the bridge, gives
  the same 7 message IDs (`10788`, `09600`, `StorageImage-FormatMismatch`,
  `OutputNotWritten`, `non-acquired-swapchain-image-used`, `CopyBuffer-00118`, `08737`).
  No new ID.
  - After merging main's RR post chain, a re-run adds `vkDestroyImage-image-01000`.
  - It comes with the `[rrpost] the game destroys an image our post steps read` log
    line, so it is the RR branch's, not this change's. That was not confirmed with a
    validation run of main alone.
- Without the database and the bridge (no API v2, `ls_lights=0`, or a failed
  allocation), the join pass does not run, and D-LS is unchanged from 9.2.

**Not done.**
- Projector (box) lights: the character's rim and fill lights are boxes, and D-LS still
  ignores box lights.
- A measured fixture size from the TLAS.
- Other scenes with the bridge.

### 9.9 Cost: where the time went, and the optimised pass (2026-10-26)

Status: **on by default**, no setting to choose. The output is **bit-identical** to the
pass before (proof below). Code:
- `shaders/gi/rt_local_shadows.hlsl`: the default-settings variant (`LS_FULL=0`) in three
  phases, and the old single loop as `LS_FULL=1` (`rt_local_shadows_full.hlsl`), which
  `core/gi.cpp` runs only for `ls_samples`, `ls_shadowed`, `ls_debug` or `ls_stats`;
- `shaders/gi/rt_local_shadows_match.hlsl`: the join keeps unchanged slots;
- A/B tooling: `shaders/gi/ls_ref/` (the pass of `f5e9eab`, frozen by
  `tools/lsref/make_ls_ref.py`), `rt_local_shadows_ref.hlsl` (`ls_impl=0` runs it),
  `rt_local_shadows_check.hlsl` (`ls_check=1/2`), `tools/pipeline_stats.py` (driver
  register counts, no game needed).

All numbers: RTX 5090, 2560x1067, GPU timestamps around the join and the shadow pass
(the core now logs the join separately), medians of 300 frames, `-Background` runs with
live setting phases (`scripts/live_phases.py`), the first window after each switch
dropped. The GPU was shared with the user's desktop (another game at times), and the
foundry ran at 24 fps with `rt_bvh_split=1`: absolute values drift by about ±10% between
runs (the old pass measured 0.55-0.77 ms in the same save), so every comparison below is
within one run.

**Where the time went (old pass, foundry, one run; each switch from the default; shared GPU):**

| Configuration | ms | vs default |
|---|---|---|
| default | 0.77 | |
| no rays at all (`ls_min_radius=1e5`) | 0.37 | rays −0.40 |
| `rt_origin=0` | 0.70 | −0.065 |
| `ao_direct=0` (no sun recompute, no AO give-back) | 0.70 | −0.07 |
| `rt_bvh_split=0` (the frame then ran at 130 fps) | 0.69 | −0.08 |
| `ls_soft=0` | 0.73 | −0.035 |
| `ls_lights=0` (no join; the character light traced again) | 0.77 | 0.00 (join −0.011, character light +0.01..0.07) |
| `mat_classes=0` | 0.77 | **0.00** |
| `ls_owners=31` (character light traced) | 0.84 | +0.07 |
| `ls_samples=1` | 0.57 | |
| `ls_lights`, `mat_classes`, `rt_origin`, `ao_direct` all 0 | 0.64-0.66 | −0.12 |
| ... and `rt_bvh_split=0` | 0.59 | |
| `rt_alpha_test=0` (another run, default 0.59) | 0.535 | −0.06 |

- **The per-feature switches explain only part of it.** `mat_classes=0` costs nothing to
  turn off, although the per-class terms were the largest audit change: the price of
  the per-class code is paid in **registers**, whether a class is used or not.
- **Registers** (`tools/pipeline_stats.py`, the driver's own count): the old pass used
  **252 registers and 16 bytes of local memory** per thread, so 8 warps per SM (of 48).
  The ray queries, the alpha test at their candidates and the generic per-class BRDF
  were all live in one loop. A pure ray pass (`rt_shadow.hlsl`) needs 72.
- The rays themselves (about 0.25 ms after the optimisation) did not get cheaper: 2.08
  per pixel, the ray origins (`rt_origin`) and the BVH split's instance lookups at alpha
  candidates make them longer, and exactness forbids shortening them.

**What changed (all exact):**
1. **Two variants.** Every debug view, statistic, the RIS reservoirs and the
   shadow-map modes live only in `LS_FULL=1`. Nothing but the four sums the output needs
   is live in the default variant's loops.
2. **Three phases per 32 list entries** instead of one loop:
   1. *Which entries are traced*: the trace conditions first (no game shadow map,
      `ls_min_radius`, owner from the join), then `bg3_light_eval_mat`'s own acceptance
      tests (channel, range, cone, N·L or the class's transmission/hair test), from the
      few surface terms they need. The BRDF is no longer evaluated for the lights that
      are not traced (the character light alone was 16.6% of the evaluations).
   2. *The rays*, the k-th traced light of every lane together, with only the ray
      inputs live.
   3. *The rest of the surface* (reloaded at an index the compiler cannot prove equal to
      the pixel, so it is not kept across the rays), each traced light's exact
      unshadowed diffuse and specular, and the sums in list order with the old
      expressions.

   Result: **96 registers, no local memory, 21 warps per SM.**
3. **The sun is not recomputed where it cannot matter.** With `ao_direct=1` it only
   enters as `(1 − fdif)·sun`; where both AO factors are exactly 1 that is 0. Where the
   sun mask is 0 the cloud-shadow lookups are skipped (0 × a finite factor).
4. **The specular-AA neighbours**: the pixel's own normal is reused (2 normal loads instead
   of 4; the clamp at the odd last row behaves as before). `gAux` is read only in mode 2.
5. **The join keeps unchanged slots.** Each slot caches the inputs of its result (absolute
   position and radius bits, and a generation the core bumps when the uploaded records
   change); a group whose slots are all unchanged skips the search. 0.011 → 0.005-0.007 ms.

**Tried and dropped (measured, not kept):**
- Rays to the wave's union of list positions (rays to the same light together): +0.007 ms.
- The rays of the first 32 entries as their own dispatch (72 registers there): +0.07 ms;
  the second pass reads the G-buffer and the list again.
- No ray for lights of colour 0: exact, but no such light is traced here; no change.
- Skipping rays to lights whose unshadowed term stays below 1/256 or 1/64 of the
  pixel's (not exact): no measurable gain, and the extra evaluation raised the default
  variant to 125 registers (+0.03 ms).

**Not tried, or not applicable:**
- Wave-scalarised light loops and packed per-light caches. A wave's pixels mostly share one
  froxel list, the light records are read from L1, and the no-ray time is now 0.14 ms in
  all; the registers were the limit, not these loads.
- Ray flags: `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` was already set, and `tmax`
  already stops `max(R, ls_clear)` short of the light. Back-face culling would change the
  image (back faces occlude too).

**Result (foundry, same runs, new / old; shared GPU):**

| Run | new | old (`ls_impl=0`) |
|---|---|---|
| A/B 2 (4 + 4 alternating phases) | 0.388-0.401 | 0.566-0.595 |
| A/B 6 | 0.409-0.417 | 0.59-0.65 |
| A/B 6, `rt_bvh_split=0` | 0.377 | 0.563 |
| no rays (`ls_min_radius=1e5`) | 0.143 | 0.372 |
| join alone | 0.005-0.007 | 0.011-0.013 |

- **−0.18 to −0.23 ms (−31 to −37%)** at the same image.
- **The 0.35 ms target is not reached** under these conditions: 0.39-0.42 ms against an
  old pass of 0.57-0.65 ms in the same runs. Scaled to the 0.59-0.63 ms the old pass logged
  when this work started, that is about 0.40 ms. The remaining time is the rays
  (≈0.25 ms) and the G-buffer traffic of a full-resolution pass (≈0.14 ms with no ray at
  all). Reaching 0.35 needs fewer or shorter rays, which changes the image (for example
  `ls_samples=1`, or a lower resolution for the visibility).
- Under the 0.5 ms budget (QUALITY R7) again.
- Per feature, new pass (A/B 6): `rt_origin` +0.045, `ao_direct` +0.026, `rt_alpha_test`
  +0.015, `ls_soft` +0.012, `mat_classes` 0, the join + owner skip −0.04 (`ls_lights=0`
  traces the character light), `ls_owners=31` +0.056.

**Other scenes** (shared GPU; `-Save`, same tooling: a check phase, then new and old alternating,
three phases each):

| Scene | new | old | change | `ls_check=1` |
|---|---|---|---|---|
| Steel Watch Foundry (default save) | 0.39-0.42 | 0.57-0.65 | −31 to −37% | 0 px differ |
| Grymforge (`Grymforge - 32h 59m`, torches and lava) | 0.71-0.73 | 0.97-0.98 | −26% | 0 px differ (2.29·10⁹ px) |
| Water Queens House | 0.114 | 0.169 | −33% | 0 px differ (6.23·10⁹ px) |

Grymforge is above the 0.5 ms budget in both: it has many more traced lights (torches
along every wall). That is a ray count, not a regression of this change.

**Image equality.**
- `ls_check=1` runs the frozen old pass and the live one on the same pixel, frame and
  inputs in one dispatch, compares the float outputs bit for bit, and logs the counts per
  120 frames (`ls check`). It also searches every join slot and compares the kept results.
- Foundry, default settings: **0 pixels differing in any bit** over every window of every
  run since the last change (for example 2.62·10⁹ pixel evaluations in A/B 3, 1.97·10⁹ in
  A/B 6), and **0 kept join slots differing** from a new search.
- Also 0 with `ls_owners=31`, `ao_direct=2`, `rt_origin=0`, `ls_soft=0`,
  `mat_classes=0`, and for the `LS_FULL` variant with `ls_samples=1` and `ls_debug=2`.
- `ls_check=2` compares the old pass with itself (a second evaluation the compiler
  cannot merge): 0 over 16 windows. So the inputs do not change under the pass, and 0
  means equal.
- The `LS_FULL` check showed one window with differences in three runs, each time the
  window in which a setting switched variant (`ls_samples` or `ls_check`); the following
  windows are 0.
- A first version was not exact: it wrote the `ao_direct=1` give-back as
  `dir·(1 − fdif)` with a constant 1, and 2.1% of pixels differed in the last bits
  (17 px per 120 frames above 1e-5 relative). Keeping the old expression
  `dir·(td_f − fdif)` made it exact. The check is what found it.

**Validation layer and robustness.**
- Baseline: `-Validation` on `f5e9eab` (the commit before this work), foundry. 8 message
  IDs, all seen before: `10788`, `09600`, `StorageImage-FormatMismatch`,
  `OutputNotWritten`, `non-acquired-swapchain-image-used`, `vkDestroyImage-image-01000`,
  `CopyBuffer-00118`, `08737`.
- `-Validation` with this work, after merging main (`c604311`), foundry. Live phases:
  check, self-check, the `LS_FULL` check with `ls_samples=2 ls_shadowed=1`,
  `ls_debug=4` with `ls_stats=1`, the old pass, `ls_lights=0`, pass off and on.
  - The same 8 IDs, plus **`VUID-vkCmdDispatch-format-07753`**.
  - Cause: when the join does not run (`ls_lights=0`, or no per-light data), binding 46
    was given the RGBA16F `gAux` view as a placeholder. The shaders declare it RGBA32UI
    and use it statically.
  - This is older than this work: the 9.8 code binds the same placeholder. The baseline
    never turned `ls_lights` off, so it did not show the ID.
  - Fixed: a 1×1 RGBA32UI placeholder (`g_ls_nodata`).
  - Not re-confirmed: two later validation runs stayed in the main menu. The save did not
    load under the validation layer with `-Save`, and the newest save had changed under
    `-continueGame`. The menu-only run shows the 8 baseline IDs and no 07753.
- In that run, `ls_check=1` still found **0 differing pixels** after the merge with main
  (6.55·10⁸ px), as did the `LS_FULL` check (9.8·10⁸ px). Kept join slots: 0 differ.
- `-Move` (pan from the foundry): 2 traces in motion, **0 non-finite values** in 183
  dumps. No `nvlddmkm` event during the run.
  - An earlier `-Move` attempt ended with the game exiting 5 s after its first trace,
    with `nvlddmkm` 153 events. One A/B run also ended early that way.
  - `nvlddmkm` 153 pairs are logged on this machine at almost every game exit, including
    runs of the baseline code and other branches, so the cause was not identified.
  - The repeat `-Move` run was clean.

Settings (all live): `ls_impl` (1 the optimised pass, 0 the frozen old one), `ls_check`
(0 off, 1 old against new, 2 old against itself; measurement only, about 1.5 ms).

### 9.10 Who casts and who receives (audit 2026-10-27)

Question: in the on/off compares (`captures/compare`), characters and the lights they carry
seem not to cast or receive RT shadows. Findings (offline):
- **Casters.** Every instance has mask 0xFF (`bvhsplit.cpp` 893/918/1189, `rtscene.cpp` 746)
  and every ray traces 0xFF (`rt_common.hlsli`), in both `rt_bvh_split` modes. The TLAS is the
  big depth prepass (`geocapture.cpp` 1108; passes under 64 draws are skipped, 1382), direct
  triangle-list draws only (0 dropped in the logs). Skinned characters are in it: Campsite
  `captures/20261027-062830` has 14 draws of the skinned VS `1cf36b97` in the prepass (82
  class-B draws in the G-buffer, 14-23 in each cascade). Not in it: forward/transparent
  geometry (water, glass, particles, VFX flames), decals, off-screen and cut-away geometry.
  No class is excluded as a caster, and no game "does not cast" flag is applied either.
- **Sun.** Characters cast traced sun shadows (Lower City compare, the NPC on the sand: a
  crisp traced silhouette inside the game's soft one). Outside `rt_sun_replace`'s trusted
  pixels the fold is `min(game, traced)` (`rt_shadow.hlsl` 168), and the game's cascades
  already hold the characters, so on/off differ only at the shadow's edges.
- **Local lights.** Not traced, by rule: lights with a game shadow map (`ls_shadowed=0`,
  `rt_local_shadows.hlsl` 212), owner "character" (`ls_owners=15`, 218: the darkvision pair,
  which the game flags `Shadow = CastShadow = false`, and every `Light_Player_*` template,
  including world-fixed `Light_Player_Fill_*` lights; `BootstrapClient.lua` 134), and all
  box/projector lights (the characters' rim and fill rigs). In dark interiors (Adamantine
  Forge) these are most of the light on and around the party, hence no character shadows.
  Carried weapon lights (`VFX_..._WPN_DippedInFire`, owner 0) are traced; their join tolerance
  was the defect fixed above.
- **Receivers.** The sun fold edits the mask b7 and D-LS edits b10/b11, both read only by
  the deferred lighting. Every G-buffer class receives except unlit (hair is deferred, class
  5). Forward-rendered surfaces (water, glass, particles, VFX) receive neither: they sample
  the game's cascades themselves and get no D-LS.

**Policy (R0, coordinator 2026-10-27), implemented:**
- **The owner is no reason to skip a light** (`ls_owners=31`). The only skip is art intent
  from the game's own flags (`ls_noshadow`, `rt_local_shadows.hlsl` `ls_skip_light`).
  - Data (asset DB, 19,572 placed lights): **98% of point lights and 70% of spots have
    neither `Shadow` nor `CastShadow`** (only 0.8% / 0.4% of all lights set `CastShadow`).
    So "skip whenever both are false" (`ls_noshadow=2`) would stop tracing almost every
    placed lamp and bring back the leaks D-LS fixes (9.4); in BG3 the flag means "no shadow
    map", not "meant to leak". `Light_Player_Fill_*` also have both false.
  - Default `ls_noshadow=1`: a light is skipped only when both flags are false **and** it is
    the darkvision light that follows the active character (`BG3_RTL_PLAYER_LIGHT`, now set
    for `Light_Player_Darkvision_*` only). `ls_noshadow=0` traces it too.
  - `Light_Player_Fill_*` are no longer character lights: `sebridge.cpp` re-tags them on
    parse (owner unknown, no player flag), so this works with the installed v1 Lua. The Lua
    now classifies them like any other visual light (`BootstrapClient.lua` 134); the
    reinstall only refines their owner (item/scenery instead of unknown), it is not needed
    for the behaviour.
- **Lights with a game shadow map** (`ls_shadowed=3`, default; the High and Ultra presets):
  the traced visibility replaces the atlas term where the segment surface → light lies in
  the view frustum (the light projects on screen; the frustum is convex and the surface is
  on screen, so every opaque caster on the segment was drawn and is in the TLAS). Elsewhere
  `min(traced, atlas)`: the atlas has the off-screen casters. Not detectable: casters in the
  atlas but not in our prepass (forward/transparent geometry, if the game renders them into
  light shadow maps); they would be lost on screen. `ls_shadowed=1` replaces everywhere
  (Full+), 2 is `min` everywhere, 0 the game's map. The default variant (`LS_FULL=0`) now
  handles every mode, so mode 3 does not fall back to the 252-register pass. Hair keeps the
  atlas term (its self-shadow model for shadowed lights is not replicated). Note: `ls_check`
  / `ls_impl=0` compare against the frozen reference, which knows modes 0-2 only.
  - Cost. Budget: Ultra at 3840x1600 on the 5090 needs a base frame of ≥ 65 fps (15.4 ms)
    under FG 2x to 120; QUALITY R7 gives D-LS 0.5 ms. Rule: keep 3 on High/Ultra if it adds
    ≤ 0.15 ms over 0 and the pass stays ≤ 0.5 ms.
  - **Measured (session jobs `casters-S6/S8-*`, 2026-10-27, medians of 300 frames):**

    | Scene | `ls_shadowed=0` | `=3` | lights with a game shadow map |
    |---|---|---|---|
    | S6 Adamantine Forge | 0.372 ms | 0.375 ms | not measured (the stats job hit the open nv153 stall) |
    | S8 Water Queens House | 0.120 ms | 0.120 ms | 0.00 of 0.41 evaluations per px |

    | S5 Grymforge | 0.629 ms | **0.690 ms** (mode 1: 0.688) | 0.34 of 4.34 evaluations per px (7.8%) |

    S6 and S8 have no shadow-mapped lights in view. **Grymforge** (`32-casters-S5-*`, one
    session, no faults): replacing the 0.34 shadow-mapped evaluations per px costs
    **+0.061 ms** (traced 3.83 per px, occluded 1.13; light removed 18.0% of the lit
    luminance), within the +0.15 ms rule, and the on-screen test of mode 3 is free
    (mode 1 = 0.688 ms). **Default kept: `ls_shadowed=3` on High and Ultra.** The pass
    itself is over its 0.5 ms budget in Grymforge in every mode (0.63 ms at mode 0, the
    D-LS performance task of STATUS). The traces of the three modes cannot be compared
    frame to frame: their mean luminance swings 1772 / 419 / 77 nits with large bright
    VFX blobs in all three (mode 0 included), so a dedicated in-frame A/B (as `rt_debug=7`
    for the sun) is needed to judge the replaced shadows' look.
- **Other results of the same jobs.**
  - Coverage (S8, `rt_debug=3`): match 99.23%, miss 0.069%, in front 0.072%, behind 0.633%
    of which 0.629% "through vt cutouts": the stochastic alpha test at blurred mips letting
    primary rays through the edges of ferns and ivy (the debug frame shows them only there).
    The characters are matched. Missing geometry is ≤ 0.07%.
  - Skipped by art intent (`ls_noshadow=1`, owners 0x1F): 11.4% of light evaluations in S6,
    86% in S8, 11.8% in S5. That is the darkvision pair; in S8 most of it is the `Point_Default` half,
    which has intensity 0 (it covers the view at r = 18 and adds nothing). The owner view
    (`ls_debug=9`) shows the party's floor in S8 lit by an owner-unknown visual light (the
    carried fire weapon), which is traced; little magenta (character) light anywhere.
  - Join: unmatched 0.0% with `ls_dyn_tol` 0.05 and 1.5 alike: the carriers stood still in
    both sessions. The benefit needs a moving carrier (`-Move`), not yet run.
  - Non-finite values: 0 in S8; in S6 2.6-4.4k in every trace and mode, all in one D32F
    depth image of pass `2a6a525c` (the game's), none in an image of ours.

**Box (projector) lights: the gap and the plan (not implemented).** They are the third loop
of `f469db78` (set 1 b29, struct `_53`, about 128 bytes; lines 735-930 of
`captures/20261026-165848/shaders/f469db7865d7c524.glsl`). A box light is a **directional
light inside an oriented box**:
- `uvw = _m6 · (P, 1)` (row-major 4x3, world → box), lit only for `|uvw − 0.5| < 0.5`;
- direction to the light is the constant `_m9` (N·L = `dot(N, _m9)`, H from `V + _m9`);
- falloff: along z, ramps below `_m2.x` (× `_m2.z`) and above `_m2.y` (× `_m2.w`); across x/y,
  `(0.5 − |uv − 0.5|) · _m3` below 1; each ramp picks one of four curves by `_m4 >> 24`
  (linear, square, smoothstep, smootherstep); `_m4 & 0xFFFFFF` is the channel mask;
- colour `_m0`, optionally × a cookie texture `_m1 >> 1` (index − 1) at `uv`; `_m1 & 1`
  scales by the global light scale b13[0];
- shadow: `_m8 & 1` → atlas slot `_m8 >> 2`, the same 12-tap PCF as spots, × `min(_m5, fade)`;
- BRDF: the same GGX/Burley as the point/spot loop (the class permutations add their terms).
So the light eval allows it. Plan: (1) read the record offsets from the SPIR-V `Offset`
decorations (as for b25) and bind b29 to D-LS (one more push-descriptor binding); (2) walk
the froxel's projector entries (`bg3_cluster_projectors` already counts them) and build a
`Bg3Light`-like record with a direction instead of a position, reusing
`bg3_light_eval_mat` with `Ld = _m9` and the box falloff in place of the range falloff;
(3) trace one ray along `_m9` (cone-sampled by a source angle) with `tmax` = the distance to
the box's face on the light side (the box is the light's extent; casters outside it do not
shadow it); (4) the same delta and `ls_shadowed` logic, the same art-intent rule (the rim
and fill rigs come from `CharacterLight` lists, owner character, flags from their
templates). Validation as 9.3: `ls_debug=4` with the projector pixels included.

**Forward receivers: design note (later).** Water, glass, particles and VFX are drawn after
the deferred lighting by the game's forward passes, which evaluate the sun with the cascades
and the local lights with the atlas themselves. To make them receive: (a) sun — write the
traced visibility into a screen-space image is not enough (they are not at the G-buffer
depth); trace in a small compute pass over the forward pass's own depth (a prepass of the
transparent surfaces does not exist; the refraction copy in step 13 gives the opaque depth
only), or patch the forward shaders' shadow lookup to read a world-space visibility cache
(SHaRC-like, STATUS backlog 5); (b) local lights — the same: a per-froxel visibility or a
patched lookup. Water is the most visible case and also needs the forward capture of
backlog item 12 to cast. Do it with item 12.

### 9.11 Exact-k + RIS remainder (DESIGN_SOTA_NEXT 1a; 2026-10-27, measured, off by default)

Why: Grymforge is over the 0.5 ms budget (0.73 ms with the 9.10 policy; `ls stats`: 6.09
listed, 4.34 evaluated, **3.83 traced** lights per pixel, against 2.1-2.3 in the foundry).

What (`shaders/gi/rt_local_shadows.hlsl` `ls_main_k`, both variants; `ls_k` > 0 with
`ls_samples=0`, `ls_debug=0`):
- **Phase 1**, one pass over the froxel list: the trace conditions and acceptance tests as
  before (`ls_light_traced`), then the light's exact unshadowed `c` (`bg3_light_eval_mat`,
  the surface loaded for phase 1 and reloaded in phase 3 as in 9.9) and `w = lum(c_d + c_s)`;
  a running top-`ls_k` by `w` (registers, insertion) and, for each remainder sample, a
  streaming reservoir over the lights that never enter the top-k or are pushed out of it.
- **Phase 2**: at most `ls_k + ls_rest_samples` rays, same ray setup. The exact set is
  traced in list order, not by rank, so that neighbours with the same set but another
  ranking trace the same light in the same iteration (coherent rays, as the `ls_k=0` loop).
- **Phase 3**: the exact terms of the selected lights only; the remainder sample's delta,
  and its share of the `ao_direct` sums, × `Σ_R w / (n·w_j)`.
- Estimator `Σ_E c_i(t_i − s_i) + (1/n) Σ_samples c_j(t_j − s_j)/p_j`, `p_j = w_j/Σ_R w`:
  **unbiased** (the split is a deterministic function of the pixel; `w_j > 0` wherever
  `c_j ≠ 0`). A remainder sample's scaled term has the luminance of the whole remainder
  (`Σ_R w`) at most: the variance is bounded.
- **Noise for RR** (QUALITY R1/R2): one pcg3d draw per (pixel, frame) from an input domain
  disjoint from the ray directions' (`x | 2^20`) drives both reservoirs (the uniform is
  rescaled after each decision); nothing is reused across pixels or frames.
- Registers (`tools/pipeline_stats.py`): **128 with and without this path** (the policy
  merge had already taken the default variant from 96 to 128); FULL 251 → 252.
- `ls_check=1` still compares the `ls_k=0` path (`ls_main`) with the frozen reference:
  it proves the shared tail (`ls_finish`, moved out of `ls_main`) bit-exact.

**First A/B (jobs `30-lsk-*`, 2026-10-27; optimised pass, no `shader_debug`, one session per
scene; first version, irradiance weight, see below):**

| Grymforge (S5) | ms (median, 300 frames) | rays/px |
|---|---|---|
| `ls_k=0` | 0.730, 0.731 (two sessions) | 3.83 |
| `ls_k=2` | 0.643 (−12%) | 2.76 (exact 1.96 + remainder 0.80) |
| `ls_k=1` | 0.528 (−28%) | ≈1.9 (estimated) |

- Stats (`ls_k=2`): 80.1% of pixels over the cap, 2.33 remainder lights per capped pixel
  carrying 13.9% of the weight; remainder rays occluded 32.7%.
- Campsite (S3): `ls_k=0` 0.965 / 0.965, `ls_k=2` 0.955 (−1%); 1.26 traced lights/px,
  15.1% of pixels over the cap.
- **Not faster per ray.** A linear fit `ms = F + O + rays·c` over the three Grymforge runs
  gives ≈0.135 ms per ray per pixel and a fixed **≈0.06-0.07 ms extra** for the exact-k
  path (k1 and k2 alike, so not the top-k insertion, which grows with k). The candidates:
  phase 1's extra work per light, and the remainder rays (0.8-1 per pixel at k1 and k2,
  each to a different light per lane: incoherent traversal). Registers are not the cause
  (128 either way). The follow-up jobs `31-lsk2-*` separate them (`ls_rest_samples=0`).
- **Images: fireflies.** The `ls_k=2` Grymforge trace has blue fireflies on wall edges
  (dumps saturated at 65,000 after the pass; mean luminance ×18). Cause: the first
  version weighted by irradiance without the BRDF lobe; at grazing view angles the
  specular term over the irradiance grows without bound (`vis ∝ 1/(N·L)` cancels the
  irradiance's N·L), so `c_j / p_j` was unbounded. Unbiased, but infinite in practice.
  Fixed by the exact `w = lum(c)` above (bounded by `Σ_R w`), at the cost of a BRDF
  evaluation per traced light in phase 1. `ls_k=1` and the FULL-variant `ls_k=2` trace
  happened to show none; their means are within the run-to-run spread of two `ls_k=0`
  sessions (−1.3%), and the QA `hard_dark` regions flagged in them are moving characters
  (crops checked), not shadows.

Settings (live): `ls_k` (**default 0**: the follow-up below did not pass; 1-4), `ls_rest_samples`
(default 1; 0 drops the remainder: biased, timing and debug only; 2). Push: `ls_kmode`,
the old spare word `pad` (bits 0-3 / 4-7).

Statistics (`ls_stats=1`, line `ls exact-k stats`, counters 122-127): lights `ls_k=0` would
trace, rays per pixel (exact / remainder), the share of pixels over the cap (k-hit), the
remainder size, its rays' occlusion, and the remainder's share of the weight. In this path
the `ls stats` line's "evaluated" count is 0. `ls_stats=1` costs ≈3-4 ms (FULL variant):
time only from runs without it.

**Follow-up (`31-lsk2-*`, exact weights, 2026-10-27).** Rule set before the run: the default
becomes the largest `ls_k` with Grymforge ≤ 0.69 × `ls_k=0` (0.5 ms of 0.73) and no
`hard_dark` or firefly region against `ls_k=0` (dump maxima after the pass within 2× of
`ls_k=0`'s); otherwise 0 stays.

| Grymforge (S5), one session | ms | vs `ls_k=0` |
|---|---|---|
| `ls_k=0` (again, next session) | 0.780 (0.774) | |
| no rays (`ls_min_radius=1e5`): the floor F | 0.197 | |
| `ls_k=2` | 0.718 | 0.92: **fails** |
| `ls_k=2, ls_rest_samples=0` (biased) | 0.582 | |
| `ls_k=1` | 0.606 | 0.78: **fails** |
| `ls_k=1, ls_rest_samples=0` (biased) | 0.447 | |

Campsite (S3): `ls_k=0` 1.024 / 1.029, `ls_k=1` 0.929 (−9.5%), `ls_k=2` 1.017 (−1%).

- **Where the time goes** (exact rays 1.96 / 0.99 per px at k2 / k1, remainder 0.80, from
  the stats of the first A/B): an exact ray costs **0.139 ms per ray per px** (the two
  `rest=0` runs), against 0.152 in the `ls_k=0` loop (which includes each light's
  evaluation); a remainder ray **≈0.17** (`rest=1 − rest=0` at both k): +22%, from its
  incoherent target (a different light per lane) and its evaluation; and a **fixed
  0.11 ms** over the floor (`0.447 − 0.197 − 0.99·0.139`): phase 1's surface load and the
  exact BRDF of every traced light, which `ls_k=0` evaluates once and this path twice
  (≈0.06 ms with the first version's irradiance weight). Registers are not the cause
  (128 in both paths). That overhead takes most of the saved rays: Grymforge's floor is
  already 0.197 ms, and even 2 rays per pixel (k1) cost 0.41 ms above it.
- **Images pass.** No fireflies: the maxima after the pass are 39.5 (k2) and 47 (k1)
  against 51 / 45 for the two `ls_k=0` runs (the first version: 65,000). Mean of the lit
  HDR (compose) −3.6% (k2), +0.2% (k1) against +1.4% between the two `ls_k=0` runs. Every
  `hard_dark` region flagged on the final frame (S5 k1 3, k2 1; S3 k1 2) is a moving
  character or the animated lava, as two `ls_k=0` frames also differ there (crops
  checked). Pre-RR noise (high-pass of the compose): k1 +13% at Grymforge, k2 +1%;
  unchanged at Campsite; the final frames are within run-to-run spread.
- **Verdict: `ls_k` stays 0.** Neither k meets the budget rule. `ls_k=1` is a valid,
  unbiased cost option (−22% Grymforge, −9.5% Campsite, more pre-RR noise) for a lower
  preset; that is a preset decision, not made here.
- **What would make it pay:** a weight as cheap as the first version's but bounded, so
  that phase 1 needs no full BRDF (e.g. irradiance × a per-pixel bound of the specular
  lobe, the bound evaluated once per pixel); at most ≈−0.05 ms, so k1 ≈0.55 ms: still
  over 0.5. Grymforge's budget miss is the floor and the 3.8 traced lights per pixel;
  1b (ReSTIR) does not remove either. A lower-resolution visibility or a per-froxel
  light cull (lights fully occluded for the whole froxel) are the levers left.

## 10. D-RF: ray-traced reflections (implemented, tiers (a) and (c))

Status (2026-10-26): **implemented, on by default** (`rt_reflections=1`, live; needs `rt=1`
and `gi=1`). Code: `shaders/gi/rt_reflections.hlsl` (the pass), `shaders/rt/bg3_ambient.hlsli`
(the game's specular ambient, D-EN4), `core/gi.cpp` (bindings, pass, timing, statistics),
`layer/ngx.cpp` (the RR hit distance), `layer/cmd.cpp` (`query_set`, host ABI 5).
Measured in the Steel Watch Foundry (the auto-run save) and at the docks (end of the `-Move`
pan), RTX 5090, 2560x1067.

### 10.1 The game's specular ambient (D-EN4)

BG3 has no SSR: its only reflections are cube-map probes, applied inside the tiled lighting
`f469db78` (FRAME_MAP "Specular ambient"). The term in b11 is `S · k · SO`:
- `S` (`_1684`): the global probe b35 (and b37) at `R = reflect(−V, N)`, mip `a·8 + _m30 − 8`,
  times the split-sum env BRDF `E = F0·DFG.x + DFG.y` (LUT b38 at `(max(N·V, 1/512), a)`); then
  every local probe listed in the pixel's froxel (b23 `y >> 24`, records b33, cubes in the
  bindless array b45: box/sphere parallax, edge fade, its own roughness and DFG) lerped over it;
- `k = b13[0] · 100`;
- `SO`: Lagarde's specular occlusion from the AO target;
- then the aerial-perspective transmittance (1 indoors; not replicated).

**How the pass sees the probes.** The local probes need the bindless cube array, which
cannot be pushed (push descriptors are capped at 32 and D-LS already uses 30). The host now
exposes `query_set` (ABI 5): the descriptor set the lighting dispatch has bound at set 1, its
`VkDescriptorSetLayout` and its dynamic offsets (2 here). D-RF binds **that set itself** as its
set 2, through a pipeline layout built on the game's own set layout (rebuilt if the layout
changes; it never did). Every probe resource is the game's exact descriptor. The froxel walk
and the lighting constants come from the D-LS push bindings 21-29; the hit distance image is
push binding 30 (31 push descriptors).

**Validation** (`rf_validate=1`, `rf_stats=1`): the D-LS prep pass sets the AO to 1 (so
`SO = 1`) and the sun mask to 0 before the lighting, and D-LS is skipped. b11 then holds only
the local lights (recomputed with the game's own atlas shadows, as D-LS does), projector
lights and `S·k`. Over the pixels without projector lights:

| Foundry, gameplay (same result in two runs) | Value |
|---|---|
| pixels compared | 1.34M per frame |
| game specular (luminance) | 0.01813 |
| local lights + probe term recomputed | 0.01825 |
| **mean \|residual\|** | **0.00011 = 0.58% of the game's** |
| recompute above the game | 0.58% (almost all of the residual) |

The replication meets the < 1% gate of D-EN4. The remaining +0.7% is an excess of the
recompute; its cause (LUT/cube filtering, the transmittance) is not identified. The uber
permutation `34a007cc` handles shading models 2-7 (`RT2.w & 7`) differently (two-sided
foliage blends a back-face probe, model 7 takes F0 from RT3); D-RF only touches models 0 and 1
(`rf_models=3`): 99.8% of the foundry's pixels, 98.5% at the docks. (Validation numbers logged
on loading-screen frames are meaningless: that G-buffer is not the scene's.)

### 10.2 What the pass does

Full resolution, after the lighting and D-LS, **before** the GI composite (which stores this
frame's lit radiance, reflections included, as next frame's `prevRadiance`), SSS and fog.

1. The surface exactly as `f469db78` sets it up (shared with D-LS): `P`, `N` (not
   renormalised), `V`, F0, roughness `a` with the game's specular AA, the shading model.
2. `Tp = S·k·SO` (10.1) and the pixel's `E`.
3. One ray (`rt_closest`, alpha-tested TLAS) from `P + N·dist·rt_bias`, up to `rf_tmax` (100):
   - with RR (`rf_rr=-1` follows `rr`): GGX **bounded VNDF** (Eto & Tokuyoshi 2023), α = a²,
     white noise from `pcg3d(pixel, frame)` on its own stream; a mirror ray for
     `a < rf_mirror_rough` (0.1);
   - without RR: mirror rays only, up to `rf_max_rough_norr` (0.3). There is no denoiser for
     GGX noise there, so the fallback is deterministic and nothing of ours filters (R2).
4. Tiers:
   - **(a)** the hit projects on screen, the depth there is the hit itself (`rf_depth_k` × the derived depth_tol; was `rf_depth_tol`
     3%) and that surface faces the ray: `L = prev_radiance_at(hit pixel)` (last frame's lit
     diffuse + specular, reprojected with the MVs, QUALITY Q1), weight `w = 1`, faded within
     `rf_edge` of the screen border and between 0.75 and 1 × `rf_max_rough` (0.6);
   - **(c)** a miss, a hit hidden on screen or off screen, or a sample below the horizon:
     `w = 0`, the game's own probe term stays exactly as it was. A TLAS miss is never sky
     (QUALITY R3; roofs are cut away).
   - Tier (b), shading off-screen hits, needs the hit material (D-EN2) and is not done.
5. The exact delta `b11 += w·(L·E − Tp)`, clamped at 0 only against the transmittance it does
   not replicate. The game's `SO` is not applied to traced hits (`rf_so=0`): the ray found the
   occluder and returns its radiance; applying `SO` as well would double-occlude.
6. RR's `DLSSD.SpecularHitDistance` (R16F): the hit distance for tier (a), 65504 elsewhere
   (the probe behaves as an environment at infinity). Non-finite guard on every write (R5).

Per pixel the estimate is stochastic between (a) and (c); in expectation it is
`P(a)·L·E + (1 − P(a))·Tp`, and RR denoises it (R2: no history or filter of ours).

### 10.3 RR guides: the specular hit distance

`layer/ngx.cpp` sets `DLSSD.SpecularHitDistance` (and its subrect base) on every RR evaluate
while the image is fresh (written this frame or the previous one) and at the render
resolution; otherwise the key is cleared, since the game reuses its parameter block. It goes
with the v2 world normals and camera matrices (TRACK_A "Guides v2"). The log line
`[ngx] RR gets the specular hit distance (D-RF)` confirms it, and RR reported no evaluate
error in any run. `rr_spec_hit_distance=0` withholds it (an A/B of its effect was not made).

### 10.4 Results

Foundry, one run, live toggle (`auto-run.ps1 -Traces 4` with `scripts/live_phases.py`,
`dump_png_width=3840`; `scripts/ab_compare.py`):

- [rf_foundry_off.png](img/rf_foundry_off.png) / [rf_foundry_on.png](img/rf_foundry_on.png) /
  [rf_foundry_diff.png](img/rf_foundry_diff.png) (×4): the presented frames, downscaled.
- [rf_foundry_crops.png](img/rf_foundry_crops.png): three 1:1 crops of the 3840x1600 output,
  off | on | diff ×4. Top: the metal pipes along the walkway. With the probe they and the wall
  behind them carry a uniform sheen; with D-RF they reflect the dark wall and walkway they
  face, and the sheen is gone. Middle and bottom: the chimney pipes and the curved beam,
  rough; almost no change (the moving NPC is most of the diff).
- [rf_foundry_probe_term.png](img/rf_foundry_probe_term.png) (`rf_debug=3`): the game's probe
  term as recomputed, i.e. everything D-RF can replace.
- [rf_foundry_tiers.png](img/rf_foundry_tiers.png) (`rf_debug=1`): green tier (a), red hidden,
  orange off screen, blue miss (the walkway's rays leave through the cut-away roof), grey not
  traced. [rf_docks_tiers.png](img/rf_docks_tiers.png): the same at the docks.

| Statistics (`rf_stats=1`, 120-frame windows) | Foundry | Docks (end of the `-Move` pan) |
|---|---|---|
| traced (models 0/1, `a` below the fade) | 54.1% of px (1.48M rays) | 23.3% (0.64M) |
| of the traced: tier (a) on-screen hit | 42.0% | 4.8% |
| hidden on screen / off screen / miss | 20.9 / 6.1 / 28.4% | 1.5 / 3.0 / 5.7% |
| below the horizon | 2.7% | 85.0% (G-buffer under the forward-rendered water) |
| local probes per pixel | 1.29 | 0.01 |
| mean luminance: game specular / probe term / probe term after D-RF | 0.01425 / 0.01093 / 0.01098 | 0.03700 / 0.01714 / 0.01717 |
| presented frame, mean luminance | −0.78%; 10.5% of pixels change by > 2/255 | water animation dominates the diff |
| non-finite values (every dump of every trace, all runs) | **0** | **0** |

The probe term is 77% of the foundry's specular, yet its mean moves by +0.5% only: the
foundry is mostly rough painted metal and stone, and the change is local (concave areas lose
the probe's sheen, faces opposite lit surfaces take their colour). The docks view the pan
ends at is mostly water, which is forward-rendered over the G-buffer; there D-RF changes only
the ship's hull and the quay. **No scene with strongly reflective G-buffer surfaces (wet stone,
polished floors) was reached by the automated runs**, so that A/B (R6) is still owed.

Moving camera (`-Move`, RR on): the presented frame during the pan
([rf_docks_moving.png](img/rf_docks_moving.png)) shows no noise, smearing or black holes on
the reflecting surfaces; 0 non-finite values. The dark dissolve pattern at its left edge looks
like the game's see-through cut-away of the wall in front of the camera, but no D-RF-off frame
of that exact view was captured to prove it. Not measured: a temporal-stability metric and
FLIP against a reference.

### 10.5 Cost (GPU timestamps around the pass, medians of 300 frames)

| Configuration | Foundry | Docks |
|---|---|---|
| **Default (RR, GGX VNDF, 1 ray per px below a = 0.6)** | **0.33 ms** (p10 0.31, p90 0.36) | 0.13 ms |
| `-Validation` | 0.31 ms | — |
| `rf_stats=1` (wave-reduced atomics; measurement only) | 4.1-4.4 ms | — |

`rr=0` (mirror rays up to a = 0.3, 2% of pixels traced) was only timed with `rf_stats=1` on,
so it has no clean number; the probe recompute runs on every pixel either way. The default is
within the 0.5-1.0 ms budget (QUALITY R7). Track D in the foundry: sun + RT GI 0.26-0.28 ms,
D-LS 0.35 ms, D-RF 0.33 ms, about 0.95 ms of the 2.5 ms.

### 10.6 Validation layer and robustness (R5, R8)

- `auto-run.ps1 -Validation`, `rt_reflections=0` against `1` with every mode toggled live in
  the lit foundry (`rf_stats`, `rf_validate` with debug 5, debug 1, 2, 3, 4, 6, `rr=0` (the
  mirror path), off, on): **identical message IDs**, all the game's own (`09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, `CopyBuffer-00118`, `08737`, the headless
  `non-acquired-swapchain-image-used`). Binding the game's own descriptor set into our
  pipeline raised nothing.
- `fg=2` (`-RealPresent`, DLSS-G 2x) with D-RF on and RR getting the hit distance: DLSS-G
  status 0x0 on every logged frame (26 reports), 2 presents per rendered frame, 0 Streamline
  API errors; the pass costs the same (0.313 ms).
- Missing inputs (no TLAS or a stale one, a missing light-list binding, the lighting's set 1
  being a push set, `gi=0`): the pass does not run and logs why once; the game's probe term
  is untouched, and RR stops getting the hit distance after 2 frames.

### 10.7 What does not work / next steps

- **Tier (b)**, off-screen hits shaded with the hit material (D-EN2 + EN1 lights): 6% of the
  foundry's rays (and the 21% hidden on screen) fall back to the probe.
- **Off-screen and culled geometry** is missing from the TLAS (D-EN3), so hits behind the
  camera are probe-shaded, and misses through cut-away roofs keep the probe (by design).
- **A reflective-scene A/B** (wet stone, polished floors) and the temporal-stability metric.
- **Shading models 2-7** are left to the game (`rf_models`).
- **The transmittance** is not replicated (delta clamped at 0), as for D-LS.
- **`rr=0`**: mirror rays only, up to `a = 0.3`; NRD REBLUR is not integrated.
- **Bounce source.** `prevRadiance` holds view-dependent specular (QUALITY Q8). For reflections
  that is the usual SSR approximation (the hit's highlight as the camera sees it).
- `stats_begin` adds 3.8-4 ms when `rf_stats=1`, as for D-LS: measurement only.

Settings (`bg3rtx.ini`, all live): `rt_reflections`, `rf_max_rough`, `rf_max_rough_norr`,
`rf_mirror_rough`, `rf_rr`, `rf_tmax`, `rf_depth_k` (was `rf_depth_tol`), `rf_edge`, `rf_strength`, `rf_so`,
`rf_models`, `rf_debug` (1 tiers, 2 change ×4, 3 probe term, 4 probe term after D-RF,
5 validation residual, 6 hit distance), `rf_stats`, `rf_validate`, `rr_spec_hit_distance`.
The log reports `rf: first frame ...` (the lighting's set and its layout),
`rf: reflection pass GPU median ...`, `[ngx] RR gets the specular hit distance` and, with
`rf_stats=1`, `rf stats` and `rf validation`.

Tooling added with D-RF: `auto-run.ps1 -MoveSec N` (release the pan key after N s), and the
`trace.request` file trigger now works (its path had a `\t`, a tab, so it never fired).

## 11. Static/dynamic BVH split: instanced object-space BLAS (`rt_bvh_split=1`)

AUDIT_SOTA rank 2, implemented 2026-10-26 (`layer/bvhsplit.cpp`, `shaders/rt/mesh_extract.hlsl`,
`shaders/rt/rt_common.hlsli`). On by default and live: `rt_bvh_split=0` switches back to the
single soup BLAS of sections 6-7 on the next frame (that path is unchanged).

### 11.1 What was wrong

The scene was one BLAS over the whole per-frame XFB soup: refit while the draw sequence held,
rebuilt whenever it changed. BG3 culls on the CPU (AUDIT_ENGINE_DATA §0), so every camera
pan changes the drawn set, and the BLAS was rebuilt every frame of a pan (2.3-3.4 ms). Static
geometry was also captured, converted and refit every frame.

### 11.2 Design

**Which draws are instanced.** Class A (section 1) places a mesh with an affine
per-instance matrix whose three rows are vertex inputs. `analyze_vs_instancing`
(`layer/spirv_xfb.h`) proves this from the game's SPIR-V. It looks for
`W = vec4(P, 1) * mat4(R0, R1, R2, (0,0,0,1))` with P and the rows plain input loads, and W
reaching `gl_Position` only through `vec4(W.xyz, 1) * matrix`. Then W is exactly the 3x4
row-major matrix `[R0; R1; R2]` applied to P, which is the `VkTransformMatrixKHR` layout.
- Over the 2,418 dumped vertex shaders, 187 match. The foundry prepass ones are `e6fc6bf6`,
  `689836de`, `31c4659f`, `646a930e` and `f4b35620`.
- These are rejected: wind and cloth (`5f757332`: W gets a displacement), the variants that
  add a per-draw UBO translation to the rows (`2ad8656b`, `106a0550`, `05d39f75`), and the
  skinned and per-draw-matrix variants.
- `make_inst_pipe` (`layer/geocapture.cpp`) checks the pipeline's vertex input. The rows must
  be `R32G32B32A32_SFLOAT` attributes of instance-rate bindings with divisor 1, and the
  position must come from a per-vertex binding. For alpha-tested draws, the captured UV must
  be a copy of a per-vertex input.

**Mesh key.** The VS hash and per-vertex input layout, the per-vertex vertex buffers and
offsets, the index buffer, offset and type, the draw's count, first index and vertex offset,
and the alpha test. New hooks provide the buffers: `vkCmdBindVertexBuffers(2)` and
`vkCmdBindIndexBuffer`.

**Building a mesh, once.** The first time a key is seen, its draw is captured as before.
`mesh_extract.hlsl` maps the first instance's triangles back to object space with the
inverse of that instance's matrix, into a mesh pool (with the UV, stride 24).
- It checks the result against the draw's last instance (`M_last · P` against its captured
  triangles).
- A single-instance draw is checked again in a later frame instead: `M_now · P` against that
  frame's capture.
- Collapsed triangles (sky z override, NaN-parked vertices; `xfb_to_world` now marks them
  `w = 0`) and singular or non-finite matrices reject the mesh.
- Rejections back off and are retried later: 600 frames, doubling.
- The mesh's BLAS is built from the pool (`PREFER_FAST_TRACE | ALLOW_COMPACTION`) and
  compacted once its size is back.
- Results come back through a host-visible status buffer that a transfer marks done after
  the batch's jobs. A query's availability can still be a previous use's until the
  GPU-side reset executes. The first version read stale availability and lost results.
- Until a mesh is verified, its draws stay on the per-draw path below. New meshes are
  budgeted at `rt_mesh_new_tris` triangles per build.

**Every frame.** A draw of a verified mesh is not captured at all. Each of its instances
becomes a TLAS instance whose matrix comes from the game's own instance stream, which is
host-mapped.
- The matrices are copied when the game **submits** the command buffer (`bvh_on_submit`),
  which is exactly what the GPU will read.
- Non-finite or singular matrices get mask 0.
- The log counts draws whose instance data changed between record and submit. It was 0 in
  every run.

**Everything else** (skinned, wind/cloth, per-draw matrices, terrain, sky, meshes in
probation) is captured and converted as before. Each draw gets its own BLAS, keyed by the
draw's identity (pipeline, binding 0, index range, n-th occurrence in the frame).
- It is refit while its triangle count and build flags hold.
- It is rebuilt at least every `rt_rebuild_frames`, staggered per draw.
- So a draw entering or leaving the view costs only its own build.

**TLAS** over all instances (`PREFER_FAST_TRACE | ALLOW_UPDATE`).
- Instance records live in a host-visible ring (8 regions).
- The TLAS is refit while the instance list (count, BLAS and flags in order) holds, at
  least every `rt_rebuild_frames`.

**Eviction.**
- A mesh is evicted when one of its game buffers is destroyed (the one `vkDestroyBuffer`
  hook, in `tracker.cpp`; geocapture's duplicate hook used to shadow the tracker's), or
  when a transfer writes its index or vertex range (`vkCmdCopyBuffer(2)`,
  `vkCmdUpdateBuffer`, `vkCmdFillBuffer` hooks).
- It is also evicted by LRU over `rt_mesh_budget_mb` and after `rt_mesh_lru_frames` unused.
- A periodic **audit** catches any rewrite the hooks cannot see (host writes to mapped
  vertex memory). Up to `rt_mesh_audits` cached draws per frame, each mesh at most every
  `rt_mesh_audit_frames`, are captured once more and compared. A failure evicts the mesh
  with a warning.

**Ray queries** (`rt_common.hlsli`). One `InstInfo` per TLAS instance (set 1 binding 7)
gives: where its vertices are (the mesh pool, binding 8, object space; or the soup, binding
6), the stride, the alpha record, and the triangle count.
- Alpha-tested candidates read their UVs and corners through it and transform them by
  `CandidateObjectToWorld3x4` for the footprint.
- `rt_origin`'s geometric normal uses `rt_hit_normal`, which applies the committed object
  to world transform.
- The legacy TLAS has one instance marked `kInfoLegacy` and keeps the old lookups (geometry
  0/1, records by triangle index).
- Opaque draws' instances are `FORCE_OPAQUE`. `rt_alpha_test=0` forces all instances
  opaque, as before.

### Invisible helper draws (`rt_capture_discard`, 2026-10-30)

The inn's two dark floor squares come from one captured 24-triangle helper draw,
VS `ca869467434cd2db`, FS `dc180592a8fc6087`. In capture 890 both floor regions hit
TLAS instance 1834 in frame 3992, on the per-draw path. This FS has one empty
entry block ending in `OpKill` and no EarlyFragmentTests mode. It never produces
raster depth or color, regardless of its pipeline's depth-write flag.

Transform feedback runs before fragment discard. `rt_capture_discard` filters
this proved always-discard case before either BVH classification or XFB allocation.
`common/spirv_visibility.h` recognizes only that exact control-flow shape. It
rejects conditional alpha tests, normal returns, multiple entry points and early
fragment tests (which can write depth/stencil before the kill). Raster commands
still run. The check is cached per module/pipeline and its live key once per capture.
No host ABI or lighting shader change is needed.

The split-off oracle was incomplete: the legacy capture overflowed about 168
opaque draws/frame and omitted this late helper. A corrected same-run refit sweep
leaves 30,343-30,362 opaque-front pixels for TLAS refit, dynamic refit and in-place
rebuild toggles. Capture-all/audit-all also overflow; their lower residual is not
parity. The cause is capture eligibility, rather than firstInstance, instance count,
vertex offset or stale transforms.

Gate: `sup-hidden-quads-discard-gate`, valid FG 2 run, 0/1 twice in debug 8.
Opaque-front pixels 29,411 / 29,429 -> 132 / 127; the two square regions together
29,288 / 29,303 -> 0 / 1 pixels. Primary-ray match 96.095% / 96.092% ->
96.951% / 97.190%. Normal-image mean +0.28%. BVH build GPU median, with all
statistics and primary diagnostics off: 1.387 -> 1.368 ms (14 timing windows each;
median reported p99 1.791 -> 1.686 ms). Zero VK errors, NVIDIA events or dumps.
Kept, `rt_capture_discard=1` by default. Check The Blushing Mermaid - 96h 39m near
the crates and stage edge after a layer restart. Diagnostic keys remain default 0.

### Static-camera LOD retention (`rt_dyn_hold`, 2026-10-29)

BG3 alternates captured LOD draws every two frames even with the camera still
(sup-static-flicker3-fg2: 380 / 764 and 310 / 154 primitives). `rt_dyn_hold=N`
keeps a missing captured draw for N frames since its last actual capture (max 120).
Movement clears all holds. The last submitted BLAS is reused; a GPU copy of all
positions and UVs lives in the persistent mesh pool, with an identity transform
and the existing pool InstInfo flag. No shader or host/core ABI change is needed.
Alpha records retain actual views and material constants, remapped to the current
scene's descriptor indices. Views and images are protected through the hold window.
Dropped command buffers cannot supply holds. Pool ranges retire through the existing
GPU/scene lifetime guard. Held draws never refresh their own expiry.
Gate pending: QuickSave_3784, FG 2, static camera, live hold 0 / 4 for 40 seconds each;
alternations within 8 frames, primary match, RT GPU cost. Default remains 0 pending gate.

### 11.3 Results (RTX 5090, 2560x1067, Steel Watch Foundry, `-Background`)

**Geometry parity.** The same view, live A/B in one run, `rt_debug=3 rt_stats=1`:

| | legacy soup | split |
|---|---|---|
| primary-ray match | 98.976-98.984% | 98.974-98.980% |
| in front (opaque / vt cutouts) | 1.007-1.015% (0.95 / 0.060) | 1.011-1.017% (0.95 / 0.060) |
| behind / miss | 0.009% / 0% | 0.009% / 0% |
| triangles in the TLAS | 2.59M (0.68M alpha-tested) | 2.060M static (2,070 instances of 507 meshes) + 0.535M dynamic (182 draws) = 2.595M |
| ray origins: TLAS surface found under | 98.3-99.3% | 98.3-99.3% |

The 0.95% "opaque in front" is the screen-door dithered geometry of 7.1, present in both.
- In the 3D main menu, before the save loads, the primary check reads 67-69% with the legacy
  soup and 68-72% with the split (separate runs, same scene). That low figure belongs to the
  menu scene, not to the split.
- The static instance count follows the view. During a pan the cache grew to 627 meshes
  (1.81M unique triangles) with 0 rejections for mismatch.

**Cost.** GPU timestamps around the scene build (convert + extraction, BLAS builds/refits,
TLAS). The medians are the medians of 300-frame windows; p99 is the worst window's p99.
The depth prepass time with XFB active is separate (`geo_timing=1`).

| | legacy | split |
|---|---|---|
| **camera panning** (`-Move`), scene build | median **1.37 ms**, p99 **2.91 ms** (every frame a rebuild: BLAS 1.47-2.55 ms) | median **0.45 ms**, p99 **0.83 ms** |
| panning, depth prepass with XFB | 0.22 ms (0.16-0.37) | 0.17 ms (0.14-0.20) |
| static foundry, scene build | 0.71-0.72 ms (p99 0.73-0.92); forced rebuild every 60 frames 2.8-2.9 ms | 0.49 ms (p99 0.75) before the TLAS refit; **0.29-0.35 ms (p99 0.57-0.74)** with it |
| static foundry, depth prepass with XFB | 0.47-0.49 ms | 0.23-0.24 ms |
| static, light view after the pan | 0.21 ms (refit of a small soup) | 0.34-0.37 ms (TLAS 0.14 ms, 60 small per-draw refits 0.17 ms) before the TLAS refit; TLAS 0.020 ms once refit (98% of static frames), total 0.25 ms in a comparable light view |
| level load / first frames | rebuild 2.9 ms | up to 2.1 ms p99 while ~500 meshes are built once |

- **Motion: −0.9 ms median and −2.1 ms p99** per frame on this machine.
- **Static foundry: about −0.65 ms** (build −0.40, XFB −0.25), and no 2.8 ms rebuild every 60
  frames.
- In the light outdoor view after the pan, the split was 0.15 ms slower than a legacy refit:
  the fixed TLAS cost plus many small refits. The TLAS refit (11.2) was added for this.
- The forced legacy rebuild every 60 frames (2.9 ms, the 1% lows) is gone for static
  geometry.

**Memory** (foundry): mesh pool 87-124 MB (object-space position + UV, 72 B/triangle).
Mesh BLAS after compaction 78-90 MB for 1.3-1.8M unique triangles. Compaction saved 22-44 MB
per 100-500 meshes (about 25-35%).

### 11.4 Validation layer and robustness

- `auto-run.ps1 -Validation -Background` (Khronos validation below the layer): a baseline
  was taken with the unmodified main first, then the split was run in gameplay with a 15 s
  pan (8 minutes). **The message IDs are identical**: the game's own `09600`, `10788`,
  `08737`, `CopyBuffer-00118`, and the headless `non-acquired-swapchain-image-used`.
  - The first split run added two IDs of ours: `vkCmdFillBuffer-dstBuffer-00029` and
    `vkCmdCopyQueryPoolResults-dstBuffer-00825`. The status buffer lacked
    `TRANSFER_DST` usage; that is fixed.
  - A third run covered the main menu only, also clean.
- Non-finite values: 0 in every image dump of the 15 traces taken during this work (legacy
  and split, static and panning).

- Per run: 0 non-finite or singular instance matrices (about 600k read per 300 frames), 0
  late instance writes, 0 commands recorded but never submitted, 0 validation results lost
  after the fix, 0 audit failures over thousands of audits, and a maximum accepted error of
  0.012 × tolerance. The one recurring "invalid" rejection is the sky dome (z = 0).
- `-Move` runs: no device loss, no GPU page fault, no crash.
- The alpha region "full" warning in the main menu (64 of 77 MB) predates this work (the
  legacy capture shows it too).

### 11.5 What does not work / next steps

- **Not compared:** Campsite, Lower City and Water Queen's House (foliage, view distance) were not
  run with the split; `fg>=2` with the split.
- **UBO-offset class-A variants** (`2ad8656b`, `106a0550`, `05d39f75`; 45+ draws in the
  foundry prepass) stay per-draw: their rows plus a per-draw translation from a UBO. The
  matrix could take the UBO offset (read like the alpha constants).
- **Skinned meshes** use XFB per draw (refit each frame), not the skinning SSBO b13 directly
  (AUDIT_ENGINE_DATA 2.4); that would save their XFB.
- **Wind foliage** (class D) is per-draw and rebuilds when its instance count changes;
  outdoors it is the remaining motion cost. A rest-pose instanced BLAS plus a refit, or
  CLAS, are the options.
- **Opacity micromaps** (AUDIT_SOTA rank 4) can now attach to the cached mesh BLAS: the pool
  holds the UVs, and OMMs survive compaction.
- **Retention (EN3)**: meshes stay cached after leaving the view; keeping their instances in
  the TLAS needs the last matrices and a policy.
- `-Move -Background` posted one key-down every 2 s, which the game reads as taps, so the
  pans of earlier docs were mostly static. `auto-run.ps1` now posts key-downs at about 30 Hz
  with the repeat bit (to the game window only).

Settings (`bg3rtx.ini`, all live):
- `rt_bvh_split`; `rt_tlas_refit`; `rt_rebuild_frames`, which now paces the per-draw BLAS
  and the TLAS;
- `rt_mesh_compact`, `rt_mesh_compact_per_frame`, `rt_mesh_new_tris`;
- `rt_mesh_pool_mb`, `rt_mesh_pool_max_mb`, `rt_mesh_chunk_mb`, `rt_mesh_budget_mb`,
  `rt_mesh_lru_frames`;
- `rt_mesh_audits`, `rt_mesh_audit_frames`, `rt_mesh_tol_abs`, `rt_mesh_tol_rel`.

The log reports `[bvh] last N builds ...` (draws, instances, triangles, refits),
`[bvh] mesh cache ...` (meshes, memory, verification, rejections, evictions, compaction),
`[bvh] instance matrices read at submit ...`, and
`[rt] scene build GPU (<mode>, static|camera moving, N samples): ... total median, p99`.
`[geo] depth prepass pass GPU time (...)` now gives p99 and splits by motion and mode.

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
     radius of the camera, nearest first, at most `kSunMaxVolumes` (256). The radius is
     derived per frame (`core/sun_trust.cpp roof_radius`): the farthest pixel whose trust
     still depends on the roof test (read back from `rt_sun_resolve.hlsl`, header byte 48)
     + `rt_tmax` (the ray's reach), so every volume a counted ray can meet is in the query;
     `rt_sun_roof_radius` > 0 overrides it. Volumes beyond 256 are dropped farthest first,
     logged once per level (`sun: WARNING N roof volumes ...`) and counted in `sun stats`
     (`over kSunMaxVolumes on N frames`). `scripts/roof_stats.py` (asset DB): at most 398
     volumes in a level (CTY_Main_A), at most 183 within 300 or 600 m of any volume centre
     and 287 within 1000 m, so 256 holds for a radius up to 600 m. A ray that starts
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

## 13. Unknown ray queries, shared reflection origin, QA local-light knob (2026-10-27)

**Candidate cap = unknown, never lit.** A ray query that passes `kMaxCandidates` (4096)
alpha-tested candidates is aborted (counted in guard 121, as before). Its answer is now
*unknown*: `rt_visibility()` (rt_common.hlsli) returns `kRtLit` / `kRtOccluded` /
`kRtUnknown` (a hit committed before the abort is still `kRtOccluded`); `rt_occluded()` keeps
its signature and returns `rt_visibility() != kRtLit`, so unknown counts as occluded wherever a
caller has nothing better. `RtHit.aborted`, `RtDebugHit.aborted` and
`rt_closest(o, d, tmax, cone, out bool aborted)` say the closest hit is unknown (its fields are
then unreliable). Invalid rays and an unbuilt scene are unchanged (`kRtLit`, guards 120 / scene
stamp). Per caller:

| pass | on unknown |
|---|---|
| sun (rt_shadow.hlsl) | the game's mask value for the pixel (one ray per pixel: per ray = per pixel); `rtVis.a = 8/255` marks it, so the fold and rt_sun_resolve keep the mask exactly; `rt_debug=6` shows it yellow |
| D-LS (rt_local_shadows*.hlsl, ls_ref) | `ls_debug=10` yellow; `t = s` for that pixel and light: the atlas PCF for a light with a game shadow map, 1 for one without (the game never shadows those: its own visibility is unshadowed, this is the game's term, not "lit by default"). Same in every path (default, LS_FULL, RIS, exact-k) and in the frozen reference (hand edit), so ls_check stays comparable |
| D-RF (rt_reflections*.hlsl) | tier 6: w = 0, the game's probe stays, SHaRC is not asked; `rf_debug=1` yellow |
| RT AO (rt_ao.hlsl) | per ray, an occluder at distance 0 (weight 1): conservative; an abort means > cap alpha candidates within `rt_ao_radius`, i.e. dense foliage |
| GI (gi_trace_rt*.hlsl) | not changed here (separate change); counted as "GI" if it sets `g_rt_pass = kRtPassGI`, else "other" |

Unknown queries are counted per pass (rtStats 128..133, always on, like the guards) and logged
in the `[core] guard:` lines: `... unknown (candidate cap) queries ...: other N, sun N, D-LS N,
D-RF N, AO N, GI N`. **QA knob** `rt_max_candidates` (live, Push `rt_max_cand`, 0 = 4096, never
above): 1-4 forces aborts wherever a ray crosses more alpha-tested candidates (foliage).

**Reflections use the shared origin.** rt_reflections.hlsl starts its ray at
`rt_ray_origin(fp, P, Nn, foot, dir)` (rt_origin=1: TLAS surface point, geometric normal,
Wachter-Binder offset to the side `dir` leaves on; rt_origin=0: the old rt_bias offset) instead
of `P + Nn * dist * rt_bias`. The D-RF push set now also binds rtOrigin (40): 32 push
descriptors, the RTX 5090's limit (core/gi.cpp kExtras).

**QA ambient floor, `qa_zero` bit 1** (rt_local_shadows*.hlsl, ls_ref): every point/spot light
the tiled lighting evaluated at the pixel gets t = 0 and its unshadowed transmission ct is
removed too, so D-LS subtracts exactly what the game added for it (no rays; exact-k / RIS
bypassed). It includes the lights D-LS normally leaves alone (ls_shadowed=0, below
ls_min_radius, ls_owners / ls_noshadow). It cannot remove: shadow-mapped lights on hair pixels
(the game's hair self-shadow is not modelled), projector lights, anything outside the froxel
light list (forward draws, fog in-scatter, emissives), and nothing when D-LS does not run
(rt_local_shadows=0, no TLAS, sun-set ring full) or in ls_debug=4-7. No cleaner single point:
the light buffer is the game's (read-only to us), and the prep pass runs without the light list.

**To verify in game** (S3 Campsite, S8, S14 Lower City; `-Background`):
1. Baseline: `rt_max_candidates=0`; the `[core] guard:` heartbeat must show unknown 0 everywhere
   (or the real count: any non-zero here is a scene that needed the fallback).
2. `rt_max_candidates=1`, `rt_debug=6`: yellow pixels in foliage = aborted sun rays; with
   `rt_debug=2` their blue (written) must equal red (game mask). `rf_debug=1`: yellow
   reflection pixels keep the probe (`rf_debug=2` black there). `ls_debug=10`: yellow
   pixels had a D-LS ray aborted (that light kept the game's term; `ls_debug=2` removes
   nothing there that the game's own shadow did not). The
   300-frame `[core] guard:` line must list non-zero sun / D-LS / D-RF / AO counts.
   No pixel may get brighter than with `rt_max_candidates=0` where the game's term is dark.
3. `qa_zero=2`: the local lights vanish (torches, braziers in S3; street lamps in S14); what
   stays lit is sun, ambient, projector lights, fog and emissives. `ls_debug=1` counts the
   lights per pixel for comparison. `ls_check=1` with `qa_zero=2`: rtStats 81 (differing px)
   must stay at its usual floor.

## 13. D-EM data: emissive geometry capture (`rt_emissive=1`, 2026-10-27; data side only)

This is the data the emissive effects will consume: emission in GI and reflections, and
emissive light sampling. No effect reads it yet.

| Part | Where |
|---|---|
| Pass and draw identification, records, build, light list, logs | `layer/emissive.cpp` |
| The emissive scene (scene 1) | `layer/bvhsplit.cpp` |
| Effects-pass XFB | `layer/geocapture.cpp` |
| Format, radiance model, lookup | `shaders/rt/em_common.hlsli` |
| Light list | `shaders/rt/em_lights.hlsl` |
| Debug views | `core/emissive.cpp`, `shaders/rt/em_debug.hlsl` |
| Host API | ABI 8 `Bg3HostApi::rt_emissive` |

### 13.1 Which passes and draws emit

AUDIT_ENGINE_DATA 2.2: after the G-buffer, BG3 redraws its emissive meshes, depth-tested,
into the effects target (B10G11R11, render resolution, the G-buffer's D24S8). Compose adds
that target to the lit diffuse + specular. The sky (`c9f013fc`) and the clouds (`68d5b764`)
are drawn into it too.
- **Pass:** 1 colour attachment B10G11R11_UFLOAT + D24S8 at render resolution.
  - The SSS passes (`6f535075`, `d1669194`) and the forward pass have the same signature.
  - So a pass counts as an effects pass only when one of its draws is an EMI variant, and
    the effects target image is taken from those draws.
- **Draw:** its fragment shader is, in the asset DB, a material variant with `pass == 2`
  (EMI).
  - Counted, not captured: decal-class EMI draws (box volumes, `56d5eaaf`) and `*Skydome*`
    backdrops (`f12c2004`). `rt_em_backdrop=1` captures the backdrops, as a measurement.
  - Engine shaders and the shaders the DB does not know are counted too.

### 13.2 Radiance model (one record per draw)

`L(uv) = colour × intensity × glow(uv) [× base colour(uv)] [× global]`, in the effects
target's units. Every factor comes from the material's own data:
- **Colour and intensity:** the DB roles `GlowColour`/`GlowColor` and `GlowMultiplier`,
  read from the game's mapped constant buffer at record time. Unlit helpers use their base
  colour.
- **Glow:** the view bound at the `Glowmap` role's binding, `.r`. Families that switch to
  the texture's colour use `.rgb`.
- **Base colour, "BGM" templates** (Base × GlowMap): the shader multiplies the glow by the
  surface's base colour.
  - `_VT` templates use the virtual-texture albedo. The record carries:
    - the uv transform and the tile-set index (engine CB roles);
    - the tile-set constants (the SSBO entry at set 0 b12);
    - the indirection and cache views.

    `em_vt_albedo()` reproduces the game's Granite lookup, with the same arithmetic as the
    alpha test.
  - The plain templates use the base colour map.
- **Global scale, luminance-emissive materials** (DB flag): `× set 2 b0 [0]`.
  - That buffer is in device memory, not host-visible. The first version read it on the
    CPU; the read failed silently and left the factor at 1.
  - It is now bound as a buffer in the build and read on the GPU: one per build, and
    conflicting bindings are counted.

Families checked against their decompiled shaders (captures `20261026-*`):

| FS | Template | Game's formula | Modelled | First projected check (ours/game) |
|---|---|---|---|---|
| `20864dc6` | CHAR_BASE_GM (+9 aliases) | `c · glow.r · k` | exactly | 1.21 |
| `222986b8` | Base_GM_VT | `mix(c·glow.r, glow.rgb, floor(m1)) · m2` | exactly | — |
| `159a6211` | Base_GM_Pulse_VT | `mix(c·glow.r, glow.rgb, floor(m4)) · m5 · mix(m2, m3, cos(t·m1)/2+1/2) · global[0]` | exactly (pulse at record time; global on the GPU) | 3,318: the global factor was 1 (CPU read of device memory) |
| `7085eee7` | Base_BGM_MaskedShadows_VT | `VTalbedo.rgb · c · glow.r · k` | exactly (VT albedo) | 2.0: the albedo factor was missing (a mean albedo of about 0.5) |
| `aab75fed` | ENV_Tileset_MultipleTextures_BGM_MaskedShadows | `mix(mix(b2(uv·m0), b3(uv·m1), vc.x), b4(uv·m2), vc.y).rgb · glow.r · m3 · (1 − max(vc.x, vc.y))` | b2(uv·m0) · glow.r · m3, i.e. vertex colour vc = 0: the upper bound (flagged approximate) | 5.9: the base colour was missing |
| `48332370` | Helper/FlatColorShadowless | `c` | exactly | (black in the scenes seen) |
| `b86501b6` | Engine_Default_HLOD_GM_VT | three VT layer channels × m0..m2 × global | colour 1 × global (approximate) | not seen on screen |

The vertex colour of `aab75fed` is not captured: it selects which of the three tiles a
vertex uses and where it emits, and XFB records only the position and one UV. Expect
ours/game > 1 on its tiles with vc ≠ 0. If that matters, the fix is to capture a second
output.

### 13.3 Geometry and the build

**Capture.** Every VS is already XFB-patched. Each emissive draw is captured into the
emissive soup, a ring (`rt_em_xfb_mb`, 32 MB). It is converted to camera-relative world
after its pass, in the pass's command buffer, with that pass's camera UBO.

**Build per game submission.** The draws are kept per command buffer. When the game
submits the command buffers that hold a frame's effects passes:
- the layer records a command buffer of its own: one BLAS per draw, the TLAS, the light
  list;
- it submits that command buffer on the same queue, right after the game's batch
  (`em_after_submit`, under the queue lock).

This groups exactly one frame's draws. BG3 records the next frame's effects passes before
this frame's lighting, which broke the two earlier designs:
- **A build at the lighting**, from the draws recorded so far, mixed frames. It could
  include draws whose XFB ran after it, which caused the DEVICE_LOST at the first level load.
- **The draws of the previous frame index only** was safe, but split each frame's draws
  across two builds. That split is the main reason the first projected check covered only
  10.6% of the bright pixels (13.5).

**Latency and camera.** Consumers in later frames bind the latest build: one frame of
latency.
- The header holds the build camera's absolute position.
- `em_shift(camera now)` moves the current frame's lighting-space points into the scene.
  This is exact for static emitters.

**Every build is a full build.** No cached meshes, no refits, no compaction: tens of small
BLAS (36k triangles took 0.31 ms in the first design).
- Nothing depends on an earlier command buffer having run.
- **The emissive scene does not need `rt_bvh_split=1`** (main has the split off for now). It
  uses the split's per-scene machinery (`bvh_available()`), not its mesh cache.

**Our command buffers** are fenced, in a ring of 8. When none is free, the build is skipped
and counted. They inherit the game's image references, so the lifetime tracker keeps those
images alive.

**A separate TLAS, not an instance mask in the main one.**
- The emissive draws redraw meshes that are already in the main TLAS (coplanar duplicates).
  Mixed in, they would z-fight with their own opaque copy.
- Every existing ray (sun, GI, D-LS, D-RF) would also have to change its mask.
- Instead, a ray that needs emission traces the main TLAS, then asks the small emissive TLAS
  for a hit within ±eps of that distance (`em_lookup`).
- Light sampling uses the light list, which needs its own structure anyway.

### 13.4 Data format (`shaders/rt/em_common.hlsli`, set 3 by default)

| Binding | Content |
|---|---|
| 0 | emissive TLAS |
| 1 | InstInfo per instance (16 B): base, stride, **record index**, triangles |
| 2 / 3 | (unused pool) / emissive soup (the scene's camera-relative world, uv at +16) |
| 4 | `EmRec` per draw (256 B): colour × intensity [× pulse], mode (constant, × glow.r, × glow.rgb), flags (luminance, approximate, pulse, base map, VT albedo), glow and base map slots + uv transforms, VT constants and slots, FS hash, class |
| 5 | `EmLight` per triangle (80 B): p0, e1, e2, area, **mean radiance** (all maps at the mip where one texel covers the triangle), **power π·area·lum(L)** (one-sided Lambertian), record, instance, primitive |
| 6 / 7 | textures (glow maps, base maps, VT indirection; 256) / linear wrap sampler |
| 8 | debug-view statistics (host-read) |
| 9 | header: lights, triangles left out, instances, records, pre-exposure, frame, global bound, build camera; power sums per 256 triangles |
| 10 | the game's global scale buffer (set 2 b0) |
| 11 | VT tile caches (16) |

Units and limits:
- Radiance is in the effects target's units: what the game's emissive shader writes into
  the pre-exposed HDR buffer.
- Divide by the header's pre-exposure (lighting s1b13[0]) to get the lighting's units.
- The list is capped at `rt_em_max_lights` (65,536) triangles per build. Triangles past the
  cap are counted ("left out").

### 13.5 Results so far and coverage

Only the first design ran in gameplay: one 300-build window, at the foundry level load,
2026-10-27 05:28. Per build:
- 10.8 emissive draws, 36k triangles, 35.5k lights (429 over the cap);
- total power 1,586, 0 non-finite power sums;
- build 0.31 ms median (p99 0.34); effects passes with the capture 0.011 ms per frame.

The projected check is in the table in 13.2. 29% of the hits fall where the game shows
< 2% of ours, and coverage is **10.6%** of the effects-target pixels brighter than 0.05.

**Where the uncovered bright pixels come from.** Sources: the shader table, the decompiled
shaders and the foundry capture `20261026-183925`. Its units 52-59 are:

| Unit | Draws |
|---|---|
| 52 | sky `c9f013fc` |
| 54, 56 | clouds `68d5b764` |
| 57 | the EMI draws `48332370 20864dc6 aab75fed 159a6211 f12c2004` (21 draws) |
| 58 | `e0e3b441 f081b803 87ea6f1c` (4 draws) |
| 59 | `56d5eaaf` (1 draw) |

- **The build split each frame's draws across two builds** (the second design). Of about
  26 effects-pass draws per frame, about 16 were recorded during the build's own frame index
  and went to the next build. Each TLAS therefore held about half of one frame and half of
  another, which agrees with the 21 EMI draws of unit 57 against 10.8 captured per build.
  The per-submission build fixes this.
- **Unit 58, `e0e3b441` / `f081b803` / `87ea6f1c`.** They are not in the asset DB: no `.bshd`
  of Materials.pak hashes to them.
  - Their decompiled shaders write `vec4(colour, alpha)`, with scrolling noise textures, a
    radial `smoothstep` around a centre and `_14._m1.w` as the alpha.
  - They are alpha-blended VFX planes (magic and glow effects), not surfaces, so they are
    correctly kept out of the TLAS. They belong with the particle case (AUDIT_ENGINE_DATA R10).
  - In this capture the effects target's dumps after units 57, 58 and 59 have the same mean
    (0.000840): units 58 and 59 added nothing measurable.
- **Unit 59, `56d5eaaf`:** an emissive deferred decal (VFX_Decal_..._Emissive), a box
  projected onto the surface. It is not geometry (counted as "decal"), and it added nothing
  measurable in that capture either.
- **`f12c2004` Skydome mountains** (a backdrop, excluded). Its pixels have depth > 0, so
  wherever the mountains are visible (outdoors, through windows) they count as bright and
  uncovered. `rt_em_backdrop=1` measures that share.
- **Sky and clouds** are drawn where the depth is the far plane (depth 0 in the linear
  depth), so they are not counted.

### 13.6 Not done / unverified

- **The per-submission build has not run in the game yet**, and neither has any of 13.2's
  modelling (VT albedo, base maps, the GPU global scale). Every number in 13.5 is from the
  first design.
- Grymforge, Adamantine Forge and Water Queen's House were never run: **lava coverage is
  unverified**. Any EMI material is captured; whether lava is EMI, forward or VFX is open.
- `-Validation`: the one run (first design, with a debug view) added only two IDs,
  `VUID-VkImageMemoryBarrier-oldLayout-01197` and `VUID-vkCmdDispatch-imageLayout-00344`.
  Both came from the effects-target misidentification, fixed since.
- No `-Move` run.
- Candles and fire are forward-pass particles (AUDIT_ENGINE_DATA R10), not captured.

**Settings:** `rt_emissive`, `rt_em_max_lights`, `rt_em_xfb_mb`, `rt_em_log`, `rt_em_tol`,
`rt_em_fx_thr`, `rt_em_backdrop`, `rt_em_lights`.

**Logs:**
- `[em] last N builds`: census, left out, VT albedo missing, global scale conflicts,
  command buffers re-recorded unsubmitted, builds skipped;
- `[em] per build`: instances, triangles, lights, power, GPU cost;
- `[em] emissive families`, `[em] projected check`, `[em] ours/game per family`,
  `[bvh] emissive scene`;
- `rt_em_log=1` adds a line per build and per frame.

**Debug views:** `rt_debug=6` our emission, `7` the game's effects target alone, `8` a ratio
heatmap.

### 13.7 In-game round 2 (the gate, build `74b2338`, `rt_bvh_split=0`) and the fixes after it

The coordinator ran T1-T3 with the per-submission build. No device was lost in any of the
three scenes.

- **S1 `QuickSave_3784`** now opens on a beach and docks view, not the foundry. It had 0
  emissive draws (6 effects-pass draws: engine 1, unknown 3, backdrop 1, other 1), so it is
  not a valid test of the foundry content. Another foundry save is needed.
- **S5 Grymforge: the lava is not in the effects target.** The projected check counts 0
  bright effects-target pixels. A census of the trace's B10G11R11 passes (asset DB names):
  - effects target: `PPEmissiveSkydome`, clouds, `48332370` Helper, and 9 unknown VFX
    draws (`e176c25a` ×6, `e0e3b441` ×2, `12e2531f`);
  - **the forward pass** (55 draws): the lava is `56c5e5a0` **`Environment/ENV_Lava:
    StaticInstancedForward`** (alpha-blend, emissive), next to the VFX particles, the
    waterfall and the water.
  - The decompiled forward shader emits `Glow_Colour × Glow_Intensity × (1 −
    smoothstep(noise) + 0.2·…)`, plus lighting, with fog in alpha.
  - **Fix:** forward-variant materials with the emissive flag that are not VFX, water or
    decals are now captured ("forward" kind). They take the DB roles (colour × intensity:
    the upper bound of the noise factor) and are flagged approximate and forward.
  - Their reference is the forward-lit HDR target, not the effects target, so the projected
    check does not validate them and they are not calibrated.
- **S6 Adamantine Forge: the lava is in the effects target.** It is `1a81f41b`
  `Fluids/FLUID_Lava_Plane_Flowmap` (EMI), and coverage was **98.5%**. Problems found:
  - **Lava ×4.0 (731k px).** The decompiled shader is `blackbody(T) · m23 · (1, 2.5, 0)`, a
    LUT at set 0 b11 at `u = (m0 + (m1 − m0)·h·m22) / 12000`. The heat h comes from two
    flow-noise layers, vertex colour (z, w), distance and a rock mask, and is not
    reproducible from XFB data. Our record had colour 1 × intensity 1.
    - Now modelled as the blackbody LUT at a constant heat `rt_em_lava_heat` (0.5), × m23 ×
      (1, 2.5, 0), which gives the right hue and the game's own temperature range.
    - The scale is left to the **on-screen calibration**: the projected check runs without a
      view every `rt_em_check_frames` (16) frames. For approximate families only, the
      radiance is scaled by game/ours (√ per window, clamped to [1/64, 64]). Exact families
      are never scaled: their ratio stays the validation.
  - **`304be1c7` Base_GM_Pulse_FX ×0.049.** It has the Pulse_VT constant buffer and formula,
    without the global scale, and the pulse fixup applied to `159a6211` only. It now applies
    to every `_GM_Pulse` template.
  - **`159a6211` Pulse_VT captured 0 of 3, and `soup full` 8 per build.** The lava's 152k
    triangles (11 MB of XFB per frame) filled the 32 MB soup's per-frame third first.
    - The soup now **grows to demand** at a build: 4× the frame's demand, up to
      `rt_em_xfb_max_mb` (512), logged "[em] emissive soup grown".
    - Draws captured into the replaced soup are dropped once and counted.
  - **87,051 triangles over the 65,536-light cap.** The list no longer drops triangles.
    Above the cap, k consecutive triangles of an instance share one light, with k chosen so
    that everything fits:
    - the light has their summed area and power;
    - its radiance is the area-weighted mean;
    - its shape is the largest triangle;
    - `prim`, plus `flags >> 16`, gives the cluster's triangles, so a sampler can pick one
      by area.

    Total power is conserved. k is in header word 7 and in the log.
  - Build 0.33 ms median at 152k triangles, 0 non-finite values.

### 13.8 In-game round 3 (`b74c5ab`) and the fixes after it

**Results:**
- **S6 Adamantine** lost the device after 81 s, with only the main menu's Helper draw.
- **S5 Grymforge** lost the device after 169 s. Before that, everything was captured:
  - the forward lava and the Pulse families: 35 draws, 519k triangles, 64.9k lights;
  - coverage 100%, overall ratio 1.13, build 0.52 ms;
  - the calibrated VFX family `10086377` at about 1.0.
- **S8 Water Queen's House** ran clean: 37 draws, 171k triangles, build 0.38 ms. The exact
  families pass: CHAR_BASE_GM `20864dc6` 1.03-1.05, `9228b9cb` and `222986b8` Base_GM_VT
  1.04, VertCut `a7caa456` 1.31. Pulse families read 0. Coverage was 31% (2,548 bright
  pixels, 0.1% of the screen).

**1. Device loss: never build from unchecked vertex data.** The split's fault was traced to
BLAS builds over garbage, unwritten or non-finite vertices, which Vulkan leaves undefined.
- Every captured vertex already goes through `xfb_to_world`, which writes finite positions
  within 1e7 and marks each one w = 1 (valid) or w = 0 (collapsed).
- A new **sanitize pass** (`shaders/rt/em_sanitize.hlsl`) now runs in our command buffer
  over exactly the soup ranges the BLAS builds will read, before them.
- It collapses any triangle with a vertex that is non-finite, beyond 1e6, or not marked (not
  converted this frame) to the origin, and counts it ("invalid soup triangles collapsed").
- If its pipeline is unavailable, the build is skipped.
- A grown soup is zero-filled before its first use, and the effects passes' barrier now also
  waits for that fill.
- Only ranges captured by the draws of the submitted batch are built (as before).

**Images read a frame later.** Consumers read the set in the next frame's lighting, a frame
after the game drew with those glow maps and that effects target.
- `Bg3HostApi::rt_emissive` now takes the consumer's command buffer.
- The layer adds the set's game views, images and buffers to that command buffer's lifetime
  references, so the image-lifetime tracker keeps them alive until it has run. Before this,
  a texture the game freed after its own last use could still be read by our consumer.

**2. The Pulse families read 0.000.** Two causes:
- **`a201d825` Base_GM_Pulse_AlphaBlend** has its material constants at set 1 b1 (intensity
  +24, no texture-colour switch). The fixup read set 1 b0 (the engine constants). It now
  uses the DB's material-CB binding, and the intensity offset says which layout applies
  (+28: with the switch at +24).
- **`159a6211` Pulse_VT** (luminance-emissive) gets the global scale from set 2 b0, a
  per-frame buffer. The build reads it right after the game's batch, but the check (and any
  consumer) reads it a frame later, when that region holds other data.
  - The light-list pass now copies the value into the header (word 11).
  - Consumers use the copy.
- The census now counts material constants that are not host-readable. Possible causes of
  the two small 0.000 families (`38e91cf3` 80 px, `5d837ba1` 23 px) will show there.

**3. Where Water Queen's House's uncovered 69% comes from.**
- **The census of the trace** (`20261027-074655`):
  - all the effects-target content is unit 10's 34 EMI draws;
  - unit 11 (`f081b803`, a VFX plane) adds nothing measurable (same dump statistics).
  - Unit 10 includes the **Skydome mountains backdrop** (`f12c2004`, excluded) and two
    **alpha-blended** glow cards (`a201d825` Pulse_AlphaBlend).
- **Alpha-blended emitters are not in the depth buffer.** The check accepts a hit only at
  the pixel's depth, so such a card in front of a surface counted as a miss, and its own
  hits read 0 (the layout bug above). The check now also looks for an emitter in front of
  the depth.
  - Such pixels count as covered.
  - They are compared per family and their share is logged ("of which ... by an emitter in
    front of the depth").
- **The remaining misses** are:
  - the backdrop (`rt_em_backdrop=1` measures it);
  - characters' glowing parts at their silhouettes: the scene is the previous frame's,
    shifted for the camera but not for the character's own motion.

### 13.9 In-game round 4 (`502aa29`) and the fixes after it

No device was lost in any of the three scenes (`nv153=0`).

| Scene | Run | Coverage | Build median | Passing | Failing |
|---|---|---|---|---|---|
| S8 Water Queen's House | 135 s | 63.5% | 0.40 ms | `20864dc6` 1.02, `9228b9cb` 1.03 | `222986b8` 1.27; `159a6211` 0.000; `38e91cf3` 0.000; `5d837ba1` 0.000 |
| S6 Adamantine | 248 s | 99.8% | 0.43 ms | — | see below |
| S5 Grymforge | 129 s | 100% | 0.53 ms | lava 1.04 | VFX `10086377` unstable at 0.95-1.36 |

In S6 Adamantine:
- the whole lava pool is **`f298bf4d` `ENV_Base_MutipleUV`**, which reads 0.000 over 1.68M px;
- `1a81f41b` Lava_Plane reads 1.55 at scale 0.68, so it was still converging;
- `222986b8` reads 1.92 over 432k px;
- `159a6211` reads 0.000.

**1. Luminance-emissive families read 0** (`159a6211` and `f298bf4d`).
- Both multiply by the game's set 2 b0 [0], and the game rewrites that buffer later in the
  same batch.
- Neither binding it in the build (13.2) nor copying it in the light-list pass (13.8) saw the
  draw's value.
- **Fix:** the value is copied on the GPU right after the effects pass, in the pass's own
  command buffer (`shaders/rt/em_copy.hlsl`), into a slot of our own buffer (a ring of 4096).
  The record carries the slot (`EmRec.glob`).

**2. The lava calibration stayed at 1.0 in Adamantine.**
- The lava that covers the screen there is `f298bf4d`. It read 0 (cause 1), and a ratio
  that low is taken for a forward family and never calibrated.
- `1a81f41b` did calibrate (0.68 in the last window), but at most ×√ per window, and only
  over windows with ≥ 500 px.
- **Fix:** the full correction per window (clamped to ×4), with ≥ 200 px.
- **`f298bf4d` itself:** `glow(uv1).rgb · noise(scrolled uv0)·1.5 · m2 · global`. Its glow map
  reads the **second** UV, which XFB does not capture (the captured UV is the VS's
  lowest-location vec2 output, the first). It is now modelled as a constant (intensity ×
  0.75 × global, the noise's mean), and the map's mean is left to the calibration
  (approximate).

**3. The 0.000 character families** (`38e91cf3` 76 px, `5d837ba1` 25 px).
- The census reports **0 material constants that are not host-readable**, so the constants
  are read.
- `38e91cf3` is exactly `c · glow.r · k` (CHAR_BASE_GM, instanced). It is on moving
  characters, and the scene is one frame old and shifted for the camera only, so its few
  hits land on other texels of a mostly dark glow map.
- `5d837ba1` VertCut_VT also cuts by a vertex-colour mask, which is not captured.
- Both are under 0.01% of the screen: not fixed, reported.

**4. Water Queen's House coverage.**
- No emitter in front of the depth was found (0.0%).
- **Captured:** the census has 4 emissive decals and 1 skydome backdrop per build, neither
  captured. The trace's unit 10 holds every effects-target pixel.
- **Two measurement switches** split the remaining 36.5% between those sources:
  - `rt_em_backdrop=1` captures the backdrop;
  - `rt_em_decal=1` (new) captures the decals' boxes. Their front faces count as covered
    "in front".

  Both are measurements only: a decal's box is not the surface it lights, and the backdrop
  stands in for the sky.
- The rest are the moving characters of point 3.

**`222986b8` at 1.27 and 1.92** is not explained.
- It is an exact family by its decompiled shader (`mix(c·glow.r, glow.rgb, floor(m1))·m2`, no
  discard, UV at location 0).
- The ratio depends on the scene.
- The `rt_debug=6` and `rt_debug=7` dumps of the same view would show whether it is the
  pattern (mip or UV) or the level.

**Merge note:** my conversion-barrier change in `bvhsplit.cpp` (`502aa29`) is reverted
(`dd70778`), so the split agent's barrier audit (`c9297e3`) applies alone when merged.

### 13.10 Round 5 (`db5808a` results), offline fixes

**Decals are captured by default** (`rt_em_decal=1`). They were 55% of Water Queen's House's
bright effects pixels (coverage 63.5% to 99.6%). Their geometry is the projection box, so they
are flagged approximate and calibrated.

**1. `159a6211` Pulse_VT and `f298bf4d` still read 0.000.**
- The projected check's per-family sums were 32-bit, at 1/16 per pixel. Two failures follow:
  - a family's sum wraps past 2^32 / 16 (2.7e8): `f298bf4d` covers 1.7M px, so a mean
    luminance above about 160 wraps;
  - pixels under 1/32 round to 0.
- The sums are now 64-bit (a carry word) at 1/4096 per pixel.
- The copy pass is now checked in the log, not assumed. Every 300 presents,
  `[em] luminance scale copies` prints:
  - the copies dispatched and the copies owed but not recorded (the pipeline is missing);
  - per luminance family, the slot index its records use;
  - the value the GPU copy wrote, read back from our host-visible slot buffer, with min and
    max;
  - the records without a slot (no set 2 b0 bound at the draw).

  If the value is 0 while the game's pixels are bright, the copy reads the wrong buffer or
  offset. If it is non-zero while the ratio is still 0, the fault is downstream (the record's
  `glob` index or the shader).
- The copy's barrier now also makes the result visible to the host.
- Pulse templates: when set 0 b0 (the time) is not host-readable, the pulse factor is its time
  average, (m2 + m3) / 2. Before, the factor was missing altogether. This is counted as `pulse
  time not host-readable`.

**2. `222986b8`: 1.92 in Adamantine Forge, 1.03 in Water Queen's House.**
- The shader is exact.
- The scene-dependent part is the measurement. Adamantine Forge is the bright scene, so the
  32-bit game-side sum is the one most likely to wrap. That would make the ratio too large.
- Fixed by the 64-bit sums. Not yet verified.

**3. `a201d825` 1.41 and `a7caa456` 1.42.**
- `a201d825` (GM_Pulse_AlphaBlend) lost its pulse factor whenever the time was unreadable. A
  stable ratio of about 1.4 is the inverse of a mean pulse of about 0.7. It now uses the
  average.
- `a7caa456` is a VertCut template. Its emission is gated by a vertex-colour mask that XFB does
  not capture, so ours is an upper bound (a ratio above 1, as measured).
- VertCut templates are now flagged approximate, so they are calibrated rather than gated as
  exact.

**4. The lava calibration moved the wrong way.**
- The lava's heat flow swings its own ratio between 1.0 and 2.1 from one window to the next.
- A full step per window chased that animation.
- The scale is now 1 / the exponential average, in log space, of the family's ratio at scale 1:
  weight 1/4, with the first window setting it. That converges to the time average.

**5. Families under 200 px** are marked "under 200 px: not gated" in `ours/game per family`.

**6. The missing `rt_emissive=0` window** ("0.000 ms without (0 frames)").
- The log window counted builds. With `rt_emissive=0` there are none, so it never closed, and
  the next window mixed both phases.
- Windows are now `rt_timing_frames` presents. A window without builds logs `no builds in the
  last N presents`, with the effects passes' median with and without capture.
- The runs also need each live phase to last at least one window: `auto_interval=1200` and
  `-Traces 6`.

### 13.11 em6-WQ (`7f3f89a`) and the fixes after it

- Coverage 99.6%; total ours/game 1.073; build median 0.399 ms.
- **`159a6211` Pulse_VT still reads 0.000.** The copy runs and writes 0.000406901, constant
  over 2,700 reads.
  - The decompiled fragment shader multiplies its output by `_7._m0[0u]` (set 2 b0, a std430
    `float[]`) as is, exactly as our model does. No exponent and no unit factor sits between
    them.
  - So either the word is not the one the draw reads, or the game's bright pixels there come
    from another emitter.
  - The copies line now logs the source's offset (dynamic offset applied), its range, its
    first 4 raw words, and per family the mean luminance per checked pixel: ours, the
    game's, and ours before the scale. One run tells the two cases apart.
- **`7123ab1d` Base_Fresnel_GM_VT (about 10, over 10 px):** the Fresnel view-angle term is not
  modelled. It is flagged approximate.
- **Merge with main (`99d7a02`).** The sun replace took section 12, so D-EM is section 13.
  `rt_debug` 6/7 are the sun's, so the D-EM views are now 16 (ours), 17 (the game's alone)
  and 18 (ratio). Sections 13.1-13.10 keep the old numbers 6-8.
- **The `rt_emissive=0` cost** is now measured in its own session job, not as a live phase.

### 13.12 em7 session runs (`61006ba`) and the fixes after it

Water Queen's House (WQ) and Adamantine Forge (AF) ran without faults. Grymforge (GF) lost
the device during the load (nv153 = 2).

**Verdicts:**
- **Decals by default:** pass.
  - WQ coverage is 97.8-99.0%.
  - The decal family `6a988967` reads 0.88-1.00 once calibrated. Its scale is about 0.02: the
    projection box's radiance is about 50 times the game's mean.
- **`159a6211`: the copied word is the right one.**
  - The source's words 0-3 are s, 1/s, log2(1/s) and -8. For example 0.000406901, 2457.6,
    11.26 and -8; s × 2457.6 = 1.
  - So it is the game's pre-exposure. It varies with the scene and the view (3.0e-4 to
    1.4e-3), and a luminance-emissive material's physical intensity is multiplied by it.
  - Per checked pixel, the game's value is 0 in WQ (under 1/4096) and 0.0066 in AF. Ours is
    0 in both. So the family is dim in the game too, but ours is lower still: in AF, under
    3% of 0.0066.
  - The fault is on our side of the scale, in the level, the map or the pulse. The next run's
    record dump says which. It logs:
    - the record (radiance, mode, texture, flags, slot);
    - the share of hits where ours is exactly 0;
    - ours without the maps (radiance × scale);
    - the constant-buffer words, the time and the pulse.
- **AF lava `1a81f41b`:** 1.047 at the end of the 90 s check (scale 0.73). Pass. The later
  short jobs' windows are partial: 19 and 13 frames.
- **`222986b8`:** AF 1.93 (432k px, the largest family), WQ 1.26. The 64-bit sums did not
  change it, so the quantisation explanation is rejected. It is in the record dump.
- **`304be1c7` Pulse_FX:** AF 0.59-0.67 (894 px). It fails as an exact family and is in the
  record dump.
- **Capture-off cost: not measured.**
  - With capture off, a pass's draws returned before marking the pass as an effects pass, so
    its time was dropped as "not an effects pass". No "without" sample existed, so no
    window logged.
  - Fixed: `em_note_draw` marks the pass.
  - With capture, the effects passes cost 0.066 ms (WQ) and 0.096 ms (AF). The build is
    0.39 / 0.43 ms.
- **GF device loss:** it happened right after `[em] emissive soup grown to 128 MB` at the
  load.
  - The old soup was freed with the frame-based deferral only. The current descriptor set
    (and the consumers that read `emSoup` a frame or more later), plus passes recorded into
    it, could outlive that deferral.
  - Old soups are now retired, and freed only after kRing + kOwn (16) further builds.
  - A pass's conversion now runs on the soup its own draws captured into, not on the
    current one.

**Logs:** lists longer than the log's 2 KB line are split into parts. The cut-off per-family
lines came from this limit.

### 13.13 em8 runs (`b17eaaf`), the merge with main (`eb54bca`) and the fixes after it

There were no faults. `em8-gf-load` passed 2 of 2 runs, each with a soup resize, so retiring
the replaced soups holds.

**Verdicts:**
- **Capture-off cost:** in WQ the effects passes take 0.021 ms per frame without capture and
  0.065 ms with it, so capture adds +0.044 ms (≤ 0.1: pass). The build median is 0.39 ms (WQ),
  0.43 ms (AF) and 0.53 ms (GF, 519k triangles).
- **`159a6211` Pulse_VT: no model error; the family is dark in the game as well.**
  - Two materials share it.
  - In AF and GF the texture-colour switch is 1 and the intensity (+28) is 0. The shader then
    writes `glow.rgb · 0`, and our record is 0, which is correct. The game's 0.0032 per pixel
    there in AF comes from other emitters in the same effects-target pixels.
  - In WQ the switch is 0 and the intensity 1: radiance (0.87, 0.10, 0.003) × pulse. 97% of
    our hits land where `glow.r` is 0, and the game's mean per hit is under 1/4096 too.
  - So the ratio was 0/0.
- **`222986b8`: not a level error.**
  - In AF, 99.6% of the hits are black in both. The game's mean is 1.5e-4 per hit over
    432k px, so the ratio comes from a handful of texels.
  - In WQ it moves with the view: 1.28, 1.17 and 1.02 in the three jobs. That points to the
    mask's filtering (our isotropic mip against the hardware's anisotropic sampling), not
    the level. It is left as a residual.
- **`304be1c7` Pulse_FX:** 0.84 (AF, 894 px) and 1.04 (GF, 55.7k px). Pass.
- **`f298bf4d` ENV_Base_MutipleUV** had radiance 0: the database's intensity slot read 0. Its
  shader multiplies by the material CB's `_m2` (+16), which is now read directly.
- **Uncalibrated decals after a load:**
  - The first `em8-wq-on` window, a new session, had the decal family at 35×. The total
    power was 280 against 7.9.
  - A family never calibrated is now calibrated after 4 checks: about 64 frames, not 300.
- **GF lava `1a81f41b`:** 0.75-0.78 with scale 1.17-1.23 (180k px). It is still converging
  at the end of a run, so the EMA weight is now 1/2 instead of 1/4.
- **The gate is on bright hits** (effects-target pixels above `rt_em_fx_thr`), not all hits.
  Families that are dark in the game (`159a6211`, `222986b8` in AF) no longer pass or fail
  as 0/0.

**The emissive scene's own acceleration structures.**
- Main's split rework (versioned scene slots, TLAS slots and build records) conflicted with
  the 3-scene extension of `bvhsplit.cpp` everywhere. Its fault analysis also ties device
  faults to destroying AS objects at run time.
- The emissive scene therefore no longer uses the split. `bvhsplit.cpp` is main's version
  unchanged. `layer/emissive.cpp` `em_as_build` builds the scene itself:
  - one BLAS per build, with one geometry per draw (the sanitized soup, no indices);
  - a TLAS with that BLAS as its one identity instance;
  - per ring slot, BLAS and TLAS objects that are reused while they fit, so AS objects are
    created or destroyed only on growth;
  - one shared scratch buffer.
- `em_lookup` reads the geometry index as the draw's `emInfo` entry. `em_lights` reads
  identity per-geometry instances.
- The host ABI goes to **9**: `rt_scene_set` took 8 on main.

### 13.14 em9 runs (`6786dd6`, own acceleration structures) and the merge with main (`253e437`)

Five runs, with no faults.

**Verdicts:**
- **Validation:** pass.
  - `em9-af-validation` gives `VkShaderModuleCreateInfo-pCode-08737` ×2,
    `vkCmdDraw-None-09600` ×6 and `vkCmdWaitEvents2-pEvents-10788` ×6. All three are in main's
    baseline (`captures/validation.log`).
  - None names our acceleration-structure builds or our queue submission.
- **WQ:** pass.
  - Coverage 98.5% / 99.8%.
  - Total ours/game, window by window from a fresh load: 0.80, 0.93, 0.95, 0.96, 0.95, 1.03,
    1.05. The first-window 35× is gone (calibration after 4 checks).
  - `222986b8` 1.02-1.05 (≥ 200 bright px); decals 1.03-1.09.
- **GF:**
  - Lava `1a81f41b`: 1.23, 0.91, 0.89, 1.08, 0.91, 0.99, last 1.035. Pass.
  - `304be1c7` 1.04 (12.8k bright px). Coverage 100%.
  - **Build median 0.616 ms**, against my 0.6 ms criterion. It was 0.53 ms with the split's
    per-draw BLAS. The cause is 519k triangles in one BLAS with `PREFER_FAST_BUILD`. Marginal.
- **AF:**
  - Lava 1.02, 0.92, 1.14, 1.21. Pass. Coverage 100%.
  - `f298bf4d` is dark in the game (8 bright px of 1.68M) and ours is 0, since its intensity
    at CB +16 is 0. Consistent.
  - **`222986b8` 1.93 (381 bright px) and `304be1c7` 0.64 (237 bright px) fail as exact
    families.**
  - The same shaders pass elsewhere: `304be1c7` 1.04 over 12.8k bright px in GF, and
    `222986b8` 1.02 in WQ. In AF they are a few hundred bright pixels inside large dark
    families: 99.6% of `222986b8`'s 432k hits are black. Their game pixels also take the
    additive glow of other emitters (lava, particles).
  - The all-hit ratio mixes those effects. A ratio over bright hits only is now logged
    beside it (`bright`) and is the gate. The calibration keeps the all-hit energy ratio.
    This needs an AF run.
  - In the validation run (a fresh load, slowed by the layers) the lava's last window read
    0.74 while its average was still converging. Informational.

**Merge with main (`253e437`, SHaRC):**
- `common/host_api.h` defines `BG3RTX_HOST_HAS_RT_EMISSIVE`, so core/gi.cpp's SHaRC
  emission injection (`BG3RTX_SHARC_EM=AUTO`) compiles in. The core DLL carries
  "D-EM effects target", not "built without D-EM".
- The injection reads `Bg3EmissiveInfo::fx_view/fx_image/fx_layout/fx_width/fx_height`, which
  `em_scene` fills from the effects target seen this frame (fresh within 2 frames).
- The `sharc_*` shaders compile with the layer's flags: `-enable-16bit-types` and the SDK
  path.

### 13.15 Derived gates (QUALITY.md R0 addendum)

The 0.8-1.25 ours/game band, the 200 px cut and the 0.6 ms build limit were not derived.

**ours/game per family** (`scripts/qa/em_gate.py`): the tolerance comes from the ratio's own
noise.
- **Temporal noise:** the window-to-window std of ln r at the run's fixed view, measured.
- **Counting noise:** 1/N over the N bright px per frame. N is an upper bound on the
  independent ones, which errs toward failing.
- **Exact families:** σ² = s_w²/W + 1/N on the mean over windows.
- **Calibrated families:** σ² = s_w² + 1/N on the last window.
- **Pass:** |ln r| ≤ z·σ, with z two-sided at ALPHA_FW / families (Bonferroni, as
  `scripts/qa/thresholds.py`).

**Applied to em9:**
- **WQ:** pass.
- **AF:**
  - lava: pass;
  - `222986b8` 1.93 and `304be1c7` 0.635: fail;
  - the particles at 1.73 with N 42: fail.
- **GF:**
  - lava and particles: pass;
  - `304be1c7` 1.038: **fail**. Over 12.8k bright px the counting noise is 0.9%, so a +3.8%
    bias is real. Candidate causes: the mask filtering (our isotropic mip against the
    hardware's anisotropic sampling) and the B10G11R11 effects target. The target is
    unbiased per pixel, so the filtering is the more likely one.
- em10 logs the bright-hit ratios.

**Coverage (≥ 95%)** is review-only until a derivation exists.

**Build cost:** there is no D-EM entry in SETTINGS_DESIGN's split.
- Proposed: ≈ 0.7 ms (capture 0.044 + build and light list 0.44-0.62 ms, measured), out of
  Ultra's static headroom (≈ 5 ms) and pan headroom (≈ 2.5 ms).
- Until that entry exists, the cost is review-only.
- The planned optimisation is a BLAS refit while the draw list and the triangle counts are
  unchanged. Our command buffers are fenced and never dropped, unlike the game's.

### 13.16 Budget entry and the filtering hypothesis

**Budget (approved).** SETTINGS_DESIGN §5.4, row #8: "D-EM data" at 0.7 ms, taken from the
pan headroom.
- It is measured: 0.044 ms of capture plus the largest build, 0.62 ms in GF.
- The pan ledger, against the 15.4 ms line:

  | Item | Claim | Headroom left |
  |---|---|---|
  | Ultra pan | 13.4 ms | 2.0 ms |
  | SHaRC | ≤ 0.6 ms | 1.4 ms |
  | D-EM | 0.7 ms | **0.7 ms** |

- The static frame is ≈ 12.2 ms.
- The BLAS refit planned after em10 lowers the D-EM claim.
- The 0.7 ms is an optimisation target, not a gate (user directive: budgets never degrade a
  physically better result on Ultra). The derived ours/game gate decides the merge.

**Is the mask's filtering the bias** (AF `222986b8` 1.93 and `304be1c7` 0.64, GF `304be1c7`
+3.8%)?
- The game samples its glow maps with implicit derivatives: hardware LOD and anisotropic
  filtering. Our check samples at an isotropic ray-cone LOD.
- Replicating that offline needs the glow textures and UVs dumped and a BC decoder. Neither
  exists here, and the game's paks are not extracted.
- The replica therefore runs on the GPU instead, over the game's own bound texture,
  live-switchable in the check (`rt_em_filter`):
  - **The derivatives:** the UV's change per screen pixel, as the game's fragment shader
    has it. Each comes from the rays through the next pixel in x and in y, met with the hit
    triangle's plane (ray differentials, `em_uv_on_plane`).
  - **1:** the hardware's isotropic LOD, log2(max |dx|, |dy| in texels).
  - **2:** anisotropic filtering as the Vulkan spec's reference describes it: up to
    `rt_em_aniso` (16) trilinear probes along the footprint's major axis, at the LOD of
    major / probes (`em_sample_grad`).
  - **0 (default):** unchanged.
- The game's sampler state (its anisotropy level, any LOD bias) is not tracked. 16× is
  assumed: BG3's default, and graphicSettings.lsx has no override.
- The em10 jobs `af-filt1/2` and `gf-filt0/1/2` measure the three modes per family with
  `scripts/qa/em_gate.py`. Whichever mode removes the biases becomes the default for every
  `em_lookup` consumer. If none does, the hypothesis is rejected and the next candidate is
  the B10G11R11 target and the alpha blending of the overlapping emitters.

### 13.17 em10: the filtering test, overlap and the refit

Scored with `scripts/qa/em_gate.py`.
- **Exact families** are gated on the `bright` ratio: their per-pixel radiance is modelled.
- **Calibrated families** are gated on the all-hit (energy) ratio: only their mean is
  modelled, and it is what the light list carries.

**Filtering hypothesis: rejected.**

| Family | Scene | filter 0 | filter 1 | filter 2 |
|---|---|---|---|---|
| `304be1c7` (bright ratio) | GF | 0.998 | 0.861 (fails) | 0.992 |
| `304be1c7` (bright ratio) | AF | 0.417 | | 0.424 |
| `222986b8` (bright ratio) | AF | 1.005 | | 1.006 |

- The GF +3.8% and the AF 1.93 of `222986b8` were the all-hit metric, which includes the dark
  texels. On bright pixels both match.
- **The filter stays 0** (the ray-cone LOD). Filters 1 and 2 remain as diagnostics.

**AF `304be1c7` at 0.42 is independent of filtering.**
- The candidate: other emitters on the same ray, such as alpha-blended glows or particles
  over the pulse lights near the lava. The game's pixel holds both emitters; ours holds only
  the surface at the depth.
- The check now looks for a captured emitter in front of each bright hit (the front half of
  the ray). It leaves those pixels out of the bright sums and counts them (`overlapped`).
- Emitters that are not captured cannot be detected this way.

**Statistics.**
- With W windows the temporal spread rests on W - 1 differences. σ's degrees of freedom are
  now Welch-Satterthwaite, and the band uses the Student t quantile.
- Runs with fewer than 2 windows after the first are not evaluated. The AF jobs had only 2
  windows in 90 s.

**BLAS refit (the optimisation toward the 0.7 ms target).**
- A ring slot whose last build had the same geometries (keys: material, vertex and index
  buffers and offsets, draw arguments, triangles) is refit in place (`MODE_UPDATE`,
  `ALLOW_UPDATE`).
- Every `rt_em_rebuild` (30) uses of a slot it rebuilds instead.
- Our command buffers are fenced and always submitted, so the refit source is always built.
- Logged as `[em] BLAS: N refits, M full builds`.

**SHaRC with D-EM:** `sharc: D-EM effects target injected`, no fault (em10-af-sharc).

### 13.18 em11 (`0e57c99`)

There were no faults.

**Validation:** pass. `pCode-08737` ×2, `vkCmdDraw-None-09600` ×6 and
`vkCmdWaitEvents2-10788` ×6, all in main's baseline. None of them is on the acceleration
structure update.

**Derived gate (`em_gate`):**

| Scene | Windows | Result |
|---|---|---|
| WQ | 15 | all 5 families pass |
| GF | 11 | all 3 families pass: lava 1.055, `304be1c7` 0.999, particles 0.79 |
| AF | 8 | 5 of 6 families pass: lava 1.006, `222986b8` 1.005, `4d3f7dab` 1.006, `8bcd0aff` 1.014, particles 1.13 |

**AF `304be1c7` fails at 0.360** (bright ratio, 236 bright px; all-hit 0.632).
- **0 overlapped:** no captured emitter lies in front of its bright pixels, so the overlap
  explanation is rejected for captured emitters.
- **s_w 0.006:** steady across windows, while the pulse varies by ±18% over 6.3 s. The pulse
  phase therefore matches.
- **The pattern:** ours is relatively high where the game is dark (all-hit 0.63) and low
  where it is bright (0.36). That points to the glow mask being sampled in the wrong place,
  not to a wrong level.
- **The candidate:**
  - The fragment shader reads its UV at location 0 (vec2).
  - The XFB patch captures the vertex shader's lowest-location **vec2** output
    (`layer/spirv_xfb.h`).
  - If AF's vertex shader for these draws writes location 0 as a vec4 (packed UVs), a
    different vec2 is captured.
  - GF's draws of the same fragment shader pass at 0.999, so their vertex shader differs.
  - The next step is to log the captured UV location per family, against the fragment
    shader's UV input location.
- **Other unconfirmed candidates:**
  - the 16 unclassified ("unknown") effects-pass draws per build in AF, which can add to the
    same pixels;
  - coplanar overlays, which the front-half test cannot see.

**Refit** (`[em] BLAS` per 300 builds):
- WQ: 292 refits / 8 builds. Build median **0.095 ms**, from 0.39.
- GF: 96 / 204, 0.61 ms.
- AF: 35 / 265, 0.50 ms.
- The geometry key included the vertex and index buffer offsets. The game streams dynamic
  and instanced geometry through per-frame ring offsets, so the key changed every frame.
  - The key is now the material, index buffer, triangles, draw count, instances and kind.
  - A refit of another mesh with the same counts stays correct; only its BVH is worse, and
    the rebuild every 30 uses bounds that.

### 13.19 The UV-slot check

**Question.** Is the captured UV the one the fragment shader reads? The XFB patch
(`layer/spirv_xfb.h` `patch_vs`) captures the vertex shader's lowest-location float2
output. The fragment shader reads its texture coordinate at its own Location, which
can be a wider vector.

**In-game check (every pipeline).**
- `stage_io` parses every module's user interface: Location, Component and float component
  count.
- `uv_match` gives each VS/FS pair a verdict:
  - **ok:** the FS's lowest float2 input is at the captured location;
  - **VS wider:** the FS's UV location is a vec3/vec4 output of the VS;
  - **FS elsewhere:** the FS reads its float2 at another location;
  - **no FS float2**;
  - **unknown:** pipeline libraries.
- Logged as:
  - `[geo] UV slots ...`: totals over all patched pipelines, for every user of captured UVs,
    including EN2's hit materials;
  - `[geo] UV slot mismatch`: each mismatching VS/FS pair, the first 64;
  - a verdict per family in `[em] emissive families`.

**Offline, over every traced pair** (`scripts/qa/uv_slots.py`: 510 distinct VS/FS pairs from
all traces, all modules dumped):

| Verdict | Pairs |
|---|---|
| ok | 433 |
| FS without a float2 input | 77 |
| mismatch | 0 |

- In those 77 pairs the FS reads location 0 as vec4 or vec3, the VS writes the same width,
  and the VS has no float2 output. So nothing is captured as UV: a gap, not a wrong UV.
- **The capture's UV choice is never wrong in the traced scenes.**
- The `304be1c7` draws use VS `f4b35620d54abc6c` in both AF and GF. That VS stores its float2
  input straight into its only float2 output, location 0, and the FS reads `in vec2` at
  location 0. **The UV hypothesis is rejected for `304be1c7`.**

**What else is known about AF `304be1c7`.**
- It shares its glow texture (`s1b1`) with `159a6211`, uses the same VS, and draws 3
  instanced meshes.
- Coverage is 100%, so the geometry matches the depth.
- No captured emitter overlaps it.
- Its pulse phase matches.

**Next: per-pixel comparison.**
- `rt_debug` 19 and 20 show one family alone (`rt_em_dbg_fam_hi/_lo`): ours (19) or the
  game's effects target (20) at its hits, black elsewhere.
- The em12 jobs `af-view19/20` trace both for `304be1c7`. The images show whether the
  mismatch is a pattern (mask, UV transform, texture) or a level (constants).


## Current native-caster sun scene (ABI 19, default off)

`rt_sun_cascade_current=0`: source/record-order-correct sun-only TLAS clone,
private same-submit build insertion, cloned alpha/stamp, strict whole-ray coverage.
Design, tests and remaining material/LOD/volume gates: docs/SUN_CURRENT_SCENE.md.
This is not a passing exploration-shadow gate; zero certified pixels is INVALID.
