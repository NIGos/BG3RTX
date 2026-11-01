## Hit-material per-draw hooks: lean path (branch hook-cost, READY_FOR_LIVE, 2026-11-01)

- sw3 21:55 (recycle 1, capture_unlocked 0/1): every pan timeout in phase "prepass recording"; capture end to commit
  ~1.1 ms. `[cpu] hit material` in pans: prepass hooks 2.7-3.2, G-buffer material hooks 2.5-3.5, query 0.9 ms/frame;
  inside: collect 1.3-1.5 ms, 1.2-1.8 allocations (31-40 MB) and 1.2-1.9 frees per frame (a free ~0.6-0.75 ms).
  capture_unlocked=1 did not remove the timeouts.
- `rt_hit_material_lean_hooks=0/1` (live, default 0): no vkFreeMemory on a recording thread (evicted / idle buffers
  freed at present), best-fit reuse (`take_fit`: same usage/memory, size within [need, 2 x need]; moving query sizes
  no longer allocate + evict), collect() once per frame (forced only at the packet/query bound), the G-buffer draw key
  only for draws that qualify (the family/pipeline-declined majority skipped it), prepass candidate vector reserved.
- New split on the `[cpu] hit material` line: draw keys and mutex waits (inside the hook phases), `lean_hooks N`.
- Expected: frees and allocations off the hooks (~1.3-1.6 ms), collect ~0.1 ms; the rest of the hook time is the
  per-draw key (bindings, mapped reads, buffer sizes) and the join: the new split says whether the next cut is there.
  Target hooks < 1 ms total and 0 timeouts in pans: needs the live A/B, not yet shown.
- Job sw3-motion-s8-fg2 now A/Bs lean_hooks 0/1/0 (capture_unlocked 1). 93/93 CTests, lints, dryrun pass.
