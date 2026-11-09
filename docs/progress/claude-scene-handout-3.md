## Scene handout increment 3: handout without the scene wait (branch claude/scene-handout-3, READY_FOR_LIVE, 2026-11-09)
- `rt_scene_handout=2` (live; needs `rt_scene_handout_layout=1` at startup; not with `rt_hit_material`): the capture
  begin reserves the slot R the build will write (P = the current scene); the lighting's first RT query binds set R and
  the dummy TLAS without waiting and without the scene lock; spec-1051 shaders trace the TLAS address the build writes
  into R's stamp. At every vkQueueSubmit the layer resolves an open handout before the driver call: in order (R's build
  earlier in the batch or submitted) or the fallback (set P copied into set R, P's traversal address downgraded).
- common/scene_handout.h: the state machine (reserve, hand_out, commit, resolve, pins). tests/scene_handout_test.cpp:
  13 scripted cases (in order, lighting first, build failed, order, committed first, no capture, dropped, toggles,
  second capture begin, extra users, retirement, older frame, stale reservation) and every order of capture begin /
  lighting query / capture end with three submit splits, three frames each; the safety model (traced scene's build
  executed, slots never reused while bound) catches 13 violations when the fallback copy is removed (mutation control).
- Lock split (layer/rtscene.cpp): `g_handout_mu` is a leaf for the lighting thread (never `g_mu`); `g_wait_mu` for the
  wait line. Order g_mu -> g_wait_mu -> g_handout_mu. `slot_retired` keeps R and P while the handout is open.
- Core: `prepare_omm_traversal` lets a pass run on the handout only with a constant-1051 pipeline and a registered command
  buffer; otherwise it settles once per frame to the committed pair (rt_scene_set wait) and a pipeline without the
  constant keeps the next two frames on the waiting path. Water settles before `water_rf` (it needs the real TLAS).
- Host ABI 29 -> 30 (`Bg3HostApi::rt_scene_handout`): rebuild both DLLs. No shader change (variants.txt delta 0).
- Logs: the wait line gains `, handed out N (0 ms)`; `[rt] scene handout: ... fallbacks F (not committed, build failed,
  order, dropped), pairing mismatches M ...`; `[rt] scene handout fallback: ...`; `[core] scene handout: ...`.
- Limits (increment 4): OMM acceleration off for reserved slots (tickets per user recording), native hit materials
  refused (their query reads commit-time rows on the CPU), legacy single-BLAS builds never fill a reservation.
- Key off: no reservation, no submit work, wait line unchanged. Tests: 223/223 CTests (new scene_handout_test).
- Prepared, not run: tools/live_batches/rel095_verify jobs/sh2-s8-scene-handout.json (S8, uncapped, focused, 0 / 2 / 0
  in turns and static windows, check windows with rt_primary_check), predictions in predicted.json.
- Live job lint: live corpus 0 failing, frozen snapshot 0 failing after extend_readiness (5 queries) and 8 new regexes
  added with 0 matches (a full lint_snapshot refresh would shrink the corpus 79 -> 27 VALID logs: not done).
