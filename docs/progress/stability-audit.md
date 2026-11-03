## Long-session and device-loss audit (branch stability-audit, offline verified, 2026-11-03)

- Audited 87 queue logs plus the user log; no provable VRAM leak. Water BLAS peak 2211 -> 437 MB; mesh remains capped at 2047 MB.
- Fixed 14 silent device-loss results in 6 layer files; all 19 submit/wait sites audited. No ABI/settings/shader changes; variants delta 0.
- Evidence: docs/evidence/long-session-audit-20261103.md and CSV; missing live counts/CPU timing and shader-edit CPU retention have exact deciding observables.
- Validation: full build passed (55 s); 147/147 CTests passed (198.64 s). No game launch, live qualification or Git metadata writes.
