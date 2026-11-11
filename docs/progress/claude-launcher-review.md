## Launcher review: RTX Hair Off sticks, version from the package, save flow (branch claude/launcher-review, offline, 2026-11-11)

- RTX Hair could not be turned off (0.9.6-hairtest8). Cause: the page saves with policy "enable" and the experimental
  opt-in "Strand hair edge resolve" (Analytic coverage) needs RTX Hair On, so the review of Save and Play raised RTX
  Hair again (`common/settings.h` preview_dependencies, called from `tools/launcher/web_bridge.cpp` save_preview).
  Fix: a knob the player turned down since the last save stays down; what needs it is lowered and listed in the
  review (`lower_dependents`). Needs already unmet in the saved file keep the enable policy.
  Test: `launcher_web_bridge_test` loads the player's file (`tests/fixtures/launcher/user_hairtest8.ini`) over
  bg3rtx.ini + release.ini and runs set / save / play; 8 checks fail without the fix.
- Version: the window showed the constant 0.9.5.0. It now shows line 1 of the package's `VERSION.txt`
  (`tools/launcher/version.h` display_version); a developer tree shows "0.9.6.0 (developer build)". Tests: bridge
  test and `tests/launcher_version_test.py`.
- Save opened the review dialog every time, also with "No settings change". It now writes at once when there is
  nothing to review (as Play does). "Save" in the close prompt used the other dependency policy and left the
  window open; it now sends the Save button's request and closes once saved. Behaviour change: tell me to revert.
- Hair conversion (`tools/launcher/preparation.cpp`): a style captured while the converter ran waited for the next
  capture or Start; choosing granny2.dll again did not start a saved "Convert all styles"; a folder ending in a
  backslash broke the converter command line (test added).
- Reports: bg3rtx.ini, bg3rtx_user.ini and VERSION.txt older than 24 hours made every report "Partial" (exit 1). The
  staleness note is for logs only now (test added). The game upscaler guard no longer reports a fix it did not write.
- Debt: the page's sample data matches the table again (eight descriptions, RTX Hair marked "restart") and the JSON
  test compares it with the bridge; the user file heading "Hardware limits" names what is under it (hotkeys, DLSS 5
  runtime file); stale comments (Python converter, "C2 may register", "F9", "pair"); the native fallback counts
  every hotkey in its unsaved count.
- Key cross-check: every knob, bg3rtx.ini and release.ini key is read by layer/core/common (launcher-only keys excepted).
- No preset value, profile rule or knob table entry changed. No ABI or shader change (variants delta 0).
Decisions (user, 2026-11-11):
1. Save with nothing to review writes at once: kept.
2. dev=1: a saved RTX Hair part knob (the exp_hair_* ones the On bundle also writes) wins over the bundle. Done in
   `render_user_ini(..., dev, loaded)` (`common/settings.h`): a part changed since the last load or save, or already
   set in the file, is saved and its key written once; a part left alone while RTX Hair is turned On stays under
   the bundle. Release and non-dev are as before (bundle saved whole). Tests: `settings_test`, `launcher_web_bridge_test`.
3. bg3rtx.prev.log (and tagged .prev.log files) never makes a report "Partial": it is the last session's log by
   design. Other old logs still do. Test: `launcher_diagnostics_test`.
4. Native fallback kept; its Overview page says RTX Hair preparation and its shortcut are only in the full window.

Still open, not changed:
- The hair card trusts a stale preparation.json: with the game running and "Also while playing" off, a new capture
  still reads "All needed styles are done".
- Report redaction misses a game library path outside the Users folder and a bare user name.
