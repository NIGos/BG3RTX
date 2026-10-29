# Session handoff — P1 shadow flashes, 2026-10-29

Read AGENTS.md, docs/P1_PAN_FLASHES.md and the top of docs/PROGRESS_CODEX.md.
The user explicitly resumed this conversation and asked to continue the thread
Risolvi i flash d’ombra durante il pan (01a186cc-2243-7521-a2f5-19ba41b4b6c7).
Its newer P1 request supersedes the old pause and P2 priority recorded below in
historical progress. No new renderer synchronization fix has been applied.

## Workspace and permissions

Use C:/Users/quali/Documents/Code Projects/BG3RTX explicitly. This checkout is
currently on codex/p1-pan-flashes, NOT main. Main ref is 58b06b4. Keep experimental
work on this branch; only scripts/merge-green.ps1 may integrate into main.
Paired ABI 16: host and core must match. Do not reuse other branch processes.

The latest user permission allows the game visible in the background while they
use the PC. Do not require USER_AWAY or force focus. Always use real presentation.
Never launch if USER_PLAYING exists, a user game runs, or the queue/runner is busy.
Every game uses scripts/test-queue.ps1 with patterns/expect. Freeze source, binaries
and HEAD until the actual job is terminal. Observation timeout is not completion.
Never touch saves, backups, snapshot tags or junction targets. No Ponytail ever.
Restore display settings through the existing auto-run helpers. Do not spawn agents
without new user authorization. The original overall project scope remains P1-P6;
FG toggle issues and the roughly 18 fps base rate follow this explicit P1 task.

## Current checkpoint and next action

Camera identity probe redirects 1,800 / 1,800 selections, zero unmatched.
Old age guards disabled RT for exact matches, so that run was cancelled INVALID.
Now exact matches are guarded by GPU slot retirement, not present age, in host
and core. rt_primary_check=1 supplies the existing GPU primary counters without
bursts or debug colour; the A/B fixture enables it throughout. Four CTests pass.
Run the corrected p1_camera_match_job.json; quality gate is still OPEN.

LATEST USER STEERING: stop burst-tool work. Existing primary-ray evidence is
the accepted positive control (99.53-99.57% static, 89.25% slow, 31.40% fast).
Fix renderer alignment behind a key; A/B within one presented pan run. Gate
is moving primary match near the static 99.5% level; images are optional.
Current experiment rt_scene_camera_match defaults to 0 and matches the native
lighting-camera UBO identity to its scene. Paired ABI is now 16, not 15 below.
Fixture tests/fixtures/p1_camera_match_job.json alternates off/on in ONE run.
No renderer quality result yet. Do not confuse missing RT with a passing gate.

The first normal-rate final-colour positive run (08a0256) is INVALID: only
6 / 4 / 7 / 8 final samples across the four bursts, with all other channels
complete. The present counter advances during recording. Capture now binds
all channels to the depth command-buffer recording and checks that identity
at submit and analysis. Four CTests and 14 analyzer tests pass. No renderer fix
or ABI change. Repeat the same positive fixture on this corrected capture.
Evidence: docs/evidence/p1-pan-08a0256-positive.json.

Prior positive-control job a0bb416 completed on the same frozen build, with no
new driver event/dump. All three bursts lack final colour, so quality is INVALID.
Static/slow/fast captured record cadence: 18.82 / 16.84 / 18.82 fps. Primary match
99.534% / 88.044% / 30.922%. GPU conversion-camera origins in the fast burst
match lighting origins at offset +1 for all ten tested centres. This is evidence,
not proof of instance-transform timing or a final-image flash. No lag fix yet.
Evidence: docs/evidence/p1-pan-a0bb416-positive.json. Full prior log retained.

The final-colour capture now uses native hudless tonemap output, matching the
existing tracer/Streamline signature. Validation burst pan-20261029-102059-10
contains all 176 samples for 16 frames, including final RGB10A2 3840x1600 and
G-buffer 2560x1067. Dimensions are read from resources. Image values are finite.
Native UBO word 55 is unused metadata, not a camera-origin float or colour.
No copy-image VVL tags; other errors remain, including one unattributed invalid
source-buffer copy. Full VVL: captures/p1-pan-flashes/08a9c39-final-validation.log.
Capture rate 11.86 fps under validation is NOT a positive-control result.

Analyzer fixes: actual image format precision, correct Inf/NaN decoding, camera
jitter classification, footprint-bounded textured-surface matching, per-world
static noise instead of a scene-wide maximum, independent static evaluation.
13 synthetic tests and all three CTests pass. No rendering logic/lag correction.
The largest static noise must not disable every binary-mask detection.

Next queue tests/fixtures/p1_pan_positive_job.json, after fresh idle/playing checks.
It uses QuickSave_3784, Ultra, FG off, real presentation, no renderer fixes.
Plan: static calibration 11, independent static 12, slow pan 13, faster opposite
pan 14; each capture is K=16. The existing runner restores display settings.
Analyze only those new burst directories from the job's full log with
python -m scripts.qa.pan_artifacts ... --out captures/p1-pan-flashes/positive-analysis.
Report actual capture cadence, coverage, events/area, mask attribution, primary
match, offset evidence and contact sheets. If not reproduced at comparable
cadence, fix the test setup (FG on is authorized as a second case) before changing
the renderer. No independent moving-run image comparisons or guessed camera lag.

After reproduction, confirm TLAS/camera data correspond to the G-buffer epoch.
Check rt_after_capture/g_last_frame deduplication and scene-slot reuse against
GPU pose evidence; counters alone do not prove geometry lag. Add a default-off
fix only when justified, then require static-level flash and mismatch rates.
Ask the user only for final in-game confirmation after the automatic gate passes.

## Tools and preserved work

Build: scripts/build.ps1 -Test. Analysis tests: py -3 -m unittest tests.pan_artifacts_test.
Runtime shaders: tools/compile_shaders.py. Vulkan SDK: C:/VulkanSDK/1.4.357.0/Bin.
Authoritative queue records: captures/queue/results.jsonl and full log paths.
Never hold captures/queue/results.md open. Preserve validation.log before overwriting.
User source log: captures/p1-pan-flashes/source-logs/user-0929-0934.log.

Old P1 GI branch: en2-selfcheck/BG3RTX, dbd320c, ABI 14; failed original energy and
no resolved-image quality gain. Do not substitute its slow duplicated GI pipeline
for the user's normal-rate flash reproduction. Old P2 emissive branch 7a87ba7,
ABI 9, remains off; overlap hypothesis rejected and facing probe only planned.
P3 box ABI 9 differs from P2; gates open. P4 CPU classifier only. P5 depends on
EN2. P6 water design minimum done; runtime open. Backups D:/BG3_Tools read-only.
The one authorized reset was consumed; no new reset authorization/credit assumed.
