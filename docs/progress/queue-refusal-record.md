## Queue refusal and crash records (branch queue-refusal-record, kept, 2026-11-04)
- Preflight/lint refusals now write invalid results and the final stdout line with duration/cap; synthetic hv5: 1,109.5 s versus 900 s. Accepted jobs print their duration at start.
- Crash outcomes override INVALID and retain device-loss lines, phase/time and full dump paths. Focused synthetic queue/guard checks pass; broader child-permission/cancellation checks blocked by missing pwsh/py. No game, real queue or commit.
- Changed-key waits now gate measurement at motion-ready; shared generators add known pipeline signals, and lint rejects missing or unknown waits. Fresnel has no VALID timing sample; its signature comes from wsf1/source.
- Fixed stalled polls skipping settings/traces; 13 trace scheduling checks pass. Preflight charges VALID readiness medians, prints timeout allowances separately, and archives terminal REFUSED jobs only after recording them.
- Offline checks: 51 flow + 16 guard tests, 623 camera checks and 29 fake-runner permission checks pass. All 15 active jobs fail stricter lint and need regenerated/explicit waits; full audit: 103/104 fail. Reports: captures/offline/queue-refusal-after.{json,txt}.
- No game, real queue or Git metadata writes. Cross-shell child and cancellation checks remain blocked by missing pwsh/py.
