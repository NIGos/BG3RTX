# P4 opacity micromaps: current isolated runtime candidate

Default rt_omm=0, Experimental GUI off in all presets. ABI remains 19; both DLLs
must travel together. Main/user build is unchanged. No current game/GPU validation
or measured acceleration/cost is claimed.

The isolated renderer now wires alpha bindings 10/11 (original micromap-free
reference and coherent content epochs), generation-matched scene selection and
all four query entry points. Every actual Core dispatch path registers its own
traversal command, including local lighting, reflections and SHaRC. Ticket-use or
feedback-tracking failure declines acceleration; an unavailable tuple is rejected,
not counted as a quality pass. Reference-only scenes still have stable build IDs.

Snapshot/Bound retain the exact build generation, so a recycled AS handle cannot
substitute another frame's scene. Reference selection is scoped per dispatch and
restores the original bound state. Failed queries do not mark stale GI, local,
reflection, primary or Sun outputs fresh; GI uses the current screen-space trace.
Specular hit distance is cleared when reflection dispatch fails.

The layout capability persists through key-off/core reload. A core-consumer bit
is cleared on unload: an older core receives baseline scenes. An ordinary pipeline
caught during capability publication also receives baseline, even if another
pipeline already enabled acceleration. A later unsupported device keeps the
superset layout with baseline/dummy bindings and zero proof. Fresh key-off paths
still select ordinary shader files.

Alpha proof validation is outside the alpha mutex, preserving BVH -> alpha lock
order; native compute bindings are restored after it. Scene retirement and ticket
release use actual completion, not frame age. Sun's separate scene has no OMM;
its self-reference and cleared proof cannot redirect rays into the camera scene.

Offline checkpoint: both DLLs build; current composition passes 40/40 CTests.
Per-command binding tests cover 58 cases: reference fallback, reused generation,
key-off and unwind restoration. Against d444e70, 229 existing shader entries are
byte-identical; 198 OMM wrappers compile/validate. Wrappers retain native DXC16-bit
flags. Short owned baseline extraction fixes Windows MAX_PATH without changing
the archived baseline source. No GPU execution follows from this shader gate.

OPEN: actual renderer descriptor/GPU traversal validation; nonzero useful known
coverage; same-ray reference violation counts; energy/movement/FG2 quality;
capture/build/traversal GPU ms and real residency. All-UNKNOWN or missing RT never
passes. Current code is an unqualified opt-in candidate, not a shipped speedup.
Only an explicitly authorized queue batch may test it, with automated A/B/settings,
input/captures/restoration. No manual INI/toggles are requested.

## Historical CPU groundwork

CPU domain preparation was merged to main e277548 through merge-green. The
historical entries below describe their original checkpoints and limitations;
their statements about absent production callers are superseded only in the
isolated candidate described above, not in main.

## Offline domain preparation - 2026-10-30

Current branch codex/p4-omm-domain starts at main 47ac6a9 in an isolated worktree.
The user's running checkout/binaries/settings remain unchanged. No live jobs are
queued or launched: live tests are coordinated in the root chat only.

Implemented, with NO production callers:
- common/omm_classify.h and the existing classifier oracle imported from 8d902d5.
- common/omm_alpha_bounds.h: validated immutable selected-channel LINEAR UNORM8
  mip planes; conservative WHOLE-MIP code hull across sampler/image-clamped LOD
  support, including both trilinear endpoints. Repeat/negative UV and scale cannot
  escape a whole-mip hull. Validates all declared levels, dimensions, spans,
  residency and arithmetic; checks max_texels before data reads. Missing/unsupported
  VT/sRGB-RGB/raw compressed data or exhausted budget returns incomplete.
- common/omm_bake_identity.h: exact geometry/UV/material/content/view/descriptor/
  sampler/precision/evaluator/policy identity, primitive/microtriangle/subdivision
  identity, and matching submitted-work completion timeline. Frame age/raw handles
  cannot certify readiness. Content/residency changes invalidate even live views.

CompleteCodeCoverage is an INTEGER support statement, NOT a GPU sampler/LOD/
conversion proof. There is deliberately no conversion into a classifier Interval,
no bake attachment, extension request, runtime key or shortcut around alpha_eval.
The initial whole-mip hull is broad; spatial microtriangle bounds are still needed
for useful coverage. VT is deliberately unsupported by this provider.

SDK availability is now checked: external/omm-sdk-1.10.0 already exists; no download
was needed. Optional CMake BG3RTX_OMM_CPU_PROBE (OFF by default) builds a CPU-only
SDK contract probe. With installed DLL 1.10.0, 18 constant UNORM8 cases found six
known-state disagreements with native >=: equality at 0,128/255,1, for nearest and
linear filtering. SDK defaults assign FULLY_TRANSPARENT at equality (strict >),
while native accepts it. All neighbor cases agree. This is a reproduced integration
hazard, not a passing runtime-alpha or Vulkan traversal gate. Do not bake defaults
unchanged or repair coverage by forcing unknown states to two-state opaque/transparent.

Follow-up CPU SDK experiment: set alphaCutoffLessEqual to UnknownOpaque, leave
alphaCutoffGreater Opaque and keep the cutoff unchanged. In FOUR-state mode this
withholds the unsafe <= transparent certificate while preserving > opaque states.
It does not change native alpha, shift a cutoff or certify equality. The actual
SDK probe exhausts every UNORM8 constant code at equality and adjacent float32
cutoffs in [0,1], nearest and linear: 1,532 cases, ZERO known-state contradictions,
12,192 known opaque and 12,320 unknown microtriangle states, zero known transparent.
The baseline still reproduces six contradictions in 18 cases. This conservative
configuration trades away all known-transparent acceleration; UNKNOWN must execute
the ordinary alpha predicate, never be treated as opaque by traversal flags.
It applies ONLY to fixed hard cut with the same sample. It is not valid evidence
for stochastic thresholds, general nonconstant textures, different filters/LOD, the GPU
baker or Vulkan traversal. No production caller or runtime integration was added.

Mixed-texture follow-up (offline CPU only): four fixtures use 4x4 UNORM8 zero/one
stripes in both axes, nearest and linear mip-0 sampling, with three primitives
per fixture at subdivision 0. Debug-only DisableSpecialIndices forces real packed
array decoding; the conservative alpha policy is unchanged. Each bake returns two
descriptors and two array bytes, mapping to one Opaque and two UnknownOpaque
primitive/microtriangle states. The crossing primitive MUST remain UnknownOpaque;
the solid primitive MUST remain Opaque, so an all-unknown result fails. Level 0
has one microtriangle per primitive and needs no guessed bird-order mapping.

An independent ideal sampler checks a 65-edge barycentric lattice, including
vertices/edges: 25,740 sample checks, zero known-state contradictions. Linear
fixtures include 66 exact alpha=cut points overall, without epsilon or cutoff
shifts. The solid/zero fixtures also have analytically constant full filter
footprints. Eight decoder tests cover exact byte ends, truncation, huge offsets,
invalid subdivision/format and null storage; descriptor bounds are checked before
pointer formation. These finite CPU samples are regression evidence for these
fixtures, NOT continuous-domain coverage, higher-subdivision bird-order mapping,
GPU rounding/filtering/LOD, stochastic alpha or Vulkan traversal certification.
No GPU/device/resource API, runtime integration or performance measurement was added.
Evidence: docs/evidence/p4-omm-sdk-cpu-mixed-20261030.txt. The incremental probe
compiled in isolation and returned 0; the prior full 19/19 run below predates this
increment, and was not repeated for this tool-only change.

Offline isolated FULL build / CTest: 19/19 pass (probe enabled). Individual oracles:
197,621 classifier checks; 398,526 alpha code-support checks; bake identity/completion
checks logged in docs/evidence/p4-omm-domain-20261030.json. GPU cost and speedup are
NOT measured. No rendering change or main merge is justified by these CPU results.

Remaining before a live OMM gate: tighter UV/filter support, GPU numeric/filter/LOD
precision certificate, VT residency/content provider, native >= and stochastic
threshold adapter with useful transparent coverage, one consistent EXT backend (not the different KHR object API),
micromap triangle indexing/bake/build/BLAS attachment and GPU-completion retirement.
Then same-ray known-state comparison with zero contradictions; only afterwards
foliage build/query/memory cost and visual checks.

The user's new dialogue/camp report takes runtime priority: unsupported CHAR_Hair/
CHAR_Scalp cutouts are captured opaque in the current renderer. That coverage must
be handled correctly before attempting to accelerate it with OMM. See root
PROGRESS_CODEX and captures/dialogue-shadow-review-20261030/capture-index.json.

The earlier classifier design/evidence below is retained as history; its paths
are superseded by the current common/ headers and its old four-test count is historical.


Branch codex/p4-omm starts from current main f98bfb2. The old branch
worktree-agent-a8b7cb486c1434ebd at 8b3b87a contains a design, no implementation.
Its mip-0/1 known-state rule is unsafe for today's alpha evaluator and is
superseded here. No extension/device/BLAS change or runtime key is enabled.

## Actual predicate

`shaders/rt/rt_common.hlsli` alpha_eval samples the texture/VT at the ray's
footprint. At levels <=1 it compares alpha >= cut. With stochastic alpha enabled,
k = saturate(level - 1); threshold = lerp(cut, alpha_hash, k). The 24-bit hash
grid includes zero. A constant-alpha 0.75 texture therefore is not universally
opaque, and constant 0.25 is not universally transparent, even if every mip is
identical. Statistical agreement cannot justify a wrong hardware known state.

Do not shift the hash by 0.5 as part of OMM. That changes the feature-off runtime
and the highest half-integer can round to 2^24 in float32. Any physical endpoint
fix must be a separately justified change with its own checks.

## Implemented proof primitive, not hardware integration

`layer/omm_classify.h` classifies an outward-rounded alpha interval against a
proven threshold interval. Opaque requires lower(alpha) >= upper(threshold);
transparent requires upper(alpha) < lower(threshold), because the runtime uses
>=. Incomplete, nonfinite or inverted bounds return UNKNOWN.

The legacy all-footprint stochastic threshold hull is [0,1], including rounding.
Hard-cut-only operation has [cut,cut]. This preserves current runtime semantics:
even alpha zero cannot be declared transparent for the full stochastic domain
because the native predicate accepts hash zero. Restricting to hard cut is legal
only when that restriction is actually enforced for every ray using the BLAS;
the current runtime flag/policy must be part of the bake identity.

The CPU test uses an independent predicate oracle over alpha intervals, cutoffs,
hard/transitional/coarse footprints, hash endpoints and fused/nonfused lerp. It
also exercises the old fractional-alpha counterexamples. It does not prove
texture bounds, GPU filtering or traversal correctness. No production path calls
the primitive yet. No speedup or runtime quality result is claimed.

## Required provider and hardware work

1. Derive alpha bounds over the entire microtriangle's UV and actual filter
   support, every reachable mip and VT fallback, channel, scale and transform.
   Two fine mips cannot cover arbitrary GI/reflection/shadow footprints. Include
   texture/filter precision in outward bounds; do not add a tuned epsilon.
2. Missing/streaming VT data is UNKNOWN. A known state remains usable only while
   its material, texture/image generations, sampler, cut policy, UV topology and
   residency/mip domain match. A retained descriptor is not proof of unchanged
   texture content. Animated/deformed geometry also needs proven UV stability.
3. Bake UNKNOWN whenever the provider lacks that proof. Never force UNKNOWN
   transparent/opaque, or drop existing alpha evaluation from unknown candidates.
   Expect limited known coverage under the legacy coarse stochastic policy;
   measure it before choosing an acceleration design, without weakening quality.
4. Port the useful old plan only after this provider exists: microtriangle
   indexing/encoding checked against the SDK, device capability negotiation,
   Vulkan micromap build sizes/usage counts, BLAS attachment/update/compaction,
   per-instance disable for mismatched identity and exact feature-off fallback.
5. Use actual recording/submission completion for lifetime and availability.
   Review the old frame-ring retirement proposal against current lifetime rules;
   do not certify an unsubmitted bake from a frame number.
6. In one run, compare hardware-known states against the native alpha predicate
   using the same ray, footprint, mip/residency and policy. Known-state violations
   must be zero. UNKNOWN coverage is honest fallback, not failure to be hidden.
   Check native traversal with validation before claiming an alpha-query saving.
7. Serial S3/S4 foliage and S5/S6 controls: same-session use off/on, geometry and
   alpha correctness first, then measured memory/build/query cost. Keep particles
   and unsupported families on the existing path. No user saves are modified.

P4 remains incomplete. The next concrete step is a sampling-domain provider and
its independent filter/mip/residency fixtures, followed by the GPU bake/backend.

Offline validation: full build and all 4 CTests pass. The classifier oracle
performs 197,621 checks over 142 known interval cases (93 opaque, 49 transparent).
The asset DB test skips when the unversioned database is absent in this fresh
checkout; the OMM oracle itself runs without SDK/data dependencies. No shader
or runtime rendering source is changed by this step.
