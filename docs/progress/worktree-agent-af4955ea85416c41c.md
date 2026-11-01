## Release check and GUI audit (branch worktree-agent-af4955ea85416c41c, offline, 2026-11-01)

- Default-path identity vs f6b6636 (the last fully VALID lean_regression, 08:39): 628/1242 requests identical.
  All 614 that differ are behind off keys (gi_hit_*/gi_far 320, rf_hit_shade/rf_hit_gi 192, ls_box 60,
  rt_hit_material/reference 40, water compose 1, em_debug 1). The 30 requests the lean runs compiled are
  identical, except the two box variants, which compiled only with ls_box=1. No bug.
- The 14:22 lr1 was on f97a793, not 23b6d65, and INVALID: FG at 1x on a desktop larger than the game's
  output, the case `fg_letterbox` fixes. Not a code regression.
- Defaults f6b6636 -> main: no existing value changed; 13 new keys, all off or inert; 9 new Experimental knobs.
- data/release.ini pins 21 more diagnostics off (rt_primary_check, cpu_timing, crash_dump, pan/replay
  captures, sharc/ls checks, diagnostic logs). They are already 0 in bg3rtx.ini, so the shipped values do not
  change. settings_test 681 checks, 0 failed. Details: docs/RELEASE_CHECK_20261101.md.
- Open for the user: rt_emissive has no GUI entry and release.ini forces it to 0, so "Emission at ray hits"
  cannot be enabled in a player build. Also: ls_box/rt_emissive in release.ini, and crash_dump for players.
- GUI audit (proposal only): 28 Experimental entries -> 8 (hit-shading bundle, RT water levels, native
  coverage group; letterbox, recycle and foliage rows automatic; exact-k, current sun and reference
  developer-only). docs/GUI_AUDIT_20261101.md.
