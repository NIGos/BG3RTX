## Ray tracing pipeline GI trace, game path (branch claude/gi-rt-pipeline, READY_FOR_LIVE, 2026-11-08)
- What: the Ultra hit-GI trace (gi_trace_rt_hit_sun[_em].hlsl without SHaRC) can run as a ray generation shader
  over the same grid and descriptors (docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"). Keys gi_rt_pipeline
  (Performance) and gi_rt_pipeline_reorder (Advanced; 1 hit kind, 2 kind + instance): live, off in every preset,
  dev-only knobs until qualified. ABI 28 -> 29: rebuild both DLLs and restart the game.
- Layer, about 175 lines: layer/rt_pipeline.cpp enables VK_KHR_ray_tracing_pipeline + VK_EXT_ray_tracing_invocation_reorder
  only when both are supported. It never overrides an application struct and is dropped first on a device creation
  failure. The alpha and emissive set layouts name the raygen stage; layer/shaders.cpp drops -E for lib profiles.
- ABI, about 20: Bg3HostApi::rt_pipeline (handle size, alignments, reorder mode) and 3 VkDeviceFuncs entry points.
- Core, about 185: refresh(raygen) and build_pipeline (ray tracing pipeline, one general group), raygen-only
  pipeline layouts on the compute set layouts, a 64-slot SBT, vkCmdTraceRaysKHR with full barriers. It falls back
  to compute on any missing piece; choose_path logs each reason once. Reorder is constant 1050 (no prepass at 0).
- Pure logic, about 100, with tests/rt_pipeline_test.cpp: common/rt_pipeline.h covers the feature decision, the SBT
  layout and alignment, and the compute fallback decision.
- Settings, about 50: bg3rtx.ini, 2 knobs, generated defaults, settings_schema.json. Tooling, about 40:
  shader_variant.h kRtPipeline (raygen_entry, profile), shader_identity.py lib profile without -E, frame_replay
  --ab-rgen <file> adds GI_TRACE_RAYGEN=1, and RT_SPEC_CASCADE replaces a pack's RT_CASCADE_MASKS.
- Shaders: variants.txt +4 requests (hit_sun, hit_sun_em, each with and without OMM; GI_TRACE_RAYGEN=1, lib_6_6),
  4/4 compiled and spirv-val clean. The other 221 requests are byte-identical to main (shader_identity --baseline main).
- Seed gate on the shipped request (compute vs raygen, reorder 0, 32 draws per set, rt_stats sweeps 16), PASS on
  every channel:
  - camp: energy r -1.8e-5 (bound 4.8e-5), alpha +8.2e-5 (1.1e-4); alpha pixels 1.59e-4 vs control 1.36e-4
    (bound 1.66e-4); counters 0 of 67 fail;
  - inn: r -2.8e-5 (7.1e-5), alpha +3.4e-5 (8.8e-5); pixels 1.24e-4 vs 1.27e-4; counters 1 of 66 (limit 1.4);
  - S8: r -7.3e-6 (1.7e-5), alpha +1.5e-5 (9.6e-5); pixels 9.6e-5 vs 9.3e-5; counters 0 of 68.
  No emission (not in the packs). The inn sun-trust is now CTY_Main_A, the correct level for The Blushing Mermaid.
- Live job: tools/live_batches/gi_rt_pipeline (grp1-camp, grp2-inn; visible_unfocused_fg, one launch each, 104 s,
  0/1/0/1 with exposure held, trace pass GPU ms, energy from dumps.json). live_flow lint passes; predicted.json
  committed (camp -2.5 ms, inn -0.6 ms, energy 0). NOT queued, NOT authorized.
- Tests: build.ps1 -Test 209/212. release_install_test and release_setup_test fail only through this shell's
  PowerShell 7 PSModulePath (Get-FileHash missing) and pass with the machine path; batch_jobs_lint_test fails on main already
  (rp-camp / rp-lowercity patterns missing from the frozen snapshot). The new jobs' regex and readiness entries were
  added with lint_snapshot.extend_readiness: [0, null], text new in this branch.
- Only a live can answer: the emission variant, the real-frame cost, FG/Streamline with the extra device features,
  the Script Extender, the OMM raygen request (Ultra has OMM on), validation-clean binding at the RT bind point.

## Live 2026-11-08 (grp2-inn VALID, grp1-camp not run)
- Inn (The Blushing Mermaid - 96h 39m, Ultra, FG on, window unfocused), gi_rt_pipeline 0/1/0/1 in one run:
  trace pass 12.09 / 11.88 ms OFF vs 10.03 / 9.86 ms ON: -1.95 and -2.13 ms (OFF spread 0.21). Whole GI -1.5 / -3.5 ms,
  rt passes -1.7 / -2.1 ms. Offline predicted -0.6 (the pack's view is lighter; the live frame runs the emission variant).
- The emission variant runs as a ray tracing pipeline; device features and the reorder extension are on; FG/Streamline
  status 0x0, no device loss, no compute fallback beyond the first build.
- First build of the raygen pipeline took about 60 s (compute meanwhile): fine as a fallback, but a cold start cost.
- Energy (pre-post scene colour, m4_promotion_gate): ON vs mean OFF -0.32% and -0.35%; OFF-OFF spread 0.27% (one pair).
  Gate band within; the old OFF-pair rule calls it DARKER. Not resolved with one OFF pair: needs more pairs.
- Camp not run: "Campsite - 27h" no longer exists; the job now uses "Test Campsite" (not run).
- Status: key stays off. Open: the energy question (more OFF/ON pairs), camp, the 60 s first build (warm the pipeline
  at load), reorder=1 in game, launcher page.
