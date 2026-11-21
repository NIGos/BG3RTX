## rt_hair_groom_async in the RTX Hair On bundle (branch claude/hair-async, applied overnight 2026-11-21, confirm or revert)
- Bundle change: `rt_hair_groom_async=1` in the On choice of `exp_hair_groom_primary` (common/settings.h, 27 keys), its
  settings_test list and count, the bg3rtx.ini bundle comment, data/settings_schema.json. The rt_hair live switch gates
  it with the rest of the bundle (layer/util.cpp hair_gated). Code default stays 0; RTX Hair Off is unchanged.
- Evidence: live r4-inn.1.030500 (inn, 0/1/0): outward dolly p99 98-111 -> 40 ms, scene wait max 163-167 -> 6-7 ms,
  every ON decode on the worker, energy within the OFF trace range (firelight, not the key). Its '[hair groom] frame'
  lines: kept on the built LOD while a decode is pending 54-59 groom-frames per 120 frames in the ON outward dollies
  (OFF 0-24), groom dropped 0 in every window.
- Live staged (not queued): `tools/live_batches_staged/hair_async` hx1a-s8-hair-async, hx1's S8 plan and gates with the
  key from the start, about 99 s of phases. Added gates: every decode of the moving windows on the worker, '(groom
  dropped)' 0 in the bundle windows; reported: kept groom-frames, decodes per window, hx1's card runs. hair_stable's
  bundle copy gained the key (its check_bundle reads the schema). Lint with and without --frozen: 0 failing.
- Decision rule (predicted.json): confirm if every hx1 gate holds, no groom is dropped and the card-run median of
  entering heads stays at most 6 frames (202905: 3); else revert the bundle line, the key stays READY_FOR_LIVE.
- Live hx1a-s8-hair-async.1.033523 (S8, 2026-11-21, done, 30 every-window gates ok): CONFIRMED by the predicted rule.
  Bundle windows (in-on, out-on, orbit-r, orbit-l, in-on2, out-on2): 0 strands->cards, 0 frames on cards, watchdog 0;
  card runs of entering heads median 3 in all six (max 3; hx1 202905: medians 3/3/6/3/3/3, max 9). Groom dropped 0 in
  every window; kept on the built LOD 3 (in-on) and 4 (orbit-r) groom-frames, 0 elsewhere. 20 decodes, all on the
  worker (warmup 10, pre-in 8, orbit-r 2). Keep-off control 10/10/10 and 10/10/130 (202905: 1/9/10, 10/10/72).
- Cost seen outside the gated windows: a head whose groom is decoded for the first time stays on cards 18-21 frames
  (pre-in, 3 runs, 30 fps: about 0.6 s) and 18-25 at the load (warmup, 5 runs). Predicted "at most 3 frames more"
  was wrong: the one worker decodes serially, also LODs the dolly no longer wants (16fa158ca4825f93 LOD 0, 2, 1 in
  0.4 s). Without the key that time is a frame freeze instead (r4-inn: 100-220 ms frames). No old pre-in numbers kept.
  Follow-up (not done): drop queued decodes nobody asks for any more, or decode the newest request first.
- Round 3 (layer stream, no key: scheduling only): a queued decode is a wake-up; the worker decodes what
  `DecodeSlots::take` names, the newest request still wanted (asked in the newest frame or the one before), and forgets
  queued requests nobody asks for (common/hair_groom_decode.h, layer/hair_groom.cpp). CTest hair_groom_decode_test
  (44 checks) replays the pre-in sequence: LOD 0, 2, 1 asked while the worker is busy -> only LOD 1 decodes, and before
  an older still-wanted request. Predicted for the hx1a rerun (predicted.json rerun_round3): pre-in card runs median
  12-18 frames (from 19), pre-in decodes 6-8 (from 8), bundle windows unchanged.
