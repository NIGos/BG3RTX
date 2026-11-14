## Consolidation, stream A (host layer): stutter and memory from the user's play logs (2026-11-14)

Evidence: the installed app's `bg3rtx.log` / `bg3rtx.prev.log` of 22:12-22:27 (Ultra, FG 2x, RTX Hair On, 3840x1600)
and the live logs hq-s8-ultra 215258 and hq-inn-fix 211816. Offline only. No shader or ABI change.

Fixed (branch `claude/consol-layer`):
- Elastic capture growth. The RT and cascade captures grew in 16 MB quanta, each growth a whole new buffer with the
  old one kept for four frames: 22:15:10-23 has ten RT reallocations of 1.4 to 1.8 GB for +16 to +112 MB, and the
  VRAM guard went LOW twice with old + new allocated ("RT capture 3520 MB", "3615 MB"). A growth is now at least
  1/8 of the size when the budget allows it (`capture_budget::elastic_growth`, `vram_fits`): that sequence takes
  3 reallocations and ends at 2016 instead of 1840 MB; no shrink back. CTest `rt_capture_elastic_test` replays it.
  Cost in ms of one reallocation: unmeasured (needs a live window).
- `rt_hair_groom_async` (new key, default 0, live). Groom LOD read + LZMS decode ran inside the scene build:
  228 decodes in the two sessions, median 47 ms, max 203 ms, 15.6 s in all (179 were repeats after the Ctrl+Shift+F7
  toggles and evictions). With the key they run on a below-normal worker (at most 4 outstanding); the draw keeps
  its cards a few frames longer. CTest `hair_groom_decode_test`. READY_FOR_LIVE: decode ms leave the build
  (`[rt] scene wait` max, `[pace]` max in a town walk); card runs of entering heads must stay within the hx1 gate.
  Stream C: add `rt_hair_groom_async=0` to `bg3rtx.ini` and regenerate the defaults.
- Shader source scan (`shader_poll`, present thread, 160 files): every 5 s with dev=0, 250 ms with dev=1 as before.
  About 0.3 to 0.8 ms per scan (Python proxy 0.76 ms), 4 times a second before.
- Streamline: `g_layout` / `g_input_layout` kept the layout of a destroyed FG input rotated out of the watch ring
  (stale layout for a reused handle, one entry per image). Erased with the other tag metadata.

Found, not fixed (ranked):
1. VRAM. Ours is 10.2 to 12.6 GB of 23 to 27.5 GB used (budget 30 GB); 26 hitches over 100 ms in 10 s at 22:19:16
   start with a Streamline `waitCPUFence` timeout at 26.5 GB. Largest parts: cascade capture 3 x 1088 MB for a
   510-580 MB demand (the rise reserve never decays: one camera cut of about 500 MB is kept for the session); RT
   capture 2047 MB for about 900 MB; BLAS chunks 2.6-3.9 GB reserved for 0.8-1.3 GB live (`rt_as_cap_occupied`
   is the staged answer); mesh pool 1947 MB with 640-920 used (shrink deferred by "native hair ranges"). The 88-byte
   capture stride of RTX Hair applies to every draw (1408 MB start instead of 1024). All need an A/B: policy, not bugs.
2. Hold snapshots: 247 copies, 91.5 MB per build, every static-camera build (hq-s8-ultra: 10.98 GB per 120 builds)
   at the 64-byte stride. The 24-byte pack exists only for the 88-byte layout (`dyn_hold_stride`). GPU ms unmeasured.
3. `Tracker::sets` / `dynamic_counts` / `set_layouts` are never erased on FreeDescriptorSets / ResetDescriptorPool /
   DestroyDescriptorPool (layer/tracker.cpp:515-523); `layout_dynamic` never on DestroyDescriptorSetLayout. Growth
   depends on the driver reusing handle values: unmeasured, needs a counter line in a long session.
4. Captured draws: 282 per build, all refit and re-captured every frame with a static camera (convert + extract
   0.66 ms, cascade pass 0.69 ms, BLAS 0.36 ms GPU; capture hooks 6.1 ms CPU over all threads, 4.2 in pre-draw).
5. Small: `log_stats` gate = 5 locked string lookups per pass; `cfg_int` per cascade and water draw; `log()` flushes
   per line under one mutex; `hair_primary_note_pipelines` builds up to 33 pipelines inside the game's create call.
