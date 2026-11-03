# Water hidden-hit shading: S8 draft

One Water Queens House launch, Ultra water, visible_unfocused_fg. Not queued.
Static and smooth turn windows toggle rt_water_hit_shade 0/1/0/1 with
rt_water_compose_shift=1. Turns reuse wf9 direction +1, one pixel per four
rendered frames, and equal opposite returns. Cost windows have no diagnostics.
Every ON window requires hidden > 0 AND offscreen > 0 in BOTH extended water
stats lines, as requested. The offscreen gate is predicted to fail: the shared
native light grid rejects truly out-of-frustum hits. Keep this failure visible;
this candidate cannot qualify full offscreen shading until a world-light provider
exists. No gate or RF bias threshold was relaxed.
Record compose GPU median/p90 and reflection GPU ms versus OFF; cost is unknown.
The same-frame/TLAS borrowed-descriptor guard stays conservative: earlier
lighting falls back to ordinary compose, so ON compose coverage may fail.

Energy: 3 static traces per state, no motion before all four shots. The job
automatically runs the existing energy analyzer with post_gate=true; to inspect:
`python tools/m4_promotion_gate.py --batch water_offscreen --pool run`
The job selects water-compose: pre-post scene float mean plus in-frame compose
delta, exposure-normalized. Require BOTH ON means within the OFF-repeat noise
and encoding band. Never use PNG/bloom energy or rescale radiance. Commit the
prediction before an authorized live run; read-only Git prevents that here.

`./tools/live_batches/water_offscreen/dryrun.ps1` uses a fake scratch queue only.
Estimated plan 225 s, 12 traces, no float bursts, under 1 GB. Final restore clears
the experimental keys and exposure hold; runner restores per-run overrides.
