## S6 m4p3 non-finite alarm: the game's depth, not the bundle (branch worktree-agent-ad96013fbc66f09f7, offline, 2026-11-01)

- The `nonfinite 8692` of shot-step-gi was the gate summing every dump of three traces: OFF-a 2917 +
  step 2865 + OFF-b 2910, all in the game's own D32F 2048x2048 depth of pass `76433aef` (also with every
  RT key off). The measured scene colour `5c3fdabc` has 0 non-finite values in all 8 S6 traces.
- `tools/m4_promotion_gate.py`: scene-colour count per trace, other dumps listed per trace and marked
  game-owned or ours. Test case in `tests/m4_promotion_gate_test.py`, now in CTest.
- The darkening (step-gi -1.05%, sun -1.82%, inst -3.10%) stays open: not NaN in the measured buffer.
- Guard in the hit-GI variants only (key-off SPIR-V byte-identical): non-finite or negative hit term
  -> fallback0 (counter 417); mean bounce above the RGBA16F max -> 65504 (counter 418), so no +Inf can
  reach gi_composite's revert (which would drop ~36 px of bounce and keep the occlusion: darker).
  Logged on the `GI hit direct` line. Kept, off with the keys; cost: two predicated counters.
- Rerun m4p3: read `non-finite hit terms (fallback0)` and `bounce over the half-float max` in diag-on.
  Both 0: the darkening has another cause (next: the hit-GI alpha set vs b.alpha, scene drift).
