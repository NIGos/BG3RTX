## Tileset qualification jobs (branch ts-fix, READY_FOR_LIVE, 2026-11-04)

- ON diagnostic enables rt_debug=9 with rt_stats=1 to populate Tileset counters;
  cost, shot and restore explicitly reset debug to 0. No rendering/ABI changes.
- Unknown kind 5 records retain UNKNOWN/aborted fallback (missing/unsupported
  inputs); report their count. Gate complete > 0, capture-incomplete 0 and
  positive qualified family2/native primary participation.
- OFF 175623 cost already contained RT 9.242 ms / GI 11.583 ms for 60 frames.
  Unconditional first-sample removal caused INVALID; shared reset-aware
  selection now retains the first complete cost and diagnostic periods.
- Regenerated jobs/plans: 2/2 preflight lints pass, 90 unittest checks plus
  standalone queue/analyzer checks pass. No frozen corpus changes needed.
  No live queued or launched; rendering quality and new ON cost remain pending.
- User check: authorized ts1 OFF/ON on Test Dialogue3, inspect retained PNG pair
  and descriptive float energy/coverage/cost deltas; no quality pass claimed.
