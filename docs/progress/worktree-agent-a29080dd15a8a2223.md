## Silent degradations: caps, variant matrix, gates (branch worktree-agent-a29080dd15a8a2223, merged / live-confirmed in part, 2026-11-01)

- Roof volumes: `kSunMaxVolumes` 256 -> 512 (shaders `SUN_MAX_VOLUMES`). CTY_Main_A had dropped 103 of 359
  (radius 1980 m); the largest level has 398 (`roof_cap_test`). GPU cost not measured live yet.
- `sun stats` says why there are no volumes ("level unknown"): "0 roof volumes, scene top -96.8" was the menu.
  Scene wait "73 / 168 skipped": menu and loading only, 0 in game. No change.
- CTest `variant_matrix_test`: every key pair has its variant; the only skips are the water passes under
  rt_omm, tileset, scalp, hair, hair origin (`kWaterNoVariant`; their 24 requests compile). Log
  convention `skipped: no variant for <keys>`.
- Set-1 out-of-range reads were nonzero exactly in the runs whose mesh pool doubled to 2048 MB (2^31;
  0.7-4.2M per frame). The pool now stays at 2047 MB and within maxStorageBufferRange (`[bvh] mesh pool
  full at` when full). smoke_ultra 23:48: guard 0 for the whole run (live-confirmed).
- Cascade casters capture grows on overflow to demand + 25% (`[cascade] buffer full: ... grows to`);
  the casters line prints `demand max`.
- Hit-material basis: over-cap meshes are logged (`over kBasisCapBytes`); at 256 MB 128-255 meshes waited
  (demand 410-565 MB), so the cap is 768 MB.
- Alpha "region full 0.30" after loading: the region had shrunk to its 171 MB floor; the S8 load frame
  wanted 223.8 MB. The floor at a wide stride is 40% of the buffer (409 MB of 1024; the day's peaks:
  alpha 404.6 MB, opaque 463.8 MB). Not yet confirmed live.
- smoke_ultra forbids the variant skip, roof WARNING, nonzero guard, `over kBasisCapBytes` and
  `mesh pool full at`. Table: docs/GATE_AUDIT_20261101.md section 8.
