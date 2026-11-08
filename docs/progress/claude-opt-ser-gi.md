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

## SER prototype: reorder hint at uniform control flow (branch claude/opt-ser-gi, offline, 2026-11-08)
- Fix: gi_ser_prepass traces the first two GI rays up front (same setup, random stream and query), combines their
  hints (7 bits each: 0 miss/unknown, 1 on screen, 2 + instance) and issues one OpReorderThreadWithHintEXT (14 bits)
  before any thread returns; the loop then reads those hits. SPV_EXT_shader_invocation_reorder only restricts the
  instruction to RayGenerationKHR (the hint form needs no hit object; no control-flow rule is stated).
  spirv-val passes; compute requests 221/221 byte-identical to main 11d14456.
- 64x64 with the Khronos validation layer plus GPU-assisted and sync validation: hint OFF and hint ON, no validation
  error, no device loss, outputs and counters bit-equal to compute.
- 3072^2 lit fixture (3.4% hits), 40 ABBA/BAAB pairs each vs compute: raygen OFF +0.307 ms median [+0.247, +0.314]
  (the up-front traces cost about 0.25 ms more than the in-loop version above), hint ON +0.638 [+0.165, +1.535]
  (clock-noisy). Hint ON output = hint OFF output, 0 differing components.
- Striped fixture (--synth-stripes, synthetic: one material quad behind 2-pixel strips, neighbouring threads alternate
  miss / qualified hidden hit, 37% hits): compute median 0.979 ms; raygen OFF +0.266 [+0.155, +0.386];
  raygen + hint +0.015 [-0.064, +0.047] (mean -0.175 [-0.424, +0.074]). Outputs: hint ON = hint OFF exactly; vs
  compute 258 of 9.4M half components differ by 1 ulp (stage codegen); counters equal.
- Reading: the hint recovers the raygen port's cost on divergent work but does not beat compute here. Not
  representative (one material, no sun/far; the production far + sun variant with groupshared stats is not ported;
  the game path needs the layer: RT pipeline feature, entry points, ABI). Recommendation: park SER.
- Status on main (2026-11-08): verdict NOT EVALUATED ON GAME DATA (fixtures have one material, no sun or far-field);
  next step is the production far+sun variant on game replay packs (below).
