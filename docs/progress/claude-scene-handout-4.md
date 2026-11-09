## Scene handout increment 4: OMM on reserved slots; hit materials analysed (branch claude/scene-handout-4, offline-verified, 2026-11-09)
- OMM: a reserved slot keeps the OMM accelerated TLAS in stamp byte 24 when `bvh_omm_tickets` (layer/bvhsplit.cpp) gives
  its micro-map tickets; the commit registers them on every recording that bound the handed-out set (`Machine::accelerate`
  hands back users and their command buffers), later joiners register in `rt_scene_handout`. A recording that already
  ended cannot hold tickets: byte 24 goes back to the reference (monotonic). Handout line: `OMM accelerated commits A,
  downgraded D` appended (existing gates unchanged).
- Native hit materials: still refuse the handout. Exact cause and the increment 4b plan (query built at the commit in the
  G-buffer command buffer, per-slot query set, camera check at the submit settle) in docs/SCENE_HANDOUT_DESIGN.md.
- Legacy single-BLAS path: left on the waiting path (shared TLAS, never fills a reservation).
- Tests: scene_handout_test cases 14 (accelerate: users + handles, only the committing slot, joiners, reset on close and
  on a new reservation) and 15 (ticket lifetime: an ended user forces the downgrade); 223/223 CTests. No shader, ABI or
  variants change. Key off identical (no reservation: nothing new runs).
- Live job sh2-s8-scene-handout unchanged in plan (Ultra without hit materials, OMM on): predicted.json gains the OMM
  observables (accelerated commits > 0, downgraded 0, RT GPU ms equal to OFF). Not run.
