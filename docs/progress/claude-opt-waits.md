## RTX Hair single option, OMM dyn refusal churn, scene wait cause (branch claude/opt-waits, READY_FOR_LIVE, 2026-11-08)
- RTX Hair: "Strand hair on screen" (exp_hair_groom_primary) On now writes the user-tested set from the root
  bg3rtx_user.ini: the 5j bundle plus rt_hair_groom, shadow_t, ao_t, cascade_drop, fibre, fibre_gi, fibre_spec,
  fibre_mapped, rr_hair_guides, rt_hair_physical_origin. Tips, coverage, macro_normal, coverage_footprint stay off.
  Off in every preset. Restart keys: rt_hair_groom_primary, rt_hair_physical_origin, rt_hair_tangent_world (the note
  says so). Choice kv 12 -> 18. settings_test test_strand_bundle extended; schema regenerated.
- OMM dyn churn (growth mechanism): the backend's fixed 256 MB (micro-map storage plus kept scratch) filled at 682
  micro-maps; the cache never evicts, so every masked draw without a micro-map was retried every build: 3 dedicated
  VkDeviceMemory buffers, a bake dispatch, 3 "[fault] range of ours: OMM dyn" lines, then the refusal. Refusals grew
  with the foliage seen (6003 -> 10846 per 300 builds, ~36 per build); 111k log lines in 4 minutes; scene wait mean
  2.0 -> 18.2 ms and frame 25.9 -> 52.6 ms with GI/RT GPU ms flat. Nothing leaked (ranges are untracked on free).
- Fix: `rt_omm_dyn_retry_frames` (default 64; 0 = old per-frame retry, common/omm_dyn_refusal.h): a refusal of n
  triangles at backend live bytes L defers every bake of >= n triangles before allocation until live bytes drop or
  64 frames pass. The same micro-maps are built (the budget test is monotonic in size). Fault-range log: first 32
  lines per 600 frames, then one summary line per name ("[fault] ranges of ours not logged: ..."), common/log_budget.h.
  Unit test omm_dyn_refusal_test.
- Scene wait: CPU ordering, not a GPU fence: the lighting thread waits for the G-buffer thread's scene commit; the
  12 ms cap cannot hold because rt_after_capture holds g_mu for the whole recording. Overlap plan (pin the slot at
  capture begin and hand it out at once) in TRACK_D_SCENE_BUILD.md; not implemented.
- Expected: about 14-16 ms less scene wait in foliage-heavy late-session play (back to the 2-4 ms baseline), ~36 x 3
  fewer allocations and ~108 fewer log lines per frame. No shaders touched; no ABI change.
- Follow-up: backend scratch is 1.39 MB of the 256 MB (storage 254.57 MB over 682 micro-maps): freeing it fits about
  3-4 more micro-maps, so it was not done. `rt_omm_dyn_evict_frames` (off): LRU eviction of micro-maps unused for N
  frames and held by no per-draw BLAS, 64 per build at most; unit-tested (omm_dyn_refusal_test). Suggested A/B:
  600 against 0 in the same foliage walk.
- Live check (one session, the same walk through foliage, rt_omm_dyn_retry_frames 64 / 0 / 64 toggled live):
  `[omm dyn]` failed per 300 builds near 0 with 64 (vs thousands with 0), `[rt] scene wait` mean back to 2-4 ms,
  cache size and micro-map share equal across the toggles.
