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
