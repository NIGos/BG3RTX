# Meta-audit 2026-11-01 (main db75b54)

This audit was read-only: no code, builds, GPU tools or game. It repeats the 10-31 audit ([META_AUDIT_20261031.md](META_AUDIT_20261031.md)). Sources: the 11-01 queue logs (up to 09:42), PLAN_M4 "Promotion", PLAN_M5_WATER, PT_FEASIBILITY_OFFLINE, TRACK_D_REFLECTIONS 14 and TRACK_D_LOCAL_LIGHTS 9.11.

Section 7, the gate audit (every live gate replayed on the logs; 70 gates, 30 OK / 28 weak / 12 broken before the fixes): [GATE_AUDIT_20261101.md](GATE_AUDIT_20261101.md).

## 1. Status of the 10-31 items
- **Water, partly.** M5.1-M5.3 are live at S8.
  - Compose cost fixed: 8.171 ms median (m5w3 083416, 297k records) became 0.096 ms (093518). Every frame composes now (112-120 of 120).
  - S1 docks never measured: QuickSave_3784 loads the foundry.
  - M5.4 shadows: offline only (job `m5w4` not run).
- **Dither walls, closed by user decision, not fixed.** Faded walls keep casting shadows. Q14 (dither hash in any-hit) and a solid-discard counter were not built.
- **Box lights, open.**
  - lr3 08:49:39 `ls box stats`: game box light is 37.9% of lit luminance. Only 0.009 box lights per pixel are traced, and all of them are occluded.
  - `projector validation px 0`: the accuracy gate has no samples.
  - The 3,329 unshadowed box lights are unchanged.
- **Hit material, partly.** Qualified share: S8 48.1%, S6 27.9%, S14 22% to 40% (PLAN_M4). Reasons are logged; there is still no floor on the share.
- **Emission at hits, partly.** `rt_hit_emission` is built; S6 participation is 6.9% of lookups. The OFF/ON/OFF run is still owed.
- **Roofs, partly.** `rt_cascade_casters` is ON in Ultra. COVERAGE (73.8 per mille at S7) has not been re-measured.
- **Coverage tool stale, open.** `scripts/qa/coverage.py:44` still reads "main, 2026-10-27". S2 and S10-S13 were never captured.
- **cb2 retirement, open.** `tools/live_batches/consolidation/jobs/cb2-scene-wait-rotation-inn-fg2.json:73` still calls `rebase_gate`.
- **GI R3 "0 negative px", open.** It is still the `gi_hit_direct` gate in PLAN_M4, and it cannot fail.
- **M4.1 motion, open.** No moving run and no isolated cost: +0.52 ms is `gi_hit_direct` and `gi_hit_sun` together.
- **M4.2 z gate, replaced.** It fails at both scenes and became "bias within 9%". That is now a user question.
- **`ls_check`, open.** lr3 08:49:26: 28.3M of 306M pixels (9.3%) differ by more than 1e-5 relative. The threshold is still not derived.
- **Done:** pool/soup counters (`rt epoch ... pool hit match`); AO at hits (`hit_ao_rays=2`, S8 hit AO 0.771 vs screen 0.775); local diffuse probes at hits; sun roof rule at hits (`NATIVE_HIT_SUN_ROOF`).
- **Projector lights at hits, partly.** `rf_hit_shade` evaluates them; `gi_hit_direct` skips them (`NATIVE_HIT_PROJECTOR_SKIP`).
- **Fog and off-frustum lights at hits, open.**
- **"Before closing M4": all five open or partly.** Gates that can fail: partly. M4.1: open. M4.2 at a second scene: done (S6). Reason histogram as a share of tier-(b) pixels: not done. cb2 and coverage: open.

## 2. Decisions closed on weak evidence since 10-31
- **The wind clip fix's hypothesis is disproved by the live run** (m4i 093913 09:40:52, "rows with the derived clip tolerance 5"): wind `within the derived tolerance 0`, `over x256 22372`; 2S `differs 556432` (96% of the rejects); rejected 2.63%, then 3.14%, 0.39% and 0.00% in later windows, against a gate of < 0.04%. Qualified barely moved (6.5M OFF vs 6.4-7.2M ON) while attempted lookups fell from 29M to 16-19M: here the share gain is a denominator change (in 074646 it was real: 7.1M to 11.4-12.6M at ~31M attempted). Root cause found later (pairing across frames and instance order, branch 4925937).
- **One matching window passes a gate.** The m4i expect line is satisfied by the single 0.0000% window, although other ON windows show 2.6-3.1%. The m5w3 composed and GPU expect lines work the same way. Gates must hold in every ON window.
- **The PT far-field decision rests on a scene without roofs.** The pack ran with `rt_cascade_casters=0`; `tools/pt_reference.hlsl:17` does not model cut-away roofs, and S8 is about 95% under roofs. So "sun 98.9%", "64.2% of first rays miss" and "56-86% beyond 1 m" are probably inflated. One frame, fallback albedo 0.5, no sky, no native ambient; the in-domain 0.97 uses a k fitted on the same paths; per-region error 84%.
- **M5.1's violation gate was redefined after the run** ("behind opaque = violation" became "occluded water"): physically plausible, but the coverage check can no longer fail.
- **The water compose darkening was accepted on its own explanation** (ratio of the means 0.31; below 0.5 in 38-56% of composed pixels). The histogram cannot tell occlusion from a partial unit error; no reflection crop against a reference.
- **Water compose mostly uses the previous frame's lighting** (114-116 of 120 passes). No moving run shows this is harmless.
- **`rf_hit_shade` cost is unexplained:** +1.4 to +1.84 ms against a budget of 0.2-0.4 ms; no `hit_ao_rays` 0/2 split.
- **The promotion energy gate is one-sided:** "brighter with the step named" passes any explained brightening; the instanced +0.27% was accepted with no predicted delta.
- **Single- or few-scene gates:** `ls_box` S6 only; `ls_k` S7 only; `rf_hit_shade` S8 and S6 (camp and inn unmeasured); `gi_hit_sun` S3 never run.

## 3. Fallbacks and fail-closed paths whose share is not gated
- Water compose keeps the probe on 87.3% of owned pixels (15-24% for roughness; hidden and off-screen rays, 8.8%, have no tier (b)).
- Water FS patch: only the 3 audited FS families, only if the key was on at pipeline creation; other FLUID variants counted as "unknown".
- Hit material, S14 OFF: of 29M lookups, "row" 13.9M (48%) and "base" 8.6M (30%) unsupported. Instanced clip rejects fail closed.
- `rf_hit_shade` tier (b) shaded share: S8 24%, S6 3%; no floor.
- `rt_hit_emission`: 58,520 uncalibrated hits skipped at S6. `gi_hit_direct` drops the projector term (37.9% of S6's lit luminance); counted, not gated.
- Scene wait: m4i 093913 09:41:09, 224 of 600 frames used a stale scene (wait mean 11.3 ms); nothing gates it and cost windows run with it.
- Sun native-mask fallback: still no expect line.
- `ls_box` validation reports 0 samples, which looks like "0 error".
- Inn rotation: lr2 08:46:48 `pool hit match` 34-37% while rotating vs 99.6% static. PROGRESS says "~83%" and "pool not lagging": a different metric, the gap is not explained.

## 4. Physical correctness gaps, by measured impact
1. **Far-field indirect light:** 56-86% of the traced bounce energy at S8 beyond the 1 m GI ray. Largest gap, least certain number (no roofs, no materials, native ambient unknown).
2. **Box lights:** 37.9% of lit luminance at S6, essentially untraced (0.009/px); 3,329 unshadowed box lights light through walls; skipped at GI hits.
3. **Water:** visible water S8 9.67% of pixels, S1 89.2%. The game's water has no sun visibility at all (no cascade sample in the FS). Compose changes the mean by only -0.35 to -0.65%, because 87% stays on the probe.
4. **Hits:** `rf_hit_shade` bias -7.4% S8, +9.1% S6; hit-material coverage 28-48%; instanced rejects 2.6-3.1%; fog, off-frustum lights, GI projector lights not covered.
5. **Small:** faded walls casting shadows (by decision); bloom min-filter 3.6% darker under stochastic terms, -0.07% of the final image.

## 5. Process lessons
- **Host-visible atomics, three times** (m5w2 15 ms, m5w3 7.2-8.2 ms, the `rt_stats` ~80 ms/frame pattern). Fix the pattern: a CTest or lint failing when a shader atomic targets an `alloc_host_buffer` buffer; one shared device-local, group-reduced stats helper copied outside the timer.
- **CPU contention in live windows:** m4i 093913 frame medians 20-37 ms and scene-wait timeouts up to 37% of frames; the PT cost run under other GPU tests was ±2x. Proposal: a lock that pauses builds and GPU tests during a live run; CPU load logged per job.
- **Merge serialization:** since 11-01, 97 commits, 51 merges, 19 "Merge main"; conflict markers left in the AUDIT_SOTA archive (fixed in 3c6f314).
- **PROGRESS conflicts and staleness:** 28 commits touched PROGRESS_CODEX.md; 304 lines against ~150; stale main commit ids; stale entries ("How to test" cites S6 1.48; Milestones "M5.1 in progress"). Proposal: one owner writes PROGRESS; branches write a fragment file.
- **Disproved hypotheses go unrecorded:** a live result that disproves a fix must be written up within the batch.

## 6. Next steps, ranked
1. **M4 promotion.** Re-run m4i with the pairing fix and an every-window gate, fixed-view qualified counts; keep `rt_hit_material_instanced` out of the bundle until it passes. Then `m4_promotion` (4 launches, ~18 min) with a two-sided per-step energy gate and the predicted delta written before the run, capacity at 1024/427 MB, and `hit_ao_rays` 0/2 inside the cost window to explain `rf_hit_shade`'s +1.4-1.8 ms. Separately: `gi_hit_sun` at S3 and one M4.1 moving run with isolated GI trace ms. `rf_hit_shade` promotion stays the user's call.
2. **M5 shadows and transparency.** Run `m5w4` (S8): occluded > 0 in every ON window, removed light only where `rt_debug=26` is red, cost < 0.05 ms. One moving m5w3 window (previous lighting on 114-116 of 120). A reflection crop against a reference to settle ratio 0.31. A save with open sea for S1. Then `rt_water_hit_shade` (tier (b) for 8.8% of rays). M5.5 transmission only after the medium is mapped.
3. **M6 step 1.** Re-capture the PT pack with `rt_cascade_casters=1` or add the roof-volume test to the PT's sun rays; the VT-cache pack (`replay_pack_cap_mb`) and the native ambient/sky; one interior pack (S3 or S6). Only if far-field sun stays above ~30%: the far-field key (4-8 m GI rays + `gi_hit_sun` at hidden hits, native ambient removed only for hitting rays), off by default, gated against the reference in one run, ~1.8 ms at 4K Performance.
4. **Housekeeping.** Retire cb2; refresh the coverage HANDLING table and recapture S1-S14; trim PROGRESS to ~150 lines and fix its stale lines; derive the `ls_check` threshold.
