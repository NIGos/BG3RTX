## RTX Hair motion stability ON in High and Ultra (branch claude/hair-mv-preset, 2026-11-11; user decision)

- Key: `hair_motion_stability` On = `rt_hair_mv_card=2` (docs/RTX_HAIR_PRIMARY.md 5y). Preset row {0, 0, 1, 1}:
  High and Ultra On, Low and Medium Off. bg3rtx.ini default 0 -> 2 (High = the developer defaults, settings_test).
- data/release.ini: `rt_hair_mv_card=0` removed (a release.ini value is the code default, which must equal High).
  Release players: the preset the launcher saves writes On for High and Ultra, Off for Low and Medium, as the presets;
  a file without a preset follows bg3rtx.ini (High): On. A player's own Off/On choice survives the release policy.
- Gates: hm1 210705 (static, mode 2) camera-only share 11% (off 98%), hair/ring MV ratio 0.77 (cards 0.55); hf2 000619
  (Ultra, all new keys, orbit burst) camera-only share 3.2%, hair/neighbour ratio 1.00.
- Cost: strand G-buffer 0.22-0.31 ms, no regression. Effect only with RTX Hair on (Off in every preset).
- Turning it on from Off needs a game restart (specialization 1049 at pipeline creation); a preset change mid-session
  to High/Ultra shows "Restart to apply". Off is live.
- User check: Ultra, S8 at isometric distance, characters walking or the camera turning: hair no longer smears or
  trails behind moving heads.
- Tests: settings_test `test_hair_motion_stability` (preset rows, defaults read as High, release per preset); schema.
