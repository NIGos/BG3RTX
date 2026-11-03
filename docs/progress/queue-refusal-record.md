## Queue refusal and crash records (branch queue-refusal-record, kept, 2026-11-04)
- Preflight/lint refusals now write invalid results and the final stdout line with duration/cap; synthetic hv5: 1,109.5 s versus 900 s. Accepted jobs print their duration at start.
- Crash outcomes override INVALID and retain device-loss lines, phase/time and full dump paths. Focused synthetic queue/guard checks pass; broader child-permission/cancellation checks blocked by missing pwsh/py. No game, real queue or commit.
