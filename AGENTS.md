# AGENTS.md: rules for any agent working on BG3RTX

Start each session with `docs/HANDOFF_SESSION.md`, then the top of `docs/PROGRESS_CODEX.md`.
These rules come from mistakes already made in this repo. They apply on every branch.

## Never
- Touch the user's game saves.
- Force-push, rewrite history, or move/delete the `snapshot-20261027/*` tags.
- Launch the game outside `scripts/test-queue.ps1`, or start the runner while the user plays
  (a bg3.exe the runner did not launch, or `captures/queue/USER_PLAYING` exists).
- Hold `captures/queue/results.md` open (`tail -f` included): it stopped the runner once.
- Archive, delete or clean a checkout that has NTFS junctions to shared folders
  (`external/`, `data/assetdb`). Cleanup followed them and emptied the SDKs once.
  Remove the junctions first (`cmd /c rmdir <junction>`, never a recursive delete), then check
  the targets still exist.
- Write to the backups in `D:\BG3_Tools` (read-only for recovery).

## Fix first, measure second
- The goal is a visible, physically correct improvement in game (QUALITY R0). Instrumentation is
  a means, never the deliverable. If the next step is a known fix, do the fix; don't park it.
- One hypothesis -> read the code -> the smallest change behind an ini key (off by default) ->
  A/B -> keep or revert. Before writing a new tool, check whether existing logs/QA answer it.
- Don't open new research threads (attribution, exposure, provenance, FG, HDR UI, swapchain)
  while a phase has a concrete pending fix.
- A failed or confounded measurement: note it in one line and fix the test, not the theory.

## Measuring correctly
- Compare A/B within ONE run (toggle the key during the same moving session), never images
  across runs: two pans never match.
- Energy: a quality change must not change mean brightness. Rejected temporal history falls
  back to the fresh current sample (same expected value, noisier, never darker). Check that
  key-on mean equals key-off within noise on a static camera before trusting any residual.
- A lower residual with lower energy is not an improvement.
- Every threshold is derived (encoding precision, footprint, noise of key-off repeats), never tuned.
- Background stats lines that need 120 frames need `"hold": 90` or more in the job.
- Give every queue job `patterns` and `expect`; a run without its expect lines is INVALID.

## Merging
- Only `scripts/merge-green.ps1`, after reading the deciding log numbers yourself.
- Features stay off by default until their gates pass. Push main and the branch after a merge.

## Writing
- Plain English with normal spacing: always a space between words and numbers ("weekly 100%",
  not "weekly100%"). Short sentences. Keep handoff and progress files concise (handoff ~150 lines).
- Progress entries: what changed, the key numbers before/after, kept/rejected/paused. No log dumps.
- If a user decision is needed, put the question at the top of `docs/PROGRESS_CODEX.md`, take the
  conservative option or do other work, and keep going.
