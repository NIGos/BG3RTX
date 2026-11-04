# BG3RTX progress - 2026-11-04 night

## User questions and decisions

- Promote rr_post_temporal to Ultra on the user's visual judgement? Keep it off pending that decision.
  Dialogue3 paired evidence is inconclusive: 12 sign tests p 0.17-0.83; energy +0.15% unproven; cost 0.153 ms.
- Choose the project licence and whether to notify NVIDIA for the beta. No notification has been sent.
- Decide game-data redistribution, SHaRC distribution form and Microsoft runtime terms before release.
  SDK notices do not grant game-data rights. Static CRT is selected; no VC runtime installer/DLL is shipped.
- Complete manual launcher checks: Apply/restart badges, keyboard/focus, WebView2 zoom, DPI, high contrast and Narrator.
- Open appearance choices: S8 water sun-shadow edge/glint in shade; ambient ratio 1.44; rt_hit_emission 1 or 2.
  Ultra keeps emission 1. Mode 2 remains developer only.

## Night report - 10 lines

- Bundles 12-15 merged; rendering main 755e41b6. The folding commit is fb0dd1ca; ABI remains 26.
- smoke_ultra VALID twice: cascade buffer full 0.0 in every window; OMM 1.23 G rays, 0 violations.
- Water Fresnel participates in game: coverage-weighted owned 6,914,793, invalid 0; later wsf1 VALID, analysis pending.
- Camp pak join: 22,686 unique candidate observations, reported all Engine_Default_HLOD_VT; legitimate publication 0%.
- Hair load device loss fixed in bundle15 lives, including hv5 reloads; strands still never draw.
- Camera prepass sees an older groom publish; probe refused 300/300. Probe/publish-order fix is in progress.
- Hair conversion: 877/1,217 pass; all companion styles except Gale beard LOD1 are reported converted.
- Tooling includes fail-fast, 15-minute cap, measured-FPS windows and cached-aware/hotkey-key readiness waits.
- Tooling also includes REFUSED/CRASH rows, frozen lint statistics and crash monitoring; readiness is not a quality pass.
- Water glint, M5.6 step 1, rejected offline. Keep the branch for evidence only.

## Evidence boundaries

- [Night archive](archive/PROGRESS_20261104_night.md) preserves the entire 495-line folded file verbatim.
- [Handoff](HANDOFF_SESSION.md) gives the next queue; [GOAL_NEXT](GOAL_NEXT.md) defines stages 2-7.
- The task supplies later night updates absent from the folded file: bundle13-15 outcomes, both smoke runs,
  later VALID wsf1, bundle15 hair survival, 300/300 refusals, companion completion and glint rejection.
- Treat those as supplied session updates, not independently checked log receipts in this consolidation.
- Main's folding subject confirms rendering main 755e41b6. This checkout remains fb0dd1ca.
- Folded Fresnel evidence is partial/INVALID wsf1 031444, not the later VALID run; its analysis is still required.
- The folded pak audit proves 22,686/22,686 culled observations, not 22,686 distinct meshes.
  It identifies HLOD_VT for count 39,408 and lacks complete attribution for other buckets.
  The all-HLOD_VT attribution above is the supplied update; do not infer a publication defect from it.
- Later verdicts supersede stale crash warnings, conversion counts and READY_FOR_LIVE labels in the archive.

## Stage 2 - Shader specialization: done

- Migration merged; scc1 VALID, 0 DXC compiles on converted toggles.
- Requests 2,946 -> 147 -> 135 before later layouts. These are historical snapshots, not today's total.
- Bundle 6 gate: 262/262 requests, 28 hit-shade interfaces, 0 failures; full-resolution identity 243/243.
- New on/off keys use specialization constants unless layouts/bindings change. Retired IDs stay reserved.
- Open: first-use pipeline continuity/toggle latency. Readiness repairs do not prove rendering quality.

## Stage 3 - Visible defects: fixes kept, water qualification open

- Dialogue cascade identity matches 16-18 duplicates/frame, formerly 0; moving shadow band fixed by default.
- User accepts AO self-occlusion: 50% of character pixels; 99% of self-hits below 5 cm.
- Water camera correction looks right to the user; offline static camera delta is 0.
- wf9 still needs valid static delta < 0.01 m, nonzero samples and held Q/E coverage/energy evidence.
- Exploratory compose cost +0.009 ms is not qualification. Reflection edge/hit-distance keys remain off.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa. Do not restore their old jobs.

## Stage 4 - Qualification: integrated smoke kept; hair and temporal open

- Ultra qualifies rt_omm, rt_omm_dyn, rt_vt_alpha_masks, rt_alpha_fine_lod and ls_box=2.
- ls_box_honour_shadow=1. Two later smoke_ultra runs are reported VALID with zero cascade overflow.
- Earlier OMM camp: 1.17 G rays, 0 violations; RT 8.65-8.75 vs 9.37-10.08 ms. Later smoke: 1.23 G rays.
- Box S6: signed energy delta 0.0 within noise; unshadowed traces 369 M -> 0; local lights +0.13 ms.
- Pool alpha S6 coverage 14.8% -> 52.1%; GI trace +0.46 ms. Wind RGB S8 45.0% -> 63.5%; +0.67 ms.
- Mask rows: S8 +0.65 coverage points/+0.006 ms; inn +12.1 points/+0.92 ms.
- Earlier secondary grooms cost about 40% less than native cards. This does not qualify primary strands.
- Primary strands remain off in presets: no successful draws yet. Current blocker is probe/publish order.
- rr_post_temporal stays off/Advanced. VALID 032335 has 10 pairs; all 12 primary sign tests miss significance.
- Exact p range 0.171875-0.828125 vs 0.00416667; presented registration rejects 9/20, face support 0.
- Paired float energy +0.1492% fails; background join fails. Cost 0.152-0.153 ms; promotion is unproven.
- User sees steadier DoF. Any visual-judgement promotion must be explicit; S8 quality is still missing.
- Rejected: CPU perf keys, rr_hair_resolve, hair GI culling (+9-20% energy). No new queue for these.
- IP grass stays off: +3 coverage points costs +1.9 ms and about +1.5 GB.
- Generated-row publication/recovery and GI reprojection guards still lack complete valid qualification.

## Stage 5 - Effects and performance: experimental candidates stay off

- Ultra keeps water + Refraction, hidden-surface shading + Bounce, rt_hit_emission=1 and gi_far=6 m.
- gi_far: S8 energy -2.01%, camp -2.42%; trace +3.2/+3.7 ms. Ultra retains two GI rays.
- Fresnel uses native foam coverage c plus clean BRDF (1-c), with finite/domain/partition checks.
- Folded partial wsf1: weighted ownership 6,914,793; invalid 0; static compose 0.433 -> 0.458 ms.
- The +0.025 ms is partial compose cost, not whole-water cost. Analyze the later VALID wsf1 before qualification.
- Check S8 pool rim/interface-column partition. A shading-model change is judged against the PT reference.
- M5.6 step 1 water glint is reported rejected offline; retain evidence, do not queue it.
- Camp pak join proves 0 additional publication: all 22,686 unique observations fail recipe/cull.
- Unique observations are 5.655% of attempts and 3.762% of draws. Raw UV matches alone cannot publish.
- Need a compatible Tileset/HLOD-mask scene, e.g. S6, with full witness/resource/recipe/cull/basis gates.
- Compact v4: 40,983 rows/40.01 MiB. Native precedence remains mandatory; rt_hit_material_paks stays off.
- Water sparse-hit reflection 2.107 -> 0.530 ms, repeat 2.491 -> 0.531 ms; energy/coverage not qualified.
- wo1 must prove post-water float energy within OFF noise, preserved coverage and zero truncation in every ON.
- rt_compact_hit_state, rt_alpha_metadata_cache and rt_water_empty_tiles remain experimental/off.
- Offline replay is 36/36 byte-exact; empty-tile compose saving ~0.156 ms is exploratory only.
- Low/Medium rt_trace_scale=0.5 lacks quality gates; High/Ultra stays 1.0. Medium RT saving ~0.18 ms is exploratory.
- SHaRC stays off: no PT superiority/cache-history evidence. Hit-GI AO needs a pack with instanced hits.
- Animated lava, HeightGradient, HLOD sidedness, texture publication and AS fragmentation remain open.

## Stage 6 - Tooling and release: repairs kept; acceptance open

- Fail-fast, preflight lint, 15-minute launch cap, measured-FPS windows and session fault invalidation are in place.
- Readiness handles cached/no-op keys and hotkey keys; every new affected build still needs exact completion.
- Five cached-wait replays pass: hv6 25 ms, hv5 79 ms, wsf1 66 ms, cp4 86 ms, Dialogue3 57 ms.
- Frozen lint corpus: 223 KB derived from 90 VALID runs; preflight still uses live corpus.
- Folded checks: 147/147 CTests, 26/26 active lints, 6/6 combined dryruns. These are offline receipts.
- REFUSED/CRASH rows and crash monitor are reported added; verify runner behaviour on real use without fault injection.
- GUI fixes cover 13 review findings; settings 4,356 checks and bridge 904 passed. Manual acceptance remains open.
- Version-matched DXC/Vulkan notices are resolved: five texts retained verbatim; packaging tests 3/3 pass.
- Project licence, NVIDIA notification, data rights, Microsoft terms and SHaRC distribution remain release decisions.
- Disposable-VM installer, long sessions, acts 2-3 and other GPUs remain open. No release approval is implied.

## Stage 7 - Backlog and cleanup

- Follow the handoff queue before remaining READY_FOR_LIVE jobs; no speculative research ahead of the hair fix.
- Open branch: water-glint, evidence only. Earlier open-branch lists in the archive are historical.
- Audit rejected keys/generated-row supersession, inn material records, BLAS relocation and M5.5 steps 2-4.
- Keep current replay/gate inputs; remove worktrees only with the dedicated junction-safe script.
- This consolidation performs no cleanup, live testing, code/shader edits, commits or git metadata writes.

## RTX hair milestones M0-M8

- [Complete hair plan](RTX_HAIR_COMPLETE_PLAN.md) defines the scope; its milestone table predates later night updates.
- M0 stability: bundle15 lives, including hv5 reloads, reportedly survive. Primary participation is still blocked.
  Probe refuses older publishes 300/300; fix probe/publish order before hv6, hv5 and hair_crowd confirmation.
- M1 conversion: 877/1,217 pass, confirmed as the coverage-bound cohort; companion completion is a supplied update.
  Converter 2.4-3.4x faster, renders 1.8x, bit-identical; 80-style representative set, GR2/groom caches.
  LOD0-to-LOD1 transfer rejected by coverage/IoU gates. Gale beard LOD1 remains unresolved.
- M2 shadows: transmittance offline verified, rt_hair_groom_shadow_t off; shadow-map consistency and AO open.
  GPU bench: 3,000/3,000 crossing counts match; transmission error 1.25 sigma. L2 live/cost still missing.
- M3 image: dithered tips offline verified, off; 1,600 samples/alpha give exact expected shares.
  Sub-pixel coverage, RR guides, DoF/fog/water depth and full temporal/energy quality remain open.
- M4 indirect: fibre-GI offline candidate; white furnace 1.0, coloured errors <= 1.3882%, GPU/CPU 1.46e-7.
  Full suite was 145/146 with inherited water identity failure; no complete acceptance or live/GPU cost claim.
  Specular indirect, many-light fibre sampling and strand radiance-cache integration remain open.
- M5 views: portraits, dialogue/small passes, instanced hair, reflections and water-hit fibre shading remain open.
- M6 performance: half-resolution traces and crowd budgets remain unqualified; no measured primary strand cost.
  SER does not apply to these inline queries. Native-card cost is not a strand budget receipt.
- M7 content: 877-style cohort exists; all companions except Gale beard LOD1 reported converted; dyes need live A/B.
- M8 qualification: not done. Need visible strands, every-window gates, cost/VRAM, real Ultra smoke and user check.
  Keep unqualified keys off in presets; run hv4 GPU-AV after the participation and L2 prerequisites.
