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

Offline check: `powershell -File tools\live_batches\sharc_far\dryrun.ps1 [-Out <scratch dir>]`.
