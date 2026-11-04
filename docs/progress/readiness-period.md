## Periodic readiness (branch readiness-period, kept offline, 2026-11-04)
- Runtime and preflight use the shared producer period plus settings latency/margin; fresh observations follow acknowledgement. Wsf1 phase 3: 2.0 s bound -> 6.448 s; replay ready at +2.494 s.
- Six historical replays and 62 unit/lint tests pass; affected Fresnel plan and lint reports regenerated. No game launch, real queue writes, Git metadata changes or shader variant changes.
