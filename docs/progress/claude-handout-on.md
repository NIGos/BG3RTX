## Low-latency scene On in High and Ultra (branch claude/handout-on, ready to merge, 2026-11-14)
- User decision "Si, accendilo": `scene_handout` preset row Off / Off / On / On; a normal Performance control (Frame rate
  group), no longer awaiting qualification, not an opt-in knob; no release pin involved. `bg3rtx.ini` carries the High keys.
- Evidence (handout_qual, focused, FG 3x, OFF/ON/OFF): inn Ultra 30.0 -> 26.5 ms (wait 8.5 -> 3.0), inn + RTX Hair
  31.1 -> 28.6, S8 Ultra 20.9 -> 19.9 (pan 20.8 -> 19.8), S8 + hair 22.1 -> 21.2, S8 High pan 12.1 -> 11.6; fallbacks 0,
  pairing mismatches 0 everywhere. Energy inn +0.11%, S8 -0.72% (OFF spread 647.8-656.6): OPEN, 3 traces per state.
- Release player: High and Ultra On, Low and Medium Off; a user file with only a preset line becomes On for High and
  Ultra at the next game start, untouched. Tests: settings_test `test_scene_handout`, launcher UI and bridge tests.
- Analyzer: per-pass costs (RT, GI, scene build) now "not higher beyond noise" (the first run failed when they fell).
  Pans are 3 periods (every window at least 2 complete periods); `hq-s8-energy` (8 traces per state) and `hq-s8-check`
  (primary-ray match, split to stay under 15 minutes) are staged, not queued. Lint 0 failing, live and frozen.
- Check in game: restart on High or Ultra; `[rt] scene handout` line with 0 fallbacks; turn it Off on the Performance
  page (live) to compare. Docs: SCENE_HANDOUT_DESIGN.md 8.5, USER_SETTINGS.md.
