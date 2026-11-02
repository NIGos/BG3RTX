## GUI audit stage 2 done: fg_letterbox automatic, default 1 (branch worktree-agent-a604b8c794b0a930c, offline, 2026-11-02)

- Found on main 5136099: the audit proposal is already implemented (ec9665a, then the M4/M5 promotions).
  Experimental section 28 -> 6 entries (Emission at ray hits, Far-field sun bounce, Native coverage,
  Physical hair ray origins, Opacity micromaps, Preserve cutout capture); RT water and Hidden-surface
  shading are level knobs in Advanced (+ Refraction and + Bounce in Ultra). Emission writes `rt_emissive`.
  Developer only (dev=1): lighting reference, exact-k (`ls_k`), current cascade sun, `rt_hit_emission=2`.
  Old knob ids migrate through `kKnobAliases` / `Choice::mark` (settings_test `test_user_ini_migration`).
  `rt_hit_material_recycle`, `_capture_unlocked`, `_lean_hooks`, `rt_scene_wait`: default 1, no entry.
- This branch: `fg_letterbox` default 0 -> 1, moved from `kIniOnlyKeys` (removed, now empty) to
  `kAutomaticKeys`. The layer logic is already automatic: `fg::output_extent` returns the pass extent
  whenever a sampled image has it, so equal sizes are unchanged; it only acts when the swapchain is larger.
  Evidence: smoke_ultra su1 2026-11-02 01:08 with `fg_letterbox=1`: every-window gates ok (7), FG
  displayed/rendered 2.00, scene wait 0 timeouts.
- Files: bg3rtx.ini, common/settings.h (generated defaults), layer/streamline.cpp (fallback literal 1,
  host layer: needs a game restart), tests/settings_test.cpp, docs/USER_SETTINGS.md, docs/GUI_AUDIT_20261101.md.
- Presets: knob values unchanged. Effective key changed in every preset: `fg_letterbox` 0 -> 1 (no-op
  at equal sizes). Old user inis: the old Off was never written (only knobs differing from the preset
  are), an old On wrote `fg_letterbox=1`, the new default; a hand-set 0 is kept for A/B.
- Not live-verified: the larger-desktop case itself (3840x1600 game in a 3840x2160 swapchain). su1 ran
  with the pass at the output size. Check: borderless on a 3840x2160 desktop, FG on, log line
  `[sl] game output ... backbuffer subrect`, FG displayed/rendered 2.00, no UI ghosting at the bars.
