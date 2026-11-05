## Release smoke capture windows (branch relsmoke-fix, kept offline, 2026-11-05)

- Cause: make_final used frozen tightening. flow.retime -> flow.tighten -> duration_budget
  reduced energy_phase's 8 s to 3.8 s using stale capture-stall evidence. Current runner
  preflight requires more than 3.85 s, including serialized capture completion.
- Fix: regenerate with the runner's current VALID corpus through existing live_flow
  sizing. S8 energy windows are 3.9 s (minimum > 3.851297 s); Dialogue1 is 4.2 s
  (minimum > 4.1988 s). No preflight or gate was weakened. Regenerated
  rel1/rel2/rel3, final combined S8 and its stock-hair companion, manifests/predictions.
- Runner live_flow --preflight against main captures/queue/results.jsonl: 4 jobs,
  0 failing, 67 readable VALID logs. Rel1/rel2/rel3/final scheduled seconds:
  52.740 / 61.640 / 79.100 / 115.548. Readiness remains explicitly unproven where absent.
- Snapshot: only 3 readiness queries appended; every existing entry/model preserved.
- Tests: release_qual 6/6, batch_jobs_lint 12/12, live_flow 72/72. Added a regression
  checking generated energy windows exceed their serialized capture completion bounds.
- No shader/ABI/rendering change (variants delta 0); no GPU cost measurement needed.
  No commit, game launch or queue staging. READY_FOR_LIVE is not a quality pass.
- git merge main was denied by the filesystem at ORIG_HEAD.lock; HEAD already contains
  all of main (HEAD..main count 0), so checks used the current tools without merging.
