# BG3RTX progress - 2026-11-21 (main 41a5e7f8)

## Question for the user (taken: the conservative option, OMM stays as fixed)
OMM works again in High and Ultra (it was off since 2026-11-14 by a bug). Measured in one run: Ultra cheaper (GI
-0.33 ms), High dearer (RT +0.14..+0.20 ms, GI +0.13..+0.22 ms), image equal. Keep OMM in High (release profile of
2026-11-04), or turn it off in High (`rt_omm=0` in the High preset, needs a restart A/B not yet run)?

Live tests: allowed overnight by the user's /goal of 2026-11-21 only; a new session must ask again.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). Release check: [RC_CHECK](release/RC_CHECK.md).
Full text of the 2026-11-21 entries (every branch fragment of the night) and of 2026-11-20:
[archive 2026-11-21](archive/PROGRESS_CODEX_2026-11-21.md); older: [2026-11-20](archive/PROGRESS_CODEX_2026-11-20.md),
[2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md), [2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md).
Installed test app: 0.9.6-hairtest18 (commit 1c09e039), unchanged.

## Overnight run: release profile changes, applied overnight, confirm or revert (coordinator, merged, 2026-11-21)

- `rt_hold_pack_base=1` (fl-s8-holdpack, A/B in one run): copies per static build 91.0 -> 35.6 MB, scene build GPU
  1.044 -> 0.810 ms median, mesh pool used -53 MB, energy equal (ON 0.16323, OFF 0.16316-0.16333).
- `rt_hair_header_early=1` (fl-header-early, two launches): same header address, 519 strand pipelines 15.4 s -> 0.27 s.
- `rt_as_size_class=1` + `rt_cascade_keep_refused=1` (r2-inn): 0 refusals in both states on this PC (benefit unseen here;
  replay 53 -> 2), energy -0.03%, costs within OFF noise, fewer chunks over 64 MB (5-13 vs 4-29).
- `rt_hair_groom_async=1` in the RTX Hair On bundle (27 keys; r4-inn, hx1a): inn outward dolly frame time p99 98-111 ->
  40 ms, scene wait max 163-167 -> 6-7 ms; hx1a 30/30 gates, 0 strands->cards, 0 grooms dropped. Cost: a head decoded
  for the first time shows cards for about 0.6 s (19-frame runs, cause: slot key churn of heads appearing together).
- Release smoke with these keys on: Dialogue1 15/15 gates; S8 Ultra + High 29/29 window gates (final-s8-ultra-high
  055556, crashed post-analysis; valid rerun 062059 + Dialogue1 062427 after the analyzer fix), images checked, costs within 0.1 ms of 2026-11-04 or lower.

## Overnight run: fixes found by the smoke and the rounds (coordinator, merged, 2026-11-21)

- OMM was off in High and Ultra since the scene handout default (2026-11-14): tickets registered on an ended game
  recording retained OMM until device destroy; every handout line said "OMM accelerated commits 0". Fixed
  (claude/omm-diag); live 66/66 lines accelerated. A/B (oa-s8, accelerated vs reference in one run): Ultra GI -0.33 ms,
  RT -0.14..-0.33 ms (noise); High RT +0.14..+0.20 ms and GI +0.13..+0.22 ms; energy +0.07% / +0.02%. Question below.
- Water one-frame "no scene" flips under the handout (7-24% of 60-frame periods at S8): a later pass now takes the newer
  committed scene (claude/water-noscene). Live: 28/29 periods "no scene 0", the other a scene-wait timeout case.
- Groom BLAS full build into compacted storage (memory overwrite with RTX Hair on), status ring UB (claude/r3-layer).
- Groom decode scheduling: only the newest wanted LOD decodes (one wasted decode fewer), no regression.
- Test tooling: smoke gates derived from the exported profile (High has no water/casters), stats waits time out, water
  and handout counters reset with the window, readiness ignores a superseded GI raygen build, energy windows sized from
  the live corpus, ab-on readiness derived, downgrade gate INFO (a downgrade traces the reference scene, correct image).
- New developer keys, off: `rt_mesh_pool_trim_move` (-491 MB at the hx1 hair layout, needs a live), `rt_sun_overlap`,
  `rr_post_jitter_align` (0-3).

## Overnight run: tested and kept off (coordinator, 2026-11-21)

- `gi_far_roof_emission`: not testable offline (1054 lives only in the emission variant; pack writer, replay and PT
  have no emission); live energy +0.25% (noise). Stays off until emission packs exist.
- `rt_sun_overlap`: RT interval 7.53 / 7.50 / 7.32 ms, no gain. `rr_post_jitter_align=1` on High: 2-4x more background
  flicker (premise wrong). Ultra temporal shift (=3): no defect shown.
- High OMM downgrades (about 1 per 60 frames, reference scene, correct image): no pass waits for the commit in High;
  a fix changes the handout protocol. Left for later.
