# gi_rt_pipeline 0 / 1 / 0 / 1 (live_batch gi-rt-pipeline): READY_FOR_LIVE, NOT authorized

Qualifies `gi_rt_pipeline` (docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"). The Ultra hit-GI trace (far + sun +
emission) runs as a ray tracing pipeline instead of a compute pass. Lean format: one visible-unfocused launch per save
(FG 2, `window_mode` visible_unfocused_fg, the game never takes focus), A/B by toggling the key inside the run, static
camera, exposure held from `cost-off-a` to `shot-on-b`. `make_jobs.py` writes the plans and jobs; lint with
`py -3 tools/live_batches/live_flow.py tools/live_batches/gi_rt_pipeline` (passes, 2026-11-08). Not in any pending
inbox; queue only after the user's go. The test tree must hold the claude/gi-rt-pipeline build: ABI 29, so restart the
game with the new layer and core.

| Job | Save | Plan | Predicted (predicted.json, committed before any run) |
|---|---|---|---|
| `grp1-camp` | Test Campsite (Campsite - 27h is gone since the reinstall) | 104 s, 4 PNG traces | trace pass -2.5 ms ON vs OFF; energy unchanged |
| `grp2-inn` | The Blushing Mermaid - 96h 39m | 104 s, 4 PNG traces | trace pass -0.6 ms; energy unchanged |

Phases:
- `warmup` 20 s (key off, wait for a quiet driver).
- `warmup-on` 10 s: the raygen pipeline builds in the background and the compute pass runs meanwhile. The phase waits
  for `gi: gi_rt_pipeline: the hit-GI trace runs as a ray tracing pipeline` and a quiet driver.
- Then `cost-off-a`, `cost-on-a`, `cost-off-b`, `cost-on-b`, 12 s each, each followed by a `shot-*` of 6 s with one
  trace at 4 s.
- `restore` 2 s.

## Gates
- Validity: every `expect` line, among them `[rt pipeline] on the device ... reorder REORDER`, the raygen request's
  `GI_TRACE_RAYGEN=1 generation` line, `runs as a ray tracing pipeline, emission variant` (Ultra; `sun variant` if
  D-EM has no fresh build) and `runs as compute: key off`. `expect_every` needs a `gi: trace pass` line and a
  0-timeout scene wait in every cost window. A forbid on any compute fallback for a missing feature, entry point or
  SBT, on a raygen pipeline creation failure, and on device loss.
- GPU: the `gi: trace pass ... GPU median` of each cost window, ON (on-a, on-b) minus OFF (off-a, off-b). The lines of
  the same state must agree within their own spread before the delta counts.
- Energy: the float mean of the pre-post scene colour (pass 5c3fdabc, slot 0, each trace's `dumps.json`; never PNGs)
  divided by the trace frame's s1b13[0]. Each ON shot is compared with its OFF neighbours against |OFF-b - OFF-a|
  (`py -3 -m tools.m4_promotion_gate <log>`). Predicted unchanged: offline, the seed gate on the exact shipped request
  passes on camp, inn and S8.
- Only a live run can answer: the emission variant (the replay packs have no emissive set), FG/Streamline with the
  extra device features, the Script Extender with the new entry points, OMM traversal (Ultra has OMM on, so the
  RT_OMM_SCENE raygen request runs; the packs replay without OMM), and the real-frame cost.
