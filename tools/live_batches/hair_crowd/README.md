# Hair crowd batch (live_batch hair-crowd), READY_FOR_LIVE, NOT authorized

Question: with every hairstyle converted (tools.hair_groom.batch) and the crowd budget
(`rt_hair_groom_crowd_ksegments`, docs/RTX_HAIR_PRIMARY.md increment 5), how many hair draws match per scene,
what does each mode cost, and does the budget keep a crowd's cost congruous? Predictions: [predicted.json](predicted.json).

Jobs (`py -3 make_job.py`, plan `plans/hc_plan.json`, 247 s per launch, 12.2 min and ~1.8 GB in all): `hc1-dialogue1-fg2` (Test Dialogue1) and
`hc2-campsite-fg2` (Campsite - 27h). Two launches: the queue shares a game only between jobs of one save.
Config: the hair_primary job's (hair_qual Ultra keys, hair startup latches, `rt_hair_groom=1` at startup).
Phases: warmup / settle (strands in RT), warm-strands, warm-crowd, warm-back; shots cards / strands on screen /
strands + crowd budget / cards (8 s, one full-resolution PNG at 5 s); cost cards / RT / on screen / crowd / cards
(25 s each).

Gates: hair_primary's window gates; groom instances in every groom-on cost window; strand G-buffer and velocity
draws and the strand cost line in cost-strands and cost-crowd; the crowd plan line in cost-crowd.
After the run: matched and unmatched styles per scene from the log; cost per mode; the user judges the shots.
