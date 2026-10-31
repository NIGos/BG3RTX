# BG3RTX progress

## Native Floor_B positions qualified; practical checks retained - 2026-10-31

Main d4a1612 is integrated and pushed: ABI 19, 44/44 CTests, 527/527 runtime
shader entries and QA passed. The installed personal Hair trial is 047e105;
the user has NOT yet reviewed its capacity and physical-origin updates.

A bounded S1 native geometry capture completed in 53 s, with 835/835 draws and
zero overflow. Floor_B export matches 48 native instances (47 held out), including
12 Flip cases: 20256 ordered corners, maximum POSITION residual 3.22e-5.
The selected tile residual is 2.10e-5 within its derived 2.20e-4 envelope.
ScaleZ applies to local X; Flip does not reflect observed positions. Keep this
scoped result; UV/normal/material equivalence and room closure remain open.
Asset checks: 57 passed, one existing skip. No renderer or GPU cost change.
Code: source 942113f, development import 33b14b0; not yet integrated into main.
Evidence: m2-foundry-mesh/BG3RTX/captures/m2-foundry/floor-b-native-oracle/native-position-oracle.json.
Next: nearby floor support and the static-VT runtime hit-material consumer.
No unrelated global match threshold will block a demonstrated local correction.

## Physical Hair origins kept and installed in the personal trial - 2026-10-31

Desktop BG3RTX - Hair Scalp now selects frozen hair-origin-live 047e105, with
rt_hair_physical_origin=1, native Hair/Scalp ON and camera/cascade 1408/587 MiB.
OMM/current Sun remain OFF. The prior capacity trial 859b879 and shortcut backup
are retained. The feature remains default OFF in all presets; GUI: Experimental,
Physical hair ray origins. User has NOT yet tested this update.

One Test1 FG2 same-run off/on/off + motion/cost job completed in 759 s. Virtual
primary match stayed 97.410/97.414/97.409%. Physical origins were available for
99.43-99.52% of matched Hair in 19 diagnostic windows. Encoded Hair luminance was
0.26649 -> 0.32127 -> 0.26837; sun visibility 0.1503 -> 0.3547 -> 0.1635. This is
reversible world-view support for removing false Hair self-shadow, not radiometric
energy or dialogue closure: only 1177-1239 Hair pixels/frame, and idle poses leave
zero common Hair pixels across all 48 frames. Non-Hair was not proven identical.

Source audit then added a necessary native class-5 receiver guard, so depth-near
skin/other materials cannot receive Hair origins. Accepted Hair arithmetic stays
unchanged. A short guarded job completed in 242 s, with active RT/FG, participation,
one complete K16 burst and no NVIDIA153/dump/build change. All three earlier
K16 bursts are complete too; the capture warmup/tonemap fix is now live verified.

Separate stats-off RT cost: 3.438 ms off / 3.409 ms on, no speedup claim. Extra
image payload: 41.68 MiB at 2560 x 1067. Guarded smoke's last RT samples span
3.352-3.434 ms. Keep the scoped fix and continue; no user-witness requirement.
Detailed measurements/contact sheet:
C:/Users/quali/.codex/worktrees/omm-bake/BG3RTX/captures/hair-origin-20261031/roi-analysis/hair-off-on-off.png
Same folder summary.md/report.json; parent live-summary.json and guard-smoke/.

Offline prerequisites also advanced: verified native class-0 parameters now feed
an explicitly declared shared FresnelBlend model (not native BSDF equivalence).
786570 parameter checks, 1700 independent scattering comparisons and numerical
furnace cases pass (max 0.999126497). Runtime hit-material acquisition and the
reference consumer remain unwired; no offscreen GI/PT completion claim.


## Consolidated on main f1402ec; scoped capacity fix kept - 2026-10-31

Merged only through merge-green and pushed main plus codex/omm-bake. ABI 18 -> 19;
both matching DLLs rebuilt. Required checks passed: 42/42 CTests, 415/415 runtime
compute shaders and QA detectors. New experimental feature controls remain OFF
in presets. The installed Desktop Hair/Scalp trial stays at verified 859b879 with
explicit camera 1408 MiB/cascade 587 MiB; its original fallback is retained.

Optional same-run Test1 cascade comparison R5 supports the fix: rotation primary
match 95.930% at 160 MiB -> 97.493% at 587 MiB, near static 97.472% at 587 MiB.
FG was healthy on every captured frame, presentation about 42 fps; settled cascade
overflow was zero at 587. Keep the demonstrated correction, without demanding an
unrelated global 99% threshold. User witnessing is not required for advancement.

R5 flash counts/contacts are UNKNOWN: its old producer missed the first final
image, and the reader rejected a legitimate separate tonemap command recording.
The small consolidation now explicitly omits one warmup and validates 16 complete
retained frames; mixed lighting records/missing finals still fail. Host build,
21 Python checks and epoch regression pass; the corrected capture path has not
yet had a live rerun. No false zero-flash claim and no large repeat matrix.
Evidence: omm-bake/captures/vegetation-regression-20261031/r5/analysis/summary.md.

NOT YET TESTED BY USER: capacity update and this integrated program base. Original
Hair/Scalp trial was user-tested with improved faces, darker/dithered hair and
vegetation regression. Physical Hair secondary origins remain in isolated offline
development, not installed. OMM/current Sun remain unqualified and OFF in the trial.


## Hair/Scalp capacity correction installed; user review pending - 2026-10-31

The existing Desktop shortcut BG3RTX - Hair Scalp now selects isolated candidate
hair-capacity-fix 859b879. Native Hair/Scalp remain ON; OMM/current Sun remain OFF.
The prior a9f8688 trial and shortcut backup are retained. Main code/binaries and
user saves are unchanged. Ctrl+Shift+F10 is restored through private dev=1.

Confirmed regression: 24 -> 88 bytes per captured vertex exhausted the unchanged
camera and legacy cascade buffers. Native88 reservation feedback was corrected;
explicit capacities are now camera 1408 MiB and cascade 587 MiB, restoring prior
vertex capacity (+2305 MiB total across one camera and three cascade buffers).
Two presented FG2 Test1 runs completed: 485 s and 450 s, seven phases each,
including slow/fast mouse rotations both ways. The second run has ZERO forced
alpha, camera drops and cascade overflow in every measured steady window.
RT and FG remained active; measured rotation cadence 45.4-48.2 rendered fps,
displayed/rendered about 2, no interior FG toggles. Static RT cost without the
primary diagnostic: 3.397 ms median. This is not total frame time or a controlled
speedup claim. Queue reports zero NVIDIA 153 events/dumps and unchanged builds.
The first run had a recovered startup stall before measurement, retained in evidence.

Keep the capacity correction. It fixes demonstrated geometry loss; full flicker
closure is not claimed. Global primary match is 97.410% static and 96.247-97.007%
rotating in this scene; full P1/M1 is unqualified, but that unrelated threshold
is not a veto on this capacity fix. Optional burst evidence is being kept small.
Detailed numbers: omm-bake/captures/vegetation-regression-20261031/r3 and r4.

User review status: UI descriptor fix confirmed; original Hair/Scalp trial reported
improved faces with darker/dithered hair and vegetation regression. The capacity
correction installed today is NOT YET TESTED BY THE USER. Physical Hair secondary
origins are in offline development; OMM and current-Sun remain unqualified and
OFF in the trial. RTX Hair is a scoped feasibility/prototype plan, not installed.
The user explicitly permits autonomous advancement after adequate scoped checks;
manual witnessing/confirmation is not an implementation or release prerequisite.


## Hair trial regression: capacity fix ready; live setup blocked - 2026-10-31

User reports improved faces but new vegetation flicker and darker/dithered hair.
OMM was OFF. The supplied trial log confirms capture exhaustion after the global
vertex stride increased from 24 to 88 bytes with the same 384 MiB allocation:
alpha demand peaked at 660.8 MiB; the last window forced 41.57 alpha draws/frame
opaque and dropped 30.41 opaque draws/frame. This is a confirmed coverage defect;
it does not prove every remaining visual symptom has the same cause.

Native88-only feedback correction 1e324af removes forced-alpha bytes from the
opaque reservation. Explicit diagnostic allocation 1408 MiB restores previous
vertex capacity (+1024 MiB in one camera buffer). 0a08191 rejects unsupported
storage ranges and failed binding/mapping. Both DLLs build; 42/42 CTests pass.
Private trial Ctrl+Shift+F10 was gated by dev=0, not removed; dev=1 is restored.

The user authorized live testing in this chat. Original trial a9f8688 is retained;
frozen candidate hair-capacity-fix c22a887 has Hair/Scalp ON, OMM/current Sun OFF.
One bounded Test1 FG2 static/rotation capacity diagnostic is prepared. No game
has launched: first setup rejected an absolute job tree (corrected to relative),
then the claimed child failed authorization validation. This run is INVALID,
not a renderer failure. Cross-process authorization is being corrected offline.
Independent review closed a separate late-unowned-game launch race; 33 synthetic
child authorization checks pass, but actual cross-PowerShell setup still needs proof.
No measured candidate GPU cost or live quality claim. Dark hair/dither remains open.


## Hair/Scalp personal trial installed - 2026-10-30

At the user's request, installation only; no agent game launch. Desktop shortcut
BG3RTX - Hair Scalp points to the frozen isolated a9f8688 candidate (ABI 19,
both matching DLLs). Native Hair/Scalp are on in its private user settings; OMM
and current-cascade Sun are off. Other user choices were copied and preserved.
Launcher dry-run passes and selects the trial layer. 39 CTests pass; prior native
Hair/Pubes GPU fixture37cases passes. Actual user quality/cost is unqualified.
Main binaries/source/prefs are unchanged. Rollback: use the original tool.
Candidate: C:/Users/quali/.codex/worktrees/hair-user-trial/BG3RTX.
Manifest: captures/trial-package.json in that candidate. Do not rebuild/edit it
while the user's game runs. Development continues in the separate omm-bake tree.

## Offline materials and bounded architecture advanced - 2026-10-30

40/40 CTests and 54 asset tests pass (one existing zstd skip). Native construction
material a1a20440 uses an exact vertex-color branch, not texture blending: Green
when interpolated G > 0, else Red when R > 0, else Base. Nine enabled Texture2D
resources and their native FS/VS/parameter layout are joined; 230 new decoder
checks pass. Actual view/color/filter/residency and runtime provider remain open.
Existing F10 extension retains exact VT sampled-view metadata; no new capture run.

A real floor (5,114 triangles) and wall (9,502) were exported and placed, followed
by a 730-triangle supporting corner. The latter supports only two of 16 sampled
low board vertices; room floor/closure is NOT qualified. Adjacent Floor_C/wall
are explicitly not counted as support. Seven of nine active Floor_B scale tests
fail the declared precision envelope; no tolerance increase or guessed flip was
introduced. Exact evidence stays in m2-foundry-mesh/captures/m2-foundry/.

Native Fur source/capture/scalar helpers pass 60,048 checks, with no runtime hook.
Descending native SmoothStep defaults stay UNKNOWN until proven; Fur has no Hair
virtual-depth displacement. Historical retention review releases zero raw bytes:
52 capture directories / 17.88 GiB remain needed for failed or unresolved gates.
Source docs/evidence/historical-capture-retention-20261030.* list exact holds.

Next: current-Sun diagnostic counter correction and strict phase gates; complete
material provider evidence; shared cache-free BSDF math. The user tests the frozen
Hair/Scalp installation independently. No development touches that candidate.

## OMM renderer selection wired offline - 2026-10-30

Default-off rt_omm now reaches the renderer in the isolated candidate: alpha
reference/epoch bindings, generation-checked fast/reference tuples and per-command
ticket/feedback registration in GI, Sun, local, reflection and SHaRC dispatches.
Old-core/key-off/reference cases retain baseline scenes. Failed dispatches no
longer claim stale output as fresh; GI falls back to the current screen trace.
ABI stays 19; both DLLs build. Final OMM shader gate: 229 existing entries
byte-identical to d444e70; 198 new entries compile/validate. Current composition
passes 40/40 CTests. No agent game/GPU run or cost claim.
Useful known coverage, same-ray comparison and live quality/energy/cost remain
OPEN. All-UNKNOWN/missing RT cannot pass. See P4_OMM.md for the exact contract.

## Rendering program: verified isolated composition - 2026-10-30

Full M0-M6 plan remains active. No live grant exists. Main stays at 92183e7;
its uncommitted planning documents and the user's build are preserved. Isolated
codex/omm-bake source checkpoint 7259c21 combines native Hair/Scalp/Pubes, current
four-cascade Sun, OMM producer/BLAS, Experimental GUI and initial asset/material
increments. ABI remains 19; both DLLs build together. All new presets stay off.

Gate: 36/36 CTests and QA pass; 271 settings checks. After correcting double
rebasing of current-Sun rays, 219 existing shader entries are byte-identical to
75bb833; 10 new entries compile/validate. Retained Sun ring copies now require
the source buffer's actual TRANSFER_SRC usage. Independent review found no other
concrete combined-Sun blockers. Native GPU/game quality and pass cost are NOT
measured. OMM descriptor/Core selection remains unwired: no acceleration claim.

M0: scoped explicit batch receipts, cancellation during single/command execution,
nested QA injector cancellation, blocked-inbox skipping and runtime C# source
freezing are composed. Scratch tests pass: 28 cancellation, 27 permission, 26
window policy, four phase cancellation and two C# identity checks. The worker's
full fake queue also passed (14 jobs / 15 runs). No real inbox/permission changed.

M2: 40e57c4 exports one real Foundry board and its enabled VT (64 tiles,
1024 x 1024 RGBA). 7259c21 adds full affine placement, inverse-transpose normals
and mirrored winding: 5,728 LOD-0 world triangles; other LODs stay alternatives.
44 asset tests pass, one existing zstd skip. World/XFB agreement and room closure
are NOT qualified. Actual artifacts remain in the m2-foundry-mesh checkout under
captures/m2-foundry/. Nearby construction reconstruction is the next asset step.

M3a: 56ab2de decodes audited Base_AlphaTest_VT fields after native sampling;
164,188 CPU checks pass. Normal swizzle and material parameters follow actual
native shaders. Coverage remains UNKNOWN; actual view/color interpretation,
VT sampling/residency and the runtime provider are still missing. Existing F10
view metadata is being extended only to resolve that concrete missing evidence.

Next offline work: OMM reference/descriptor selection; next observed Fur coverage
recipe; bounded room geometry; native sampled-view/material proof. Future live
batches automate all keys, A/B, input, capture and restoration. No manual INI or
comparison is requested. Main integration waits for merge-green and clean main.
Evidence: docs/evidence/rendering-composition-20261030.json.

## Native material BLAS and same-frame scene clone prepared - 2026-10-30

Both ABI-19 DLLs build; 32/32 CTests pass. Native static draw capture now feeds
material-specific OMM baking, owned post-VS snapshots, attached BLAS and a
separate GPU-cloned TLAS. Original Mesh.as and baseline instance order/material
records remain intact. GPU clone preserves late native matrix updates and changes
only selected AS addresses. Cache owner release supports pending/replayed commands.

RTX 5090 combined probe: six rays with zero contradictions; three private
snapshots / 216 exact copied bytes; two cloned instances with 120 untouched bytes
and two late matrix updates preserved; 14 validator cases, eight feedback cases,
2,176 UNORM / 8,704 BC endpoint checks; zero validation errors. Evidence:
docs/evidence/omm-native-clone-20261030.txt. This is not a live BG3 quality gate.

Retired-slot invalidation/rebake logic is prepared, with matching generation,
recording, dispatch/storage limits and AS scratch dependencies. The actual
alpha descriptor/reference bindings and Core accelerated-scene selection are
NOT wired yet: OMM is not active in rendered BG3 queries. rt_omm stays default0;
useful hardware-known coverage, same-ray violations, live energy/cost and FG2
gates remain OPEN. No game launch, live settings change or GPU ms claim.

Hair candidate now includes the two verified static Pubes recipes under existing
rt_native_hair, with source-faithful FMA ordering, no new key/kind/ABI/stride.
37/37 production GPU cases pass; 95 ordinary shaders remain byte-identical.
Test1 face/strand quality and temporal/cost checks await an explicitly authorized
automated batch with both ABI-19 DLLs. The runner sets startup keys and keeps
RR/FG/light settings fixed; FG2 is deciding. No manual INI/A-B is requested.
Other native families and full solar coverage remain open; defaults stay off.

## Verified static Pubes coverage added - 2026-10-30, default off

Two observed static Pubes shaders now use native dual-mask/root/strand coverage,
camera dither and procedural depth under existing rt_native_hair. No new key,
ABI or vertex stride. 31/31 CTests and 19,782 capture/strand checks pass; 37 actual
GPU cases (10 Pubes), zero validation errors. 95 ordinary shaders byte-identical,
95 capable compile/validate. Other Pubes passes and live/native G-buffer quality
remain open; no game deployment or measured GPU cost. Exact supported hashes and
evidence: docs/NATIVE_HAIR.md, docs/evidence/native-pubes-20261030.json.

## Combined candidate: native material capture and coherent OMM guards - 2026-10-30

Isolated codex/omm-bake now includes native Hair plus Scalp camera dither,
immutable-sampler/source factory, paired before/after image-write epochs, and
ticket-owned attached BLAS. Main/user build is unchanged. No game launched.
Both ABI-19 DLLs build; 31/31 CTests pass. Native capture now runs at eligible
static draws, including cached draws; actual accelerated scene attachment is
still pending. Do not mistake captured metadata for active OMM acceleration.

Real RTX 5090 probe traverses the production ticket-owned BLAS: six rays, zero
contradictions; BLAS is destroyed before its micromap after actual completion.
Fourteen counter-phase states and fourteen scene-validator cases pass, along
with 2,176 UNORM and 8,704 BC endpoint checks; zero Vulkan validation errors.
Evidence: docs/evidence/omm-coherent-attached-scene-20261030.txt.

All four query entry points can choose the original, micromap-free reference
scene when the GPU content proof fails. Readers use coherent atomic snapshots
and device memory barriers around active-write/epoch checks. Native image/RT
synchronization remains mandatory: these counters cannot repair a concurrent
image read/write race. The shader branches are not enabled by a production
layout/caller yet. Native scene clone, descriptor wiring, nonzero useful known
coverage and live same-ray/cost gates remain OPEN; rt_omm stays off by default.

Before the final coherent-reader change, 190 ordinary/capable entry points were
byte-identical with RT_OMM_SCENE off and compiled/validated with it on. The final
coherent readers plus Scalp were compiled/validated in Sun, origin, GI and
reflection entries; producer/init/content/scene kernels also validate. No GPU
pass timing or acceleration claim follows from these synthetic checks.

## Scalp camera coverage candidate verified - 2026-10-30, default off

Observed native Scalp camera shaders now capture original Frame, projection and
viewport and evaluate native screen dither. Physical rays retain native Shadow
hard coverage. Existing rt_native_scalp=0 stays; no new ABI or stream layout.
22/22 CPU tests;178,810 Scalp checks; 95 ordinary shaders byte-identical and 95
capable validated. Actual production GPU queries pass 27/27 cases (11 Scalp),
zero validation errors. No game launch/deployment or measured GPU pass cost.
Next user-led Test1 check: enable rt_native_scalp=1 in coordinated new layer,
keep lighting/RR/FG fixed, inspect the same affected close-up face and primary
match near static baseline. Evidence: docs/evidence/native-scalp-camera-20261030.json.

## OMM counter coherence fixed in isolation - 2026-10-30

Image writes now mark GPU active-writers before the native command, then update
image/global epochs and clear the active count after a device memory fence.
Unsupported writes disable OMM before the write. This fixes torn counter proofs;
native image/RT synchronization and scene attachment remain separate root gates.
Full DLL build and 28/28 CTests pass. RTX 5090: 14 counter-phase states, six EXT
rays, 2,176 UNORM and 8,704 BC endpoint samples pass; zero validation errors.
No live deployment or measured GPU cost. Evidence: docs/evidence/omm-write-protocol-20261030.txt.

## Isolated integration checkpoint: Hair GPU gate and versioned OMM - 2026-10-30

Branch codex/omm-bake now combines native Hair renderer 4ca35f5 with the actual
EXT producer/content backend. Both ABI-19 DLLs build; 28/28 CTests pass. Main's
user build remains unchanged. No game launch, live deployment or performance
claim. Goal remains open: finish OMM, Hair and RT shadows without extra tooling.

Hair production bounds and all four query entry points pass 16/16 GPU cases on
RTX 5090, zero validation errors. Physical card t=3.0, native camera primary
t=3.20000029; dither, held geometry, rebasing and invalid-bound UNKNOWN pass.
Renderer source 4ca35f5, optional probe 8b1c5cc, live instructions 19fd488
(imported here as 1b85a62/ca994f6/c74b792). Startup rt_native_hair=1 is still
default-off. Test1 face/strand quality, moving primary match and pass cost need
the coordinated live check in docs/NATIVE_HAIR.md. Other native families remain
separate gaps, not implicitly covered by CHAR_Hair.

OMM commits ac641b8/ed7e9e1/797d7ec capture actual GPU image/global epochs before
sampling and recheck them per primitive, derive all reachable view mips, and
reject unsupported BC/sRGB numeric certificates. Combined synthetic test:
three produced O/T/UNKNOWN micromaps and GPU stamps, 2,176 UNORM endpoint and
8,704 BC3/BC7 endpoint checks, zero failures/validation errors. Evidence:
docs/evidence/omm-combined-bc-producer-gpu-20261030.txt. These tests do not prove
native scene attachment, stale-scene reference selection or useful acceleration.

Canonical material capture is shared with Sun work (d0e6269). Only two audited
plain-alpha shader pairs enter the OMM recipe whitelist; six incomplete recipes
are excluded (c5c41fc). Next: sampler proof, material-specific BLAS/reference
scene integration, then same-ray native quality and cost gates. Do not bake
unsupported hair/fur recipes as a single texture channel.

## Native Hair GPU candidate verified - 2026-10-30

Windowless RTX 5090 probe: 16/16 cases, zero Vulkan validation errors, actual
production bounds and all four query functions. Physical ribbon t=3; native
camera surface t=3.20000029; origin window t=0.1. Dither, vertex phase, held
geometry, rebase, inactive NaNs and invalid-bound UNKNOWN pass. Runtime remains
default off, no live deployment. Next: integrated user-led dialogue check with
rt_native_hair=1 at startup, rt_alpha=1 and rt_bvh_split=1; inspect the affected
hairstyle face shadows and primary match near static 99.5%. Other families and
GPU pass cost remain open. Evidence: docs/evidence/native-hair-gpu-20261030.json.

## Procedural CHAR_Hair candidate - 2026-10-30, default off, no live gate yet

Native camera primary/origin rays now solve material-dependent fragment depth;
physical shadow/GI/AO/reflection queries use native Shadow thin-ribbon semantics.
Conservative AABBs cover both, with held-range recertification and explicit UNKNOWN
on invalid bounds. Precise shared-edge tests avoid false holes; proven culled
native geometry remains inactive. No manual replacement for hardware sampling.
22/22 CTests; 310,883 geometry and 19,734 capture/strand checks; 95 ordinary shaders
byte-identical and 95 capable variants compile/validate. ABI 19 Sun/OMM callback
extension imported separately; Hair adds no ABI layout change. No live deployment,
GPU cost or native 99.5% primary-match result yet. Other hair families remain open.
Implementation and exact material-model limits: docs/NATIVE_HAIR.md.

## Native Hair coverage candidate - 2026-10-30, full Hair still open

Isolated native-coverage adds startup-only rt_native_hair=0default. Actual native
88-byte strand attributes, dual mask/noise/root/view coverage and camera dither
are implemented across normal query entry points. Native shadow queries use the
light-ray hard branch. Split1 enforced; no mixed-stride hot enable. No ABI change.
21/21 CTests and 19,716 Hair checks; all15nativeVS patch/validate; 95 ordinary
shader binaries byte-identical and 95 capable compile/validate. No live change.
Native fragment depth creates a real virtual surface; original card intersection
is NOT equivalent. Procedural depth/origin support remains required before closing
Hair. Source/layout/formula evidence and next implementation: docs/NATIVE_HAIR.md.

## Disk audit and cleanup coordination - 2026-10-30

docs/DISK_HYGIENE.md records owner holds and disposal/retention rules. Active
rendering coordinator notified and added protected datasets. Captures: en2-selfcheck
128.13 GiB, pan-motion-vectors 49.05 GiB, old .claude trees 20.60 GiB, main 17.22 GiB.
These include unresolved P1 evidence and are not wholesale disposal candidates.
Synthetic queue/policy temp outputs are regenerable but total only 884,048 bytes.
Bounded SHA-256 check found 2,675 PNG alias groups / 1,495.4 MiB redundant storage;
names remain referenced, so no blind deletion. Exact paths/hashes and durable disk
snapshot are linked from DISK_HYGIENE. Owner has not released old P1 raw sets.
QA docs now warn that prune can run after incomplete analysis; preserve raw until
classified. No captures/checkouts deleted and no live work requested by this audit.

## Third plan review - 2026-10-30

Independent read-only audit found no material blockers. Checked six handoff
documents, 21 tool/source paths, M0-M6 dependencies, goal structure and offline/live
boundaries; diff whitespace check passed. Ready to hand to the execution agent.
This qualifies the plan only: M0 guard, rendering changes and live tests are not
implemented or passed by this review. No new procedural gates added.

## Execution plan agility review - 2026-10-30

Latest steering: pending/declined live permission must not stop useful independent
offline work. Plan/goal require a cross-milestone task scan and worker reassignment
before stopping, with actual missing-input/resource blockers and no busywork.

Independent read-only review identified avoidable serialization. Plan/goal now
split M2 probes and M3 providers, allow supported M4 increments on existing geometry,
and develop the minimal reference alongside M3/M4. M0 gates live release only and
stays a small queue-policy change. Request candidate-ready live batches without
waiting for unrelated offline work; authorized iterative windows need no per-build
approval. Workers may own shared-file edits; only conflicts/integration serialize.
Focused checks during edits, mandatory merge-green checks at integration, concise
docs and feature-level GUI updates. Explicit user steering remains required for live.
Documentation changes only; no implementation, build or live test performed.

## Autonomous execution plan and receiving-agent goal - 2026-10-30

docs/RENDERING_EXECUTION_PLAN.md defines M0-M6, tooling, ownership, GUI/settings,
evidence gates and live authorization; docs/GOAL_RENDERING_AGENT.md is the prompt
to activate in the receiving conversation. AGENTS/handoff link the new mandate.
No renderer implementation or live test started by this planning task.

Read-only tooling review found old queue permission exceptions and watched root/
worktree pending inboxes. Keep unauthorized drafts outside those inboxes; M0 adds
a minimal fail-closed batch authorization check with fake-runner tests. Existing
experimental GUI metadata is reusable; schema currently omits experimental.
Current hair/cascade work keeps priority. Parallel offline room/data proof follows;
whole-game PT stays outside the required deliverable. No user live grant recorded.

## Offline path tracing feasibility study - 2026-10-30

User-requested study: docs/PT_FEASIBILITY_OFFLINE.md, with reproducible probe and
JSON in docs/evidence/pt-feasibility-probe-20261030.*. No game/save access, runtime
changes, build or GPU measurement. Package probe: 3,183 content banks parsed,
zero errors. Foundry ordinary scenery resolves to 209 visual resources, 185 mesh
paths and 79 material IDs; all referenced meshes/materials found. Three other
resource IDs are effects (40 placements), not missing meshes. Reference/header
availability only: no mesh export, texture decode or visual coverage claim.

No hard impossibility found. Whole-game PT remains unqualified: construction and
terrain reconstruction, dynamic offscreen state, full materials and light semantics
are major risks. Recommend improved hybrid rendering, then a bounded opaque PT
lighting proof with raster primary visibility. Current hair/cascade fixes retain
priority; this study changes no implementation order or defaults. Historical
5-8 ms PT estimates are not a measured budget for the proposed renderer.

## Solar shadow sharpness: native-mask restriction confirmed in logs - 2026-10-30

Dedicated solar agent found correct finite-disc radius, normalization and changing
sample seeds. Final direct sun can still be clipped by the native mask: untrusted
resolve keeps nativeMask * rtVisibility. In six 600-frame dialogue windows,
94.6-98.1% of native-dark/RT-lit disagreements keep native shadowing because of
the off-screen guard; only 0.3-1.3% are fully replaced. RR is active but cannot
restore the omitted expected direct-sun contribution. This supports the reported
hard edges in those views, not an attribution of every outdoor shadow or hairstyle.

No radius, bias, blur, trust setting or renderer changed for this audit. Correct
off-screen caster coverage, including native cutouts, is required before relaxing
the guard. Sun has no dedicated mask accumulator; nearby hair can also be naturally
sharp. Report docs/SUN_PENUMBRA_REVIEW.md and matching evidence JSON. Existing RT
scope 1.130-2.521 ms/frame includes other work, not isolated solar cost. Live
same-view trust/mask check remains user-coordinated; no game job was launched.

## Follow-through: UI merged; dedicated native capture and OMM CPU work - 2026-10-30

UI descriptor fix is now on main 7b9c1e4, merged ONLY through merge-green after the
game and launcher closed: 16/16 CTests, 107/107 runtime shader compilations and QA
detector tests passed. Main and codex/ui-contamination-offline pushed; test-main
refreshed. This closes the earlier integration deferral. No game was launched.

Dedicated capture work is merged through merge-green onto main dd13361, pushed
with codex/native-coverage. Key rt_native_scalp remains OFF by default. Gate:
20/20 CTests, 202/202 shader files and QA passed; 178,780 scalar/capture/capability
checks, 95 ordinary shader binaries unchanged and 95 capable variants validated.
This is native scalp hard-cut support, not full Hair/cascade/dither handling.
Actual F10 native Frame mode is unknown; the reported close-ups may still be
unsupported. No live quality or GPU cost measured, no key enabled or game launched.
Future user-led check: Test1, same dialogue and pose, key 0/1; require successful
native hard-record counters first. Unsupported dither/zero records is NOT a pass.

OMM CPU groundwork is now also merged via merge-green onto main e277548, pushed
with its branch; 19/19 CTests, 107/107 shaders and QA passed, test-main refreshed.
It has NO production caller or GPU OMM activation. The feature branch was updated
with main's UI fix before integration; the old baseline-DLL warning is historical.
Root OMM follow-up 9bb426d and mixed-texture regression fab1ee5: actual CPU SDK configured
with <= UNKNOWN and > opaque, unchanged cutoff, four states. All 1,532 UNORM8
equality/adjacent-float cases pass with zero known-state contradictions; baseline
still reproduces six in 18 cases. Outputs include 12,192 opaque and 12,320 unknown
states, zero transparent. This sacrifices transparent acceleration, is fixed-cut
only, and is NOT GPU/filter/LOD/stochastic or traversal certification. No OMM
production caller or performance claim. Mixed-texture real descriptor/array tests
add 25,740 independent finite samples, 66 exact-cut points and eight decoder bounds
checks, all passing. Runtime GPU OMM is not integrated.

## Native scalp hard-cut implementation evidence - 2026-10-30, before integration

`rt_native_scalp=0` is the new default-off runtime key. Kind3 captures native scalp
material edge/root and uses the exact hard-cut scalar transform across normal RT
query paths, with existing RT-cone sampling explicitly retained. Camera mode != 0
stays unsupported/opaque and is counted; no fake RT dither or raw-alpha substitute.
Sticky host capability keeps held scenes safe through key-off and core reload.
No ABI change. Not full Hair/cascade support. F10 native Frame mode is unavailable;
the user's close-up may remain unsupported. No live files/game changes or GPU cost
and quality gate claimed. Full isolated build and 17/17 CTests pass; 178,780 scalar/
capture/capability checks, 95/95 ordinary shader SPIR-V byte-identical to 37299f2,
95/95 capable variants compile and validate. After merging main e277548, full
isolated build and 20/20 CTests pass with both scalp and OMM tests. No live gate.

## Native scalp CPU contract - 2026-10-30, isolated and not deployed

`codex/native-coverage` adds native scalp depth/shadow hash mapping, bounded
material/Frame capture and full scalar root/smoothstep/dither evaluation. No
production caller, key, ABI or shader change. This is preparation for the actual
coverage correction, not a live fix. MSVC standalone test: 178,522 checks pass;
43,191 shadow and 2,826 dither reference decisions. Exact raster/sampler capture,
Hair view/strand/depth attributes and cascade material records remain required.
Design and exact source hashes: docs/TRACK_D_RT.md, native scalp section.
No live files or game state changed; no GPU cost or quality gate claimed.

## User steering: native shader discovery before workarounds - 2026-10-30

Search the packaged native shaders thoroughly before choosing an unknown-shader
fallback. A missing alpha_lookup entry means missing RT coverage support, not an
unidentified native program. scripts/alpha_table.py only scans retained runtime
dumps and recognises two cutout families; that is not a complete shader census.
The two actual dialogue prepass programs are already identified and decompiled.
Their full coverage formulas, including material parameters and depth handling,
must guide the correction. No hair fallback or simple raw-alpha entry was added.

Offline catalog audit: 1,149 templates, 17,235 variants, 15,363 material modules;
the 2026-10-26 manifest records zero unreadable BSHD files. Hair/name classification
selects 17 families and 234 distinct fragment programs; adding three eyelash
families gives 20 families / 273 distinct fragment programs, only 22 listed in the
RT alpha table. Table presence does not certify exact native coverage. The local
cache initially contained 83 of 273; absence from cache is not archive absence.
All nine uncached Hair/Scalp fragment programs were recovered from Materials.pak,
hash-verified and CPU-decompiled: all 29 distinct Hair/Scalp native programs now
have GLSL available for exact coverage implementation. No fallback was needed to
find them. This is source availability, not an implementation correctness gate.
Archive-body cross-check found all 273/273 selected fragment hashes in 300 native
containers, zero missing/read errors. These shaders are available, not unknown.
Per-pass hashes and F10 correspondence: captures/dialogue-shadow-review-20261030/
hair-inventory.json. Catalog scope is the game's Data package set, not separate
Mods or loose overrides; zero BSHD errors does not prove every format was indexed.
No live files, settings, binaries or game state were changed.
Concise versioned inventory: docs/evidence/hair-native-inventory-20261030.json.

## Offline continuation: dialogue shadows and OMM groundwork - 2026-10-30

Latest user clarification: face shadows are STATIC and hairstyle-dependent,
separate from distant shadow changes (suspected trees at culling/LOD limits).
Other dialogues/camp also show odd shadows. User created Test1 and is collecting
F10s; no save files were accessed. No game launch, setting, shader, DLL or input
changes while the user plays. All future live tests coordinated in this chat.
The UI blue-block fix still has positive provisional user confirmation.

Face finding: actual prepass CHAR_Hair 5a03e5b678b2d040 and CHAR_Scalp 9e77575a9f377ec5
conditionally discard pixels but are absent from alpha_fs.txt; capturable draws
therefore enter RT as opaque. The dark polygonal face/neck shapes already appear
in pre-post diffuse d471(-161818)/d694(-161845), before RR/FG/DoF. Scene wait is
current, no timeouts/skips around those frames. Native hair coverage needs dual
mask/root/view/dither/depth treatment, not a raw alpha cutoff. Correct handling
or explicit native fallback must include visibility AND closest/origin queries.
This is a concrete coverage defect, not yet a measured per-ray attribution/fix.

Distant finding: rt_cascade_alpha=2 assumes unknown discarding FS are only fades,
but actual D16 passes contain unsupported multi-texture cutouts, plus hair and a
skydome material. Camera-pass dedupe can switch an unsupported cutout from skipped
to solid as it leaves camera keys. Counts cover all D16 passes, not post-dedupe
selected-layer captures; no particular tree/pixel is identified. Do not label all
unknown shaders as foliage. Root report docs/DIALOGUE_SHADOWS.md, evidence JSON
and captures/dialogue-shadow-review-20261030/ index/log/images retain the findings.

Solar radius is already finite: rt_sun_radius=0.00459 rad half-angle (~0.526 deg
full diameter), one sample per pixel, RR reconstructs. Characteristic penumbra
~9.18 mm per metre of caster/receiver separation; near-contact hair shadows can
be sharp. Native-mask fallback also remains. No sun-radius/bias tuning performed.

OMM milestone in ISOLATED worktree C:/Users/quali/.codex/worktrees/p4-omm-domain/BG3RTX,
branch codex/p4-omm-domain, commit 7134338: conservative whole-mip UNORM8 code hull,
exact bake identity/content/residency/completion guards and imported classifier.
19/19 CTests/full build pass with optional CPU-only SDK probe; 197,621 classifier,
398,526 support and 65 identity checks. Actual installed OMM SDK 1.10 CPU baker:
18 cases, six equality contradictions (SDK > vs native >=) under nearest/linear.
This is an integration hazard, not a runtime gate pass. No production callers,
GPU bake/BLAS/backend/ABI/shader changes or measured speedup. No OMM main merge.
That worktree is based on main 47ac6a9 and DOES NOT include UI fix 17fb500; never
copy its baseline DLLs into the user's working build. Its outputs are isolated.
Two agents used for bounded offline work; no extra live test runner.

UI fix main integration is pending while the user's game/launcher are open; merge
only via merge-green when they are closed. Runtime priority is now correct hair
and unsupported cascade coverage before accelerating those materials with OMM.


## User confirmation and remaining plan — 2026-10-30

After descriptor reset 17fb500, the user reports the foliage/UI artifact appears
resolved and cannot currently trigger it. This is a positive provisional in-game
confirmation, not a guarantee that every temporal symptom is closed. Keep the
trial specific; material-source/RR-helper changes remain absent. No new game job.
The fix is built on codex/ui-contamination-offline, not yet merged into main.

Verified current plan: OMM is NOT integrated. codex/p4-omm 8d902d5 contains only
an unused conservative interval classifier and tests; no device extension,
GPU bake, BLAS attachment or production caller. Main has no OMM runtime path.
Remaining: qualify actual foreground base fps (CPU alpha-stream optimization is
already merged/default-on in main 47ac6a9), close experimental box-light gates,
face/skin and any separate FG/RR temporal issues if still reproducible; implement
OMM backend; finish EN2 hit-material correctness and D-EM consumers/gates, light
tree and SHaRC v2; implement water (P6 has design only). SHaRC v1 already exists,
off by default; D-EM data capture exists, off. Older roadmap snapshots may be stale.


## READY FOR USER TRIAL: descriptor allocation lifetime — 2026-10-30

User asked to try the minimal patch. Applied ONLY the fresh-allocation reset:
every successfully allocated descriptor-set handle starts with empty tracked
bindings/dynamic counts, even if its handle and layout were used before.
Unrelated live sets and ordinary partial writes within one lifetime are preserved.
Caller still holds the tracker lock; failed allocations do not change state.
The shared helper is common/descriptor_epoch.h, used by layer/tracker.cpp.

The layer DLL in bin/VkLayer_bg3rtx.dll was rebuilt at 15:56:50 local time.
No ini key/change is required: this is CPU bookkeeping correctness, not a lighting
feature. No ABI/HLSL/GPU dispatch changes. Restart the game through the normal
launcher to load it. Same location as the screenshots (Blushing Mermaid), FG as
currently configured: make overhead dialogue, name/health banners or other world
UI appear while looking at the affected cypresses/flags. Compare RR on/off if useful.
Look for saturated blue blocks and UI-triggered flicker, not a general brightness change.

Build -Test: 16/16 CTests pass, including the new descriptor_epoch_test.
Actual fixed allocation function + existing source reproduction: 9 cases, 0 failures
(previous production 4 failures). Patch is built for this trial branch; NOT merged
into main and no visual-quality pass claimed. No game launched, saves/settings
changed or foreground input sent. CPU/GPU performance was not measured; no GPU
work was added. The separate material-source selection and RR exposure-helper
issues remain unchanged to keep this trial specific.
Evidence: docs/evidence/ui-descriptor-trial-20261030.json. Offline analysis and
historical source reproductions remain in docs/UI_CONTAMINATION_OFFLINE.md.


## UI / foliage offline investigation — 2026-10-30

The user's new screenshots still show saturated blue blocks on foliage/flags,
with and without RR; UI elements are possible triggers, not only mouse hover.
Offline analysis found a reproducible descriptor-lifetime bug in main 47ac6a9:
AllocateDescriptorSets updates the layout but keeps old descriptor contents and
dynamic counts when a handle is recycled. Optional RT material queries can then
accept a retired UI buffer/view. First nonempty RT3/SSS inputs are kept even when
a later native material permutation supplies the correct source. The shader uses
SSS RGB tint/thickness on class-4 foliage; finite garbage passes bounds/NaN checks.

Native C++ reproduction uses actual extracted production functions, with only driver
allocation stubbed: 9 cases, 4 failures current -> 0 failures with allocation clearing.
A second exact-block reproduction retains the stale profile and never queries the
real material source. The minimal patch is PREPARED ONLY, not applied. Optional material reads also need
active-shader provenance; clearing allocation state alone does not certify all cases.
No renderer/DLL/shader/ini change, game launch, live test, merge or quality claim.

A separate RR issue: exposure helper 4681b95e is rejected as an unknown color effect,
causing fallback to RR on graded color. It is not a UI shader and cannot explain the
RR-off symptom alone. Current log also reports SSS ranges 16384 / 144 / 327680;
range alone is not proof of a bad buffer. Three retained traces have no non-finites,
but do not reproduce the screenshot's saturated patches in inspected compose views.
Exact UI-to-blue-frame attribution remains OPEN: descriptor epochs/profile bytes at
the visibly bad frame are absent. Prior zero near-UI foliage support was inconclusive.

Report: docs/UI_CONTAMINATION_OFFLINE.md. Versioned proof/patch/JSON:
docs/evidence/ui-descriptor-20261030/. Original screenshots, copied user log and
compiled harnesses: captures/offline-ui-20261030/. Next: fix allocation lifetime and
material provenance, then qualify a same-frame UI transition; no brightness clamp.


USER CHECK / DECISION (base frame rate): may the next speed gate use one focused foreground
run? Required visible-unfocused runs stay at 30 fps after temporary game/driver cap and
immediate-present controls. Until an uncapped fps gate passes, keep `rt_alpha_stream=0`.
At the inn save, `rt_alpha_stream=1` is available for a live CPU-cost A/B. No restart needed.

## Base frame rate - 2026-10-30 (`sup/perf`, not merged)

- KEPT as a default-off candidate: `rt_alpha_stream` reads alpha material constants with
  aligned SSE4.1 streaming loads, as the existing instance-matrix path already does.
  Ordinary memcpy reads from mapped game uploads stalled capture preparation. Copies
  preserve the requested bytes, including alignment prefixes and tails. No ABI/shader change.
- One deciding Ultra 3840x1600, FG 0, static inn run. Rendered fps by window (means):
  RT off 30.056; defaults 30.056; GI off 30.000; sun off 30.000; LS off 30.000;
  RF off 30.056; SHaRC off 30.000 (already off); RR off 30.000; repeat 30.000;
  CPU timers off/on 30.000/30.056. Live RT-off disables build and every consumer;
  the device remains RT capable because rt is a restart key. RR stays on.
- Pacing limited, not GPU saturated: 33.3 ms present interval, WDDM GPU busy 6.48 ms/frame
  RT off vs 10.52-10.63 defaults. Exact CPU critical-path frame time is unavailable:
  PresentMon produced no CSV. Sampled game stacks sleep in NtDelayExecution; the exact
  ceiling cause is not established. Do not infer the user's uncapped bound from this run.
- Our known CPU: capture 4.224 -> 2.973 ms/frame (-1.251 ms, 29.6%); pre-draw 2.989 ->
  1.730. Build recording 0.97 ms is INCLUDED in capture; submit matrix copies add 0.48 ms.
  Scene wait ~5.4 ms overlaps recording; it is not additive and was left intact.
  GPU: complete GI/effects chain 2.54 ms (RT scope 1.03, LS 0.74, RF 0.71), scene 1.67,
  RR 2.36. With streaming: 2.55 / 1.63 / 2.36 ms, within baseline repeats.
- FPS GATE NOT QUALIFIED: three off means 30.000/30.000/30.000, on 30.000/30.042/30.000.
  Last on window enabled CPU timing manually after 2 s; clean 300-frame reports only.
  Parity check: primary median 98.969 -> 98.968%, ranges 98.951-98.984 / 98.957-98.980
  (21 reports each). Copy unit test covers every alignment/tail and protected boundaries.
  This is not a final-image energy measurement. Full rt_stats used ONLY for parity costs
  about 19.4 ms in the RT GPU scope, versus 1.03 with stats off; exclude it from speed gates.
- Build -Test 15/15. Queue expect gates pass; zero VK error lines, driver events or dumps.
  No bursts created. Game/driver overrides restored. Initial cap controls and an aborted
  immediate-present job are retained in evidence. Cascade code and the user's untracked shaders were left alone.
- Deciding job `sup-perf-game-uncapped`; evidence `docs/evidence/perf-base-20261030.json`.
  No merge-green/main push: fps improvement remains unproved. Next: authorized uncapped
  foreground repeats; if passed, integrate with merge-green and push both branches.

USER CHECK (FG input preservation is now the default): restart after rebuilding the layer.
Load The Blushing Mermaid - 96h 39m with FG 2. Rotate past foliage with NPC names or
selection UI visible. `fg_input_copy=1` now preserves finite MV/nonempty depth from
NGX; compare live with 0 if needed. Keep `fg_ui_pq=0`. Full flicker resolution is unproved.

## FG copy default decision - 2026-10-30 (`sup/fg-copy-default`)

- PROMOTED for correctness. Earlier private-input gates passed 200/200 depth and
  MV matches, zero empty depth/non-finite MV; original late depth was zero in half
  the samples. Copy GPU cost remains 0.026 ms. No ABI or shader change.
- One FG 2 run, fast Q rotation, 0/1 alternating four times, 16 presented PNGs per
  window. World mean +/- window SE: 7.039 +/- 0.352 -> 7.940 +/- 0.100 codes;
  UI ROI 7.458 +/- 0.509 -> 7.835 +/- 0.447. Observed deltas worsen by 0.901 and
  0.377. They do not exceed the simultaneous one-sided 95% noise bounds, 1.075
  and 1.663 (Welch SE 0.366 / 0.677, df 3.48 / 5.90, two ROIs, family alpha 0.05).
  This decision is not a proof of equivalence or a passed full flicker-quality gate.
- Copy-0 repeat SD was 0.831 / 0.055 in the prior two windows; this run measures
  0.704 / 1.017. Windows, not PNGs, are the sampling units. All capture windows
  reset SDK timers (33 total); last on camera step is larger. Steady ratio 2.00,
  capture ratios 1.70-2.30, zero host toggles, no new VUID IDs; two existing SDK
  latency-marker threading warnings. No driver events or dumps. Queue valid.
- Job `sup-fg-copy-default`; evidence `docs/evidence/fg-copy-default-20261030.json`.
  Defaults regenerated; build -Test passes 14/14 CTests. Integration uses merge-green.

## FG pairing / UI follow-up - 2026-10-30 (`066b38b`, not merged)

- Fixed two publication hazards behind `fg_input_copy`: color/UI now follow submit
  order, and camera constants are snapshotted under the input lock. Copied depth/MV,
  camera and color must share the current submit frame. Secondary buffers propagate
  private-copy metadata. Disabled live UI tags are explicitly cleared in Streamline.
- Input gates PASS: 200/200 MV pairs and 200/200 masked D24 pairs equal the same
  NGX evaluate inputs; zero non-finite private MV components and zero empty private
  depth samples. All 245 selected submit/token stamps align. This does not prove
  an original one-frame input lag. Camera-derived MV scale 0.9994, median error
  0.61 render pixels; reversing the sign gives 104.65 pixels. Copy GPU 0.026 ms.
- A NOT PROMOTED. Final one-run fast Q rotation, copy 0/1 twice, world residual
  off 7.21 / 8.38, on 3.87 / 8.38 codes; UI ROI off 7.24 / 7.31, on 3.97 / 8.35.
  Pooled world 7.80 -> 6.13 and UI 7.28 -> 6.16 do not qualify the worse second
  UI repeat. Steady SDK median 2.00, capture samples 1.70-2.30; 16 timer resets,
  zero host toggles, zero new VUID IDs, one SDK latency-marker threading warning.
- B NOT QUALIFIED. Five valid UI combinations repeated in one selected-character
  rotation job at FG 2, copy 0. Both baseline near-UI foliage reprojection supports
  are zero. Far class 4 change: baseline 7.80, UI off 8.52, PQ UI 8.08, hudless off
  8.06, hudless off + PQ 8.32 codes. Camera/material support varies; these numbers
  do not establish causal hover-excess removal. No UI default change.
- KEPT safety repair: RR descriptor exhaustion previously called DeviceWaitIdle
  while the SDK pacer submitted, producing a validation queue race and a stall.
  Cache turnover, exhaustion and resize now retire pools through the existing GPU
  completion fence. Repaired runs completed 64 pool rollovers without that stall.
  No new GPU dispatch, ABI or shader-source change.
- Debug PNG encoding now uses a bounded FIFO writer, with ordered drain and queue
  backpressure tests. GPU readbacks still stall presentation. Temporary virtual
  mouse / Alt / reset-gap experiments were rejected and removed from the change.
- Build -Test 14/14; runtime shaders 107/107; QA detectors pass; camera/permission
  checks 25/25; FG parser 11/11. All 44 new completed raw burst directories deleted.
  Main remains 65bd0a2. No merge-green or default promotion: quality gates did not pass.
- Evidence: `docs/evidence/fg-followup-20261030.json` and
  `docs/evidence/fg-copy-normal-20261030.json`. Deciding jobs:
  `sup-fg-pair-ui-fixed`, `sup-fg-copy-normal`; earlier controls are listed in evidence.
  Next: remove GPU-readback stalls, retain adjacent native endpoint identities,
  and obtain actual near-UI foliage support before repeating the quality gates.

## FG input copy motion gate - 2026-10-30 (`sup/fg-copy-gate`, not promoted)

- One Q/E and pan probe, followed by one same-run copy 0/1/0/1 matrix for slow rotation,
  fast rotation and pan. The probe showed that key repeat does not change held-key
  speed. Added tested key duty cycles and separated native bursts from presented PNGs.
- Input validity and the copy cost are measured independently of image-quality claims.
  The existing motion parser and static pose checker are reused. Their original gates
  target primary-ray tracking and FG 0/2 static effect parity, not interpolation error.
- The slow pulse input still aliases with readback stalls. Changing in-world UI support
  also prevents matched semantic comparisons. Presented midpoint residuals are unwarped
  review proxies, not a passed interpolation gate. Default stays 0; no integration.
- Validity gate PASS: off late depth zero in 42/96 samples; 4,890,263 non-finite MV
  components across those samples. On: 101/101 private depth/MV pairs match NGX;
  zero all-zero private depth samples and zero non-finite private MV components.
- NOT PROMOTED: fast-rotation midpoint residual off -> on, in 8-bit code values:
  world 1.90 -> 8.93, foliage ROI 2.44 -> 9.93, character ROI 2.45 -> 9.16,
  world-UI ROI 2.07 -> 9.44. These fixed-ROI, unwarped proxies are review only;
  no claim of ground-truth interpolation error or matched semantic coverage.
- FG interior host toggles 0; steady SDK ratio 2.00. Captured windows range 1.60-2.10
  (33/462 ratio lines differ from 2.00), with 85 frame-timer resets. No new VUIDs
  relative to the six known probe IDs, no NVIDIA events or dumps. Copy GPU 0.026 ms.
- Jobs: `sup-fg-copy-probe`, `sup-fg-copy-motion-gate`; both transport/expect checks
  pass. The latter's quality gate is INVALID, not a PASS. All 15 raw bursts were
  analysed and their plain directories deleted. Evidence: docs/evidence/fg-copy-gate-20261030.json.
- Next: normal-cadence generated-frame capture with explicit endpoint identities,
  then matched continuous motion and selection/nameplate support. No default change.
- Rotation-only foliage job `sup-fg-foliage-rotation`: native-final class 4 reprojected
  mean absolute change (codes/255), clear / hover-request repeats:
  FG 0 clear 6.68 / 9.99, hover 7.17 / 6.47; FG 2 clear 10.05 / 8.88,
  hover 11.07 / 10.82. FG 0 watch ratio 1.00; FG 2 SDK median 2.00; no interior
  option toggles. World UI pixels: clear always 0; hover FG 0 2,547 / 15,008,
  FG 2 14,905 / 0. The last hover lacks world UI. Selection rings/nameplates are
  not proved matched; camera paths also differ. Attribution unresolved; no fix.
- Build `-Test`: 13/13 CTests pass; 25 camera schedule/permission checks pass.
  All 23 job bursts are analysed and deleted. Main and the default remain unchanged;
  the branch records test harness changes and evidence only.

USER CHECK (FG input protection, candidate stays off): restart the game after the layer
update. Load The Blushing Mermaid - 96h 39m. Compare live `fg_input_copy=0/1` while
rotating past foliage with floating text, names or selection UI visible. Keep `fg_ui_pq=0`
for this comparison. Input preservation is verified; the full flicker bug is still open.

## UI / FG inputs - 2026-10-30 (`sup/ui-inputs`, default off)

- Found two invalid FG inputs in the inn. In the deciding 128 paired samples, late
  depth is entirely zero in 64 samples (8/16 in each burst), despite nonzero scene
  depth before RR. The same MV image is finite through RR/post, then gains 3 non-finite
  components in a 40 x 24 pixel region before UI composite. The writer is not identified.
- Layouts were also wrong: depth tagged 3 / observed 5, MV tagged 2 / observed 5.
  CmdWaitEvents(2) transitions were missing from FG tracking. With the correction,
  both tags are 5 / observed 5. Diagnostics observe actual layouts without repairing
  the key-off tags, so the baseline is preserved.
- KEPT, default 0: `fg_input_copy` preserves the depth/MV pair from the same NGX evaluate
  in private D24S8/RG16F images, with a depth-only view. Submit order selects the pair;
  the SDK input-consumption fence guards reuse, including copies deselected by toggles.
  No ABI or shader changes. All ordinary shader sources remain identical.
- `sup-ui-inputs-pair-gate`: 8 valid bursts, 17 lighting frames and at least 16 RR/FG
  input frames each. 68/68 MV copies and 68/68 masked D24 copies equal the finite,
  pre-UI inputs. GPU pair-copy median 0.026 ms. FG steady-state ratio 2.00, status 0,
  API errors 0; no new VUIDs after capture starts, no VK_ERROR, device events or dumps.
  Six existing validation IDs occur before measurement; they are listed in the evidence.
- NOT PROMOTED: native final class 4 tflk off 1.51 / 1.45%, on 1.43 / 1.63%.
  Static mean energy differs by +0.020%, within the off-repeat spread (0.126%).
  Native final is pre-UI/pre-FG. Posted rotation has stationary windows and view jumps,
  and near-UI foliage coverage differs. A full visual improvement is not established.
- The captured floating text is absent from RR colour and hudless, present only in UI.
  Raw UI fails the HDR10 blend (mean error 0.085 on UI pixels). Existing fg_ui_pq=1
  reduces it to 0.000075, but also stays 0 without a qualified visual gate.
- Initial evidence was invalid (15 late frames / expect typo); first copy gate had a
  present-counter offset; Vulkan exposed stale layouts. Each test was corrected.
  The lease stress completed 16 on/off cycles without exhaustion; the paired gate
  also exercises off/on twice in static and scheduled rotation windows. All 13 CTests pass.
- Raw pan directories are deleted after analysis. Manifests, previews and numbers remain
  in captures/ui-inputs. Evidence: docs/evidence/ui-inputs-20261030.json.
  Next: qualified continuous rotation with matched in-world UI before default 1.

USER CHECK (hidden floor squares fixed, default on): restart after the layer update. Load
The Blushing Mermaid - 96h 39m. Look near the crates and stage edge; compare the live key
`rt_capture_discard=0/1`. The two dark square shadows should disappear with 1.

## Hidden floor squares — 2026-10-30 (`sup/hidden-quads`)

- Cause: the camera prepass includes a 24-triangle helper (VS `ca869467434cd2db`,
  FS `dc180592a8fc6087`). The FS is just `OpKill`, without EarlyFragmentTests. Raster
  writes no surface, but transform feedback captured it as an opaque RT caster.
  Capture 890 identifies both blobs as dynamic TLAS instance 1834, frame 3992.
- Fix: `rt_capture_discard` excludes only fragment entry points proved to be an empty
  block ending in unconditional kill. Conditional alpha tests and early depth tests
  are kept. No ABI change. Unit tests cover the proof's positive and negative cases.
- `sup-hidden-quads-isolate`: TLAS refit, dynamic refit and in-place rebuild toggles
  leave 30,343–30,362 opaque-front pixels. firstInstance, draw counts and vertex offset
  are already applied correctly. No matrix changes between record and submit.
- Split-off was a confounded oracle: about 168 opaque draws/frame overflowed the
  capture. Capture-all likewise removes the helper but drops primary match to 75.68%.
- `sup-hidden-quads-depth-gate`: INVALID (missing primary log); 1 draw / 1,024 triangles
  excluded per frame, blobs 28,271 -> 28,242; rejected as a fix, diagnostics kept off.
- Early refit/identity toggle windows were invalid (settings scheduled after phase end);
  corrected in the isolate fixture. Their instance-ID baseline remains useful.
- Optional diagnostics: `rt_mesh_draw_log=1` logs draw keys, counts, transforms and TLAS
  indices; N > 1 samples every N frames. `rt_primary_identity=1` captures hit-index bytes
  in primary Y/Z. Both default 0. Plain and epoch default SPIR-V remain byte-identical.
- KEPT / default 1: `sup-hidden-quads-discard-gate`, valid FG 2 run, key 0/1 twice:
  opaque-front pixels 29,411 / 29,429 -> 132 / 127; square ROIs 29,288 / 29,303 -> 0 / 1.
  Primary match 96.095% / 96.092% -> 96.951% / 97.190%. Normal-image mean +0.28%.
  Stats-off BVH build median 1.387 -> 1.368 ms (14 windows each); no VK errors,
  NVIDIA events or dumps. All 12 CTests pass. Integration uses `scripts/merge-green.ps1`.

QUESTION FOR THE USER (FG flicker around UI): the game outputs HDR10 and composites its sRGB UI
over the PQ scene; the UI image we give DLSS-G is the raw sRGB one. Please compare in game,
camera moving with names/labels on screen: fg_tag_ui=1 (now) vs fg_tag_ui=0 (bg3rtx.ini,
live). Third option ready: fg_ui_pq=1 (our UI image consistent with the HDR10 composite;
sup-uipq-s7-fg2: pipeline built, no new validation errors, FG 2.00). The cleanest becomes default.

## Night 2026-10-29/30 (supervisor): in-world UI flicker, roof casters (merged, main f72abca)

READY FOR THE USER'S CHECK (restart the game; Roof sun shadows on): save The Blushing Mermaid -
96h 39m. Look for: black squares on the floor and sun patches inside the inn (should be gone),
tree/flag flicker while rotating and with dialogue on screen, red blocks with RR off.


Ready for the user's in-game check once a live test confirms (layer change: restart the game).
Save: The Blushing Mermaid - 96h 39m. Look at cypresses, flags and curtains while dialogue or
overhead text appears; the Tab party screen portrait. Log lines to read: '[geo] scene prepass
moved', '[rt] scene wait' (timeouts, skipped), '[alpha] ... held records dropped'.
- User: foliage and cloth flicker starts when in-world UI (dialogue, overhead text) appears; RR
  off shows saturated red square blocks on cypresses. Static job sup-redsq-inn-fg2 (no in-world
  UI): no red blocks, no flicker. So: a capture problem triggered by in-world UI.
- Fix 1 (b10e434): the RT scene's depth prepass was found by its learned position in the command
  buffer. Small depth-only passes before it come and go with what is on screen, so a shift froze
  the RT scene for 8 frames, or made a small pass the RT source (clearing the frame's draw and
  alpha lists). Now: the first non-minor depth-only pass on the G-buffer depth each frame
  (rt_prepass_by_ordinal=0; 1 = old behaviour).
- Fix 2 (b10e434): held alpha-tested draws (rt_dyn_hold) drop when a sampled view was destroyed
  and its handle reused (e.g. by a glyph texture); counted in the '[alpha]' line.
- Fix 3 (65f5fb5): a smaller pass with the G-buffer formats (party screen portrait) replaced the
  main G-buffer: RR refused frames and the RT capture lost its render size. The main G-buffer is
  now the largest seen in the last 4 frames.
- Fix 4 (c09dff9): after 8 rt_scene_wait timeouts the wait paused up to 64 frames, so the RT
  passes traced the previous frame's scene for about a second. Static inn job: 16 timeouts and
  96 skipped per 600 frames, previous-frame TLAS in 50 of 300. A recent build now ends the pause.
- Live (user away, 00:30-00:55), sun buffer: 'leak' = applied lit where the game is dark,
  'dark' = applied dark where the game is lit. Roof sun shadows (rt_cascade_casters=1):
  * Black squares / false sun shadows: solid alpha-tested cascade casters (tree and vine
    cards). View over the inn roof: dark 17.5% (alpha 1) -> 2.7% (alpha 0); casters off 2.6%.
    rt_cascade_alpha default 0.
  * Sun leaking into the inn (window-shaped patches): trust 1 dropped the roof volumes and the
    shutters/glass are alpha-tested (skipped). Leak 4.5% (trust 1) -> 0.14% (trust 0); casters
    off 0.12%. rt_cascade_roofs_trust default 0 (roof volumes stay, cascade adds occluders).
  * rt_scene_wait: 0 skipped after fix 4; timeouts 4-6 per 600 frames at 8 ms (max wait 10 ms):
    rt_scene_wait_ms default 12.
- Tree flicker with in-world UI: Shift-held labels do not raise foliage frame-to-frame change
  (final, static: 1.5-1.8% with labels, 2.4-4.3% without); gi_rt=0 and rt_shadows=0 do not
  lower it. Static runs vary 1.8-6.1% with nothing changed (wind). Open: needs motion runs.
- Next for Roof sun shadows: alpha-test the cascade casters instead of skipping them. Inn,
  sup-ccalpha-inn-fg2: 750-796 alpha-tested cascade draws skipped per frame, 475-489 (62%) with
  an FS already in the alpha table and the UV the patched VS captures; the rest need table
  entries (scripts/alpha_table.py over the shadow-pass FS). Work: a non-opaque alpha geometry
  in the cascade BLAS, its records appended to the build's alpha set (views kept alive one
  frame more: the cascade capture is F-1's), InstInfo for the cascade instance.
- Foliage flicker, measured on the final image (pan bursts now keep all 16 tonemap frames):
  foliage px changing > 8/255 per frame, inn view, static 20-28%, rotating 16-50% (Q/E
  differ). With all RT strengths at 0 (the game's image, RR still on) the same (static
  21-42%, rotating 25-50%); gi_rt=0, rt_shadows=0, rt_local_shadows=0 or
  rt_alpha_stochastic=0 alone lower nothing. So our effects add no measurable foliage
  flicker in this view after the fixes above. User to re-check in game (main f72abca+).
- Local-light shadow cost in the inn (sup-lsstat-inn-fg2, Full + shadowed lights): 0.93 ms GPU
  (no rays 0.26 ms; Fast mode 0.75 ms). The 7-10 ms seen earlier was ls_stats=1 in the job sets
  (its counters cost ~6.6 ms). rt_fast_trace=1: no gain (build +0.3 ms). The 'removed 85%'
  stat jumps after a live ls_box toggle while the image is unchanged: stats-only.

## Late Thursday 2026-10-29 22:40 (supervisor, user away)

- Roof sun shadows leak with FG off: fixed (main 52bb6cd). With fg=0 the game records frame F's
  build before F-1's cascade pass: 294 of 297 builds had no casters. Now the F-1 or F-2 soup
  (triple buffer); S7 fg0 and fg2: instance in 60/60 builds, mean brightening 0.0011/px.
- Box light shadows (experimental): main-menu skull loses its beam and the red gems' shadows
  flicker. Cause: unshadowed box lights get RT shadows along the box; decorative casters inside
  the box block it. Open: an art-intent rule for unshadowed box lights.
- Foliage flicker while rotating (user: worst with rotation, around white in-world text; also
  without FG): not the stochastic alpha (raw sun-mask flips equal on/off) and not FG. The raw
  mask is noisy by design (static 3.4% flips), so only the post-RR image can judge it. Pan bursts
  now also capture the tonemapped 'final' (main 0ca8ba1), but only on some frames in motion.
  Next: live A/B with the user (rt_alpha_stochastic, rt_dyn_hold, rt_cascade_casters, RT off).

## Launcher "Experimental" section; cascade casters fixes from the user's play — 2026-10-29 21:50

- Launcher: knobs with `experimental=true` (off in every preset) sit under "Experimental (being
  tested)": Roof sun shadows (`rt_cascade_casters`), Box light shadows (`ls_box`). New features
  to try go there.
- User play (focused, FG 2): 57-60 fps rendered, FG 2.00 (FG pauses only while unfocused,
  Streamline "window not focused"). Roof disocclusion gone; box lights look right.
- Cascade casters: distant shadows that vanished on approach (coarse cascade LODs shading their
  own surface) -> guard max(0.5 m, 2% of camera distance); polygonal leaf shadows (solid cascade
  copies of visible foliage) -> `rt_cascade_alpha_dedupe=1` skips alpha-tested cascade draws whose
  mesh the camera pass drew (S7: 306 of 680 draws skipped; game lit & RT shadowed 1.19% -> 0.94%,
  mean brightening 0.0010 -> 0.0011/px, unchanged within noise).
- D-EM data work (codex/p2-emissive) merged, ABI 18, off.
- Open: tree shadow flicker / blue dots the user ties to white in-world text (item labels, barks);
  a Shift-hold trace showed no extra pass (Shift may not reach the game through PostMessage).

## Status Thursday 2026-10-29 evening (supervisor)

- P1 (shadows detach / walls flash while rotating): fixed in main (rt_scene_wait, default on); rotation match 33% -> 97%; the pan residual is far backdrop and game-hidden geometry, not lag.
- P2 FG toggling fixed (fg_current_destroy_only=1): 0 toggles, displayed/rendered 2.00. Static LOD shadow flicker fixed (rt_dyn_hold=4).
- Roof sun leak fixed (rt_sun_roof_up 50 m). New, off by default: rt_cascade_casters=1 (cut-away roofs cast RT sun shadows); waits for the user's visual check (+1.7 ms).
- P3 base frame rate: unfocused runs sit at a 30.0 fps cap; the real focused rate needs the user's play.
- Next: face/skin flicker (P4), then box lights (P3 phase), OMM, EN2/SHaRC, water.

## Cascade casters: ready for the user's in-game check (off by default) — 2026-10-29 21:00

`rt_cascade_casters=1` (needs a game restart once, then live): the sun's cascade 2 is captured
and added to the RT scene for sun rays only, so cut-away roofs cast RT shadows and the roof
volumes no longer block the RT sun. Fix found in game: the cascade's positions are relative to
its own UBO origin (92 m from the camera), and alpha-tested casters must be included.
S7, static, one run A/B: key off game dark & RT lit 30% of traced px (kept by roofs), mean
brightening 0.0004/px; key on 0.45%, kept by roofs 0%, brightening 0.0010/px. Cost +1.7 ms GPU
(scene build 1.42 -> 2.72 ms, cascade pass 0.43 ms). User check: Blushing Mermaid save, inside,
roof cut away, sun through windows: toggle rt_cascade_casters 0/1; look for light through the
roof (bad), sharper roof shadow edges (good), solid foliage shadows (artefact).
Details: TRACK_D_RT.md 8.5 "Cascade casters as implemented".

## P1 pan residual explained, not a lag (supervisor) — 2026-10-29 19:00

Job sup-panmiss-cap-fg2 (FG 2, defaults, A/D pans, 16-frame primary captures): pan windows
match 90-92% vs static 99.5%. The gap is not the RT scene lagging: (1) misses (~5%) are the far
backdrop at 1.5-1.8 km (not in the TLAS, no shadow role); (2) "in front" (~3%) is one near opaque
object that the game hides while the camera passes (a ~7% wide band sweeping across the screen,
140 tolerances in front), present in the TLAS as in the world. Nothing to fix for P1.
Note: in visible-unfocused runs the rendered rate is exactly 30.0 fps (displayed 60, monitor
120 Hz), a cap; the real base frame rate needs the user's focused play.

## P2 FG toggling: fg_current_destroy_only=1 on by default (supervisor) — 2026-10-29 18:15

Cause: our FG stability guard turned DLSS-G off for ~1 s whenever the game destroyed any image
that was ever tagged, even a retired one (current roles 0x0): every "stability-cooldown" decision
in the logs had roles 0x0. fg_current_destroy_only=1 (Codex) resets only for current inputs; a
retired tag still waits for the DLSS-G inputs fence before it is destroyed.
Job sup-fg-destroy-fg2 (FG 2, Q/E rotation + static, live A/B, 50 s windows): key off 7 and 1
FG off/on toggles, displayed/rendered 1.81 / 1.92; key on 0 and 0 toggles, 2.00 / 2.00.
No crash or device-lost lines. User check: play with FG on; the FG indicator stays on.

## Static shadow flicker: rt_dyn_hold=4 on by default (supervisor) — 2026-10-29 18:00

User report: small polygonal RT shadow patches flicker now and then with the camera still.
Cause (rt_stability_log=1, "[bvh] static-camera draw diff"): BG3 LOD swaps replace a captured
draw by its other LOD in one frame (e.g. 154 <-> 310, 784 -> 1570 triangles), sometimes
alternating every 2 frames; RT shadows follow the swap. rt_dyn_hold=4 keeps a vanished captured
draw (up to rt_dyn_hold_max_prims=8192 triangles, GPU snapshot of its soup) in the TLAS for 4
frames while the camera is static; all holds drop when it moves.
Job sup-dyn-hold3-fg2 (FG 2, static, live A/B): same-frame swaps off 1/4/7 per 40 s window,
on 0/0 (46/42 holds); static primary match 99.524-99.525% both; scene build GPU 0.22 -> 0.41 ms.
User check: static camera, any save, watch small shadows for a minute.
Codex wrote the first version (f7dacc4); the supervisor added the reason counters and the size cap.

## P1 same-frame scene + P3 CPU fix (supervisor) — 2026-10-29 17:25

rt_scene_wait=1 is now the default (user decision 17:35). In-game check: FG on, any save,
rotate the camera with the mouse or Q/E. Shadows should stay attached and walls should not flash.

P1 cause: BG3 records the lighting command buffer (our RT passes) on a worker thread before
another thread ends the G-buffer pass of the same frame, where the scene is captured and built.
Both go down in one vkQueueSubmit, G-buffer first (rt_order_trace=3 logs it). So the RT passes
traced the previous frame's camera-relative scene. rt_scene_wait=1 (default off) makes the first
RT query of a frame wait for that frame's scene commit (bounded by rt_scene_wait_ms=8).
Rotation, FG 2, ~2.6 deg/frame, one run (perf-sup-cpu4-fg2): off 33-35%, rt_scene_rebase 95.2%,
rt_scene_wait 97.4% primary match; static 99.5%. Wait: 600/600 frames got their scene,
0 timeouts, mean 3.4-3.8 ms. Residual ~2% while rotating is in instanced (pool) hits; not solved.

P3 cause (base frame rate): the scene build read mapped (write-combined) memory with ordinary
loads: TLAS instances assembled with bit-field stores in the mapped ring (7.6 ms), read back for
the signature/chunk guard (6.3 ms), and 2,500 instance matrices read at submit (28 ms on the
submit thread). Now built locally and copied whole, and read with SSE4.1 streaming loads.
CPU per frame: capture "pass after" 14.1 -> 0.66 ms, submit copies 28.5 -> 1.3 ms.
Static FG 2, same run: build on 30.0 fps vs build off 30.0-30.1 fps (before: 19.1 vs 30.0).
30 fps looks like a cap in this hidden/unfocused mode (the user saw GPU clocks at ~2200 MHz there),
so real-display fps must be checked by the user. RT pass GPU cost 0.84-0.87 ms.
cpu_timing=1 logs the [cpu] split. User reports occasional static shadow flicker: separate issue.

## P1 rotation: rt_scene_rebase lifts the rotating primary match from 33% to 94% (supervisor) — 2026-10-29 16:50

Root cause, measured: every RT pass traces the TLAS built in the previous frame index
("TLAS this frame 0, previous frame 300" in every rt passes line). BG3 renders camera-relative,
so the traced scene is offset by the camera translation between the two frames. Orbiting moves
the camera origin most, which is why rotation shows it far more than panning.
Fix: rt_scene_rebase=1 (Codex 314840d) shifts every ray origin by one per-frame constant: the
lighting camera origin minus the scene build's camera origin (snapshotted on the GPU).

Job p1-sup-rebase-rot-fg2-b (QuickSave_3784, FG 2, visible unfocused, real presentation,
keyboard rotation Q/E at a steady ~2.55 deg/frame, live A/B in one run):
| window | rebase=0 | rebase=1 |
| --- | ---: | ---: |
| static | 99.52% | 99.52% |
| rotate left (two windows) | 33.0-35.0% | 94.3-94.8% |
| rotate right | 33.1-38.7% | 94.0-94.4% |
RT pass GPU cost 3.87-3.90 ms off, 4.04-4.29 ms on (with the epoch counters on; ~1.1 ms
without them in the earlier run). Static identical, so brightness is unchanged.
Residual 5-6%: pool hits 95%, soup hits ~74%. The scene camera trails the lighting camera by up
to 5.1 deg (more than one frame), so the edge band that enters the view during rotation is
missing from the older, CPU-culled scene. The real fix for that is tracing the TLAS of the
same frame as the G-buffer (next step). Kept; gate (match ~99.5% while rotating) not passed yet.
Input: scripts/auto-run.ps1 now maps scan codes 0x10/0x12 to Q/E, BG3's camera-rotate keys,
posted to the window (no focus needed); tests/fixtures/p1_sup_rebase_rot_plan.json.

## P1 NOT ready: static parity passes, motion qualification fails — 2026-10-29 15:56

Read D:/BG3_Tools/USER_STEERING.md (both 13:57 entries and 14:29 authorization).
Ran the queue without USER_AWAY, visible/non-activating, real windowed presentation,
QuickSave_3784, FG 2 cold-loaded. No observed activation or focus setter; the window
was covered by other apps throughout. Actual generated presentation was measured.
No user game/USER_PLAYING, save writes, merge or default promotion. Display restored.

Static: PASS, all 13 phases / 30 traces. FG 2 normal primary 99.5195-99.5215%,
18.7-18.8 rendered fps, displayed/rendered 2.00; FG 0 99.516%, 18.2 fps, ratio 1.00.
No interior host toggles. All five effects have dispatch/timing evidence; no persistent
FG-dependent debug difference exceeded repeated-noise/encoding/jitter bounds.
GI debug was black in BOTH modes: this proves parity only, not nonzero GI contribution.
The composite feeds debug diffuse into previous radiance; app dumps also do not
certify generated-frame correctness. Sheets:
- captures/p1-fg/static-effects-sheets/sun-fg-static.png
- captures/p1-fg/static-effects-sheets/local-fg-static.png
- captures/p1-fg/static-effects-sheets/GI-fg-static.png
- captures/p1-fg/static-effects-sheets/reflections-fg-static.png
- captures/p1-fg/static-effects-sheets/RR-fg-static.png

Motion: FAIL / INVALID qualification. Stopped the 56-phase job after 13 complete
phases plus part of phase 13, because posted 60 px/s rotation alternated between
stationary windows and camera jumps up to 98.58 degrees/frame; normal speed/direction
fidelity was not proved. Baseline FG 2 rotating displayed/rendered median 1.00,
so sustained FG generation was absent. Missing unrun phases are not observed RT loss:
all sampled windows retain RT 120/120, none 0, plus sun/local/GI/reflection/RR evidence.
The analyzer's primary-mismatch reproduction flag does not certify visible flashes.

| First negative rotation | FG 2 match | Rendered fps | Displayed/rendered | Options calls / interior mode toggles |
| --- | ---: | ---: | ---: | ---: |
| baseline | 21.299% | 26.0 | 1.000 | 5 / 0 |
| rt_scene_rebase=1 | 85.984% | 25.3 | 1.163 | 3 / 1 |
| fg_current_destroy_only=1 | 20.5465% | 26.6 | 1.412 | 1 / 0 |

FG 0 reference matches: 17.434 / 75.819 / 15.887%, ratios 1.00, 26.2 / 26.4 /
25.7 fps. These paths/views differ after earlier rotations: do not treat the median
change as an isolated fix benefit. Required FG-on static floor was 99.501%.
Both candidates stay default-off. Promotion and a "ready for user check" claim are
REJECTED; the code remains experimental. No merge-green invocation or main change.

New evidence: 6,106 retired-only destructions reset stability with the guard off;
3,279 did not with it on (current roles 0x0). Three current-role destructions still
reset with it off. No SDK focus-disable or crash exception was logged in this run.
The guard targets real unnecessary resets, but interior toggles were 0 vs 0 in the
first deciding pair, and it does not restore moving primary match. Its gate did not pass.

Next hypothesis: translation rebasing alone is insufficient. Remaining pool/soup
mismatch and sampled scene/lighting pose separation are consistent with older instance
or dynamic-geometry data, or a camera-buffer epoch mismatch; exact frame lag and a
Reflex/FG cause remain unproved. First repair/qualify no-focus relative rotation input.
PostMessage reaches BG3 but is NOT yet validated for controlled background rotation;
no global input or focus forcing will substitute for that check. No new selector.

Logs: captures/queue/logs/p1-fg-static-effects.1.152122.{log,runner.txt,status.json}
and p1-fg-motion-matrix.1.153616.{log,runner.txt,status.json}. Results:
captures/p1-fg/{static-effects-result,motion-gate-result}.json. Curated evidence:
docs/evidence/p1-fg-live-20261029.json. Stop was deliberate CloseMainWindow; the
subsequent "Invalid window handle" runner exception was a teardown race, not a
proven game crash/timeout. Earlier 242 s exit remains unattributed.

Cost: same-phase full per-ray stats off raised 15.5 to 19-20 fps; GI ~6.8 ->
0.379 ms, local ~5.25 -> 0.530 ms (15:02:53-15:03:18 retained log). This exposes
a large diagnostic cost, not a rebase regression. Historical 15.040 vs 4.269 ms
remain confounded by scene/settings/presentation; RR was enabled in both.
RT 2.872 ms was a mixed 300-frame interval, not an isolated key-off pass cost.

Kept tool changes: 92cc6ad, fe17233, 0a6a8cf, b5a2e87, c5a909c (pushed).
Checks: 20 matrix/static/plan tests, 26 window-policy/geometry and 18 camera/policy
checks pass; prior 7 CTests, 21 ordinary SPIR-V identities, 20 shader variants pass.
No further game jobs queued after the invalid verdict. Fixtures remain for repair.


## Static capture budget corrected — 2026-10-29 15:21

The first lightweight attempt met cadence/FG/primary requirements but its 1 GB
capture budget omitted G-buffer and debug diffuse. It was stopped as INVALID.
Restoring the established QA budget of 6 GB produced both buffers, confirmed
in captures/20261029-151939 and 151949. The complete static check is requeued
with that budget; no threshold or lighting change. Earlier partial captures
are not a parity pass. The 56-phase motion gate still awaits a valid static run.

## FG-on cadence setup isolated — 2026-10-29 15:08

Windowed/native extent enables actual FG: SDK status 0, two presented frames,
displayed/rendered ~2.00. Static primary ~99.52%, but full per-ray RT/LS/RF
statistics cost ~15.5 fps. Within the SAME FG-2/static sun-debug phase, turning
only those statistics off raised cadence to 19-20 fps: GI median ~6.8 -> 0.379 ms,
local shadows ~5.25 -> 0.530 ms. RT interval 2.872 ms at 15:03:08 includes a mixed
300-frame window; do not subtract it as an isolated pass cost. Evidence:
captures/queue/logs/p1-fg-static-effects.1.145628.log, 15:02:53-15:03:18.

That setup probe was deliberately ended, INVALID, before completing parity.
The retry keeps full-resolution primary/epoch GPU counters with rt_stats=0;
ordinary shaders remain byte-identical (21/21), all 20 variants validate.
Only the diagnostic epoch producer counts primary checks independently of full
alpha/light statistics. Lightweight recorded-dispatch counts plus existing GPU
timers/debug captures prove sun/GI/local/reflection application; RR still uses
its GPU timer. Lighting and both fix defaults are unchanged. Seven CTests and
20 matrix/static/plan tests pass. The >=18 fps gate is unchanged.

## FG setup extent mismatch confirmed — 2026-10-29 14:56

Second static attempt was stopped as a setup probe after FG never enabled.
Trace captures/20261029-145417/frame.json proves UI compositor 046c6949 samples
s1b1 i11700 RGB10A2 and s1b2 i11701 RGBA8 at 3840x1600, while swapchain i11/i13
are 3840x2160. The existing FG classifier requires matching extents and misses
UI/hudless entirely. Scene static primary was 99.55-99.57%, but SDK presented 1,
displayed/rendered 1.00: INVALID FG-on evidence. This is not a renderer fix.
Authorized unfocused FG runs now use reversible windowed display settings to
keep the native output extent. Default background/foreground behavior is unchanged.

## Live setup retry — 2026-10-29 14:50

First authorized static job was INVALID before its first phase: runner policy
closed the game after 71 s because exposed client area reached zero. This was
not a crash or its 1120 s timeout. No deciding FG/match/effect data was collected.
For the explicitly authorized visible_unfocused_fg mode, coverage and observed
foreground are now logged instead of aborting; the runner still never sets focus.
Other apps can cover the shown game. Native presentation, actual FG generation
and >=18 fps remain required. USER_PLAYING/unowned games still stop execution.

## Authorized live FG matrix, no focus forcing — 2026-10-29 14:46

Read `D:/BG3_Tools/USER_STEERING.md`, including both 13:57 entries and the
14:29 authorization. The current user instruction supersedes USER_AWAY waiting:
run the queued pair now unless USER_PLAYING or an unowned game exists. Neither
is present. `visible_unfocused_fg` preserves the planned FG 2/0 values with real
presentation and non-activating startup/placement. It never calls a focus setter.
The default `visible_unfocused` mode still forces FG off; the new explicit mode
does not. Actual generated presentation is still required for a quality pass.
Twenty-six policy/geometry checks pass. No renderer/default-key change.

## P1 now decided by FG ON; paired jobs prepared — 2026-10-29

FG `2` with real focused presentation is the MAIN deciding configuration.
FG `0` is reference only; its match does not decide whether a fix passes.
The user reports the visible bug only with FG on. No game launched in this
continuation; USER_AWAY is absent. No renderer fix was promoted or merged.
`USER_STEERING.md` was not found in this checkout or the other project/worktree
locations searched. The two 13:57 entries could not be read; the explicit
requirements restated in the user's message are applied here.

First completed: visible-unfocused launch mode committed/pushed as `6207338`.
It forces FG off and remains opt-in. The focused FG-on matrix does not use it.
Unfocused rotation acceptance and startup focus behavior remain unverified.

Pending replacement queue pair (QuickSave_3784, foreground, real presentation,
FG 2 loaded at startup so Streamline is available for both live configurations):

- `p1-fg-static-effects`: 820 s of one stationary view. Normal FG 2 / FG 0 /
  FG 2 windows prove sun, local shadows, GI, reflections and RR via per-pass
  counters/timers. Sun/visibility, local visibility, GI, reflection tier and RR
  normal-guide debug views each have three captures in both configurations.
  Sampled GPU camera origin/forward must match at native FP32 precision.
  Differences beyond repeated noise, preview precision and jitter are a parity
  failure that blocks the moving job. No difference has yet been measured.
- `p1-fg-motion-matrix`: 56 paired 60-second phases, 3,360 s total. Rotation
  60/120/240 px/s in both directions, slow/fast pans and static windows. Both FG
  configurations test baseline, `rt_scene_rebase=1`, and
  `fg_current_destroy_only=1` separately, not combined. It requires a successful
  static check from the SAME build. Combined job timeouts total 4,780 s.

Each phase reports moving primary match, rendered fps, actual displayed/rendered
ratio, SDK presented frames/status, all slDLSSGSetOptions calls, full-phase mode
toggles and interior toggles. Planned FG changes are separated from instability.
FG 2 must really generate frames (watch ratio above 1 plus SDK presented >= 2),
keep active RT and at least 18 rendered fps. FG 0 must really stop generating.
FG-on baseline must reproduce mismatch; a candidate must restore all FG-on
moving windows to the FG-on static repeat-derived level (about 99.2%). Missing
RT, ignored input, focus loss or incomplete evidence cannot pass. The destruction
candidate also needs a measured reduction in interior FG toggles to be retained.

Present-path code audit: the app present increments `g_frame` once; Streamline's
real/generated presents call the next driver's present and increment displayed
counters, not the app frame. `new_frame()` obtains a token and calls ReflexSleep
after the app present. The FG 0 reference retains this initialized Streamline/
Reflex path, so this is not a cold-no-Streamline comparison. There is no demonstrated
double frame-counter increment. Toggle/primary-window association is reported;
causation by Reflex or older native TLAS matrices remains UNPROVEN. No speculative
Reflex or renderer change was made. Existing FG-on logs lack this deciding
moving primary evidence; earlier FG-off positive controls are now reference only.

Seven CTests and 19 new matrix/static/plan tests pass, including midnight rollover.
No shader/ABI change;
only keyed epoch logging adds sampled GPU pose values. The old FG-off-only pair
is preserved in `captures/queue/held/fg0-superseded-20261029-1435/`. The new paired
jobs are pending until USER_AWAY under `captures/queue/pending/001-p1-fg-static-effects.json`
and `002-p1-fg-motion-matrix.json`.
Expected reports: `captures/p1-fg/static-effects-result.json` and
`captures/p1-fg/motion-gate-result.json`. Debug contact sheets will be under
`captures/p1-fg/static-effects-sheets/`; none have been captured yet.

## Answer: visible background queue mode is opt-in — 2026-10-29

Use job field `"window_mode": "visible_unfocused"` for FG-off tests only.
Default launch behavior is unchanged. The queue forces `fg=0`, `headless=0`,
real presentation and window-targeted PostMessage input. Native startup uses
SW_SHOWNOACTIVATE; the owned window gets WS_EX_NOACTIVATE and HWND_BOTTOM with
SWP_NOACTIVATE, without topmost or SetForegroundWindow calls. This mode does
not move the user's physical cursor or send global keys. Existing background
SDL hooks keep the game rendering; frame generation is deliberately off.

Limits: background rotation delivery and startup focus behavior are NOT verified
in BG3. The runner aborts if it observes the game taking focus, minimization,
off-screen placement or complete occlusion. If another app covers the entire
game, this cannot guarantee visible presentation without moving that app; it
fails instead. The rectangular visibility check is conservative for translucent
or shaped windows. It does not prove uninterrupted visibility between polls.
There is no synthetic WM_INPUT or focus-stealing fallback. Actual GPU camera
rotation and at least 18 fps still have to pass the P1 gate.

USER_AWAY remains required, and USER_PLAYING/user games still block launch.
No game/window-control calls ran during these checks. Passed: 22 policy/geometry
checks, six preflight tests and the offline queue (14 jobs / 15 fake runs).
Unfocused rotation fixtures are prepared in tests/fixtures/p1_rotation_unfocused*
but are NOT queued. The two existing foreground jobs remain pending unchanged.
No runtime pass or merge. FG-on P1 tests must use focused real presentation.

## P2 FG toggle candidate; RT cost log review — 2026-10-29

Code/log-only continuation: no game launch, no display changes. The two P1
rotation jobs remain queued and unchanged; foreground still requires USER_AWAY.
New `fg_current_destroy_only=0` stays off by default. No merge or runtime pass.

Retained user log 09:29-09:34: 27 host on/off transitions, peak 16 per 60 s;
all 13 host offs follow a watched-image destruction by 2-63 ms. This is the
available excerpt, not proof of the reported whole-session 30 toggles/min.
It also records 31 SDK interpolation transitions: 12 offs requested by our
host, three offs explicitly caused by `window not focused` while mode stays on.
Those three are not invalid inputs. Thirteen FG queue logs contain 50 host
transitions. Their same-timestamp readiness lines identify all 20 offs: 17 have
no missing/stale current input (`ready=1`, gate 0x80: destruction cooldown),
three have stale UI (`age=4`, gate 0x40) during swapchain recreation. These three
real stale-input shutdowns stay protected. Current-ready cooldowns do not alone
prove whether the previously destroyed image was current or retired.

The user excerpt's destruction log omits handle/current role, so its exact input
at each host off is UNKNOWN. Do not call hudless/UI invalid just because their
handles changed. Code does expose a concrete overbroad guard: destruction of
any historical watched image resets stability, even if all current inputs have
live replacements. With the key on, only destruction of a current depth/MV/
hudless/UI image resets it. Retired-image/view consumption-fence waits and
cleanup remain active. Missing/stale inputs still turn FG off. Each host options
decision now logs its reason, missing/stale role masks, all four handles/views,
extents, ages/layouts, API results and the destruction that set the cooldown.
Candidate benefit and CPU/GPU cost remain UNMEASURED; no shader or ABI change.

Existing GPU log samples, separate timing scopes:

| Sample | RT interval average | GI trace median | Local shadows median | RR evaluation + guides |
|---|---:|---:|---:|---:|
| Historical 19:20:51 | 4.269 ms | 0.466 ms | 0.378 ms | 2.590 ms nearby |
| Rebase-on 12:37:42 | 15.040 ms | 6.212 ms | 5.271 ms | 2.337 ms nearby |
| User CTY 09:30:56 | 0.937 ms | 0.359 ms | 0.534 ms | 2.708 ms nearby |

RR was on in both compared jobs; FG was off. The historical log explicitly has
headless presentation ON, versus real foreground presentation in the new job.
Both identify a 2560x1067 G-buffer. Scene changes WLD to CTY; GI 1 to 2 rays,
shadowed mode 3 to 1, roughness 0.6 to 0.8, rebuild 60 to 600. The new job also
enables full RT/LS statistics and primary checking. The 4.269 ms window contains
44 missing RT observations and GI p90 7.195 ms; later that same log is 1.943 ms.
The user CTY sample has the newer ray/LS/roughness settings yet costs 0.937 ms:
settings alone do not explain 15 ms. GI and LS are measurably slower, while RR
is not. No isolated primary/statistics timer exists in these logs, so the causal
split remains UNKNOWN. Do not add pass medians to interval averages or attribute
the difference to rebasing. The queued preflight compares primary/epoch off/on
with full RT/LS statistics enabled throughout; it is not a statistics-off benchmark.

Seven CTests and four log-audit tests pass. The CPU tests cover legacy cooldown
boundaries, retired-tag churn, every current role, missing inputs and recovery.
Evidence with source line numbers and every user off:
`docs/evidence/p2-fg-and-rt-log-review.json`; full audit:
`captures/p2-fg/retained-log-audit.json`. Kept: default-off candidate and diagnostics.
Rejected: inventing missing roles in the user excerpt, a confirmed FG fix or an isolated
15 ms regression from these logs. Next P2 evidence needs the logged role at an
actual toggle before any promotion; the P1 rotation gate remains pending.

## P1 rotation gate prepared; waiting for USER_AWAY — 2026-10-29

Implementation `da4b64d` is committed and pushed. No live game jobs ran in this
continuation. `USER_AWAY` is absent. Two jobs are pending under
`captures/queue/pending/`: `001-p1-rotation-preflight.json`, then
`002-p1-rotation-origin-ab.json`.
The latter requires a successful preflight from the same build. Foreground
authorization fields cannot bypass the marker; `USER_PLAYING` blocks launch.
Rotation result: NOT MEASURED. No merge, promotion or in-game check request.

The main A/B uses QuickSave_3784, real presentation, FG off, static windows and
mouse rotation at 60 / 120 / 240 px/s in both directions, key off/on in ONE run.
The gate requires actual GPU rotation, opposite yaw directions, increasing
measured speeds, active RT, at least 18 fps, and key-on primary match near the
static level (about 99.2%). Key-off must reproduce mismatch. Missing RT fails.
`rt_primary_epoch=0` adds optional GPU camera/source-pose and pool/soup hit
cohorts without another primary ray. Native instance-row epoch remains unproven.

The old exit at 242 s was before its 750 s deadline and before runner cleanup;
no matching crash dump or fault event was found. Cause remains UNKNOWN. Fixed
the observation gap: FG off previously skipped exception logger installation.
New default-off `crash_dump=0`, retained exit code/status, completion checks,
and post-analysis gates prevent interrupted jobs being called valid. The
360-second static preflight must establish stability before the 1,440-second A/B.

Previous static match off/on: 99.164-99.177% / 99.157-99.177%; cadence
17.60 / 17.50 fps. Previous moving rebase-on result is absent. RT interval cost
15.592 / 15.040 ms includes primary diagnostics plus GI/AO/sun work. Historical
4.269 ms used WLD_Main_A, RR GI 1 ray and rebuild 60 (new run: 2 rays, rebuild
600); that sample also had 44 missing scenes. The residual cost increase is
UNEXPLAINED, not attributed to rebasing. Preflight compares diagnostic off/on
and rebasing within the same static run.

Validation: six CTests, 14 Python gate tests, 18 input/permission checks, all
55 shaders compile, 21 ordinary SPIR-V binaries byte-identical, 20 rebase
variants validate. Offline queue: 13 jobs / 14 runs, including early exit,
failed analysis and forbidden-fault rejection; QA detectors and five merge-gate
scenarios pass. `rt_scene_rebase=0` stays default-off on the branch; ABI 16.
Evidence: `docs/evidence/p1-rotation-preparation.json`. Runtime reports will be
`captures/p1-pan-flashes/rotation-preflight-result.json` and
`captures/p1-pan-flashes/rotation-gate-result.json`. No new contact sheets:
the accepted primary-match gate takes priority over burst tooling.

## P1 rebasing gate FAIL / incomplete — 2026-10-29 12:38 Europe/Rome

Implementation 314840d is kept on the branch, default-off `rt_scene_rebase=0`.
No merge and no in-game confirmation request. ONE job `p1-origin-rebase-ab`
ran with foreground focus, real presentation, FG off and QuickSave_3784. It
exited after 242 seconds, before all four slow/fast windows. Exit cause unknown;
no NVIDIA 153, Aftermath dump or matching Application error was found. Queue
execution validity is not a quality pass. Automatic gate: FAIL / incomplete.

The initial static window drifted. The cursor was near the lower window edge;
it was centred in the owned game at 12:37:38.551. Later static off matches
99.164-99.177%; static on 99.157-99.177%, RT active with zero missing scenes in
the interior timing windows. Moving on/off: NOT MEASURED. Static median cadence
17.60 / 17.50 fps also misses the prepared 18 fps floor. The contaminated static
calibration is rejected, never relaxed into a passing threshold.

Static RT-query cost off/on median 15.592 / 15.040 ms; the off window includes
camera drift, so this is not a performance win. Five CTests, 21 unchanged normal
SPIR-V binaries, 19 validated variants, four gate tests, the QA detectors, all
five merge-gate scenarios and the offline queue scenarios pass.
Evidence: docs/evidence/p1-origin-rebase-314840d.json. Full log and runner clocks:
captures/queue/logs/p1-origin-rebase-ab.1.123412.log and .runner.txt.

Stop this bounded turn; do not run a second job. The rebasing hypothesis remains
unresolved. Next: fix the static/exit control before another quality measurement.
If displacement persists with rebasing, test whether native instance rows and
the GPU conversion camera belong to different G-buffer epochs. Record the same
draw/command identity and compare actual native GPU instance inputs with TLAS
rows; matching coordinate conventions or CPU record/submit values do not prove
matching epochs. Do not add another scene selector.


## P1 origin rebasing resumed — 2026-10-29

The user lifted the pause for one bounded fix and one presented A/B run.
`rt_scene_rebase=0` is a new default-off shader variant. It adds
lighting absolute origin minus scene absolute origin to all four query origins.
Ray distances stay unchanged; returned object transforms stay in lighting space.
The frozen local-light reference also uses the shared rebasing infrastructure.

Native static instance rows map object positions into the G-buffer camera-relative
world axes. Dynamic XFB recovers that same space using the native `_m2`. Existing
GPU extraction/audits compare the two paths. The new scene snapshot reads the
actual conversion camera UBO on the GPU into that scene's stamped set, avoiding
the failed CPU-pose selector. No new host/core ABI: paired ABI remains 16.

Five CTests pass. All 21 ordinary shader binaries are byte-identical to the
preserved baseline, and all 19 rebase variants pass SPIR-V validation. Four gate
tests reject missing RT, insufficient improvement and absent positive control.
One foreground static/slow/fast off/on fixture is prepared. Runtime gate pending.
The queue's hiding helper now leaves real-present windows visible. Display
settings still restore after the run. No merge or quality claim yet.


## PAUSED by user for CLI continuation — 2026-10-29

All activities paused; no owned game/runner active and no pending/running job.
Implementation f6b7ad9 is pushed on codex/p1-pan-flashes, paired ABI 16.
The camera-pose selector is NOT a fix: static off/on is about 99.55%, but
moving poses have no exact matching live scene, causing missing RT. Cancelled
INVALID. Both experimental keys remain off by default. Next idea is consistent
lighting-to-TLAS ray-origin rebasing; NOT implemented. Five CTests pass.
User gate remains moving primary match near static 99.5%, in ONE presented A/B
run with RT active. Burst images are optional; stop burst-tool work.
Read docs/HANDOFF_SESSION.md for exact logs, failed attempts and next steps.

## P1 match camera pose, not upload-ring address — 2026-10-29

The address-only selection is rejected: upload-ring offsets recur, so it found
a retired camera epoch (built 2446 versus present 2449) rather than the frame
being shaded. Second probe cancelled INVALID; full log preserved at
captures/p1-pan-flashes/0dff159-camera-epoch-full.log. Static key-off primary
match is 99.537-99.565%; moving key-off samples fall to 88.943-91.373%.
The keyed selector now compares the actual native camera pose: absolute xyz
and the view rotation, with exact finite equality. Projection jitter and unused
metadata do not identify the pose. Scene pose snapshots are taken from the
capture camera; the RT callback reads the lighting camera. Only live scene
slots qualify; newest matching build wins. Instance transforms still come from
that captured draw stream at submit. Key remains off, ABI 16. Five CTests pass,
including camera rotation/translation changes versus projection jitter.
Next: the same-run primary-counter A/B with normal rendered colour.

## P1 exact camera versus present age — 2026-10-29

First camera-selection probe: 1,800 / 1,800 queries redirected to another
scene, zero missing camera identities. The old age guard rejected the matched
scene, producing 1,046 missing-RT frames in the first logged window. Probe
cancelled as INVALID; missing RT is never quality improvement. Full log:
captures/p1-pan-flashes/0edf4ed-camera-match-full.log.
Exact camera selection now uses live-slot retirement instead of present age;
the core no longer rejects an exact match solely because it was recorded two
presents earlier. Dropped builds and retired slots remain rejected. Camera
identity is assigned only after a successful scene commit. ABI remains 16.
rt_primary_check=1 reuses the existing GPU primary-check producer for counters
without burst capture or a debug-colour overlay. Default 0; no shader changes.
The same-run off/on fixture enables this counter in every phase. Four CTests
pass. Moving primary-match gate remains open until this corrected A/B runs.

## P1 renderer alignment takes priority — 2026-10-29

User accepts the real-present FG-off primary-ray evidence as the positive
control: static 99.53-99.57%, slow 89.25%, fast 31.40% (36.1% in front,
27.9% behind). Burst images are optional and no longer block a renderer fix.
First fix hypothesis: latest-global scene selection can pick another camera
epoch. rt_scene_camera_match=1 selects the live TLAS/set pair by the lighting
camera UBO buffer and offset; default 0 preserves the ordinary selection.
Host/core are paired ABI 16. Native instance rows still come from the recorded
draw stream at submit; no guessed frame offset or shader-space shift.
Build and four CTests pass. Next: ONE real-present job with static and slow/fast
pan phases alternating the key. Gate: moving primary match reaches the static
level, approximately 99.5%, with RT active. Missing scenes are not a pass.
No burst-tool work or default promotion before this measurement.

## P1 presentation-counter capture race — 2026-10-29

The normal-rate positive run is INVALID: final colour was captured in only
6 / 4 / 7 / 8 of the 16 frames in static/static/slow/fast bursts. All other
channels have 16 samples. No flash absence or renderer quality claim follows.
The depth-to-tonemap association now follows the same command-buffer recording,
not a present counter that can advance during recording. The manifest retains
the recording identity and the analyzer rejects mixed recordings. No renderer
fix, ABI change or GPU-copy change. Four CTests and 14 analysis tests pass.
Repeat the positive control on this build before considering a lag correction.
Evidence: docs/evidence/p1-pan-08a0256-positive.json.

## P1 complete-burst transport and analyzer correction — 2026-10-29

Final capture now contains 176 samples / 16 frames. All required image channels
are finite; unused native camera metadata is preserved as bytes, with matrix/
origin fields validated separately. No copy-image VVL errors, but other errors
remain, including one unattributed buffer-usage error. Validation cadence is
11.86 fps and is not the positive control. Evidence: p1-pan-final-transport.json;
full log: captures/p1-pan-flashes/08a9c39-final-validation.log.

Corrected jitter/static classification and footprint-bounded texture matching;
static common coverage rises from 604 to 766,671 pixels on the same data. Noise
is calibrated per matching world sample, with native format precision, rather
than taking a global maximum that made binary-mask detection impossible. A
second static burst is independent evaluation. 13 analysis tests pass; no lag
fix or default promotion. Next: one real-present FG-off positive run, serials
11/12 static and 13/14 moving. Contact sheets pending that complete run.

## P1 capture continuation — 2026-10-29

Resumed from the interrupted pan-flashes conversation at a0bb416. Its owned
positive-control process finished without a build change or driver event.
All three bursts lack final colour and are quality-INVALID despite successful
transport. Static/slow/fast burst rates are 18.82 / 16.84 / 18.82 fps. Primary
match windows are 99.534% / 88.044% / 30.922%. GPU conversion-camera origins in
the fast burst match lighting origins at offset +1 throughout the ten tested
centres; this does not prove instance-transform timing or a visible flash.
Evidence: docs/evidence/p1-pan-a0bb416-positive.json. No renderer fix applied.

The capture now reads native hudless tonemap output, using the existing tracer/
Streamline signature. Swapchain attachments alone missed the interposed final
path. Missing final data remains invalid. Packed HDR Inf/NaN decoding is fixed.
All six analyzer tests and three CTests pass. First verify complete transport
with validation, then repeat the same-run positive control at comparable fps.
The latest user permission allows visible second-plane presentation without
USER_AWAY; never steal focus, run with USER_PLAYING or interfere with a user game.
FG stays off first. The automated plan restores display settings on exit.

## P1 transient shadow flashes resumed — 2026-10-29

Positive control is mandatory before any renderer fix. User permits a visible
second-plane window while using the PC; USER_AWAY is not required by the latest
instruction. Never run with USER_PLAYING or a user-owned game. Use real present,
normal frame rate, FG off first and FG on if reproduction fails. Restore display
settings. Report mode and measured burst fps, not just the uncaptured baseline.

Transport smoke 56d0d4a: real presentation, FG off, background focus protection,
static camera, roughly 20 fps outside capture. Primary match 99.563%. The analyzer
correctly rejects all 16 frames because the final image is missing. No symptom
reproduced yet; no lag fix. First-burst shader compilation also confounds cadence.
Next test fixes swapchain-image classification, prewarms diagnostic pipelines and
captures the conversion camera on the GPU alongside its scene slot. The normal
renderer remains uncorrected for the required positive control.

The user explicitly resumed P1 with transient dark walls while panning. This
supersedes the old pause and P2 priority below. FG toggle errors and the roughly
18 fps base rate follow P1. Branch: `codex/p1-pan-flashes`.

Experimental 16-frame burst producer and automatic flash analyzer added.
`pan_capture=0` remains the default. ABI 15 adds the host readback callback;
both DLLs are rebuilt together. No lag fix or default promotion yet. Runtime
transport and the gate are pending. Contact sheets: not generated yet.
User log: `captures/p1-pan-flashes/source-logs/user-0929-0934.log`.
Three CTests, four synthetic analysis tests and existing QA detectors pass.
All 34 shaders compile; seven ordinary shader binaries are byte-identical.
Record/submit age alone is not camera-pose lag.


## User pause and model handoff — 2026-10-29

The user paused game activities for a live test and requested a handoff for a
new conversation/model. The thread goal is PAUSED. USER_PLAYING remains present;
no test/runner or pending job is active in the latest check. Do not restart work
or remove the marker until explicit resume. Read docs/HANDOFF_SESSION.md first.
P1 and P2 trees are clean and pushed (dbd320c / 7a87ba7). No rendering or game
settings changed while preparing this handoff. Earlier active-state entries
below are historical and do not override this pause.

## Current checkpoint - 2026-10-29 09:03 Europe/Rome

- Main rendering remains 6dba0a8; local-light reference, water design and legal QA hold are merged.
- P1 requested fix and static/moving measures are complete; P1 quality remains incomplete.
- Static 301 fails original-PRE energy (+0.460%); all 23 GPU cases and raw safety checks pass.
- Cadence-controlled pan 303 has motion +1.162%, raw residual +21.951%, cohort energy +11.193%.
- Keep the GI candidate off. PAUSED.md preserves its evidence and resolved-image return point.
- After explicit resume, P2 is next under the stalled-phase rule; the goal is currently paused.
- D-EM 295 rejects additive overlap: 67,031 receivers, zero multi-draw; native blend is disabled.
- AF emissive family still fails 0.362 against 0.842-1.188. Next smallest probe is native facing.
- No GI, EN2, D-EM, box-light or OMM candidate is promoted. P3-P6 gaps remain documented.
- P2 is synced from main at 7a87ba7 (AGENTS/docs only); no game is active.
- Evidence and branches are pushed. Reset used; backups and shared junctions remain protected.

## Resumed — 2026-10-29

P1 resumed by the user. The previous park decision is superseded. No new
attribution, exposure or provenance research before the static energy and
single-run pan measurements. The new AGENTS.md applies to every branch.

P1 commit 0a0a9d2 keeps the complete fresh rejection fallback and adds a normal
cone derived from FP16/oct16 precision and the real GI footprint. Only TLAS
neighbours on the same geometric plane widen the shading-normal bound. The
actual history texel still must pass depth and normal checks. No denominator,
ray weight, camera payload or accumulation reset changes. GPU self-tests cover
20 cases; all 12 CTests and 24 compiled shader ABI checks pass. Seven ordinary
GI/LS/sun/RF shader binaries are identical to d9cb628. Defaults remain off.

Test 296 finished execution-valid. All 12 captures passed all 20 GPU predicates,
finite GI and exact saved alpha. Normal rejection was 13.45%. Energy remains
inconclusive: preserve the final gate-off baseline spike; original-PRE change
was +0.350% and its interval excludes zero. No pan gain is claimed. Trace median
was 1.260 ms; whole GI was 56.466 ms under validation and duplicated experimental
passes. No scoped layout/push/write VVL errors appeared; other errors remain.

Repeat 297 finished execution-valid on b6165ce. All 36 captures are finite,
saved alpha is exact and all 20 GPU predicates pass. Normal rejection was
14.70% of 291,499,658 queries. On/off energy +2.142%, paired CI
[+0.00005157, +0.00068514] Y: FAIL. Original-PRE +0.271% includes zero narrowly;
this cannot waive the paired failure. Keep every sample, including two high
original-PRE values in the on cohort. Trace median 1.273 ms; whole GI 54.986 ms
under validation. No scoped layout/push/write VVL tags; other errors remain.

Shuffled static 298 finished execution-valid but still fails energy parity:
on/off +0.8243%, CI [+0.00005181, +0.00023276] Y; original-PRE +0.3690%,
CI [+0.00003676, +0.00009067] Y. All 36 samples retained, finite, alpha-exact,
20 GPU predicates pass, max static shift 0.44 pixels. Normal rejection 14.22%.
Trace median 1.734 ms; whole GI 53.658 ms under diagnostic validation.
No scoped layout/push/write VVL tags; other VVL errors remain. No pan pass.

4e11688 fixes a concrete early zero return: absent/nonfinite previous colour
now attempts the same complete fresh fallback when rejection is enabled. The
old availability-cohort restriction is superseded; control behavior is unchanged.
Unsupported current sources remain unavailable. This is not a claimed explanation
of the positive bias. All 12 CTests, eight energy tests and 24 shader ABI checks
pass; seven ordinary shaders are byte-identical. Static 299 started at 08:18:39
on that frozen commit, using the same shuffled fixture and a 90-second hold.
299 completed on 4e11688. All 36 samples are finite, alpha-exact and pass all
20 GPU predicates. Max static shift 0.621 pixels, normal rejects 14.03%.
Both energy intervals include zero, but power is LOW: on/off +2.651%, relative
interval about [-1.093%, +6.395%]; original-PRE +0.2126%. This does not erase
297/298 or establish equivalence. Trace median 1.242 ms; whole GI 52.880 ms
under validation. No scoped layout/push/write tags; other VVL errors remain.

ONE moving diagnostic 300 started at 08:28:17 on 45a275f, identical renderer
to 299. It uses 24 balanced four-second blocks and a 96-second hold, private/
self-test diagnostics off. Report residual with energy, coverage and motion;
no quality promotion follows from the weak static result. Next concrete normal
fix: the half-step FP16 bound underestimates conversion error permitted by
Vulkan. Replace it with exact neighbour intervals after 300 is terminal.
Specs: docs/GI_NORMAL_FOOTPRINT.md. P1 remains active.

Pan 300 completed execution-valid on 45a275f, identical renderer to static 299.
Off/on residual 0.1359554 / 0.1384546 (+1.838%), cohort GI Y -15.445%, motion
67.80 / 80.14 pixels (+18.197%). No demonstrated quality win. Samples 28,648,686 /
21,195,860; grid coverage 25.01% / 18.67%, much wider than the old 0.08% cohort.
Trace median 0.550 ms; whole GI 65.771 ms under the experimental duplicated
pipeline. Validation was off; preceding full 299 VVL remains preserved.

4ba5bfc fixes a derived normal-bound error: Vulkan format conversion may choose
either adjacent FP16 value. The previous half-step bound can be short by factor 2.
The new bound uses actual neighbours and marks its metadata convention. No angle
tuning, source/ray/resource/ABI changes. Existing spatial support is unchanged.
The endpoint sweep covers 31,743 finite nonnegative intervals. GPU mode 1 now
checks 23 cases (row 12 [8388607, 8388607, 23, 1]). All 12 CTests and 24 shader
ABI checks pass; seven ordinary shaders are identical. Static 301 started at
08:35:24 on frozen 4ba5bfc. Inspect runtime evidence before the corrected pan.
See docs/GI_NORMAL_FOOTPRINT.md and
[Vulkan format conversion rules](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html).


Static 301 completed execution-valid on 4ba5bfc. All 36 captures finite and
alpha-exact; all 23 GPU cases pass, max static shift 0.36 pixels. Normal rejection
13.24%. On/off +0.6464%, paired CI [-0.00005839, +0.00028256] Y includes zero;
original-PRE +0.4599%, CI [+0.00003984, +0.00011942] Y still FAILS. No complete
energy-parity or quality claim. Trace median 1.250 ms; whole GI 52.621 ms under
validation. No scoped layout/push/write tags; other VVL errors remain. Retain
full evidence on P1, update the branch from main, then one corrected-precision
pan diagnostic. No repeated unchanged static runs seeking a favourable interval.


P1 updated from main as d836c09 while no game was active. AGENTS.md and shared
QA/reference fixes are present; GI source and paired binaries are unchanged.
All 12 CTests, 49 shader files, QA detectors, eight energy tests and five merge
gate scenarios pass. No experimental phase was integrated into main.
Corrected-precision pan 302 started at 08:47:17 on frozen d836c09, one session
with 24 balanced four-second blocks and a 96-second hold. It is diagnostic only,
given static 301's original-energy failure. Preserve all evidence and defaults.


Corrected-precision pan 302 completed execution-valid on d836c09. Off/on residual
0.1393289 / 0.1372433 (-1.497%), cohort energy -17.904%, motion +19.888%.
No quality gain. Samples 34.4 / 25.9 million, grid coverage 29.75% / 23.01%.
Trace median 0.551 ms; whole GI 65.059 ms under the experimental duplicated
pipeline. No VVL in this moving run; full 301 validation remains preserved.

Test 303 started at 08:54:05 on frozen 8fe00de. Only the existing fixture changes:
fps_cap=8 in both modes, same balanced 96-second pan. The common cadence tests
the mode-dependent camera step confound without new GI or camera logic. Require
actual framecap/watch logs and report energy, motion and coverage with residual.
Static 301's original-PRE gate still fails; no promotion is permitted.


Pan 303 completed execution-valid on 8fe00de. The 8 fps cap is verified by
19 complete windows of 40 frames per 5 seconds. Off/on motion 93.57 / 94.66
pixels (+1.162%), raw residual 0.1114922 / 0.1359662 (+21.951%), cohort energy
+11.193%. No quality gain. Samples 20.96 / 14.45 million, grid coverage 31.49% /
22.16%. Trace median 0.548 ms; whole experimental GI 64.288 ms. No VVL in this
moving run; full 301 validation is retained. The requested P1 fix and measurement
sequence is now recorded, with the candidate off and the phase incomplete.
The stalled-phase rule advances the priority slot to P2; the overall goal stays
active. P1 dbd320c PAUSED.md preserves the next useful resolved-image/source test.

Audited preserved D-EM 295: 67,031 target receivers, 71,754 triangle candidates,
zero distinct-draw overlap or invalid/overflow/missing counters. Nine actual
native draw records show blend disabled and back-face culling. Additive-stack
hypothesis rejected for that cohort; no brightness scaling. AF 304be1c7 remains
0.362 against 0.842-1.188. Final captures finite, native D32F nonfinite values
preserved. Build median 0.496 ms, effects passes 0.118 ms with capture only.
The short hold-45 diagnostic is not a promotion gate. Evidence on P2 at 1922260.
Next: sync main, count committed facing using existing debug resources, and
compare native front-face state before any physical change. No new ABI needed.



Previous goal turn made progress: merged a valid legal QA exposure-hold fix,
rejected P1 source consistency again with valid held data293, narrowed native
box residual291 and recovered the archival junction incident. No rendering
candidate was promoted. P1 park file2914bde now preserves293 and next constraints.

P2 D-EM next hypothesis: nearest-hit-only emissive lookup may miss coincident
native additive/ordered draws. AF family304be1c7 still fails0.361/0.362 while GF
passes0.999; UV/pulse hypotheses already checked, no coefficient or gate tuning.
New codex/p2-emissive from main6514a13 selectively cherry-picks consolidated D-EM
work through9f4d17d (69b4eaa), preserving original branches/history. Defaults/release
rt_emissive0, no scene allocated while disabled; startup required to enable.
ca1959b adds default-off observer using reserved push/stats words: same-family
triangle candidates vs distinct draw geometry, explicit invalid/overflow/missing.
It changes NO emission/calibration/light list. Actual native draw blend/cull/depth
state logged separately; dynamic state/multiplicity alone cannot prove addition.
Build/3 CTests/36 shaders pass, seven ordinary GI/LS/sun/RF binaries identical main.
SDKs use explicit paths and owned small SDK/DB copies; no shared junctions.
295 LIVE frozen ca1959b, runner2692 at07:11:07 after fresh idle/playing checks:
AF debug0 with scene-only legal hold; inspect actual full native-state log,
post-ready probe populations, complete VVL/captures before interpreting results.


Archive incident and recovery: the managed-worktree archival tool followed the
qa-exposure-hold checkout's NTFS junctions for external and data/assetdb, emptying
the shared targets. It was caught by immediate existence checks. No game was
launched afterward. Restored all SDKs from D:/BG3_Tools/external_backup and the
asset DB from snapshot-20261028/files/data/assetdb, reading backups only. All992
SDK file SHA256 hashes and both DB/manifest hashes match those backups exactly;
DB7,961,840 bytes, SHA25617af233f58e552fbe28e78738d9c3153dab954ea39cca3eece9fb4bed93e637a.
Sources/pushed branches and user saves were outside the removed target paths.
NEVER archive another checkout while shared dependency/data junctions remain.
Recovery manifest is docs/evidence/qa-archive-shared-dependencies-recovery.json.
Build/3 CTests/32 shader files rechecked after restoration. No feature promotion.


QA exposure-hold fix MERGED7a98949 through merge-green after actual stable-tree
294 PASS129s/61s session.388 successful writes, held EV13.0581 matches both
exposure buffers/native lighting scale, re-exposure k1. No hold refusal/usage/
layout/push/write errors, both captures/ALL dumps finite, no faults/dumps/build
changes. Other VVL categories unresolved; no global-clean claim. Merge gate
passes full build/3 CTests/32 shader files/all QA detectors. Main and isolated
codex/qa-exposure-hold9612419 pushed; no P1/P3/P4 rendering feature merged.
Reusable smoke fixture defaults to stable test tree. Logs/VVL/captures copied
under main captures/qa-exposure-hold-294 before temporary checkout
archival (confirmed). Original/current source math remains unchanged with hold0. P1 research
stays off: legal held exposure still fails original PRE energy in293. Shared
queue idle after294; next launch needs fresh user/process/queue checks.


293 valid execution210s/142s scene session confirms legal exposure writes:
0 hold usage errors/refusals; held EV13.0522 matches both probes after472/1072
successful writes. All12 captures/dumps finite, saved alpha exact, pose valid,
static<=0.54px. All SIX gate-off controls are bit-identical RGB to original PRE.
Original PRE consistency still FAILS+0.595469%,95%Y CI[+0.0000262039,+0.000185806].
Paired gate on/off-0.0694147% has CI[-0.000774917,+0.000750046], half width4.26%
of offY: includes0 but NOT equivalence. Exposure variation alone is insufficient;
no new transport/compensation/brightness tuning or GI promotion. Full layout/
push/write categories absent; other VVL unresolved. P1 evidence/spec d9cb628 pushed.

The QA fix is isolated from ALL P1/P3 work on codex/qa-exposure-hold6523aaa
(stable main ABI8, worktree qa-exposure-hold/BG3RTX): cherry-pick4941657 of5f84d9b
plus smoke fixture. Full build/3 CTests/32 shader files/all QA detectors pass.
294 LIVE runner17296 at06:44:18 after fresh idle checks, two scene-only held
captures/full VVL. Freeze isolated tree; inspect actual held probes, no refusal/
usage errors and finite output before merge-green. This improves the test suite,
not production lighting. Temporary worktree should be archived after integration.


P1 292 is INVALID despite a superficial statistical pass. The pre-existing
qa_exposure_hold used CmdUpdateBuffer on a native STORAGE_BUFFER without
TRANSFER_DST: full VVL reports1262 dstBuffer-00034 errors. Never interpret its
apparent+1.4378% paired/+0.4944% original-PRE intervals (both broad/include0) as
valid exposure isolation. Six off controls were RGB-exact, saved alpha/raw/pose
passed, static<=0.4px and all dumps finite; these do not waive invalid usage.
Queue also missed a startup-only mouse line outside the scene slice; full log
proves actual SDL3840x2160/client centre1920,1080/static1. Evidence292 retained.

Separate fix5f84d9b uses a one-thread storage shader on the ACTUAL adaptation
dispatch binding1/1, including dynamic offset, storage type/24-byte range checks,
existing descriptor12/push/ring, compute dependencies and restored native bindings.
Missing input/pipeline REFUSED, successful writes only counted on dispatch. No
new native allocation/usage/ABI/transport or production exposure change.12 CTests
and11 post shader entries compile; ten previous entry binaries identical3d411aa.
bfb723b records invalid evidence and replaces the session's startup-only expect
with recurring PRE metadata.293 LIVE frozen P1 bfb723b, runner308 at06:40:14 after
fresh idle/playing checks. Require no hold refusal/usage error plus full audits
before interpreting the repeated12 static AB/BA captures; moving gate still open.


291 execution PASS139s/48s session, nine loaded-scene windows:2,950,041,600
receivers,2,947,407 failing (0.0999107%),17,700,249,600 bounded channels with
15,775,122 violations; invalid0/unbounded0. Mean outside-interval excess3.50383e-6Y.
Standard class2,918,571 failures/2,804,123,970 receivers, skin26,926/947,525;
remaining classes small. Raw diffuse diagnostic d156 shows sparse red points on
metallic rims/details and characters, not a cause proof. Raw projector residual
+0.599412% has a different cohort.62 modules/no refusals/faults/dumps/build changes.
Final finite; native D32F6402 nonfinite retained. Full scoped layout/push/write
categories absent, other VVL unresolved. Evidence291 and spec committed b913e8e.
Native accuracy, per-query unknown delta, cookie filtering/moving quality OPEN.

Return to P1 source-epoch isolation using EXISTING qa_exposure_hold1:3d411aa adds
only a fixture/spec, no rendering change or exposure payload. The hold is applied
after loaded-scene session readiness (not menu), with15s warmup,12 static AB/BA
traces, production gi_strength1/gi_ao1/shared stream/geometric gate. Require actual
held writes, finite/static/saved alpha and paired/original-PRE noise intervals.
292 LIVE runner3980 at06:32:25 after fresh idle/playing checks; P1 frozen until
terminal. A static isolation result cannot close P1's required moving gate.


P3 290 execution PASS,145s/49s session, eight scene windows. All shaded receiver
cohort2,622,259,200 pixels/15,733,555,200 bounded channels: invalid0/unbounded0,
2,605,305 failing pixels (0.0993535%),13,986,533 violating channels, mean excess
outside derived UFLOAT intervals3.48717637e-6Y. Raw projector-only residual is
+0.599347%; populations DIFFER, do not subtract their means to claim an exact
explained fraction.62 controlled modules, NO whole-log refusals/faults/dumps/
build changes. Final finite, D32F2481 nonfinite retained; full layout/push/write
categories absent, other VVL unresolved. Evidence290 on P3; quality still OPEN.
9beb1e1 adds material-class population/failure counts and debug4 violation map
through existing diffuse output (red above, blue below, green invalid, cyan
unbounded). No new ABI/resource/transport; all six ordinary shader binaries are
identical56bcc06, five CTests/two diagnostic shaders pass.291 LIVE runner37908
at06:26:41 after fresh idle checks. P3 source/binaries/HEAD frozen until terminal.


P3 289 is terminal, queue INVALID: a one-time box-active message occurred before
its scene slice. No result is retroactively passed. Nine actual scene windows
cover 2,932,082,331 projector receivers: native Y0.0292808848, recompute0.0294563717
(+0.599322%), abs error0.0001767432, invalid0. All62 native modules controlled;
whole log has NO refusals. Final finite, native D32F d130 has2604 nonfinite.
Full layout/push/write categories absent; other VVL categories remain unresolved.
Evidence is committed on P3. Initial reset refresh lag resolved: fresh weekly0%,
ordinary usage allowed; the single authorization is consumed, credit balance0.

8ec56ab tests the storage-format hypothesis without waiving the residual:
Vulkan allows either adjacent UFLOAT value, so each loaded R11/G11/B10 component
defines its own open neighbour interval. Diffuse/specular RGB checked separately
on ALL shaded receivers; finite maxima inconclusive, invalid values explicit.
No tuned percentage or model-error allowance. Shared HLSL/C++ code is exhaustively
checked against an independent decoder (2,974 bounded codes plus maxima), 5 CTests
pass, two diagnostic shaders compile; all six ordinary variants byte-identical.
Recurring scene box-active stats replace the startup-only queue expectation.
290 LIVE frozen P3 8ec56ab, runner33888 at06:21:53, after fresh idle/playing checks.
No native-accuracy/default/merge approval; wait terminal and inspect full audits.


Usage reset consumed once after fresh weekly100% reading, per explicit user
authorization. Tool outcome reset, credit balance0; idempotency key
8ce84295-c9a8-4ba8-bbfb-cf1f81dd0af7. Initial quota refresh lagged; do not retry
a successful logical reset or redeem another without fresh authorization.


P3 7adc91d adds startup-only native-direct validation (default/release off).
Recognized native compute scalar reads of set1/b0 flags become AO enabled,
direct-AO coefficient0 and sun-mask enabled. Native UBOs/descriptors/BRDF/cookie
code are untouched. Actual successful variant follows module -> pipeline ->
command state -> dispatch; core refuses missing/mixed controls and unsupported
modes. Separate owned validation full/full-box variants use matching direct
factors1; all six ordinary/default/FULL/check/box shader binaries are identical
to56bcc06. Paired host/core ABI9; no reuse of P1's ABI14 process.
Offline semantic/foreign-interface/unsupported/malformed tests pass with release
assertions enabled. All62 captured native lighting modules transform exactly
three scalar loads/components11 and pass spirv-val. Full build/4 CTests and both
new owned shaders compile. These are not native-accuracy or quality passes.
288 LIVE frozen7adc91d, standalone S6 two traces with full unlimited VVL. Runner
37748 started05:58:38. Require actual applied modules, positive signals, no REFUSED
lines and full layout/push/write audit before interpreting error numbers. Atmosphere
in-scatter/transmittance remains a possible residual, not a blanket waiver.


287 S7 cap1 exact-population control PASS: valid 115 s / 49 s session, seven
complete windows total 640,149,005 traced queries, 192,085,618 occlusions,
7,637,662 unknowns and 10,083,681 cookie evaluations. Final/all captured dumps
finite; no driver faults, dumps or build changes. This establishes actual path
coverage, not independent per-query delta identity, native cookie accuracy or
moving quality. P3 56bcc06 is pushed; shader transport is unchanged by integer
logging. Main rendering remains unchanged and every feature stays off.
Shared queue is idle. Further uncontrolled AO1 smoke repeats cannot close native
accuracy: the next P3 intervention must isolate native direct lighting without
both ambient contamination (debug6) and direct suppression (debug4/native flag1).
Inspect native flags/binding ownership before any replay/override implementation;
use owned inputs and actual-submit lifetime for any replay, and preserve the
unresolved validation categories. P1 source-epoch/conditional mismatch and unsupported unlit shading
remain unresolved; no candidate promotion, denominator or brightness tuning.


286 recursion-off isolation still FAILS original-PRE consistency: valid 198 s /
195 s exclusive, static <=0.16px, finite/pose/actual alpha pass, no faults/dumps/
build changes. Gate +0.8129% has broad paired Y CI [-0.000058595,+0.000336572]
including zero (half width about1.16% offY, not equivalence). Original POST/PRE
+0.4292% CI [+0.000014482,+0.000132254] excludes zero. Off mean changes remain
below0.0024%, with41..67 RGB differences explicitly retained. Recursion removal
alone is insufficient; keep complete production lighting. P1 725955f pushed.
Existing lagged native exposure probes were inspected before proposing another
payload: reported applied-k ranges271 0.999264..1.00135,280 0.999787..1.00011,
286 0.999477..1.00019 (28 probes each). They do not establish a dominant source
of drift and are not bounds on all history reads; no new transport added.
287 LIVE frozenP3 b97477b: S7 cap1 with EXACT integer unknown/cookie counts.
Runner20748 started05:31:48; no repeat from rounded0.000-rate assumptions.
EN2 offline context: its root HANDOFF.md is stale sun-replace history. Actual
EN2 source/spec is layer/hitmat.cpp, shaders/rt/rt_hitmat.hlsli and TRACK_D_RT13,
branch0916cff; no edits there. Existing blue/metalness/class mismatch failures
remain open, rather than being excused by join coverage or narrower cohorts.


285 bounded S7 retry PASS for ray/cookie coverage: valid116s/48s session,
seven complete windows; last traced0.295/px, occluded0.092/px, cookie0.004/px.
Final nonfinite0 and all captured dumps report0; no faults/dumps/build changes.
281 remains a separate startup failure, not evidence against the box shader.
Native cookie filtering accuracy and unknown fallback are not proved by this
smoke. P3 now appends exact traced/occluded/unknown/cookie counts from existing
counters:0.000 rates cannot prove count0. Host-only change, build/3 CTests pass;
S7 cap1 fixture requires positive integer unknown and cookie populations.
286 LIVE frozenP1 7bb43b5: gi_strength0 static12 isolates recursive-source
coupling. This changes the lighting composition and is not a production fix,
energy waiver, quality pass or permission to drop multibounce lighting.


284 strict positive-old-loss gate FAILS: stable 135 s / 133 s exclusive,
no driver faults, dumps or build changes. All fresh failures are unlit; up to
25/26 have positive old energy. Weighted failed-old Y reaches 6.7333e-9,
versus total positive replacement delta 1.40043e-4 in the same window. The
small loss is NOT waived, but cannot explain the much larger positive drift.
Reasons close exactly; invalid sums 0. Full layout/push/write VVL categories
absent; other categories unresolved. Raw GI finite/alpha exact; off controls
have 45/54 RGB differences (-0.00206/+0.00299% mean), explicitly retained.
On captures are +0.859/+0.688% versus PRE. No denominator reset or lost bounce
explains the main excess. P1 7bb43b5 pushes evidence and prepares existing-key
gi_strength0 static12 isolation of recursive/source-epoch coupling. A changed
lighting model is diagnostic only, never a fix; unsupported unlit shading stays
open and must not be replaced by trusted rejected history or an altered gate.
285 S7 bounded startup retry LIVE on frozen P3 b97f4d4. Unlike attempt281,
actual game37436 started05:22:01, creates a fresh layer log and is loading the
requested scene. Runner39080 started05:21:59. Do not relaunch it or infer success
from startup alone. Next runnable P1 control follows this job's terminal result.


283 S5 shadow coverage PASS: valid 113 s total / 49 s session, ten complete
windows, evaluated/traced 0.079 per pixel and occluded 0.029. Final nonfinite 0;
a D32F dump reports 2,015, retained without a global finite-output claim. No
faults, dumps or build changes; cookies, shadow-mapped and unknown rates remain
0.000, so those cases are not proved. P3 b97f4d4 is pushed.
284 LIVE on frozen P1 c3b924f, four static off/on/on/off captures and full VVL.
Require reason/count closure, invalid 0 and no failed POSITIVE old energy;
inspect every window, actual alpha and off-control parity before interpreting
the weighted means. Main handoff is condensed to 144 lines with exact live-job
ownership and the remaining quality limits. Next bounded S7 startup retry.


282 weighted attribution is execution/expect INVALID:9 fresh failures among
9,094,425 replacements, invalid sums0; reason totals close exactly. The mixed
window old/fresh weighted Y0.004275577/0.004360368 (delta+0.000084791).
Full layout/push/write VVL categories absent; other categories remain unresolved.
Raw finite/pose/alpha pass; off capture has53 RGB differences/2.732M pixels
(+0.00353% mean), so no exact diagnostic-control parity claim. On+1.908% PRE.
P1 c3b924f keeps the original selection condition and adds failed legacy-energy,
strict failed-positive counts, and unlit/other failure classes (201 counters).
No return value, fallback, source or output changed to meet an expect. Next
four captures allow pure windows; require invalid0 and ZERO failed-positive
energy, rather than assuming nine failed samples either matter or do not.
Build12 CTests/24ABI pass; candidate ready after current P3 job.
283 P3 S5 normal shadow coverage LIVE on frozene56cf88. S7 startup281 remains
unverified; try one bounded retry later, without touching saves/mods.
Usage checked:93% weekly used, one reset available but NOT consumed. User
condition is exhaustion; preserve that authorization for one use at the limit.


280 native-AO isolation FAIL: valid188s/185s exclusive, static<=0.16px,
finite/pose/actual alpha pass; all six off captures match PRE RGB exactly.
Gate on/off+1.1697%, pairedY95%CI [+0.000041581,+0.000373752]; original
POST/PRE+0.8004%, CI [+0.000040715,+0.000242889]. Both exclude0. Removing
the native GI-AO fold alone does not explain source drift; no AO/cap change.
P1 aaa3754 adds diagnostic mode4 for old versus fresh Y*w/rays on the SAME
rejected ray, including fresh failure, rejection reasons and all receiver pixels.
Q24 carries/bounds use receiver bins and actual width/height/rays,196 counters;
no output/weight/visibility/source/camera change. Exact source/independent rays/
adaptive fallback2 are refused. Build12 CTests/24ABI pass, seven ordinary GI/RF
shaders byte-identical. 282 LIVE frozenaaa3754, two static off/on traces with full
VVL; inspect invalid/failed/count closure and all layout/push/write categories.
281 S7 INVALID at startup: exits2s before new layer log or scene-ready; no driver
fault/dump or box result. P3 state/evidence is pushed; try S5 after P1 and a
bounded S7 retry without save/mod changes. It was historically loadable, not
proved currently loadable. P3 branch3075e38 at attempted launch.


278 actual box shadow execution PASS: normal GI/debug0, valid111s/49s session,
ten complete120-frame windows; evaluated/traced1.319/px, occluded0.738/px,
unknown/cookie0.000/px. No faults/dumps/build changes. Final/lighting finite;
D32F dump reports4590 nonfinite values, retained without global-finite claim.
279 cap1 unknown-coverage attempt INVALID: execution117s/49s session is stable,
but reported unknown box population stays0.000/px. Opaque committed hits can
finish before the nonopaque-candidate cap; do not claim branch verification or
relax the expect. P3 prepares S7/S5 shadow coverage on known-loadable saves.
P1 raw271 visibility over32.778M samples: exact0 only0.0211%, but below the
0.001 give-back denominator covers3.2255%. Frequency only, not source-energy
attribution or proof of native flags at Camp. 280 LIVE frozenP1 2df23ae:
existing-key gi_ao0 static12 isolates native visibility/source coupling with
geometric1/exact0/independent0. Its changed ambient/bounce integrand makes any
pass diagnostic-only, never a production workaround. No safety-cap removal,
brightness scaling, new random-stream variants or camera transport.


271 corrected-binding, stream-0 baseline: valid 200 s, no faults/dumps/build
changes; static max shift 0.08 px, finite/pose/alpha pass. All six off frames
match original PRE RGB bit-for-bit. Off/on +1.1048% has a broad paired interval
including zero, but pooled POST/PRE +0.6630% FAILS the stricter target (95% CI
[+0.000035013,+0.000193181] Y); all on captures are +1.03..1.77% versus PRE.
This is VALID evidence after the binding correction, unlike 265/266.
272 independent stream3 on the SAME frozen 7433185 build: valid190s,
no faults/dumps/build changes, static0.10px and all saved alpha bits exact.
Gate on/off +1.4178% FAILS: paired95% CI [+0.000009341,+0.000483439] Y.
Original POST/PRE +0.8620% also FAILS, CI [+0.000037327,+0.000261842].
Paired intervals have half widths about1.2/1.4% off Y for271/272: no equivalence
or variance claim. Independence alone is rejected as a sufficient fix.
Both evidence files are committed on P1 a999f5d and pushed (transient GitHub500 resolved). No candidate promotion or brightness/tolerance adjustment.
273 GI-off/native-AO control INVALID positive-signal gate,108s/48s session,
no faults/dumps/build changes. Counts remain0, so GI folding is not required.
Source audit: debug4 prep itself forces AO0; native m0.x/y=1 then suppresses
both direct terms. The old claim native AO untouched was wrong for this mode.
P3 e9a73d5 records negative evidence and prepares existing debug6/AO1 control;
it includes native ambient, so positive signals cannot prove total parity.
274 native previous-camera/MV probe valid117s, no faults/dumps/build changes,
all15 cases/validity and PRE=POST pass; zero full layout/push/write categories.
Ground offsets agree with canonical jitter to~0.002px; animated tent point limits
joint inference (2/3 match). No camera/jitter correction justified. Other VVL
categories remain unresolved. P1 bd6f6d2 preserves results and PAUSED.md.
276 ONE corrected-interface moving AB/BA: valid109s/51s session, no faults,
dumps/build changes. Residual0.133864671/0.161903599 (+20.95%), RGB+20.78%,
cohortY+44.51%, motion29.58/52.66px (+78.03%). Grid coverage3.856/2.646%,
2.317/1.572M samples. Unequal motion/cohorts prevent causal regression claims;
there is no demonstrated gain, and static energy failures271/272 remain decisive.
Park this candidate temporarily per goal, not the overall goal. Next P1 inspect
native direct-AO/capped give-back at visibility0 before any correction; use
existing data/keys and keep changed-integrand diagnostics clearly separate.
275 P3 AO1/debug6 control PASS: valid110s/48s session;10 complete windows,
3.236B samples, native positive100%, recompute positive3.165B, invalid0,
no faults/dumps/build changes. Last nativeY0.0385764/recompute0.0295358,
boxY0.0186991, overshoot1.248e-6. Native ambient remains: no total-parity or
shadow/quality pass.
277 ONE short AO1 box0/1 control valid150s/88s session, zero faults/dumps/build
changes. After excluding configuration-straddling/mixed windows,7 pure off/9 on
cover2.281/2.932B pixels, invalid0. Absolute difference0.027741794/0.009043992
(-67.3994%), nativeY0.038576762/0.038576244, boxY0.018699574 on.
Overshoot1.33e-7/1.258e-6 remains nonzero and is NOT waived. Ambient/in-scatter
prevents total direct-light parity; no shadow rays were traced in debug6.
P3 8973a34 evidence pushed. Next short debug0 box-on test must exercise actual
shadow rays and inspect finite output/unknown fallback, then S7/S5 cookie cases;
FULL compatibility and native-accuracy gates remain open. Shared queue is idle.

269 corrected-binding validation passes its full layout/push/write audit: valid
112 s, zero driver faults/dumps/build changes, all 15 cases and current projection
probes valid PRE/POST. Other validation categories remain unresolved; this is not
a clean VVL certificate. Saved alpha is exact; on versus PRE is still positive
in two samples, which cannot establish energy parity. Never reuse invalid 265/266.
P1 7433185 adds default-off gi_history_fresh_independent: POST uses stream 3,
PRE lighting/GI stream 0. Reusing the same stochastic transport for nested fresh
lighting can correlate self-hits/short cycles; independence is a hypothesis to
test, not a proven cause. No extra rays, images, weights, camera payload or AO
change. Build 12 CTests/24 ABI checks pass; seven ordinary shaders are identical.
271 LIVE on the frozen build with independent=0, corrected origin binding and
12 static gate pairs. Next use independent=1 on the SAME build before any pan.

270 P3 precise repeat INVALID: 52 windows, 16.919 billion projector samples,
zero reported native/recomputed positive samples and invalid=0. No nonzero-signal
accuracy evidence. New precision is not a reason to accept a zero comparison.
A box-on/debug-4 GI-off probe is prepared to isolate native AO; no production
quality workaround. Shadow rays and FULL-variant exactness remain open.

Full validation audit invalidates geometry runs 265 and 266: base surface passes
lacked declared/bound origin image 40 (VUID 07988). Their energy/rejection numbers
are retained as INVALID observations, not algorithm evidence. The separate 15-case
math kernel and four-binding projection kernel do not use image 40. P1 7e91e6d
shares the base layout/write contract (20/40/48), with a compiled pointer/type test
and reflection derived from that header. All 12 CTests and 24 ABI checks pass.
269 is the frozen unlimited-VVL repeat; inspect ALL layout/binding/pipeline errors.
268 isolated probe: all three captures have zero B1/B2 offset within arithmetic
error, PRE equals POST despite nonzero jitter. No position correction is justified.
P3 8cb61f0 adds bounded exact uint64 projector sums with 24 fractional bits and
nonzero/invalid counts; default/check shaders stay byte-identical. Runtime pending.

266 geometric static12 FAILS off/on noise parity: +0.46644%, CI
[+0.000041845,+0.000119520]Y. Pooled PRE target includes0 but cannot override
that failure. Valid204s, maxshift0.08px, finite/pose/alpha pass, no faults/dumps/
build changes. No reconstruction or threshold change is justified yet.
P1 bc71cb3 adds diagnostic self-test mode2: reserved pose row12 measures frozen
B1-ray versus native B2 projection at two render-relative pixels and classifies
agreement with known current/previous jitter. PRE AND POST checkpoints compare
the same frame; packed24-bit integers survive float exports exactly. Existing
bindings5/48/50/51, Push256, actual-submit pins, no new camera payload or image
change. Build11 CTests, shader/24 ABI checks pass.268 LIVE three static captures,
gate0; do not infer which matrix is jittered or MV sign from an offset alone.

267 first P3 activation valid291s/219s session, zero driver faults/dumps/build
changes; native set3 binds with2 dynamic offsets, projector evaluations nonzero
(last1.319/px). Accuracy INCONCLUSIVE: reported game/recompute means0, while fx
quantizes raw radiance at1/256 and native scale0.00136865. Zero counters do not
prove black rendering or agreement. VVL full log retained; contains unresolved
format/event/layout categories, no set3-naming message; no clean certificate.
Debug4 did not exercise shadow rays. P3 f63da03 pushed/default off; next recover
sufficient projector-stat precision or raw signal before assessing accuracy.

265 geometric-normal smoke valid111s, no driver faults/dumps/build changes.
BOTH GPU self-test rows[32767,32767,15,1]:15/15 pass. Static shift0.06px,
finite GI, PRE alpha exact. Off Y0.017494613 vsPRE0.017495760;45/2,731,520
RGB pixels differ in this diagnostic run (do not call control bit-exact).
On Y0.017680915 vsPRE0.017518661 (+0.926%); two captures are not a noise pass.
One on source window:39,157,093 queries,25,108,223 history,14,040,564 fresh,
8,306 original-unavailable. VVL2667 existing-category sync messages; zero
compute-dispatch/private-source/new-metadata messages, scoped only.49/49 shader
files compile/validate after sharing the origin declaration. P1 head82b8528
pushed.266 LIVE frozen twelve captures without self-test or VVL overhead.
If depth rejection/energy bias persists, compare the predicate's plain ray_at
position with the pan metric's existing native-camera round-trip correction;
reuse existing matrices, no new camera payload/empirical threshold/brightness fix.

264 P3 DEFAULT-path baseline PASS: valid165s/104s session,24 complete windows,
7,844,924,963 reference pixels, ZERO any-bit/relative/one-writer/join differences,
no faults/dumps/build changes. c039f87 evidence pushed; FULL-variant250 still
open, no box-on run or merge. Queue was used while preparing the next GI fix.

262 exact-source static: valid203s, maxshift0.0601px, finite/pose/AO pass. ALL
six off frames have zero PRE/POST RGB differences (2,731,520px each). Off/on
mean-0.20675% is within paired noise, but original-PRE reference FAILS pooled
+0.015598%, CI[+0.0000009073,+0.0000044876]Y; on consistently+0.0311..0.0314%.
No brightness correction or gate relaxation. Exploratory263 source-toggle pan
valid112s/52s, no faults: Y residual+2.596%, RGB+2.539%, sampleY+3.942%, motion
76.57/79.55px, coverage5.033%/6.675%; no gain or exact-source promotion.

P1 caae9b0 fixes the original availability cohort in BOTH modes: fresh fallback
replaces existing colour rejected by surface checks, without adding edge samples
where the original had no finite colour. b6a1c84 adds default-off geometric
normal identity using the existing owned rtOrigin image40, only matched TLAS
hits. Shading detail can vary on static geometry below a point footprint; depth
AND geometric normal still must match, with propagated fp16/oct16/fp32 bounds,
no angular fudge/neighbour rescue/new camera data. Metadata certifies normal
mode on the GPU. Full build/11 CTests, nine shaders/24 ABI checks, ordinary
seven shaders identical;15 GPU cases await runtime proof. 265 LIVE unlimited
validation smoke with self-test and static gate off/on; all other knobs isolated.
Specs/evidence on P1, keys0, no main rendering merge. Next runnable GI test keeps
queue priority; P3 box-on validation is ready after its passing baseline.

261 exact-diffuse smoke valid117s: zero driver faults/dumps/build changes,
finite/GPU-pose/PRE-alpha pass, static shift0.12px. Off/on Y0.017292882/
0.017589541, PRE0.017282156/0.017571717; two samples are not an energy pass.
Unlimited sync validation reports2647 existing renderpass/NGX-category hazards,
zero dispatch hazards or messages naming21 private source handles/new metadata.
Scoped evidence, not a clean VVL certificate; raw log retained on P1.
066e59b fixes a control confound: gate0 no longer substitutes fresh lighting
when original history is unavailable; gate1 still tries fresh on every rejection.
Exact-source key can now be isolated without adding edge samples. Static exports
also report PRE/POST RGB bit differences. 262 is the frozen twelve-capture
exact-source static run, surface gate0, original-PRE target retained.
P3 ec68966 (offline): reference fixture now explicitly selects default path
(ls_stats=0,ls_samples=0,ls_debug=0), matching accepted P2 controls. FULL-variant
bit differences from250 remain open; no threshold relaxed/runtime pass claimed.

260 four-ray diagnostic: valid118s/52s session, zero faults/dumps/build changes.
Within-run off/on residual0.281396/0.260002 (-7.60%), but cohort GI Y0.155257/
0.015238 (-90.19%), motion74.61/80.65px; off has strong radiance spikes. No
quality win. Both-zero normalized residual is not a monotone variance estimate
as ray count changes; no cross-run image or noise-reduction inference.

P1 bd4215c adds an INDEPENDENT default-off gi_history_diffuse_exact hypothesis:
legacy RGB*(diffuse_Y/total_Y) preserves source Y but mixes view-dependent
reflection colour into GI. Exact complete diffuse RGB now has its own metadata
record, retaining complete radiance for RF/control and fresh same-source fallback.
Requires gi_history_fresh=1; surface gate0 isolates it. gi_pan_metric=2 buckets
this source key; same-cohort RGB residual complements Y. No extra ray/descriptor,
weight/count change or camera transport. Metadata stride3 stays fixed across
source toggles; range guard remains. Full build/11 CTests, eight affected shader
variants, 24 ABI checks, five energy regressions and seven byte-identical ordinary
shaders pass. 261 is the frozen two-capture unlimited-sync-validation smoke;
then original-PRE/static-energy and ONE moving source-key test if valid. All keys0.
Spec/evidence on P1: docs/GI_DIFFUSE_EXACT.md; no main rendering merge.

259 ONE-run balanced moving test: valid112s/52s session, zero faults/dumps/
build changes. Off/on residual0.134896214/0.144665732 (+7.242%); mean GI
Y0.015967731/0.016094788 (+0.796%); motion68.822/71.812 render px. Cohort
3,532,908/4,600,302 samples, grid coverage4.869%/6.822%; compatible-moving
coverage7.461%/8.514%. The fresh complete source avoids massive darkening,
but increases raw residual: NO pan-quality win, main merge or default change.
Unequal cohorts/motion and fresh sample noise limit interpretation; raw GI
is not resolved RR output. The small static conditional bias remains open;
run258's wide CI does not erase run256 or its other +0.50..2.06% samples.
All evidence committed/pushed on P1; queue terminal. Next separate fresh noise
from resolved-image instability using same-run evidence, no new transport or
cross-run picture comparison. One authorized usage reset remains unused.

258 / af093ae completed valid149s: all twelve exports, stationary max0.18px,
finite/pose/AO pass and zero faults/dumps/build changes. Both energy CIs include
zero: POST/PRE -0.0292%, CI [-0.000282538,+0.000272094]Y; off/on +6.975%, CI
[-0.001305235,+0.003713217]Y. POWER IS LOW: one true light pulse broadens off/on
relative CI to roughly [-7.56%,+21.51%]. Other on captures remain +0.50..2.06%
versus PRE; do not claim the smaller bias is solved. 257 was incomplete (five
late POST exports exceeded 2048MiB), not an energy pass; fixture now4096MiB.
Job259 results are above: balanced AB/BA toggles/directions every4s, private/AO
diagnostics off; no defaults/merge approval. Full evidence remains on the P1 branch.

Complete-source run256 execution valid149s, stationary max0.06 render px,
finite/pose/alpha pass, zero driver faults/dumps/build changes. Energy checks
FAIL: on/off +1.0583%, paired CI [0.000003457,+0.000362817]Y; POST versus
original PRE +0.5418%, CI [0.000011949,+0.000175550]Y. The large darkening is
gone, but this is not noise parity or a P1 quality pass.
P1 ccb1f65 additionally mirrors original composite spectral diffuse-share
reconstruction and fp16 storage; fresh replay now follows RF so its specular
source is complete. No empirical energy multiplier. Full build/11 CTests and
two changed trace shaders pass. Job257/258 results are above. Balanced moving fixture is ready; keep all keys off.

Run255 completed valid in 200 s: all twelve captures stationary (maximum shift
0.16 render px), finite GI, valid GPU pose, unchanged PRE alpha and zero driver
faults/dumps/build changes. Direct-only off/on parity was -0.807%, paired 95%
CI [-0.000601951, +0.000329959] Y. This is PARTIAL evidence: both controls used
a different integrand than the original complete composite source.

P1 d53f3e1 restores original complete history in the control/accepted branch;
rejection now combines fresh native diffuse with the immutable PRE GI at the
source, matching material/strength/skin conventions. No dropped weight/count.
The energy gate additionally checks same-frame POST versus original PRE; equal
loss in both modes cannot pass. Five regressions, 11 CTests, four changed shaders
and 24 ABI checks pass. Metadata shrinks to surface/optional metric records.
Job256 results are above. Only a pass of both energy checks permits the next
ONE-run moving toggle measurement.
All research keys stay off; no P1 main merge or pan-quality claim.

Static fixture254 / 8ec52c4 now proves stationary capture: albedo shifts only
-0.04 / 0.0200 render px, GI finite, pose valid, PRE/POST alpha identical;
99 s, zero driver events/dumps/build changes. The fix initializes the engine's
event-owned cursor with a centred SDL event per present, forcing an empty poll
after it so event batches terminate. 253 still moved despite polling/input
neutralization; keep it INVALID. Only SDL_GetGlobalMouseState is statically
imported by the game; event/poll consistency is necessary, not just coordinates.
254 off/on Y 0.017001871 / 0.016828934; two captures do not pass the noise gate.
Historical fixture proof at 7b6e82a is preserved; job255 results are above.

Static energy run252 execution-valid189s with zero driver events/dumps/build
changes, but the quality check correctly rejects it: camera moves from camp
to woods, about -318 render pixels x / -42 y between the first two captures.
No noise/energy pass is claimed. The initial fixture neutralized global mouse
state but left local/relative mouse and keyboard paths live. P1 e33f356 now
uses SDL's own window coordinate space consistently, blocks physical keyboard,
buttons/wheel and relative motion only in opt-in StaticCamera, and logs hooked
APIs. Job253 is running to prove stationary captures before another full run.
The frozen normal-error bound also now uses the same frozen basis as its normals.

User resumed and made P1 energy preservation first priority. The rejection
branch drops L*w then divides by the unchanged ray count: lost bounce, not a
quality improvement. New default-off gi_history_fresh (P1 d2173c7, current
evidence head bc8cea0) replays RGB after native lighting with existing frozen
PRE pose. Both gate modes use paired direct native source, with fresh current
source on rejection. It preserves PRE visibility and fixed ray weights/counts;
it does not compare recursive composite radiance to a different fresh integrand.
Legacy PRE energy is reported separately. Initial scope RR full-res/diffuse-only.

Offline: 11 CTests, 49 shaders, 24 ABI checks, 4 energy-check regressions and
36 script checks pass; seven ordinary GI/RF binaries remain byte-identical.
Smoke251 valid113s, no driver events/dumps/build changes. Off/on raw Y
0.016625201 / 0.017001902, PRE legacy 0.017214335 / 0.017579911; finite, GPU
pose valid, identical PRE/POST alpha, static albedo within raster jitter.
One source window proves 27,078,193 cached and 12,110,691 fresh reads, only 3
unavailable. Two captures are not enough for the measured-noise energy gate.

Unlimited VVL reports 2,723 sync hazards in existing renderpass/NGX categories;
no dispatch sync hazard or message naming 21 private exported handles/new
metadata. Earlier gate-only smoke has the same three categories. This is
scoped evidence, not a clean validation certificate. Raw VVL log and evidence
JSON retained on P1; docs/GI_FRESH_FALLBACK.md explains bounds and limitations.

Job252 running: twelve static captures in ONE run, six balanced AB/BA pairs,
owned centred pointer, raw checkpoints for actual gate mode and unchanged AO.
The check compares mean GI with its paired Student-t 95% noise interval and
rejects black output, invalid pose, camera motion or missing pairs. Report CI
width/power; only after this passes repeat the production moving pan. The
residual cohort already widened to 13.4%/7.15% in run248; no cross-run images.

P3 baseline250 completed INVALID: 21 windows, 6,861,577,296 reference pixels,
12,304 any-bit differences (41–1533/window), zero >1e-5-relative or one-writer
differences; zero driver events/dumps. ls_stats=1 chooses the full variant,
unlike the old golden default control. No new box-light activation or merge.
P4 8d902d5 adds the conservative interval classifier and corrected alpha-domain
contract; full build/4 CTests, 197,621 independent-oracle checks pass. GPU bake,
domain provider and hardware attachment remain unimplemented; no main merge.
P6 minimum water design remains in main. One usage reset is authorized if we
reach the limit; that authorization has not been consumed.

## Evening summary — 2026-10-28, 22:20 Europe/Rome

1. Main 6dba0a8 adds the corrected local-light reference and complete merge checks; P1 is not merged.
2. P1 remains open; history rejection is parked on its branch, default off, after decisive same-frame energy loss.
3. P1 uses one moving run with live toggles and per-run reprojection; independent trajectories are not image comparisons.
4. The point-footprint correction removes unused-neighbour rejection; pan coverage is 13.4% off / 7.15% on.
5. Paired point-gate raw bounce loses 70.7% / 88.0% energy, while captured alpha matches exactly; no quality win.
6. Latest checks: 36 helper checks, 6 surface shaders, 20 ABI checks; 7 ordinary shaders remain byte-identical.
7. P3 box-light reference job 250 is running, hold90; default-off integration passes offline build/3 CTests/34 shaders.
8. P4 OMM and P5 SHaRC/material consumers remain unapproved; EN2 and emissive failures are documented.
9. P2 reference merged after two fresh hold-90 passes; EN2 and one D-EM family still fail and remain excluded.
10. P6 water minimum design is merged at 441904a; forward-water rendering and coverage gap 1 remain open.

## Operating plan

Work until Saturday, 2026-10-31 evening. Use 20:00 Europe/Rome as the stop target;
finish with a summary here, push all resulting branches and main, then stop.
No user decision is currently needed. Never modify saves, rewrite history,
force-push, or move/delete any snapshot-20261027/* tag.

Before starting a runner or game, check both the USER_PLAYING marker and live
BG3 processes. An unowned game or the marker means offline work only. In-game
tests use the serial hidden queue and explicit expect patterns. Feature merges
use merge-green only after manually checking the deciding log numbers, followed
by pushes of main and the branch. A phase branch starts from current main and
imports useful old work selectively. New features remain off by default until
their quality gates pass.

## P1 — Pan instability

P1 temporarily parked, not completed: phase head 1a57286 has PAUSED.md with
evidence and next intervention; all research keys remain off, no main merge.
Run 249 is valid, 217 s, zero driver events/dumps/build changes. In the two
same-frame ordinary/point-gated raw pairs, GI Y falls 70.66% / 87.96%:
0.013646539 -> 0.004004193 and 0.016753490 -> 0.002016792. Alpha bits match
over 2,731,520 pixels per pair, all finite, pose valid and eleven GPU cases pass.
Normal rejects 52.35% and depth 22.46% of reported queries (startup included);
these counts are not an energy attribution. The low pan residual is not a
quality result. Next return: a physically valid fresh-source fallback on
rejection, plus spatial normal-footprint handling; no tuned match threshold.
P3 now receives its first runtime checks while the overall goal remains active.

Point-footprint run 248 / 409ffe1 is valid: 403 s total (344 s loading), 57 s
single moving session, zero driver events/dumps/build changes. Off/on residual
0.151383524 / 0.064525116 (-57.38%); sample GI Y 0.321177270 / 0.008689996
(-97.29%). Grid coverage 13.3877% / 7.1512%, or 14.5655% / 10.1579% of the
static-compatible moving samples. Motion 98.73 / 51.33 render px. Broader
coverage makes the measurement more useful; unequal cohorts and lower sample
energy still prevent a visual-quality claim. No cross-run image comparison.

Offline paired exports from jobs 242/243 answer the earlier energy concern:
within each individual frame, raw legacy/gated bounce Y falls 71.81%, 99.83%,
99.88%, 99.86% over the four captures. Alpha bits match exactly over 2,731,520
pixels each, both outputs finite, no brighter gated pixel. This is causal
same-frame evidence against the OLD five-texel candidate, not final-image
brightness or a comparison between runs. Phase evidence JSONs preserve inputs.

P1 head d8b204f is pushed. Job 249 is running: repeat the existing paired
private capture on the corrected point-footprint code, headless diagnostic,
two same-frame legacy/gated pairs. AO counter zero is not required. This test
isolates energy loss; it cannot close the moving-camera quality requirement.

Production repeat 247 (b5e0e93, diagnostic AO comparison off) is queue-valid:
130 s total, 56 s one moving session, zero driver events/dumps/build changes.
Residual off/on 0.127411757 / 0.046098715 (-63.82%); sample GI Y 0.019353448 /
0.005471711 (-71.73%). Coverage 0.11889% / 0.02111%, motion 88.65 / 41.56
render px. These unequal cohorts and lower energy do not establish quality.
Evidence: P1 branch docs/evidence/p1-surface-pan-production-247.json.

Job 246 is INVALID: no pan activation in the lighter loaded scene (876–1413
draws). The helper incorrectly required >2000 draws even after authoritative
post-load stability. b5e0e93 uses the layer's scene-ready or automatic-capture
signal, requires the saved-load transition and completed baseline, and retains
the no-Save fallback. All 36 offline helper checks pass; 247 proves actual pan
activation. No desktop-resolution or scene draw-count threshold for saved runs.

P1 409ffe1 corrects another concrete overrejection: all colour/history reads are
integer point loads, but the candidate validated four additional unused texels.
It now requires depth AND normal on the actual fetched texel, with no neighbour
rescue and unchanged tolerances. Six surface shaders and all 20 ABI/SPIR-V checks
pass; seven ordinary GI/RF binaries remain byte-identical to ac77150.
Job 248 finished with the one-session production toggle protocol above. No new
camera transport, AO diagnostics or cross-run image comparison in that pan.

Latest user steering: stop cross-run image comparisons. Use per-run GI
reprojection residual or live off/on alternation within one moving run. The AO
counter discrepancy is noted and compared with the same-build key-off control;
it does not block the first pan measurement.

First pan number: job 245 on bc15dd3 DLLs, valid, 149 s including startup / 65 s
session, key alternated every four seconds in ONE hidden moving game; zero
driver events, dumps or build changes. Sample-weighted normalized GI luminance
reprojection residual off/on is 0.123120933 / 0.044079878 (-64.20%); RMS is
0.347363809 / 0.209209448. Sample mean GI Y is 0.019496607 / 0.008939488 (-54.15%).
Only 29,136 / 59,473 samples pass the static-compatible/moving-surface filters,
0.07653% / 0.07900% of tested grid samples. Mean displacement differs too,
119.41 / 260.65 render pixels. This is a limited cohort and lower GI energy,
not an established visual improvement: keep the candidate off, do not merge.
AO counter differences occur in BOTH modes, 11.14% off / 9.37% on. The next
step is a production-metric repeat without the diagnostic, then investigate
normal-footprint overrejection and energy loss. No additional camera transport.
Branch codex/p1-pan e8ff955 is pushed. The metric uses native B2/B5 projection,
verified camera round-trip, paired previous GI, GPU mode buckets and a rotating
4x4 diagnostic grid; GI rendering stays full-resolution. Evidence and the
repeatable phase plan are committed under phase docs/evidence/ and tests/fixtures/.

Moving A/B now captures actual motion in both configurations. Jobs 240/241 use
identical d431702 host/core DLLs, gi_current_radiance off/on, headless=0: valid
85/74 s, zero driver events, dumps or build changes. In the last interval albedo
changes 83.38% / 79.68%; both cameras move. Their final views differ by 80.54%,
so this is not a matched-surface quality comparison: no GI improvement is
established and P1 remains open. P1 build/moving-current-ab-r11.json keeps numbers.
The -Move fixture isolates physical desktop input with a virtual pointer and
consistent SDL motion events, using the actual game client dimensions. This
addresses desktop-resolution dependence; normal play is unchanged. Moving QA
defaults to real presentation while remaining hidden by the serial queue.
All 10 CTests, six capture/resolution tests and 32 log-policy checks pass.
Earlier focus experiments had no effect and were reverted. Real presentation
alone was insufficient (237 moved off, 238 stayed static); do not blame headless
mode from those confounded controls. Branch codex/p1-pan ac77150 is pushed.
Implemented gi_history_surface_gate=0 on P1, independently of current-radiance
replay: accept bounce history only when previous depth AND world normal match.
Paired GPU metadata, conservative footprint intersection and derived precision
bounds apply to bounce RGB and half-res RGB accumulation; visibility keeps its
legacy predicate. 11 CTests, 45 shader files and 20 ABI checks pass; seven ordinary
GI/RF shader binaries are identical with the key off. No new camera transport.
Smoke 242 completed in 210 s without events/dumps, but is INVALID: aggregate AO
counters report 8–10% differences. Both exported same-frame alpha pairs instead
match exactly over 2,731,520 pixels each; both GPU predicate tests pass eleven
cases. Unlimited VVL names no logged private handles or the metadata buffer.
The discrepancy also occurs in repeat 243 with native geometry math (92 s,
zero events/dumps). Job 244 ended before the old 120-frame report window;
its invalid contract also expected a startup marker outside the session slice.
Job 245 uses short windows and the corrected session contract; see numbers above.
Details: phase docs/GI_HISTORY_SURFACE_GATE.md and build/surface-smoke.json.

The last production S3 result predates the temporal sample-coordinate safety
correction. It measured temporal excess areas of GI 118.672, local 55.052,
sun 42.088 and reflections 34.557 per mille, with 23.0% common motion excluded.
Those numbers remain the baseline, not proof about the current candidate.
The fresh phase branch codex/p1-pan at 03679ed excludes unrelated FG work.
Production diagnostics were off for p1-gi-coordinate-candidate-S3: queue-valid,
356 s, no device loss, dumps or build changes. GI excess area is 172.938 per mille,
mean temporal excess 0.001258 PQ against 0.000978. All-RT excess is 219.062 per
mille, mean 0.002334 PQ; 24.0% common scene motion is excluded. This fails and
does not establish a reduction. The older baseline is not a controlled same-build
A/B, so these numbers do not prove a regression either. No merge was made.

Coverage gap confirmed from scripts/qa/capture.py: --temporal-gi does not request
-Move, and the same_view gate rejects moved views. These S3 numbers measure
fixed-camera temporal instability, not a direct pan comparison. Keep this as an
anti-regression gate, then require separate moving-camera off/on captures with
actual camera-motion evidence before claiming a pan improvement or closing P1.

Owned velocity joined 6,485 copies, with 5,066 late resolutions and no recorded
late-path rejection reasons. The last GI window reserved 300/300 descriptors,
with zero unreserved frames. Correct transport alone did not resolve the failure.
Next hypothesis: previous-frame bounce radiance still carries previous visibility
and feedback. Assess a default-off current-frame diffuse source after native
lighting, preserving pre-lighting visibility and freezing its camera/settings.
The candidate is implemented at 3c7b411 with paired host/core ABI 14 and default
gi_current_radiance=0. Full build, 10 CTests, 39 shader files and 14 ABI/SPIR-V
checks pass. A follow-up at 9c68f9e keeps seven ordinary GI/RT shader binaries
byte-identical to bf170f8; P3's default/check local-light shaders are also
byte-identical to main with boxes off. These are compatibility checks, not proof
of visual improvement.

The first current-radiance smoke and same-build off control both stalled before
gameplay (82 s each, zero device-loss events/dumps, no build changes). No GPU
candidate evidence was produced. A concrete porting error held tracker.mu across
a helper that reacquired it: c428cde releases the lock before the metadata bind
observation, as the tested research branch did. All 10 CTests pass after the fix.
The direct-helper unit test missed this native callsite. The fresh off control
now completes validly in 85 s, with zero device-loss events/dumps/build changes.

Current-radiance smoke-r2 completes two captures in 89 s. Both export all nine
private writers and a GPU-valid pose header. Actual RGB changes on 323,905 /
326,814 pixels, with mean absolute differences 0.0079245 / 0.0078829; raw and
temporal alpha match PRE exactly and all outputs are finite. No VVL messages
name the 57 exported private handles, but the validation duplicate limit was
the default 3: suppressed IDs can hide later messages, so this is not a full
hazard clearance. Evidence: the P1 branch build/current-smoke-r2.json and saved
validation log, captures 20261028-191549 / 191600.

That smoke has one nvlddmkm event 153 at 19:16:02.671, before decode finishes;
rendering continues afterwards, with no VK_ERROR/device-fault log or dump. Its
cause is unresolved; do not attribute it to teardown or declare stability.
A production repeat without validation/private readback completes validly in
79 s with zero events/dumps/build changes, 1,200 accepted captures and no reject.
This proves execution, not visual improvement. No P1 merge has been made.

Same-build static A/B started at c428cde with diagnostics off. The first off
measurement is GI 144.405 per mille / mean 0.001571 PQ, still outside the gate;
all RT 210.329 / 0.002801 PQ. Its queue status is INVALID because the expectation
incorrectly searched for QA stdout in the kept game log, although all 28 traces
and result.json completed. Do not use it as the deciding queue pass. The repeat
checks the actual game-log completion marker and is queued/running as
p1-current-radiance-ab-off-S3-r2. It completes validly in 338 s, with GI excess
150.059 per mille / mean 0.000988 PQ (limit 0.000978), all RT 226.224 / 0.002580.
It also has one NV153 while the candidate is off; that still does not identify
the event's cause. No further fixed-camera on run is scheduled ahead of motion.

User steering: moving-camera off/on on the SAME build takes priority, and static
scores cannot close P1. QA --move is implemented at 3ff190f: it forwards -Move,
keeps each trajectory's settings constant, retains frame provenance/dimensions,
requires --capture-only and rejects static analysis. Six resolution/capture and
five temporal-plan tests pass. Moving off job 227 is running before the matching
on run. The next GI hypothesis is a default-off depth AND normal history
rejection at the reprojected previous pixel, before any more transport work.

The first moving pair is queue-valid on the same 3ff190f build (142 / 80 s),
three traces each, zero NV events/dumps/build changes. Actual albedo movement
is observed, but the trajectories do not match: off shifts about (-40,49) then
(-2,81) preview pixels; on shifts (-2.87,-2.20) then nearly zero near the scene
boundary. No numerical quality improvement is claimed from this pair.
a8c5db5 starts the pan only after a saved-view baseline trace, retaining the
gameplay-ready guard. All 32 log-policy and six capture/resolution checks pass.
Renderer DLLs are unchanged; their hashes are saved before the synchronized
repeat. Job 229 p1-moving-current-off-S3-r2 is running, followed by its on pair.

Next implementation proposal is gi_history_surface_gate=0: previous depth and
world normal metadata paired with the radiance writer, using existing B2/B5
camera data, with conservative precision/footprint checks. It must gate the GI
bounce source as well as temporal accumulation, since RR uses history weight 1.
It is not implemented yet. Missing/invalid writer data must reject, and any
dimming or excessive rejection must be measured rather than called a pan fix.

Owned-motion tests reject offscreen and nonfinite coordinates on the GPU.
In four NaN/Inf captures, temporal output equals the fresh sample exactly and
all ordinary outputs remain finite. This establishes numeric fallback safety;
it does not establish a visible pan improvement or correct previous visibility.

Research camera runs on 2026-10-28 established binary transport and actual
camera movement. The additional B1/B2/B3/B4 camera reads remain off pending
guarded implementation. A wider validation check found an introduced overlap:
copy/fill of metadata followed by a write to its last 48 bytes. Separate write
ranges fixed it. Static/moving repeats on 929be7a are queue-valid (116 / 126 s),
with no device-loss events, dumps or build changes, and no validation messages
naming the 3,255 / 3,345 logged private handles. Raw camera transport, checkpoint
completeness and paired depth checks still pass. The complete camera verdict
remains inconclusive because the additional payload is deliberately absent.

This research is retained, not merged. The phase branch will exclude unrelated
FG/HDR/swapchain work. Source recording identity and actual GPU history epochs
are different; no history acceptance is certified from a present counter.

User check when a P1 candidate is ready: pan across static building and ground
edges in the usual save, comparing the candidate's GI key off/on. Look for light
or shadows sliding away from those fixed edges. Ctrl+Shift+F9 switches all RT
effects off/on as the reference. Do not treat wind-blown grass alone as a failure
or use all-RT-off as a claimed fix. No new user-facing candidate key is ready yet.

## Next phases

- P2: the useful lscheck fix is merged at 6dba0a8 and pushed with codex/p2-pending. Fresh valid S6 hold-90 runs have 25 windows each and zero any-bit, relative or one-writer differences across 8,172,707,324 / 8,172,707,294 pixels; no faults, dumps or build changes. The first refresh pair was invalid because a startup-only expectation was incorrectly applied to session slices; valid repeats supersede it. Full startup independently confirms the real 8.0 MB asset DB. Details: docs/P2_PENDING_MERGES.md.
- EN2 pure S8 windows join 300/300 without waits but still fail blue/metalness and luminance bias. D-EM family 304be1c7 fails AF at ratio 0.361 / 0.362 (band 0.842–1.187); GF passes at 0.999. Both remain excluded.
- P3 offline preparation: codex/p3-box at 907857c is pushed, using the managed p2-pending checkout. The useful shader foundation is ported onto current main; host pipeline selection, native descriptor replay, cleanup, default-off ls_box key and projector validation logging are implemented. The stats allocation now covers added counters 136–150; the inherited 136-counter allocation was insufficient. Full build, 3/3 CTests, default agreement and 34/34 shader files pass. No runtime or merge yet; S6 / S7 / S5 recompute and shadow checks follow P1's runnable candidate. See that branch's docs/P3_BOX_LIGHTS.md.
- P4: implement and validate the conservative OMM foliage design.
- P4 design review found a correctness gap before implementation: the old mip-0/1 hard-cut bake can mark constant alpha 0.75 opaque, while alpha_eval at LOD 2 uses a stochastic threshold and accepts only about 75% of rays. This is bias, not equivalent noise. Known micromap states must respect all supported runtime thresholds/mips or remain unknown; the ignored P3 build/P4-alpha-review.md records the counterexamples and endpoint issue. No OMM source was changed.
- P5: SHaRC v2 after EN2, then the light tree and EN2 consumers.
- P6: at least a water design covering the docks gap.

If P1 stops yielding measurable progress, record state, evidence and the next
idea in PAUSED.md on its branch, move to P2, and return later. Apply the same
rule to later phases. Do not substitute more instrumentation for deciding tests.

## Small fixes and gate gaps

- Resolution checks now use actual source and preview dimensions; changes make comparisons inconclusive and retain captures.
- The window hider checks PID plus process start time and retains a process handle through UI use; 13 policy checks pass on PowerShell 5 and 7. The latest live queue game also verified matching ownership, hidden style and alpha zero.
- Fixed the incomplete merge gate: build.ps1 -Test runs all CTests and rejects an empty suite; merge-green checks the main branch, playing state and merge result, compiles all shader entries through the shared compiler, and aborts its own failed merge. Real disposable Git fixtures pass invalid-ref, build/shader/QA failure, successful merge and already-merged cases. Integration passed 3/3 CTests, 32/32 shader files and QA detector tests. The P1 branch separately passes 8/8 CTests before the in-progress GI changes.

Research evidence remains in the managed pan-motion-vectors worktree and main's
ignored queue logs. Snapshot tags and backups are unchanged.
