# RF replay capture: READY_FOR_LIVE

Prepared only. No authorization, staging, queue or game launch.
ABI 27 unchanged; rebuild the diagnostic host and core before capture.

- Job: `jobs/rf-pack-inn.json`; save `The Blushing Mermaid - 96h 39m`.
- Ultra, default reflections without hit shading, stock hair, non-OMM; static camera throughout.
- One launch: 20 s warmup with a fresh pipeline-quiescence witness (120 s timeout),
  20 s diagnostic capture window with `replay_pack=1,replay_pack_pass=1`, 5 s restoration to 0.
- One RF frame per serial. Pack capture is diagnostic work, not a cost measurement.
- `visible_unfocused_fg`, no traces, no PNG dumps, no automatic pack deletion.
- Pack cap 2048 MiB; the writer reports actual bytes and rejects an incomplete RF pack.
  Actual inn size and replay/live equality remain unmeasured until authorized capture.
- Gates: successful RF pack write, scene/core readiness; fail on
  capture failure, budget failure, shader failure or device loss.
- Expected delta: replay inputs and references become available; rendering is unchanged.
  No optimisation or GPU-time saving is claimed by this capture.

After capture, replay the manifest with `frame_replay --pack <dir> --repeat 2`.
Keep output/counter mismatch diagnostics. Use `--pipeline-stats` and
`--ab-spec ID=A,B --ab-require-equal` for a candidate only after restoring the
captured production specs. Synthetic checks do not qualify live geometry identity.

Offline lint (neither command queues anything):
```
python tools/live_batches/live_flow.py tools/live_batches/rf_pack --frozen
python tools/live_batches/live_flow.py tools/live_batches/rf_pack --cache .scratch/rf-pack-corpus.json
```
Both pass structural checks; new capture expect lines lack historical matches.
