# Native hair runtime coverage and depth

Default-off physical secondary-origin candidate: see "Physical secondary origin" below
(merged from the former HAIR_PHYSICAL_ORIGIN.md, now in [archive/](archive/HAIR_PHYSICAL_ORIGIN.md)).
It preserves virtual primary/history depth. CPU/shader qualification is separate
from the pending actual GPU/in-game gate and the user's darker-Hair attribution.

## Status: procedural CHAR_Hair/Pubes candidate, family and live gates remain open

`rt_native_hair=0` is default. Enabling requires a restart with `rt_alpha=1` and
`rt_bvh_split=1`. The latter requirement is enforced: legacy startup rejects this
mode, logs the reason and never mixes 24/88-byte geometry. Hot-enabling after a
24-byte startup is also explicitly rejected. No live build was deployed.

The current increment implements actual native CHAR_Hair coverage and procedural
primary depth. It is not an alpha-A surrogate. Console GPU gate passes; native G-buffer
comparison remains pending; do not call all hair or face shadows closed.

## Observed static CHAR_Pubes increment - 2026-10-30

Only two exact additional native recipes are enabled, under the existing startup
key; no new kind, stream stride or host ABI. Packaged/cached native source:

| Pass | FS | VS | Material / noise / mask, set 1 |
|---|---|---|---|
| StaticDepth | 2f2c46668da906a2 | 2f07f6463a175a20 | 1 / 4 / 5 |
| StaticDepthShadow | bf44216269789070 | e5aea62376fd3451 | 1 / 4 / 5 |

Both use UV 0, native sampler set 0/binding 7, world N/T/B at locations 1..3,
native W at 4; camera also supplies fade vec4 at 5. The VS use Camera._m2 and
negate clipY. Existing 88-byte capture therefore needs no extra attributes.
Camera material 32B maps noise 0/1 to canonical 2/3, thickness 2/3 to 4/5,
smoothstep edge/width 4/5 to 6/7 and root 6 to 8. Shadow material 16B maps noise 0/1
and root 2. Density is exactly 1: Pubes has no density material scalar.

AlphaRec kind 4 and all four shared query paths are reused. Existing unused
uint at byte 196 is subtype 0 for Hair/old records, subtype 1 for Pubes; Sun copies all 208B
before descriptor remapping and preserves it. This is needed for native camera
FMA grouping: Pubes fma(maskRoot,angular,noise) versus Hair's already multiplied
coverage times separate density. Native Shadow uses the same hard formula with
density 1. Camera fragment depth and physical thin-ribbon policies are otherwise
the same source equations as CHAR_Hair and reuse its procedural implementation.

31/31 CTests and 19,782 native capture/strand checks pass. Actual RTX 5090 fixture:
37 cases, including 10 Pubes cases, zero validation errors. Checks cover camera
depth, physical shadows, dither/fade, root suppression, held geometry and rebase.
One reference case distinguishes single FMA from explicitly rounded multiplication.
The full nontrivial-angle source expression uses the existing gamma128 FP32
envelope: independent normalization differed by 1 ULP in the initial fixture.
This is not a bit-exact GPU/native sampler precision certificate. RT-cone
footprints and the pending native G-buffer/live gate remain unchanged.
Other Pubes passes, including instanced and tetrahedron, are NOT enabled by hash
similarity. Their native sources remain the next bounded recipe backlog.
Fresh DXC proof: 95 ordinary entries byte-identical to fc46634; 95 capable variants
compile/validate. Evidence: docs/evidence/native-pubes-20261030.json.

## Native source and pass contract

Materials.pak native SHD8 SPIR-V was identified using exact layer FNV hashes.
The catalog has all 15 CHAR_Hair variants; all 15 VS and 15 FS bodies were recovered
offline. Source provenance: captures/native-hair-20261030/native-module-provenance.json.
Depth and shadow shader source recipes implemented here:

| Native pass | Static FS / VS | Instanced FS / VS |
|---|---|---|
| Depth | 5a03e5b678b2d040 / bf72b58803efe34d | 0527bb148e1d6c03 / 00e2829fe1dd1a70 |
| DepthShadow | 34951669ed4b3b0f / c39780dc8824981e | 8dddc0bc49e56efc / fb74490e1a527950 |
| DepthShadowTetrahedron | 00a9ecbb9c2b4ff8 / f99b997be5725de7 | a39086306bfc9c03 / 304b4f616f0c2f7b |

Static material/noise/mask descriptors are set 1 bindings 1/3/4. Instanced uses
0/2/3. Sampler is native set 0 binding 7, UV location 0. Depth reads 48 material
bytes; Shadow reads 32, with root/density at offsets 16/20 rather than Depth's
32/36. Captured records canonicalize these into material indices 8/9. Noise scale
and shift are material offsets 8/12. Shader queries use existing linear repeat,
trilinear isotropic RT-cone footprints, max sampler LOD 16. Original native raster
anisotropic sampling and precision identity are NOT claimed.

Deferred, Forward, Emissive, Velocity and Bake passes have separate bindings and
layouts. They were inventoried, not falsely entered as the Depth recipe. All six
supported VS provide the exact required strand outputs. Some other native passes
do not; their missing layouts are not labeled as Hair support.

## Capture and query implementation

Startup opt-in stream stride 88: clip/world position at 0, UV at 16, world N/T/B
at 24/36/48, native post-wind camera-relative position at 60, fade vec4 at 72.
Only smooth float32 location 1/2/3/4 vec3 and optional location 5 vec4 are advertised.
Camera Depth requires fade; native Shadow does not. Wrong/missing interfaces are
logged and counted. Hair remains dynamic: world strand frames cannot enter the
ordinary object-space static cache. Held draw snapshots preserve both 2D views
and their image identities, material, attributes and original camera metadata.

AlphaRec remains 208 bytes; kind 4 uses texture/cache indices for mask/noise 2D
views, canonical material48B at byte48, original VP64B at96, viewport16B at160,
near/far8B at176, ortho flag at184; header scale.xy contains native phase/mode.
The existing sticky capability makes all normal query entry points and original
scene buffers use compatible variants after key-off/core reload. Legacy helpers
honor the recorded stride, although procedural legacy support remains separate.

Coverage combines original and noise-shifted masks using the native centered-red
signed/max expression, not just max(alpha). It then applies root suppression,
the full interpolated nonorthogonal strand-frame projection and density. Native
front-face sign cancels in the absolute expression; 4,912 skewed-frame cases test
this invariant. Camera visibility/origin uses original native W, VP, viewport,
phase and interpolated fade, including native nonzero-mode dither. Single-sample
raster coordinates are reprojected to their pixel centers. Shadow queries use the
light-ray view direction and native shadow hard branch, without camera fade or
dither. No generic RT stochastic cutoff replaces either native branch.

## Native fragment depth and the intended RT material model

Native Depth writes `D' = (D*w + h*A)/(w+h)`, `A = near/(near-far)`, for perspective;
orthographic writes `D' = D + h/(near-far)`. This is a material-dependent virtual
surface, not an ordinary vertex offset or a proven negligible bias. With standard
native perspective projection `D=A+B/w`, the displaced point is
`Q=C+(P-C)*(1+h/w)`: it moves AWAY from the camera. The coverage view expression's
toward-camera shift is a separate shading calculation, not that geometric depth.

For finite normalized mask channels and defined smoothstep,
`0 <= h <= 0.01*max(material[4], material[5], 0)`. For positive vertex clip w,
component ratios `(P_i-C)/w_i` bound the whole triangle's ratio, permitting a
conservative widened AABB. Actual close-up material bytes were not in the F10
capture; no claim that h is below `depth_tol` is possible from existing evidence.

Implemented: procedural Hair AABBs and original 88-byte triangle sidecar, InstInfo
bit 12. An outward-rounded interval builder bounds the general captured projection
and takes the union with the physical base card. The last record words store a
normalized-mask/defined-height certificate at 188 and the AABB pool offset at 192.
Geometry-type changes force rebuilds; refits retain their original geometry type.
Bounds are regenerated and certified for each scene, including held snapshots.

The material model is explicit by query purpose. Camera-collinear primary checks,
screen debug and origin queries solve the native camera virtual point directly,
using hardware SampleLevel at the original base-card ray footprint. They retain
native camera coverage/dither and use the actual depth-buffer geometric normal
for the virtual primary surface. A primary request which does not match the
captured camera ray is UNKNOWN. The origin search can begin after the base card;
the virtual intersection may still be inside its search window.

Sun/local visibility, GI, AO, reflections and ordinary closest queries intersect
the physical thin ribbon, with ray-view strand coverage and the native Shadow
hard branch. Native Shadow has no fragment-depth output. A frozen main-camera
faux-depth sheet would introduce unrequested off-axis geometry; it is deliberately
NOT the physical secondary material model. This is not an unresolved fallback or
a fixed-step heightfield solver. Point sampling stays hardware SampleLevel;
native raster anisotropic/derivative identity is not claimed.

All four query loops accept generated procedural hits. Dominant-axis shared-edge
intersection uses precise products and a derived FP32 uncertainty band. Clearly
outside is MISS; unresolved edges are UNKNOWN, never false holes. A proven
xfb_to_world inactive marker is handled before inspecting original NaN outputs:
it keeps a finite point AABB for refit identity and is a known inactive MISS,
skipped before the software candidate cap. Unsupported finite bounds are different:
GPU scene-stamp byte 12 becomes nonzero, all four query modes become UNKNOWN or
aborted, and the primary gate cannot falsely pass on missing geometry.

## Offline validation

Full isolated ABI 19 build and 22/22 CTests pass. The ABI bump is the separately
approved Sun/OMM tuple extension, imported unchanged; Hair adds no ABI fields.
Native Hair CPU test: 19,734 checks,
including six exact parameter-binding recipes, reset-on-read-failure, signed mask
combination, root/density effects, 4,912 strand sign cases and XFB interface checks.
All 15 native VS patch and validate; nine expose strand frames, five also fade,
and all six actual supported pass interfaces meet their individual requirements.
Shared C++/DXC geometry math passes 310,883 checks, including 20,475 perspective
and 20,475 orthographic native-depth equations and 11,250 shared-edge, translated,
scaled and mirrored triangle cases against an independent high precision oracle.
Inactive identity and an origin window after the physical card are also tested.
95 fresh key-off ordinary shader entries are byte-identical to main92183e7;
95 capable variants compile and validate; the new bounds module also validates.
The separate windowless GPU probe passes 16 cases with zero Vulkan validation
errors on RTX 5090. It executes the actual bounds shader and all four query
functions: physical t=3, camera depth t=3.20000029, origin window t=0.1; native
dither/vertex phases, held pool, rebase, inactive NaNs and invalid-bound UNKNOWN
all pass. No renderer correction was needed; the initial fixture omitted the
rebase epoch word and was corrected. See evidence/native-hair-gpu-20261030.json.
This synthetic gate does not exercise game capture hooks or the DynSlot allocator,
and does not replace the native G-buffer primary-match gate.

Build the optional console target with BG3RTX_HAIR_GPU_PROBE=ON. Compile
hair_bounds.hlsl and tools/native_hair_gpu_probe.hlsl using DXC cs_6_6, Vulkan1.3;
compile the query once more with RT_SCENE_REBASE=1. Pass those three SPV paths
to native_hair_gpu_probe.exe. The executable disables implicit layers, requires
installed Khronos validation, creates no window and never launches the game.

No game jobs run. GPU cost, live depth match, image quality and temporal behavior
remain unmeasured. The other catalog families (Scalp, Pubes, ExtraUV, fur and lashes)
remain an explicit backlog, not implicitly covered by these six recipes.

## Prepared automated Test1 qualification

No live permission is currently granted. A future batch uses test-queue only,
both ABI-19 DLLs, startup rt_native_hair=1, rt_alpha=1, rt_bvh_split=1 and stable
RR/FG/light settings. The runner owns all configuration changes and restoration.
The user is not asked to edit INI, toggle flags or repeat comparisons.

Hold the affected hairstyle/face pose, then automate an opaque-coverage control
(rt_alpha_test=0) and native coverage (1) in the SAME run. This isolates coverage;
it is not a hot toggle of the startup vertex layout. Collect final/debug views,
face-region errors and the existing primary-ray check. RT must be active and
native records nonzero; invalid/missing state and bounds failures must be zero
for the targeted recipes. Static and movement match should agree within measured
repeat noise (historical aggregate baseline about 99.5% is not a face-ROI proof).

Include the distant-shadow scene separately. FG2 is the deciding moving case;
FG0 is reference only. Use existing pass timing with heavy stats disabled in the
cost window. No GPU cost, temporal quality or native capture-hook gate has passed.
Other native hair families remain separate unsupported recipe gaps.

## Physical secondary origin (`rt_hair_physical_origin`, default off)

A candidate correction, not confirmation of the user's darker/dithered Hair
attribution. No game/GPU check has been done. Native sampling and other Hair-family
gaps remain. The user has not tested it.

**Problem.** Native Hair camera depth moves the apparent surface behind its base
card. Camera primary queries reproduce that virtual surface; physical secondary
queries use the thin base ribbon and native Shadow coverage. `rtOrigin` launched
secondary rays from the virtual surface, so a camera-facing light could hit the same
card in front of that origin. The ULP offset removes floating-point
self-intersection, not a real material displacement.

**Fix.** `rtOrigin` stays unchanged for primary checks and history matching. Only
after the virtual primary match succeeds is the physical point/normal rebuilt from
the **same procedural instance/primitive**. Eligibility also requires raster
material class 5: native deferred FS `42a95e17ec3adc51` (Hair static),
`0ca5ee93d0f33b8c` (Hair instanced) and `7362bcdec4c0415c` (Pubes static) always
write `(5 | ((channel & 7) << 4)) / 255` to RT2.a. Depth-only FS do not write the
class. Other receivers keep the original origin. The triangle test starts at camera
zero with the same scene rebase. Ambiguous edges and invalid state keep the original
path and are counted.

- A full render-resolution RGBA32F image stores the unit physical normal and the
  positive physical radial distance (0 = inactive). Cost: `16 * renderWidth *
  renderHeight` bytes, **41.68 MiB** at 2560x1067 (one image, no ring; padding and
  GPU time unmeasured). A gamma16 FP32 envelope checks encoding in CPU cases; it is
  not a native/GPU error certificate. The ULP offset and terminator fallback are
  unchanged but anchored at the physical point. No bias is weakened.
- Sun, GI, AO, reflections and local visibility use the physical launch data.
  Finite-light segments choose direction/soft sample from the physical receiver,
  then keep the actual centre (also hard mode with nonzero radius) or the sampled
  target after the offset. The shared CPU/HLSL segment helper rejects an unresolved
  sphere intersection instead of inventing a hit or miss. Frozen local-light
  arithmetic and the procedural caster traversal limit are unchanged.
- Startup latch: capability is fixed at first resource init and kept across core
  reload. Restart with this key, `rt_native_hair=1` and `rt_origin=1`; then
  `1 -> 0 -> 1` works in one session. No host ABI or shader-loader change. GUI:
  Experimental in every preset.
- Layout: startup on adds set 0 binding 50; RF drops proven-unused binding 12 to stay
  within 32 descriptors. OMM bindings 10/11 stay in set 1. Key-off uses the frozen
  normal entry.

**Competing Scalp receivers.** Scalp FS `600446334bc7d478`/`87aab1f9603cf4a9` also
write class 5 with the same channel bits, and no G-buffer field identifies the
writer. So publication also scans triangles over the SAME `[t0, dist+w]` interval
(reference AS when OMM is on). Native camera Scalp records `9e77575a9f377ec5` and
`36eeb96ba83ea027`, unreadable identity, candidate-cap exhaustion or an automatic
opaque commitment keep the virtual origin. Non-Scalp candidates need a valid source
range, stride and record kind 1..5. This may reject valid Hair near other surfaces.
It closes the known admission hole but is NOT a raster-writer identity certificate
or proof of the hairline cause. A/B windows must be capture-complete: the preserved
trial had alpha-capacity fallbacks in 11 of 77 windows.

**Checks.** `native_hair_origin_test`, `settings_test`, the scalp shader check and
`tests/hair_origin_shader_test.py` (key-off SPIR-V byte-identical, feature modules
validated, binding 12 unused in all 16 RF variants). Last gate: 100 origin checks,
272 settings checks, 43/43 CTests after main 5f3e93b, 112 byte-identical and 112
validated modules; scalp gate 95/95; 31 Scalp-decision cases; 16 off-SPV identical
to 39e5475, 16 on-variants valid. Review fixed hard nonzero-radius targeting (48
local modules recompiled).

**Live gate (pending).** Both mode markers, RT active, nonzero matched-Hair and
available-origin counts, recorded unresolved counts (`rt_stats` counters 188/189/190).
Compare primary agreement with this scene's own off/off repeat, not an assumed 99.5%.
Compare Hair-local visibility and energy, unchanged non-Hair pixels, and report the
`rt passes` GPU cost. Missing RT or no affected Hair cannot pass.

## Dialogue validation plan (2026-11-02, prepared, not run)

Reuses the dialogue attribution jobs (tools/live_batches/dialogue_shadows; close-ups). Start: rt_native_hair=1,
rt_native_scalp=1, rt_hair_physical_origin=1 (latches: 88-byte vertices, origin image), rt_xfb_mb 1408 /
rt_cascade_mb 587 (the user's explicit 1024 / 427 x 88/64: an explicit value is not scaled by the stride). Base
phases set all three keys to 0 at run time, so hair is opaque as in the user's stride-64 game; `shot-hair-on`,
`shot-hairorigin-on`, `diag-rt-hairon`, `cost-hair` turn them on. All three keys are read per draw or frame after
the startup latch (alphatest.cpp:551, 566; core/gi.cpp:2566), so the A/B is inside one run.

Gates per job: valid run (stride 88 line, `[hair origin] startup auxiliary capability 1`, no `rejected`);
participation `[alpha hair] native coverage records` > 0 and `[alpha scalp]` records > 0 in the hair windows (a
dialogue without CHAR_Hair is no evidence, not a pass); invalid/missing native state 0, retired bounds-invalid 0,
unsupported interfaces reported; alpha made opaque 0.00 in every window (the 10-31 trial lost 28-31 cutouts per
frame to a full region); `[hair origin]` matched / available / unresolved in `diag-rt-hairon`; primary-ray check
match on against off-a / off-b of the same run; character-ROI energy ON >= OFF - spread (visibility only); the face
shadow shape of `shot-off-*` gone or kept in the PNG pairs; cost cost-hair - mean(cost-off-a, cost-off-b) with that
spread (prior 10-31: RT 3.44 / 3.41 ms, Hair sun visibility 0.150 -> 0.355 -> 0.164 off/on/off, CODEX_BRANCH_TRIAGE.md).
Still open after it: the other hair families (lashes, beards, fur, ExtraUV), the Astarion hairline band (class 5
shared by Hair/Pubes/Scalp), and the GUI default (Experimental until these gates pass in all three saves).

2026-11-02 product blockers fixed offline: water reflect/compose have scalp/hair variants (no water skip with
hair on), and new pipeline variants build on background workers (common/pipeline_async.h) instead of freezing
the render thread for about 60 s at the first hair frame. Checklist: docs/progress/worktree-agent-a78f48005e3b350bd.md.

## Dated entries moved from TRACK_D_RT.md (2026-10-30)

Newest first, as they were at the top of TRACK_D_RT.md. Later entries supersede earlier ones.

### Observed static Pubes coverage - 2026-10-30, default off

Existing rt_native_hair now recognizes static depth 2f2c46668da906a2 and shadow
bf44216269789070, using verified N/T/B/W/fade outputs and the existing 88-byte
capture. Native dual-mask/root/thickness inputs map into canonical kind 4;
byte 196 subtype preserves the different Pubes camera FMA grouping. Sun's full
record copies preserve it. No new key/kind/ABI/stride. All four normal queries
reuse procedural primary depth and physical Shadow ribbons. Other Pubes passes
remain unreviewed for activation; this is not complete all-family Hair closure.
31/31 CTests, 19,782 capture/strand checks, 37 GPU cases (10 Pubes), 0 validation
errors; 95 fresh ordinary shader identities and 95 capable compilations validate.
GPU cost/native G-buffer/live gate remain pending. See NATIVE_HAIR.md and
docs/evidence/native-pubes-20261030.json for exact source/layout/sampling limits.

### Native Scalp camera dither - 2026-10-30, default-off candidate

The earlier hard-only limitation below is superseded for camera FS
9e77575a9f377ec5 and 36eeb96ba83ea027. Existing `rt_native_scalp=0` default stays.
The same 208-byte kind3 record captures material8B, Frame phase/mode, original
Camera set0/binding2 VP at128, and native viewport. Marker float at record56
distinguishes new camera packets from held hard-only records. VP occupies96..159,
viewport160..175, Frame header32..39; no host ABI or stream stride change.
Scalp VS0e58570a2aa88c5a/f4b35620d54abc6c apply Camera._m2 and negate clipY;
the existing converter recovers that native position. Kind3 stays dynamic so
object caching cannot separate the position from its original camera packet.

Explicit camera primary/debug/origin queries project the triangle hit into the
captured viewport, use the original single-sample pixel center and native Frame
truncation remainder/noise, then native smoothstep/root/dither comparison.
Visibility and physical closest/GI/reflection queries use native Shadow hard
coverage, independent of camera dither. No RT hash, extra geometry attributes,
or fragment-depth displacement: the verified Scalp FS never writes depth.
The canonical capture helper is shared with Sun snapshots without camera table
mutation. Missing/nonfinite source state is rejected and counted. Existing RT
cone filtering remains an explicit policy, not native raster/aniso equivalence.

22/22 CTests; 178,810 Scalp checks; 95 ordinary SPIR-V byte-identical to 19fd488,
95 capable compile/validate. Windowless RTX 5090 GPU probe: 27/27 cases, zero
validation errors, including 11 Scalp cases through real 24-byte triangle queries:
native phases, B-channel root suppression, negative phase/viewport, held/rebase.
Scalp primary and physical t=3; Hair primary remains 3.20000029/physical 3.
Evidence: docs/evidence/native-scalp-camera-20261030.json. No game deployment or
live image/99.5% primary gate; GPU pass cost remains unmeasured. User check after
coordinated layer restart: rt_native_scalp=1, rt_alpha=1, same Test1 close-up pose;
require native camera records>0 and invalid state=0, then primary stats and face
region inspection. Keep other keys fixed; other Hair-family gaps remain open.

### Procedural native Hair geometry - 2026-10-30, default-off candidate

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

### Native CHAR_Hair coverage - 2026-10-30, default off

Actual native strand coverage is implemented in isolated `codex/native-coverage`:
startup-only `rt_native_hair=0/1`, validated88-byte output capture, six depth/shadow
hash mappings, dual masks/noise, material root/density, strand-frame view coverage,
original camera phase/fade dither and native shadow hard branch. Record208 unchanged.
21/21 CTests, 19,716 Hair checks, all15nativeVS patch/validate, 95 ordinary SPIR-V
byte-identical and 95 capable compile/validate. No live deployment/GPU work.
Full Hair is OPEN: base-card triangles do not reproduce native fragment depth.
Exact derivation, data layout, supported source hashes and procedural plan:
docs/NATIVE_HAIR.md. Do not claim a complete face-shadow or origin-match gate.

### Native scalp hard-cut runtime candidate - 2026-10-30, default off

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

### Native scalp coverage contract - 2026-10-30, offline increment

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
