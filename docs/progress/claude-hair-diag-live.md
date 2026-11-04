## Strand diagnosis preparation (branch claude/hair-diag-live, READY_FOR_LIVE, 2026-11-04)

- Split hv5 reload stages from hv6 first-load/order 0/1/0/1/tips/coverage diagnosis.
- Per-launch plan + readiness: hv5 668.3 s, hv6 613.3 s; both below 780 s / runner 900 s cap.
- Every swapped draw must meet the logged coverage bound. Low coverage retaining cards is safe.
- Conditional participation/cost uses complete coverage periods; no-swap does not require GPU cost.
- Eight consecutive-frame bursts and three PNG traces compare the same player-head close-up;
  three cards repeats derive the edge-flicker envelope. Full energy/MV/PT quality remains unqualified.
- hc2 had 300 G-buffer draws/period and no GPU cost: publish-owned timestamp reset can miss
  G-buffer CPU recording; logs do not identify query availability. Layer fix remains separate.
- Reached hc2 scene-wait windows pass; missing later windows followed fail-fast aborts.
- No game, live queue submission, commit or Git metadata modification. Predicted.json still needs
  committing before a future authorized live. Offline verification receipts are scratch-only.
- Final offline checks PASS: queue_gates, hair_diagnostic (4), hair_validation (17), live_flow (43),
  batch_jobs_lint (2), both fake-queue branches and shared lint. Dry-run estimate 25.4 min / 1,430 MB.
- Scratch receipt: %TEMP%/bg3rtx-hair-diag-20261104-final/live-flow.json; fake receipts grant no live authority.
