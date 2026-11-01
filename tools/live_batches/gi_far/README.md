# gi_far OFF / ON / OFF (live_batch gi-far), NOT authorized

Decides whether `gi_far` (docs/TRACK_C_GI.md "Far-field sun bounce") stays and what it costs. Lean format of
[../lean_regression/README.md](../lean_regression/README.md): one foreground launch per save, FG 2, A/B by toggling
`gi_far` inside the run, `rt_stats` only in `diag-on`, 20 s diag-off `cost-*` windows, one PNG trace per `shot-*`
phase, static camera. Independent jobs. Receipt `gi-far.json`. The test tree must hold this branch's build
(the key and its variant are not on main).

Startup (both jobs): `preset=ultra`, `rt_hit_material=1`, `rt_hit_material_instanced=1`, `rt_cascade_casters=1`,
`gi_rt=1`, `gi_rt_radius=1.0`, `gi_rt_fallback=0`, `gi_hit_direct=1`, `gi_hit_sun=1` (in every phase), `gi_far_m=6`,
`rf_hit_shade=0`, `rt_hit_emission=0`, `rt_emissive=0`, `sharc=0`. `gi_far` toggles; in gf2's stage windows also
`ls_stats` and `gi_debug`.

**Exposure hold (2026-11-01 run).** The lighting is pre-exposed by s1b13[0] = 0.8333 / 2^EV, so the game's eye
adaptation scales every buffer before post. Unheld, it raised s1b13[0] in the ON shot by 3.2% (S8) and 4.1% (camp):
the raw +2.87% / +3.97% were -0.32% / -0.14% at a fixed exposure. Both plans now set `qa_exposure_hold=1` from
`cost-off-a` to the last window (latched after 30 s of warm-up and diag), 0 in `restore`. The gate tool also divides
by the `[rrpost] exposure probe` value of each trace frame, so held and unheld runs read the same.

| Job | Save | Plan | Why this scene |
|---|---|---|---|
| `gf1-camp-fg2` | Campsite - 27h | 113 s, 3 PNG | S3 outdoor sun: far sun-lit hidden hits (`qualified sun-lit` > 0 expected) |
| `gf2-s8-fg2` | Water Queens House | 157 s, 7 PNG | S8 roofed interior, the PT reference scene: the roof rule; plus the stage windows |

Phases: `warmup-on` 10 s (compiles the RT_GI_FAR variant), `diag-on` 20 s (gi_far ON, rt_stats: the `GI far` line),
`cost-off-a` 20 + `shot-off-a` 6, `cost-on` 20 + `shot-on` 6, `cost-off-b` 20 + `shot-off-b` 6, `restore` 5. Trace at 4 s.
gf2 adds before `restore` (exposure held): `diag-ls-off` / `diag-ls-on` 10 s each (`ls_stats=1`: the ao_direct
give-back), `view-vis-off` / `view-vis-on` 6 s (`gi_debug=3`: diffuse = 0.2 x the folded visibility),
`view-bounce-off` / `view-bounce-on` 6 s (`gi_debug=2`: diffuse = 4 x bounce x diffuse colour), one trace each.

## Deciding lines and gates
- Read with `py -3 -m tools.m4_promotion_gate <log> [...]` (energy table, cost, the "Stages" table).
- Validity: every expect line, among them the `RT_GI_FAR=1 generation` compile line, `gi: gi_far on (rays 6.0 m, base
  radius 1.0 m, sharc 0)`, `gi: gi_far off`, `GI far (...)`, `GI hit sun`, `gi: trace pass ... GPU median`,
  `[rrpost] exposure held (qa_exposure_hold=1)` and `... hold released`, gf2 `ls stats ... ao_direct 1 gives back`.
- Energy: float mean luminance of the scene colour before post (pass 5c3fdabc, slot 0, each trace's `dumps.json`;
  never PNGs, TRACK_D_LOCAL_LIGHTS 9.11) divided by the trace frame's s1b13[0], ON vs the OFF-a/OFF-b mean against
  |OFF-b - OFF-a|. With the hold the exposure ratio is 1.0000 (a check of the hold). The change must be explained by
  the `GI far` line of the same run: per ray, ambient visibility ON vs old rule (the native ambient the AO fold
  removes) and bounce ON vs old rule (what the composite adds x albedo). An unexplained change fails.
- Stages (gf2), predicted from the `GI far` line and the derivation in docs/TRACK_C_GI.md ("Live energy: the eye
  adaptation"): `view vis` ON / OFF = 0.834 / 0.880 = 0.948 (folded visibility); `view bounce` ON / OFF about 1.6;
  `giveback ls` ON / OFF about (1 - 0.834) / (1 - 0.880) = 1.38 (traced direct light itself unchanged). 2026-11-01
  measured (exposure-normalised): S8 diffuse -0.26%, scene -0.32%; offline prediction -2.0%. If `view vis` is 0.948
  the fold works and the shortfall is in how the lighting uses the AO (offline E x AO model); near 1.0 means the
  fold does not receive the far visibility.
- Cost: `gi: trace pass` GPU median ON minus the OFF mean (OFF spread reported); 2026-11-01 S8 0.97 -> 3.00 ms,
  camp 2.63 -> 5.70 ms. Reported, not gated.
- Participation: `GI far` qualified sun-lit and screen hits beyond the base radius; camp expects qualified > 0.
- Images: shot-on vs shot-off-a/b, look for leaks under roofs (S8) and stippling (sparse far sun hits through RR).

Duration about 8.5 min (dry run estimate), disk about 0.6 GB (10 traces); delete the trace folders after reading.
Offline check: `powershell -File tools\live_batches\gi_far\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01: all checks passed (Windows PowerShell 5).
