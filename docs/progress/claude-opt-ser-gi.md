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
- Correction (user, 2026-11-08): another game was running on the GPU during these runs, and it crashed too.
  The timing (+0.31 ms raygen) and the hint-on device loss are CONFOUNDED: the verdict is "not proven", not
  "dropped". Still valid: support check, hint-off output equality, compile/spirv-val. Rerun hint-off on an idle
  GPU first (that path ran without errors); hint-on only with the uniform-control-flow fix and the user's OK.
- Rerun on idle GPU (2026-11-08, util 5-6%, hint off only, claude/opt-ser-gi 278e5b6d): raygen vs compute at 3072^2
  +0.045 ms [-0.207, +0.298] on 1.36 ms (no difference resolved; the earlier +0.31 ms was GPU sharing); fixed
  ~35 us per launch at 64^2 (bit-equal). The raygen port costs nothing measurable; any SER gain needs the hint path,
  still untested (fix: reorder at uniform control flow), and a divergent fixture (this one has one material).
- Hint ON, fixed (2026-11-08, claude/opt-ser-gi dfa47ce6): reorder at uniform control flow (both rays traced first);
  validation layers (GPU-assisted + sync) clean, no device loss, output identical to hint off. 3072^2 vs compute on
  an idle GPU: lit fixture +0.31 ms (hint off) / +0.64 ms (hint on, noisy); divergent stripe fixture +0.27 ms
  (hint off) / +0.015 ms [-0.064, +0.047] (hint on). The hint only recovers the RT-pipeline port cost. NOT EVALUATED ON GAME DATA: the fixtures have one material and no sun or far-field, so they cannot show a game gain. Next step: replay packs from the game (camp, city or dialogue), then SER on the production far+sun variant. Capture deferred by the user on 2026-11-08.
  Reopen only with a game-representative divergent fixture (replay pack) showing a gain.
