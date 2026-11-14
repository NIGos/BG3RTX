# BG3RTX progress - 2026-11-14 evening (main d2cceda0)

Live tests: on the evening of 2026-11-14 the user allowed autonomous lives while the PC was free. That go is spent: a new
session must ask again, and tell the user right before launching the runner. Pending live evidence is READY_FOR_LIVE.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). RTX Hair: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 6g-6k.
Scene handout: [SCENE_HANDOUT_DESIGN](SCENE_HANDOUT_DESIGN.md) 8.5-8.8.
Full text of every entry below: [archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md) (part 2 = afternoon and evening); older: [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md).
Installed test app: 0.9.6-hairtest17 (commit 6199932b). Main d2cceda0 adds only gate refinements and docs.

## 2026-11-14, afternoon and evening

### RTX Hair instability under camera motion: solved for converted styles (branches claude/hair-stable, hair-residual, hair-adopt, hair-final-report, hair-cleanup)
- What the user saw (play log of hairtest13, about 2 minutes, Ultra): 960 of 2927 hair frames with every hair draw on cards,
  139 switches to cards, 161 draws under a tenth of their card pixels. Still camera: 0 switches.
- Cause 1: the header camera was read on the CPU and lagged one camera step (0.08-0.57 m); `rt_hair_header_cam_gpu` was 0.
  Fix: RTX Hair On writes `rt_hair_header_cam_gpu=1` (old files get it on load).
- Cause 2 (6h): the header was applied at the first pass with 4 or more colour targets, and the decal pass matches too. When the
  present fell between the scene G-buffer pass and the decal pass, the next frame's hair traced the previous publish's TLAS:
  52-76% of all frames in hx1 190905 (165 of 217 and 131 of 183 in the dollies). Fix: `rt_hair_header_scene_pass=1`, in the
  bundle (only the 5-target scene pass applies the header). The "same build" reading of 6e was wrong (stamp copied at that apply).
- `rt_hair_coverage_keep=1` in the bundle (26 keys): coverage evidence does not expire by age. "Coverage stale" refusals
  16-35 -> 0. A head entering the view shows cards for 10 frames without it, typically 3 with it (5 before the handshake change, 6k).
- Final live hx1 202905 (S8, 16 of 16 phases; INVALID only for a wrong control gate, replayed ok): all six bundle windows
  0 strands->cards, 0 frames on cards, 0 watchdog trips. Card runs min / median / max with keep: 3 / 3 / 3 in four windows,
  3 / 3 / 9 in in-on, 3 / 6 / 6 in orbit-r (the 9 and the 6 are unexplained). Keep off: median 9-10, max 72.
- hx1 194658 (25 of 25 phases, VALID): strand draws 97.4-99.1% of the hair draws; dense samples moving 79-84% accepted, inside
  the still spread (control without the key: 59% and 70%). Cost: strand G-buffer 0.13-0.20 ms zoomed out, 0.27-0.35 ms near.
  The two new keys are CPU only: GPU cost 0, no shader or ABI change, variants.txt delta 0.
- Removed with code, knobs, ini lines and tests: `rt_hair_stale_publish`, `rt_hair_row_stable`, `rt_hair_watchdog_draw`,
  `rt_hair_watchdog_one_key`, `rt_hair_swap_latch`, `rt_hair_coverage_min_pixels` (all read 0 in the qualifying run; old user
  files still load). The switches log line now has seven causes.
- `rt_hair_card_fill` (card under missing strands, restart, spec 1053): Advanced option, not in the bundle. It draws 4.5-5.6%
  of the card pixels; final image equal within 1 of 255, no visible gain (pairs in `docs/hair_fill/`).
  `rt_hair_dense_sample` stays as a diagnostic. Disproved: LSS refit as the cause (61347 of 61347 hits kept).
- Open: styles without a groom file stay cards (S8 index counts 3399, 7086). Hitch frames (two scene passes in one present
  interval) are fixed offline only (CTest `test_header_pass`), never seen live; the first one of a session writes one
  `[hair primary] hitch frame` line. The 2 build-latency frames of an entering head stay. The key-off header path stays as the A/B.
- RTX Hair is still Off in every preset (user decision pending). Check: S8, RTX Hair On alone, zoom fast with the wheel and
  orbit: heads in view keep strands; a head that enters shows cards for about 3 frames.

### Scene handout, "Low-latency scene": On by default in High and Ultra (branches claude/handout-on, handout-energy, handout-modes, handout-fix)
- User decision "Si, accendilo", then "Tienilo acceso" after the corrected numbers. Preset row Off / Off / On / On, a normal
  Performance control, always with `rt_scene_handout_hit_wait=1` (code default 1, release policy keeps it, old files load with it).
- First qualification (handout_qual, focused, FG 3x): inn Ultra 30.0 -> 26.5 ms, S8 Ultra 20.9 -> 19.9, S8 High pan 12.1 -> 11.6.
  The Ultra gains were mostly dropped work: the commit-time hit-material query was never published on handout frames
  (`[hit material submit] ordered 0`), so hit GI traced almost no light rays at hits (0.14 M against 3.84 M). Energy -0.20% at
  S8 (hq-s8-energy 191419, 4.7 standard errors), GI trace 4.83 -> 3.49 ms (S8), 8.2 -> 4.7 ms (inn). High was not affected.
- Fix `rt_scene_handout_hit_wait=1`: hit-material passes of a handout frame take the committed pair (one wait).
  hq-s8-modes 205520: 3.83 M rays, diffuse +0.064%, wait 3.5 -> 0.28 ms, frame 21.58 against OFF 21.29 / 21.49, GI trace equal.
  hq-inn-fix 211816: wait 8.5 -> 2.9 ms, frame 30.0 -> 30.1 (noise 0.35), energy +0.06%, 0 fallbacks, 0 ON timeouts.
- Honest result: frame time unchanged on Ultra (both scenes GPU-bound), about -0.5 ms on High at S8, 0 fallbacks, energy equal.
  Card text now "Saves 0 to 0.5 ms"; the "1 to 3.5 ms" claim is withdrawn.
- Rerun hq-s8-ultra 215258 (PC idle, 26 of 26 phases): EXPLAIN, no failure. Wait 3.4-3.7 -> 0.16-0.24 ms. Hair-pan frame time
  p99 +1.2 ms inside a noise of 6.0, so the afternoon's +4.4 ms (run 160909) was external load: the user was using the PC.
- Open: GI trace reads 0.2-0.4 ms lower with the handout in that rerun (static -0.36, pan -0.22, energy windows -0.38;
  EXPLAIN; not reproduced in 205520, where it was equal). Benefit in CPU-bound scenes unmeasured.
- Check: restart on High or Ultra; `[rt] scene wait` near 0.3 ms and `hit material passes on the committed scene N` with N
  above 0; Performance, Low-latency scene Off (live) to compare.

### Sensible gates (branches claude/handout-energy, handout-modes, gates-refine; rule in AGENTS.md, `tools/live_batches/sane_gates.py`)
- FAIL only beyond the OFF noise plus a floor (cost max(0.1 ms, 1%), energy 0.5%, match 1 point); INFO between noise and noise
  plus floor; an unpredicted drop is EXPLAIN; a p99 takes the full range of the OFF periods as noise; events in OFF windows are
  the baseline; equal counts allow the OFF spread plus 1%; short windows are "not judged" instead of an INVALID run.
- hq-inn-fix 211816 had 6 false FAILs, now 0. Still failing by design: mode 2 without the fix, +1 ms at 21 ms, a fallback.
- `live_flow.PRODUCER_SETTINGS`: GI far needs `rt_stats=1`, the core handout line needs `rt_scene_handout=2` (the stop of
  hq-s8-modes 204217 was this job fault). tests/sane_gates_test.py, 10 tests. No code or shader change.
- Disk: trace folders `.claude/worktrees/test-main/captures/20261114-191625` to `-191832` (24, 1.17 GB) are analysed, deletable.

## 2026-11-14, morning

### Performance investigation (branches claude/perf-ab, claude/cpu-hooks, claude/vram-guard-latch)
- Release profile today vs 2026-11-05 (static windows, GPU ms, Ultra): GI much cheaper (S8 12.10 -> 8.20, inn 21.18 -> 15.99,
  Dialogue1 14.12 -> 10.94). Scene build +0.1 to +0.7 ms (S8 1.18 -> 1.84, inn 2.49 -> 2.59, inn High 2.14 -> 2.66). Local shadows,
  reflections, RR within about 0.3 ms (noise). Wide split 0 vs 1 differs by only 0.05-0.1 ms. Camp save "Campsite - 27h" no longer exists.
- RTX Hair On costs about +1.4 ms frame time at the inn (28.9 -> 30.3 ms, focused, FG 3x). Base CPU hooks 12.13 vs 12.39 ms: no regression.
- Likely cause of the user's "very low fps": the VRAM guard latched LOW after the OS halved the budget (31419 -> 15477 MB, log of 2026-11-13).
  Fixed (`common/vram_guard.h`): drop memory of two stats windows, slack capped by what the yielders can free, one trim ask per episode,
  log lines `OS VRAM BUDGET TOO LOW` and `budget covers this process again`. Unit-test replay of the log. Unconfirmed in play.
  User check: alt-tab away and back; one `headroom LOW` line, then `headroom back`.
- Hair-on per-draw global mutex removed (`hair_primary::DrawFilter`): about 6900 non-hair draws per frame took `g_mu`.
  Offline model only: 0.20 -> 0.04 ms/frame (1 thread), 1.15 -> 0.07 (4 threads).
- The S8 perf-ab job timed out on pipeline readiness; DLSS 5 cost not measured. Staged: `tools/live_batches_staged/perf_ab/`.
- Queue runner: `test-queue.ps1` pruned logs older than 3 days, then the preflight refused every job. Fixed: `keep_logs.py` keeps the
  newest 3 VALID logs per save; an empty corpus lints with the frozen snapshot. The hair live logs of 2026-11-10/11 are gone.
- Windows Smart App Control turned itself on at 14:06 and blocked fresh test exes and a setup; the user turned it off at 14:18.

### Launcher UI rework (branch claude/ui-rework, merged) and review (claude/launcher-review, 2026-11-11)
- "Direction A": sidebar with Play, setting rows in groups with indented children, detail panel, cost chip, amber restart badge.
- Instant apply: a click writes at once (no Save, no review). The notice names what else changed, with Undo. A failed write changes
  nothing and offers Retry. The native fallback UI is unchanged (still has Save). In the installed app.
- Rendered only in headless Edge (`docs/ui_rework/shots_impl/`); not reviewed by the user in the real WebView2 window. Detail text
  of some settings is still long. Check: `tools/launcher/ui/index.html?sample&chrome=1`.
- Review fixes: RTX Hair Off sticks; version from the package `VERSION.txt`; reports no longer "Partial" for old ini files; converter fixes.

## RTX Hair, 2026-11-10/11
- Motion stability ON in High and Ultra (`rt_hair_mv_card=2`, user decision; needs a restart). hm1 camera-only share 11% (off 98%), ratio 0.77.
- Detail at distance (pixel LOD, Off / 1/4 / 1/8, applies live): heads 35-60 px keep 1/4; strand G-buffer -27% (1/4), -34% (1/8). Camera BLAS about 16 MB per groom, not compacted.
- Holes under a fast dolly: cause 1 found then (`rt_hair_header_cam_gpu`), cause 2 on 2026-11-14 (above). Other branches of those days: see the archive.

## Current user decisions
- Stock visible hair ships; strand hair hidden until its post-release update. Experimental page in release: only Water surface reflections (off).
- The game-derived asset database ships in the archive. GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP plus portable ZIP; manual download. Lean: only the tests that are really needed. Optimisations that qualify are always on.
- 2026-11-11: RTX Hair motion stability On in High and Ultra. 2026-11-14: launcher Direction A, instant apply, dark only.
- 2026-11-14: Low-latency scene On in High and Ultra, always with the hit wait fix ("Si, accendilo", "Tienilo acceso").
- 2026-11-14: sensible gates (fail beyond noise plus floor). Lives allowed that evening while the PC was free; ask again in a new session.
- Pending: RTX Hair On in a preset; `rt_hair_card_fill` stays an Advanced option unless the user wants it.

## Older entries (2026-11-04 to 2026-11-09; full text in the archives [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md), [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md))
- Releases: 0.9.3-beta, 0.9.4-beta (RC PASS), 0.9.5-beta packaged 2026-11-08; build.ps1 prefers VS 2022; packaging on a mounted-folder disk; launcher picks the RTX GPU.
- Scene handout design and increments 1-5 (2026-11-08/09), mesh cache budget charged with its own BLAS, BLAS chunk caps, VRAM reserve from the observed rise.
- RTX Hair 2026-11-04 to 11-09: groom storage v3 and converter `bg3rtx_hairconv`, swap and join fixes, strand fibre shading, edge quality, warm-up,
  switchable while playing (Ctrl+Shift+F7), header camera from the GPU, strand-resolve pairing, capture stride split, M1-M5 quality items (mostly READY_FOR_LIVE).
- Lighting: RT pipeline GI trace (on), compact GI hit state (always on), early roof rejection (on), exact local-shadow optimisation, RF probe work (mixed),
  SER feasibility, wide split on by default, rise reserve on, Home dashboard and Hotkeys page.
- Rejected: far GI live state, conservative GI-only TLAS, GI origin audit, BLAS flags replay, sun wave, default Ultra RF follow-ups.
- Audits (fixed offline): DLSS 5, FG, core completion contract, L11/L12/L17 submission, suspect review, S-F05 R11G11B10, ReShade isolation, queue cancellation.
