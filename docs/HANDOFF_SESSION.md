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

Main 6dba0a8 adds the corrected frozen local-light reference and merge checks
to the stable 6bd6967 rendering base. No P1 research feature is in main yet.
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
Older S3 excess areas: GI 118.672, local 55.052, sun 42.088, reflections 34.557
per mille. Latest production candidate 03679ed: GI 172.938 per mille, mean
0.001258 PQ against limit 0.000978; all RT 219.062, mean 0.002334 PQ.
The 356 s queue run is valid, with no faults/dumps/build changes. FAIL, no merge.
This is not a controlled same-build A/B against the old baseline; no reduction
or regression is established. Details and log numbers are in PROGRESS_CODEX.
QA coverage gap: --temporal-gi uses a fixed camera; same_view rejects movement.
Those numbers are a temporal anti-regression gate, not a direct pan result.
Also capture -Move off/on with observed camera motion before claiming P1 progress.

Already established in research:
- Offscreen/NaN/Inf owned-motion GPU tests fall back to fresh GI samples exactly.
- Full-resolution radiance/depth writer pairs match the exact bound game depth.
- Static/moving camera transport works; movement is observed in GPU camera position.
- Full additional native camera payload is still absent, so its verdict is inconclusive.
- An introduced private metadata write hazard was fixed by nonoverlapping ranges.
- Static/moving repeats on 929be7a pass their queue checks (116 / 126 s), with no faults and zero messages naming 3,255 / 3,345 logged private handles.

Fresh codex/p1-pan is in en2-selfcheck/BG3RTX, now c428cde, pushed. It excludes
unrelated FG work. Current-radiance candidate 3c7b411 adds default-off
gi_current_radiance and paired host/core ABI 14. Full build, 10 CTests, 39 shaders
and 14 ABI/SPIR-V checks pass; docs/CURRENT_RADIANCE.md defines its scope.
9c68f9e preserves seven ordinary GI/RT shader binaries byte-identical to bf170f8.
Initial on/off startup controls both stalled before gameplay (82 s, no faults
or dumps), so neither proves anything about GI quality. c428cde fixes a concrete
recursive tracker shared-lock at the native descriptor-bind callsite; off control
then passes in 85 s. Smoke-r2 proves GPU-valid pose, changed RGB and exact PRE
alpha in two captures; all nine writers complete, finite. One NV153 is unresolved.
A 79 s production repeat is valid with zero events, 1,200 accepted / zero rejected.
The first static off A/B has GI 144.405 per mille / 0.001571 PQ but INVALID queue
status from a misplaced stdout expectation. Repeat 226 checks the game-log marker,
job p1-current-radiance-ab-off-S3-r2 completed validly: GI 150.059 per mille,
mean 0.000988 PQ (limit 0.000978), one NV153 also with the candidate off.
User steering: moving-camera same-build off/on comes FIRST. QA --move now
forwards -Move, captures a constant configuration and requires --capture-only;
static analysis refuses moving captures. P1 HEAD 3ff190f (same c428cde DLL pair).
Jobs 227/228 both completed validly (142/80 s), three moving traces each, zero
events/dumps/build changes. Camera movement is observed, but trajectories are
unmatched, so quality comparison is inconclusive. a8c5db5 starts -Move only after
the baseline trace; 32 policy / six capture tests pass, renderer DLLs unchanged.
Synchronized jobs 229/230 both completed validly (82/84 s), zero events/dumps,
but albedo shifts remain below 0.1 preview pixel: both cameras stayed static.
Jobs 231–236 stayed static; ineffective SDL/Win32 focus changes were reverted.
Real-present jobs 237/238 moved only off, so that pair was also inconclusive.
P1 ac77150, pushed, now isolates -Move with a client-relative virtual pointer:
centre before baseline, right edge while held, coherent SDL events/polled data.
No physical cursor/focus changes. Ordinary play and non-Move runs are unchanged.
Jobs 240/241 use identical d431702 DLLs: valid 85/74 s, zero events/dumps/build
changes; both actually move in the last interval (83.38% / 79.68% albedo change).
Final off/on views differ (80.54% albedo change), so GI quality is INCONCLUSIVE.
Report: P1 build/moving-current-ab-r11.json. No live game/runner after job 241.
10 CTests, six capture/resolution tests and 32 log-policy checks pass. Moving QA
now defaults to headless=0, still hidden by the queue. Do not infer a headless bug
from the earlier confounded controls. Next: implement gi_history_surface_gate=0,
accept history only when both previous depth and world normal match at the
reprojected pixel. Gate try_prev_bounce_at before colour fetch (RR history=1 makes an
EMA-only gate ineffective); pair metadata with the actual radiance writer.
Keep this checkout's source and binaries frozen while its queued job is active.
Use existing B2/B5 camera data, derived footprint/precision bounds, conservative
footprint intersection and GPU-ordered availability, not a CPU epoch certificate.
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
- P6 water: at least a design covering COVERAGE gap 1, especially the docks.

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
