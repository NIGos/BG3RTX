## gi_far live +2.9% was the eye adaptation; energy gate now exposure-normalised (branch worktree-agent-a8879277994ee06b3, offline, 2026-11-01)

- Stage found: none of ours. The lighting is pre-exposed by s1b13[0]; in the gf2 ON shot the adaptation had raised it
  3.2% (EV 11.413 -> 11.367; camp +4.1%). The ON / OFF change is a uniform factor (R / G / B +2.91 / 2.93 / 2.94%,
  16 x 9 tiles median 1.030 with the OFF-OFF noise width). The gf jobs had no `qa_exposure_hold`.
- Derivation (docs/TRACK_C_GI.md "Live energy: the eye adaptation"): temporal (RR pass-through), gather, AO fold,
  SSS and compose are linear; the ao_direct give-back makes traced direct light independent of the folded AO. With
  a lower visibility only the bounce can add light, so no GI stage can flip -2% to +2.9%.
- Normalised by the trace frame's s1b13[0]: S8 scene colour -0.32%, diffuse -0.26% (OFF spread 0.21%), camp -0.14%
  (0.11%). The sign now agrees with the offline -2.0%; the size does not (open, see the next run).
- Fix of the test: `tools/m4_promotion_gate.py` divides scene colour by the `[rrpost] exposure probe` value of each
  trace frame (raw change and exposure ratio printed beside it) and prints a "Stages" table; CTest
  m4_promotion_gate_test (new in the CTest list). m4_promotion runs barely move (camp shot-on +0.25% normalised).
- `tools/live_batches/gi_far`: both plans hold the exposure from cost-off-a; gf2 (S8, one launch, 157 s) adds the
  stage windows: `diag-ls-off/on` (ao_direct give-back), `view-vis-off/on` (gi_debug=3, folded visibility, predicted
  ON / OFF 0.948), `view-bounce-off/on` (gi_debug=2, about 1.6). Dry run: all checks passed, 8.5 min, 0.6 GB.
  NOT authorized. No shader, core or default-path change.
- gi_far stays off. Next: run gf1 + gf2 when the user steers a batch; read `py -3 -m tools.m4_promotion_gate`.
