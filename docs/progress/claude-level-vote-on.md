## Level detection by lights on by default (branch claude/level-vote-on, kept, 2026-11-24)

- User decision 2026-11-24: `assetdb_level_vote=1` by default (bg3rtx.ini, settings.h), knob
  `exp_assetdb_level_vote` no longer experimental, preset Low 0 / Medium 1 / High 1 / Ultra 1.
- Why: since Patch 8 the asset DB never learned the level, so sun replace, the far-GI roof rules and the
  per-light database data were silently inactive in every log since about 2026-11-07. A regression fix.
- Live 2026-11-23/24 (`level_vote`, S8 and Dialogue1): level found, kept after an F8 reload, cleared on
  switch-off, 0 faults, about +0.2 ms. Offline PT: far GI screen hits FARTHER on 1.4% of px (accepted).
- The knob is developer-only, and the release policy (`enforce_release_policy`) writes every developer knob to 0
  without dev=1, so a preset value alone would never reach a player: the policy now writes it On wherever Sun
  shadows are ray-traced (as for `exp_ls_exact_k`); settings_test pins it.
- Low stays 0: no RT sun, local shadows or RT GI there, so no consumer, and the On choice needs RT sun shadows.
- Liveness: row `level_known` now covers the key; it and `roof_volumes`, `sun_replace_active`, `sun_resolve`,
  `ls_per_light_data` already apply to Medium, High and Ultra.
- Check in game: the log shows `[assetdb] level ... (from lights)` and no `level unknown: nothing trusted`;
  `[core] sun stats` reports replace 1 and roof volumes > 0.
