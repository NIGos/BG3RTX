# Evidence for the 0.9.6-beta release notes

Developer file. Where each number and claim of [RELEASE_NOTES_0.9.6.md](RELEASE_NOTES_0.9.6.md) and
`nexus/changelog_0.9.6.txt` comes from. Line numbers are those at main dd24204e (2026-11-15).
Short names: PROGRESS = `docs/PROGRESS_CODEX.md`; A14 = `docs/archive/PROGRESS_CODEX_2026-11-14.md`;
A11 = `docs/archive/PROGRESS_CODEX_2026-11-11.md`; HAIR = `docs/RTX_HAIR_PRIMARY.md`;
HANDOUT = `docs/SCENE_HANDOUT_DESIGN.md`; HANDOFF = `docs/HANDOFF_SESSION.md`;
A21 = `docs/archive/PROGRESS_CODEX_2026-11-21.md` (line numbers at main 37ff925d); UX = `docs/LAUNCHER_UX_REVIEW.md`.

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
| Second start: 519 strand pipelines 15.4 s -> 0.27 s (`rt_hair_header_early=1`) | A21 14, 361-362 (fl-header-early, two launches, same header address); `bg3rtx.ini` 250-252 |
| Static rebuild: 91 -> 36 MB copied, scene build 1.04 -> 0.81 ms, mesh pool -53 MB, energy equal (`rt_hold_pack_base=1`) | A21 12-13 (fl-s8-holdpack, A/B in one run: energy ON 0.16323, OFF 0.16316-0.16333) |
| Size classes and cascade keep refused: 4.0 GB for 1.27 GB in the 2026-11-20 log; replay 53 -> 2 refusals; 0 refusals on the test PC; energy and costs unchanged | A21 15-16, 356-360; `bg3rtx.ini` 263-271 (r2-inn: energy -0.03%, costs within OFF noise) |
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
| OMM off under the handout in the test builds, fixed; 0.9.5 not affected | A21 24-27, 248-264 (claude/omm-diag; live 66/66 handout lines accelerated). The handout became the High/Ultra default on 2026-11-14, after the 0.9.5 package (cb2d03f2, 2026-11-09) |
| OMM A/B: Ultra GI -0.33 ms; High +0.3 to 0.4 ms; same image; kept on | A21 27-28 (oa-s8, accelerated against reference in one run: High RT +0.14..+0.20, GI +0.13..+0.22 ms; energy +0.07% / +0.02%). Kept on in High: user decision 2026-11-21 |
| Water one-frame flicker: 7-24% of 60-frame periods; after the fix 28 of 29 periods clean | A21 29-30, 48-62 (claude/water-noscene; the 29th period is a scene-wait timeout case) |
| A style unpacked up to 9 times; now once; offline only | `docs/progress/claude-final-layer.md` 2-9 (user log 2026-11-20 16:45-16:50: two groom LODs decoded 9 times each, 1.07 s, under five 100 ms hitches; fix in main 745a1fff, on, no key) |

## RTX Hair

| Claim | Source |
|---|---|
| Off in every preset; Lighting > RTX Hair (Beta badge); Ctrl+Shift+F7. Home has only the conversion progress (Preparation), no switch | `common/settings.h` 415-456; `common/hotkeys.h` 9; `tools/launcher/ui/app.js` (`layout.Lighting`, `hairJob`) |
| On did nothing in 0.9.5-beta; fixed | A11 209-217 (`test_release_rtx_hair`) |
| 0 switches to cards in every test window (fast zoom and orbit, S8) | HAIR 44-45 (hx1 202905); PROGRESS 90 |
| 97 to 99% of hair drawn as strands | HAIR 45-46 (hx1 194658: 97.4 to 99.1%) |
| Entering head: about 3 frames (the 6 and 9 frame runs of hx1 202905 are no longer in the notes: hx1a with the async decode had median 3, max 3, in all six bundle windows) | HAIR 45, 52; A21 320-323 |
| Detail at distance: saves 0.08 / 0.10 ms, 75-100 MB, one scene, 13 characters | `common/settings.h` 469 (card); A11 577-593 (pl1 211046) |
| Motion stability on in High and Ultra, restart, acts only with RTX Hair | A11 60-72; PROGRESS 98 |
| Older files get the new settings on load | A11 670-680; HANDOFF "RTX Hair strands" |
| Unconverted styles stay as the game's hair | HAIR 50; PROGRESS 91 |
| Strand pass 0.13 to 0.35 ms | HAIR 46 |
| Background groom decode: inn outward dolly p99 98-111 -> 40 ms; first-loaded head on cards about 0.6 s (18-25 frames) | A21 17-19, 309-328 (r4-inn.1.030500; hx1a-s8-hair-async.1.033523: 30/30 gates, 0 strands->cards, groom dropped 0); `rt_hair_groom_async=1` in the RTX Hair On bundle (`common/settings.h` 445-458) |
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
| Badge wording, stacked rows, detail panel presets, one name "Hotkeys" | UX findings 1, 2, 3, 5 (claude/launcher-ux, merged e4a041c6); `tools/launcher/ui/app.js` `restartBadge` |

## Known issues

| Claim | Source |
|---|---|
| RTX 5090 and driver 621.01 only | `docs/release/README.md` 16-21; HANDOFF "Release profile" |
| Inn after a dolly: 2.3-2.7 GB held for 0.65-0.8 GB live; 0.9-1.0 GB after about 15 s | PROGRESS 33-34 |
| No VRAM recommendations; Act 2/3 and long sessions untested | `docs/release/README.md` 74-81, 117-119 |
| First start 15-50 s longer creating hair shader variants, also with RTX Hair off; later starts cached | `docs/progress/claude-final-layer.md` 5-7, 20-23 (14.7-48.5 s in 18 of 25 queue launches; user logs 17.4 s and 19.6 s); `layer/tracker.cpp` 658 calls `hair_primary_note_pipelines` for every hair pipeline, not gated by `rt_hair`. `rt_hair_header_early=1` since 2026-11-21 (row "Second start" above) |

## Left out of the player text on purpose

- `gi_far_roof_emission` (0): PT verdict CLOSER, but +0.7 to 1 ms of GI/RT in Dialogue1 (PROGRESS, section
  "gi_far_roof_emission: PT verdict CLOSER", r2-d1-roof-em 080726); kept off by the user on 2026-11-21. Off, so not mentioned.
- `rt_mesh_pool_trim_move`, `rt_sun_overlap`, `rr_post_jitter_align` (developer keys, 0): no player effect.
- The groom BLAS overflow into compacted storage and the groom decode scheduling (A21 31-32): RTX Hair internals,
  no separate player claim. The tracker's descriptor-set maps now erased and `refresh()` reading its keys once per
  frame (main 745a1fff): no measured player effect.
- The GI image reallocation fix at level loads (`rr_mode_latch`, READY_FOR_LIVE): built, unverified live. No gain is claimed.
- High OMM downgrades (about 1 per 60 frames, the reference scene, correct image; A21 270-280): no visible effect.
- GI cost 2026-11-05 against 2026-11-14 (A14 152-170): the baseline predates 0.9.5, and 0.9.5 already claimed the GI gain.
- `rt_mesh_budget_own`, `rt_as_cap_occupied`, `rt_as_chunk_fit`: off by default (`bg3rtx.ini` 183, 245; PROGRESS 21-22).
- The per-draw hair mutex removal (PROGRESS 94): no measured player effect.
- "RTX Hair could not be turned off" (A14 111-119): a bug of the test build 0.9.6-hairtest8, never published.
- The user's 26.5 GB sessions (PROGRESS 14-15, 36): they predate the fixes and were not re-measured.

## Release evidence of 2026-11-21

- Release profile confirmed by the user on 2026-11-21: `rt_hold_pack_base`, `rt_hair_header_early`, `rt_as_size_class`,
  `rt_cascade_keep_refused` = 1 in the packaged `bg3rtx.ini` (lines 249, 252, 268, 271); `rt_hair_groom_async=1` in the
  RTX Hair On bundle (27 keys); OMM stays on in High; `gi_far_roof_emission=0` (line 990).
- Final release smoke with that profile: final-s8-ultra-high.1.062059 (S8, Ultra and High, 29/29 window gates) and
  final-stock-hair-dialogue1.1.062427 (Dialogue1, 15/15 gates), both valid runs with the post-analysis
  (REVIEW_REQUIRED by design; S8 images checked on run 055556, which crashed in the post-analysis before the analyzer
  fix). Costs within 0.1 ms of 2026-11-04 or lower. OMM downgrades INFO (High about 1 per 60 frames).
- A/B numbers (each in one run): hold pack, header early, size classes and keep refused, groom async, OMM and the
  water fix as in the rows above. `gi_far_roof_emission` in Dialogue1: GI trace OFF 7.85 / 8.40, ON 8.85 ms; RT passes
  OFF 9.64 / 10.31, ON 11.05 ms (FAIL on cost); energy -0.39%; kept off.
- Candidate rebuilt from main 9ae2ede3 (the smoke-tested code plus the diagnostic emission pack writer, off, and the
  launcher UX pass): verify_rc rc-20261121-9ae2ede3-35b9a6f6, 11 PASS, Setup SKIPPED (another install present).
  Hashes: `docs/release/RC_CHECK.md`.
- The user tested the candidate 9ae2ede3 on 2026-11-21 and reported everything OK.

## Before publishing

- The packaged player docs (`README.md` from `docs/release/README.md`, and `docs/USER_SETTINGS.md`) changed after the
  candidate was built: repackage and rerun verify_rc so the archive carries them.
- The compiled Setup.exe has not been executed on this PC (RC_CHECK "Not verified" 6).
- The launcher was rendered in headless Edge only; the user has used it in the installed test builds.
- `docs/release/README.md` says ReShade 6 works (tested 2026-11-05); `docs/progress/claude-leftovers-small.md`
  says the rs1 run was INVALID and the ReShade case is unverified. The README line was not changed here.
