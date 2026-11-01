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

## Set-1 out-of-range, cascade buffer, basis pool (branch worktree-agent-a29080dd15a8a2223, offline verified, 2026-11-01)

- Set-1 out-of-range reads: in all 56 queue logs of the day the `[core] guard:` count is nonzero exactly in the
  runs whose mesh pool doubled to 2048 MB (2^31 bytes: m4p2 4.2M/frame, su1 0.85M, sw1 0.67M, sw3 1.3M, the
  user's CTY run), from the first readback after that growth; every run that stayed at 512 or 1024 MB reads 0.
  Fix (layer): the pool stays below 2^31 and within maxStorageBufferRange (`mesh_pool_capacity`, 2047 MB);
  when full, new meshes stay uncached, logged once (`[bvh] mesh pool full at N MB`). Unit-tested. Exact cause
  at 2^31 not proven (descriptor range or a 31-bit offset); the smoke_ultra forbid on the guard decides.
- Cascade casters buffer (427 of 427 MB, 1.3 draws/frame left out): grows on overflow to the frame's demand
  (captured + left-out bytes) + 25% in 16 MB steps, below 2048 MB (`cascade_grow`), logged
  (`[cascade] buffer full: ... grows to N MB`); the casters line now prints `demand max`.
- Hit-material basis pool (240 of 256 MB, over-cap skips silent): meshes waiting for a basis range are counted
  and logged (`[hit material pool basis] over kBasisCapBytes ...`, at 1, 2, 4, ... meshes). The cap stays
  256 MB until one live window gives the over-cap demand (raising it pushes the mesh pool toward its cap).
- smoke_ultra forbids `over kBasisCapBytes` and `mesh pool full at` too (the guard forbid was already there).

## Basis cap and the alpha floor after loading (branch worktree-agent-a29080dd15a8a2223, offline verified, 2026-11-01)

- smoke_ultra 23:48 confirmed the mesh-pool fix: `[core] guard:` out-of-range 0 for the whole run (was ~1.5M/frame).
- Hit-material basis: at 256 MB, 128-255 meshes (154 MB at 128) waited, a demand of 410-565 MB.
  `kBasisCapBytes` 256 -> 768 MB (inside the 2047 MB mesh pool beside 200-400 MB of vertices).
- Alpha "region full 0.30" in the first window: the region (window line: 336 MB at its end) was at its
  171 MB floor on the S8 load frame, which wanted 223.8 MB (36 draws, 52.9 MB made opaque once); the
  region had shrunk to the floor while loading. The floor at a wide stride with the default rt_alpha_mb is
  now 40% of the buffer (409 MB of 1024): the day's largest alpha demand was 404.6 MB, opaque peak 463.8 MB.
