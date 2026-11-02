# S6 reruns in one launch (live_batch m4-s6-all), NOT authorized

After 385efd1 (the hit-material capture skipped every traced frame, so every earlier traced energy of gi_hit_direct,
gi_hit_sun, hit emission and gi_far measured the features OFF), the three S6 jobs in one launch of Adamantine Forge
(`m4s6-all-fg2`, plan 618 s, about 12.3 min with the load, 35 PNG traces at about 40-45 MB):

1. m4p3's phases (the + Bounce steps, emission em-off / on / em-off, cost-ao0), as in `../m4_promotion`.
2. The alpha pool rows (`rt_hit_material_pool_alpha`, `../m4_pool_alpha`): diag-pa-off / -on (rt_stats), cost-pa-*,
   shots pa-off-a / pa-on-1 / pa-off-b / pa-on-2 / pa-off-c, the bundle on throughout.
3. m4p5's phases (`../m4_promotion/s6_diag`): the base variant set live, the replay pack, the GI shots and the
   gi_debug views; its emission repeat renamed `em2` (no label collides with m4p3's).

Predictions: predicted.json (derived offline from the S6 pack 021627 and the hit counters; the derivation is its
`_rule`). Gates: one trace in every shot window, scene wait 0 timeouts and capture drops 0 in every cost window and
m4p5's 8 s+ windows, cascade buffer full 0 where the casters are on, pool rows > 0 in diag-pa-on, hit emission in
diag-em2. Analysis: `py -3 -m tools.m4_promotion_gate <log>` (energy per view: shot, shot-pa, shot-vis, shot-add,
shot-em2; cost triples against cost-off-a/b and the em-off pair). Offline check: `dryrun.ps1`.
