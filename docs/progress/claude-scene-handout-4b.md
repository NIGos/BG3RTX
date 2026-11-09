## Scene handout increment 4b: native hit materials under the handout (branch claude/scene-handout-4b, READY_FOR_LIVE, 2026-11-09)
- The hit-material query of a reserved slot is built at its commit (`native_hit::commit_query`, layer/hit_material.cpp):
  `register_use(..., commit=true)` on the G-buffer command buffer (row uploads, join/remap/gather recorded there, before
  the lighting in the same submit) into a per-slot handout query set (`handout_set`, update-after-bind pool, allocated
  at the reservation). Frame/generation checks unchanged (slot frame and generation at the commit, row header on the GPU).
- The lighting's hit-material passes (rt_hit_material_use with the dummy TLAS and set R) get that query set at once and
  record the camera they bound; the submit settle checks it against the query's camera. Mismatch (kCamera) or no query at
  the commit (kQuery: packet missing, no exact mapping, capacity, recording domain) resolve to P: set P and P's live query
  set of its own generation (else P's plain set, whose rows header rejects every row) copied into R's; R poisoned.
- Lifetime: in order, every user recording owns the commit-time query (recycle collect retires it only when the G-buffer
  and all lighting recordings are GPU-done); on a fallback the users own P's query.
- Tests: scene_handout_test cases 16 (query failed at the commit), 17 (camera mismatch), 18 (query built, state reset per
  reservation, other slot ignored), 19 (recycle ownership model); 223/223 CTests. No shader, ABI or variants change.
  Key off identical (no reservation: register_use runs as before; commit=false keeps every check).
- Handout line gains `hit material queries at the commit Q, failed F (fallbacks: query X, camera Y)`.
- Live job sh2-s8-scene-handout now runs the real Ultra preset (hit materials + OMM on); predicted.json first; live and
  frozen lint 0 failing. Not run.
- Risk to watch first in the live: the commit-time query requires the G-buffer command buffer to be a one-time recording
  (`one_time` in register_use); if not, every query fails and every handout frame falls back (fallbacks query = frames).
