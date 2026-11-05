## Far GI live state (branch farregs, rejected, 2026-11-05)
- Offline only. No game, queue, git writes or settings-lane edits. SpecId 1039 retired.
- Production Ultra sun/emission request, native/hit-inst/cascade/far; 1029=1, 1038=1:
  main near/far 168/243 registers. Roof-before-shading 240; plus fade rematerialization 239;
  plus lane-owned shared far stats 218 (2816 extra shared bytes). No requested occupancy step.
- Roof test retained one veto bit instead of the ray origin across hidden lighting; cheap base/far
  fades rematerialized at use. Far stats used lane-owned uint words with the original wave/group sums.
  Same query rays, FP32 math and radiance accumulation order; no approximate shading or fp16.
- Sun-trust S8/camp/inn, radius 6, two rays: each candidate has 40 ABBA/BAAB pairs per pack
  at rt_stats=0 and 1 (480 total pairs). Every stored output byte and raw counter bin matched.
- Roof/fade ON-OFF ms, 95% median intervals (S8 / camp / inn):
  stats 0: +0.439400 [-0.312720,+1.328608] / -0.176096 [-0.547168,+0.132832] /
  +0.353320 [+0.138256,+0.608576].
  stats 1: +0.437216 [-0.692576,+1.184160] / +0.398328 [-1.223680,+1.360672] /
  +0.001008 [-0.396512,+0.845216].
- Shared-stats ON-OFF ms (same pack order):
  stats 0: +0.958704 [+0.634592,+1.955568] / +0.131816 [-0.058944,+0.956672] /
  +0.093280 [-0.173152,+0.250624].
  stats 1: +0.667000 [+0.009856,+2.411616] / +0.052376 [-1.687568,+0.513840] /
  -0.537464 [-1.212224,+0.103904].
- Rejected: neither reaches 168/128 registers or a consistent timing win; inn first and S8 second lose.
  All candidate shader code removed; no developer setting ships. Request delta 0; ABI unchanged.
- Final release equivalence vs main: 215/215 byte-identical, negative control PASS.
- BelowNormal build.ps1 -Test PASS: 193/193 CTests (build 53 s, tests 164 s).
- Receipts: worktree .scratch/farregs/{summary.json,regs-*.log,main-far*.log,*stats*.log,
  release-equivalence.json,build.log}; candidate source/SPIR-V and paired CSVs retained there.
