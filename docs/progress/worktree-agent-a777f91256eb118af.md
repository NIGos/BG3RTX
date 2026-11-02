## Live read of gi_far, box_lights, em_forge (branch worktree-agent-a777f91256eb118af, analysis + job fix, 2026-11-02)
- gi_far (gf1 camp 01:54, gf2 S8 01:57; valid, exposure held, ratio 1.0000). Energy, scene colour / s1b13[0]:
  camp ON +0.17% (band 2.81%, no prediction); S8 ON -0.10% vs predicted -2.0% (band 1.38%): gap persists, gate
  fails. Cost (GPU trace pass, ON - mean OFF): S8 1.09 -> 4.35 ms (+3.26, OFF spread 0.003), camp 3.43 -> 6.90 ms
  (+3.47). The S8 frame time is confounded by the 01:57-01:59 build; GPU ms only.
- S8 stages: folded visibility (gi_debug=3, d1669194) ON / OFF 0.9998 (0.17643 -> 0.17640; predicted 0.948).
  The same run's `GI far` line says 0.840 vs old rule 0.887 per ray. The far rule changes the counted rays but
  not the visibility the fold reads: the gap is between giRaw and the fold, not in the AO x E lighting model.
  The only ON evidence of the rule is from rt_stats=1 frames (diag-on); every image window has rt_stats=0.
  The offline emulation that matched the line used `--define GI_FAR_M=6` (gi_trace_rt.hlsl:181-183), not the live
  Push::radius path (gi_trace_rt.hlsl:185, core/gi.cpp:2408-2411). Next (offline, game closed): replay the
  hit_sun RT_GI_FAR variant with push radius 6 and rt_stats 0 vs 1, compare giRaw.a.
- The `view bounce` stage (gi_debug=2) is not usable: d1669194 max R = 65024 (R11G11B10 saturated), mean
  (2.49, 2e-5, 7e-6): a few overflowed red pixels set the mean. Its 0.870 ratio means nothing.
- box_lights ls_box=2 (bx1 S6 02:01, bx2 S7 02:05; valid). Cost, ls pass ON - mean OFF: S6 0.449 -> 0.772 ms
  (+0.32, slightly above the expected 0.15-0.3), S7 0.736 -> 0.936 ms (+0.20). Energy within the band: S6 -0.54 /
  -1.35% (band 2.00%, OFF spread 1.39%), S7 +0.10 / -0.13% (band 1.66%).
- S6 mode 2: unshadowed boxes traced 369M rays / 120 frames, 18.0% occluded, removed 5.45% of the lit luminance;
  shadow-mapped part 0.165% only. Near-plane cut 1.96M rays, but "occluded where the atlas term is 1" did not
  fall (211k mode 1 -> 211k mode 2, already 6.7% of the shadow-mapped occluded rays in mode 1, not the lr3
  ~100%): the near-plane half has no measurable effect here. S7: 26.5% of 84M unshadowed rays occluded, removed
  0.48%. Bound: `at or above the upper neighbour` 0.026% px (S6) and 0.16-0.18% (S7), the same in mode 1 and 2
  (not a mode-2 over-estimate); inconclusive 0.
- S6 PNGs: ON darkens a band on the lower-right walkway (planks right of the front disc, in shadow of the raised
  ledge) and the central pillar; view-removed shows the removed light as long strips on that walkway. User check:
  S6 save, lower-right walkway and the pillar, ls_box 0/2. S7: no change beyond moving characters; view-removed
  is stippled on the floor around the stage and under the balcony rail (noisy 1-ray visibility for RR).
- em_forge ef1 (02:09) INVALID only through the job: the runner prints `[motion-trace] phase=N` only for phases with
  trace_at (scripts/auto-run.ps1:694-699); check-off/full/sliced have none. Fix: expect the `[pan-plan] window 7/8/9`
  set lines instead (all match the 02:09 runner.txt). lean_dryrun.ps1 now fails any expect_runner motion-trace line
  of a phase without trace_at (fails on the old job, all 17 lean dryruns pass).
- em_forge gate from the same log: check-sliced 0.016 ms per run median, max 0.026 ms (< 1 ms: pass), 0.017 ms/frame;
  check-full 0.113 ms per run, 0.007 ms/frame; gi: GPU cost check-off 2.770-2.786, sliced 2.778-2.837 ms (within
  the off spread + 0.017). A rerun is needed only for a formally valid receipt.
