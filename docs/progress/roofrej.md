## Early roof rejection restored (branch roofrej, prepared READY_FOR_LIVE, 2026-11-05)

- Restored gi_far_roof_early, raw developer key default 0, SpecId 1038, request delta 0; release forces OFF. Identity stays removed.
- IDs 1035/1036 stay retired. The host forces 1038=0 whenever rt_stats != 0, retaining the original cached executable and diagnostics.
- Same original-origin/first-padded-roof/t > RB proof, after emission; finite, subnormal and unknown fallbacks remain conservative.
- Exactness PASS: S8/camp/inn outputs and every raw counter bin 100% bit-equal at rt_stats 0 and 1.
- Statistics off: 40 ON/OFF ABBA/BAAB pairs each. Statistics on: original main vs specialization 0 and host key ON/OFF 0/0, 2 pairs each.
- RTX 5090, sun trust, radius 6 m, two rays; ON-OFF median [95% median interval], ms:

| Pack | rt_stats=0, 40 pairs |
|---|---|
| S8 | -1.734 [-2.734, -1.398] |
| Camp | +0.495 [+0.103, +1.696] |
| Inn | -1.361 [-2.429, -0.798] |

- The 40-pair camp +0.495 [+0.103, +1.696] loss was not reproduced by the stability check below; no reproducible camp loss established.
- Camp identical OFF/OFF control, 40 pairs: +0.128 [-0.821, +0.323] ms, exact. All requested candidate results retained.
- Registers unchanged: compact state 0, near 168 / far 255; production compact state 1, near 168 / far 243. ON/OFF match main.
- Key-off release equivalence vs main 9b38f38c: all 215 requests / 2,048 assignments pass, negative control passes.
- scripts/build.ps1 -Test PASS: 192/192 CTests, 169.53 s. Updated build-free roof_jobs test also passes all four cases.
- tools/live_batches/opt_r6/roof: copied compact_hit profile/schedule, inn + S8 Ultra, dev=1, OFF/ON/OFF/ON 600 frames, fresh QUIET.
- Qualified rt_compact_hit_state=1 fixed throughout. GI trace decides; all six timers retained; JSON/stdout contain preceding-OFF pair deltas.
- pipeline_readiness TABLE uses [GI_TRACE]; snapshot extends six queries only, preserving all original receipts/models. Both lints pass 2/2.
- Main checkout .scratch/roofrej1038/summary.json: commands, modules, CSVs, raw counters, lints, timing/control receipts. Original packs unchanged.

- Camp stability check: three 80-pair A/B runs, each preceded by its own 80-pair OFF/OFF control; idle GPU, no games:

| Run | ON-OFF median [95% CI], ms | OFF/OFF control |
|---|---|---|
| 1 | -0.005904 [-0.143744, +0.059024] | +0.008064 [-0.132944, +0.245616] |
| 2 | -0.110912 [-0.211504, +0.079696] | +0.022808 [-0.174240, +0.104528] |
| 3 | +0.100592 [-0.012960, +0.200400] | +0.000448 [-0.076752, +0.093728] |

- Pooled 240 ON-OFF pairs: +0.005064 [-0.060240, +0.059024] ms; all outputs/raw bins exact.
- Count: 0 early rejections / 492,480 guard evaluations / 5,463,040 rays. Temporary count view only; production unchanged.
- If needed, test origin containment before direction rotation/slabs. Host zero-roof gate alone cannot help camp (15 volumes).
- Fresh receipts are worktree-local .scratch/roofrej-camp-repeat/summary.json; prior receipts remain at the main-checkout path above.
- Coordinator decision: camp settled with 3 x 80 pairs: pooled +0.005 [-0.060, +0.059] ms, early path taken 0 of
  492,480 guard evaluations. Candidate passes (S8 -1.73, inn -1.36, camp neutral); live confirmation next.
