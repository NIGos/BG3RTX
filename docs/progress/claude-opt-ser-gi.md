## SER prototype for the GI trace (branch claude/opt-ser-gi, offline, stopped: no gain, hint crashes, 2026-11-08)
- Support: RTX 5090 driver 621.14 exposes VK_EXT_ray_tracing_invocation_reorder rev 2 (rayTracingInvocationReorder,
  hint mode REORDER) and VK_NV_ray_tracing_invocation_reorder rev 1. The shipped DXC (SDK 1.9.0.5399) has no
  dx::HitObject/MaybeReorderThread for SPIR-V, but vk::ext_instruction OpReorderThreadWithHintEXT (5314) at lib_6_6
  compiles and passes spirv-val (3 s for the hit_direct raygen).
- Prototype: GI_TRACE_RAYGEN in gi/gi_trace_rt.hlsl (raygen entry over the same grid; one hint per GI ray after the
  trace: 0 miss, 1 on screen, 2 + instance), spec 1050 kSpecIdGiSer (0 off). Offline only: tools/gi_trace_rgen.hlsl
  (hit_direct base; far/sun variants use groupshared stats and need more work), frame_replay --ab-rgen <hlsl>
  --rgen-spec 1050=V (B = ray tracing pipeline, one raygen group, vkCmdTraceRaysKHR), --synth-size N.
  Compute path untouched: 221/221 requests byte-identical to main b4f3cd99; shader requests delta 0.
- Fixture (synthetic lit pack at 3072x3072, 2.36M threads, 2 rays, gi_rt_radius 6, 3.4% hits, one material; not
  representative of the game): raygen with hint OFF vs compute, 40 ABBA/BAAB pairs: +0.313 ms [+0.193, +0.476]
  (compute 0.73, raygen 1.02 ms mean). Outputs: 37 of 9.4M half components differ by 1 ulp (driver codegen of the
  two stages), means equal to 7 digits; counter totals equal (only wave-leader bins move).
- Hint ON: device lost on the first dispatch (64x64 and 3072 packs); the PC restarted right after. Not rerun.
  Likely cause: OpReorderThreadWithHintEXT inside the per-ray loop after divergent early returns; an untested fix
  is a reorder at uniform control flow with the hint computed for both rays first.
- Not done, by design: no game path (the layer enables no ray tracing pipeline feature and vk_funcs.h has no RT
  entry points: an ABI change in the layer, owned by another stream), no live job. Recommendation: drop SER for
  now; the raygen path alone is ~40% slower on the fixture and the hint path is unsafe on this driver.

## SER prototype: rerun on idle GPU, hint off only (branch claude/opt-ser-gi, offline, 2026-11-08)
- The first fixture runs shared the GPU with another game (that game crashed too), so their timing and the hint-on
  device loss are confounded. Rerun with nvidia-smi at 5-6% (desktop apps only), no game process, hint off only
  (spec 1050 = 0); hint on was not run. Sources: this branch's shaders, exported (the worktree was on opt-bundle2).
- 64x64 (128^2 pack, validation on): compute 0.0017 ms, raygen 0.0374 ms; delta mean +0.0356 ms, 95% CI
  [+0.0353, +0.0359] (40 ABBA/BAAB pairs): about 35 us of fixed ray tracing launch cost. Outputs and counters
  100% bit-equal.
- 3072x3072 (2 rays, gi_rt_radius 6, 3.4% hits): compute mean 1.363 ms, raygen 1.409 ms; delta mean +0.045 ms,
  95% CI [-0.207, +0.298] (median delta +0.075, CI [-0.048, +0.202]); repeats spread 0.35-4.08 ms (clocks), so
  no difference is resolved. Outputs: 37 of 9.4M half components differ by 1 ulp, channel means equal to 7 digits;
  counter totals equal.
- Reading: the earlier +0.31 ms was the shared GPU. The raygen port alone costs nothing measurable beyond ~35 us
  per launch; any SER gain still depends on the hint path, which stays untested. Fixture not representative.
