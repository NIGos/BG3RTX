## SHaRC on Ultra: live job prepared; cheaper gi_far measured (branch sharc-offline, READY_FOR_LIVE, 2026-11-02)
- SHaRC offline: not measurable on the packs. frame_replay has no SHaRC set 4, and a one-frame pack has no cache
  history (the cache is filled by screen injection over frames; 2.10-2.16 found 1.5-30% of non-screen hits live).
  What it could add, from the PT splits: S8 off screen up to 35.8% + multi-bounce 24.9% of the PT bounce, camp off
  screen 38.1%. gi_far already takes visible-far and qualified hidden sun-lit hits. No code change: the sharc +
  gi_far + hit_sun variant exists (gi_trace_rt_sharc_hit_sun.hlsl, RT_GI_FAR).
- Live job `tools/live_batches/sharc_far` sh1-s8-fg2 (113 s, 3 PNG, dryrun passes): Ultra + gi_far, only sharc
  toggles. predicted.json +1.5% (carried over from 2.15, written before the run). Observables: energy, cache
  found share, GI trace and resolve/inject cost.
- Cheaper gi_far (replay, live defines, rt_stats 0, GPU median of 9, `frame_replay --push rays=N`):
  | pack | off, 2 rays | 6 m, 2 rays | 6 m, 1 ray | 4 m, 2 rays | 3 m, 2 rays |
  |---|---|---|---|---|---|
  | S8 giRaw.a / ms | 0.8790 / 0.92 | 0.8320 / 4.31 | 0.8321 / 1.73 | 0.8444 / 2.68 | 0.8528 / 2.36 |
  | camp giRaw.a / ms | 0.8149 / 2.68 | 0.7492 / 7.36 | 0.7492 / 3.90 | 0.7636 / 6.62 | - |
  One ray per pixel (gi_rr_rays=1 for the whole GI pass) keeps the mean visibility and bounce (S8 bounce 0.01763 vs
  0.01761) at 40-53% of the 2-ray cost; the price is variance into RR. 4 m / 3 m lose 26% / 44% of the S8 change.
  Not changed; a live 1-ray A/B (flicker, stippling) would decide it.
- Risk noted: gi_far screen hits beyond 1 m take no roof rule; a ray leaving a roofed room through the cut-away roof
  can reach on-screen exterior ground (S8 went darker as predicted, so not dominant).
