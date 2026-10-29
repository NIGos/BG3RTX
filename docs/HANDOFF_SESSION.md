# Session handoff - 2026-10-29

Read this first, then the top of docs/PROGRESS_CODEX.md and AGENTS.md.
The goal remains active until Saturday 2026-10-31 at 20:00 Europe/Rome.

## Hard rules

- Never modify user saves, force-push, rewrite history or alter snapshot tags.
- Never use Ponytail, its skills, hooks or instructions.
- Before every launch check USER_PLAYING, actual BG3/runner processes and pending/running jobs.
- Games run only through the serial hidden queue, with patterns and expect checks.
- Freeze an active job's source, binaries and HEAD until the actual process is terminal.
- Merge only through scripts/merge-green.ps1 after reading the deciding numbers; push both branches.
- Quality R0, derived gates and quality before speed on Ultra. Research defaults stay off.
- Never archive or clean a checkout with live junctions to shared SDK/data directories.
- Backups in D:/BG3_Tools are read-only. Do not touch snapshot tags or backup files.
- Background 120-frame statistics require a job hold of at least 90 seconds.
- Write short English sentences with normal spacing between numbers and words.

The one authorized usage reset was consumed at weekly 100% on October 29.
The successful reset was confirmed by a fresh usage reading. Reset credits: 0.
Do not redeem another without fresh user authorization.

## Current priority: P1 is active

The user explicitly resumed P1. Do not park it or start more attribution/exposure
research before measuring the concrete fix. Required order:

1. Rejected history uses a complete fresh current-frame sample with the same expected value.
2. Derive the normal tolerance from encoding precision and the actual GI sample footprint.
3. Static camera: key-on mean GI must equal key-off within measured noise.
4. Then measure the reprojection residual with live toggles in ONE moving run.

Never compare images from independent pans. A lower residual with lower energy is
not a quality improvement. Keep the original weights, PDF, ray count and PRE alpha.
Do not block on the old aggregate AO discrepancy; saved PRE alpha is authoritative.

## Live job and next action

296 is LIVE on frozen P1 commit 0a0a9d2, paired host/core ABI 14.
Runner PID 8396 started at 07:30:39 after fresh idle and playing checks.
Fixture: tests/fixtures/gi_normal_footprint_static_job.json.
It takes 12 static off/on captures, then holds for 90 seconds. No exposure hold.

New key gi_history_normal_footprint stays off by default. The sample cell and
point reprojection derive a 3 by 3 full-resolution or 4 by 4 half-resolution
support. Only proven same-plane TLAS neighbours bound shading-normal variation.
Encoding, normalization, oct16 and arithmetic precision supply numerical bounds.
Depth and normal still test the actual contributing texel. No neighbour supplies
replacement history colour. Existing complete fresh fallback retains all weights.

The fresh prototype currently requires full-resolution RR. Half-resolution
support is derived and unit checked, not a claimed half-resolution runtime pass.
Existing predicate counters report gate-on queries and each rejection reason.
GPU test mode 1 now expects row 12: [1048575, 1048575, 20, 1].
All 12 CTests, 24 shader ABI checks and four changed shader compiles pass.
Seven ordinary GI/LS/sun/RF binaries are identical to d9cb628.

After the actual job ends, inspect all 12 raw samples with scripts.qa.gi_energy:
positive finite GI, valid pose, static camera, saved alpha and off/on energy.
Check the GPU predicate rows, reason-count closure, full VVL categories and GPU
cost. Keep the actual noise interval width visible. Only then run the single-run
pan fixture. Do not substitute another research branch for this measurement.

## Workspaces

Use explicit paths. The app may select a different managed checkout.

- Main: C:/Users/quali/Documents/Code Projects/BG3RTX, branch main, pushed.
- P1: C:/Users/quali/.codex/worktrees/en2-selfcheck/BG3RTX, codex/p1-pan, 0a0a9d2.
- P2 D-EM: C:/Users/quali/.codex/worktrees/p2-emissive/BG3RTX, ca1959b, D-EM ABI 9.
- P3: C:/Users/quali/.codex/worktrees/p2-pending/BG3RTX, codex/p3-box, b913e8e, ABI 9.
- P4: C:/Users/quali/.codex/worktrees/p4-omm/BG3RTX, codex/p4-omm, 8d902d5.
- P6: p6-water/BG3RTX, codex/p6-water, cd04c63.

The two ABI 9 research branches have different contracts. Never reuse their game
processes. SDKs and generated asset data are available in the main checkout.

## Established results and limits

Main rendering remains 6dba0a8. P6 water design 441904a and the QA-only exposure
hold fix 7a98949 are merged. No GI, box-light or OMM prototype is promoted.

P1 historical results are in the phase specs and docs/evidence. Runs 265/266 are
INVALID because a base descriptor layout lacked origin binding 40. The corrected
binding passed the scoped audits. Run 276 gave no demonstrated moving gain.
Run 293, with a legal held exposure, still failed original-PRE energy by +0.595%.
It had six RGB-exact off controls and exact saved alpha. Exposure/attribution work
is now stopped by user steering; those results do not replace the current fix.
The original missing/nonfinite-colour cohort remains a known fresh-source limit.

P2: local-light reference is merged after two hold-90 repeats with zero differences.
EN2 and D-EM remain off because their material/radiance gates failed. D-EM test 295
finished before P1 resumed; do not pursue it while the required P1 measures remain.

P3: real box shadow rays and cookie/unknown coverage have been exercised.
Run 291 found 0.09991% format-incompatible receivers out of 2.950 billion samples.
The cause, native accuracy, cookie filter, unknown-delta and moving gates remain open.

P4: a conservative CPU classifier passes 197,621 oracle checks and four CTests.
The sampling-domain provider, GPU bake and OMM backend are not implemented.
P5 depends on EN2. P6's minimum water design is done; runtime water remains open.

## Validation and cleanup

Preserve full validation.log before the next validation run. Inspect every layout,
push and descriptor-write category, including VUID 07988. Other pre-existing VVL
categories remain unresolved; scoped success never means globally clean validation.

The QA exposure hold now uses legal storage writes. Stable test 294 passed with
finite dumps and matching held probes. Its evidence is under main captures/
qa-exposure-hold-294. During archival, the tool followed SDK/data junctions and
emptied their targets. SDKs and the generated database were restored from read-only
backups; all 992 SDK file hashes and both database/manifest hashes match the backup.
The recovery manifest is committed. Never repeat cleanup with shared junctions.

Keep HANDOFF_SESSION and PROGRESS_CODEX current. At the Saturday stop time, write
the final summary, push main and work branches, then stop.
