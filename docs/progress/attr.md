## Ultra GI attribution batch (abandoned after the S8 result, 2026-11-05)

- `tools/live_batches/opt_r6/attr`: inn and S8, one launch each, Ultra dev profile.
  Windows: base-a, near-only (gi_far=0, sun on), base-b, sun-off (gi_hit_sun=0), base-c;
  480 frames each, both variants prewarmed first. Diagnostic only: image changes allowed.
- analyze.py reports variant minus the mean of the adjacent baselines, and baseline drift.
- First S8 result (VALID, opt-attr-s8.1.173918): GI trace 8.0 -> 2.1 ms with sun-off
  (which also removes gi_far), -> 1.5 ms with direct-off. Sun and far are confounded;
  the near-only window separates them.
- Readiness fixes: cached GI variants need only a fresh QUIET after the settings
  acknowledgement; at the inn QUIET came while 3 GI builds were pending, so the ready
  phases now wait for GI idle or burst completion after the acknowledgement.
- OMM, AO and direct-off windows dropped to keep each launch under 5 minutes.
- Lint frozen and non-frozen pass; attr test 5/5.
- Abandoned: the inn never passed readiness; the batch, its test and the GI-idle readiness change
  were removed (readiness back to eb935214). The far/sun split continues offline in frame_replay.
