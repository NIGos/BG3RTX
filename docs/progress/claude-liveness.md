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
