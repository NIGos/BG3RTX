## A stale level never stays current; sun resolve liveness row (branch claude/fix-level-clear, READY_FOR_LIVE, 2026-11-23)
- Cause (.scratch/coord/hunt2_lifecycle.md finding 2): nothing cleared the asset DB level once set, except turning
  `assetdb_level_vote` off. After travel or a save load the old level stayed current until a check found 3 or more
  lights of the new level. It stayed for good when the new level was not in the DB or had too few DB lights in view.
  With the vote on, core/sun_trust then had `roofs` true and 0 volumes around the new camera, so every pixel was
  trusted: an interior sun leak. The SE bridge kept the old level for a level name the DB lacks.
- Fix (layer/assetdb.cpp, behind `assetdb_level_vote` / `assetdb_join`, both 0: no image change today):
  - `assetdb_vote` gets the clear rule: with at least 3 voters, none for the current level and no qualifying switch,
    it clears (fail closed). Right after a level load, any lack of evidence clears it, no voters included.
  - A level load (a new G-buffer depth image, as hair_groom's LoadGuard) drops a light-list copy recorded before it.
    It re-runs the vote 30 presents later, not up to 240. New lines: "[assetdb] level load at present N", and
    "... after a level load: kept | switched | cleared" on the vote line.
  - `level_from_se`: a sub-level maps to its world. A level the DB lacks clears the current level.
- Tests: assetdb_test.
  - Inn level then the camp's lights: switched.
  - Level A, then only unknown lights: cleared.
  - Level A, then 2 lights of level B: kept in play, cleared after a load.
  - No lights: kept in play (no false clear), cleared after a load (no false keep).
  - The current level's own lights after a load: kept.
  - The SE bridge: sub-level BGH_SteelWatchFoundry_B gives CTY_Main_A; NoSuchLevel_Z clears.
- Liveness: row `sun_resolve` ("[core] gi: sun resolve pass GPU cost", silent_dead with detailed logging,
  hunt2_claims D3). It covers rt_sun_offscreen, ramp, rise, roof_pad and roof_up, which leave baseline_no_row.
  New field `follows`: ["level_known", "roof_volumes"]. A DEAD row whose cause row is DEAD in the same preset says
  "(follows ... DEAD)". The checker is VERSION 2. liveness_test gets a case for it.
- Staged (not queued): level_vote/lv-s8-ultra gains reload-on (F8, key on) and an after-reload window. It waits for
  the load line and an after-load vote that keeps or switches. It gates "replace 1, N roof volumes" N > 0 in every
  period. predicted.json updated. live_flow 0 failing with and without --frozen. A change to another level is not
  staged: the runner loads one save per launch.
