# Session handoff - 2026-11-21 (candidate tested by the user, docs updated)

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first (top: the user's decisions of 2026-11-21).
Release check: [RC_CHECK](release/RC_CHECK.md). Design and history: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 6g-6k,
[SCENE_HANDOUT_DESIGN](SCENE_HANDOUT_DESIGN.md) section 8. ABI 31 (unchanged overnight).
Main is b7532c90 plus the docs commit that follows it.

## State

- Nexus upload set (user action, same layout as 0.9.5): `.scratch/release/v0.9.6-beta/` with
  `BG3RTX-0.9.6-beta-Portable.zip` (a8556bc3...), `BG3RTX-0.9.6-beta-Setup.zip` (f16817b8..., Setup.exe plus
  "README - Setup.txt"), `SHA256SUMS.txt`, `nexus/` texts and images, `PUBLISH.md`. The GitHub repo is public (source).
- Release candidate 0.9.6-beta, rebuilt from main b7532c90 (lean layer, 230 files; updated player docs; the smoke-tested release code plus the diagnostic emission pack writer, off, and the launcher UX pass):
  - `dist/BG3RTX-0.9.6-beta.zip`: SHA-256 a8556bc33ec18c837c2f3fc36e3be68417589d96a8f37251411c3228728f7f3f
  - `dist/BG3RTX-0.9.6-beta-symbols.zip` (private): 0b12b3bb63dc54b6d8ac31ced5810430e072d3cd8ca5d30bb97ce041eb9b684b
  - `.scratch/setup-0.9.6-beta/BG3RTX-0.9.6-beta-Setup.exe` (unsigned): 84d8706c39b778dea4c450ba2143aac22add7c1799d23465cb002bacf9628b58
  - `VERSION.txt` says commit b7532c90. Release folder ready to publish: `.scratch/release/v0.9.6-beta/` (see its PUBLISH.md). `.scratch/setup-0.9.6-beta/SHA256SUMS.txt` lists these three hashes.
  - verify_rc: 11 PASS, Setup SKIPPED (the user's own install is present). The user installed the 9ae2ede3 Setup.exe
    over hairtest18 on 2026-11-21 and found everything fine: Setup install tested by the user.
  - Previous candidates in `.scratch/old_rc_122493b1/`, `.scratch/old_rc_3ed4ec08/` and `.scratch/old_rc_08cb1e99/`; older in `.scratch/old_rc_745a1fff/`.
  - The user tested this candidate on 2026-11-21: everything OK.
  - claude/docs-nexus changes the shipped `README.md` (source link, Nexus-only downloads) after this build:
    repackage and rerun verify_rc before publishing so the archive carries it.
  - Nothing is published, tagged or uploaded. Publishing is the user's action.
- Installed app: 0.9.6-hairtest18 (commit 1c09e039), unchanged; it lacks the 2026-11-20 and 2026-11-21 changes.
- Lives: allowed overnight by the user's /goal only. A new session must ask again; after a go, tell the user and wait
  for their ok right before launching the runner. Nothing is queued. No merge is in progress.
- `test-main` is at 41a5e7f8 (merge-green resets it).

## Release blockers

1. None left from tests: the final release smoke passes with the release profile of 2026-11-21 (Dialogue1 15/15,
   S8 Ultra + High 29/29 window gates; valid runs final-s8-ultra-high 062059 and final-stock-hair-dialogue1 062427 with the post-analysis).
2. Done: the user confirmed the overnight changes, kept OMM on in High and `gi_far_roof_emission` off (2026-11-21).
3. The installer has never been executed on this PC: install it once over hairtest18, or ship the ZIP only.
4. Repackage with the updated player docs (claude/docs-096), rerun verify_rc.

## What the overnight run changed (details in PROGRESS and its 2026-11-21 archive)

Release profile, confirmed by the user 2026-11-21: `rt_hold_pack_base`, `rt_hair_header_early`, `rt_as_size_class`,
`rt_cascade_keep_refused` on by default; `rt_hair_groom_async` in the RTX Hair On bundle (27 keys).
Fixes: OMM back in High and Ultra (dead since 2026-11-14), water one-frame no-scene flips, groom BLAS overflow into
compacted storage, groom decode scheduling, smoke tooling (derived gates, stats resets, readiness).
Kept off: `gi_far_roof_emission` (PT CLOSER, but +0.7-1 ms GI/RT in Dialogue1; user 2026-11-21), `rt_sun_overlap`, `rr_post_jitter_align`,
`rt_mesh_pool_trim_move` (READY_FOR_LIVE, hair only).

## Known, not fixed

- High OMM downgrades about 1 per 60 frames (no pass waits for the commit without hit materials); correct image. A fix
  changes the handout protocol.
- RTX Hair: a head decoded for the first time shows cards for about 19 frames (slot key churn of heads that appear
  together, not the decode queue).
- After Ctrl+Shift+F9 ON, GI starts one non-emission raygen build that is superseded 34 ms later (12 s of wasted driver
  work in the background, no visible effect).
- Water game shadow NaN at Vg = 0 (`common/water_fs_patch.h:699`): the pixel keeps the game colour. No small fix.
- RR post history rejected in motion (`rr_post_temporal.hlsl:51`, energy-safe; needs a per-frame matrix).
- GI ray cone in RR mode one mip too coarse (`gi_trace_rt.hlsl:141, 313`): about 0 energy effect, skipped.
- The pixel-LOD camera BLAS is not compacted (about 16 MB per groom; log the compaction ratio first).

## RTX Hair strands

- Off in every preset. In release it is an opt-in Beta control (Lighting > RTX Hair, Ctrl+Shift+F7). On keeps all 27
  bundle keys (26 plus `rt_hair_groom_async`, confirmed 2026-11-21) under the release policy. The user decides whether it goes On in a preset.
- Instability under camera motion is solved for converted styles (hx1 202905: 0 strands->cards).
- Open: styles without a groom file stay cards. Inn dolly hitches (100-220 ms groom decodes) fixed by `rt_hair_groom_async` (live 2026-11-21).
- Launcher controls: "RTX Hair detail at distance" (Off / 1/4 / 1/8, live) and "RTX Hair motion stability" (On in
  High and Ultra, restart). Staged hair jobs: `tools/live_batches_staged/README.md` (hx1 in `hair_stable/`).

## Release profile (do not change without a user decision)

- Stock visible hair. Ultra: temporal post, exact-k, two GI rays. High: OMM and offline masks (OMM working again since
  2026-11-21; High pays about 0.4 ms for it; kept on by the user 2026-11-21).
- Always on: wf9 water camera correction, far-GI roof-leak fix. Low-latency scene On in High and Ultra, with the hit wait.
- The four VRAM right-sizing policies are On by default. Since 2026-11-21 (confirmed by the user): hold pack
  base, strand header early, BLAS size classes, cascade keep refused.
- Experimental page (behind "Show experimental options", all off): Water surface reflections, two Frame Generation
  pacing tests, Strand hair edge resolve. RTX Hair: Beta control on Lighting, off. Tilesets hidden and off.
- 23 visible controls, 32 developer knobs hidden. Without a user file the package resolves to High without OMM and masks.
- Ctrl+Shift+F9 master RT toggle. GPL-3.0-or-later plus NVIDIA exception. "Tested only on RTX 5090".
- Asset database ships in the archive (user decision). Nothing is uploaded by agents.

## Next work

1. User action: publish (`.scratch/release/v0.9.6-beta/PUBLISH.md`). Setup tested and docs repackaged on 2026-11-21.
2. Read the next user log: OMM accelerated commits, `[mem] ours`, BLAS refusals, groom "decoded ..., worker" lines.
3. `rt_mesh_pool_trim_move` (READY_FOR_LIVE, hx1 hair job with the key on).
4. The known items above, cheapest first; emission replay packs would make `gi_far_roof_emission` testable.
5. RTX Hair: unconverted styles, head slot-key churn on appearance, then the preset decision.
6. Deferred: L11, L12, L17, C4/C7/C8/C12, S-F05, T2; SHaRC removal; native Tilesets; water off-screen shading.

## Gates

- Sensible gates: `AGENTS.md` and `tools/live_batches/sane_gates.py`. FAIL only beyond the OFF noise plus a floor.
- An unpredicted drop is EXPLAIN, not a pass. Short windows are "not judged" instead of making the run INVALID.

## Housekeeping

- Worktrees with unmerged commits under `.claude/worktrees/` and 24 unmerged remote branches: still to review with the user.
- Unregistered folders `agent-a0dba2e726bdabdad` and `ui-rework`: check for junctions before touching them.
- `test-main` is used by merge-green; keep. Remove worktrees only with `scripts/remove-worktree.ps1 -Name <folder>`.
- E: is a mounted disk: read free space with `Get-Volume -FilePath`, not drive E:.

## Rules that bit recently

- A cost that falls without a predicted reason is not a gain. Check energy and counts.
- Frame time measured while the user uses the PC is not evidence.
- A job `post` must not rely on `python` resolving (the Store alias made vr-s8 and vr-inn INVALID; the runner uses `py -3`).
- Give a job's any-line `expect` only lines that its first state prints; per-state lines go in the per-window gates.
- `reg.exe` in the Bash sandbox and PowerShell read different Apps versions (hairtest10 against hairtest18): read the install's `VERSION.txt`.
- A spirv-opt crash prints nothing: the equivalence gate now names it. Do not rerun an exhaustive cross blindly.
- Check blobs over 20 MB before every commit and merge. Generated files stay in `.scratch/`.
- Never run builds or merge-green while the user plays (`captures/queue/USER_PLAYING` or bg3.exe running).
- Follow [AGENTS.md](../AGENTS.md): never saves, backups in D:\BG3_Tools, force-push or snapshot tags;
  remove worktrees only with scripts/remove-worktree.ps1; launch the game only via scripts/test-queue.ps1.

## Open for the user

1. Answered 2026-11-21: overnight changes confirmed, OMM on in High, `gi_far_roof_emission` off, final test OK.
2. Done: claude/docs-096 merged and repackaged (b7532c90). claude/docs-nexus adds the source link to the shipped
   README and release notes: repackage and rerun verify_rc once more after it merges.
3. Setup.exe: done (the user installed and tested it on 2026-11-21).
4. Publish 0.9.6-beta (user action): upload to Nexus Mods only (Files tab). No GitHub release; the repository
   https://github.com/NIGos/BG3RTX is public as the source (user decision 2026-11-21).
5. Decide whether RTX Hair goes On in a preset. Unconverted styles: convert them or accept cards.
6. The worktrees, folders and remote branches under review: keep, merge or remove.
