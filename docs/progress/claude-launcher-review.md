## Launcher review: RTX Hair Off sticks, version from the package, save flow (branch claude/launcher-review, offline, 2026-11-11)

- RTX Hair could not be turned off (0.9.6-hairtest8). Cause: the page saves with policy "enable" and the experimental
  opt-in "Strand hair edge resolve" (Analytic coverage) needs RTX Hair On, so the review of Save and Play raised RTX
  Hair again (`common/settings.h` preview_dependencies, called from `tools/launcher/web_bridge.cpp` save_preview).
  Fix: a knob the player turned down since the last save stays down; what needs it is lowered and listed in the
  review (`lower_dependents`). Needs already unmet in the saved file keep the enable policy.
  Test: `launcher_web_bridge_test` loads the player's file (`tests/fixtures/launcher/user_hairtest8.ini`) over
  bg3rtx.ini + release.ini and runs set / save / play; 8 checks fail without the fix.
- Version: the window showed the constant 0.9.5.0. It now shows line 1 of the package's `VERSION.txt`
  (`tools/launcher/version.h` display_version); a developer tree shows "0.9.6.0 (developer build)". The constant
  stays for the exe resource only, and `scripts/package.ps1` refuses a numbered `-Version` that does not start with
  it. Tests: bridge test and `tests/launcher_version_test.py`.
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
- Key cross-check: all 103 keys the 60 knobs write, all 357 bg3rtx.ini keys and all 61 release.ini pins are named in
  layer/core/common, except `dlss5_nr_runtime` and `game_dlss_guard`, which only the launcher reads (correct).
  105 keys are read with `cfg_int`/`cfg_float` but have no line in bg3rtx.ini (developer and diagnostic keys).
- No preset value, release profile rule or knob table entry changed. No ABI or shader change (variants delta 0).

Open, not changed:
- With dev=1, the 14 strand part knobs that RTX Hair On bundles (exp_hair_fibre and others) cannot be saved Off while
  RTX Hair is On: the bundle's keys win at the next load. Writing the part after the bundle breaks the test that
  pins "RTX Hair On saves the whole bundle", so it needs a decision (which wins in developer A/B).
- The hair card trusts a stale preparation.json: with the game running and "Also while playing" off, a new capture
  still reads "All needed styles are done".
- The native fallback (`ui_product.h`, used without WebView2 or with `--native-ui`) has no RTX Hair hotkey field, no
  preparation card and its own save review. Keep it, or reduce it to "install WebView2"?
- Reports: "Partial" when bg3rtx.prev.log is older than 24 hours (a test pins it; probably too strict). Redaction
  misses a game library path outside the Users folder and a bare user name.
