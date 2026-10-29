# Session handoff — 2026-10-29

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

User explicitly resumed on Oct29. P1 energy preservation has first priority.
One usage reset is authorized if needed at the limit; not used yet.
Put future questions atop PROGRESS_CODEX and continue conservative/offline work.

## Main and research state

Main 441904a includes the P6 water design; rendering remains 6dba0a8 (corrected
local-light reference/merge checks). No P1 research feature is in main yet.
Remote: origin, private NIGos/BG3RTX. Preserve all snapshot tags and backups.
Old pre-rewrite SHAs can be translated with
D:/BG3_Tools/git_backup/commit-map-old-to-new.txt; never rewrite that history again.

Old research is preserved at pan-motion-vectors/BG3RTX 929be7a; camera-native
53f0925 is in motion-jitter/BG3RTX. Import only needed work, no unrelated FG.

## P1 — Pan instability

User report: RT lighting/shadows seem to detach from objects during pans;
Ctrl+Shift+F9 (all RT off) restores the normal appearance.
Old static S3 candidate 03679ed fails: GI 172.938 per mille, mean 0.001258 PQ
against 0.000978. Details in PROGRESS_CODEX. Those fixed-camera scores and
independent moving trajectories cannot establish a P1 improvement.

Old research proves owned-motion nonfinite fallback, paired depth/radiance and
observed camera motion. A private metadata overlap hazard was fixed. It does
not prove pan quality or the unavailable additional native camera payload.

Fresh codex/p1-pan is in en2-selfcheck/BG3RTX, now d53f3e1, pushed. It excludes
unrelated FG work. Old current-radiance replay is default-off, paired ABI14;
scope/evidence: phase docs/CURRENT_RADIANCE.md. No pan-quality win established.
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
242/243: eleven GPU cases pass, saved alpha matches, aggregate AO counters differ;
native geometry repeat confirms it. Zero driver events/dumps; do not block the pan.
gi_pan_metric=0 now measures RR GI against paired previous GI with native B2/B5
camera reprojection, round-trip validation, static-compatible depth/normal/MV
filtering and a rotating 4x4 diagnostic grid. GPU buckets separate off/on even
across a reporting window. Spec: phase docs/GI_HISTORY_SURFACE_GATE.md.
First pan 245 and production 247 lower residual but lose sample GI energy with
tiny coverage; no quality win. Phase docs/evidence preserves every deciding log.
246 invalid: >2000-draw fixture blocked pan in lighter gameplay. b5e0e93 now
uses post-load stability; 36 helper checks pass.
409ffe1 validates only the actual integer-loaded colour texel, removing four
unused neighbours. Bounds unchanged; 6 surface shaders/20 ABI checks pass and
7 ordinary shaders byte-identical. 248 valid: 403 s total/57 s moving session,
zero faults; off/on residual 0.151384/0.064525, GI Y 0.321177/0.008690,
coverage 13.3877%/7.1512%. Unequal cohorts/energy: still no quality win.
Same-frame legacy/gated raw pairs in 242/243 confirm the OLD five-texel gate
removes 71.8–99.9% of bounce energy; alpha identical in all four captures.
249 VALID, 217 s, zero faults/dumps/build changes. Point-footprint paired raw
bounce Y falls 70.66%/87.96%; alpha identical over 2,731,520 px per pair, finite,
pose valid, eleven GPU predicate cases pass. Normal/depth reject 52.35%/22.46%
of reported queries (startup included). No P1 quality win or merge; all keys off.
User steering: rejected history must use a fresh sample of the same expected
value; key-on static mean must match key-off within measured noise. P1 resumed.
d53f3e1 adds default-off gi_history_fresh: POST frozen-pose replay keeps original
complete history or uses fresh native diffuse PLUS immutable PRE GI at the source
on rejection, with material/strength/skin conventions and unchanged ray weights.
PRE AO is preserved. RR full-res/diffuse-only. Metadata stride1/2; token consumed
after POST readers; no new transport. 11 CTests, 49 shaders, 24 ABI checks,
5 energy regressions and 36 helper checks pass; ordinary shaders unchanged.
251 direct-only smoke valid113s, finite/pose/alpha pass, zero faults. Unlimited
VVL has existing renderpass/NGX categories, no dispatch/private-handle hazard;
scoped evidence only. 252/253 camera moved: INVALID static QA. SDL cursor event
initialization fixed this; 254 static shift -0.04/0.0200 render px in99s.
255 valid200s, twelve stationary captures, max shift0.16px, alpha exact, no faults.
Direct-only internal parity -0.807%, CI [-0.000601951,+0.000329959]Y. PARTIAL:
both modes changed integrand, so it does NOT prove original estimator parity.
256 LIVE on frozen d53f3e1 complete source. Require BOTH off/on noise parity AND
same-frame original PRE parity; only then ONE balanced moving toggle run.
Spec: phase docs/GI_FRESH_FALLBACK.md. Never report moving captures as static pass.
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

- P3 codex/p3-box 8d40fdc (p2-pending/BG3RTX), default off. 250 INVALID: 21 windows/6.862B pixels, 12,304 any-bit differences, zero >1e-5/one-writer differences, no driver faults. ls_stats=1 selects full variant, unlike earlier golden control. No activation-on run or merge; defer to P1.
- P4 codex/p4-omm 8d902d5, managed p4-omm/BG3RTX: conservative interval classifier and corrected stochastic-alpha contract, 197,621 checks/4 CTests pass. No runtime integration or merge. Old mip0/1-only plan is unsafe; unknown retains native alpha.
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
