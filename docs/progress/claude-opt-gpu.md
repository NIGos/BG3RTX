## GPU cost breakdown, hair fibre timing, fibre compile time (branch claude/opt-gpu, offline, 2026-11-08)
- Breakdown of the user's session (bg3rtx.log 09:55, RTX 5090, 2560x1067 RR, Ultra): `gi: GPU cost 16.9 ms` =
  pre region 13.3 (`rt passes`: GI trace 10.6 + origin, sun trace, sun resolve, temporal, AO fold ~2.7) +
  post region ~3.6 (D-LS 1.97 incl. the hair fibre pass, D-RF 1.48 incl. the fibre spec pass, composite ~0.03).
  Split of the 2.7 ms from the last detailed_logging run (hair-ab-r, 23:25): origin 0.94, sun trace 0.68,
  temporal 0.04, AO fold 0.02. Outside gi: RR evaluate 2.33, water compose 0.42 + reflections 0.11 + build 0.14,
  cascade capture 0.36-0.52.
- GI trace: no new key. Every standard exact candidate is already in or measured and rejected
  (docs/OPTIMIZATION_20261105.md rounds 1-7: roof early 1038 and compact hit state 1029 on; lane permutation,
  sun wavefront, far lean, origin fast/lazy, GI-only TLAS, FAST_TRACE rejected; binary rays already
  accept-first; sky early-out exists; SER needs a raygen port). The trace is the hidden-hit far branch
  (attribution: 8.0 ms -> 2.1 without gi_hit_sun/far). Checked here and dropped: sun-first for far hits
  (with rt_cascade_casters the roof status never fires, so almost no skipped work), unlit/metal early-out
  (not exact through the temporal lerp). No replay packs exist after the reinstall, so no offline paired timing.
- Added: `[hair fibre] fibre pass` / `spec pass` GPU median lines (inside the ls/rf lines), hair epoch and
  stats-window resets honoured; CTest hair_fibre_timing_test, gpu_logging_gate_test covers the helpers.
- gi/hair_fibre.hlsl: one call site per heavy function (rt_visibility had three inlined copies: stubbing it
  alone cut the compile 12 -> 1.5 s). Offline compile 12.0 -> 2.9 s, driver pipeline 5.6 -> 1.3 s, ISA
  912 -> 260 KB, registers 168 -> 168; non-hair pixels exit before ls_lite. Not behind a key (a key would keep
  both bodies). Shader requests delta 0; only the two hair_fibre requests change SPIR-V.
- Open: GPU output equality of the restructured fibre pass (no replay contract); in game, rt_hair_fibre=2
  (the in-frame check) and Test Dialogue1 strands must look unchanged. Next live: read the two new lines.
