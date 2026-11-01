## Silent degradations: roof-volume cap, variant matrix, gates (branch worktree-agent-a29080dd15a8a2223, offline verified, 2026-11-01)

- RT sun roof volumes: `kSunMaxVolumes` 256 -> 512 (shaders `SUN_MAX_VOLUMES`, slot 16 -> 32 KB). The user's
  CTY_Main_A run dropped 103 of 359 volumes (radius 1980 m) on 306 of 600 frames, so pixels under them were
  trusted; the largest level has 398 (`roof_stats.py --check`, new CTest `roof_cap_test`). GPU cost: the
  roof loops exit on the first hit, nearest first; only CTY uploads more than 256. Not measured live yet
  (read `rt passes: GPU cost` in a CTY window).
- `sun stats` now says why there are no volumes ("level unknown: nothing trusted"): the user's
  "0 roof volumes, scene top -96.8" was the menu before the level was known (fail-closed, by design).
- Scene wait "73 / 168 skipped": menu and loading only (the wait probes every 64 frames after 8 misses with
  no recent scene); every in-game window has 0. No change.
- New CTest `variant_matrix_test`: every pair of experimental / water / hit / box / D-LS / GI keys has its
  shader variant; the only skips are the water passes under rt_omm, rt_native_tileset, rt_native_scalp,
  rt_native_hair, rt_hair_physical_origin (`shader_variant::kWaterNoVariant`, now the one table water.cpp
  reads). Their 24 water requests compile (shader_identity). Log convention `skipped: no variant for <keys>`.
- smoke_ultra forbids `skipped: no variant for`, the roof WARNING, `over kSunMaxVolumes on [1-9]` and a
  nonzero set-1 range guard; m5 jobs' "no water variant" forbids use the new text.
- Open (owners): set-1 out-of-range reads are hit at ~1.5e6 per frame in the user's run (19.2e9 by frame
  16216; 0 at frame 574); cascade casters buffer full 1.3/frame at 427 MB; hit-material basis pool 94%
  with a silent over-cap skip; emissive soup full 0.10/build. Table: docs/GATE_AUDIT_20261101.md section 8.
