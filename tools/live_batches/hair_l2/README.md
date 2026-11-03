# Hair L2 batch (live_batch hair-l2), READY_FOR_LIVE, NOT authorized

Question: live L2 of docs/RTX_HAIR_COMPLETE_PLAN.md (M2 shadows, M3 image). With strands on screen, does strand
shadow transmittance (`rt_hair_groom_shadow_t`, C2) lift the skin and neck under hair without a brightness change
elsewhere, and do dithered strand tips (`rt_hair_groom_tips`, C3a) fade the tips without flicker or energy change?
What does each cost? Predictions: [predicted.json](predicted.json).

Jobs (`py -3 make_job.py`, plan `plans/l2_plan.json`, 228 s per launch, ~11 min and ~2.6 GB in all): `l2a-dialogue1-fg2`
(Test Dialogue1) and `l2b-campsite-fg2` (Campsite - 27h). Two launches: the queue shares a game only between jobs of one save.
Config: the hair_crowd job's (hair_qual Ultra keys, hair startup latches, `rt_hair_groom_primary=1` latched at startup,
`visible_unfocused_fg`). Strands on screen in every phase.
Phases: warm-ups for each key value; shots base / shadow / base / tips / base / both (8 s, one full-resolution PNG at
5 s); cost base / shadow / tips / base (25 s each).

Gates: hair_crowd's window gates without the crowd plan; strand G-buffer, velocity and groom participation in every
cost window; the `[hair primary] shadow transmittance:` line with measured draws and groom rows in cost-shadow.
After the run: character energy per shot pair (float pre-post mean, ROI as hair_primary); cost deltas against
mean(cost-base-a, cost-base-b); the user judges the shots.
