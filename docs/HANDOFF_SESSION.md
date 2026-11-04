# Session handoff - 2026-11-05

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first: its top section is the consolidation report and the one
open question (go for the final qualification live). Then [CONSOLIDATION_20261105](CONSOLIDATION_20261105.md)
and [RC_CHECK](release/RC_CHECK.md). ABI 26.

## State

- Consolidation done offline and merged: reviews, fixes, cleanup, UI/UX, installer, tooling. Every table row closed.
- Release candidate verified PASS 12/12 (ZIP and Inno setup, 219 files).
- Final qualification run 2026-11-04 23:24-23:38: stock hair VALID, Ultra S8 gates all pass twice, High part blocked by job readiness defects (post-release fix). See PROGRESS top.
  Queens House, stock hair in Dialogue1). Lives are paused: queue it only after the user's explicit go.
- After the live: analyse `<log>.final.json` against `predicted_final.json`, write the outcome here and in
  PROGRESS, then the user approves the release. Nothing is uploaded by agents.

## Release profile (do not change without a user decision)

- Stock visible hair. Ultra: temporal post, exact-k, two GI rays. High: OMM and offline masks.
- Always on: wf9 water camera correction, far-GI roof-leak fix.
- Experimental: only Water surface reflections (off). Strand hair hidden. Tilesets hidden and off.
- Ctrl+Shift+F9 master RT toggle. GPL-3.0-or-later plus NVIDIA exception. "Tested only on RTX 5090".
- Asset database ships in the archive (user decision).

## Post-release queue

1. RTX Hair strands: consolidated offline on main; hv9 READY_FOR_LIVE in its session; no live without approval.
2. Deferred findings: L11, L12, L17 (submission-dependency tracking), C4/C7/C8/C12 (core completion contract),
   S-F05 (R11G11B10 emulation), T2 (queue cancellation of descendants).
3. SHaRC removal after separating its device capability (layer/rtscene.cpp, water.cpp, hair_primary.cpp).
4. Native Tilesets, opaque hidden-hit reflections, water off-screen shading: see the backlog and TRACK docs.

## Rules that bit recently

- Check blobs > 20 MB before every commit and merge. Generated files stay in `.scratch/`.
- Removing a spec-constant branch changes SPIR-V bytes: prove the release path with
  `tools/release_shader_equivalence.py`, not raw identity.
- merge-green lints only active or new live jobs; historical jobs are evidence.
- Never run builds or merge-green while the user plays (`captures/queue/USER_PLAYING` or bg3.exe running).
- Follow [AGENTS.md](../AGENTS.md): never saves, backups in D:\BG3_Tools, force-push or snapshot tags;
  remove worktrees only with scripts/remove-worktree.ps1; launch the game only via scripts/test-queue.ps1.
