## Compact GI hit state (branch gi-livestate, kept offline, 2026-11-05)

- Added frame_replay `--pipeline-stats`, pack-free non-SHaRC GI `--compile-only`,
  uint32 `--spec`, exact `--reference-out` output/counter gates and repeat median/range/spread.
  The driver feature is optional; every executable statistic is printed verbatim.
- `rt_compact_hit_state=0` by default, developer Advanced knob, specialization ID 1029.
  ON retains the already evaluated triangle normal across local-light queries instead of
  retaining the complete hit for sun/AO normal reconstruction. Guard increments are replayed.
  Rays, samples, precision and accumulation order are unchanged. Request delta: 0; ABI unchanged.
- RTX 5090 production `gi_trace_rt_hit_sun_em` request: RT_SPEC_NATIVE/HIT_INST/CASCADE/GI_FAR=1;
  specs 1000/1003/1007=1, 1008/1010=0, 1004 tested at both 0 and 1 (remaining defaults).
  Registers baseline/OFF/ON: 255/255/243 for both wind values; shared bytes 816/816/44.
  Replay `gi_hit_direct` registers: 168/168; shared bytes 8192 -> 4608 (all three packs).
- Final GPU replay results (ms; 24 dispatches per run, median [max-minus-min spread], first included):

  | Pack | Baseline OFF | Current OFF repeat | Current ON |
  | --- | --- | --- | --- |
  | S8 20261101-142819-1 | 16.329 [6.654] | 9.956 [12.815] | 9.945 [7.291] |
  | Camp 20261102-013545-2 | 50.149 [15.568] | 41.269 [24.617] | 44.663 [21.434] |
  | Inn 20261102-013914-3 | 12.090 [10.689] | 11.988 [3.127] | 11.374 [11.399] |

- No GPU-time saving is qualified: ON differences are within repeated OFF noise.
  Kept on the production register reduction (12 registers, 4.7%), as the offline gate permits.
- Every final ON and current OFF output matches baseline: giRaw 10,926,080/10,926,080,
  giRawDepth 2,731,520/2,731,520 components; raw counters 32,768/32,768. All 100% bit-equal.
  Packs use their recorded defines: S8 none, camp/inn RT_CASCADE_MASKS=1.
  Older camp/inn trials reused S8's module; confounded and superseded by these final runs.
- Rejected projection-matrix reload: registers 255 -> 247, but S8 changed 50 output
  components and 49 counter bins. Removed; no precision relaxation.
- OFF release equivalence against 79dc9c17: 214/214 unique requests (188 identical, 26 equivalent),
  2,120 specialization assignments, negative control PASS. Only 28 uncached entries compiled;
  132 entries reused preprocessed-text cache matches. Evidence: .scratch/gi-livestate/*final*.
- scripts/build.ps1 -Test PASS: build 15 s, 183/183 CTests in 204 s, including settings,
  frame replay and GPU fixtures; all BelowNormal. Game never launched; no queue access.
  tasklist is denied here; Get-Process checked bg3 absent before each GPU command.
- Check offline with tools/frame_replay.md (`--spec 1029=0/1`); no live GPU-time claim.
  Commit currently blocked: linked-worktree Git metadata is outside writable sandbox roots.
