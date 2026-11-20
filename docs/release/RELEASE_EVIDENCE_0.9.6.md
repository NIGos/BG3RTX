# Evidence for the 0.9.6-beta release notes

Developer file. Where each number and claim of [RELEASE_NOTES_0.9.6.md](RELEASE_NOTES_0.9.6.md) and
`nexus/changelog_0.9.6.txt` comes from. Line numbers are those at main dd24204e (2026-11-15).
Short names: PROGRESS = `docs/PROGRESS_CODEX.md`; A14 = `docs/archive/PROGRESS_CODEX_2026-11-14.md`;
A11 = `docs/archive/PROGRESS_CODEX_2026-11-11.md`; HAIR = `docs/RTX_HAIR_PRIMARY.md`;
HANDOUT = `docs/SCENE_HANDOUT_DESIGN.md`; HANDOFF = `docs/HANDOFF_SESSION.md`.

Baseline: the published 0.9.5-beta package is commit cb2d03f2 (2026-11-09 15:08, `VERSION.txt` of
`.scratch/release/v0.9.5-beta/pkg`). 81 first-parent commits follow it on main.

## Performance and memory

| Claim | Source |
|---|---|
| VRAM used 16.4 to 15.6 GB (S8), 18.6-18.9 to 17.7 GB (inn); Ultra, RTX Hair on, FG 3x | PROGRESS 23-25 (vr-s8.1.232616, vr-inn.1.233221: 16422-16456 -> 15648 MB, 18587-18895 -> 17697 MB) |
| No object left out; brightness the same | PROGRESS 27-29 (0 dropped draws, 0 buffer full; energy -0.25% and -0.18%, floor 0.5%) |
| Always on, no option | PROGRESS 16-20, 109; `bg3rtx.ini` 211, 217, 222, 227 |
| Shrink pause 36 to 56 ms, rare | PROGRESS 29 |
| Ten reallocations of 1.4-1.8 GB became three | PROGRESS 41-43 (offline replay of the 22:15 sequence, CTest `rt_capture_elastic_test`; not a live result) |
| Low-latency scene: wait 3.5 to 0.3 ms (S8), 8.5 to 2.9 ms (inn) | HANDOUT 447-456; PROGRESS 92 |
| About 0.5 ms on High at S8; unchanged on Ultra; same brightness; 0 fallbacks | HANDOUT 454-458; `common/settings.h` 364-365 (card text) |
| On in High and Ultra; restart to turn on; shaders rebuild once | `common/settings.h` 359-365; PROGRESS 107 |
| Shader file check every 5 s, was 250 ms | PROGRESS 47 (code change, release profile only) |
| Strands about 70 frames after a load, was about 310 | A11 1054-1058 (hw1 VALID: 73 and 66 frames on, 309 off) |

## Stability

| Claim | Source |
|---|---|
| Guard stayed low after one budget drop; fixed; not confirmed in play | A14 7-30; PROGRESS 94 ("unconfirmed in play") |
| A hair style that does not fit no longer stops the scene build | A11 996-1004 (fix 1, key-independent; rv3 183745: 118 of 120 frames skipped before) |
| Stale record of a destroyed FG input removed | PROGRESS 48 (offline) |
| A style unpacked up to 9 times; now once; offline only | `docs/progress/claude-final-layer.md` 2-9 (user log 2026-11-20 16:45-16:50: two groom LODs decoded 9 times each, 1.07 s, under five 100 ms hitches; fix in main 745a1fff, on, no key) |

## RTX Hair

| Claim | Source |
|---|---|
| Off in every preset; Lighting > RTX Hair (Beta badge); Ctrl+Shift+F7. Home has only the conversion progress (Preparation), no switch | `common/settings.h` 415-456; `common/hotkeys.h` 9; `tools/launcher/ui/app.js` (`layout.Lighting`, `hairJob`) |
| On did nothing in 0.9.5-beta; fixed | A11 209-217 (`test_release_rtx_hair`) |
| 0 switches to cards in every test window (fast zoom and orbit, S8) | HAIR 44-45 (hx1 202905); PROGRESS 90 |
| 97 to 99% of hair drawn as strands | HAIR 45-46 (hx1 194658: 97.4 to 99.1%) |
| Entering head: about 3 frames; two runs of 6 and 9 frames unexplained | HAIR 45, 52 |
| Detail at distance: saves 0.08 / 0.10 ms, 75-100 MB, one scene, 13 characters | `common/settings.h` 469 (card); A11 577-593 (pl1 211046) |
| Motion stability on in High and Ultra, restart, acts only with RTX Hair | A11 60-72; PROGRESS 98 |
| Older files get the new settings on load | A11 670-680; HANDOFF "RTX Hair strands" |
| Unconverted styles stay as the game's hair | HAIR 50; PROGRESS 91 |
| Strand pass 0.13 to 0.35 ms | HAIR 46 |
| A player session reported a large frame-rate loss, unexplained | A14 152-155 (perf_ab staged, not run) |
| Strand edges can dither in motion | `nexus/changelog_0.9.5.txt` "Known" (carried over; no newer result) |
| "Softer at distance" | Pixel-LOD study, HAIR 5v (1545-1548) and its fragment `git show eca698b9:docs/progress/claude-hair-pixel-lod-study.md` (tool `tools/hair_groom/pixel_lod.py`): at heads of 40-150 render px a strand is 0.06-0.12 px wide, so the image converges to the smooth average of sub-pixel strands (RMS 0.08-0.12 against the truth, no energy shift); its high-frequency detail is 0.04-0.07 (truth) against 0.09-0.15 for the game's cards. The softness is the correct average, not an error. Offline, one groom (a beard), no motion, no RR model. |

## Launcher

| Claim | Source |
|---|---|
| New layout, instant apply, Undo strip, restart badge | A14 46-60; `docs/USER_SETTINGS.md` 3-28 |
| Basic window keeps Save | A14 70; HANDOFF "Launcher" |
| 640 px: all pages reachable | PROGRESS 63-64 (headless Edge only) |
| Frame limit starts at 58 fps (was 30) | PROGRESS 61 |
| Version from the package (was the constant 0.9.5.0) | A14 120-122 |
| Converter at idle priority; Play stops it | PROGRESS 61-62 |
| Diagnostic report on Home, optional save | commit 686b864a (2026-11-09, after the 0.9.5 package); A11 1121 |
| Fixes: "Partial" report, capture during conversion, backslash path, slider drag | A14 126-130; PROGRESS 60-61 |

## Known issues

| Claim | Source |
|---|---|
| RTX 5090 and driver 621.01 only | `docs/release/README.md` 16-21; HANDOFF "Release profile" |
| Inn after a dolly: 2.3-2.7 GB held for 0.65-0.8 GB live; 0.9-1.0 GB after about 15 s | PROGRESS 33-34 |
| No VRAM recommendations; Act 2/3 and long sessions untested | `docs/release/README.md` 74-81, 117-119 |
| Start 15-50 s longer creating hair shader variants, also with RTX Hair off | `docs/progress/claude-final-layer.md` 5-7, 20-23 (14.7-48.5 s in 18 of 25 queue launches; user logs 17.4 s and 19.6 s); `layer/tracker.cpp` 658 calls `hair_primary_note_pipelines` for every hair pipeline, not gated by `rt_hair`; the fix `rt_hair_header_early` is 0 and READY_FOR_LIVE |

## Left out of the player text on purpose

- `rt_hold_pack_base`, `rt_hair_header_early` and `gi_far_roof_emission` (all 0, READY_FOR_LIVE, 2026-11-20); the
  tracker's descriptor-set maps now erased and `refresh()` reading its keys once per frame (main 745a1fff): no
  measured player effect.
- `rt_hair_groom_async` (off, READY_FOR_LIVE) and the GI image reallocation fix at level loads
  (`rr_mode_latch`, READY_FOR_LIVE): built, unverified live (PROGRESS 44-46, 50-55). No gain is claimed.
- GI cost 2026-11-05 against 2026-11-14 (A14 152-170): the baseline predates 0.9.5, and 0.9.5 already claimed the GI gain.
- `rt_mesh_budget_own`, `rt_as_cap_occupied`, `rt_as_chunk_fit`: off by default (`bg3rtx.ini` 183, 245; PROGRESS 21-22).
- The per-draw hair mutex removal (PROGRESS 94): no measured player effect.
- "RTX Hair could not be turned off" (A14 111-119): a bug of the test build 0.9.6-hairtest8, never published.
- The user's 26.5 GB sessions (PROGRESS 14-15, 36): they predate the fixes and were not re-measured.

## Before publishing

- No live in the release profile has run since the changes of 2026-11-14/15. The numbers above come from
  qualification runs with job settings, not from the packaged build.
- The launcher was rendered in headless Edge only (PROGRESS 70).
- `docs/release/README.md` says ReShade 6 works (tested 2026-11-05); `docs/progress/claude-leftovers-small.md`
  says the rs1 run was INVALID and the ReShade case is unverified. The README line was not changed here.
