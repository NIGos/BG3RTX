## Water reflection flicker: rt_water_rf_edge screen-edge fade; water_flicker wf2 job (branch worktree-agent-a81baa03b972b117a, READY_FOR_LIVE, 2026-11-02)
- Cause in code (shaders/rt/water_compose.hlsl): the compose switches each water pixel per frame between the traced
  term (tier 1: C + A L - B) and the probe (C) with no weight and no temporal smoothing. A L and B share the weight E,
  not the radiance (bins 29-31 log L / probe). Under a turn the screen border moves every frame, so hits crossing it
  flip pixels between two radiances. D-RF fades at the border (rf_edge); the water passes did not.
- Fix: key `rt_water_rf_edge` (0, live): WATER_RF_EDGE compose variants (7 requests + cascade twins in variants.txt),
  C' = C + w (A L - B), w = D-RF's edge fade (rf_edge 0.05 uv). Key off: 19/19 water requests byte-identical to main
  (tools/shader_identity.py); key on: 14/14 compile and pass spirv-val. Build + 96 CTests pass.
- Job wf2-s8-fg2 (replaces wf1): A/B of rt_water_rr_hitdist and rt_water_rf_edge in one launch. All 4 turns in the same
  direction from the same pose (ret-* turns back between them); every key toggles 9 s before its burst (a plan step's
  set applies at capture_at: wf1's rot-on burst started at its toggle); rt_debug 21 coverage traces at the static pose
  and in the turn as masks (new `pan_artifacts --mask`, coverage_mask unit test). 199 s, 5 bursts (about 11.5 GB,
  deleted after the analysis). Dryrun + lints pass. predicted.json: shot-rr and shot-edge energy null, cost 0;
  rot-edge masked transient rate below both rot-off by more than the OFF-OFF spread.
- Water energy: the gate's dump 5c3fdabc runs BEFORE the forward water pass, so every M5 energy gate was blind.
  The plain after-water dump (423e0548) is dominated by the water FS's sun glints (max up to 1584; a measured-only
  key moved it +2.5%): m5w3's "compose -2%" was that noise, in the water pass's dump before the compose ran.
  New `energy_fs` "water-compose" (tools/m4_promotion_gate.py): 5c3fdabc + the in-frame compose delta (next
  scene-image dump minus the water pass's own dump, frame.json order). OFF-OFF 0.01%. Frame deltas: reflections
  -0.12% / -0.19%, sun shadows -9.7%, local shadows -2.0%, refraction -0.7%. Verdict: physically right, no
  bug (analysis: PLAN_M5_WATER M5.3c). `--energy-fs` re-reads old runs; wf2 uses it.
- Open: the hidden-tier switch (tier 2) is still binary; BG3's orbit camera moves the eye, so it can flicker too.
