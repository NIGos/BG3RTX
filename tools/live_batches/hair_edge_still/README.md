# Strand edge flicker on still pixels (live_batch hair-edge-still), READY_FOR_LIVE, NOT authorized

Question: after RR, does `rr_hair_strand_aa=2` (docs/RTX_HAIR_PRIMARY.md 5n) lower the temporal flicker of strand
edges, once animation is excluded? Predictions: [predicted.json](predicted.json), committed before the job.

Job (`py -3 tools/live_batches/hair_edge_still/make_job.py`): `he2-dialogue1-still`, one Test Dialogue1 launch (the
only save the hair jobs use with strands large on screen), he1's RTX Hair bundle, `visible_unfocused_fg`.
warm-all, warm-inputs, warm-hold (latches the exposure, `qa_exposure_hold=1` from here on), then eight pairs
settle-i (108 frames, the mode set here) / burst-i (16-frame burst with the RR inputs, timed 14 s) in the order
off aa aa off off aa aa off, restore. About 6.3 min with the load; about 4.4-4.8 GB of bursts (one-off: delete the raw
bursts after the summary is written).

Gates: zero refused strand pipelines, zero orphan depth, swapped frames and the render-res chain line in every settle
window; G-buffer-end strand coverage > 0 in every aa settle window; the eight burst completions (16 frames, transport
ok); the exposure-held line; both mode lines; the strand resolve and snapshot lines. Forbidden: a resolve period with no
frame, a refused snapshot, a pipeline or compile failure, an INVALID burst transport.

After the run:
`py -3 tools/hair_groom/edge_burst.py <burst dirs in plan order> --modes off,aa,aa,off,off,aa,aa,off`.
It prints INVALID / PASS / FAIL for final and rr_color (still pixels: summed RR motion over the burst <= 1/4 px), the
positive control (rr_color band hf of every aa burst below the off mean) and each burst's half-burst drift.
Cost from the `[rrpost] gpu: render-res chain` lines of the settle windows.

Dry run: `tools/live_batches/hair_edge_still/dryrun.ps1 -Out <scratch>`: all checks pass except the
producer-sanity-warm-inputs diagnostics role, which live_flow's startup_producers inserts (he1 fails it the same way).
