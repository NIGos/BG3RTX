# BG3RTX progress

## Working summary — 2026-10-28, 17:40 Europe/Rome

1. Main still has the stable rendering code at 6bd6967; no research feature has been merged.
2. P1 is open: the last production S3 baseline measured GI 118 per mille, local 55, sun 42 and reflections 35.
3. No measured visual reduction from the later GI corrections has been established yet.
4. GPU tests confirm fresh-sample fallback for offscreen, NaN and infinite owned motion inputs.
5. Paired radiance and full-resolution depth match the exact game depth in the captured writer executions.
6. Camera transport works in static and moving captures; additional native camera reads are still disabled.
7. An introduced diagnostic metadata write hazard was fixed: 2,159 / 2,155 messages became zero in the two repeats.
8. The latest repeats cover 3,255 / 3,345 logged private handles; this is scoped validation, not universal safety.
9. P2 through P6 remain pending; work now follows the phase order and short hypothesis / A-B / keep-or-reject loops.
10. The next P1 milestone is a measured GI comparison; diagnostic passes alone will not close the phase.

## Operating plan

Work until Saturday, 2026-10-31 evening. Use 20:00 Europe/Rome as the stop target;
finish with a summary here, push all resulting branches and main, then stop.
No user decision is currently needed. Never modify saves, rewrite history,
force-push, or move/delete any snapshot-20261027/* tag.

Before starting a runner or game, check both the USER_PLAYING marker and live
BG3 processes. An unowned game or the marker means offline work only. In-game
tests use the serial hidden queue and explicit expect patterns. Feature merges
use merge-green only after manually checking the deciding log numbers, followed
by pushes of main and the branch. A phase branch starts from current main and
imports useful old work selectively. New features remain off by default until
their quality gates pass.

## P1 — Pan instability

The last production S3 result predates the temporal sample-coordinate safety
correction. It measured temporal excess areas of GI 118.672, local 55.052,
sun 42.088 and reflections 34.557 per mille, with 23.0% common motion excluded.
Those numbers remain the baseline, not proof about the current candidate.
The next deciding experiment must measure the candidate with production
diagnostics off and isolate GI before expanding to the other effects.

Owned-motion tests reject offscreen and nonfinite coordinates on the GPU.
In four NaN/Inf captures, temporal output equals the fresh sample exactly and
all ordinary outputs remain finite. This establishes numeric fallback safety;
it does not establish a visible pan improvement or correct previous visibility.

Research camera runs on 2026-10-28 established binary transport and actual
camera movement. The additional B1/B2/B3/B4 camera reads remain off pending
guarded implementation. A wider validation check found an introduced overlap:
copy/fill of metadata followed by a write to its last 48 bytes. Separate write
ranges fixed it. Static/moving repeats on 929be7a are queue-valid (116 / 126 s),
with no device-loss events, dumps or build changes, and no validation messages
naming the 3,255 / 3,345 logged private handles. Raw camera transport, checkpoint
completeness and paired depth checks still pass. The complete camera verdict
remains inconclusive because the additional payload is deliberately absent.

This research is retained, not merged. The phase branch will exclude unrelated
FG/HDR/swapchain work. Source recording identity and actual GPU history epochs
are different; no history acceptance is certified from a present counter.

User check when a P1 candidate is ready: pan across static building and ground
edges in the usual save, comparing the candidate's GI key off/on. Look for light
or shadows sliding away from those fixed edges. Ctrl+Shift+F9 switches all RT
effects off/on as the reference. Do not treat wind-blown grass alone as a failure
or use all-RT-off as a claimed fix. No new user-facing candidate key is ready yet.

## Next phases

- P2: decide EN2 hit materials, D-EM emissive and lscheck-fix from the handoff jobs; lscheck needs hold 90.
- P3: finish box-light host integration and run S6 / S7 / S5 checks.
- P4: implement and validate the conservative OMM foliage design.
- P5: SHaRC v2 after EN2, then the light tree and EN2 consumers.
- P6: at least a water design covering the docks gap.

If P1 stops yielding measurable progress, record state, evidence and the next
idea in PAUSED.md on its branch, move to P2, and return later. Apply the same
rule to later phases. Do not substitute more instrumentation for deciding tests.

## Small fixes and gate gaps

- Resolution checks now use actual source and preview dimensions; changes make comparisons inconclusive and retain captures.
- The window hider now checks PID plus process start time and retains a process handle through UI use; 13 policy checks pass on PowerShell 5 and 7.
- The old merge-green script delegates to build.ps1 -Test, which runs only settings_test. Its all-tests gate must be corrected before feature merges.

Research evidence remains in the managed pan-motion-vectors worktree and main's
ignored queue logs. Snapshot tags and backups are unchanged.
