## Automatic pipeline readiness (branch queue-refusal-record, kept offline, 2026-11-04)

- Shared readiness generation: 25 active jobs pass lint/preflight; 84 historical jobs inactive. Combined S8/camp split; maximum scheduled + VALID-median wait 876.510 s. Per-job before/after: tools/live_batches/readiness_report.json; hv1-hv5 used (no local hv6).
- Nine mappings explicitly use conservative generic shader completion; timing/quality remain unproven. Six extraction dryruns and 147/147 CTests pass (144.19 s); stale settings executable rebuilt and old timeout-cap assertion corrected. No game, queue submission or commit.
