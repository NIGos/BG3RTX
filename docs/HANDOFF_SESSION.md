# Session handoff - 2026-11-14

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first (top: 2026-11-14 performance investigation, handout, launcher). Design and history of
the hair work: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) sections 5v-6f. ABI 31. Main is 4e5e435d plus the docs commit that follows it.

## State

- Lives: the user allowed them for the performance investigation on the morning of 2026-11-14, then took the PC back about 14:10.
  Offline only until a fresh explicit go. Flag: `.scratch/coord/PAUSE`. Pending live evidence is READY_FOR_LIVE, not a pass.
  After a go, tell the user and wait for their ok right before launching the runner.
- Installed test app: 0.9.6-hairtest12 (commit 4e5e435d) in `%LOCALAPPDATA%\Programs\BG3RTX`. Setups of hairtest6-12 are in `.scratch/setup-hairtest*`.
  The user's settings file is untouched.
- Releases 0.9.4 and 0.9.5 are packaged. The release profile below is unchanged.
- Windows Smart App Control turned itself on at 14:06 on 2026-11-14 and blocked fresh test exes and a setup. The user turned it off at 14:18.
  Results from that window were discarded. If fresh exes are blocked again, check it first.

## Performance investigation (2026-11-14)

- The user reported very low fps (Ultra, FG 3x, RTX Hair On). The release profile today vs 2026-11-05 is not slower (table in PROGRESS):
  GI much cheaper, scene build +0.4-0.7 ms in the opt-profile run, wide split 0 vs 1 differs by only 0.05-0.1 ms.
- RTX Hair On costs about +1.4 ms frame time at the inn (28.9 -> 30.3 ms, focused, FG 3x). Base CPU hooks 12.13 vs 12.39 ms: no regression.
- Most likely cause of "very low fps": the VRAM guard latched LOW after the OS halved the budget (log of 2026-11-13). Fixed on
  `claude/vram-guard-latch`, unconfirmed in play.
- Hair-on per-draw global mutex removed (`DrawFilter`, branch claude/cpu-hooks). Offline model only; no live number yet.
- Not done: the S8 perf-ab job timed out on pipeline readiness; the camp save "Campsite - 27h" no longer exists; DLSS 5 cost not measured.
  Staged: `tools/live_batches_staged/perf_ab/`.
- Queue runner: logs older than 3 days were pruned and the preflight then refused every job. Fixed (keep the newest 3 VALID logs per save,
  frozen snapshot fallback). The hair live logs of 2026-11-10/11 are gone; the numbers are in the docs.

## Scene handout (complete, off by default)

- Launcher: Experimental page, "Low-latency scene". Keys `rt_scene_handout_layout` (restart) and `rt_scene_handout` 0/1/2.
- Only evidence: S8 2026-11-09, scene wait 4.4-5.1 -> 0.5-0.7 ms, frame about -0.5 ms. The inn (wait 8-9 ms) is unmeasured.
- Staged qualification batch: `tools/live_batches_staged/handout_qual/` (READY_FOR_LIVE, predicted.json first).
- Other unshipped optimisations worth a live: `rt_mesh_budget_own` (rv3 rerun), `rt_as_cap_occupied` (rv5), camera BLAS compaction for pixel LOD.
  Rejected ones stay rejected.

## Launcher

- UI reworked to "Direction A" with instant apply (no Save or review; dependency notice with Undo; restart indicator), merged. Design docs: `docs/ui_rework/`.
- Rendered only in headless Edge, not yet checked in the real WebView2 window. The native fallback UI is unchanged (still has Save).
- Detail-panel text for some settings is still long and technical.

## RTX Hair strands

Hair holes during fast camera moves: unchanged since 2026-11-11 (partial fix `rt_hair_header_cam_gpu`; the `rt_hair_dense_sample` job is READY_FOR_LIVE).
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

- Left, not reviewed (under `.claude/worktrees/`): 10 worktrees with uncommitted work or commits not on main:
  agent-a2c38a4884e982692 (anim-materials), agent-a507a735412f7f20f (hair-m5-m2), agent-a7c43f1e79329fb0f (hair-footprint),
  agent-a84a839fb14568d30 (hair-m1-rerun), agent-ae065cb9955e96e59 (hair-m4-locals), codex-hair-lod1, fix-rrpost-src (water-glint),
  hair-edge-still, hair-pixel-lod-study, hair-reload-fix (release-licences). `test-main` is used by merge-green; keep.
  Remove only with `scripts/remove-worktree.ps1 -Name <folder>`.
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

## Open for the user

1. Confirm in play that performance is back with hairtest12 (VRAM guard fix).
2. A go for the handout qualification live (`handout_qual`), then the perf-ab and hair jobs.
3. RTX Hair default (still off in every preset; hidden in release).
4. The 10 old worktrees above: keep, merge or remove.
