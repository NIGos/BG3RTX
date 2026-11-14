# Strand edge flicker on still pixels (live_batch hair-edge-still), READY_FOR_LIVE, NOT authorized

Question: after RR, does `rr_hair_strand_aa=2` (docs/RTX_HAIR_PRIMARY.md 5n) lower the temporal flicker of strand
edges, once animation is excluded? Predictions: [predicted.json](predicted.json), committed before the job.

Job (`py -3 tools/live_batches_staged/hair_edge/make_job.py`): `he2-dialogue1-still`, one Test Dialogue1 launch (the
only save the hair jobs use with strands large on screen), today's RTX Hair On bundle (26 keys, read from
`common/settings.h` by `perf_ab.make_jobs.bundle()`, with `rt_hair=1`), `visible_unfocused_fg`.
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

Regenerated on 2026-11-15 (branch claude/hair-edge-still-2) against today's settings: the bundle above instead of he1's
hand copy, no `rt_xfb_mb` / `rt_cascade_mb` pins (the capture buffers follow the demand since 2026-11-14), none of
the six keys removed on 2026-11-14. Lint: `py -3 tools/live_batches/live_flow.py tools/live_batches_staged/hair_edge`
and the same with `--frozen`: 0 failing (the burst windows' duration notes are intended: a burst is a timed window).
