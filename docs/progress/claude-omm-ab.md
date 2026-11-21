## OMM live A/B staged, High downgrades explained, release downgrade gate made INFO (branch claude/omm-ab, offline, 2026-11-21)

- Why High downgrades and Ultra does not (final-s8-ultra-high 052606):
  - Ultra has rt_hit_material=1 with rt_scene_handout_hit_wait=1. Its hit-material passes wait for the commit
    (`[core] scene handout: ... N settled to the committed scene`, about all 60 of 60). So the game's lighting
    command buffer is still recording when the commit registers the micro-map tickets.
  - High has rt_hit_material=0: "0 settled" in every High line. The passes run on the handed-out set without waiting,
    and about 1 frame in 60 the buffer has already ended at the commit. Its tickets are declined
    (`try_register_ticket_use`) and the slot traces the reference scene.
  - Not fixed, not small: registering tickets ahead (at the hand-out) or committing earlier changes the handout
    protocol. The downgrade gives a correct image.
- Release gate (`tools/live_batches_staged/release_final/make_jobs.py`): "no OMM downgrade" is gone. Hit-material query
  failures and fallbacks still fail. Downgrades are INFO with their count per window
  (`release_qual/analyze_final.py` omm_downgrades). release_final regenerated from today's profile (MSVC profile.exe);
  frozen and live lint 0 failing. Test: `tests/release_qual_test.py`.
- Staged `tools/live_batches_staged/omm_ab` (READY_FOR_LIVE, not queued), job oa-s8, one S8 launch:
  - release Ultra, then release High through its settings reset; dev=1 (the release policy resets the developer knob);
  - rt_omm_reference 1 / 0 / 1 live (push constant bit 28); rt_omm itself needs a restart;
  - per preset and state the smoke's static, Q/E pans and R/L rotations (one 120-frame period after a 60-frame
    settle), plus 3 float traces with the exposure held;
  - `analyze.py`: RT passes "lower", GI "not higher" against the OFF pair (sane_gates), energy per preset,
    downgrades INFO.
  - 625 s planned plus 260 s readiness allowance; predicted.json first.
  - `pipeline_readiness.py`: rt_omm_reference selects no pipeline.
