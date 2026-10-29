# Session handoff — P1 rebasing active, 2026-10-29

The user explicitly resumed P1 for a bounded origin-rebasing fix and ONE
presented pan A/B. Stop at a clear gate result or approximately two hours.
No Ponytail ever. Read the top of PROGRESS_CODEX.md for the current outcome.

Current implementation adds default-off rt_scene_rebase, all four rt_common
query entry points, scene-specific GPU camera snapshots in the stamped set,
and cached normal/rebase pipeline variants. Ordinary 21 SPIR-V binaries are
byte-identical; 19 variants validate. Paired ABI remains 16. Prepared fixtures:
tests/fixtures/p1_rebase_job.json and p1_rebase_plan.json. Runtime gate pending.
scripts/qa/rebase_gate.py reads retained runner phase clocks and primary/RT/fps
windows. Missing RT cannot pass. Do not launch a second experiment after a
clear failure; state the next evidence-based hypothesis. No merge yet.

## Checkout

Use C:/Users/quali/Documents/Code Projects/BG3RTX explicitly.
Branch codex/p1-pan-flashes; implementation commit f6b7ad9, pushed.
Main remains 58b06b4. Experimental changes have NOT been merged.
Host and core are paired ABI 16. Five CTests pass.
Read AGENTS.md and the top of docs/PROGRESS_CODEX.md before resuming.

## Latest user steering: the actual P1 gate

Continue the thread Risolvi i flash d’ombra durante il pan
(01a186cc-2243-7521-a2f5-19ba41b4b6c7).
The user explicitly says STOP polishing the burst tool. Existing primary-ray
check evidence is the accepted positive control: real presentation, FG off,
about 21 fps; static 99.53-99.57%, slow pan 89.25%, fast pan 31.40%
(in front 36.1%, behind 27.9%). It shows RT/G-buffer displacement during pan.
Fix the camera/TLAS frame alignment behind a default-off key. A/B within ONE
presented moving run. Gate: moving primary-ray match reaches the static level,
approximately 99.5%, with RT active. Missing RT is NEVER a passing result.
Burst images are optional illustration, not a gate. Ask for in-game confirmation
only after the automatic primary-match gate passes.

## What was tried, and what is NOT fixed

rt_scene_camera_match defaults to 0. The existing scene selector normally hands
out the latest global TLAS/set pair. Experiments tried selecting another scene:

- 0edf4ed: native UBO buffer/offset matching. All 1,800 queries redirected but
  the old present-age guard rejected the selected scene; RT was disabled.
- 0dff159: permit exact camera selection while the GPU slot remains live,
  independent of present age. Address matching still chose an old upload-ring
  epoch: built 2446, present 2449. Cancelled INVALID, not a quality improvement.
- f6b7ad9: match actual absolute origin and view rotation, ignoring projection
  jitter and unused metadata. Static off/on stays around 99.55%, with RT active.
  During slow motion there is NO exact matching scene. Example lighting origin
  (-342.220398, 17.340019, -155.492493), latest source snapshot
  (-342.220520, 17.340019, -155.463211). Key-on accumulates 962 unmatched frames
  and disables RT. This hypothesis is rejected as a fix; cancelled INVALID.

Source camera poses are CPU snapshots read from the capture's native UBO at
rt_after_capture. Lighting pose is read from its native compute UBO in the
callback. Native static instance rows are read from each recorded draw's mapped
instance stream at bvh_on_submit. Current data does not yet prove the CPU pose
is exactly the coordinate origin used by every GPU geometry path.

NEXT IMPLEMENTATION IDEA, NOT IMPLEMENTED: consistently rebase ray origins
between lighting space and the actual TLAS camera-relative space, using the
measured scene origin, instead of searching for a nonexistent identical pose.
The translation sign is lighting_absolute_origin minus scene_absolute_origin.
Confirm the source origin used by native instance matrices and dynamic XFB
conversion; CPU snapshots and GPU UBO observations need not be interchangeable.
Apply consistently to all four query entry points in rt_common.hlsli (sun,
local lights, GI, AO/reflections), preserving lighting-space returned positions.
Keep off-by-default shader variants byte-identical to the existing ordinary
SPIR-V. No compensation shader or new rebase key has been written yet.

## Existing measurement and saved evidence

rt_primary_check=1 reuses gi/pan_primary.hlsl for GPU primary counters every
frame, WITHOUT burst readbacks or a debug-colour overlay. Default 0.
The existing tests/fixtures/p1_camera_match_job.json and _plan.json alternate
static, slow and fast off/on phases in ONE run, real presentation, FG off,
QuickSave_3784. It still tests the failed selector; update it for the next fix.
Do not use rt_debug=3 for normal-colour quality runs: it overwrites the image.

Full preserved logs in captures/p1-pan-flashes/:
- 08a0256-positive-full.log: accepted primary positive-control evidence.
- 0edf4ed-camera-match-full.log: first selector, missing RT.
- 0dff159-camera-epoch-full.log: upload-ring address aliasing.
- f6b7ad9-camera-pose-full.log: exact pose has no moving match, missing RT.
Queue kept logs are also under captures/queue/logs/.
The 21 ordinary shader baseline binaries were compiled before any rebase edit:
captures/p1-pan-flashes/rebase-spv-before/. No HLSL source has changed yet.

Burst work is stopped. Earlier capture fixes are committed but final images
remain incomplete at normal rate. Do not spend more time repairing this before
the renderer fix. Old burst reports marked INVALID are not absence-of-flash proof.

## Execution rules and permissions

Only scripts/test-queue.ps1 launches games; every job needs patterns/expect.
Check USER_PLAYING, actual bg3/bg3_dx11 processes, runners and pending/running
before any launch. Never kill a user game. Cancelled probes above were verified
owned test processes; their runners completed cleanup. Display helpers restore
the user's settings. Latest standing permission allows visible second-plane
real presentation while the user uses the PC, without stealing focus or
requiring USER_AWAY. The latest request also refers to a foreground pan job.
Never headless for this gate. No source/binary/HEAD changes during a live job.
Never hold captures/queue/results.md open; use results.jsonl and kept logs.
Never touch saves, snapshot tags, read-only backups D:/BG3_Tools or shared
junction targets. Do not archive a checkout with external/data junctions.
No subagents without new user authorization. One reset was already consumed;
no new credit or reset authorization is assumed.

## Preserved broader work

The P1-P6 overall goal remains unfinished and paused. Older GI experiment
(en2-selfcheck, dbd320c, ABI 14) failed its energy/quality gate; do not substitute
its slow duplicate pipeline for this pan test. P2 emissive 7a87ba7, P3 box,
P4 OMM and P6 water work remain separate; see historical progress. FG toggle
and base performance follow the explicit current P1. Merge only through
scripts/merge-green.ps1 after reading the deciding numbers.
