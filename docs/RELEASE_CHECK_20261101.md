> Superseded. This is historical evidence. See the [2026-11-04 release report](PROGRESS_CODEX.md) for current decisions.

# Release check, 2026-11-01 (main 89dd500)

## 1. Default-path identity against the last live-verified main

Baseline: **f6b6636**. lean_regression lr1 camp, lr2 inn, lr3 box S6 at 08:39-08:51 are all VALID
(results.jsonl, build f6b6636). The 14:22 run was not on 23b6d65 but on **f97a793**, lr1 only, and
INVALID: FG stayed at 1 presented frame per app frame in all 24 DLSS-G lines. gf1, gf2 and rc1 at
14:16-14:30 on the same build show the same, while m5w2/m5w4 at 14:07/14:11 on that build had FG.
It was the desktop larger than the game's output (8923807 names gf1 141602), fixed behind `fg_letterbox`.
That is not a code regression, but it is not a verified baseline either.

`tools/shader_identity.py --baseline f6b6636` (1247 s): 628 of 1242 requests byte-identical, 614 differ.
- All 30 requests the three f6b6636 lean runs compiled at run time (the default Ultra path, including
  the `RT_CASCADE_MASKS` variants of roof shadows) are identical, except `rt_local_shadows_box` and
  `_full_box`. Those two compiled only in the lr3 windows with `ls_box=1`.
- Every differing request is behind a key that is off by default. No bug found:

| Requests (count) | Key (off) | Commits |
|---|---|---|
| gi_trace_rt_*hit_direct*/*hit_sun* (320) | gi_hit_direct, gi_hit_sun, gi_far | 175486b gi_far, e6c30fb non-finite guard |
| rt_reflections_*hit_shade* (192) | rf_hit_shade, rf_hit_gi | 0a0e672 cost cuts, 0be532f rf_hit_gi |
| rt_local_shadows_*box* (60) | ls_box | fe033a2 ls_box=2, 0be532f |
| rt_hit_material, rt_hit_direct, rt_hit_two_bounce (40) | rt_hit_material, rt_hit_reference | 4eb1671, b22c94c, 50b6e65 (native_hit_material.hlsli) |
| rt/water_compose (1) | rt_water_compose and its children | 8726923, 50eed16, 57f1b19, 02da9fb, a0b4810, 70b7c53, 64c0f5d |
| rt/em_debug (1) | rt_emissive (D-EM check) | a5c0b40 |

Default settings, f6b6636 -> main (bg3rtx.ini, data/release.ini, settings_schema presets): **no existing value
changed**. 13 keys were added, all off or inert: fg_letterbox 0, gi_far 0, gi_far_m 6, replay_pack_cap_mb 2048,
rf_hit_gi 0, rt_em_check_slices 16, rt_hit_material_recycle 0, rt_hit_material_wind_rgb 0,
rt_scene_wait_log_frames 600, rt_water_game_shadow 0, rt_water_ls_shadows 0, rt_water_refraction 0,
rt_water_shadows 0. There are 9 new Experimental knobs, off in every preset. The live `[cfg] keys` lines
(08:39 vs 14:22) agree except for the new keys. Code defaults: `rt_cascade_mb` went from 64 to 160 in code
(the ini already had 160). Capture caps now follow the vertex stride, which is unchanged at 24 B by default.
`rt_em_check_slices=16` only acts with `rt_emissive=1`.

## 2. What a player build ships

scripts/package.ps1 = bg3rtx.ini + data/release.ini, and the lean layer (`BG3RTX_DIAGNOSTICS=OFF`). The
launcher reads that bg3rtx.ini plus bg3rtx_user.ini (the GUI).

| Key | Shipped | Recommended | Why |
|---|---|---|---|
| dev | 0 (release.ini) | 0 | Ctrl+Shift+F10 trace hotkey; could fill the disk |
| dump_shaders, shader_debug, census_dump, geo_capture/timing | 0 | 0 | already in release.ini |
| rt/ls/rf/gi_debug, *_stats, rf_validate, rr_* checks | 0 | 0 | already in release.ini |
| rt_primary_check / _epoch / _identity / _fade_class | 0 (dev ini) | 0, **now pinned** | ~80 ms/frame with rt_stats |
| cpu_timing, rt_order_trace, rt_stability_log, rt_capture_state_log, rt_mesh_draw_log, rt_em_log | 0 (dev ini) | 0, **now pinned** | log/timing diagnostics |
| crash_dump | 0 (dev ini) | 0, **now pinned** | minidumps into captures\crash; the queue turns it on |
| pan_capture/prepare/inputs, replay_pack, rt_mesh_capture | 0 (dev ini) | 0, **now pinned** | disk captures; rt_mesh_capture disables the mesh cache |
| sharc_debug/stats/leak_check/fine_check, ls_check | 0 (dev ini) | 0, **now pinned** | debug views, reference checks |
| ab_hotkey | 1 | 1 | documented player feature (USER_SETTINGS.md) |
| rt_mesh_audits 2, rt_em_calib 1, gpu timestamps | on | on | correctness / cheap; feed the GUI cost lines |
| auto_trace, sl_dev | absent (0) | 0 | code default 0; not in bg3rtx.ini, so not pinnable |
| log (bg3rtx.log) | always on, ~3 KB/s | keep | truncated at every launch; no level key |
| ls_box, rt_emissive | 0 (release.ini) | see decision | release.ini forces them in the base layer |

Implemented: data/release.ini pins the 21 "now pinned" keys (0 in bg3rtx.ini already, so nothing a player
sees changes). The developer defaults are unchanged. settings_test: 681 checks, 0 failed.

## User decisions
1. `rt_emissive` has no GUI entry and release.ini forces it to 0, so "Emission at ray hits" cannot be enabled
   in a player build without editing the ini. Add it to the GUI (written by that knob), or hide the knob?
2. `ls_box` and `rt_emissive` are features in a "developer keys" list. Should they move out of release.ini
   (the GUI layer overrides them anyway)?
3. Ship `crash_dump=1` for player bug reports (needs dbghelp, writes captures\crash)? Conservative: 0.
