## GI optimisation round 1 (branch opt-gi, candidates rejected, 2026-11-05)

- Cost attribution: the enclosing GI timer includes pre work (origin, trace, temporal,
  AO fold, sun) and post work (local shadows, reflections, composite, water and optional
  diagnostics). Hit direct/sun/emission/far bounce run inside the trace dispatch.
  The reported 4.3-7.8 ms difference is not an isolated GI filtering measurement;
  do not subtract independently aggregated summaries to assign pass costs.
- Added detailed_logging-only GPU timers: origin, temporal, AO fold, sun trace,
  sun resolve, contact and composite. Means per successfully dispatched frame;
  transitions/barriers excluded, asynchronous readback, reset at rt_stats_reset.
  GPU ms remain UNMEASURED. Rendering unchanged; shader request delta 0.
- Roof early rejection: dropped because all five available production replay packs
  use gi_hit_direct without emission set 4 or the temporal/composite chain. Thus
  complete Ultra ON output equality cannot be proved. Audit estimate 0.30-1.50 ms
  remains unverified, not a saving. No retained key or live job.
- Temporal current-sample path: dropped because no production temporal replay fixture
  covers stored formats, history/depth/motion and downstream AO/composite. Cached
  SPIR-V uses FMix; algebra is insufficient proof. Estimated 0.03-0.12 ms is unverified
  and does not establish the 0.05 ms minimum. No retained key or live job.
- Other hit skips: zero local-light terms/back-facing geometry already skip visibility
  (native_hit_direct.hlsli:214,218); sun guards precede visibility (:73-75);
  AO is contribution-gated. Emission traces a separate TLAS, so the ordinary material
  cannot safely identify a zero-emission interval. No safe additional candidate.
- Cached Ultra sun/emission module: 71,319 SPIR-V instructions, 1,230,984 bytes;
  sun-only 69,102 instructions. No driver spill/register/occupancy conclusion.
- Validation: gpu_logging_gate_test 5/5 passed; final core Release rebuild and
  focused CTest passed (1/1).
  Full build is incompatible with the 20 MB cap (SDK copies 48-59 MB); core-only
  build -Test attempted all 175 CTests: FAILED (exit 8), with missing unbuilt
  executable fixtures. No full-suite pass claimed. No shader changes, so
  release_shader_equivalence and settings regeneration are unnecessary.
- No commit, game launch, staged queue job or live evidence. To inspect the new costs,
  use detailed_logging=1 and the existing stats period/reset controls in a separately
  authorised profile; no optimisation A/B is ready from this branch.
