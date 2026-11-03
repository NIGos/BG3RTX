## Strand fail-safe strike rule (branch claude/hair-strike-rule, 2026-11-03)
- hv5 evidence: every strike event struck every groom of its counted frame at once (5 of 5 at 20:45:32 and 20:46:17,
  3-4 in warm-up); one groom shadowing another cannot do that (it hides only a fully covered groom). The code allows a
  frame misalignment: the build (and publish) is recorded at the end of the prepass, the G-buffer in another pass the
  game may submit in another order, so the record carries an adjacent frame's tag and the count sees 0 for all.
- Fix: the count pass also counts strand entries sealed with tag - 1 / tag + 1; a frame with only those is no
  evidence (logged "carry an adjacent frame's tag"). Grooms sharing a draw key (same hairstyle twice) are summed.
  A strike retires a draw for 600 frames (doubling to 9600), not for the session. Unit tests in hair_groom_asset_test.
  predicted.json (hair_validation, hair_crowd) names what confirms or refutes the hypothesis in one run.
