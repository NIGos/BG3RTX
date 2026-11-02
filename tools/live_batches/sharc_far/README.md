# SHaRC on Ultra with gi_far (live_batch sharc-far), NOT authorized

Question: does the SHaRC radiance cache (`sharc`, off in every preset; docs/DESIGN_SOTA_NEXT.md 2.9-2.16) still
work on the current Ultra (hit materials, gi_hit_sun, gi_far 6 m) and what does it add at S8? Lean format of
[../lean_regression/README.md](../lean_regression/README.md): one foreground launch, FG 2, only `sharc` toggles
live, static camera, exposure held from `cost-off-a`. `sharc_device=1` at startup (the default).

| Job | Save | Plan | Why this scene |
|---|---|---|---|
| `sh1-s8-fg2` | Water Queens House | 113 s, 3 PNG | S8: the PT reference scene, last measured SHaRC scene (2.15) |

Phases: `warmup-on` 10 s (compiles `gi_trace_rt_sharc_hit_sun.hlsl?...RT_GI_FAR=1`), `diag-on` 20 s (`rt_stats`,
`sharc_stats`: `GI far` and `sharc stats ... cache found`), `cost-off-a` / `cost-on` / `cost-off-b` 20 s with a 6 s
`shot-*` after each (one trace at 4 s), `restore`. No `gi_debug` views: they pause the injection and the cache clears.

## Gates
- Validity: every expect line (the sharc variant compile, `sharc stats ... cache found`, `sharc: resolve GPU median`),
  no `sharc: allocation` or `device lacks` line.
- Energy: `py -3 -m tools.m4_promotion_gate <log>`, shot-on against predicted.json (+1.5%, carried over from 2.15).
- Participation: `cache found ... of queried` > 0 in diag-on (2.12: S8 22%).
- Cost: `gi: trace pass` ON minus mean OFF, plus `sharc: resolve ... inject` medians (2.14: +0.37 ms at 4 m).
- Images: shot-on vs shot-off-a/b: no light through walls under the cut-away roofs (S8 2.15 crops were shaded stone).

## Second section: gi_rr_rays 2 / 1 / 2 (sharc 0, Ultra + gi_far 6 m, exposure held)
`cost-rays-off-a` / `cost-rays-on` / `cost-rays-off-b` 20 s, each followed by a 10 s `shot-rays-*` with three traces
(2, 5, 8 s). Plan 203 s in all, 12 PNG traces (~0.7 GB).
- Energy: the gate tool's `shot-rays` view, predicted null (replay: giRaw.a 0.8321 vs 0.8320).
- Cost: `gi: trace pass` of cost-rays-on against the rays-2 windows, predicted -2.6 ms (replay 4.31 -> 1.73 ms).
- Flicker / stippling: `py -3 tools/live_batches/sharc_far/rays_flicker.py <log>`: the QA temporal detector (final
  frame PQ code, static px, derived limits) on the three rays-1 traces against each rays-2 set; the OFF-b vs OFF-a
  null must pass, else the verdict is void. Static camera only (a pan needs K16 bursts, 2.3 GB each).

Offline check: `powershell -File tools\live_batches\sharc_far\dryrun.ps1 [-Out <scratch dir>]`.
