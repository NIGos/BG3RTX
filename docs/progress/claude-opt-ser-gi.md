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

## SER on game frames: production far + sun trace (branch claude/opt-ser-gi, offline, 2026-11-08)
- Setup: packs camp 013545-2, inn 013914-3, S8 142819-1 (test-main captures, read-only). Production Ultra compute
  module gi_trace_rt_hit_sun.hlsl (RT_SPEC_NATIVE, RT_SPEC_HIT_INST, RT_SPEC_GI_FAR; the pack's RT_CASCADE_MASKS=1
  on camp/inn instead of RT_SPEC_CASCADE, which cannot combine with it), specs 1003=1 1004=1 1007=1 1008=0 1010=0
  1020=0 1026=0 1029=1 1038=1 1044=0, --sun-trust (pt_roofs levels WLD_Main_A / SCL_Main_A / CTY_Main_A), push
  radius 6, rays 2, rt_stats 0, --ab-warmup 8, 40 ABBA/BAAB pairs. No emission (the packs have no emissive set).
  Every run guarded by nvidia-smi (3-6% idle, desktop apps only) and a process check: no game, no DXC; all clean.
- Baseline (same-module control, ms): camp 8.64 (delta +0.009 [-0.112, +0.079]), inn 2.32 (+0.100 [-0.164, +0.218]),
  S8 3.12 (+0.082 [-0.010, +0.198]); outputs and counters 100% bit-equal. Inn and S8 absolute ms move 2.2-3.8 between
  runs (clocks at this low load); only paired deltas are compared.
- Port: tools/gi_trace_rgen_sun.hlsl = the far + sun module as a raygen; the far counters' group-shared reduction is a
  per-wave sum into the first lane's 8x8-group bin (totals equal compute; bins may move). rt_stats=0 path unchanged.
  Hint 1050=1 kind + instance (14 bits), 1050=2 kind only (4 bits); GI_SER_PREPASS_RAYS=0u = raygen without prepass.
  spirv-val passes; validation layers: no error except the replay's missing set 1 b19 (tileset sampler, both
  pipelines, unused at 1008=0). No device loss. Compute requests: 30/30 gi_trace_rt* byte-identical to main.
- NOT bit-equal on game data (the --ab-require-equal gate fails): raygen vs compute differ on camp 380866 of 2.73M
  pixels (14%), S8 1.3%; the old hit_direct port too (fixture: 37 components). Each pipeline is deterministic (new
  same-pipeline check: 0 of 40 repeats changed); channel means equal within 0.04%, rt_stats=1 counter totals within
  0.04% (rays equal). The hint changes outputs too (camp 376222 pixels vs hint off), which the fixture never showed:
  some result depends on the lane/wave placement of a thread. Cause not found (no wave op or missing
  NonUniformResourceIndex outside stats; candidates: tie order of equal-t candidates, an undefined read).
- Raygen vs production compute, median delta [95% CI] ms, 40 pairs each:
  - camp: no prepass -2.577 [-2.826, -2.394]; prepass, hint off -2.501 [-2.678, -2.386];
    hint kind + instance -3.205 [-3.298, -3.030]; hint kind -3.210 [-3.356, -3.100].
  - inn: -0.611 [-0.666, -0.440]; -0.181 [-0.312, -0.016]; -0.241 [-0.309, -0.107]; -0.160 [-0.283, -0.094].
  - S8: -0.278 [-0.353, -0.147]; -0.045 [-0.216, +0.065]; -0.120 [-0.285, +0.000]; -0.202 [-0.260, -0.141].
- Reading: the ray tracing stage itself is faster than the compute dispatch on all three packs (camp 30%), with no
  reorder. SER adds about -0.7 ms on camp (foliage, divergent) over the prepass, little on S8, and the prepass costs
  0.1-0.4 ms (inn: no prepass -0.61 beats every hint variant). The driver reports no ray tracing pipeline statistics,
  so the stage's register / occupancy difference cannot be read (compute: 243 registers, 44 B shared).
- Cost breakdown of the compute trace (paired, ms of A in the same run): far on/off 1007: camp -5.75 of 8.66 (66%),
  inn -2.51 of 3.78, S8 -2.50 of 3.79; hidden-hit shading (hit_direct vs gi_trace_rt base, far off): camp -1.05 of
  2.43, inn -0.34 of 1.36, S8 -0.94 of 1.65; sun (sun vs direct module, far off): camp -0.31, inn +0.04 (noise),
  S8 -0.21 [-0.33, +0.004]; roof early 1038 off: camp -0.05 (noise), inn +0.82, S8 +0.92 (it saves that much);
  compact hit state 1029 off: noise on all. Rays 1 (unpaired runs): camp 4.6 vs 8.6 ms. Far-field rays dominate.
- Verdict by the rule (hint-on CI below the control's lower bound on 2 of 3 packs): met by the kind hint (camp,
  S8), not by kind + instance (camp only). Not productised: the gain is mostly the stage, not reordering, the outputs
  are not bit-equal, and a game path needs the layer's ray tracing pipeline (feature, entry points, ABI).
- Next: find the lane-dependent result (it may affect the compute pass as well); then a raygen GI trace without
  prepass (largest, simplest win), with the reorder hint only as an option for divergent scenes.
- Receipts: worktree .scratch/ser/<pack>-{self,rgen*,bd-*}/ (run.log, ab.csv, guards).

## Raygen vs compute mismatch: cause (branch claude/opt-ser-gi, offline, 2026-11-08)
- Not lane-dependent. Compute is a pure function of the pixel: GI_DEBUG_SWIZZLE (new offline define,
  gi_trace_rt.hlsl:290, id ^= k) remaps pixels to other lanes (k=1), the other wave of the 8x8 group (4) or
  another group (8, 9). Production far + sun module vs remapped: 100% bit-equal outputs and counters on camp
  (k 1, 4, 8), inn (9) and S8 (4). Production is not affected (no flicker or noise from the thread mapping).
- Cause: the driver compiles the same arithmetic differently for the compute and the ray generation stage. A temporary
  debug dump of ray 0's inputs on camp, before any ray query: the pixel position P (ray_at(uv) * d,
  gi_common.hlsli:132) differs in the last bits on 54% of pixels, the normal N (decode_normal +
  to_lighting_space, gi_common.hlsli:116-127) on 30%, the ray direction on 79%; rtOrigin (a texture read) on 0%.
  The SPIR-V is the same in both modules (no RelaxedPrecision; the same execution modes). Then the hits on foliage
  amplify these last-bit differences into different leaves (camp: 92541 hit/miss flips of 2.73M rays; S8 1.3%).
  - FMA contraction: DXC -Gis (NoContraction on every operation) makes P equal and cuts the camp mismatch from 380866
    to 83825 pixels.
  - Approximate operations: even with -Gis, dot(dn, dn) after the 2.0 / t division in decode_normal, rsqrt and 1 / x
    give a different last bit in the two stages (Vulkan allows 2.5 ulp for OpFDiv and leaves sqrt / rsqrt / sin
    / cos implementation-defined, so the driver may pick a different sequence per pipeline).
  The hint-on output change is most likely the same thing: the reorder instruction changes the raygen code the driver
  emits (inferred, not tested separately).
- No small shader fix makes the stages bit-equal: it would need correctly rounded division, sqrt, rsqrt, normalize
  and sin/cos written out on the whole GI and shading path, and that would change the compute output too. Gate
  (hint-off raygen vs compute, --ab-require-equal, three packs): FAILS, not fixable in the shader. Means stay
  equal within 0.04%, counter totals within 0.04%: a different noise draw, the same expected value.
- Consequence: a raygen GI trace cannot pass a bit-equality gate against compute. It needs a statistical gate instead
  (energy on a static camera, PT reference), or it should replace the compute pass outright and be gated against
  itself. The debug dump block is kept out of the tree (.scratch/ser/debug_ray0_block.hlsl). Compute requests:
  170/170 gi/ byte-identical to main.
