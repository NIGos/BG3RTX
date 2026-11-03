# BG3RTX progress - 2026-11-03 evening

## User questions and decisions

- Open: decisions in GUI_AUDIT_20261102; project licence; NVIDIA notification for the beta.
- Open: SHaRC distribution form, Larian data terms, and obtaining missing DXC/Vulkan-Headers notices.
- Open look: soften the S8 water sun-shadow edge or keep native glint suppression in shade?
- Open, not blocking: rt_hit_emission 1 or 2? Ultra keeps 1; 2 remains developer only.
- Lives are paused by the user until testing is fixed. A fresh batch instruction is required to resume.

## Report 2026-11-03

- Bundles 6, 7 and 8 merged through merge-green; rendering main is eca2023b, ABI 26.
- Testing repairs are on main; their first live use still needs validation. No live is authorized here.
- Dialogue shadow fix and Ultra OMM/box qualification stand; the latest combined smoke was INVALID.
- Strand live enable still risks device loss. Keep it off until the pending hair-reload-fix lands.
- User reports much steadier DoF with rr_post_temporal; promotion waits for valid Dialogue3 evidence.
- S8 temporal cost is about 0.12 ms; sparse water-hit reflection cost fell about 2.1 -> 0.53 ms.
- Medium rt_trace_scale=0.5 saved about 0.18 ms RT; these exploratory costs are not quality passes.
- Pak discovery is fixed; camp participation needs live evidence. Water Fresnel is READY_FOR_LIVE.
- Hair batch converted 519/1,217 styles; 698 remain under study. SHaRC is inconclusive and stays off.
- Resume with crash fix, hv5, smoke, Fresnel, pak join, Dialogue3 and hit-cost quality, in that order.

## Evidence and reading order

- [Full folded progress, verbatim](archive/PROGRESS_20261103_evening.md) preserves all prior entries.
- [Handoff](HANDOFF_SESSION.md) gives the queue; [GOAL_NEXT](GOAL_NEXT.md) defines the stages.
- [18:56 evidence](../tools/live_batches/combined_20261103/RESULT_WATER_185612.md) supplies the exploratory costs.
- That combined run had all six slices INVALID. Repairs do not retroactively validate its captures.
- Current user steering adds the stale-soup crash diagnosis and DoF appearance report, absent from the folded text.
- The stale soup address follows elastic capture shrink; its fix is pending on claude/hair-reload-fix.
- The local branch ref does not yet expose that new fix. Earlier reload safeguards are already merged.
- Later verdicts here supersede stale READY_FOR_LIVE and unmeasured labels in the archive.

## Stage 2 - Shader specialization: done

- Migration merged and scc1 valid: 0 DXC compiles on converted toggles.
- Requests fell 2,946 -> 147 before null-key cleanup, then 147 -> 135.
- Later half-trace layouts added 18 requests; these are separate manifest snapshots, not current totals.
- Bundle 6 final shader gate: 262/262 requests, 28 hit-shade interfaces, 0 failures.
- Full-resolution identity: 243/243 requests byte-identical to the bundle baseline.
- Qualified infrastructure needs no new live job. New on/off keys use specialization constants unless layouts change.
- Open: first-use pipeline continuity and toggle latency need live confirmation; no blanket latency pass.

## Stage 3 - Visible defects: fixes implemented, water qualification open

- Dialogue cascade identity matches 16-18 duplicates/frame, formerly 0; moving shadow band fixed by default.
- Motion-pair band is within RT-sun-off noise. Check Test Dialogue3 while the camera moves.
- User accepts AO self-occlusion: 50% of character pixels, 99% self-hits below 5 cm. No correction planned.
- Rejected: rt_cascade_static_only drops every caster. Keep it off.
- rt_water_compose_shift uses matching camera fields; offline static delta is 0. User says it looks right.
- READY_FOR_LIVE: tools/live_batches/water_flicker_wf9/jobs/wf9-s8-compose-shift.json.
- Require static average camera delta < 0.01 m, nonzero samples, corrected coverage and held Q/E windows.
- Exploratory corrected compose cost is about +0.009 ms; reflection residual and energy remain unqualified.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa. Do not restore their old jobs.
- Open: reflection-only rt_water_rf_edge and rt_water_rr_hitdist remain unknown/off.
- Whole-water glint scores cannot qualify reflection quality. Never compare pans from separate runs.

## Stage 4 - Qualification: partly done; hair safety blocks resumption

- Ultra qualifies rt_omm, rt_omm_dyn, rt_vt_alpha_masks, rt_alpha_fine_lod and ls_box=2.
- ls_box_honour_shadow stays 1. Earlier integrated smoke passed; fresh smoke must follow the crash fix.
- OMM camp: 1.17 billion rays, 0 untested, 0 violations; RT 8.65-8.75 vs 9.37-10.08 ms.
- Box S6: signed energy difference 0.0 within noise; unshadowed traces 369 million -> 0 per 120 frames.
- Box local-light pass 0.45 -> 0.58 ms (+0.13 ms). Inspect S6 Grymforge and camp art lights.
- Pool alpha rows: S6 qualified coverage 14.8% -> 52.1%; energy +1.06%/-0.38%; GI trace +0.46 ms.
- Wind RGB: S8 coverage 45.0% -> 63.5%, energy -0.00%, trace +0.67 ms; kept in Ultra.
- Mask rows: S8 +0.65 coverage points/+0.006 ms; inn +12.1 points/+0.92 ms.
- Converted secondary grooms work in game, about 40% cheaper than native cards in prior trials.
- Dialogue1: RT 9.07 vs cards 10.2-10.57 ms; GI 12.51 vs 14.1-14.3 ms; refit +0.57 ms.
- Native cards stay off: +2.3-6.3 ms RT. GI culling was rejected for brightening by +9-20%.
- Batch: 519 pass, 553 review and 145 reject out of 1,217; 698 unresolved = 553 + 145.
- Strand primary/fibre/crowd are not qualified. Do not enable strand hair before the pending crash fix lands.
- Blocked live regression: tools/live_batches/hair_validation/jobs/hv5-reload-bisect.json, Test Dialogue1.
- Then smoke: tools/live_batches/smoke_ultra/jobs/su1-ultra-s8-fg2.json with the real qualified preset.
- rr_post_temporal is READY_FOR_LIVE/off; user sees steadier DoF, not yet a promotion gate.
- Exact next job: tools/live_batches/post_temporal/jobs/pt-dialogue3.json, Ultra 0/1/0/1.
- S8 temporal reports 0.127/0.119 ms (~0.12 ms); its shot had no DoF composite and cannot qualify Dialogue3.
- History is about 250 MiB at 2560 x 1067. Both ON windows need hair-over-DoF quality and float energy gates.
- Rejected by user: rr_hair_resolve and CPU perf keys. No further qualification queue for them.
- IP grass stays off: only +3 coverage points for +1.9 ms and about +1.5 GB; no promotion.
- READY_FOR_LIVE generated rows: tools/live_batches/m4_generated/jobs/hgn1-generated-camp-fg2.json
  and tools/live_batches/m4_generated/jobs/hgn2-generated-inn-fg2.json; publication race fixed, recovery unmeasured.
- GI guards remain pending in tools/live_batches/qual_stage4/jobs/01-camp.json and tools/live_batches/qual_stage4/jobs/02-inn.json.
- Open: hair runtime mask/MV/fresh/FG/PT evidence and cohort coverage remain incomplete; dryruns cannot qualify hair.

## Stage 5 - Effects and performance: active, candidates remain experimental

- Ultra keeps water + Refraction, hidden-surface shading + Bounce, rt_hit_emission=1 and gi_far=6 m.
- gi_far energy: S8 -2.01% vs -2.0% prediction, camp -2.42%; trace cost +3.2/+3.7 ms respectively.
- Earlier water chain cost 0.53-0.67 ms; hidden-hit emission +0.08 ms. These are historical scene costs.
- rt_water_hit_shade remains off/experimental; retained lighting must have proven lifetime for F-1 use.
- rt_water_hit_cost bits: 1 sparse, 2 cone/zero, 4 slot order, 7 combined. All remain off in presets.
- Sparse reflection median 2.107 -> 0.530 ms; repeat 2.491 -> 0.531 ms. Energy/coverage gates did not pass.
- Combined reflection about 2.1 -> 0.529 ms; compose about 3.5 -> 1.0 ms. No equivalence claim.
- Bits 2 and 4 show no consistent independent saving. Keep the same-run per-bit comparisons.
- READY_FOR_LIVE quality job: tools/live_batches/water_offscreen/jobs/wo1-s8-hit-shade.json.
- Require post-water float energy within OFF-repeat noise, preserved coverage and zero world truncation in every ON.
- rt_water_surface_fresnel is READY_FOR_LIVE/off; restart to provision, then live A/B in S8.
- Exact job: tools/live_batches/water_surface_fresnel/jobs/wsf1-s8-interface-column.json.
- Offline: 144/144 CTests; 2/2 ordinary raw requests identical, 16/16 compose arithmetic-OFF comparisons identical.
- Inspect shallow pool-rim reflection and interface/column partition. Shading-model brightness may change; cost unmeasured.
- Pak discovery fixed: class-A keyed draws now participate; rt_hit_material_paks stays off pending camp live evidence.
- Exact job: tools/live_batches/cov_paks_compact/jobs/cp4-compact-camp-join.json after a layer restart.
- Compact v4 has 40,983 rows/40.01 MiB. Predict draws delta 36,720-36,960 per 120 frames; UV attempts must be nonzero.
- Full witness/recipe/resource/cull/basis gates remain mandatory; retained camp has 58 unique UV candidates, 0 full witnesses.
- The old LSLib generation blocker is superseded by compact catalogue validation; publication/texture-view gaps remain.
- rt_trace_scale=0.5 is configured for Low/Medium; High/Ultra retain 1.0. Live quality is not qualified.
- Medium exploratory RT 1.040/0.864/1.057 ms: about -0.18 ms; shot count failed, energy evidence incomplete.
- READY_FOR_LIVE: tools/live_batches/gpu_opt2/jobs/go2-s8-medium.json and tools/live_batches/gpu_opt2/jobs/go2-camp-high.json.
- Exact GPU candidates stay off: rt_compact_hit_state, rt_alpha_metadata_cache, rt_water_empty_tiles.
- Offline replay 36/36 byte-exact comparisons. Empty tiles saved about 0.156 ms compose exploratorily; no live equivalence pass.
- READY_FOR_LIVE: tools/live_batches/gpu_opt1/jobs/go1-camp.json and tools/live_batches/gpu_opt1/jobs/go2-s8.json; retain cutouts/colour.
- rf_hit_shade remains experimental: +1.4-1.8 ms; rf_hit_gi needs a pack with instanced hits for its AO gap.
- SHaRC is inconclusive: no cache history/PT superiority evidence; sharc=0 remains. No promotion job.
- Historical SHaRC energy -0.86% vs +1.5% prediction is INCOMPLETE; missing dumps prevent requalification.
- One GI ray saved 4.32 -> 2.02 ms with +0.11% energy, but flicker null failed. Ultra retains two rays.
- Open defects: animated lava, Tileset colour, HeightGradient height, HLOD two-sided, fibre scattering and AS fragmentation.

## Stage 6 - Tooling and release: offline repairs merged, acceptance open

- Main contains fail-fast, preflight lint, 15-minute cap and durations derived from cadence/readback/settle time.
- Main also contains combine rebasing, session fault invalidation and interrupted merge-green recovery.
- Post-core water energy and published-stride coverage capture fixes are merged; restart and validate on first use.
- Failed shader requests no longer enter the prefetch manifest. Preserve real failure gates.
- Latest duration repair passed 146/146 CTests; first live use must confirm duration and fail-fast behaviour.
- Do not reuse oversized combined drafts. Re-lint actual jobs and inspect waits/loading before authorization.
- Launcher fixes cover preset reset, prerequisites, Apply badges, focus and contrast; bridge passed 695 checks.
- Open acceptance: real WebView2 zoom/high contrast/Narrator, disposable-VM installer, long sessions, acts 2-3, other GPUs.
- Packaging/licence decisions remain open. Game data is generated locally; historical packaging receipts are not approval.

## Stage 7 - Backlog and cleanup: ordered, not a rendering pass

- Preserve pending hair-cards-retire, anim-materials and claude/hair-reload-fix work; see the handoff dependencies.
- Audit rejected keys, generated-row supersession and remaining READY_FOR_LIVE jobs after the ordered queue.
- Keep S8 ambient-ratio 1.44 question, inn records, dynamic/cascade BLAS relocation and M5.5 follow-ups open.
- Retain current replay/gate inputs; cleanup must respect shared junctions. No cleanup is performed by this consolidation.
- This edit archives the full folded text and replaces only progress/handoff. No code, shader, game or Git metadata writes.
