# BG3RTX progress - 2026-11-21 (main 9ae2ede3: 0.9.6-beta candidate tested by the user)

## User decisions of 2026-11-21 (final for 0.9.6-beta)
- The user tested the candidate (main 9ae2ede3) and reported everything OK.
- The release-profile changes applied overnight are confirmed: `rt_hold_pack_base`, `rt_hair_header_early`,
  `rt_as_size_class`, `rt_cascade_keep_refused` on by default; `rt_hair_groom_async` in the RTX Hair On bundle.
- OMM in High: kept on (Ultra GI -0.33 ms, High +0.3 to 0.4 ms, image equal).
- `gi_far_roof_emission`: kept off (PT CLOSER, +0.7-1 ms GI/RT in Dialogue1). `claude/roof-em-on` stays unmerged.
- Player docs updated for 0.9.6-beta (claude/docs-096). Remaining: repackage so the archive carries the new docs,
  the Setup.exe install test (or ship the ZIP only), then publish (user action).

Level detection (2026-11-23 regression hunt): since Patch 8 the asset database never learns the level (zstd saves, no
Script Extender, vote only under `assetdb_join`), so sun replace, far-GI roof rules and per-light radii are inactive in
every log since about 2026-11-07. `assetdb_level_vote` (0) is ready with its live `tools/live_batches_staged/level_vote`.
Run it (it changes the image; the PT judges)? Taken: not run, offline only.

Live tests: allowed overnight by the user's /goal of 2026-11-21 only; a new session must ask again.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). Release check: [RC_CHECK](release/RC_CHECK.md).
Full text of the 2026-11-21 entries (every branch fragment of the night) and of 2026-11-20:
[archive 2026-11-21](archive/PROGRESS_CODEX_2026-11-21.md); older: [2026-11-20](archive/PROGRESS_CODEX_2026-11-20.md),
[2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md), [2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md).
Installed test app: 0.9.6-hairtest18 (commit 1c09e039), unchanged.

## Tracker forgets a destroyed pool's sets before the driver call (branch claude/fix-tracker-pool, pure fix, 2026-11-23)
- Cause (hunt2_diff S1): `layer/tracker.cpp` DestroyDescriptorPool (claude/final-layer) erased the pool's sets after
  the driver destroy. A pool handle reused at once by another thread's CreateDescriptorPool + AllocateDescriptorSets put
  its new set in the stale list, and the late erase dropped that set's tracked descriptors.
- Fix: the erase now runs before `vkDestroyDescriptorPool`, as DestroyBuffer, DestroyImage and DestroyImageView already
  do since r2-layer. There is no key: this is the same behaviour without the race.
- Test: `descriptor_epoch_test` pool_reuse covers two orders. Forgetting before the re-created pool's allocation keeps
  the new set. The old order shows the hazard: the new set is forgotten.
- No live test is needed. The window is a few microseconds, so no in-game check is possible.

## A stale level never stays current; sun resolve liveness row (branch claude/fix-level-clear, READY_FOR_LIVE, 2026-11-23)
- Cause (.scratch/coord/hunt2_lifecycle.md finding 2): nothing cleared the asset DB level once set, except turning
  `assetdb_level_vote` off. After travel or a save load the old level stayed current until a check found 3 or more
  lights of the new level. It stayed for good when the new level was not in the DB or had too few DB lights in view.
  With the vote on, core/sun_trust then had `roofs` true and 0 volumes around the new camera, so every pixel was
  trusted: an interior sun leak. The SE bridge kept the old level for a level name the DB lacks.
- Fix (layer/assetdb.cpp, behind `assetdb_level_vote` / `assetdb_join`, both 0: no image change today):
  - `assetdb_vote` gets the clear rule: with at least 3 voters, none for the current level and no qualifying switch,
    it clears (fail closed). Right after a level load, any lack of evidence clears it, no voters included.
  - A level load (a new G-buffer depth image, as hair_groom's LoadGuard) drops a light-list copy recorded before it.
    It re-runs the vote 30 presents later, not up to 240. New lines: "[assetdb] level load at present N", and
    "... after a level load: kept | switched | cleared" on the vote line.
  - `level_from_se`: a sub-level maps to its world. A level the DB lacks clears the current level.
- Tests: assetdb_test.
  - Inn level then the camp's lights: switched.
  - Level A, then only unknown lights: cleared.
  - Level A, then 2 lights of level B: kept in play, cleared after a load.
  - No lights: kept in play (no false clear), cleared after a load (no false keep).
  - The current level's own lights after a load: kept.
  - The SE bridge: sub-level BGH_SteelWatchFoundry_B gives CTY_Main_A; NoSuchLevel_Z clears.
- Liveness: row `sun_resolve` ("[core] gi: sun resolve pass GPU cost", silent_dead with detailed logging,
  hunt2_claims D3). It covers rt_sun_offscreen, ramp, rise, roof_pad and roof_up, which leave baseline_no_row.
  New field `follows`: ["level_known", "roof_volumes"]. A DEAD row whose cause row is DEAD in the same preset says
  "(follows ... DEAD)". The checker is VERSION 2. liveness_test gets a case for it.
- Staged (not queued): level_vote/lv-s8-ultra gains reload-on (F8, key on) and an after-reload window. It waits for
  the load line and an after-load vote that keeps or switches. It gates "replace 1, N roof volumes" N > 0 in every
  period. predicted.json updated. live_flow 0 failing with and without --frozen. A change to another level is not
  staged: the runner loads one save per launch.

## Player docs: three claims corrected (branch claude/fix-docs-claims, docs only, 2026-11-23)

- Ctrl+Shift+F7 only flips the live `rt_hair` key (`layer/util.cpp` 301-311): it switches RTX Hair off and back on
  after the launcher turned it On, never on from Off. README, packaged README, USER_SETTINGS, release notes, evidence,
  Nexus description and changelog now say so.
- Faster bounce light tracing and Bounce trace reordering act only inside the hit-GI trace, so only with Off-screen
  bounce light Near or Far (Ultra by default); on Low, Medium and High they do nothing. USER_SETTINGS says so.
- RTX Hair after a load: about 100 frames (96 in r4-inn, 116 in hx1a, 2026-11-21 logs), not about 70. Release notes,
  evidence and Nexus changelog corrected.
- Not changed (common/settings.h, being edited by another agent): the RTX Hair description shown by the launcher
  ("Turns on and off while you play, also with its in-game key", line 460) should say the key only switches it off
  and back on after it is On; the Faster bounce light tracing and Bounce trace reordering details (lines 338, 348)
  should say they act only with Off-screen bounce light Near or Far.
- The shipped README and USER_SETTINGS changed: repackage and rerun verify_rc before publishing.

## Liveness: every run proves each on-feature does work (branch claude/liveness, ready to merge, 2026-11-23)
- Why: OMM commits 0 for a week, "level unknown" for 2+ weeks, 17 rejected VT masks: every gate passed, because the
  gates check that lines exist or that A/B deltas are small, never that a feature that is on does work
  (docs/REGRESSION_PROCESS.md: 7 root causes, the plan, next steps).
- Spec `tools/live_batches/liveness.json`: 31 rows covering 41 keys. Each row has its `when` keys, the presets where it is
  on, and a positive, appear or absent observable. Checker `tools/live_batches/liveness.py`: it resolves each
  `[cfg] settings read` segment (preset, knobs line, truncated keys line, ini, schema) and judges ALIVE, DEAD or
  UNPROVEN per preset. It prints progress and caches results.
- Wired:
  - `scripts/test-queue.ps1` runs it after every run: lines in results.md, `liveness` in results.jsonl, and `<log>.liveness.json`.
    `"liveness": "gate"` makes a DEAD row INVALID, and `"off"` skips it.
  - The staged release smoke (release_final) gates on it.
  - Offline: `py -3 tools/live_batches/liveness.py captures/queue/logs`.
- Rule: AGENTS.md, plus `liveness_test` in CTest and merge-green tooling. A newly non-zero key needs a row or an exempt reason.
  173 older keys are grandfathered in `baseline_no_row`, which may only shrink.
- Result over the 32 logs of 2026-11-21:
  - OMM DEAD in 18 (all before the 05:00 fix) and ALIVE in 11-12 after it.
  - vt_masks_valid DEAD 29/29 launches, level_known DEAD 29/29, sun replace and roof volumes DEAD 25, per-light data DEAD 17.
  - The 20 work rows (GI, RF, LS, RR, FG, handout, water, hair, emissive...) show 0 false DEAD.
- Note: the level rows stay DEAD on main until `assetdb_level_vote` is on. So the gated release smoke fails on that defect now.
- Tests: `tests/liveness_test.py` uses real lines: a DEAD OMM log before 05:00, an ALIVE one after, level unknown,
  rejected masks, per-preset High dead under Ultra alive, and the rule. `release_qual_test` checks the gate.

## VT alpha masks: all 71 load again; OMM latch visible per window (branch claude/fix-vt-masks, ready to merge, 2026-11-23)

- Cause: LSLib pads a mip level below one tile (128 px) to the tile with opaque blocks. `tools/assets/vt_alpha_masks.py`
  stored those padded levels, and the layer's `parse_mask` (common/vt_alpha_masks.h) needs the exact chain.
  So 17 of the 71 masks of 2026-11-02 were rejected at every launch (`[vt alpha] ... invalid: skipped`). They cover
  309k of the 782k camp2 triangles, which stayed on the page-cache alpha path. This predates 0.9.5.
- Tool fix: each level is cropped to its exact mip size (`crop_bc4`; the content is the top-left blocks, and
  every outside block is the opaque pad, checked on 1_1ee1...).
- `--repair <dir>` crops existing padded files in place, bit-exact, without the game's paks.
- Shared data: the 17 files in `data/assetdb/vt_alpha` were rewritten with `--repair`. Count before and after:
  54 exact + 17 padded, then 71 exact. The asset DB still holds 998 files. Loaded masks: 43.8 MB (54) before, 80.1 MB (71) now
  (+36.2 MB of VRAM, BC4). On disk the 17 files lost 4-22 KB of pad each. `vt_alpha_manifest_camp2.json` still lists the old byte sizes (informational only;
  the layer reads the index).
- Tests: `vt_alpha_masks_tool_test.py` (crop, observed padded sizes, repair, folder writes only padded files,
  idempotent). `vt_alpha_masks_test` now also parses every indexed mask of `data/assetdb/vt_alpha` with the layer's
  own parse_mask (71 of 71). It skips when there is no asset DB.
- OMM guard (finding 4 of the layer hunt): when `retained` hides the `[omm]` lines, each window now logs
  `[omm] off for this session: retained until the device is destroyed`. Layer change, no ABI change.
- User check (next launch, High or Ultra): `[vt alpha] 71 masks loaded` and no `invalid: skipped` lines. In camp,
  `VT records per frame read an offline mask` should rise.

## Level detection by the runtime lights: assetdb_level_vote (branch claude/fix-level-detect, READY_FOR_LIVE, 2026-11-23)
- Cause (.scratch/coord/hunt_core.md F1): the asset DB never knew the level in a release run. Patch 8 saves zstd-compress
  SaveInfo.json (no save hint), the runtime-light vote ran only inside the diagnostic join (`assetdb_join`, default 0,
  in no ini), the SE bridge mod is not packaged. 171 of 171 queue logs: "sun stats ... replace 0, 0 roof volumes
  (level unknown: nothing trusted)". So rt_sun_replace never ran, Ultra's gi_far had every hit beyond R0 roof-blocked
  (screen and hidden hits; the far work ran and was discarded), and D-LS had no database lights.
- Change (layer, off by default, image unchanged): the vote is its own function (`assetdb_vote`, layer/assetdb.h),
  shared by join_check and a lean `vote_check` (copy -> voters -> vote, no CSV or statistics). New key
  `assetdb_level_vote` (bg3rtx.ini, common/settings.h `exp_assetdb_level_vote`, Advanced, developer, Live, needs
  Sun shadows ray-traced): every
  `assetdb_join_interval` (240) presents; turned on it checks at once; with both keys off it forgets a level it found
  (so 0/1/0 is an A/B in one run). Ties now pick the lower level index (was map order). Lines: "[assetdb] level vote #n",
  "[assetdb] level <L> (from lights)", "[assetdb] level cleared (assetdb_level_vote=0)".
- Core (log only): "sun stats" / "sun check" follow rt_stats_period_frames like the other stats lines (600 native).
- Tests: build.ps1 -Test 240/240. assetdb_test "level vote" fixtures from the database at the logged S8 and inn cameras (CTY_Main_A, with SCL_Main_A
  sharing the place) and the Dialogue1 camp (WLD_Main_A): float32 camera-relative round trip plus 24 unknown lights;
  the right level from no level and from a wrong one, no change when current, noise alone votes for nothing.
- Staged, not queued: tools/live_batches_staged/level_vote (lv-s8-ultra with OMM off and two gi_hit_sun_em packs for the
  PT verdict, lv-d1-ultra in the user's Ultra); predicted.json first; live_flow 0 failing with and without --frozen.
- Cost: one light-list copy every 240 presents. The consumers it turns on will cost GPU ms (sun resolve, far hits
  kept): measured by the staged live, the user decides with the numbers.
- User check after qualification: the log names the level "(from lights)"; at Water Queen's House on Ultra the sun
  under the house's roofs and the far bounce light change; Advanced > "Level detection by lights".

## Launcher Play on a fresh install, Experimental notice, RTX Hair restart key (branch claude/fix-launcher-play, ready to merge, 2026-11-23)
- Regression hunt (settings, launcher, package) against 0.9.5-beta cb2d03f2; report in .scratch/coord/hunt_tools.md.
- Play on a fresh install (no bg3rtx_user.ini) now writes the shown preset. Since instant apply (b657b2ba, 2026-11-14),
  Play wrote nothing, so the game ran bg3rtx.ini without `preset=`. The release policy then kept opacity micromaps and
  cutout masks off on High/Ultra (about 1 ms at camp). Before: 0 saves, rt_omm=0, rt_vt_alpha_masks=0. After: 1 save with
  `preset=high`, rt_omm=1, rt_vt_alpha_masks=1, and a second Play writes nothing (web_bridge_test).
- "Show experimental options" no longer lists "Custom value -> Off/Standard" lines or the false "(after a game restart)"
  for the four hidden opt-in options (web_bridge.cpp, the `also` loop skips -1 opt-in values).
- rt_hair_groom_nearest 0->1 needs a restart (key_change_needs_restart): it is a pipeline-creation specialization in the
  RTX Hair On bundle since 050328b5. The RTX Hair note and details now say that the nearest-strand trace waits for a restart
  when RTX Hair is turned on while playing.
- Not changed: Hair shadows and Detailed logging restart-banner gaps (same in 0.9.5; finding 4 of the report).
- How to check: delete bg3rtx_user.ini in a portable copy, open BG3RTX.exe, press Play: the file appears with `preset=high`.
  Turn on "Show experimental options": the notice has no extra lines.

## gi_far_roof_emission: PT verdict CLOSER, cost A/B FAIL, kept off (coordinator, 2026-11-21)

- PT A/B on the inn emission pack (rerun approved by the user): 1128 of 2.73 M GI pixels change, none brighter; on them
  mean |emission - PT| 0.406 -> 0.039, all 55 tiles closer. Replay fidelity to live: 94.4% of components bit-equal.
- Release smoke with the key on (claude/roof-em-on): S8 29/29, Dialogue1 15/15.
- Cost A/B in one run, Test Dialogue1 Ultra static (r2-d1-roof-em 080726): GI trace OFF 7.85 / 8.40, ON 8.85 ms
  (+0.73, noise 0.56: FAIL); RT passes OFF 9.64 / 10.31, ON 11.05 ms (+1.08: FAIL); energy -0.39% (ok).
  The inn A/B had equal GI ms. Kept off: a physically correct change on about 0.04% of pixels for 0.7-1 ms in an
  outdoor scene. `claude/roof-em-on` (default 1, smoke regenerated) is ready if the user wants it.
- Driver event 153 (nvlddmkm, "Error occurred on GPUID: 100") seen during one S8 run and outside any game (67 in 7 days
  on this PC): INFO, no Vulkan error in the game logs.

## Launcher UX polish pass (branch claude/launcher-ux, ready to merge, 2026-11-21)

- Review of every launcher page and state at 1180 x 760 and 736 x 620: `docs/LAUNCHER_UX_REVIEW.md` (13 findings,
  ranked; before/after shots in `.scratch/ui_review/`, not committed). UI only: no setting, default, preset,
  release policy, bridge, layer or core change.
- Rows with wide choices (Water lighting, Sun shadows, Indirect lighting) put the control under the text: the
  description no longer squeezes to two words per line.
- Restart badge: "Restart to apply", "Restart for X", "Restart for some choices"; "turn on/off" only for Off/On.
- Detail panel: paragraphs, Presets as four labelled lines with the current preset bold, no repeated "Off in
  every preset." in the details, dimmed cost source, "The game may pause briefly." where the apply text says so.
- One name: Hotkeys (Home card, Hotkeys page group, row line, detail heading).
- No Experimental badge on each row of the Experimental page; clearer switch copy; one primary button in the
  report dialog; header no longer jumps on preset apply; DLSS 5 placeholder in body text; sidebar 184 px below 860 px.
- Tests: launcher_ui_test.js updated for the new badge text and switch copy, plus checks for the new logic
  (badge variants, stacked rows, preset lines, details prefix). `ui_shots.js` audit: 42 pages, 0 layout problems.
- Check: open BG3RTX.exe, Surfaces > Water lighting (control under the text), any row's detail panel (Presets).

## gi_far_roof_emission against the PT with emission: CLOSER (branch claude/em-pack-result-2, 2026-11-21)

- Pack replay-20261121-071523-1 (inn, Ultra, OMM off, 1579 MB, frame 2077), test-main a4b740f8 binaries; the scene
  handout stamp now holds the replay's TLAS (no device loss). Emissive scene: 24 records, 98647 triangles.
- Fidelity, 1054=0 replay vs live giRaw: bit-equal 94.43% of components, mean |diff| 0.0105 (live runs the raygen pass,
  the replay compute).
- 1054=1 changes 1128 of 2731520 GI pixels; 0 raised, giRaw.a unchanged. On them giRaw luminance 0.4425 (off) -> 0.0042
  (on), no-emission 0.0020; emission term 0.4405 -> 0.0022 against the PT's 0.0379 (hidden first-hit emission x fade,
  casters as roofs).
- Mean |E - PT| on the changed pixels: 0.4061 (off) -> 0.0386 (on); 55 of 55 tiles closer. PT at 64 iterations: 0.4059
  -> 0.0385, same verdict.
- Whole frame (offline, one frame, no denoiser): mean giRaw luminance -2.98% (0.006083 -> 0.005902): the dropped
  emission is a few very bright hidden hits seen through the cut roof. Live r2-inn-roof-em: energy +0.25% (within noise),
  GI GPU ms equal.
- Verdict per predicted.json: CLOSER. Default change prepared on claude/roof-em-on (not merged). Summary:
  claude-em-pack-result-2.json; the pack and the replay outputs are deleted.

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

## Overnight run: release profile changes, confirmed by the user 2026-11-21 (coordinator, merged)

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
  RT -0.14..-0.33 ms (noise); High RT +0.14..+0.20 ms and GI +0.13..+0.22 ms; energy +0.07% / +0.02%. Kept on in High (user, 2026-11-21).
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

- `gi_far_roof_emission`: live energy +0.25% (noise). Emission packs and PT emission built (claude/em-pack); the PT
  A/B then said CLOSER, the cost A/B FAIL (section at the top). Kept off by the user 2026-11-21.
- `rt_sun_overlap`: RT interval 7.53 / 7.50 / 7.32 ms, no gain. `rr_post_jitter_align=1` on High: 2-4x more background
  flicker (premise wrong). Ultra temporal shift (=3): no defect shown.
- High OMM downgrades (about 1 per 60 frames, reference scene, correct image): no pass waits for the commit in High;
  a fix changes the handout protocol. Left for later.
