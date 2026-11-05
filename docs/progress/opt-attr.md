## Readiness contracts and GI attribution (branch opt-attr, READY_FOR_LIVE, 2026-11-05)

- Fixed shared readiness selection: explicit CPU/push-value contracts generate no pipeline waits.
  Compact holds, scene-build policy and GI ray count no longer reconcile unrelated generations.
  Unknown keys require explicit evidence instead of an invented shader completion/quiet interval.
  Disabled box/exact-k passes have no completion requirement; rt_build=auto is emitted resolved.
- Regenerated opt_r1/scene_holds. Preserved its existing CPU proof and qualification status.
  Layer-only repeat toggles have no pipeline waits; real Ultra/High request changes retain theirs.
  Added 17 readiness queries to the frozen lint snapshot; all prior entries and other data preserved.
- Prepared tools/live_batches/opt_r2/gi_attr: one static Ultra inn launch, detailed_logging=1, dev=1.
  Six single-key OFF / ON / OFF brackets: hit direct, sun, emission, far bounce, second ray, material.
  Each pair keeps other Ultra dependencies on. OFF is pair-specific; an all-disabled common baseline
  would make sun/far/material attribution inert. Costs are conditional and must not be summed.
- 18 measured windows, 120 frames each, timer period 60. Only per-window GI trace cost gates.
  Signed attribution = ON minus the adjacent OFF mean; report both deltas and OFF drift.
  About 142.4 s after load, 300 s hard watchdog, no captures, estimated logs 5 MB.
  Reference Ultra inn trace is about 13.6 ms; no new measured cost or quality result is claimed.
- Offline: live_flow 72 tests and batch lint 12 tests pass; both generated batches lint cleanly.
  Shader request delta 0. No shader compile needed. No commit, queue staging or game launch.
  Pending: authorized live evidence only. This is a diagnostic job, not a rendering candidate.
