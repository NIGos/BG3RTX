# wsf1 qualification run, 2026-11-04 06:44 (VALID)

Build 7a52fb70, real Ultra, S8 Water Queens House pool rim, visible_unfocused_fg, 281 s. Key sequence 0/1/0/1
in one session, exposure held (EV 11.43). All gates passed: owned > 0 and invalid 0 in every ON window, energy
partition within 8 float eps, 8/8 paired post-water float shots, surface FS timer dropped 0.

Post-water float luminance (Rec. 709 mean of the `_p.f32` preview, two shots per window):

| window | key | pool rim | open water |
|---|---|---|---|
| static-0 | 0 | 0.1159, 0.1134 | 0.1651, 0.1641 |
| static-1 | 1 | 0.1780, 0.1554 | 0.1684, 0.1653 |
| static-2 | 0 | 0.1153, 0.1159 | 0.1643, 0.1643 |
| static-3 | 1 | 0.1634, 0.1604 | 0.1675, 0.1678 |

- Pool rim: ON 0.164 vs OFF 0.115 mean, +43%. OFF repeat spread 2.2%. Clean-water interface reflection retained
  where native foam shading darkened the shallow rim (predicted direction; size had no prior).
- Open water: ON 0.167 vs OFF 0.164, +1.8% (OFF spread 0.6%): small, mostly unchanged as predicted.
- ON shots vary more than OFF within a static window (rim 0.178 vs 0.155, 13%); OFF varies under 2.2%. Check for
  temporal shimmer at the rim in game.
- Cost: surface FS 0.10-0.15 ms median ON (p90 0.22), absolute, OFF untimed; compose 0.61-0.67 ms (prior compose
  delta +0.026 ms).
- Not a reference match: a shading-model change, no PT reference for this scene. Image quality needs the user's look.

Decision: gates pass; promotion waits on the user's visual check at the S8 pool rim (key in Advanced).
