# RF mismatch recapture: READY_FOR_LIVE

Prepared only. No launch, queue staging or authorization. This copies `rf_pack`
with a fixed-writer witness and one extra diagnostic frame. Use this branch's
rebuilt diagnostic DLLs; an old host fails the required contract marker.

The old inn pack remains unqualified: 1,270,225 specular bytes differ.
The cause is not established. The two-phase RMW bracket is correct, and no
other pass intervenes. Native packed no-hit loads/stores are exact. Lost sampler
fields cannot be recovered from the old pack, so a new capture is needed to
finish checking that hypothesis rather than invent their values.

- Job: `jobs/rf-pack-state1-inn.json`, same inn save and default Ultra RF request.
- Static plan: 20 s warmup, 20 s serial 61 with `rt_stats=rf_stats=0`,
  20 s serial 62 with both statistics keys 1, then 5 s restore to 0.
- This is diagnostic work only, with no GPU-cost claim.
- Each pack requires three distinct raw pre/post output pairs and all 20
  sampler descriptors in STATE1. The writer logs this and stores the witness
  in the manifest. Unsupported sampler/view state fails rather than guesses.
- Expected disk about 1 GB for two packs, based on the old 466 MB pack;
  each compressed pack has the existing 2048 MiB cap. No automatic deletion.
- The contract reports record-time host UBO snapshots separately. It does not
  certify those bytes as GPU-time snapshots; if mismatch persists, inspect
  this remaining capture limitation rather than altering the shader.

After an independently authorized capture, run each new pack with the production
module and its recorded specializations:

```powershell
bin/frame_replay --pack <pack61> --repeat 2 --require-live-equal --out .scratch/rf-opt/state1-default
bin/frame_replay --pack <pack62> --repeat 2 --require-live-equal --out .scratch/rf-opt/state1-counters
```

Both must exit 0, with every output and counter bit equal. If the second fails,
raw RF bins 56/58/59 distinguish probe-term luminance, the initial specular
value and local-probe count from the final packed write. They are diagnostic
evidence, not tolerance gates. Only after exactness passes resume attribution
and the 40-pair `rf_fast` qualification with the 0.05 ms minimum saving.

Offline lint (neither command stages or launches anything):

```powershell
python tools/live_batches/live_flow.py tools/live_batches/rf_pack_state1 --frozen
python tools/live_batches/live_flow.py tools/live_batches/rf_pack_state1 --cache .scratch/rf-opt/recapture-corpus.json
```
