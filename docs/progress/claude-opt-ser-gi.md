## SER / raygen GI trace on game frames (branch claude/opt-ser-gi, offline, 2026-11-08)
- Prototype: GI_TRACE_RAYGEN in gi/gi_trace_rt.hlsl (the hit-GI body as a raygen; far + sun port, far counters per
  wave), spec 1050 reorder hint (prepass of 2 rays, one OpReorderThreadWithHintEXT in uniform control flow).
  frame_replay --ab-rgen, --rgen-spec, same-pipeline determinism count, --seed-sweep. Fixtures: no gain (one material).
- Packs camp 013545-2, inn 013914-3, S8 142819-1, production far + sun compute without emission (no emissive set in
  the packs), 40 ABBA/BAAB pairs, idle GPU (every run guarded). Baseline camp 8.64 / inn 2.32 / S8 3.12 ms; self
  control [-0.11, +0.08] / [-0.16, +0.22] / [-0.01, +0.20].
- Raygen vs compute, median [95%] ms: no reorder camp -2.58 [-2.83, -2.39], inn -0.61 [-0.67, -0.44], S8 -0.28
  [-0.35, -0.15]. Kind hint: -3.21 / -0.16 / -0.20 (meets the 2-of-3 rule); kind + instance: camp only. The stage is
  the win; SER adds about -0.7 ms on camp only and the prepass costs 0.1-0.4 ms.
- Cost breakdown (compute, paired): far 1007 off -5.75 of 8.66 (camp), -2.5 (inn, S8); hidden-hit shading 1.05 / 0.34
  / 0.94; sun 0.31 / noise / 0.21; roof early 1038 saves 0.82 (inn) and 0.92 (S8); compact hit state: noise.
- Not bit-equal, cause found: not lane-dependent (compute output bit-equal under lane / wave / group remaps,
  GI_DEBUG_SWIZZLE, all packs). The driver compiles the same SPIR-V differently per stage: FMA contraction
  (DXC -Gis cuts camp 380866 -> 83825 px) and division / rsqrt sequences. Foliage amplifies last-bit ray
  differences. No small shader fix. Production unaffected.
- Statistical gate instead: tools/replay_seed_gate.py on frame_replay --seed-sweep (GI_SEED_PUSH offsets the random
  streams through Push::bounce_spec; thresholds from compute vs compute). Raygen without reorder PASSES on camp,
  inn and S8 (energy within 3 SE, per-pixel 4 sigma fraction, counters); far on/off negative control FAILS.
- Corrections: the inn sun-trust used level SCL_Main_A (the inn is The Blushing Mermaid, CTY_Main_A); the inn
  timings above ran with the wrong roof volumes (equivalence results stand, same inputs on both sides). The first
  per-pixel bound missed the control's own noise (sqrt 2); camp alpha passes only with the correct bound.
- Game path design: docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"; implemented on claude/gi-rt-pipeline.
- Receipts: worktree .scratch/ser/ (run.log, ab.csv, gate.json, guards). Codex not used.
