# rt_omm qualification at S3 camp (live_batch omm-qual), NOT authorized

Decides whether `rt_omm` (opacity micromaps, docs/P4_OMM.md) earns a place in Ultra as a pure performance
feature. Lean format of [../lean_regression/README.md](../lean_regression/README.md): foreground, FG 2, static
camera, `rt_stats` only in `diag-check`, 20 s diag-off `cost-*` windows, one PNG trace per `shot-*` phase.
The test tree must hold this branch's build (the `omm check` / `[omm]` lines and `rt_omm_reference` are new).

**Why two launches.** `rt_omm` requests the device extension at device creation and latches the content-epoch
tracking, so it cannot toggle live. The A/B inside the ON launch is `rt_omm_reference` (live, ls_flags bit 28):
the OMM shader variants keep their scene guard but trace the reference scene (no micromaps). So:
- om1 `cost-on` minus the `cost-ref-a`/`cost-ref-b` mean = the traversal saving of the micromaps (same launch);
- om1 `cost-ref-*` minus om2 `cost-off-*` = the fixed overhead of rt_omm (guard, bake, content epochs, clone
  TLAS), across launches: GPU ms only, never images.

Startup (both jobs): the smoke_ultra set (`preset=ultra` with every qualified key: hit materials, instanced,
gi_hit_direct/sun, rt_emissive + rt_hit_emission, ls_box=2, rt_cascade_casters, rt_scene_wait, fg_letterbox),
`rt_omm=1` (om1) or `0` (om2).

| Job | Plan | Phases |
|---|---|---|
| `om1-camp-omm-on-fg2` | 118 s, 3 PNG | `warmup-on` 20 (compiles the OMM variants), `diag-check` 15 (rt_stats=1), `cost-ref-a` 20 + `shot-ref-a` 6, `cost-on` 20 + `shot-on` 6, `cost-ref-b` 20 + `shot-ref-b` 6, `restore` 5 |
| `om2-camp-omm-off-fg2` | 71 s, 1 PNG | `warmup-off` 20, `cost-off-a` 20, `shot-off` 6, `cost-off-b` 20, `restore` 5 |

Exposure held (`qa_exposure_hold=1`) from the first cost window to the last shot. Save `Campsite - 27h`.

## Deciding lines and gates
- Validity: every expect line, among them `[omm] EXT backend available`, `[omm content] GPU epochs active`, the
  `[omm] last N builds` coverage line, `omm check (...)`, both `gi: rt_omm_reference` toggles, the exposure hold.
- Violations (`expect_every`, `diag-check`): `omm check ... violations visibility 0, closest hit 0;`. The check traces
  each visibility / closest-hit query of the OMM variants again in the reference scene (rt_common.hlsli wrappers)
  when the accelerated scene was current; the answers must be equal (closest hit: the same t). Not covered:
  `rt_closest` (t-only) and `rt_closest_debug` queries.
- Coverage (layer `[omm]` line, every 120 builds): alpha-tested triangles per build, with a native plain capture,
  with an attached micromap BLAS (and the % of alpha-tested), variants baked / reused / declined, accelerated scenes,
  micromap + BLAS bytes, and the cumulative baked microtriangle states (known opaque / known transparent / unknown).
  `omm check` adds the traversal view: candidates in the accelerated vs the reference trace on the same rays, and
  how many queries found no current accelerated scene.
- Cost: `rt passes: GPU cost`, `gi: GPU cost`, `gi: trace pass`, `[rt] scene build GPU (split, static ...)` (the
  bake, the micromap BLAS and the clone TLAS are inside its TLAS part), `[mem]` VRAM. Scene-wait 0 timeouts and the
  capacity gates in every cost window.
- Energy: shot-on vs the shot-ref-a/b mean (pre-post scene colour, `dumps.json`), within |ref-b - ref-a|.
- Reject when: violations > 0; or coverage ~0 (no traversal saving) with any fixed overhead; or ON is not faster
  than REF beyond the REF spread. Predictions in [predicted.json](predicted.json), written before the run.

## om4: rt_omm_dyn, micro-maps on the masked per-draw BLAS (docs/P4_OMM.md 11.2e)
`om4-camp-omm-dyn-fg2` (plan om4_camp_dyn.json, 123 s, 3 PNG): startup as om1 plus `rt_vt_alpha_masks=1`,
`rt_alpha_fine_lod=1`, `rt_omm_dyn=1` (B on throughout). `warmup-dyn` 25 s bakes the micro-maps (131072 triangles
per build), then the om1 phases: `diag-check` (rt_stats=1: `omm check` violations 0 is the gate, candidates
accelerated vs reference the work removed), REF / ON / REF cost + shot via rt_omm_reference (no rebuild: the
reference TLAS disables the micro-maps per instance). Coverage: the `[omm dyn]` line in every cost window (masked
per-draw triangles with a micro-map, known micro-triangle states). Cost: `rt passes: GPU cost`, `gi: GPU cost`
(local shadows, reflections), cost-on vs the cost-ref mean; the uv check and the accelerated TLAS build are paid
in REF and ON alike (`[rt] scene build GPU`). Energy: shot-on vs the shot-ref mean within |ref-b - ref-a|.

Duration about 7.2 min (dry run estimate), disk about 0.26 GB (4 traces); delete the trace folders after reading.
Offline check: `powershell -File tools\live_batches\omm_qual\dryrun.ps1 [-Out <scratch dir>]` (2026-11-02: all
checks passed).
