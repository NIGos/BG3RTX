## OMM stayed off for the whole session under the scene handout (branch claude/omm-diag, offline verified, 2026-11-21)

- Symptom: final-s8-ultra-high 043643 failed the High OMM check, "compared 0, not compared 269494583". Not a limit of
  the diagnostic: OMM was off in every normal frame of both halves. All 92 `[rt] scene handout` lines say "OMM
  accelerated commits 0", and only the first window has "downgraded 1".
- Cause: at 04:37:07.397, 30 ms after the first 8 micro-map bakes, `[omm] rejected ticket use ... retaining until
  idle`. The handout (rt_scene_handout=2, default in High and Ultra since 2026-11-14) registers the accelerated slot's
  tickets on the game recordings that bound the handed-out set (`handout_tickets`). One of them had already ended
  recording. `omm::register_ticket_use` treats that as a lost provenance and latches `retained`. From then on
  `omm::available_for` is false: no rt_omm_dyn micro-maps, no accelerated TLAS, no proof, so every query traces the
  reference scene. The 2026-11-04 receipt that passed ran before the handout was the default.
- Fix (pure, layer): `omm::try_register_ticket_use` for the handout. An ended or untracked user recording is a plain
  refusal: the caller already downgrades that slot's traversal to the reference, and nothing references the ticket.
  A missing or retiring ticket still retains, as before. Other callers are unchanged.
- Test: `tests/omm_submit_state_test.cpp` checks that an ended or unknown recording refuses the ticket and is not tracked.
- Live to confirm (High S8 smoke): "OMM accelerated commits" above 0 in the normal handout lines, no "[omm] rejected
  ticket use", and the diag-omm window "compared > 0, not compared 0, violations 0".
