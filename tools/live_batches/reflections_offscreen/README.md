# S8 water hit shading draft

Not queued. No game has been launched. `predicted.json` was written before testing.
Git metadata is read-only for this task, so the prediction is uncommitted.

`wos1-s8-fg2` uses one Water Queens House launch in `visible_unfocused_fg` mode.
The 183 s plan toggles `rt_water_hit_shade` OFF/ON/OFF on a static camera, then
repeats the same forward turn and return for each state. Ultra and its qualified
water effects stay on. There are 12 trace samples, no float bursts, and an estimated
730 MB output budget. The runner supports seconds rather than frame-count phase
termination; 20 s cost windows allow several 120-frame stats windows at 18 fps.

The static energy comparison uses `energy_fs: water-compose`: the float pre-water
scene plus the in-frame water delta, divided by exposure. PNG brightness is not an
energy gate. Run the existing `tools/m4_promotion_gate.py` on the completed run with
this batch's predictions. Require ON mean within the OFF repeat noise band. Do not
rescale the hit radiance to pass. Inspect both water GPU median/p90 lines and the
geometric tier histogram. Every ON cost window must contain qualified hidden-hit
shading; a run with no supported shading is not evidence for the feature.

True outside-frustum hits currently fail the shared shader's light-grid check.
Their probe stays. This batch cannot qualify full off-screen reflections, sky
replacement, or the separate `rf_hit_shade` bias gate. Those remain in
`docs/REFLECTIONS_PLAN.md`.

Offline pack coverage is recorded in `offline_coverage.json`: S8 pack
`replay-20261101-142819-1` is a GI-direct capture without the water TLAS or forward
records. It cannot replay the new water path.

Offline preparation checks:

```powershell
python scripts/queue_gates.py --lint-exposure-hold tools/live_batches/reflections_offscreen
python scripts/queue_gates.py --lint-scene-wait tools/live_batches/reflections_offscreen
python scripts/queue_gates.py --lint-capacity tools/live_batches/reflections_offscreen
& tools/live_batches/lean_dryrun.ps1 -Batch reflections_offscreen -Independent -Unfocused -MaxMinutes 6 -MaxMB 2048
```

The last command uses only a synthetic scratch queue and a fake game. It grants no
live authorization. Complete the candidate's offline checks before requesting or
queueing this batch. The final restore phase turns the key and exposure hold off;
the queue restores its run overrides.
