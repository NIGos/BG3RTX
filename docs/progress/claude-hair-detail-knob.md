## RTX Hair detail at distance: launcher control (branch claude/hair-detail-knob, 2026-11-10; offline, Off in presets)

- What: one release control `hair_distant_detail` ("RTX Hair detail at distance"), launcher page Lighting, card right
  after RTX Hair. Choices: Off / 1/4 (Standard) / 1/8 (Sharper). Off writes `rt_hair_pixel_lod=0`; 1/4 writes
  `rt_hair_pixel_lod=1` + `rt_hair_pixel_lod_small_keep=0.25`; 1/8 writes `=1` + `0.125` (docs/RTX_HAIR_PRIMARY.md 5v,
  common/hair_groom_budget.h pixel_lod_level). `rt_hair_pixel_lod_k` stays 1.0 (not exposed).
- Live: every choice is Apply::Live ("Applies after saving"). The launcher's Save writes bg3rtx_user.ini; the layer's
  cfg_poll (layer/util.cpp cfg_read_files) re-reads the ini files on mtime change every frame, and layer/bvhsplit.cpp
  reads both keys with cfg_int/cfg_float per capture (the pl1 live toggled them in one session). Neither key is in
  key_change_needs_restart. Turning it off releases the camera arrays from the mesh pool.
- Release: `data/release.ini` keeps `rt_hair_pixel_lod=0` as a default, but enforce_release_policy no longer pins it to 0
  (it pinned every release.ini 0 key, which would have made the control inert in release, as RTX Hair once was).
- No Need on RTX Hair: without RTX Hair the keys do nothing (the pick runs only with strands on screen), and a preset
  can select 1/4 without forcing RTX Hair on. The tooltip says it only changes RTX Hair.
- Cost on the card: strand G-buffer -0.08 ms (1/4) / -0.10 ms (1/8), camera arrays + BLAS about 100 MB (1/4) and
  74 MB (1/8), from pl1 211046 (RTX 5090, S8); "not yet qualified".
- Tests: settings_test `test_hair_distant_detail` (choice keys and labels, live, Off in presets and release, each choice
  survives the release policy, a file with only `rt_hair_pixel_lod=1` reads 1/4); release visible-knob list,
  launcher_web_bridge_json_test (21 controls, labels), launcher_ui_test (Lighting card order); schema regenerated.
- Shaders: none touched; variants delta 0.

### Making 1/4 the default (after the final qualification live)
- common/settings.h, `hair_distant_detail`: preset row `{0, 0, 0, 0}` -> `{0, 0, 1, 1}` (High and Ultra; or all four).
- High equals the developer defaults (settings_test), so also bg3rtx.ini `rt_hair_pixel_lod=1` (and data/release.ini
  `rt_hair_pixel_lod=1`, or drop that line), then `python scripts/gen_settings_defaults.py`,
  `bin/settings_test.exe --write-schema`, and update `test_hair_distant_detail`'s preset/default checks.

### Final qualification live, staged (not queued): `tools/live_batches_staged/hair_final` (hf2-s8-hair-final)
- S8, ultra + fg=3, RTX Hair bundle + rt_hair_stale_publish=1, rt_hair_mv_card=2 (from load), rt_hair_row_stable=1,
  distant detail 1/4. Warm-up, hl1 zoom (0 watchdog trips per 60-frame line), slow orbit right/left with one K8 RR-motion
  burst (mv_ratio.py ring ratio), off / 1/4 / 1/8 / off PNG traces 1.5 s after each switch, cost lines in every window.
  About 93 s after load, under 0.3 GB. live_flow live and frozen: 0 failing (4 readiness queries added to the snapshot).
### RTX Hair motion stability (`hair_motion_stability`, Lighting, after the distance detail; 2026-11-10)
- Off / On = `rt_hair_mv_card` 0 / 2 (5y; mode 1 disproved, not exposed). Off in every preset and in data/release.ini
  (a default there, not a pin: the release policy keeps a selected On). Off -> On shows "Restart to apply"
  (choice Apply::Restart, key_change_needs_restart 0 -> non-zero): specialization 1049 is compiled into the velocity
  pipelines only when the key is non-zero at their creation (layer/hair_primary.cpp). On -> Off is live (header flag).
  Not made live-compilable: always compiling 1049 would change the default velocity pipeline and touch
  layer/hair_primary.cpp, where the stale-publish fix is in progress; a later step if wanted.
- Tests: settings_test `test_hair_motion_stability`, release controls 22, bridge and UI order, schema regenerated.
