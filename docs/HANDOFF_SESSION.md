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

296 is terminal and execution-valid on P1 commit 0a0a9d2, ABI 14.
All 12 captures passed 20 GPU predicates, finite GI and exact saved PRE alpha.
Normal rejection was 13.45% of gate-on queries. Energy is inconclusive: a final
baseline spike widens the paired interval; original-PRE check fails (+0.350%).
Every sample is retained. No pan quality pass is claimed. Full evidence is on
P1 in docs/evidence/p1-normal-footprint-static-296.json.

297 is terminal and execution-valid on b6165ce. All 36 captures are finite,
saved alpha is exact, and 20 GPU predicates pass in each. Normal rejection is
14.70% of 291,499,658 gate-on queries. Static energy FAIL: on/off +2.142%,
paired CI [+0.00005157, +0.00068514] Y excludes zero. Original-PRE +0.271%
includes zero narrowly; this cannot waive the paired failure. All samples stay,
including two original-PRE spikes in the on cohort. Evidence is on P1 in
docs/evidence/p1-normal-footprint-static-297.json. No pan quality pass.

298 is terminal and execution-valid on 6e5a553. Shuffled pairing still FAILS:
on/off +0.8243%, CI [+0.00005181, +0.00023276] Y; original-PRE +0.3690%,
CI [+0.00003676, +0.00009067] Y. Both exclude zero. All 36 captures are finite,
alpha-exact and pass 20 GPU predicates; max static shift 0.44 pixels. Normal
rejection is 14.22%. Full evidence: P1 docs/evidence/p1-normal-footprint-static-298.json.
No scoped layout/push/write errors, but other VVL errors remain. Latest trace
median 1.734 ms; whole GI 53.658 ms under diagnostic validation.

A concrete early return was corrected after 298: missing/nonfinite legacy
colour now reaches the fresh current-frame fallback with the surface gate on.
The control remains original. This supersedes the old availability-cohort
restriction. Unsupported current sources remain unavailable. It does not claim
to explain the positive bias. No source components, weights or thresholds change.
All 12 CTests, eight energy tests and 24 shader ABI checks pass. Both changed
fresh shaders compile; seven ordinary shaders remain byte-identical to d9cb628.

299 is terminal and execution-valid on 4e11688. All 36 captures pass finite GI,
exact alpha and 20 GPU predicates; max shift 0.621 pixels. Normal rejects 14.03%.
Both intervals include zero, but LOW POWER: on/off +2.651%, relative interval
about [-1.093%, +6.395%]. Original-PRE +0.2126%. This does not erase 297/298,
prove equivalence or permit promotion. Full evidence is on P1 under run 299.
Trace median 1.242 ms; whole GI 52.880 ms under diagnostic validation.

300 is LIVE on frozen 45a275f, runner 28868 started at 08:28:17. The renderer
is unchanged from 299; only QA fixtures/docs changed. ONE moving diagnostic,
24 balanced four-second gate blocks and direction reversals, 96-second hold,
private/self-test diagnostics off. Never compare it with independent pan images.
Report residual, energy, coverage and motion together. Given the weak static
interval, this is diagnostic evidence only, not a quality gate or promotion.

After actual terminal 300, preserve its results and correct a separate verified
normal-precision bug. Native FP16 format rounding can choose either neighbour
(Vulkan Fundamentals, Floating-Point Format Conversions). The existing half-step
bound can be too small by factor 2. A CPU endpoint sweep covers 31,743 positive
finite intervals. Use exact adjacent representable values, including zero and
bin boundaries; reject overflow/nonfinite intervals. Do not tune an angle.
Then rerun offline checks and static/pan on the corrected normal precision.
No P1 edits or new launches while 300 is live.

The default-off normal footprint derives spatial support from the real sample
cell and reprojection rounding, bounded to same-plane TLAS neighbours. The
fresh prototype requires full-resolution RR; half-resolution support is derived
and unit checked, not a claimed half-resolution runtime pass. GPU row 12 in
test mode 1 is [1048575, 1048575, 20, 1]. All 12 CTests and 24 shader ABI checks
passed; seven ordinary shaders were byte-identical to d9cb628. No scoped layout,
push or descriptor-write VVL errors appeared in 296; other VVL errors remain.
Latest trace median was 1.273 ms and whole GI 54.986 ms per frame under validation
and duplicated experimental passes, not a release performance measurement.

## Workspaces

Use explicit paths. The app may select a different managed checkout.

- Main: C:/Users/quali/Documents/Code Projects/BG3RTX, branch main, pushed.
- P1: C:/Users/quali/.codex/worktrees/en2-selfcheck/BG3RTX, codex/p1-pan, 45a275f.
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
