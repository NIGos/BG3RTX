# Session handoff - 2026-11-04 evening

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first: its top section is the release report.
Then [backlog](BACKLOG_20261104.md) and [RC_CHECK](release/RC_CHECK.md). ABI 26.

## State

- Release backlog closed: every row has a final outcome with numbers.
- Release candidate rc-20261104-ccbbb1d2-0efe5f84 verified (PASS). The only open item is the user's approval.
  No upload without it. The archive lives in `.scratch/rc/` and never goes in Git.
- If main changes code or presets before approval, rerun
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`
  and one Ultra release smoke (release_qual rel1). Tools/docs-only merges need neither.

## Release profile (do not change without a user decision)

- Stock visible hair. Ultra: temporal post, exact-k, two GI rays. High: OMM and masks.
- Always on: wf9 water camera correction, far-GI roof-leak fix.
- Native ambient, native S8 water edge and glint, hit emission mode 1.
- Fresnel Experimental and off. Tilesets hidden and off. One-ray GI rejected for Ultra.
- Ctrl+Shift+F9 master RT toggle; F10 flips the last Experimental choice.
- GPL-3.0-or-later plus NVIDIA exception. "Tested only on RTX 5090".

## Post-release queue

1. RTX Hair strands (session "RTX Hair groom", branch claude/hair-consolidate): offline consolidation
   first (orphan counter, 9 review fixes, unit tests, shader identity, failure-mode table in
   docs/RTX_HAIR_PRIMARY.md section 9). No hair live until the coordinator approves one READY_FOR_LIVE job.
2. Native Tilesets: diag window must wait for the rt_hit_material pipeline build; explain the ON row
   rejects (+6.3 M) before any coverage claim (TRACK_D_SCENE_BUILD, outcome section).
3. Water off-screen hit shading: compose light binding 21-22 mismatch (TRACK water docs).
4. Deferred rows in the backlog (FG letterbox, opaque hidden-hit reflections, directional sky).

## Rules that bit today

- Check blobs > 20 MB before every commit and merge; corpora are in .gitignore.
- Jobs that test hidden or experimental keys need dev=1 (release policy resets them otherwise).
- Live windows in frames with a stats reset at motion-ready; one launch per scene; merge-green scoped
  by changed paths (docs, tooling, shaders, full).
- A check for a running merge-green must not match its own command line.
- Follow [AGENTS.md](../AGENTS.md): never saves, backups in D:\BG3_Tools, force-push or snapshot tags;
  remove worktrees only with scripts/remove-worktree.ps1; launch the game only via scripts/test-queue.ps1.
