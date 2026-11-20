# Session handoff - 2026-11-20

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first (top: the release candidate and the two consolidation rounds of 2026-11-20).
Release check: [RC_CHECK](release/RC_CHECK.md). Design and history: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 6g-6k,
[SCENE_HANDOUT_DESIGN](SCENE_HANDOUT_DESIGN.md) section 8. ABI 31.
Main is 08cb1e99 plus the docs commit that follows it.

## State

- Release candidate 0.9.6-beta, rebuilt from main 08cb1e99 (lean layer, 230 files):
  - `dist/BG3RTX-0.9.6-beta.zip`: SHA-256 a702c117bcbbfe3152554ba440f2543d992476103aa04a8c09661b5aee8bb9eb
  - `dist/BG3RTX-0.9.6-beta-symbols.zip` (private): 5fc43e9415d03d607c45e7f27d6780c8a5810f532e9605d9f21568a55881f135
  - `.scratch/setup-0.9.6-beta/BG3RTX-0.9.6-beta-Setup.exe` (unsigned): 5e33db7aa74d6e29d24986b7c880a1891d31c1cef09834a58fc3f3926b6015b3
  - `VERSION.txt` says commit 08cb1e99. `.scratch/setup-0.9.6-beta/SHA256SUMS.txt` lists these three hashes.
  - verify_rc: 11 PASS, Setup SKIPPED (another install present: 0.9.6-hairtest18).
  - Older candidates are in `.scratch/old_rc_745a1fff/`.
  - Nothing is published, tagged or uploaded. Publishing is the user's action.
- Installed app: 0.9.6-hairtest18 (commit 1c09e039) in `%LOCALAPPDATA%\Programs\BG3RTX`. The user played it on
  2026-11-20 and found everything fine. It lacks the 2026-11-20 changes.
- Lives: paused by the user on 2026-11-20 ("Non ora"). A new session must ask again. After a go, tell the user and
  wait for their ok right before launching the runner. Pending live evidence is READY_FOR_LIVE, not a pass.
- Nothing is queued. No merge is in progress.

## Release blockers

1. The final release-profile smoke has never run: `tools/live_batches_staged/release_final/`
   (`final-s8-ultra-high`, about 3.8 minutes, and `final-stock-hair-dialogue1`, about 3.4 minutes; `predicted.json`
   committed). It is the only release blocker. It needs a user go.
2. The installer has never been executed on this PC. Either the user installs it over hairtest18 once, or only the ZIP
   ships. The ZIP install, update and uninstall rows pass.

## What the two rounds of 2026-11-20 changed

Shipped fixes (on, no key):
- Groom decode: a refused pool growth no longer repeats a 30-190 ms decode (two LODs were decoded 9 times).
- Tracker: descriptor maps are erased on free, reset and pool destroy. Destroyed buffer and image records are erased
  before the driver's destroy, so a reused handle on another thread keeps its record.
- `refresh()` reads its cfg keys once per frame (`core/frame_memo.h`). No ABI change.
- giRaw clamp: a NaN stores 0, not 65504 (not reachable today).
- Dead code out: `GI_DEBUG_SWIZZLE`, `GI_FAR_M`, the unreachable `kCascade` branch, spec id 1012.
- Skipped unread GPU work, image identical: sun roof-volume test only where read, RF probe term at w = 0
  (replay -0.034 ms), GI temporal in RR mode, water passes without a reader. Debug early returns restore bindings.
- Logs: `[mem] ours` splits the BLAS chunk reserve; `[bvh] BLAS refusals in N frames` when there were refusals.
- Launcher: GPU list stays open, WebView failure falls back to the native window, shorter preset strip.
- Tools: verify_rc names the installed version; the shader equivalence gate names a spirv-opt crash and stops at the
  first difference (74 s, was a 13 h cross); skipped Python tests report Skipped.

Keys added OFF, READY_FOR_LIVE (all in the packaged `bg3rtx.ini` at 0):
- `rt_hair_header_early` (startup): stable strand header address, so the 519 strand variants hit the driver cache.
  Hair pipeline creation 15-48 s at launch -> under 1 s predicted. Job: `final_layer/` fl-header-early (two launches,
  same `header at 0x...` both times, `strand variants: 519 pipelines in` under 1 s in the second).
- `rt_hold_pack_base` (live): the 24-byte hold pack for the 64-byte layout. Predicted 91.5 -> 34 MB written per static
  build at S8, about 0.05 ms. Job: `final_layer/` fl-s8-holdpack (static, 0/1/0).
- `gi_far_roof_emission` (live, spec constant 1054): the far roof rule also for hidden-hit emission. Job: `r2_tools/`
  r2-inn-roof-em (inn, 0/1/0, then a GI replay pack for the PT reference A/B). Predicted: energy lower or equal, closer to PT.
- `rt_as_size_class` + `rt_cascade_keep_refused` (live): cascade casters were left out on BLAS cap refusals (162 in the
  user's log). Replay: refusals 53 -> 2. Job: `r2_layer/` r2-inn (inn dolly, both keys 0/1/0, 8.6 min). At this PC's
  cap refusals may be 0 in both states.

Staged job folders (under `tools/live_batches_staged/`, none queued): `release_final/`, `final_layer/`, `r2_layer/`,
`r2_tools/`; also `dlss5/` (now committed, lint 0 failing). States: `tools/live_batches_staged/README.md`.

## Known, not fixed

- Water game shadow NaN at Vg = 0 (`common/water_fs_patch.h:699`): the pixel keeps the game colour. No small fix.
- RR post history rejected in motion (`rr_post_temporal.hlsl:51`, energy-safe).
- Non-temporal post jitter missing on Low-High (`rr_post.hlsl:250`).
- Sun pass ordering after GI (`core/gi.cpp:3650-3672`): 0.05-0.2 ms possible, needs a GPU A/B.
- GI ray cone in RR mode one mip too coarse (`gi_trace_rt.hlsl:141, 313`).
- Validation status ring wrap: detector only, never seen.
- The mesh pool cannot shrink while a groom sits at its end.
- The pixel-LOD camera BLAS is not compacted (about 16 MB per groom).

## RTX Hair strands

- Off in every preset. In release it is an opt-in Beta control (Lighting > RTX Hair, Ctrl+Shift+F7). On keeps all 26
  bundle keys under the release policy. The user decides whether it goes On in a preset.
- Instability under camera motion is solved for converted styles (hx1 202905: 0 strands->cards).
- Open: styles without a groom file stay cards. Hitch frames are fixed offline only.
- Launcher controls: "RTX Hair detail at distance" (Off / 1/4 / 1/8, live) and "RTX Hair motion stability" (On in
  High and Ultra, restart). Staged hair jobs: `tools/live_batches_staged/README.md` (hx1 in `hair_stable/`).

## Release profile (do not change without a user decision)

- Stock visible hair. Ultra: temporal post, exact-k, two GI rays. High: OMM and offline masks.
- Always on: wf9 water camera correction, far-GI roof-leak fix. Low-latency scene On in High and Ultra, with the hit wait.
- The four VRAM right-sizing policies are On by default.
- Experimental page (behind "Show experimental options", all off): Water surface reflections, two Frame Generation
  pacing tests, Strand hair edge resolve. RTX Hair: Beta control on Lighting, off. Tilesets hidden and off.
- 23 visible controls, 32 developer knobs hidden. Without a user file the package resolves to High without OMM and masks.
- Ctrl+Shift+F9 master RT toggle. GPL-3.0-or-later plus NVIDIA exception. "Tested only on RTX 5090".
- Asset database ships in the archive (user decision). Nothing is uploaded by agents.

## Next work

1. With a user go: the release smoke `release_final/`, then the READY_FOR_LIVE jobs above (header early first: it is
   the largest start cost left). Older READY_FOR_LIVE: `rt_hair_groom_async`, the `rr_mode_latch` log check.
2. Read the next user log: `[mem] ours` reserve split, BLAS refusals line, groom "decoded" lines.
3. The known items above, cheapest first, each behind a key with an A/B.
4. RTX Hair: unconverted styles, then the preset decision.
5. Deferred: L11, L12, L17, C4/C7/C8/C12, S-F05, T2; SHaRC removal; native Tilesets; water off-screen shading.

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

1. A go for the release smoke (about 7 minutes), or accept the hairtest18 sessions as acceptance (they lack the 2026-11-20 changes).
2. The Setup.exe: install it once over hairtest18, or ship the ZIP only.
3. Publish 0.9.6-beta (user action).
4. Decide whether RTX Hair goes On in a preset. Unconverted styles: convert them or accept cards.
5. The worktrees, folders and 24 remote branches above: keep, merge or remove.
