# Strand edge resolve (live_batch hair-edge), READY_FOR_LIVE, NOT authorized

Question: with RTX Hair on, does `rr_hair_strand_aa` (docs/RTX_HAIR_PRIMARY.md 5m) remove the dithering the user saw
on strand edges, without an energy change, and what does it cost? Predictions: [predicted.json](predicted.json).

Job (`py -3 tools/live_batches/hair_edge/make_job.py`): `he1-dialogue1-edge`, one Test Dialogue1 launch, the RTX Hair
bundle on throughout (startup `rr_hair_strand_aa=2` so the strand_resolve pipeline exists). Windows of one 60-frame
stats period each: a-off (0), b-aa (2), c-off (0), d-aa (2), e-untag (1), each with a PNG trace; then K8 pan bursts
burst-off (0) and burst-aa (2) with the hv9 pulsed pan and its return; restore. `visible_unfocused_fg`.

Gates (every window): zero refused strand pipelines, zero orphan depth, swapped frames, the render-res chain GPU line;
the mode-change line in b/c/d/e; the K8 burst completion lines; at startup the hair pipeline stencil lines and the
strand_resolve pipeline. Forbidden: a strand resolve period with no frame run.

After the run: `py -3 tools/hair_groom/temporal_burst.py <burst-off> <burst-aa> --labels off,aa` (flicker, mc, hf of
rr_color and final on the hair region); energy from dumps.json of a/b/c/d (char_energy.py); cost from the
`[rrpost] gpu: render-res chain` lines; the user judges the d-aa / c-off PNG pair (strand silhouettes, flyaways).
