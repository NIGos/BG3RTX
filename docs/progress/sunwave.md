## Deferred hidden-hit sun (branch sunwave, rejected: slower, 2026-11-05)

- Offline only; no game, queue or git writes. RTX 5090, original S8/camp/inn replay packs and generated sun trust.
- Added paired replay: `--ab-spec ID=A,B`, optional `--ab-spv`, 40 balanced ABBA/BAAB samples after warmup.
  Reports every B-A delta, median and exact binomial 95% median interval; checks every output byte and raw counter bin.
  Corrected start timestamp to exclude preceding output clears. Earlier TOP_OF_PIPE timings are provisional only.
- Attribution uses main shaders: far 0 -> 1 with sun enabled; direct -> sun with far disabled. Radius 6 m, two rays, stats off.

| Pack | Far increment, ms [95% CI] | Sun increment with far off, ms [95% CI] |
|---|---|---|
| S8 | +2.429952 [2.312832, 2.456272] | +0.098344 [0.097520, 0.098976] |
| Camp | +5.460104 [5.299728, 5.826256] | +0.427008 [0.122464, 0.703888] |
| Inn | +2.447688 [2.013296, 3.750608] | +0.005360 [0.004672, 0.006176] |

- S8 short rays trace no sun visibility queries (roof/normal suppression); far-enabled S8 traces seven lit rays.
  Camp far-enabled traces 32,173 lit and 42,230 occluded sun rays, exercising the actual continuation.
- Candidate `gi_sun_wave=0`, SpecId 1034: trace records full-precision operands, dense visibility, ordered resolve.
  Preserve already-correct trace output when all queued rays are lit. Replay individual additions, never per-ray sums.
  Statistics-on retains original-lane sun queries for identical raw bins; performance is measured with statistics off.
  SHaRC/OMM fall back. Scratch at 2560 x 1067 x 2 is 1,158,164,496 bytes; includes queue and pixel tails.
- Shader request delta +2 fixed passes: manifest 125 -> 127; all unique requests 215 -> 217. No define products or ABI change.
- All three packs: 100% bit-equal giRaw, depth and all 512 x 64 raw counter bins, with rt_stats 0 and 1; validation clean.
  Every dispatch in the following 40-sample statistics-off runs also matches exactly.

| Pack | Wave ON-OFF median, ms | 95% median interval, ms |
|---|---|---|
| S8 | +0.735776 | [+0.540272, +0.849120] |
| Camp | +3.114312 | [+2.451616, +3.897856] |
| Inn | +0.737392 | [+0.638336, +0.863264] |

- Rejected: all intervals exclude a gain. Keep the raw key off; no further tuning or live test requested.
- Key-OFF release equivalence vs main `5b076d9`: 215/215 ordinary requests pass (203 byte-identical, 12 equivalent),
  2,056 assignments, 12 universal certificates, 24 frozen checks; negative control passes.
- `scripts/build.ps1 -Test` passed: full build plus all 191 CTests (217.09 s). Includes paired statistics,
  ordinary replay/paired GPU control, settings, variants, allocation boundaries and host-atomics lint.
- Reproduce with commands/provenance in `.scratch/sunwave/{attribution,exact,timing}/`; paired CLI in `tools/frame_replay.md`.
