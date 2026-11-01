## Energy recheck at a fixed exposure (branch worktree-agent-a80bee797afaacedd, offline, 2026-11-01)

- Merged 17e82e1 (exposure-normalised gate) and 9629658 (gate audit: two-sided band, plan recovery);
  `tools/m4_promotion_gate.py` now does both: scene colour / s1b13[0] first (energy and OFF-OFF noise pairs),
  then the two-sided band. Table: docs/GATE_AUDIT_20261101.md section 7.
- No run today held the eye adaptation. Verdicts that change: gf1 camp +3.97% -> -0.14% (FAIL -> within);
  gf2 S8 +2.87% -> -0.32% (sign of the offline -2.0%, still FAIL vs -2.0%); m5w4 +2.75% / +2.20% ->
  -0.12% / -0.55% (FAIL -> within, darker as a shadow must be).
- Unchanged: S6, S7 / inn and S14 kept one exposure in every trace (ratio 1.0000). The m4p3 S6 bundle
  darkening (-1.82 / -3.10%) and bx1 -2.31% are real. lr1-3 have no energy gate (one OFF shot).
- 17 energy-gated lean plans hold `qa_exposure_hold=1` from the first cost/shot phase to `restore`; jobs
  expect `exposure held` / `hold released`. Lint `queue_gates.py --lint-exposure-hold` in queue_gates_test.
  Dry runs of nine batches pass. Not authorized; no live run.
- Open: the S8 OFF-b state change (m4r1 +1.3-1.7% at a fixed exposure); predictions for m5w4 and bx1.
