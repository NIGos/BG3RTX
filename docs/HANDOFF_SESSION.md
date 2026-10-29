# Session handoff — P1 queued; P2 code/log-only candidate, 2026-10-29

Read AGENTS.md, then the top of PROGRESS_CODEX.md. No Ponytail. No subagents.
Latest user instruction: ROTATION is the main gate. Foreground real-present
jobs require captures/queue/USER_AWAY. It is absent at preparation time.
USER_PLAYING or an existing user game blocks execution. Never create USER_AWAY.
The older permission to test while the user works no longer applies. Latest
continuation explicitly authorizes P2 FG code/log-only work and RT cost review,
with no game launch. It overrides the older P2 deferral at the bottom.

## Latest P2 continuation

Default-off fg_current_destroy_only only resets FG stability for current-input
destruction, not rotated-out historical watched images. Consumption-fence waits
remain for retired views/images. Every host options decision now logs input
roles, ages, handles, API results and the cooldown event. Seven CTests and four
audit tests pass; no shaders or ABI changed. No runtime benefit/cost established.
User excerpt: 27 host transitions, all 13 offs follow destruction by 2-63 ms;
destroyed roles are unknown in old logs. Three SDK offs explicitly say window
not focused with mode on. Do not treat these as invalid DLSS-G inputs.
Queue readiness identifies 20 offs: 17 current-ready destruction cooldowns,
three stale UI age 4 during swapchain recreation; do not weaken that UI gate.
RT log review: old/new GI median 0.466/6.212 ms, LS 0.378/5.271 ms, nearby RR
2.590/2.337 ms. RR was on in both; old was headless, new foreground presented.
Scene/settings/diagnostics differ; same newer settings in user CTY log have
0.937 ms RT interval. Exact causal split of 4.269 versus 15.040 ms is UNKNOWN.
See docs/evidence/p2-fg-and-rt-log-review.json and top of PROGRESS_CODEX.md.
P1 jobs stay queued and unchanged. No new P2 game job or merge is authorized
by this code/log-only continuation. Runtime candidate remains unmeasured.

## Checkout and current state

Use C:/Users/quali/Documents/Code Projects/BG3RTX explicitly.
Branch codex/p1-pan-flashes; main remains 58b06b4. No experimental merge.
314840d implements default-off rt_scene_rebase. 5c0057a records its incomplete
pan gate. da4b64d contains the pushed rotation preparation on this branch only.
Host/core remain paired ABI 16; rebuild both for the FG-independent crash logger.
Seven CTests pass. Ordinary 21 shader binaries remain byte-identical to baseline;
20 rebase variants validate. Diagnostic keys default off.

No live game was launched during rotation preparation. Two queue jobs are
pending: 001-p1-rotation-preflight.json, then 002-p1-rotation-origin-ab.json in
captures/queue/pending/. They must stay
pending until USER_AWAY exists. Start only through scripts/test-queue.ps1.
No display settings were changed during preparation.

## Next: qualify the job, then ONE rotation A/B

Both fixtures use QuickSave_3784, visible foreground real presentation, FG off,
Ultra, no frame cap, and normal rendered colour. Do not change source, binaries
or HEAD during a live job. Every job has patterns, expect and runner expectations.

1. tests/fixtures/p1_rotation_preflight_job.json: four static 90-second windows,
   production / primary+epoch / primary+epoch+rebase / production. Timeout 700 s.
   Must finish, avoid exceptions, keep a genuinely static GPU pose, active RT
   and at least 18 fps. Retains process exit status and arms crash_dump=1.
   Reports diagnostic overhead and rebase cost within the same static run.
2. tests/fixtures/p1_rotation_job.json: sixteen 90-second windows, timeout 1800 s.
   Static off/on, rotation 60/120/240 px/s in both directions with rebase off/on
   at each speed/direction, then static off/on again. This job requires a valid,
   successful preflight from the SAME build. A failed prerequisite blocks it.

Rotation uses the existing window-targeted PostMessage input path, middle mouse
plus horizontal movement. Native inputconfig confirms mouse:middle is the
camera rotation toggle. Real-time bounded drags have released-button reset
gaps. Pointer is centred once in the owned foreground game while USER_AWAY exists.
Delivery is NOT runtime-verified yet: the gate requires actual GPU camera
rotation, opposite signed yaw and increasing measured speeds, not just input logs.

Gate: primary match with rebase on while rotating reaches the measured static
level, approximately 99.2%. The threshold comes from static repeat variability.
Key-off must reproduce mismatch. Missing RT, absent GPU pose, low cadence,
ignored rotation or an incomplete plan NEVER pass. Stop after a clear gate result.
Do not add another selector. Merge only through scripts/merge-green.ps1 if the
quality gate passes; then document key, save and what the user should inspect.

Reports, written only by actual queued runs:

- captures/p1-pan-flashes/rotation-preflight-result.json
- captures/p1-pan-flashes/rotation-gate-result.json
- captures/queue/logs/: full game log, runner transcript and status JSON
- captures/crash/: first-chance fatal minidump when enabled and captured

## Exit at 242 seconds: unresolved cause, observation gap repaired

Old p1-origin-rebase-ab had timeout 750 s; it reported game exited at 242 s
before runner cleanup. It was not that deadline or the normal CloseMainWindow
cleanup initiating the exit. No matching WER/Application/NVIDIA/Aftermath dump
was found. An external close or engine/process termination remains possible.
The exception logger was installed only after Streamline loading, so FG off
skipped it. It now installs independently of FG, with default-off crash_dump.
The runner logs signed/hex exit code, remaining deadline, phase, and its own
close requests. Unexpected exit now invalidates queue results. No claim that
the original crash/exit itself has been fixed; preflight must establish stability.

## Current hypothesis and measurements

All four rt_common query entries translate origins by C_lighting - C_scene.
Directions/t stay unchanged; returned transforms translate back. Native pool
instance rows and dynamic XFB use camera-relative world axes. The stamped scene
snapshot reads the actual conversion camera UBO on GPU. This establishes the
coordinate convention, not the epoch of every captured native instance row.

rt_primary_epoch=1 adds GPU source/lighting pose separation, actual lighting
camera motion, and nearest-hit pool versus dynamic-soup primary cohorts.
It reuses the primary ray; misses stay in global primary match. Snapshot data
is collected with rebase off as well as on. Compare cohorts and pose separation
in the ONE rotation run. These counters alone cannot prove native GPU instance
rows came from an older frame; persistent pool-specific error after rebasing
would motivate same-draw GPU row/TLAS-row evidence next, not another selector.

Old incomplete job: late static off 99.164-99.177%, on 99.157-99.177%, RT active
and no missing scenes in interior windows; median 17.60 / 17.50 fps. Initial
cursor edge drift invalidated calibration. No moving rebase-on result exists.
Evidence: docs/evidence/p1-origin-rebase-314840d.json. Full log:
captures/queue/logs/p1-origin-rebase-ab.1.123412.log and .runner.txt.

RT interval off/on medians 15.592 / 15.040 ms are not isolated sun costs. The
interval includes origin, primary diagnostics, GI/temporal/AO and sun resolve;
local lights/reflections are separate. Historical 4.269 ms was a different
WLD_Main_A/configuration, RR GI 1 versus 2 rays, rebuild 60 versus 600, and even
44 missing scenes in that sample. This does not quantitatively explain the
remaining difference. Preflight measures production versus diagnostics directly.

## Preserved evidence and rules

Accepted older pan positive control: about 21 fps, static 99.53-99.57%, slow
89.25%, fast 31.40%. captures/p1-pan-flashes/08a0256-positive-full.log.
Rejected selectors: 0edf4ed address match, 0dff159 ring epoch alias, f6b7ad9
exact pose selection disables RT during movement. rt_scene_camera_match stays 0.
Ordinary shader baseline: captures/p1-pan-flashes/rebase-spv-before/.
Rotation preparation evidence: docs/evidence/p1-rotation-preparation.json.
Do not resume burst tooling, FG toggles or base-rate work before this P1 gate.
Never touch saves or backups, rewrite history, move snapshot tags, or clean a
checkout with junctions. Never hold captures/queue/results.md open. Display
settings must restore after queued runs. Broader P1-P6 work remains unfinished.
