# BG3RTX progress - 2026-11-14 (main 4e5e435d)

Live tests: allowed by the user on the morning of 2026-11-14 for the performance investigation, then the user took the PC
back (about 14:10). Offline only until a fresh explicit go (flag `.scratch/coord/PAUSE`). Pending live evidence is READY_FOR_LIVE.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). RTX Hair design and history: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 5v-6f.
Full text of every entry below (about 400 lines): [archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md); older: [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md).

## 2026-11-14

### Performance investigation (branches claude/perf-ab, claude/cpu-hooks, claude/vram-guard-latch)
- Release profile today vs 2026-11-05 (static windows, GPU ms, Ultra): GI much cheaper (S8 12.10 -> 8.20, inn 21.18 -> 15.99,
  Dialogue1 14.12 -> 10.94). Scene build +0.1 to +0.7 ms (S8 1.18 -> 1.84, inn 2.49 -> 2.59, inn High 2.14 -> 2.66). Local shadows,
  reflections, RR within about 0.3 ms (noise). Wide split 0 vs 1 differs by only 0.05-0.1 ms. Camp save "Campsite - 27h" no longer exists.
- RTX Hair On costs about +1.4 ms frame time at the inn (28.9 -> 30.3 ms, focused, FG 3x). Base CPU hooks 12.13 vs 12.39 ms: no regression
  (the 13.3-13.8 of perf-ab-inn is the same timer under other conditions; "CPU dispatch tracking pre" is another quantity).
- Likely cause of the user's "very low fps": the VRAM guard latched LOW after the OS halved the budget (31419 -> 15477 MB, log of 2026-11-13).
  It kept the largest drop as slack, so leaving LOW was impossible. Fixed (`common/vram_guard.h`): drop memory of two stats windows, slack capped
  by what the yielders can free, one trim ask per episode, two new log lines (`OS VRAM BUDGET TOO LOW`, `budget covers this process again`).
  Unit-test replay of the log. Unconfirmed in play. User check: alt-tab away and back; one `headroom LOW` line, then `headroom back`.
- Hair-on per-draw global mutex removed (`hair_primary::DrawFilter`, `common/hair_primary.h`): about 6900 non-hair draws per frame took `g_mu`.
  Offline model only: 0.20 -> 0.04 ms/frame (1 thread), 1.15 -> 0.07 (4 threads). Live check: perf-ab inn, hair on, old vs new layer.
- The S8 perf-ab job timed out on pipeline readiness; DLSS 5 cost not measured. Staged: `tools/live_batches_staged/perf_ab/`.
- Hold path copies a lot per 120 builds (inn Ultra copy 63985 jobs, 17.1 GB); job C measures it against the split off.

### Queue runner and Windows
- `test-queue.ps1` pruned logs older than 3 days, then the preflight refused every job (`VALID corpus: 0 logs`). Fixed: `keep_logs.py`
  keeps the newest 3 VALID logs per save; an empty corpus lints with the frozen snapshot. The hair live logs of 2026-11-10/11 are gone; numbers stay in docs.
- Windows Smart App Control turned itself on at 14:06 and blocked fresh test exes and a setup; the user turned it off at 14:18. Results of that window were discarded.

### Scene handout ready to qualify (branch claude/handout-ready, READY_FOR_LIVE)
- Complete (increments 1-5), off by default. Keys `rt_scene_handout_layout` (restart) and `rt_scene_handout` 0/1/2. Launcher: Experimental page,
  "Low-latency scene" (On writes both keys, needs a restart). Only evidence: S8 2026-11-09, scene wait 4.4-5.1 -> 0.47-0.69 ms, frame about -0.5 ms.
  The inn (wait 8-9 ms) is unmeasured. Not a one-frame-old scene except on fallback frames.
- Staged `tools/live_batches_staged/handout_qual/` (hq-s8 and hq-inn, Ultra and High, smoke): 0 / 2 / 0 in one run, thresholds from the OFF repeat. 225/225 CTests.
- User check: Show Experimental, Performance, Low-latency scene On, restart; the log has `[rt] scene handout layout on` and `handed out N (0 ms)`.

### Launcher UI rework (branch claude/ui-rework, merged)
- "Direction A": sidebar with Play, setting rows in groups with indented children, detail panel, cost chip, amber restart badge, Experimental page.
- Instant apply: a click writes at once (no Save, no review). Dependencies resolve at the click; the notice names what else changed, with Undo (one write).
  A failed write changes nothing and offers Retry. Keyboard and screen-reader pass done. The native fallback UI is unchanged (still has Save).
- Rendered only in headless Edge (`docs/ui_rework/shots_impl/`); not checked in the real WebView2 window. Detail text of some settings is still long.
  226/226 CTests. Check: `tools/launcher/ui/index.html?sample&chrome=1`. Design docs: `docs/ui_rework/`.

### Launcher review (branch claude/launcher-review, offline, 2026-11-11)
- RTX Hair Off could not stick (the experimental edge resolve raised it again at Save): a knob turned down since the last save stays down.
- Version shown from the package `VERSION.txt`; reports no longer "Partial" for old ini files or `.prev.log`; converter fixes (folder with trailing backslash,
  choosing granny2.dll again). dev=1: a saved RTX Hair part knob wins over the bundle. Open: hair card trusts a stale preparation.json; report redaction gaps.

## RTX Hair, 2026-11-10/11 (holes unchanged since 2026-11-11)
- Motion stability ON in High and Ultra (`rt_hair_mv_card=2`, user decision; needs a restart). hm1 camera-only share 11% (off 98%), ratio 0.77; strand G-buffer 0.22-0.31 ms.
- Detail at distance (pixel LOD, Off / 1/4 / 1/8, applies live): heads 35-60 px keep 1/4; strand G-buffer -27% (1/4), -34% (1/8). Camera BLAS about 16 MB per groom, not compacted.
- Holes under a fast camera dolly: cause 1 fixed behind `rt_hair_header_cam_gpu=1` (coverage 2-7% -> 50-70%); a 30-50% loss remains. Next: hl1 with
  `rt_hair_dense_sample=1` (`tools/live_batches_staged/hair_late/`, READY_FOR_LIVE). The hf2 job (`hair_final/`) must set `rt_hair_header_cam_gpu` first.
  Disproved: other build's TLAS, row order, full BLAS rebuilds, pool reuse, VP eye, stale publish, newer build, refit, game LOD switch.
- Other 2026-11-10 hair branches (stale publish, slot identity, swap transitions, release policy, bundle keys, coverage diagnostics): see the archive.

## Current user decisions
- Stock visible hair ships; strand hair hidden until its post-release update. Experimental page in release: only Water surface reflections (off).
- The game-derived asset database ships in the archive. GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP plus portable ZIP; manual download. Lean: only the tests that are really needed. Optimisations that qualify are always on.
- 2026-11-11: RTX Hair motion stability On in High and Ultra. 2026-11-14: launcher Direction A, instant apply, dark only, Low-latency scene on Experimental.
- 2026-11-14: lives allowed for the performance investigation in the morning, paused again about 14:10 (offline only until a fresh go).

## Older entries (2026-11-04 to 2026-11-09; full text in the archives [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md), [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md))
- Releases: 0.9.3-beta, 0.9.4-beta (RC PASS), 0.9.5-beta packaged 2026-11-08; build.ps1 prefers VS 2022; packaging on a mounted-folder disk; launcher picks the RTX GPU.
- Scene handout design and increments 1-5 (2026-11-08/09), mesh cache budget charged with its own BLAS, BLAS chunk caps, VRAM reserve from the observed rise.
- RTX Hair 2026-11-04 to 11-09: groom storage v3 and converter `bg3rtx_hairconv`, swap and join fixes, strand fibre shading, edge quality, warm-up,
  switchable while playing (Ctrl+Shift+F7), header camera from the GPU, strand-resolve pairing, capture stride split, M1-M5 quality items (mostly READY_FOR_LIVE).
- Lighting: RT pipeline GI trace (on), compact GI hit state (always on), early roof rejection (on), exact local-shadow optimisation, RF probe work (mixed),
  SER feasibility, wide split on by default, rise reserve on, Home dashboard and Hotkeys page.
- Rejected: far GI live state, conservative GI-only TLAS, GI origin audit, BLAS flags replay, sun wave, default Ultra RF follow-ups.
- Audits (fixed offline): DLSS 5, FG, core completion contract, L11/L12/L17 submission, suspect review, S-F05 R11G11B10, ReShade isolation, queue cancellation.
