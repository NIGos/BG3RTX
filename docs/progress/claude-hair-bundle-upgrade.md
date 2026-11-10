## An upgraded install gets a grown bundle without a launcher save (branch claude/hair-bundle-upgrade, offline gates passed, 2026-11-10)

- Problem: a `bg3rtx_user.ini` written before 2026-11-10 has RTX Hair On without `rt_hair_coverage_union`,
  `rt_hair_coverage_confirm`, `rt_hair_slot_stable` and `rt_hair_live_warmup`. The mark `rt_hair_groom_primary=1`
  still selected On, but the four keys loaded as their code default 0 until the launcher saved again.
- Fix (common/settings.h `complete_marked_choices`, called by `apply_layer` before `enforce_release_policy`): when a
  file selects a release choice through its migration mark and no choice matches exactly, the choice's keys that the
  file does not set take the choice's values. A key the file sets keeps the file's value (`rt_hair_coverage_union=0`
  in the file stays 0, as before). Not with `dev=1`, never for developer-only knobs or choices, and only when the
  mark is in the same file. It lives in `apply_layer` because only there is "missing from the file" known.
- Tests (tests/settings_test.cpp `test_release_rtx_hair`): old On file gives the four keys at 1; the mark alone gives
  the whole bundle; an explicit 0 survives; RTX Hair off gives 0; `dev=1` completes nothing. Existing pinned checks
  are unchanged.
- Other knobs: the rule is general, so the four other release knobs with marks follow it: `sun_shadows`,
  `exp_water`, `exp_hidden_surface`, `exp_native_coverage`. Only a hand-edited partial file changes (a mark key
  without the rest of its choice, under a preset that selects a lower choice): for example `rt_cascade_casters=1`
  alone now also loads `rt_shadows=1`, `rt_water_shadows=1` alone loads the whole water Shadows choice. This is what
  a launcher save already wrote for such a file. Files the launcher wrote hold every key of their choices and load
  as before.
- No table, schema, shader or ABI change. Gates: settings and launcher tests, full `build.ps1 -Test`.
