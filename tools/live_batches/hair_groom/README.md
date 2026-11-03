# Hair groom A/B batch (live_batch hair-groom), READY_FOR_LIVE, NOT authorized

Question: does the strand groom (rt_hair_groom, docs/RTX_HAIR_INTEGRATION.md) lower the native hair cost in the
secondary rays without changing the character energy? Predictions (committed before the run): [predicted.json](predicted.json).

Requires on the test PC: `data/assetdb/hair_grooms/<index id>.hgrm` for Shadowheart's hair, LOD0 (e602d25d6dd783bb)
and LOD1 (a97c222a4f2c0dda: the game draws LOD1 at camp distances; the 10:40 run matched nothing), generated from the
user's paks (never shipped): `python -m tools.hair_groom.asset --gltf <LSLib export of HAIR_HUM_F_Shadowheart_Spring.GR2>`
(the LSLib glTF export is the one in docs/RTX_HAIR_FEASIBILITY.md). And this branch merged into the test tree.

Job (one foreground launch, `py -3 make_job.py`, plan `plans/hg_plan.json`, 255 s): `hg2-dialogue1-fg2`
(`Test Dialogue1`: the only save where the 11:03 rerun saw Shadowheart's hair, LOD1, 6009 indices). The camp job
was dropped: no Shadowheart hair draw there.

Config: hair_qual's Ultra keys and hair startup latches, `rt_hair_groom=1` at startup (the
device feature is negotiated there; later 0/1 is live).

Phases: `warmup` 25 s and `settle-cam` 10 s (groom on: startup builds, groom upload); `toggle-off-a` 20 s /
`toggle-off-b` 10 s (first groom toggle: the constant-1020-off pipelines build in their own slot); `warm-nohair` 25 s /
`warm-back` 15 s (the no-hair pipelines build before the cost windows; 10:40 run: they built in cost-nohair); shots off / groom /
off / groom / off (8 s, one PNG trace at 5 s, exposure held); `settle` 5 s; cost `cost-off-a` / `cost-groom` /
`cost-off-b` / `cost-nohair` (25 s each: one 300-frame rt passes and gi line); `restore` 5 s.

Gates (expect_every): every judged window 0 scene-wait timeouts, no dropped opaque draw, no opaque cutouts, no
background build (0 waiting/pending/started), hair invalid 0 in hair-on windows; groom participation: at least one
`[hair groom] frame N: K groom instances` line with K > 0 in every groom-on window (else INVALID: no matched hair);
complete rt passes / gi / scene build GPU samples in every cost window. Forbidden: `[hair groom] device WITHOUT`,
`linear swept spheres device request failed`, malformed groom files, the usual crash/pipeline failures.

After the run: character float energy with `py -3 tools/live_batches/dialogue_shadows/char_energy.py --root
<captures> shot-off-a=<stamp> shot-groom-a=<stamp> ...`; cost = cost-groom minus mean(cost-off-a, cost-off-b) with
|off-a - off-b| as noise; native hair cost = cost-off minus cost-nohair. Disk: 10 traces x ~28 MB.
