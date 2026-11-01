# gi_far OFF / ON / OFF (live_batch gi-far), NOT authorized

Decides whether `gi_far` (docs/TRACK_C_GI.md "Far-field sun bounce") stays and what it costs. Lean format of
[../lean_regression/README.md](../lean_regression/README.md): one foreground launch per save, FG 2, A/B by toggling
`gi_far` inside the run, `rt_stats` only in `diag-on`, 20 s diag-off `cost-*` windows, one PNG trace per `shot-*`
phase, static camera. Independent jobs. Receipt `gi-far.json`. The test tree must hold this branch's build
(the key and its variant are not on main).

Startup (both jobs): `preset=ultra`, `rt_hit_material=1`, `rt_hit_material_instanced=1`, `rt_cascade_casters=1`,
`gi_rt=1`, `gi_rt_radius=1.0`, `gi_rt_fallback=0`, `gi_hit_direct=1`, `gi_hit_sun=1` (in every phase), `gi_far_m=6`,
`rf_hit_shade=0`, `rt_hit_emission=0`, `rt_emissive=0`, `sharc=0`. Only `gi_far` toggles.

| Job | Save | Plan | Why this scene |
|---|---|---|---|
| `gf1-camp-fg2` | Campsite - 27h | 113 s, 3 PNG | S3 outdoor sun: far sun-lit hidden hits (`qualified sun-lit` > 0 expected) |
| `gf2-s8-fg2` | Water Queens House | 113 s, 3 PNG | S8 roofed interior, the PT reference scene: the roof rule (`roof rule` > 0 expected) |

Phases: `warmup-on` 10 s (compiles the RT_GI_FAR variant), `diag-on` 20 s (gi_far ON, rt_stats: the `GI far` line),
`cost-off-a` 20 + `shot-off-a` 6, `cost-on` 20 + `shot-on` 6, `cost-off-b` 20 + `shot-off-b` 6, `restore` 5. Trace at 4 s.

## Deciding lines and gates
- Validity: every expect line, among them the `RT_GI_FAR=1 generation` compile line, `gi: gi_far on (rays 6.0 m, base
  radius 1.0 m, sharc 0)`, `gi: gi_far off`, `GI far (...)`, `GI hit sun`, `gi: trace pass ... GPU median`.
- Energy: float mean luminance of the scene colour before post (pass 5c3fdabc, slot 0, each trace's `dumps.json`;
  never PNGs, TRACK_D_LOCAL_LIGHTS 9.11), ON vs the OFF-a/OFF-b mean against |OFF-b - OFF-a|. The change must be
  explained by the `GI far` line of the same run: per ray, ambient visibility ON vs old rule (the native ambient the
  AO fold removes) and bounce ON vs old rule (what the composite adds x albedo). Brighter or darker beyond the OFF
  spread is acceptable only with that sign in the line; an unexplained change fails.
- Cost: `gi: trace pass` GPU median ON minus the OFF mean (OFF spread reported); offline estimate +1.2 ms at 4K
  Performance for 6 m without hit shading (docs/TRACK_C_GI.md). Reported, not gated.
- Participation: `GI far` qualified sun-lit and screen hits beyond the base radius; camp expects qualified > 0.
- Images: shot-on vs shot-off-a/b, look for leaks under roofs (S8) and stippling (sparse far sun hits through RR).

Duration about 8 min (dry run estimate), disk about 0.4 GB (6 traces); delete the trace folders after reading.
Offline check: `powershell -File tools\live_batches\gi_far\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01: all checks passed (Windows PowerShell 5).
