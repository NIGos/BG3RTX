# Session handoff - 2026-10-30: UI fix confirmed; hair/cascade coverage open

Isolated `codex/native-coverage` increment: common/native_scalp_coverage.h and its
test implement six native scalp pass mappings, bounded draw-state capture and
scalar coverage/dither. No production caller/key/shader/ABI/runtime change.
178,522 standalone checks pass. Exact sampling/raster-domain capture and Hair
strand/view/depth attributes remain; see TRACK_D_RT native scalp section.

Read AGENTS.md, top PROGRESS_CODEX.md and latest user steering in this conversation.
No Ponytail. The user explicitly permits modest multi-agent OFFLINE work; two
agents were used. Live testing is coordinated here with the user, not autonomous
launches while the user plays. Never touch saves, D:/BG3_Tools backups, snapshot
tags, force-push or clean junction checkouts. Never hold results.md open.
Game launches, when authorized, only scripts/test-queue.ps1; USER_PLAYING or an
unowned game blocks them. The user is playing and the launcher is open.

## Current checkout and confirmed UI correction

C:/Users/quali/Documents/Code Projects/BG3RTX, codex/ui-contamination-offline.
Main remains 47ac6a9. 17fb500 fixes recycled descriptor-set tracking: successful
new allocations clear old binding/dynamic-count maps before recording layout.
No key, shader, ABI or GPU work changes. Built bin/VkLayer_bg3rtx.dll at 15:56:50;
16/16 CTests and actual-function nine-case regression pass (old four failures).
The user reports blue blocks/flicker no longer trigger; movement/face stability
initially good. This is positive provisional confirmation, not every shadow issue.
Evidence docs/evidence/ui-descriptor-trial-20261030.json; offline original analysis
in docs/UI_CONTAMINATION_OFFLINE.md. Material-owner first-candidate freeze and RR
exposure-helper fallback remain unchanged, to isolate this successful trial.
No main merge yet: merge-green refuses while the game runs; the open launcher
also locks its executable during a rebuild.
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

## OMM OFFLINE milestone - not runtime integration

Isolated C:/Users/quali/.codex/worktrees/p4-omm-domain/BG3RTX,
codex/p4-omm-domain commit 7134338, based on main 47ac6a9 (WITHOUT UI fix 17fb500).
Never deploy that worktree's baseline DLLs over the user's corrected build.
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
- FULL isolated build/CTest 19/19, 197621 classifier, 398526 code-support, 65 identity
  checks pass. No shader/ABI/device/BLAS/runtime-setting changes, no GPU cost claims.
  docs/P4_OMM.md and evidence p4-omm-domain-20261030.json in that worktree.
Remaining: tight spatial support, GPU numeric/LOD/filter/VT proof, threshold adapter,
consistent EXT backend (KHR API differs), GPU bake/build/BLAS and completion lifetime.
Runtime priority now correct unsupported hair/cascade coverage before OMM acceleration.

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
