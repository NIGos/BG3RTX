## Scene handout: hit-material fix on by default with Low-latency scene; inn decision job staged (branch claude/handout-fix, READY_FOR_LIVE, 2026-11-14)

- `hq-s8-modes.1.205520` (S8 Ultra static, 8 traces per state): mode 2 alone drops the native hit work (light rays at
  hits 0.14 M against 3.84 M, ordered 0, diffuse light -0.232% at -6.2 se, GI trace 4.6 to 3.6 ms). With
  `rt_scene_handout_hit_wait=1`: 3.83 M rays, ordered 1, diffuse +0.064%, scene wait 0.28 ms (OFF 3.5), 0 timeouts,
  0 fallbacks, frame time 21.58 against OFF 21.29 / 21.49 ms: correct, no gain at S8 Ultra (GPU-bound), no cost.
- Kept, now the default: the Low-latency scene On choice writes `rt_scene_handout_hit_wait=1`; code default and
  `bg3rtx.ini` are 1; the release policy keeps it at 1 (a player file from before the fix, or with only a preset,
  loads with it). settings_test `test_scene_handout` pins the keys, old files and a stale 0. Schema and UI sample
  regenerated. No ABI, shader or variant change (`shaders/variants.txt` delta 0).
- Card text corrected: "Saves 0 to 0.5 ms" (wait 3.5 to 0.3 ms at Water Queens House, frame time unchanged there on
  Ultra, about 0.5 ms on High). The "1 to 3.5 ms" claim is withdrawn: at the inn the GI trace fell 3.56 ms of the
  3.51 ms frame-time gain, so that gain was skipped work. SCENE_HANDOUT_DESIGN 8.5, 8.6, new 8.7; USER_SETTINGS.
- Analyzer: the mode 1 "FAIL" (+0.230 ms against pair noise 0.200, floor 0.214) was a noise estimate from two medians.
  `state_costs` now also uses the spread of the single OFF periods, as the window groups do (noise 0.315): INFO, the
  run is PASS. A rise beyond that spread and the floor still fails (tests/sane_gates_test.py). New: light rays at hits
  must equal the OFF pair within noise and 1% (FAIL otherwise); a `GI trace` row in the window groups.
- Staged, not queued: `handout_qual/jobs/hq-inn-fix.json` (predicted.json first). The inn, Ultra, focused, FG 3x, no
  cap: OFF / mode 2 + fix / OFF; static, pan Q and E (3 periods each), the pans with RTX Hair On, 4 float traces and a
  short rt_stats window per state. 30 phases, 807 s estimated (cap 14 minutes). Runner gates are "the line exists" in
  the windows whose state prints it (mode 2 lines only in ON windows, GI far only in rt_stats windows; no periodic
  line in the any-line expect). Lint live and frozen: 0 failing. Snapshot: 6 entries appended.
- Predicted at the inn: wait 8.5 to 0.3-3 ms, frame time gain 0 to 1 ms, GI trace and hit rays equal to OFF, energy
  inside the OFF range. Decision rule: the handout stays On in Ultra unless ON is slower beyond noise and floor or
  the hit rays differ; then Off in the Ultra preset until the query moves before the lighting (design 8.6).
- User check in game: restart on High or Ultra; `[core] scene handout: ... hit material passes on the committed scene
  N` has N above 0 and `[rt] scene wait` is near 0.3 ms; Performance > Low-latency scene Off (live) to compare.
