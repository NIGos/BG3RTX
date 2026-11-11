# Session handoff - 2026-11-11

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first (top: newest RTX Hair work, user decisions). Design and history of
the hair work: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) sections 5v-6f. ABI 31. Main is 9b83a578 plus this docs commit.

## State

- Live tests are PAUSED by the user (2026-11-11 about 00:50, "non avviare altre live"). Flag: `.scratch/coord/PAUSE`.
  No live without a fresh explicit go. Offline work goes on. Pending live evidence is READY_FOR_LIVE, not a pass.
- Installed test app: 0.9.6-hairtest8 (commit cb8b2360) in `%LOCALAPPDATA%\Programs\BG3RTX`. The user's settings file is untouched.
- Releases 0.9.4 and 0.9.5 are packaged (see PROGRESS older entries). The release profile below is unchanged.

## RTX Hair strands

Still off in every preset. In release the option is hidden (developer). New launcher controls on the Lighting page:
- "RTX Hair detail at distance": Off / 1/4 / 1/8. Keys `rt_hair_pixel_lod` and `rt_hair_pixel_lod_small_keep`. Presets Off. Applies live.
- "RTX Hair motion stability": `rt_hair_mv_card=2`. On in High and Ultra (user decision 2026-11-11), Off in Low and Medium.
  The release.ini pin is removed. Turning it on needs a game restart.
- Gate numbers: hm1 210705 camera-only share 11% (off 98%), ratio 0.77. hf2 000619 pan 3.2%, ratio 1.00. Strand G-buffer 0.22-0.31 ms.

Distant detail (pixel LOD):
- Heads 35-60 px get keep 1/4 (1/8 optional, about +2% outline). Heads under 35 px are unchanged.
- Strand G-buffer -27% (1/4) and -34% (1/8) in pl1 211046.
- Camera BLAS is about 16 MB per groom at 1/4 and is not compacted. Compaction is a possible saving (layer change, needs a live).
- Image sharpness gain is not resolved by the shots: poses change between shots.
- Above 60 px the k rule over-covers about +15% (open).

Hair holes during a fast camera dolly:
- Cause 1 found and fixed behind `rt_hair_header_cam_gpu=1`: the header camera is copied on the GPU.
  Coverage 2-7% -> 50-70% in hl1 000345. A loss of 30-50% remains (open).
- Disproved: another build's TLAS, row order (`rt_hair_row_stable`), full BLAS rebuilds, mesh pool reuse, VP eye,
  stale publish (k=1 is normal; `rt_hair_stale_publish` cannot act, retire it), newer build (build stamp hl1 003637:
  same build), BLAS refit, game LOD switch, VP mismatch in the stated form.
- Next step ready: hl1 with `rt_hair_dense_sample=1` (spec 1052) in `tools/live_batches_staged/hair_late/`.
  READY_FOR_LIVE. predicted.json lists the signatures: each one points to a fix. The hair G-buffer shader is not
  byte-identical with the key off (a spec-constant branch is present); merge-green shader gates passed.
- Final qualification job `tools/live_batches_staged/hair_final/` (hf2, hole gate report-only) exists.
  It does not yet set `rt_hair_header_cam_gpu`: add it before the run.

Other problems reported by the investigation (6c/6d), none fixed:
- Misleading counters: "own build frame's publish" and "header/BLAS mismatch" are always 0 with `rt_hair_groom_primary_current=1`.
- Rejected-after-hit is counted frame-wide.
- The same CPU camera feeds the cascade caster rebase and the groom LOD bounds.
- `python -m tools.live_batches.lint_snapshot` shrinks the checked-in snapshot; it needs a merge mode.
- Possible full build on compacted BLAS storage at the `layer/bvhsplit.cpp` refit fallback.

## Worktrees and untracked files

- 34 clean merged worktrees were removed on 2026-11-11 with `scripts/remove-worktree.ps1` (it now finds the repo from its own folder).
- Left, not reviewed (under `.claude/worktrees/`): dirty = uncommitted files, ahead = commits not on main.
  - agent-a2c38a4884e982692: branch anim-materials (dirty, 2 ahead)
  - agent-a507a735412f7f20f: claude/hair-m5-m2 (dirty)
  - agent-a7c43f1e79329fb0f: claude/hair-footprint (1 ahead)
  - agent-a84a839fb14568d30: claude/hair-m1-rerun (dirty)
  - agent-ae065cb9955e96e59: claude/hair-m4-locals (dirty)
  - codex-hair-lod1: codex/hair-groom-lod1 (7 dirty files)
  - fix-rrpost-src: water-glint (dirty, 1 ahead)
  - hair-edge-still: claude/hair-edge-still (2 ahead)
  - hair-pixel-lod-study: claude/hair-pixel-lod-study (1 ahead)
  - hair-reload-fix: release-licences (dirty)
  - hair-holes-root: claude/hair-holes-root (clean, merged; fine to remove)
  - test-main: detached at main, used by merge-green; keep.
- Untracked in the main checkout, left alone: `bg3rtx_ui.ini`, `tools/live_batches_staged/dlss5/`, `tools/live_batches_staged/reshade_compat/`.

## Release profile (do not change without a user decision)

- Stock visible hair. Ultra: temporal post, exact-k, two GI rays. High: OMM and offline masks.
- Always on: wf9 water camera correction, far-GI roof-leak fix.
- Experimental: only Water surface reflections (off). Strand hair hidden. Tilesets hidden and off.
- Ctrl+Shift+F9 master RT toggle. GPL-3.0-or-later plus NVIDIA exception. "Tested only on RTX 5090".
- Asset database ships in the archive (user decision). Nothing is uploaded by agents.

## Post-release queue

1. RTX Hair strands: close the dolly holes (dense sampler live, then the fix), then the hf2 qualification, then decide the presets.
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
