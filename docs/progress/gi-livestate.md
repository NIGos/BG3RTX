## Compact GI hit state (branch gi-livestate, qualified always on, 2026-11-05)

- Added frame_replay `--pipeline-stats`, pack-free non-SHaRC GI `--compile-only`,
  uint32 `--spec`, exact `--reference-out` output/counter gates and repeat median/range/spread.
- `rt_compact_hit_state=1` by default and in every preset, specialization ID 1029.
  No UI knob; release policy forces 1. `dev=1` preserves a raw 0 for A/B.
  ON retains the already evaluated triangle normal across local-light queries instead of
  retaining the complete hit for sun/AO normal reconstruction. Guard increments are replayed.
  Rays, samples, precision and accumulation order are unchanged. Request delta: 0; ABI unchanged.
- RTX 5090 production `gi_trace_rt_hit_sun_em` request: RT_SPEC_NATIVE/HIT_INST/CASCADE/GI_FAR=1;
  specs 1000/1003/1007=1, 1008/1010=0, 1004 tested at both 0 and 1 (remaining defaults).
  Registers baseline/OFF/ON: 255/255/243 for both wind values; spill bytes 816/816/44.
  Occupancy unchanged at 8 warps; the gain is from spill reduction.
  Replay `gi_hit_direct` registers: 168/168; shared bytes 8192 -> 4608 (all three packs).
- Offline replay (24 dispatches per run): ON differences are within repeated OFF noise.
- Live inn Ultra: two VALID runs `opt-compact-hit-inn.1.105908` and
  `opt-compact-hit-inn.1.110402`. GI trace OFF/ON/OFF/ON:
  12.721/12.555/13.228/12.634 and 12.683/12.563/13.115/12.843 ms.
  Every ON below preceding OFF (-0.17, -0.59, -0.12, -0.27 ms; mean about -0.29 ms).
  GI total and other passes within OFF spread. Camp was confounded by scene drift:
  all passes moved about 7 ms between pairs; not used.
  Qualified as exact (100% bit-equal offline), with no regression. Gain about 0.3 ms
  on inn GI trace, not separated from OFF repeat spread (0.43-0.51 ms).
- Every final ON and current OFF output matches baseline: giRaw 10,926,080/10,926,080,
  giRawDepth 2,731,520/2,731,520 components; raw counters 32,768/32,768. All 100% bit-equal.
  Packs use their recorded defines: S8 none, camp/inn RT_CASCADE_MASKS=1.
  Older camp/inn trials reused S8's module; confounded and superseded by these final runs.
- Rejected projection-matrix reload: registers 255 -> 247, but S8 changed 50 output
  components and 49 counter bins. Removed; no precision relaxation.
- OFF release equivalence against 79dc9c17: 214/214 unique requests (188 identical, 26 equivalent),
  2,120 specialization assignments, negative control PASS. Only 28 uncached entries compiled;
  132 entries reused preprocessed-text cache matches. Evidence: .scratch/gi-livestate/*final*.
- Promotion: scripts/build.ps1 -Test PASS, 184/184 CTests in 189.60 s, BelowNormal; no game or queue.
- Check offline with tools/frame_replay.md (`--spec 1029=0/1`); developer A/B at the inn
  uses `dev=1` and raw `rt_compact_hit_state=0/1` in the same run.
  Default-OFF requests retain release equivalence; default spec value is now 1.
  Promotion changes no shader requests (delta 0). Merge-green selects full scope for
  settings/native changes: build/CTest, runtime shader compilation and QA detectors;
  no separate identity/release-equivalence job is registered. Heavy DXC deferred to it.
