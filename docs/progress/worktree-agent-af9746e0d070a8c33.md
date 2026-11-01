## Replay packs carry the cascade casters; camp / inn ambient batch ready (branch worktree-agent-af9746e0d070a8c33, offline, READY_FOR_LIVE, 2026-11-01)

- Writer fix: the rt_cascade_casters instance went into packs with no triangles (InstInfo tris 0, replayed with
  mask 0), so the replay TLAS had no roofs. bvhsplit.cpp now records each ring slot's cascade instance and soup;
  layer/replay.cpp copies that soup (`casters.bin`, manifest `casters FILE INSTANCE TRIS STRIDE`) while its ring
  slot still holds the same frame, else notes it left out. frame_replay appends it to the soup, points the
  InstInfo entry there (`rp::caster_patch`) and builds its BLAS as live (not opaque, mask 0x02); `--no-casters`
  for A/B. Expected size: 52 MB at camp, 84 MB at the inn (2026-10-31 cascade logs).
- Second writer fix: core passed `defines=""`, so a casters-on pack replayed GI rays at 0xFF instead of the live
  0x01. Now the running variant's defines go in, filtered to the contract (`rp::contract_defines`: RT_CASCADE_MASKS;
  the others noted). Old packs replay unchanged (S8 142819-1: bit-equal 99.5738%, same counters).
- PT: `PT_CASTERS=1` (auto for a casters pack) traces the cut-away roofs as triangles (shadow rays with the live
  0.5 m / 2% near rule; path rays get a fallback-material vertex). `--native-ambient` prints a new `PT split` line
  (far-field share on / off screen, live domain, sun / local). `tools/ambient_table.py` prints the per-scene row.
- Tests: replay_pack_test (casters directive, patch, defines), pt_reference_test 8-10 (casters box closes at
  exactly 1 / 1.75; open without PT_CASTERS), ambient_table_test; all 82 CTests pass. replay_pack=0 is untouched
  (the layer keeps one small record per TLAS build).
- Live batch (not run): tools/live_batches/replay_capture/ambient, `rc2-ambient-camp-fg2` and `rc3-ambient-inn-fg2`
  (casters on, exposure held, cap 3500 MB, post step runs the replay, native ambient and table, keeps the newest pack
  per scene). Dry run passed: about 5.5 min, up to 7 GB (two packs of about 3.1 GB). Needs a layer + core build.
