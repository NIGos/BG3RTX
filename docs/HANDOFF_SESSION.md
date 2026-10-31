# Session handoff - 2026-10-31: Hair trial installed; native Floor_B qualified

Full rendering execution plan M0-M6 remains active; do not claim completion.
User permits live diagnostics now, queue-only with finite scoped receipts and
unowned-game/USER_PLAYING guards. No need for user witnessing or manual approval
after adequate scoped checks. Be pragmatic: unrelated global gates do not veto
a demonstrated local fix. Track what the user has not yet reviewed in PROGRESS.

Main d4a1612 ABI 19 is merged via merge-green and pushed with codex/omm-bake.
Both DLLs rebuilt; 44/44 CTests, 527/527 runtime shaders and QA passed. Related
user mandate docs were preserved in history; unrelated disk-audit files untouched. OMM is wired offline,
not game-qualified. Ordinary 229 shader identities and 198 OMM compilations pass.
Latest M1 analyzer adds 27 passing tests; full M1 lacks automatic ROI/pose proof.

Desktop BG3RTX - Hair Scalp now targets hair-origin-live 047e105 (ABI19),
with rt_hair_physical_origin=1; native Hair/Scalp ON, OMM/current Sun OFF,
camera/cascade 1408/587 MiB. Old 859b879 and a9f8688 trials are retained.
User has NOT yet checked the capacity or physical-origin updates. User saves
are untouched. This is a private profile; all new preset defaults stay OFF.

Physical Hair receiver fix: separate 41.68 MiB image preserves virtual primary
and history data, but starts secondary rays at the same selected physical card.
Audited native class 5 is required; finite-light endpoints remain correct.
R6 completed 759s, guarded R7 completed 242s; no NVIDIA 153/dumps/build drift.
Same-run primary 97.410/97.414/97.409%; encoded Hair brightness .26649/.32127/.26837.
Actual world coverage 1177-1239 Hair pixels/frame; no strict common Hair across
all 48 due idle poses. Do not claim dialogue closure or unchanged non-Hair.
Stats-off RT 3.438 ms off / 3.409 ms on; no speedup claim. Complete K16 transport
and contact output are live verified. See PROGRESS and hair-origin captures.

Native class 0 parameter mapping and declared shared FresnelBlend eval/sample/PDF
are implemented offline with tests. They are not native BSDF equivalence, a
runtime hit provider or a complete reference renderer. The M3 owner is wiring
actual static VT inputs and captured basis into qualified secondary-hit shading.
M2 native S1 reference completed in 53 s: 835 draws, no capture overflow.
Floor_B POSITION matches 48 instances/20256 corners, max residual 3.22e-5.
ScaleZ acts on local X; observed Flip does not reflect POSITION. Target residual
2.10e-5 is inside the derived 2.20e-4 precision envelope. Old decorative-anchor
tests were not a transform oracle. Room closure and UV/material parity stay open.
Next: nearby qualified floor support and the actual runtime material consumer.

Queue child ownership/late-game guards, ordinal cross-PowerShell hashing and
module-independent SHA256 are fixed and tested through actual simulated runner.
M2 actual mesh/construction/material exports and shared CPU/HLSL BSDF remain
bounded offline increments, not room closure/world runtime/PT qualification.
No Ponytail. Never touch saves/backups/tags, force-push, clean junction trees or
hold results.md open. Read DISK_HYGIENE.md before any cleanup; owner holds remain.
Only merge-green integrates main; the prior tracked-doc blocker is resolved.

## Current checkout and confirmed UI correction

C:/Users/quali/Documents/Code Projects/BG3RTX. UI fix merged via merge-green onto
main 7b9c1e4 and pushed with codex/ui-contamination-offline. 17fb500 fixes recycled descriptor-set tracking: successful
new allocations clear old binding/dynamic-count maps before recording layout.
No key, shader, ABI or GPU work changes. Built bin/VkLayer_bg3rtx.dll at 15:56:50;
16/16 CTests and actual-function nine-case regression pass (old four failures).
The user reports blue blocks/flicker no longer trigger; movement/face stability
initially good. This is positive provisional confirmation, not every shadow issue.
Evidence docs/evidence/ui-descriptor-trial-20261030.json; offline original analysis
in docs/UI_CONTAMINATION_OFFLINE.md. Material-owner first-candidate freeze and RR
exposure-helper fallback remain unchanged, to isolate this successful trial.
Game/launcher were closed at integration. Merge gate passed 16/16 CTests,
107/107 runtime compute shader compilations and QA detector tests. Test-main was
refreshed to the same merge. No game was launched.
Do not rebuild or change live shaders/settings/HEAD while a game JOB is running;
new development below was isolated from the user's active build.

## New user report: two separate shadow problems

Save REFERENCE Test1 (user created it; never inspect or modify save files).
User is collecting more F10s from dialogues and camp for later offline analysis.
1. Static, hairstyle-specific face shadows; NOT flickering faces/old blue blocks.
2. Distant shadow changes, suspected trees at culling/LOD boundary; unproven object.
Screenshots and indexed captures: captures/dialogue-shadow-review-20261030/.
Report docs/DIALOGUE_SHADOWS.md and matching docs/evidence JSON.

Actual close-ups -161818 and -161845 show hard face/neck patterns in diffuse
before compose/DoF/RR/FG. Logs use current TLAS, zero waits/timeouts/skips around
those frames. No old whole-scene lag evidence. Native prepass FS 5a03e5b678b2d040
is CHAR_Hair:StaticDepth (1/3 draws), 9e77575a9f377ec5 is CHAR_Scalp:StaticDepth.
Conditional alpha/depth recipes are absent from alpha_fs.txt, so capturable cards
enter RT as OPAQUE. Always-discard filtering does not handle conditional cutouts.
Hair needs dual-mask/noise/root/tangent/view/dither/depth coverage; scalp needs
smoothstep/root-density. A plain alpha>=cut entry is NOT an exact implementation.
Latest user steering: exhaust package/catalog shader discovery BEFORE workarounds.
Missing alpha-table entries are not unidentified native shaders. Complete the
hair/scalp/pass/hash/formula inventory first, then faithful coverage in ALL
visibility/closest/origin queries. No fallback-first, hair-skipping or bias blur.
Per-ray caster identity and the dominant sun/local/box contribution remain unproved.
The prism's visible glow alone does not establish an actual native runtime light.

Current cascade settings: user casters 1; base alpha 2, dedupe 1, roofs_trust 0.
Unknown discarding cascade FS are assumed fade-only, but D16 inventory contains
missing three/two-texture environment cutouts, hair and skydome alpha. Dedupe can
switch a missing cutout from skipped to SOLID when its camera key disappears.
F10 lacks VB/IB/placement/dedupe/depth-layer records: no exact tree/caster pixel
was established. Do not lump nontexture fades or skydomes into foliage.
User-led later SAME-pose A/B: RT sun off/on, local off/on, then boxes if needed;
separate distant test cascade off/on, retention 0/4 only if justified. NOT run yet.

Sun already has radius 0.00459 rad half-angle (~0.526 deg diameter), uniform disc
sample per pixel, RR reconstruction, plus native-mask fallback in untrusted zones.
Penumbra ~9.18 mm/metre of caster-receiver separation: near hair can be subpixel.
Do not enlarge radius to conceal solid hair/cascade cards.
Solar audit docs/SUN_PENUMBRA_REVIEW.md: units/disc/seeds correct, but off-screen
guard retains native-dark sun edges. Six dialogue windows keep 94.6-98.1% of
native-dark/RT-lit disagreements; fully replace 0.3-1.3%, with RR active. Establish
actual caster-volume coverage before lifting trust; no radius/bias/blur change.

## OMM OFFLINE milestone - not runtime integration

CPU preparation merged via merge-green onto main e277548; main and branch pushed.
Gate: 19/19 CTests, 107/107 shaders and QA; test-main refreshed. No GPU OMM enabled.
Isolated C:/Users/quali/.codex/worktrees/p4-omm-domain/BG3RTX now includes main's
UI fix after e79c49b; old pre-integration baseline DLLs must still not be deployed.
- common/omm_classify.h imported from old 8d902d5; no production caller.
- common/omm_alpha_bounds.h validates immutable selected LINEAR UNORM8 mip planes,
  conservatively scans whole selected mips, checks missing/unsupported/budget/UV/LOD.
  CompleteCodeCoverage is INTEGER support, NOT GPU sampler/numeric proof.
- common/omm_bake_identity.h uses exact material/UV/geometry/descriptor/content/
  residency/sampler/evaluator/microtriangle identity plus submitted-work timeline
  completion. Live views and frame-age alone are insufficient for reuse.
- OMM SDK 1.10 is already at main external/omm-sdk-1.10.0; no download needed.
  Optional CMake BG3RTX_OMM_CPU_PROBE is OFF by default, enabled only in isolated tests.
  ACTUAL CPU baker: 18 constant cases, SIX known-state equality contradictions:
  SDK > makes alpha=cut transparent, native >= accepts it. Both nearest/linear.
- Follow-up fixed-cut conservative CPU configuration keeps <= UNKNOWN and >
  opaque: 1,532 UNORM8 equality/neighbor cases, ZERO contradictions, with opaque
  AND unknown outputs. Sacrifices all known-transparent acceleration; not valid
  for stochastic thresholds/GPU sampling. Four mixed texture tests use real
  descriptors/arrays: 25,740 sample checks, 66 equality points, zero contradictions;
  eight decoder bounds checks. CPU-only tests, not continuous/GPU proof.
- FULL isolated build/CTest 19/19, 197621 classifier, 398526 code-support, 65 identity
  checks pass. No shader/ABI/device/BLAS/runtime-setting changes, no GPU cost claims.
  docs/P4_OMM.md and evidence p4-omm-domain-20261030.json in that worktree.
Remaining: tight spatial support, GPU numeric/LOD/filter/VT proof, threshold adapter,
consistent EXT backend (KHR API differs), GPU bake/build/BLAS and completion lifetime.
Runtime priority now correct unsupported hair/cascade coverage before OMM acceleration.

Dedicated capture branch codex/native-coverage in isolated native-coverage/BG3RTX:
37299f2 adds six exact scalp shader recipes, bounded scalar-input capture and
native smoothstep/root/dither CPU reference, 178,522 checks. No runtime fix in
that commit. Agent continues opt-in runtime scalp hard-cut support without ABI
layout changes; native camera dither and full strand/depth capture remain separate.

## Other plan/state

CPU alpha-stream optimization already merged/default-on in main 47ac6a9; cost
capture 4.224 -> 2.973 ms. True uncapped focused fps still not qualified (offline
unfocused jobs sit at 30 fps). Full per-ray stats add large cost; never leave them
on for timing. FG input-copy preservation default 1; UI/FG full quality previously
unqualified, current user descriptor-fix feedback is stronger symptom evidence.
Hidden helper floor squares fixed rt_capture_discard 1, gate 29.4k -> 132/127 false
front pixels. Roof casters and box lights remain experimental user-enabled options.
EN2 material correctness/D-EM consumers and SHaRC v2 gates remain; SHaRC v1 exists
off by default, emissive capture exists off. P6 water is DESIGN only.
