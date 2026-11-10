## RTX Hair On was inert in release: policy reset fixed (branch claude/hair-release-policy, offline, READY_FOR_LIVE, 2026-11-10)
- Bug (0.9.5-beta, dev=0): RTX Hair On drew no strands. `enforce_release_policy` reset the developer-only hair knobs
  and with them 10 of the 18 keys the release control's On choice writes: rt_hair_groom (exp_hair_groom),
  rt_hair_groom_shadow_t, rt_hair_groom_ao_t, rt_hair_groom_cascade_drop, rt_hair_fibre, _gi, _spec, _mapped (their
  exp_* knobs), rr_hair_guides (exp_rr_hair_guides), rt_hair_physical_origin (exp_hair_physical_origin).
- Fix (common/settings.h, 9 lines): the selected choice of a release control keeps the file's values of the keys it
  writes through the developer reset. Generic rule; the only choice it affects is exp_hair_groom_primary On (pinned by
  the test). Off, a stale raw rt_hair_groom=1 alone, and dev=1 behave as before. The developer knobs stay hidden.
- No new key, no shader change (variants.txt delta 0), no ABI bump. The launcher already showed On and saved the whole
  set; `rt_hair` (Ctrl+Shift+F7) still gates the On list.
- Tests: settings_test `test_release_rtx_hair` (10 FAIL lines without the fix, passes with it); `build.ps1 -Test`:
  224 of 224 CTests passed (13 min 19 s).
- Left: enabling the toggle exposes the open strand flicker (RTX_HAIR_PRIMARY 5n, 5p) to release players; decide
  whether the next package ships it On-capable or waits for hs1. A file with only rt_hair_groom_primary=1 (the mark)
  shows On in the launcher but runs the partial keys it has until saved (as before).
- User check: release package, RTX Hair On, Dialogue1: `[cfg] keys` shows rt_hair_groom=1 and strands are drawn.
