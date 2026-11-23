## Level detection by the runtime lights: assetdb_level_vote (branch claude/fix-level-detect, READY_FOR_LIVE, 2026-11-23)
- Cause (.scratch/coord/hunt_core.md F1): the asset DB never knew the level in a release run. Patch 8 saves zstd-compress
  SaveInfo.json (no save hint), the runtime-light vote ran only inside the diagnostic join (`assetdb_join`, default 0,
  in no ini), the SE bridge mod is not packaged. 171 of 171 queue logs: "sun stats ... replace 0, 0 roof volumes
  (level unknown: nothing trusted)". So rt_sun_replace never ran, Ultra's gi_far had every hit beyond R0 roof-blocked
  (screen and hidden hits; the far work ran and was discarded), and D-LS had no database lights.
- Change (layer, off by default, image unchanged): the vote is its own function (`assetdb_vote`, layer/assetdb.h),
  shared by join_check and a lean `vote_check` (copy -> voters -> vote, no CSV or statistics). New key
  `assetdb_level_vote` (bg3rtx.ini, common/settings.h `exp_assetdb_level_vote`, Advanced, developer, Live, needs
  Sun shadows ray-traced): every
  `assetdb_join_interval` (240) presents; turned on it checks at once; with both keys off it forgets a level it found
  (so 0/1/0 is an A/B in one run). Ties now pick the lower level index (was map order). Lines: "[assetdb] level vote #n",
  "[assetdb] level <L> (from lights)", "[assetdb] level cleared (assetdb_level_vote=0)".
- Core (log only): "sun stats" / "sun check" follow rt_stats_period_frames like the other stats lines (600 native).
- Tests: build.ps1 -Test 240/240. assetdb_test "level vote" fixtures from the database at the logged S8 and inn cameras (CTY_Main_A, with SCL_Main_A
  sharing the place) and the Dialogue1 camp (WLD_Main_A): float32 camera-relative round trip plus 24 unknown lights;
  the right level from no level and from a wrong one, no change when current, noise alone votes for nothing.
- Staged, not queued: tools/live_batches_staged/level_vote (lv-s8-ultra with OMM off and two gi_hit_sun_em packs for the
  PT verdict, lv-d1-ultra in the user's Ultra); predicted.json first; live_flow 0 failing with and without --frozen.
- Cost: one light-list copy every 240 presents. The consumers it turns on will cost GPU ms (sun resolve, far hits
  kept): measured by the staged live, the user decides with the numbers.
- User check after qualification: the log names the level "(from lights)"; at Water Queen's House on Ultra the sun
  under the house's roofs and the far bounce light change; Advanced > "Level detection by lights".
