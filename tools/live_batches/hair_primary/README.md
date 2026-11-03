# Primary strand hair A/B batch (live_batch hair-primary), READY_FOR_LIVE, NOT authorized

Question: does `rt_hair_groom_primary` (docs/RTX_HAIR_PRIMARY.md) replace Shadowheart's visible hair cards by the
strand groom (fibres in the G-buffer, strand motion vectors), at what GPU cost? Predictions: [predicted.json](predicted.json).

Requires: what the hair_groom batch requires (Shadowheart's LOD1 groom file in data/assetdb/hair_grooms, LSS GPU),
plus shaderInt64 (`sharc_device=1`, default) and fragmentStoresAndAtomics on the game's device; this branch merged
into the test tree.

Job (one foreground launch, `py -3 make_job.py`, plan `plans/hp_plan.json`, 190 s): `hp1-dialogue1-fg2`
(`Test Dialogue1`). Config: the hair_groom job's (hair_qual Ultra keys, hair startup latches, `rt_hair_groom=1`).
Phases: `warmup` 25 s, `settle-cam` 10 s (strands off); `warm-strands` 20 s (first on: UV/previous-point ranges,
rebuilds) and `warm-back` 10 s; shots cards / strands / cards / strands / cards (8 s, one full-resolution PNG trace
at 5 s, exposure held); `settle` 5 s; cost `cost-cards-a` / `cost-strands` / `cost-cards-b` (25 s each); `restore`.

Gates (expect_every): the hair_groom window gates (scene waits, capacity, cutouts, background builds, freeze, hair
invalid 0) in judged windows; strand participation (G-buffer and velocity strand draws > 0, 0 pipelines refused)
and a strand G-buffer draw GPU sample in cost-strands; groom instances, rt passes / gi / scene build GPU samples in
every cost window. Forbidden: `[hair primary] UNAVAILABLE`, refused or failed variants, the usual crash/pipeline
failures.

After the run: the user judges shot-cards-* vs shot-strands-* at full resolution; character float energy with
`char_energy.py`; cost = cost-strands minus mean(cost-cards-a, cost-cards-b) and the strand draw line. Disk: 5
traces x ~220 MB.
