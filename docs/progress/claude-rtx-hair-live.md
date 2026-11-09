## RTX Hair switchable while playing, hotkey Ctrl+Shift+F7 (branch claude/rtx-hair-live, READY_FOR_LIVE, 2026-11-09)
- RTX Hair (exp_hair_groom_primary) is Apply::Live. Startup parts now prepared on every capable device (LSS, shaderInt64,
  fragment stores): strand header 3.5 KB and pipeline variants (about 0.1 s at the first level load, from hcap3 logs;
  new `strand variants` line measures it), occlusionQueryPrecise, world-space tangent compiled in (spec 1045 = 1),
  BC3 TRANSFER_SRC for the capture (memory requirements identical on the RTX 5090). Only rt_hair_physical_origin
  stays startup (43.7 MB at 2560 x 1067 plus GI variants): a game started with RTX Hair off shows the restart banner
  for it while the strands come on live.
- Live key rt_hair (default 1): 0 makes the whole RTX Hair bundle read 0 (cards only, same frame or next; in-flight
  prepass drops restored by the existing depth latch; stale publishes never applied after a switch-on).
- hotkey_rtx_hair (default Ctrl+Shift+F7, no conflict with F8/F9/F10), launcher Lighting page; flips rt_hair in memory,
  one `[hair] hotkey` line. Native fallback launcher keeps its two shortcut fields (validation covers all three).
- Tests: hair_live_test (on, off, on with frames in flight), hotkeys_test, hotkey_poll_test (hair edge, two queues),
  settings_test, web_bridge_test, launcher_ui_test. No shader source change (specialization value only).
- Live job tools/live_batches/rtx_hair_live (rhl1-dialogue, ~111 s of phases, lint 0 failing), not queued.
- User check: Test Dialogue1 with RTX Hair On, press Ctrl+Shift+F7: hair turns to cards and back, no flash of
  missing hair. Cost of the always-on startup part: the `[hair primary] strand variants` log line.
