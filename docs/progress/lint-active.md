## Active cadence lint (branch lint-active, kept, 2026-11-04)
- Real: go2-s8, go2-s8-medium and fresh-corpus smoke_ultra, inherited by combined S8 part 1; shared retime raises cost 23.4 -> 24.0 s. hv3, wf9, wo1, wsf1 and combined part 2 were false positives: readiness already pays settings latency; exact VALID wsf1 064436 fixture preserves straddle/stall checks.
- Refreshed frozen corpus (89 -> 86 readable VALID logs; three disappeared during verification); active 26/26 and CTest batch_jobs_lint pass. Offline only; no game, queue or git metadata writes.
