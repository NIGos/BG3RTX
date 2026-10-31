# Session handoff - 2026-10-31: material acquisition and bounded reference

Full rendering execution plan M0-M6 remains active; do not claim completion.
User permits live diagnostics now, queue-only with finite scoped receipts and
unowned-game/USER_PLAYING guards. No need for user witnessing or manual approval
after adequate scoped checks. Be pragmatic: unrelated global gates do not veto
a demonstrated local fix. Track what the user has not yet reviewed in PROGRESS.

Main a9ab6ba ABI 21 is merged via merge-green and pushed with codex/omm-bake.
Both DLLs rebuilt; 55/55 CTests, 579/579 runtime shaders and QA passed. Related
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
Sampler clones and execution-time gather replace the unsafe initial snapshots.
Actual S1 trace proves prepass/deferred use different command buffers (866/835
draws,100 static VT). Fix cross-CB association/ownership before any zero-record
trial. Secondary-command no-write proof and Submit2 compute ordering need review.
Shared frame de1ccdd has 4338 CPU checks and validated HLSL; real consumer pending.
ABI 20 R1 failed: deferred recording precedes source preparation by median 4 ms.
ABI 21 eb1ec4d R2 completed 460 s; camera/order now reaches the query, but all
100 static draws fail pipeline-state qualification. dae7d32 fixes depth-clip parser
and logs exact predicates; integrated d8b70e3. Freeze matching ABI 21 binaries.
No material GPU participation yet. Static primary 99.524/99.524/99.525%, FG ratio
2.0, no NVIDIA 153/dumps/build drift. R2 immutable evidence is in root captures.
R3 dae7d32 is TERMINAL: 448 s, parser fix confirmed actual, binding attempts
still decline; no material GPU rows. Immutable root captures/r3 analysis retained.
76ef799/import d8510b1 fixes eligibility-before-key and ordinary frozen samplers.
R4 c82fe8d is TERMINAL (230 s): captured 16, snapshots 12, no GPU association.
Actual 8975/e2ec and 3812/8995 use conditional SSBO 0.13 position pulling; owner fixes
canonical Engine/active-stream keys (5b5c595/import bd6719f), retaining exact GPU
clip/UV proof. No cache-
only rerun: static source draws are not the instanced cached domain.
Terrain UBO usage 16 lacks TRANSFER_SRC; fa3d9f4 shaderreadback built/validated,
actual-submit pinning/stages fixed in 68321d0/import fe9f949, targeted checks pass.
R5 completed 212 s: terrain copied 1, declined 0, pending 0 (25760 bytes).
R6 proved source association (16 copies/mapped) but post counters were disabled.
3e9703d fixes only optional material/reference Push statistics. R7 completed
229 s VALID: 110849-573352 qualified hits/window, 6248598 native opaque matches.
Raw/derived R5-R7 evidence is retained. No native equality/full-coverage claim.
80acccf raises material records to 128 with exact resource reuse; CPU/host pass.
ec1b0c8 consumes real terrain mask/brush/LUT bytes; all painted bytes match
archives, 80 asset tests (79 pass/one skip). Final terrain shading still open.
Next: merge-green, then same-run direct/two-surface coverage/cost test. No game now.
Exact packet capture e3b238a/8a84f11 is opt-in only; R4 raw evidence is retained.
Two-surface reference d74f766, rt_hit_reference=2, default OFF; CPU/shader checks
pass, no GPU/path-quality claim. All consolidated via merge-green and pushed.
Main is ABI 21; personal Hair trial stays ABI 19. Do not rerun unchanged.
89 actual S1 G-buffer VS/FS modules pass the read-only scanner. Non-watched
360 s acquisition draft is under captures/native-hit-material-20261031/draft/.
M2 native S1 reference completed in 53 s: 835 draws, no capture overflow.
Floor_B POSITION matches 48 instances/20256 corners, max residual 3.22e-5.
ScaleZ acts on local X; observed Flip does not reflect POSITION. Target residual
2.10e-5 is inside the derived 2.20e-4 precision envelope. Old decorative-anchor
tests were not a transform oracle. Room closure and UV/material parity stay open.
Next: nearby qualified floor support and the actual runtime material consumer.
Nearby ground is now 736 triangles/1191 vertices, preserving native material IDs:
three Floor_B pieces plus matched Floor_A, 8/16 -> 13/16 low-vertex intersections.
Three gaps and room wall/roof/opening closure remain explicit. 58 asset tests pass,
one existing skip. Material64 partition feedback now shares the Hair88 correction
(55 fresh CPU checks); unchanged16/24 policy and explicit budgets are preserved.
The open bay is now exported: 27 native-qualified components/24506 triangles,
824 frozen terrain triangles, nine authored components separately UNKNOWN.
All 16 low-vertex lines intersect real geometry, not an opacity/contact certificate.
Persistent terrain now matches all 13512 active triangle IDs and 738 holes; local
289 triangles replace frozen capture geometry. Shared CPU/HLSL terrain arithmetic
has 1366 CPU checks, but real mask/brush packets remain missing. Seven authored
cell lights exported with raw parameters; live activation/photometry UNKNOWN.
74 asset checks: 73 pass, one skip. SE absence fix 59a4e56/2a708a3 has 16 fresh
checks; Lua install unchanged. Actual exports contain 124/89 lights; native/DB
CSV photometry adapter task active. No invented roof or material defaults.

Queue child ownership/late-game guards, ordinal cross-PowerShell hashing and
module-independent SHA256 are fixed and tested through actual simulated runner.
M2 actual mesh/construction/material exports and shared CPU/HLSL BSDF remain
bounded offline increments, not room closure/world runtime/PT qualification.
No Ponytail. Never touch saves/backups/tags, force-push, clean junction trees or
hold results.md open. Read DISK_HYGIENE.md before any cleanup; owner holds remain.
Only merge-green integrates main; the prior tracked-doc blocker is resolved.

## Current checkout and confirmed UI correction

17fb500/7b9c1e4 fixed recycled descriptor-set maps; new successful allocations
clear old binding/dynamic-count tracking. User confirms blue blocks/flicker no
longer trigger and initial movement/face stability improved. No shader/ABI/GPU
work change. Evidence: docs/evidence/ui-descriptor-trial-20261030.json and
docs/UI_CONTAMINATION_OFFLINE.md. Other shadow problems remain separate.
Never change source/bin/settings/HEAD of an active user or queue candidate.

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
The original capture lacked conditional alpha/depth recipes and treated these
cards as opaque. Selected native Hair/Scalp coverage now exists behind the keys
above; do not redo that work. Always-discard filtering alone was insufficient.
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
Pending attribution: automated same-pose Sun/local/box and distant-caster checks
where the existing evidence does not identify the responsible contribution.

Sun already has radius 0.00459 rad half-angle (~0.526 deg diameter), uniform disc
sample per pixel, RR reconstruction, plus native-mask fallback in untrusted zones.
Penumbra ~9.18 mm/metre of caster-receiver separation: near hair can be subpixel.
Do not enlarge radius to conceal solid hair/cascade cards.
Solar audit docs/SUN_PENUMBRA_REVIEW.md: units/disc/seeds correct, but off-screen
guard retains native-dark sun edges. Six dialogue windows keep 94.6-98.1% of
native-dark/RT-lit disagreements; fully replace 0.3-1.3%, with RR active. Establish
actual caster-volume coverage before lifting trust; no radius/bias/blur change.

## OMM: wired, OFF and not game-qualified

Current main includes GPU bake/EXT backend/BLAS and lifetime wiring under ABI 19.
SDK 1.10 is already at external/omm-sdk-1.10.0; no download is needed. CPU domain
history remains in docs/P4_OMM.md and p4-omm-domain evidence. Native accepts
alpha=cut while SDK fixed cut uses >; conservative equality must stay UNKNOWN.
1532 equality/neighbor and 25740 mixed-texture checks had no contradictions;
these are CPU support, not continuous GPU/filter/VT proof or game quality.
Reuse needs exact coverage/material/UV/descriptor/content/residency/sampler and
actual submitted-work completion. Handles or frame age alone are insufficient.
Useful known coverage, same-ray/energy comparison and runtime cost remain open.
The installed Hair trial keeps OMM OFF. Native inventory/source recipes are in
the native-coverage branch/docs; selected runtime Hair/Scalp support is now main.

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
