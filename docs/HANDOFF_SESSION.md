# Session handoff — 2026-10-28, evening

Read this first, then the top of docs/STATUS.md and docs/PROGRESS_CODEX.md.
STATUS describes the stable code; PROGRESS_CODEX is the current work log.

## Goal and hard rules

Advance through P1 to P6, in order, until Saturday 2026-10-31 evening.
The stop target is 20:00 Europe/Rome: update the final progress summary,
push main and the work branches, then stop. Work autonomously in the meantime.

- Never modify user saves, force-push, rewrite history, or move/delete snapshot-20261027/* tags.
- Before any runner/game launch, check captures/queue/USER_PLAYING and live BG3 processes.
- The marker or an unowned BG3 process means the user is playing: do offline work only.
- In-game tests run only through the serial queue, hidden/click-through, with expect patterns.
- Merge features only through scripts/merge-green.ps1 after reading the deciding log numbers; push main and branches after each merge.
- One codex/<phase> branch from current main; cherry-pick only useful old work.
- QUALITY R0, derived gates, quality before frame rate on Ultra. New features off by default until their gates pass.
- Prefer fixes and short A/B loops. No FG/HDR-UI/swapchain work unless a phase needs it.
- Do not use Ponytail, its skills, hooks or instructions.

No user decision is currently required; the active goal supersedes the old pause.
Put future questions atop PROGRESS_CODEX and continue conservative/offline work.

## Main and research state

Main 441904a includes the P6 water design; rendering remains 6dba0a8 (corrected
local-light reference/merge checks). No P1 research feature is in main yet.
Remote: origin, private NIGos/BG3RTX. Preserve all snapshot tags and backups.
Old pre-rewrite SHAs can be translated with
D:/BG3_Tools/git_backup/commit-map-old-to-new.txt; never rewrite that history again.

Research code/evidence is in the managed pan-motion-vectors/BG3RTX worktree.
Its current head before this handoff update is 929be7a, plus the earlier hider
fix 850e06a. Keep that work recoverable while creating the fresh phase branch.
The camera agent is developing a guarded native-camera follow-up in
motion-jitter/BG3RTX, branch codex/camera-native. Import only what P1 needs.

## P1 — Pan instability

User report: RT lighting/shadows seem to detach from objects during pans;
Ctrl+Shift+F9 (all RT off) restores the normal appearance.
Old static S3 candidate 03679ed fails: GI 172.938 per mille, mean 0.001258 PQ
against 0.000978. Details in PROGRESS_CODEX. Those fixed-camera scores and
independent moving trajectories cannot establish a P1 improvement.

Already established in research:
- Offscreen/NaN/Inf owned-motion GPU tests fall back to fresh GI samples exactly.
- Full-resolution radiance/depth writer pairs match the exact bound game depth.
- Static/moving camera transport works; movement is observed in GPU camera position.
- Full additional native camera payload is still absent, so its verdict is inconclusive.
- An introduced private metadata write hazard was fixed by nonoverlapping ranges.
- Static/moving repeats on 929be7a pass their queue checks (116 / 126 s), with no faults and zero messages naming 3,255 / 3,345 logged private handles.

Fresh codex/p1-pan is in en2-selfcheck/BG3RTX, now d8b204f, pushed. It excludes
unrelated FG work. Current-radiance 3c7b411 is default-off, paired ABI14; scope
and old static evidence: phase docs/CURRENT_RADIANCE.md and main PROGRESS_CODEX.
c428cde fixes a recursive tracker shared-lock. Smoke proves GPU-valid pose,
changed RGB, exact PRE alpha, nine complete finite writers. Production repeat:
79 s, zero events, 1,200 accepted/zero rejected. Static off control 226:
GI 150.059 per mille, mean 0.000988 PQ (limit 0.000978), one NV153 also with
candidate off. No moving quality improvement is established for that hypothesis.
Latest user steering: STOP cross-run image comparisons. Use a per-run reprojection
residual or alternate the key within ONE moving run. Do not block on AO counters;
compare their key-off control on the same build and obtain the pan number.
The -Move fixture isolates physical input with a client-relative virtual pointer;
real presentation stays hidden by the queue. Earlier focus experiments reverted.
gi_history_surface_gate=0 implements previous-depth AND world-normal acceptance
before bounce reuse. Metadata52 is paired with radiance; a CPU-zero initial token
is consumed by PRE and republished only by paired POST. Existing ABI14 pins
private resources; geometry math remains native. No new camera transport.
11 CTests, 45 shaders, 20 ABI checks and seven byte-identical ordinary shaders pass.
Smoke 242: eleven GPU predicate cases pass twice and captured alpha matches over
2,731,520 pixels each, but aggregate AO counters differ. 243 repeats that issue
with native geometry math. Both have zero events/dumps. Do not block the pan.
gi_pan_metric=0 now measures RR GI against paired previous GI with native B2/B5
camera reprojection, round-trip validation, static-compatible depth/normal/MV
filtering and a rotating 4x4 diagnostic grid. GPU buckets separate off/on even
across a reporting window. Spec: phase docs/GI_HISTORY_SURFACE_GATE.md.
Job 245 (bc15dd3 DLLs) is valid: 149 s total, 65 s single moving session, key
alternated every four seconds, zero events/dumps/build changes. Residual off/on
0.123120933 / 0.044079878 (-64.20%); sample mean GI Y 0.019496607 / 0.008939488
(-54.15%). Eligible samples 29,136 / 59,473, coverage 0.07653% / 0.07900%.
Motion also differs (119.41 / 260.65 render px): these are limited cohorts,
NOT a verified visual improvement. AO counters also differ off: 11.14% / 9.37%.
Evidence: phase docs/evidence/p1-surface-pan-live-245.json; all new keys remain off.
Production 247 (AO diagnostic off) is valid, 130 s total/56 s session, zero faults:
residual 0.127412/0.046099 (-63.8%), sample GI Y 0.019353/0.005472 (-71.7%).
Coverage 0.11889%/0.02111%; no quality win. 246 invalid: >2000-draw fixture blocked
pan in lighter gameplay. b5e0e93 uses post-load stability; 36 helper checks pass.
409ffe1 validates only the actual integer-loaded colour texel, removing four
unused neighbours. Bounds unchanged; 6 surface shaders/20 ABI checks pass and
7 ordinary shaders byte-identical. 248 valid: 403 s total/57 s moving session,
zero faults; off/on residual 0.151384/0.064525, GI Y 0.321177/0.008690,
coverage 13.3877%/7.1512%. Unequal cohorts/energy: still no quality win.
Same-frame legacy/gated raw pairs in 242/243 confirm the OLD five-texel gate
removes 71.8–99.9% of bounce energy; alpha identical in all four captures.
Job 249 is LIVE on frozen d8b204f: paired same-frame energy on point-footprint
code, diagnostic/private headless capture. It is not another pan-quality gate.
Read actual process/job state before more work. No P1 merge; all keys stay off.
Keep this checkout's source and binaries frozen while its queued job is active.
Use existing B2/B5 camera data, derived bounds, the actual contributing colour
footprint and GPU-ordered availability, not a CPU epoch certificate.
Fixed-camera scores cannot close P1. Default VVL limit 3 weakens the smoke count.
Do not close P1 from diagnostic passes alone. The phase is done when each
effect meets its derived gate, or is clearly reduced with its cause explained.
Record the candidate key and user check in PROGRESS_CODEX: static building/ground
edges while panning, candidate off/on, plus Ctrl+Shift+F9 as the all-RT reference.
If progress stalls, write PAUSED.md with evidence/next idea, move on, return later.

## P2 — Pending merges

Read each branch's HANDOFF.md and inspect its latest deciding logs/jobs.

| Branch | Feature | Deciding evidence |
|---|---|---|
| worktree-agent-a94065fdd50bc8494 | EN2 hit materials | 170-en2fix-S8-cov: joined > 0, coverage self-check, CPU wait |
| worktree-agent-a2212c228ec492214 | D-EM emissive | em12-af-check and af-view19/20: resolve AF 304be1c7 ratio around 0.36 |
| codex/p2-pending | Frozen local-light reference | Merged at 6dba0a8; fresh cap0/cap1 hold-90 repeats valid, zero differences |

P2 reference and gate changes are merged and pushed, with the phase branch.
Fresh deciding logs end 184221 / 184404: 25 windows each, 8,172,707,324 /
8,172,707,294 pixels and zero differences. Full build, all 3 CTests, 32 shader
files and QA pass. EN2 still fails blue/metalness/bias; D-EM AF family 304be1c7
still fails at 0.361 / 0.362. Both remain off. See docs/P2_PENDING_MERGES.md.
merge-green now runs all CTests and shader entries, checks main/playing/merge
state and aborts its own failed merge. Its failure/success fixtures pass.

## P3 to P6

- P3 box lights: codex/p3-box at 907857c, pushed; managed p2-pending/BG3RTX checkout. Shader foundation from a28a4c7, host selection/binding/cleanup and default-off ls_box now implemented. Build, 3 CTests, defaults and 34 shader files pass. No runtime or merge. Its docs/P3_BOX_LIGHTS.md lists S6/S7/S5 gates; P1 tests retain priority.
- P4 OMM foliage: worktree-agent-a8b7cb486c1434ebd, its HANDOFF.md. Conservative design; unknown falls back to alpha testing.
- P5 SHaRC v2 after EN2, light tree, EN2 consumers in GI/SHaRC. sharc-v2 remains off.
- P6 minimum design DONE: docs/P6_WATER.md, merged 441904a through merge-green (build, 3 CTests, 32 shaders, QA pass), main/branch pushed. Water runtime and gap 1 remain open; phase branch codex/p6-water cd04c63.

## Test and reporting workflow

Queue: scripts/test-queue.ps1. Jobs: captures/queue/pending/*.json. Give jobs
both patterns and expect; check the complete kept log, not just summary lines.
Results: scripts/queue_results.py and captures/queue/logs. Do not keep results.md
open. Check actual processes, not PID files, for liveness. Verify PID start time
before treating any game as owned. No concurrent games or test runners.

While tests run or the user plays, do offline work, including the next phase.
Keep PROGRESS_CODEX in plain English with normal spacing: a few lines per result
and a ten-line summary at the top each evening. Keep this handoff near 150 lines.
Replies to the user remain concise Italian.

References: QUALITY.md, QA.md, TEST_SCENES.md, COVERAGE.md, DESIGN_SOTA_NEXT.md,
ROADMAP.md, USER_SETTINGS.md and the relevant branch HANDOFF.md files.
Modded Act 2/Rivington saves stay out of scope; do not reinstall their mods.
Backups: D:/BG3_Tools/snapshot-20261028 and snapshot-20261027/* tags, untouched.
