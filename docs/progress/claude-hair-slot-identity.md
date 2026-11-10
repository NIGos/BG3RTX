## Stable character slots for same-style hair, FG check staged (branch claude/hair-slot-identity, READY_FOR_LIVE, 2026-11-10)
- hs1 015416 confirmed 5q: key 8c68 (two rows) lost both rows in 6 frames, 5 of them on a slot jump frame; jumps only
  on that key, in pairs. Not the owner test (one key, one draw slot): the trace wrote no strand entry for either row.
- New key `rt_hair_slot_stable` (off; launcher "Strand hair: stable character slots", Advanced): the occurrence index
  of a groom-joined hair draw follows the character's placement (model matrix read on the CPU at classify) instead of
  the draw order; a character alone keeps its slot. Layer only, no shader variant (delta 0), no ABI change.
- Counter line "[hair groom] slot exchanges resolved": draws kept against the draw order, captures swapped, captures
  with a draw missing or new, groups left in draw order. Cost: CPU only.
- Unverified until a live: that the two characters' hair draws carry different model matrices. Gate: key on, resolved
  > 0 and 8c68 dropouts and jump pairs 0; key off in the same run reproduces them.
- Test: hair_groom_asset_test (swap, both moving, one leaving, return, third character). Build and CTests: 224 of 224 (settings_test after regenerating data/settings_schema.json).
- Frame generation (user: heads switch on and off quickly, numbers clean): FG gets per-frame binary strand depth and
  motion vectors (they follow the jitter), under a stable colour; `fg` is live-toggleable when started with fg >= 2.
- Staged, not queued: tools/live_batches_staged/hair_fg `hf1-s8-hair-fg`, 98 s: windows fg=3, fg=0, fg=3, fg=3 with
  rr_hair_strand_aa=0, fg=3 with cards, 8 s each plus 12 presented frames. The user says where the toggling stops.
- Details: docs/RTX_HAIR_PRIMARY.md 5s.
