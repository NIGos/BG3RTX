# BG3RTX progress - 2026-11-21 (main 41a5e7f8)

## Questions for the user (conservative options taken)
OMM works again in High and Ultra (it was off since 2026-11-14 by a bug). Measured in one run: Ultra cheaper (GI
-0.33 ms), High dearer (RT +0.14..+0.20 ms, GI +0.13..+0.22 ms), image equal. Keep OMM in High (release profile of
2026-11-04), or turn it off in High (`rt_omm=0` in the High preset, needs a restart A/B not yet run)?

`gi_far_roof_emission`: its PT A/B became testable overnight (emission packs), but the first replay of the inn pack lost
the GPU device (cause found and fixed: the replay used the live TLAS address from the handout stamp). By the GPU crash rule it
was not rerun. Rerun it with the fix (pack kept: .claude/worktrees/test-main/captures/replay-20261121-071523-1, 1.6 GB),
or delete the pack and leave the key off? Taken: off, pack kept.

Live tests: allowed overnight by the user's /goal of 2026-11-21 only; a new session must ask again.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). Release check: [RC_CHECK](release/RC_CHECK.md).
Full text of the 2026-11-21 entries (every branch fragment of the night) and of 2026-11-20:
[archive 2026-11-21](archive/PROGRESS_CODEX_2026-11-21.md); older: [2026-11-20](archive/PROGRESS_CODEX_2026-11-20.md),
[2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md), [2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md).
Installed test app: 0.9.6-hairtest18 (commit 1c09e039), unchanged.

## Emission replay packs and PT emission for gi_far_roof_emission (branch claude/em-pack, READY_FOR_LIVE, 2026-11-21)

- Why: the 1054 A/B could not run offline (.scratch/coord/roof_em_pt.md): the GI pack writer refused the emission
  variant, a pack replay bound no emissive scene, the PT had no emission.
- `replay_pack_pass=2` (diagnostic, default 0): one pack of the rt_hit_emission + gi_hit_sun hit GI as pass
  `gi_hit_sun_em` (no SHaRC, no OMM): the live request and specialization values, the sun trust image s3b8, the D-EM
  scene set as pack set 4 (layer records its writes, `replay_note_em_sets`). No ABI change; layer and core rebuilt.
- frame_replay: compiles the pass with `EM_SET=4` (game requests keep 2: the 34 gi_trace_rt requests byte-identical to
  main), rebuilds the emissive TLAS from emInfo + emSoup (`rp::em_geometries`), binds it. `--pt-reference` on such a
  pack sets `PT_EM=1`: mode 8 = first-vertex emission x the GI ray fade at hidden first hits (`pt_emission.pfm`).
- `tools/em_roof_ab.py`: 1054=0 / 1 / no-emission replays against PT mode 8 on the changed pixels; verdict CLOSER /
  FARTHER / NO CHANGED PIXEL / INVALID (raised pixels or giRaw.a changed).
- Tests: replay_pack_test (contract, em_geometries, reference_defines), pt_reference_test 12-16 on the synthetic
  emissive box: mode 8 a = 1, path emission 1 / 1.75, the pass's hidden-hit emission 0.6918 vs PT mode 8 0.6927 per
  pixel, giRaw.a equal to the sun variant, 1054=1 lowers only; em_roof_ab_test; `build.ps1 -Test`.
- Staged (not queued): `tools/live_batches_staged/r2_tools/em_pack` job r2-inn-em-pack: one launch at the inn, Ultra
  with OMM off, ~25 s after load + up to 90 s for the writer, at most 2048 MB; live_flow 0 failing (with and without
  --frozen). Offline steps after it: predicted.json "offline". Key `gi_far_roof_emission` stays 0 until then.

## gi_far_roof_emission offline A/B on the inn emission pack: NOT RUN, the replay lost the device (branch claude/em-pack-result, 2026-11-21)

- Pack: test-main captures/replay-20261121-071523-1 (1579 MB, pass gi_hit_sun_em, frame 2077, casters 1222447 tris,
  emissive scene 256 texture / 16 cache elements). Kept: it is still the input of the A/B.
- The first step (frame_replay --pack <pack> --spec 1054=0, test-main d4ee3e0a binaries) ended in `wait result=-4`
  (VK_ERROR_DEVICE_LOST) before any output. Not rerun (the GPU crash rule). No verdict, no "vs live" line.
- Cause: the pack's own specs carry 1051=1 (rt_scene_handout on in the release profile): every scene trace takes the
  TLAS address in the stamp s1b9 bytes 16/24, which holds the live game's address (0x2073200000). The gi_hit_direct
  packs never carried 1051, so this was new with the full-request contract. Validation also named s1 b19
  (gNativeTilesetSampler, 16 samplers), missing from kGiHitSunEm.
- Fix (this branch): frame_replay stamps the replay's TLAS whenever the effective 1051 is nonzero and --handout is not
  given (note "scene handout (constant 1051) on ..."); kGiHitSunEm gains s1 b19 (new packs capture it, this pack binds
  the default sampler). replay_pack_test, frame_replay_test, pt_reference_test, frame_replay_ls/rf_test pass.
- Next: after the merge, the offline steps of r2_tools/em_pack/predicted.json on the same pack. The rerun is the
  coordinator's / user's call (it is the command that lost the device, with its cause removed). Key stays 0.

## r2-inn-em-pack readiness fix (branch claude/em-pack-2, READY_FOR_LIVE, 2026-11-21)

- Run 065644 failed fast in producer-sanity-warmup: its check was the one-shot "hit emission: first hit-GI dispatch with
  the emissive scene" line, logged at load before the first D-EM build (that run logged "without"). The emission variant
  ran from 06:59:10 (raygen build ~80 s after load).
- Job: new em-ready phase before the pack, waiting (180 s) for "the hit-GI trace runs as a ray tracing pipeline, emission
  variant" and an "[em] last N builds ... emissive > 0" line; both in expect. live_flow 0 failing with and without --frozen.
- Core: replay_pack_pass=2 now waits while the dispatch has no emissive scene instead of refusing (it refused for good
  if armed before the first D-EM build). Tests: build.ps1 -Test 240/240.

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

- `gi_far_roof_emission`: live energy +0.25% (noise). Emission packs and PT emission built (claude/em-pack); the
  offline A/B replay lost the device and was not rerun (question above). Stays off.
- `rt_sun_overlap`: RT interval 7.53 / 7.50 / 7.32 ms, no gain. `rr_post_jitter_align=1` on High: 2-4x more background
  flicker (premise wrong). Ultra temporal shift (=3): no defect shown.
- High OMM downgrades (about 1 per 60 frames, reference scene, correct image): no pass waits for the commit in High;
  a fix changes the handout protocol. Left for later.
