## Gate audit: every live gate replayed on the logs; energy gate made two-sided (branch worktree-agent-a67751e8c0a92555a, offline, 2026-11-01)
- docs/GATE_AUDIT_20261101.md (pointer in META_AUDIT_20261101): 70 gates, 30 OK / 28 weak / 12 broken before,
  43 / 24 / 3 after this branch. Left broken: GI R3 "0 negative px", M5.1 violations, rf_hit_shade "within 9%".
- `tools/m4_promotion_gate.py`: energy two-sided against `<batch>/predicted.json` (committed before the run,
  else "post hoc"); band t x sigma x sqrt(3/2), sigma from OFF-OFF pairs of every lean run on the save (S8
  0.458%, S6 0.390%); nearest-OFF comparisons; non-finite from the scene dump only; plan drift recovered from
  git history or failed; exit 1 on failure; capacity above 0 fails. The old OFF-pair rule flags 46% of noise.
- `scripts/queue_gates.py`: `"event": true` (one-off lines per window) and plan drift. Jobs: m4p1-4 a written
  trace per shot window; m5w4-6 compose per window; m5w5 sum check >= 99% and ratio within 0.1%; m5w6
  refraction per window; pl1 forbids failed BVH audits.
- New readings: m4p3 S6 (plan eeca1fd) bundle steps -1.05 / -1.82 / -3.10% with `rt_emissive=0` (band 1.73%):
  unexplained darkening. m5w4 +2.20% / +2.75% for a light-removing key (band 1.17%). gf1 +3.97%, gf2 +2.87%
  vs predicted -2%: fail. m4p2 capacity: alpha made opaque 0.03 per frame. m5w5 `clamped 46` in one window.
- Tests: m4_promotion_gate_test (added to CTest), queue_gates_test. Dry runs m4_promotion, m5_water, pool_lag
  passed. Before queuing m4_promotion: fill `tools/live_batches/m4_promotion/predicted.json` and commit.
