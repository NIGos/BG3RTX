# Water hidden-hit shading: S8 draft

One Water Queens House launch, Ultra water, visible_unfocused_fg. Not queued.
Static and smooth turn windows toggle rt_water_hit_shade 0/2/0/2 with
rt_water_compose_shift=1. Turns reuse wf9 direction +1, one pixel per four
rendered frames, and equal opposite returns. Cost windows have no diagnostics.
Every ON window requires hidden > 0 AND offscreen > 0 in BOTH extended water
stats lines. Mode 2 queries sphere-culled nearest lights outside the grid.
Only the current frame's froxel membership union certifies active point/spot
slots and their types. Raw unlisted slots are stale (docs/ASSET_DB.md).
A hit may be off-screen even when its lights also illuminate the camera volume.
Lights absent from all current froxels and world local-probe queries remain gaps.
At most 32 local lights; truncation retains the probe and is counted in both logs.
No gate or RF bias threshold was relaxed.
Record compose GPU median/p90 and reflection GPU ms versus OFF; cost is unknown. Added median and p90 GPU budget: 1.0 ms.
Hit shading accepts F or F-1 lighting only with the same TLAS and lighting list.
F-1 additionally requires compose shift into its lighting camera space; mismatches keep the probe.

Energy: 16 s shot phases with trace_at [4,7,10], 3 static traces per state, no motion before all four shots. The job
automatically runs the existing energy analyzer with post_gate=true; to inspect:
`py -3 tools/m4_promotion_gate.py --batch water_offscreen --pool run`
The job selects water-compose: pre-post scene float mean plus in-frame compose
delta, exposure-normalized. Require BOTH ON means within the OFF-repeat noise
and encoding band. Never use PNG/bloom energy or rescale radiance. Commit the
prediction before an authorized live run; read-only Git prevents that here.

`./tools/live_batches/water_offscreen/dryrun.ps1` uses a fake scratch queue only.
Estimated plan 249 s plus loading (7 min dryrun budget), 12 traces, no float bursts, under 1 GB. Final restore clears
the experimental keys and exposure hold; runner restores per-run overrides.
