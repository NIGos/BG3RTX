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

301 is terminal and execution-valid on 4ba5bfc, paired ABI 14. All 36 raw captures
are finite, alpha-exact and pass 23 GPU predicates; max static shift 0.36 pixels.
Normal rejection 13.24%. On/off +0.6464%, paired CI [-0.00005839, +0.00028256] Y
includes zero, but original-PRE +0.4599%, CI [+0.00003984, +0.00011942] Y FAILS.
Do not claim complete energy parity. Evidence is committed on P1 at 816c986.
Latest trace median 1.250 ms; whole GI 52.621 ms under diagnostic validation.
Full VVL is preserved at P1 build/p1-normal-footprint-301.validation.log.
No scoped layout/push/write tags appeared; other VVL errors remain unresolved.

P1 was updated from main as d836c09 after the terminal audit. Only shared QA,
docs/AGENTS and the already verified local-light reference fix arrived; GI code
and paired binaries are unchanged. All 12 CTests, 49 shader files, QA detectors,
eight energy tests and five merge-gate scenarios pass. No phase was merged to main.

302 is terminal and execution-valid on d836c09. Within ONE run, off/on residual
0.1393289 / 0.1372433 (-1.497%), cohort GI Y -17.904%, motion 68.07 / 81.61 pixels
(+19.888%). No quality gain: energy, motion and compatible populations differ.
Samples 34,379,240 / 25,885,465; grid coverage 29.75% / 23.01%. Trace median
0.551 ms; whole GI 65.059 ms under the experimental duplicated pipeline.
No VVL in the moving run; full 301 log remains archived. Evidence is on P1.

303 is LIVE on frozen 8fe00de, runner PID 30240 started at 08:54:05 after fresh
playing/process/queue checks. Existing balanced 96-second pan fixture, now with
fps_cap=8 in BOTH modes to test the roughly 20% per-frame motion confound.
A common 125 ms cadence is above logged average whole-GI cost; this is a test
condition, not a quality gate. Require the actual framecap/watch logs and report
motion, energy and coverage; never infer a win merely from a lower residual.
No GI/camera/source/threshold/ray changes. Static 301's original-energy gate
still FAILS. Do not edit P1 or launch another game until actual terminal 303.

The complete fresh fallback now also tries current radiance for absent/nonfinite
previous colour (4e11688). The disabled control remains original. Unsupported
unlit/nonfinite current sources remain unavailable; do not invent their energy.

4ba5bfc corrects native FP16 normal precision under gi_history_normal_footprint.
Vulkan permits conversion to either neighbour; the old half-step bound can be
short by factor 2. Exact adjacent finite values now bound the shading normal and
owned geometric normal used for footprint support. Zero/subnormal/bin boundaries
are covered, overflow/nonfinite intervals cannot certify identity. Metadata
word 1.w marks the convention. No new ABI, resource, angle or ray/PDF change.
Spatial support remains 3 by 3 full-res / 4 by 4 half-res, same-plane TLAS only.
The fresh runtime prototype still requires full-resolution RR.

GPU mode 1 row 12 now expects [8388607, 8388607, 23, 1]. Projection diagnostic
modes 2/3 retain 15 bits. All 12 CTests and 24 shader ABI checks pass; four shader
variants compile; seven ordinary shaders remain byte-identical to d9cb628.
The 301 terminal audit passed finite/alpha/pose checks and reason-count closure.
Retain its original-energy failure when interpreting the next pan diagnostic.

Earlier runs 297/298 fail energy parity; 299 is low power. Moving run 300 also
establishes no gain. Preserve their complete evidence rather than reusing a
favourable interval or comparing independent pans.

Raw/analysed evidence is in docs/evidence on P1. Scoped layout/push/write errors
were absent in 299; other VVL categories remain unresolved. No default/main
promotion. A weak interval or lower-energy residual cannot close P1.

## Workspaces

Use explicit paths. The app may select a different managed checkout.

- Main: C:/Users/quali/Documents/Code Projects/BG3RTX, branch main, pushed.
- P1: C:/Users/quali/.codex/worktrees/en2-selfcheck/BG3RTX, codex/p1-pan, 8fe00de.
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
